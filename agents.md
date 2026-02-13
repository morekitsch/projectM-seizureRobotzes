# Agents Context + Handoff Tracker

Last updated: 2026-02-13

## Current State
- Quest OpenXR menu/input work is close to target behavior.
- Hand input now supports:
  - direct touch priority near HUD
  - ray + pinch fallback
  - auto-closer HUD distance during hand tracking
  - OpenXR hand-joint rendering (`XR_EXT_hand_tracking`) for in-world hand presence
- Controller ray/trigger and legacy button mappings remain active.

## Verified This Session
- `./gradlew :app:assembleDebug` succeeded.
- `./gradlew :app:installDebug` succeeded on Quest 3 (`2G0YC1ZF9Q01G6`).
- App launch confirmed with:
  - `Enabling XR_EXT_hand_interaction for hand gesture input.`
  - `Enabling XR_EXT_hand_tracking for tracked hand-joint rendering.`
  - `OpenXR hand trackers initialized.`

## User Feedback (Latest)
- Current result is improved but still not fully "Meta Quest native" in feel.
- User wants true OpenXR hand-style interaction/presence; this pass is close but may need one more polish iteration.

## Next Context Priorities
1. In-headset QA/tuning:
   - hand-joint visual weight/opacity/size
   - touch activation depth/hover thresholds
   - HUD distance/comfort while touching
2. If still not native enough:
   - add hand mesh rendering path (Quest extension if available, e.g. `XR_FB_hand_tracking_mesh`)
3. Keep regression checks for:
   - controller ray + trigger HUD interaction
   - legacy direct controller actions
   - hand ray + pinch fallback

## Files Changed In This Pass
- `src/quest-openxr/main.cpp`
- `apps/quest-openxr-android/README.md`
- `agents.md`

## Workspace Notes
- Ignore build/runtime noise when committing:
  - `apps/quest-openxr-android/app/.cxx/...`
  - `logs/...`
  - `scripts/...` (if unrelated to this task)
