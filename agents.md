# Agents Context + TODO Tracker

Last updated: 2026-02-12

## Current Context
- Reference base commit: `d79a017c5263bec1933799f7b260c3ca2711ca52`
- Latest commits:
  - `2f4085b18` `quest-openxr: improve controller HUD usability and build wiring`
  - `be17ec469` `quest-openxr: improve HUD text layout and prevent label overlap`
- Branch status: `master` is ahead of `origin/master` by 3 commits.
- Device status: Quest 3 connected over `adb`; `installDebug` succeeded and `com.projectm.questxr` is installed.
- Working file for HUD behavior: `src/quest-openxr/main.cpp`

## Recent Work Completed
- Added Meta Touch Plus/Pro + Oculus Touch binding suggestions.
- Moved previous-track to `L3` and trigger actions to value-threshold input.
- Added in-headset HUD text layer with status lines and control labels.
- Added auto-hide HUD behavior and visibility extension on interaction/state change.
- Added per-button feedback (`INPUT: ...`) and stronger button flash cues.
- Fixed top clipping by tuning HUD placement and panel mask.
- Improved readability (closer/larger panel, stronger text contrast).
- Reduced crowding and prevented text overlap with fit-to-width + ellipsis logic.
- Built and installed debug APK to Quest after each fix iteration.

## Active TODOs
- [ ] Validate auto-skip thresholds (`min_fps`, `bad_seconds`, `cooldown_seconds`) against real slow presets on Quest.

## Backlog
- [ ] Optional: clean up OpenXR struct `next` initializer warnings (non-blocking).

## Done
- [x] Validate controller actions (`A/B/X/Y/L3/LT/RT`) are recognized on-device.
- [x] Validate menu panel is no longer cut off in-headset.
- [x] Add clear button-press feedback and improved flash response.
- [x] Improve text readability and remove visible label bleeding/overlap.
- [x] Commit working state for usability + layout passes.
- [x] Apply HUD polish notes captured through commit `be17ec469`.
- [x] Set track/source HUD policy to sanitized basename for URL/path labels.
- [x] Add runtime debug properties for HUD placement/scale and on-device tuning without rebuild.
- [x] Add performance guard: detect sustained low FPS, auto-mark slow presets, and skip marked presets.
- [x] Add HUD debug visibility toggle (`debug.projectm.quest.hud.enabled`).

## Notes
- Current package on device: `com.projectm.questxr`.
- Latest tested HUD/layout state is in commit `be17ec469`.
- Local untracked artifacts exist (`agents.txt`, `logs/`, Gradle wrapper/cache files) and were intentionally not committed.

## Resume Checklist
1. Start from `be17ec469`.
2. Build/install with `./gradlew installDebug` in `apps/quest-openxr-android`.
3. Validate performance guard behavior on known heavy presets and tune thresholds via `adb setprop`.
4. Commit each validated follow-up in small, isolated commits.
