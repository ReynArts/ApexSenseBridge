#!/usr/bin/env python3
"""Regression cases use synthetic input, never open a controller."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("capture", ROOT / "scripts/analyze-apex4-capture.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


def rows(end=3600, windows=(), offset=0):
    result = []
    for i in range(end):
        raw = [0] * 32
        raw[:2] = [4, 254]
        value = i % 200
        for n, (start, stop) in enumerate(windows):
            if start <= i < stop:
                value = 220 + n
        raw[21:23] = [value, value]
        result.append((offset + i * 1000, tuple(raw)))
    return result


def trial(data, at=(500000, 502000)):
    return {"id": 1, "rows": data, "writes": [
        {"us": t, "command": "trigger-right-apply", "ok": True} for t in at],
        "markers": [], "complete": True}


class CaptureTests(unittest.TestCase):
    def test_two_queued_stalls_are_not_background(self):
        result = capture.analyse_trial(trial(rows(windows=((500, 1573), (1573, 2646)))),
                                       ["right stick"], 1073000, 250000)
        self.assertEqual(result["status"], "OBSERVED_STALLS")
        self.assertEqual(result["observed_frozen_us"], 2146000)
        self.assertEqual(result["predictions"]["serial_residual_us"], 0)
        self.assertEqual(result["predictions"]["arrival_timer_residual_us"], 1071000)

    def test_restarted_timer_is_distinct(self):
        result = capture.analyse_trial(trial(rows(windows=((500, 1873),)), (500000, 800000)),
                                       ["right stick"], 1073000, 250000)
        self.assertEqual(result["predictions"]["arrival_timer_residual_us"], 0)
        self.assertEqual(result["predictions"]["serial_residual_us"], -773000)

    def test_trailing_freeze_is_preserved_not_free(self):
        result = capture.analyse_trial(trial(rows(end=1800, windows=((500, 3000),))),
                                       ["right stick"], 1073000, 250000)
        self.assertEqual(result["status"], "VOID")  # no recovery/movement control
        self.assertTrue(result["stalls"][0]["right_censored"])
        self.assertGreater(result["observed_frozen_us"], 1000000)
        self.assertNotIn("serial_residual_us", result["predictions"])

    def test_untouched_requested_buttons_void_the_verdict(self):
        result = capture.analyse_trial(trial(rows()), ["right stick", "buttons"], 1073000, 250000)
        self.assertEqual(result["status"], "VOID")
        self.assertTrue(any("buttons" in r for r in result["reasons"]))

    def test_onset_just_before_write_is_not_dropped(self):
        result = capture.analyse_trial(trial(rows(windows=((499, 1573),)), (500000,)),
                                       ["right stick"], 1073000, 250000)
        self.assertEqual(result["observed_frozen_us"], 1074000)

    def test_failed_write_never_gives_a_clean_verdict(self):
        data = trial(rows(), (500000,))
        data["writes"][0]["ok"] = False
        result = capture.analyse_trial(data, ["right stick"], 1073000, 250000)
        self.assertEqual(result["status"], "VOID")
        self.assertNotIn("predictions", result)

    def test_idle_control_has_no_predicted_command(self):
        result = capture.analyse_trial(trial(rows(), ()), ["right stick"], 1073000, 250000)
        self.assertEqual(result["status"], "NO_OBSERVED_STALLS")
        self.assertNotIn("predictions", result)

    def test_trial_boundaries_and_setup_are_separate(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "capture.txt"
            with p.open("w") as f:
                f.write("# connection dongle command trigger exercise stick\n")
                for n in (1, 2):
                    offset = (n - 1) * 10000000
                    f.write(f"# trial {n} begin {offset}\n")
                    f.write(f"# write {offset + 500000} trigger-resistance ok duration_us 1000\n")
                    for t, raw in rows(windows=((500, 1573),), offset=offset):
                        f.write(str(t) + " " + " ".join(f"{x:02x}" for x in raw) + "\n")
                    f.write(f"# marker {offset + 1573000} freeze-predicted-end\n")
                    f.write(f"# trial {n} end {offset + 3600000}\n")
                    # Setup traffic outside the trial must not turn into a freeze.
                    for i in range(300):
                        f.write(str(offset + 3700000 + i * 1000) + " 04 fe " + "00 " * 30 + "\n")
            result = capture.analyse(p)
            self.assertEqual(len(result["trials"]), 2)
            for t in result["trials"]:
                self.assertEqual(t["status"], "OBSERVED_STALLS")
                self.assertEqual(t["observed_frozen_us"], 1073000)
                self.assertEqual(len(t["markers"]), 1)

    def test_legacy_capture_and_noninput_reply(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "legacy.txt"
            lines = ["# connection dongle command trigger exercise stick", "# write 500000 trigger-resistance ok"]
            lines += [str(t) + " " + " ".join(f"{x:02x}" for x in raw) for t, raw in rows(windows=((500, 1573),))]
            lines += ["4000000 04 ec " + "00 " * 30]
            p.write_text("\n".join(lines))
            result = capture.analyse(p)
            self.assertEqual(result["ignored_non_input_reports"], 1)
            self.assertEqual(result["trials"][0]["observed_frozen_us"], 1073000)


if __name__ == "__main__":
    unittest.main()
