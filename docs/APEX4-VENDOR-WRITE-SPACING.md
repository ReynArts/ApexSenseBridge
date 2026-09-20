# APEX 4: two vendor commands sent close together, and the second is lost

Measured 2026-09-18 on one Apex 4 (`04B4:2412`, k2, DeviceType 84, firmware
`0x6835`) over its 2.4 GHz dongle, and confirmed in Cyberpunk 2077 under Proton.
Same family as [APEX4-DONGLE-TRIGGER-STALL.md](APEX4-DONGLE-TRIGGER-STALL.md),
different mechanism: that one is about a flag making a command expensive, this
one is about commands arriving too close together.

## The symptom, as a player sees it

Aim, fire, release, aim again - and the second aim has no trigger resistance.
It comes back after reloading the weapon. A sniper rifle is the exception,
because it chambers a round after every shot. The right trigger feels fine
throughout; only the left one goes dead.

## What is actually happening

Games send a release and then the new effect a few milliseconds apart. From a
Cyberpunk 2077 capture, every aim after the first looks like this:

```
79.3817  en1=0x0c  LT=5  RT=5     release both triggers
79.3860  en1=0x0c  LT=33 RT=37    apply both, 4.3 ms later
```

Two reports, each carrying both triggers, so four vendor commands inside 4.3 ms.
The pad keeps the first and drops what follows.

The left trigger is the one that suffers because the bridge applies the right
trigger first, which makes the left the second command every time. That is the
whole of "the revolver was fine, the left trigger died".

The first aim of a session works, which is what makes this confusing to
diagnose. The game sends that one differently - one trigger per report, so one
command at a time.

## The threshold

Blind on hardware, sending a release followed by the same effect at varying
gaps, reporting only whether the trigger resisted:

| gap | resistance |
|---|---|
| 4.3 ms | **none** |
| 10 ms | yes |
| 25 ms | yes |
| 50 ms | yes |
| 100 ms | yes |

So the boundary sits between 4 and 10 ms. The bridge spaces vendor writes 25 ms
apart, which keeps a margin without being felt: a full update is a handful of
commands and lands well inside the time it takes to raise a weapon.

Spacing waits rather than dropping. The command that a drop-if-too-soon policy
would discard is the newest one, which is the one that matters.

## Where it lives

`Apex5Device::writeSpacedOutputReport` - the single point every effect and
rumble command passes through, so they share one budget. Identity and profile
commands deliberately bypass it: they are request/response exchanges with their
own timing, and they are not sent in bursts.

## Verifying it is active

`ASB_DUMP_PAD_WRITES=<file>` records every command with a timestamp. Queue five
effects 2 ms apart and the file shows them reaching the pad 26 ms apart.

One wrinkle when reading that file: the line is written **before** the wait, so
the gap between the first two entries reflects when they were queued, not when
they were sent. From the third entry onwards the spacing shows up properly,
because the previous call blocks the caller.

## Unexplained

On the bench, a round that sends an effect with no preceding release stopped
producing resistance after this change, while the dump shows that command
reaching the pad three seconds clear of anything else. It may be a tired thumb
on a fourth repetition of the same test. The in-game behaviour is correct, so
this is recorded rather than chased.
