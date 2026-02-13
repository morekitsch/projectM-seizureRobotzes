# Agents Context + Handoff Tracker

Last updated: 2026-02-13

## Current State
- Quest OpenXR menu/input work is in late hand-touch tuning.
- Direct touch behavior was further adjusted for physical feel:
  - tighter touch hover/activation depth window
  - small fingertip forward offset derived from index distal->tip direction
  - hand-tracking-specific HUD distance/vertical offset comfort adjustments
  - center-eye head pose usage for HUD/input stability in stereo view
- Controller ray/trigger and legacy button mappings remain active.

## Verified This Session
- `./gradlew :app:assembleDebug` succeeded.
- Build succeeds with current uncommitted hand-touch tuning changes in `src/quest-openxr/main.cpp`.

## User Feedback (Latest)
- Current result is improved but still not fully "Meta Quest native" in feel.
- User wants true OpenXR hand-style interaction/presence; this pass is close but may need one more polish iteration.

## Checkpoint Commit (Freeze Recovery)
- Requested checkpoint before pausing work.
- Commit should include:
  - `src/quest-openxr/main.cpp` (hand-touch + HUD placement tuning)
  - `agents.md` (updated handoff context)
- Do not include generated noise:
  - `apps/quest-openxr-android/app/.cxx/...`
  - `logs/...`
  - `scripts/...` (if unrelated)

## Next Context Priorities
1. In-headset QA of current hand-touch tuning:
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
- `agents.md`

## Workspace Notes
- Ignore build/runtime noise when committing:
  - `apps/quest-openxr-android/app/.cxx/...`
  - `logs/...`
  - `scripts/...` (if unrelated to this task)
