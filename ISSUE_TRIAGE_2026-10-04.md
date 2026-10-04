# Open-issue triage — 2026-10-04

Scope: every open issue read on 2026-10-04, except #12 (Apex 6 Pro), which is
intentionally excluded. Comments below are drafts: none has been posted.

## Summary

| Issue | Assessment | Action |
| --- | --- | --- |
| #27 | Not a product issue | Close as spam/self-promotion |
| #26 | Firmware/USB identity cannot be repaired by ASB; false success can | Implemented capability detection and actionable warning |
| #25 | Already implemented | Ask for confirmation on beta 10, then close |
| #24 | Not implementable with the current virtual-device backend | Clarify scope; requires upstream virtual USB-audio capture support |
| #23 | Plausibly fixable, but evidence is insufficient and #26 may overlap | Request beta 10 logs and connection-specific diagnostics |
| #22 | Translation and LT/RT stream fixes already exist; residual symptom is not isolated | Request exact beta 10 telemetry and hardware trigger mode |
| #21 | Existing Steam handles cannot be revoked by HidHide | Explain one-time Steam restart/order requirement; no safe forced restart |
| #20 | Removal persistence was already fixed and regression-tested | Ask for beta 10 reproduction plus settings/log export |
| #17 | Wake/retry/recovery fixes exist; last requested log was never attached | Request a fresh beta 10 automatic report after wake |
| #15 | Original input path is fixed; a remaining orphan-process cause was found | Implemented bounded child-process reap; request retest |
| #10 | Motion path and axes are validated; sensitivity calibration was missing | Implemented overall gyro sensitivity and yaw-only correction |
| #2 | Mapping and known detection/persistence defects are already fixed | Request a current beta 10 retest of rapid clicks without Force Continuous |

## #27 — promotional post

### Analysis

This is unrelated self-promotion, contains no ApexSenseBridge defect or feature
request, and should not consume engineering time.

### Comment to publish

> This issue is unrelated to ApexSenseBridge and does not contain a bug report
> or feature request for this project. Closing as off-topic/self-promotion.

Recommended action: close as spam/off-topic.

## #26 — Apex 4 exposes two incompatible identities with the same VID/PID

### Analysis

The reports establish two descriptor states for `04B4:2412`:

- full Apex 4 identity: 64-byte output report, both LT and RT work;
- degraded `Flydigi VADER3`/Direwolf-like identity: 32-byte output report, LT
  may work, RT does not, while HID writes still report success.

ASB cannot turn the controller firmware's degraded USB identity into the full
one. It can stop claiming success. The engine now classifies the descriptor,
`identify` reports partial trigger capability, and bridge startup emits an
actionable reconnect warning. Writes remain permitted so working LT/rumble are
not unnecessarily disabled.

### Comment to publish

> Thank you — the descriptor data explains the false positive. The same
> `04B4:2412` VID/PID can expose either the full 64-byte Apex 4 trigger
> interface or a degraded 32-byte `VADER3`-like interface. On the degraded
> identity, a HID write can succeed even though RT is not implemented by the
> exposed firmware path.
>
> I have prepared a fix that classifies these two interfaces. `identify` now
> reports the 32-byte state as partial (`LT may work, RT unavailable`) and
> bridge startup warns to reconnect the controller/receiver until the 64-byte
> interface appears. I am deliberately not blocking all output, because LT and
> rumble can still be useful in the degraded state.
>
> The underlying identity transition is controller/firmware behavior and
> cannot be repaired safely by ASB. When the next test build is available,
> please run `ApexSenseBridge.exe identify` once in each state and paste both
> outputs so I can confirm the detection on your unit.

## #25 — vibration intensity control

### Analysis

Global trigger and vibration strength controls (0–100%) already ship in the
current beta line and are forwarded by the Tray to the engine. No further code
change is justified without a report that the control is absent or ineffective
on a current build.

### Comment to publish

> This is implemented in the current beta: open **Tray → Settings → Controller
> feel** and use **Vibration strength** (0–100%). The value is applied when the
> next bridge session starts; 100% preserves the original game output and 0%
> disables it.
>
> Please confirm with beta 10 that the slider changes both ordinary game rumble
> and the relevant bridged vibration path on your controller. If it does, this
> issue can be closed as completed. If not, please name the game, connection
> type, controller model and which vibration path remains unchanged.

## #24 — arbitrary PC microphone as the virtual DualSense microphone

### Analysis

This is not a simple audio-device selector. The virtual DualSense backend
currently creates the controller/audio USB device but exposes no supported path
for ASB to inject arbitrary Windows capture PCM into its virtual microphone
endpoint. A correct implementation needs upstream virtual USB-audio capture
support, device selection, format conversion/resampling, clocking, mute/privacy
semantics and lifecycle handling. Faking it in the bridge would risk feedback
loops and selecting the wrong private input.

### Comment to publish

