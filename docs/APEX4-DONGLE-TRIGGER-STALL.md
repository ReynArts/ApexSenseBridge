# APEX 4 over the dongle: applying a trigger effect freezes input for 1073 ms

Handoff note, 2026-09-18. Written in English on purpose: it is meant to be
readable outside this repo. Everything below is measured on one Apex 4
(`04B4:2412`, k2, DeviceType 84, firmware `0x6835`) on Linux 6.17, hidapi
0.14 over hidraw.

## Fixed, 2026-09-18

The bridge now clears Flydigi's apply flag on every Apex 4 trigger command.
The effects are identical - engaging and releasing both confirmed blind, 12/12
each way - and the stall is gone: the production path measured 10 commands
with 0 freezes and 299.7 stick changes a second, against 10 freezes out of 10
commands and 7 changes a second before. `kApex4ApplyFlag` in
`src/flydigi/Apex4Protocol.h` carries the reasoning, and the protocol test pins
the cleared byte.

End-to-end verification, same day, on the dongle: the regime that started this
whole thing - grip rumble and both triggers together, three commands a second -
ran 45 commands over 15 seconds with **zero freezes** and 265.6 stick changes a
second, against 9928 ms frozen out of 10 seconds and 2.9 changes a second
before the fix. All 73 trigger commands on the wire carried the cleared flag,
releases included. The operator confirmed the vibration and the resistance were
both present, which is the half no log can show.

Every mode the bridge can emit is covered. `AdaptiveTriggerTranslation`
produces exactly four - Normal, Race, RecoilRattle and SniperBreak - and all
four were confirmed blind with the flag cleared: Normal through the release
test at 12/12, Race at 12/12 and again at 5/5, RecoilRattle at 6/6,
SniperBreak at 5/5. Lock is confirmed too at 5/5 although nothing emits it.

Vibration is the one mode that scores badly, at 3/6, and it is not a flag
question: the control with the flag set scores exactly the same 3/6, so
whatever is wrong with it is unrelated. Nothing in the bridge emits it either.
A plausible reading is that trigger vibration needs something to drive it that
a bench test does not provide.

The rest of this note is the investigation that got there. It is kept because
its negative results are worth as much as its answer: they say what not to
re-test, and why several confident-sounding readings were wrong.

## A second, separate quirk on the same pad

Clearing the apply flag fixed the freeze but not everything. The pad also drops
a vendor command that arrives within a few milliseconds of the previous one,
which is precisely what games produce: a release and then the new effect about
4 ms apart. That one is written up in
[APEX4-VENDOR-WRITE-SPACING.md](APEX4-VENDOR-WRITE-SPACING.md).

Worth keeping the two apart when reading either. This document is about a
command being expensive; that one is about commands being too close together.

## The defect in one paragraph

When an Apex 4 is connected through its 2.4 GHz dongle, applying an
adaptive-trigger effect stops the pad's **entire main loop** for a fixed
~1073 ms. Input sampling, button reading and motor control all halt together
and all resume together. The report **rate** is untouched throughout, so every
throughput metric looks perfect during the fault: the pad keeps delivering
500-1000 reports a second, each carrying the same stale values. In game this
reads as the camera continuing to turn after the stick is released. Over a USB
cable the same effect applies for free and nothing stops.

## The mechanism, as far as the host can see it

Receiving and parsing the command is free: the identical report with Flydigi's
apply flag cleared crosses the same dongle path and costs nothing. What costs
is the pad executing the apply, and what it costs is the whole processor for a
constant time, not a queue of stale reports and not one subsystem. The cost is
serialised - two applied commands buy two consecutive 1073 ms windows, never
overlapping ones - and it does not vary with the effect, its mode, its payload
or its length. It happens in dongle mode and not in wired mode.

The best available reading, Michal's, is that the flag makes the pad store the
setting: a persist would explain a constant time and a stalled loop, and over
USB the same store need not block while over the radio it may take a reconnect
or drop packets until it completes. That fits everything measured and nothing
here tests it, so it stays a hypothesis.

