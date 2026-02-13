# Agents Context + Handoff Tracker

Last updated: 2026-02-13

## Current Context
- Current `HEAD`: `3aa5011bd` (`chore: full workspace checkpoint before major changes`)
- Functional XR input/UI commit: `cebeef4ca` (`quest-openxr: add hand-tracking HUD interaction and simplify menu`)
- Branch status: `master` is ahead of `origin/master` by 8 commits.
- Submodule pointer: `vendor/projectm-eval` -> `a1611be` (`projectm-eval: update scanner artifacts`).
- Device status: Quest 3 connected over `adb`; latest `installDebug` succeeded and `com.projectm.questxr` is installed.

## What Was Completed
- Added hand-tracking interaction path in OpenXR using `XR_EXT_hand_interaction` when available.
- Added aim pose action + per-hand action spaces, with HUD ray hit-testing and tile activation by trigger/pinch.
- Kept standard controller mechanisms and legacy direct button mappings active as fallback.
- Simplified/decluttered HUD text labels and added in-HUD aim/select guidance.
- Added manifest support for Quest hand tracking:
  - `oculus.software.handtracking` feature
  - `com.oculus.permission.HAND_TRACKING`
  - hand-tracking metadata (`V2.0`, `HIGH`)
- Updated Quest README to document controller + hand interaction behavior.
- Verified build/deploy flow:
  - `:app:assembleDebug` passed
  - `:app:installDebug` passed

## Active TODOs
- [ ] Do in-headset QA pass for hand tracking:
  - Verify aim alignment and pinch reliability
  - Verify HUD show/hide behavior when hidden then triggered
  - Confirm controller ray + trigger and hand aim + pinch both work in the same session
- [ ] Re-validate auto-skip thresholds (`min_fps`, `bad_seconds`, `cooldown_seconds`) on known slow presets.
- [ ] Decide whether to keep or later prune large generated artifacts captured in checkpoint commit `3aa5011bd`.

## Backlog
- [ ] Optional: clean up OpenXR struct `next` initializer warnings (non-blocking).
- [ ] Optional: if needed later, split large checkpoint commit into cleaner logical history before upstreaming.

## Superseded / Closed Out-of-Date Notes
- `be17ec469` is no longer the latest tested HUD state; replaced by `cebeef4ca`.
- Previous note about "local untracked artifacts intentionally not committed" is out of date; those artifacts were included in `3aa5011bd`.
- Previous resume target `be17ec469` is superseded by `3aa5011bd` (or `cebeef4ca` for functional-only baseline).

## Resume Checklist
1. Choose starting point:
   - `3aa5011bd` for exact full workspace checkpoint.
   - `cebeef4ca` for hand-tracking/HUD functional baseline without the checkpoint payload.
2. Build/install from `apps/quest-openxr-android` with `./gradlew :app:installDebug`.
3. Run in-headset input matrix:
   - Controller ray + trigger on HUD tiles
   - Hand aim + pinch on same HUD tiles
   - Legacy direct controller buttons (`A/B/X/Y/L3/R3/LT/RT`)
4. Proceed with major changes from the selected baseline.
