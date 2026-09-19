#!/usr/bin/env python3
"""Analyse vendor captures, including bounded serial-command trials.

Windows describe observed unchanged content, not proven firmware execution.
Queue-model matches are hypotheses, never automatic command attribution.
"""
import argparse
import json
from pathlib import Path

FIELDS = {"buttons": (9, 10), "ps": (8,), "left stick": (17, 19),
          "right stick": (21, 22), "triggers": (23, 24)}
EXERCISES = {"stick": "right stick", "buttons": "buttons", "triggers": "triggers",
             "left-stick": "left stick"}
APPLIES = {"trigger-resistance", "trigger-normal", "trigger-right-apply", "trigger-left-apply"}


def parse_capture(path):
    header, trials, current = {}, [], None
    pending_rows, pending_writes, pending_markers = [], [], []
    explicit, broken = False, False
    ignored = 0
    for line in Path(path).read_text().splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[:2] == ["#", "connection"]:
            header = dict(zip(parts[1::2], parts[2::2]))
        elif parts[:2] in (["#", "failed"], ["#", "aborted"]):
            broken = True
        elif parts[:2] == ["#", "trial"]:
            explicit = True
            if parts[3] == "begin":
                if current is not None:
                    broken = True
                current = {"id": int(parts[2]), "rows": [], "writes": [], "markers": [],
                           "complete": False}
                trials.append(current)
            elif parts[3] == "end":
                if current is None or current["id"] != int(parts[2]):
                    broken = True
                else:
                    current["complete"] = True
                current = None
        elif parts[:2] == ["#", "write"]:
            if len(parts) < 4:
                raise ValueError("incomplete write marker")
            event = {"us": int(parts[2]), "command": parts[3],
                     "ok": len(parts) >= 5 and parts[4] == "ok"}
            if "duration_us" in parts:
                event["duration_us"] = int(parts[parts.index("duration_us") + 1])
            if current is not None:
                current["writes"].append(event)
            elif not explicit:
                pending_writes.append(event)
        elif parts[:2] == ["#", "marker"]:
            marker = {"us": int(parts[2]), "name": parts[3]}
            if current is not None:
                current["markers"].append(marker)
            elif not explicit:
                pending_markers.append(marker)
        elif parts[0] != "#":
            raw = tuple(int(x, 16) for x in parts[1:])
            if len(raw) < 32 or raw[:2] != (4, 254):
                ignored += 1  # identity/replies are not input samples
                continue
            row = (int(parts[0]), raw)
            if current is not None:
                current["rows"].append(row)
            elif not explicit:
                pending_rows.append(row)
    if not explicit:
        trials = [{"id": 1, "rows": pending_rows, "writes": pending_writes,
                   "markers": pending_markers, "complete": True}]
    return header, trials, broken, ignored


def changes(rows, positions):
    values = [tuple(raw[p] for p in positions) for _, raw in rows]
    return sum(a != b for a, b in zip(values, values[1:]))