What the firmware is doing for those 1073 ms is not reachable from here. The
input report carries no counter, timestamp or any other free-running field, so
the pad never reveals its own state, and the remaining paths are Flydigi's
protocol documentation, a firmware image, or a newer firmware than `0x6835`.

## Reproducing it

The instrumentation that produced these numbers is not in this branch: a stall
probe that reads the pad while a writer thread sends chosen vendor commands and
reports per-regime stall counts and durations, a raw capture of the undecoded
vendor stream around one command, and an analyser for the resulting files. That
is a lot of surface to maintain for a measurement most people will never repeat,
so it is held back rather than submitted uninvited. Ask and it is yours.

What matters is the method, which any equivalent tool can follow.

The measure is the number of times per second the right stick's value changes
(~200 while it is being circled). A regime that sends nothing acts as a validity
gate: a run where the operator was not moving the stick is rejected rather than
interpreted, because a motionless stick reports unchanged values too and that is
not a freeze. The operator is told exactly what to do, it is recorded in the
file, and a run is void by name when a control it asked for never moved.

A freeze is charged to a command only when it starts within 250 ms of one.
Freezes running to the last report in a capture are the operator having stopped,
and freezes following no write are the dongle's own; both are reported
separately rather than added to the bill.

Three measurements reproduce the whole picture: the cost itself, with statistics
rather than one observation; what stops, by circling the stick and hammering a
button for a whole run; and whether the loop runs at all, by following a trigger
command with a rumble write and asking the operator when the motors start
relative to the predicted end of the freeze.

## Established

| Finding | Evidence |
|---|---|
| Applying a trigger effect costs ~1073 ms of frozen input content | dozens of events across six runs |
| The cost is **the apply**, not the command | `apply=0`: 0 stalls / 10 commands. `apply=1`: 10 / 10. Same bytes otherwise |
| It is a fixed **time**, not a fixed number of reports | duration held at 1071-1076 ms across report rates of 500, 650 and 1000 Hz while the report count moved 536 → 1076 |
| Costs are serialised, one quantum per applied command | two commands 2 ms apart produce two contiguous 1073 ms windows with one fresh report between them |
| Report rate and jitter are undisturbed | ~1000 Hz inside freezes; interval spread 317 µs inside vs 285 µs on a healthy stream |
| Dongle only | wired: 0 stalls across every regime, 199.6 stick changes/s with resistance commands flowing |
| Rumble is free | `05 0f` costs nothing on either connection |
| The host is not blocking | `hid_write` returns in 0.9-1.8 ms on both connections |
| Needs no game, no libVIIPER, no uhid, no feedback handler | the probe uses none of them |
| The freeze takes the whole input report, digital buttons included | valid capture, 4 freezes, stick and buttons both worked throughout: nothing moved inside any of them |
| The freeze blocks the pad's whole main loop, not only its input path | rumble asked for 300 ms into a freeze reaches the motors only when the freeze ends. Five runs: the operator felt the motors start on the predicted-end line every time, and the measured end landed +1, +3, +5, +7 and +10 ms from it. Input, buttons and motors all resume together |
| Wired mode does not stall at all - it is the apply that is cheap there, not the reporting that is tougher | three wired runs: motors answered on the command line every time, and no freeze appears anywhere near the command in the captures |
| **A cleared apply flag engages the effect and costs nothing** | blind, 24 rounds over two runs, 12/12 each: every staged round felt, every empty round reported empty, no false positive either way. The control with the flag set scored the same, so the test discriminates. Cost separately measured at 0 stalls over 10 such commands |
| The freeze is visible on the USB bus itself, not an artefact of our reader | a `usbmon` capture of the dongle's bus shows the command's transfer completing normally in 0.85-1.98 ms while input endpoint 0x83 carries unchanged reports through the window, and the pad's second input endpoint 0x81 freezes with it, so reading elsewhere does not avoid it |
| The radio relay is not what costs | a command with apply cleared crosses the same dongle path for free, so the transfer arrives; only the pad executing the apply is expensive |

## Ruled out

Each of these was measured, not assumed. Re-testing them is wasted effort.

