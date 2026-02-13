# Agents Context + Handoff Tracker

Last updated: 2026-02-13

## Current Focus
1. Optimize rendering for heavier presets:
   - validate SGSR defaults across heavier preset packs on device
   - tune `debug.projectm.quest.perf.render_scale` target by headset (Quest 2/3/Pro)
   - validate adaptive scale behavior before auto-skip on slow presets

## Recently Resolved
- Upscaling path integrated for Quest/Adreno:
  - selected Qualcomm Snapdragon GSR1 (single-pass spatial upscaler optimized for Adreno)
  - implemented lower internal projectM render resolution + SGSR upscale to output texture
  - added runtime controls:
    - `debug.projectm.quest.perf.sgsr` (bool, default `true`)
    - `debug.projectm.quest.perf.render_scale` (float, default `0.58`, clamped `0.50..1.00`)
    - `debug.projectm.quest.perf.auto_scale` (bool, default `true`)
    - `debug.projectm.quest.perf.auto_scale.min_render_scale` (float, default `0.54`)
    - `debug.projectm.quest.perf.auto_scale.step` (float, default `0.03`)
    - `debug.projectm.quest.perf.auto_scale.down_fps` / `up_fps` (float, defaults `68` / `71`)
    - `debug.projectm.quest.perf.auto_scale.hold_seconds` / `cooldown_seconds` (float, defaults `1.4` / `1.5`)
  - fallback behavior: if SGSR init fails or SGSR is disabled, app renders projectM at native internal resolution
  - HUD now shows SGSR on/off with internal->output resolution and smoothed FPS for quick verification that upscaling is active
- Beat-reactivity latency fix completed:
  - replaced fixed-rate audio dequeue with frame-time-based adaptive pull + backlog catch-up
  - reduced queue latency envelope and added periodic queue instrumentation logging
  - validated on Quest 3: beat response now near-real-time
- Audio mode UX cleanup completed:
  - HUD naming updated to `SYSTEM SOUND`, `INTERNAL PLAYER AUDIO`, `MICROPHONE`, `SYNTHETIC`
  - cycle order confirmed as system -> internal player -> microphone -> synthetic
- Quest HUD/menu sizing and spacing pass completed:
  - top-row preset buttons resized to match other action button heights
  - middle/lower text bands vertically centered with balanced top/bottom padding
- Updated build installed and launched on Quest 3.

## Verified This Session
- `./gradlew :app:assembleDebug` succeeded.
- `./gradlew :app:installDebug` succeeded.
- `com.projectm.questxr/.QuestNativeActivity` launched on headset.

## Files Changed In This Pass
- `src/quest-openxr/main.cpp`
- `agents.md`

## Regression Checks To Keep
- controller ray + trigger HUD interaction
- legacy direct controller actions
- hand ray + pinch fallback

## Workspace Notes
- Ignore build/runtime noise when committing:
  - `apps/quest-openxr-android/app/.cxx/...`
  - `logs/...`
  - `scripts/...` (if unrelated to this task)