> I investigated this beyond the UI layer. The current virtual DualSense
> backend does not expose a supported input path for injecting PCM from an
> arbitrary Windows recording device into the virtual controller microphone.
> This therefore requires upstream virtual USB-audio capture support first,
> followed by device selection, resampling/clocking, mute/privacy handling and
> feedback-loop protection in ASB.
>
> To define the feature correctly, could you clarify the target use case:
> should the selected PC microphone replace the DualSense mic only for games
> that insist on the controller endpoint, or should ASB also change the Windows
> default communications device? Please also provide one game that requires
> this route and the capture format/device you use. I will keep this as an
> upstream-dependent feature request rather than claim it is currently
> implementable in ASB alone.

## #23 — Apex 4 dongle instability/lag; wired trigger concerns

### Analysis

There are multiple possible layers: input stream freshness, the paced Apex 4
feedback writer, wireless interference/firmware, or the degraded identity in
#26. Current code already coalesces Apex 4 output and spaces writes by 25 ms to
avoid receiver stalls. The reports do not contain the requested beta logs, so
changing timing again would be speculative and could regress stable units.

### Comment to publish

> I cannot isolate this safely from the current evidence yet. The current beta
> already coalesces Apex 4 feedback and spaces vendor writes to avoid receiver
> stalls, while #26 shows that the same VID/PID can also reconnect as a degraded
> 32-byte interface.
>
> Please retest with beta 10, once wired and once with the 2.4 GHz receiver,
> with Flydigi Space Station closed. For each connection, attach the automatic
> report and paste the output of:
>
> ```text
> ApexSenseBridge.exe list
> ApexSenseBridge.exe identify
> ApexSenseBridge.exe input-status --seconds 10 --json
> ```
>
> Please state whether lag occurs with the bridge idle, only while adaptive
> triggers are active, or also with trigger/vibration strength set to 0%. That
> distinction will tell us whether to fix input acquisition, feedback pacing,
> or an interface transition rather than guessing at all three.

## #22 — Apex 5 left-trigger twitch and early bow release

### Analysis

Beta 9 added native `0x21/0x25/0x26` trigger decoding and guarded the
independent LT/RT stream. The reporter confirmed that the missing LT state was
fixed but the twitch/early-release symptom remained. The available log only
proves one stream loss/restart; it does not show whether the remaining event is
an input threshold, a game effect transition, a Space Station trigger profile,
or a stale effect. Altering `0x00`/off semantics without a raw event would risk
breaking valid no-update effects.

### Comment to publish

> Thanks for confirming that the missing LT state was fixed. The remaining
> twitch/early bow release needs one more targeted capture; the current log
> shows an LT/RT stream restart but does not identify whether the unwanted event
> comes from the physical trigger value, the game's adaptive effect, or the
> hardware trigger profile.
>
> Please retest both Horizon games with beta 10 and attach the automatic report.
> For each game, tell me:
>
> 1. wired or 2.4 GHz receiver;
> 2. the active Flydigi Space Station trigger mode for LT and RT (Normal or a
>    hardware/adaptive profile);
> 3. whether setting both hardware triggers to **Normal** changes the symptom;
> 4. whether the on-screen LT value moves when the twitch happens;
> 5. the exact action and timestamp that causes the bow to fire early.
>
> I am keeping this open. The existing protocol fixes stay in place, but I do
> not want to guess at trigger-off/no-update semantics without that evidence.

## #21 — double D-pad input / repeated DualSense notification

### Analysis

The behavior matches Steam retaining a handle it opened before HidHide changed
visibility. HidHide can prevent future opens but cannot revoke an existing
process handle. ASB should not kill/restart Steam automatically because that can
interrupt downloads, games and unsaved state. This is an ordering limitation,
not a second input emitted by the mapper.

### Comment to publish

> This behavior is consistent with Steam having opened the physical controller
> before HidHide activated. HidHide blocks new opens; it cannot revoke a device
> handle already held by Steam, which leaves both the physical pad and virtual
> DualSense visible until Steam is restarted.
>
> Please try this once: close Steam completely (including the tray process),
> start an ASB bridge session and wait for **Bridge ready**, then reopen Steam
> with Steam Input disabled for the game. If the duplicate D-pad input and
> repeated controller notification disappear, that confirms the retained-handle
> cause. ASB will not force-restart Steam automatically because doing so could
> terminate games/downloads. If it still reproduces after that exact order,
> attach the ASB report plus a `joy.cpl` screenshot taken while the session is
> active.

## #20 — excluded-game removal does not persist

### Analysis

The removal path already clears both the normalized key and title alias, and a
regression test covers reactivation after exclusion removal. The report lacks a
current version and serialized settings, so it may describe an older stable
build or a second alias not present in the report.

### Comment to publish

> The exclusion-removal path was corrected to remove both the normalized game
> key and its title alias, and it is covered by a persistence regression test.
> Please retest with beta 10: exclude the game, save, remove the exclusion,
> restart the Tray, and launch the game again.
>
> If it still remains excluded, please export the Tray diagnostic report and
> provide the exact displayed game title plus the sequence of buttons used.
> Please do not manually edit the settings file; the export is enough to see
> whether a second alias is being retained. With a successful beta 10 retest,
> this issue can be closed.