def analyse_trial(trial, required, quantum_us, threshold_us, broken=False):
    rows, writes = trial["rows"], trial["writes"]
    result = {"trial": trial["id"], "reports": len(rows), "writes": writes,
              "markers": trial["markers"], "status": "VOID", "reasons": [], "stalls": []}
    reasons = result["reasons"]
    if len(rows) < 2:
        reasons.append("too few input reports")
        return result
    if any(b[0] <= a[0] for a, b in zip(rows, rows[1:])):
        reasons.append("input timestamps are not strictly increasing")
        return result
    if broken or not trial["complete"] or any(not w["ok"] for w in writes):
        reasons.append("capture incomplete, aborted or write failed")
    first_write = min((w["us"] for w in writes), default=rows[0][0] + 500_000)
    before = [r for r in rows if r[0] < first_write]
    after = [r for r in rows if r[0] >= rows[-1][0] - 500_000]
    result["movement_checks"] = {}
    for field in required:
        positions = FIELDS.get(field)
        if positions is None:
            reasons.append("unknown required control: " + field)
            continue
        check = {"before": changes(before, positions), "after": changes(after, positions)}
        result["movement_checks"][field] = check
        if check["before"] < 2 or check["after"] < 2:
            reasons.append(field + ": need movement before commands and at capture end")
    spans = []
    lo = 0
    for hi in range(1, len(rows) + 1):
        if hi < len(rows) and rows[hi][1][21:23] == rows[lo][1][21:23]:
            continue
        end = rows[hi][0] if hi < len(rows) else rows[-1][0]
        duration = end - rows[lo][0]
        if duration >= threshold_us:
            spans.append({"start_us": rows[lo][0], "end_us": end,
                          "duration_us": duration, "reports": hi - lo,
                          "left_censored": lo == 0, "right_censored": hi == len(rows),
                          "fields_changing": [name for name, offsets in FIELDS.items()
                                              if changes(rows[lo:hi], offsets)]})
        lo = hi
    result["stalls"] = spans
    result["observed_frozen_us"] = sum(s["duration_us"] for s in spans)
    result["report_rate_hz"] = len(rows) * 1e6 / (rows[-1][0] - rows[0][0])
    censored = any(s["left_censored"] or s["right_censored"] for s in spans)
    if reasons:
        result["status"] = "VOID"
    elif censored:
        result["status"] = "INCONCLUSIVE"
        reasons.append("freeze boundary outside capture; reported duration is a lower bound")
    else:
        result["status"] = "OBSERVED_STALLS" if spans else "NO_OBSERVED_STALLS"
    applies = [w["us"] for w in writes if w["ok"] and w["command"] in APPLIES]
    if applies:
        serial_end = applies[0]
        for at in applies:
            serial_end = max(serial_end, at) + quantum_us
        result["predictions"] = {"quantum_us": quantum_us,
                                 "serial_end_us": serial_end,
                                 "arrival_timer_end_us": applies[-1] + quantum_us}
        # Report a candidate residual, not causal assignment. Background stalls
        # remain in the observed total and can invalidate this candidate match.
        if result["status"] == "OBSERVED_STALLS":
            candidate = spans[-1]["end_us"]
            result["predictions"].update(
                observed_last_end_us=candidate,
                serial_residual_us=candidate - serial_end,
                arrival_timer_residual_us=candidate - applies[-1] - quantum_us)
    return result


def analyse(path, quantum_ms=1073, stall_ms=250):
    header, trials, broken, ignored = parse_capture(path)
    required = [EXERCISES.get(x, x) for x in header.get("exercise", "stick").split(",")]
    if "right stick" not in required:
        required.append("right stick")
    return {"file": str(path), "metadata": header, "ignored_non_input_reports": ignored,
            "trials": [analyse_trial(t, required, quantum_ms * 1000, stall_ms * 1000, broken)
                       for t in trials]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--quantum-ms", type=int, default=1073)
    parser.add_argument("--stall-ms", type=int, default=250)
    args = parser.parse_args()
    if args.quantum_ms <= 0 or args.stall_ms <= 0:
        parser.error("durations must be positive")
    try:
        reports = [analyse(p, args.quantum_ms, args.stall_ms) for p in args.files]
    except (OSError, ValueError, IndexError) as exc:
        parser.exit(2, f"Invalid capture: {exc}\n")
    if args.json:
        print(json.dumps(reports, indent=2))
    else:
        for report in reports:
            print("==", report["file"])
            for t in report["trials"]:
                print(f"  trial {t['trial']}: {t['status']}; {t['reports']} reports; "
                      f"observed frozen {t.get('observed_frozen_us', 0) / 1000:.1f} ms")
                for reason in t["reasons"]:
                    print("   ", reason)
                for s in t["stalls"]:
                    suffix = " (censored)" if s["left_censored"] or s["right_censored"] else ""
                    print(f"    {s['start_us']/1e6:.3f} -> {s['end_us']/1e6:.3f}s: "
                          f"{s['duration_us']/1000:.1f}ms{suffix}")
                if "predictions" in t:
                    p = t["predictions"]
                    print(f"    hypotheses: serial end {p['serial_end_us']/1e6:.3f}s; "
                          f"arrival-timer end {p['arrival_timer_end_us']/1e6:.3f}s")
                    if "serial_residual_us" in p:
                        print(f"    last-end residual: serial {p['serial_residual_us']/1000:+.1f}ms; "
                              f"arrival-timer {p['arrival_timer_residual_us']/1000:+.1f}ms")
        print("Observed windows are not causal attribution. Compare repeated trials and idle controls.")
    return 5 if any(not r["trials"] or any(t["status"] in ("VOID", "INCONCLUSIVE")
                                         for t in r["trials"]) for r in reports) else 0


if __name__ == "__main__":
    raise SystemExit(main())