- **The trigger wire mode.** All six of Normal, Race, RecoilRattle, SniperBreak,
  Lock and Vibration cost 1.00-1.25 stalls per write. No mode is cheap.
- **Payload length.** A rumble command carrying non-zero filler bytes the
  handler never reads is free at 4, 5, 6, 7, 8, 10, 12, 15 and 20 bytes, and
  free again at 15 bytes with proper statistics in the probe.
- **Payload content.** A trigger command carrying Normal, with an all-zero
  effect payload, still costs. So does a byte-identical repeat of an effect the
  pad already has, which also rules out any per-change accounting.
- **A buffer of stale reports.** Killed by the rate invariance above.
- **An ADC or bus shared between the triggers and the sticks.** Digital buttons
  do not go through the analogue path, and they freeze with everything else.
- **The dongle synthesising filler.** The in-freeze stream carries the same
  jitter as a healthy one. Weak evidence on its own — the jitter is largely USB
  endpoint scheduling — but it points the same way as the rate result.
- **Deduplication as a defence.** The charge is per applied write.

## Two retractions, and what they cost

Both are recorded because they are method lessons, not just wrong answers.

1. **"Windows is a control experiment."** It is not. `WindowsLibViiperVirtualDualSense.cpp`
   shares the blocking-callback pattern, but nobody has confirmed an Apex 4
   running rumble together with adaptive triggers on Windows, and the protocol
   code is shared, so the defect could sit there unnoticed.
2. **"The cost is payload length."** Claimed from one controlled pair. Nine
   further captures refuted it. The dongle produces freezes of its own that no
   command asked for, at roughly one per ten short captures, and a single
   observation cannot separate those from a command's cost.

A third run was voided rather than retracted, which is the improvement. Asked
whether buttons survive a freeze, the capture showed them frozen - but the
operator had never pressed one, and an untouched control reports unchanged
values exactly like a frozen one. `analyze-apex4-capture.py` now prints which
fields were exercised at all and refuses to let an untouched field count as
evidence.

Two rules came out of this. Anything with a number attached goes through the
probe with ten or more commands per regime, never through a single capture. And
every measurement states, before its result, which control was actually
exercised - the probe's `idle` regime does this for the stick, and the capture
analyser now does it per field.

## The open question, and what is worth asking about it

The behavioural account is complete; the firmware one is not. Anyone picking
this up should know which questions the host can still answer and which it
cannot, because the tempting ones are mostly in the second group.

Why does applying an effect block input updates for ~1073 ms on the dongle and
cost nothing over a cable?

Two things make this harder than it looks. The Apex 4 report carries **no
sequence counter, no timestamp and no free-running field of any kind** — across
1903 reports with the pad at rest, not one of the thirty-two bytes changes — so
the host cannot distinguish a pad repeating itself from a pad composing fresh
reports out of unchanged inputs. And the two connections are not the same pad
minus a radio: over the dongle it enumerates as `Flydigi VADER3` with a 32-byte
output report at ~1000 Hz, wired as `Flydigi APEX 4` with a 64-byte output
report at ~490 Hz. Attributing the cost to the radio itself would be an
overreach; it is a difference between two operating modes.

The serialisation is itself a clue about where the block sits: two applied
commands 2 ms apart produce two contiguous windows and never overlapping ones,
which is what a busy main loop looks like rather than a busy peripheral.

`1073.741824 ms` is exactly 2^30 nanoseconds, which is the kind of constant a
30-bit nanosecond counter or a `1 << 30` timeout produces. Measured quanta sit
at 1067-1082 ms, so this fits — but the spread is ±15 ms and it cannot be told
apart from, say, 1072.0 ms. Treat it as a candidate, not a finding.

## Closed along the way

- **Does the pad's main loop run during a freeze?** No. Rumble asked for during
  a freeze reaches the motors only when the freeze ends.
- **Why is the same apply free over a cable?** Because wired mode does not
  stall either — its loop keeps running. The difference is in executing the
  apply, not in how each mode reports.
- **Do buttons survive a freeze?** No. They stop with everything else.

