# ApexSenseBridge 1.0.0 — local release candidate

This package is prepared locally; it has not been published to GitHub.

## Tray application and shared features — APEX 4/5/6

### Main application and game detection

- Open `ApexSenseBridgeTray.exe`, the main application. Opening the engine
  without a command now opens Tray instead of leaving an unexplained console.
- Each game can optionally select its real executable. Learned paths are
  offered too; leaving the choice empty preserves automatic detection.
- The home page reports the actual session phase, owner, game, controller,
  profile and refusal/stop reason. Prepared executable launching is available
  only in diagnostics and waits for bridge readiness before launching the game.

### Controller feel

- Global Tray controls adjust trigger strength, vibration strength and the
  audio-haptic threshold. Changes apply at the next Tray session startup.
- Strength reduction preserves effect travel, timing and frequency. The audio
  threshold does not suppress standard rumble. These are not per-game presets.

### Guided recovery after disconnection

- A confirmed runtime disconnection or physical-input stream loss pauses
  automatic activation for that session, including sleep with the dongle attached.
- Reconnect the controller, open the home page, and request **Resume bridge**.
  A confirmation explains that the virtual DualSense will be recreated.
- Recovery retains the game/profile/APEX slot and displays verified controller
  identity, virtual-device readiness, isolation and final session readiness.
- Failed retries stay paused and preserve the failure details. Dismissing the
  notice does not restart the bridge in the background. Closing the tracked
  game clears the recovery; manual sessions can be stopped with their toggle.
- Playnite-owned sessions are not taken over: restart them from Playnite.
- The game is never closed or relaunched automatically. Some games require
  restarting to recognize the recreated DualSense; live reacquisition is not
  guaranteed. Physical disconnect/reconnect gameplay validation remains needed.

## APEX 4/5-specific fixes and limits

The included APEX 4/5 trigger translation fixes preserve valid left-trigger
effects and correct packed native zones and weapon interval lengths. Multi-zone
effects remain approximated by the supported FORCEADAPT mapping. Resolution of
the reported Horizon symptoms still requires confirmation in both games.

No diagnostic assistant, per-game feel presets, favorites or compatibility
certification expansion is added in this iteration.