## #17 — wake from sleep leaves HidHide/session recovery stuck

### Analysis

The engine now waits for delayed Windows HID publication after wake, retries
identity verification and offers explicit recovery after a confirmed stream
loss. The reporter said beta 6 still failed, but the fresh log requested after
that result was never attached; later recovery changes cannot be evaluated from
the old data.

### Comment to publish

> Several wake-specific changes landed after the original report: bounded wait
> for the vendor interface to reappear, repeated identity verification, and an
> explicit recovery flow when the input stream is lost. I still need a current
> reproduction because the post-beta-6 log requested in this thread was not
> attached.
>
> Please reproduce once with beta 10 using the same sleep/wake sequence, choose
> **Resume bridge** if the recovery card appears, and attach the newly generated
> automatic report. Also state whether the controller was wired or on the
> receiver and whether Flydigi Space Station was running. If the UI is stuck,
> include a screenshot before manually restoring visibility.

## #15 — no game input, Force Continuous timeout, stale external session

### Analysis

The latest report shows the original input problem is resolved:
`apex5-v9-mapped+vendor-guarded`, `received_state=true`, and more than 2,000
reports. The remaining failure is a stale child engine. On initialization
timeout, the launcher requested a cooperative stop, ignored a timeout from that
stop, disposed its process object and lost the only handle while the process
kept the global session mutex. Later attempts then correctly—but
misleadingly—reported an external owner.

The fix now gives cleanup a bounded cooperative window, then kills only the
exact child started by that launcher. The return still records an unclean stop.
A regression test uses a deliberately hung child and verifies the forced reap.

### Comment to publish

> The newest diagnostics show that the original input path is now healthy
> (`apex5-v9-mapped+vendor-guarded`, `received_state=true`, 2,000+ reports).
> I found a separate cause for the remaining **Session managed externally**
> loop: after an initialization timeout, the launcher could discard its process
> handle even when the child ignored the stop request. That orphan retained the
> global session lock and made every later attempt appear externally owned.
>
> I have prepared a fix: cleanup first gets a bounded cooperative window, then
> forcibly terminates only the exact child process launched by that Tray or
> Playnite instance. A regression test with a deliberately hung child confirms
> that it is reaped instead of becoming a ghost session.
>
> Please retest the next build without manually killing processes. If startup
> still times out, attach the automatic report from that same attempt; it should
> now permit an immediate second start rather than staying externally managed.

## #10 — Apex 4 gyroscope

### Analysis

The important hardware validation is positive: the captured native fields,
axis orientation and virtual DualSense path work in Horizon Zero Dawn. The
remaining item is sensitivity—low on every axis, especially yaw. Free-hand
captures cannot safely establish absolute degrees/second and yaw is the least
constrained axis, so silently changing fixed gains would be overconfident.

The completed implementation keeps the validated gains as 100% defaults and
adds two 25–400% controls to CLI, Tray and Playnite: global Apex 4 gyro
sensitivity plus an additional yaw-only correction. Scaling saturates safely,
is Apex 4-only, and has unit coverage for scaling and saturation.

### Comment to publish

> Your Horizon Zero Dawn test completes the most important validation: the
> native Apex 4 motion fields, signs/axes and virtual DualSense game path are
> working. The only remaining confirmed defect is low sensitivity, especially
> yaw.
>
> I have prepared the missing calibration controls while preserving the current
> behavior at 100%:
>
> - **Apex 4 gyro sensitivity** (25–400%) scales all three gyro axes;
> - **Apex 4 yaw correction** (25–400%) adds a separate yaw-only trim.
>
> They are available in both Tray and Playnite and are also exposed as
> `--apex4-gyro-strength` and `--apex4-gyro-yaw-strength`. Values saturate safely
> and affect Apex 4 only. Please start at 100/100, raise the global value first,
> then adjust yaw only if it remains weaker. Test wired and receiver modes and
> report the two values that feel correct. Once that is confirmed, I consider
> #10 complete.

## #2 — Select/View should act as DualSense touchpad click

### Analysis

The direct View-to-touchpad mapping exists, weak `Control` substring detection
was removed, and the persisted Force Continuous mismatch was corrected. Current
tests cover immediate transitions, but the last user result predates the newest
input/session path and says rapid clicks were missed unless Force Continuous was
active. A current retest is needed before changing debounce/hold logic that
other game-specific touchpad profiles depend on.

### Comment to publish

> The requested mapping is implemented: Apex **Select/View** maps directly to a
> DualSense touchpad click. Since the last retest, the false `Control` detection,
> persisted Force Continuous state and the physical input/session path have all
> been corrected, and rapid touchpad transitions are covered by regression
> tests.
>
> Please retest with beta 10 with Force Continuous **off**. In `joy.cpl`, verify
> that only the virtual **Wireless Controller** is visible during the session,
> then try ten slow and ten rapid View presses in the affected game. If any are
> missed, attach the automatic report and name the game plus Tray or Playnite
> ownership. Also say whether the Tray showed **Bridge ready** before the game
> enumerated controllers. That will distinguish a remaining click issue from
> late controller enumeration.