Two notes on how those were measured, because both took more than one attempt.
Asking an operator whether something was late makes them judge an interval,
which nobody does well; the capture therefore prints a line at each competing
reading's predicted moment and asks which line the motors started on, which is
a coincidence and easy to call. And the first version of that test stopped the
motors inside the freeze, so a blocked loop would have processed the on and off
commands back to back and produced nothing felt — indistinguishable from a
failed write. A probe that cannot tell its two answers apart is worse than no
probe.

## Ideas that have not been tried

- **Does a cleared apply flag engage the effect anyway?** The most important
  open question in this file, because the answer might be the whole fix.
  Codex's follow-up (captures under `diagnostics/apex4-followup-20260918-144132`)
  ran a trial that sent one Race
  effect on RT with the flag cleared: no freeze in the measured window -
  independently confirmed from the raw bytes - and the operator reported RT
  resistance. A no-command control in the same session was reported as no
  resistance. If that holds, the bridge can have working adaptive triggers on
  the dongle at no cost by clearing one boolean, since
  `Apex5Device::setTriggerRaw` and `clearTrigger` pass `apply = true` today.

  **Confirmed 2026-09-18.** Blind, 12 rounds with the flag cleared and 12 as a
  control with it set: 12/12 both times, no false positive or miss in 24
  rounds. A cleared flag engages the effect exactly as a set one does, and
  costs nothing. The remaining work is not whether but how far it reaches.

  The original observation rested on one subjective report, which is why it
  was not treated as a finding until this. The blind harness tests it properly:
  each round either stages an effect or sends nothing by a coin flip the
  operator cannot see, and only the tally at the end says which was which. The
  same rounds run with the flag set, as a positive control. A clean sweep is
  the only result that should change what the bridge sends.

  Two things to settle after that, both of which decide whether the fix is
  usable rather than merely real: whether a cleared flag engages every mode or
  only Race, and whether releasing a trigger back to Normal works the same way
  - if clearing still needs the flag set, every release still costs 1073 ms.
  A plausible reading of the protocol is that the flag means persist rather
  than engage, which would make the freeze a non-volatile write.
- **Firmware currency.** The pad runs `0x6835`. Nobody has checked whether
  Flydigi has published anything newer. A fix here would be a real fix.
- **usbmon on the host-to-dongle leg.** Expected to be uninformative, since
  reports flow normally throughout, but it is a genuinely different instrument.
- **Probing unknown command IDs.** Deliberately not done: an unknown `05 XX`
  could be anything, including a firmware-update entry. Do not do this without
  the owner's explicit consent.
- **A bug report to Flydigi.** Not written yet, and it is the only path that
  removes the cause rather than working around it. The material is unusually
  precise: one command of the three an Apex 4 accepts, one flag within it, a
  constant time, one connection mode, with the method and the negative results
  that pin it down.

## What ships today

The fix above, and nothing built around the defect. While the cause was
unknown, `AdaptiveTriggerBridge` could hold the newest effect per side and
release it when the sticks were centred, so the freeze would land while the
player stood still. That was damage limitation with a note saying it should be
deleted the day the cause was addressed, and it has been: the policy, the hold,
the flush from the input loop and the three flags that tuned it are gone, along
with the startup warning that told dongle users to fetch a cable.

One genuine bug was found along the way and fixed: the session established its
Normal baseline through `clearAll`, which bypasses the bridge, so the bridge
started with an empty cache and wrote Normal over Normal the first time a game
released a trigger. The same blind spot ran the other way after an Apex 5
profile switch, where a stale cache suppressed a write the game needed.
`noteNormalBaseline` closes both.

## Code map

| Path | Role |
|---|---|
| `scripts/analyze-apex4-capture.py` | freeze measurement straight from captured bytes |
| `src/dualsense/AdaptiveTriggerBridge.{h,cpp}` | translation, deduplication, deferral |
| `src/flydigi/Apex4Protocol.{h,cpp}` | the three commands an Apex 4 accepts |

`tests/test_apex4_capture_analysis.py` drives the analyser on synthetic input
and never opens a controller.
