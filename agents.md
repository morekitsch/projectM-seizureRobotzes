# Agents Context + Handoff Tracker

Last updated: 2026-02-13

## Updated UX Plan (Active)
- This file is the single source of truth for UX planning updates.
- Any UX scope, priority, or acceptance change should be recorded here before implementation.

### Current Cycle Goals
1. Improve preset readability and scan speed.
- Show full names with overflow reveal/marquee behavior.
- Keep HUD legibility and stable layout spacing.

2. Make preset state controls explicit.
- Add `favorite` and `lock current preset` toggles.
- Ensure state is visible and easy to change from hand/ray/controller flows.

3. Simplify primary interaction surface.
- Move pack/download workflow into a secondary tab or sub-panel.
- Keep quick access, but reduce primary panel clutter.

### Exit Criteria
- Input parity is maintained across hand direct touch, hand ray + pinch, and controller shortcuts.
- No regressions in SGSR/runtime tuning or slow preset handling.
- New controls are discoverable without adding cognitive load to the main panel.

### Execution Status
- Goal 1 `preset readability`: in progress, first implementation landed in `src/quest-openxr/main.cpp`.
- Implemented behavior:
  - Keep longer preset labels (`kMaxPresetHudLabelChars = 160`) instead of hard clipping at 56 chars.
  - Add timed overflow marquee for the preset label line while keeping the existing HUD rect/spacing.
  - Keep `PRESET` prefix stable and scroll only the overflowing label content.
- Validation:
  - `./gradlew :app:assembleDebug` successful on 2026-02-13 after this change.
  - `./gradlew :app:installDebug` successful on 2026-02-13 (installed on Quest 3).
- Goal 2 `preset state controls`: in progress, first implementation landed in `src/quest-openxr/main.cpp`.
- Implemented behavior:
  - Added HUD toggle for starring the current preset (`STAR PRESET ON/OFF`) and lock control (`LOCK PRESET ON/OFF`) in the middle interaction band.
  - Added favorite persistence via `favorite_presets.txt` in app internal data.
  - Added favorites-only browsing mode in the `UTILS` panel center tile (`SHOW FAVS ON/OFF`).
  - Manual prev/next switching and timed auto-rotation now respect favorites-only filtering.
  - If favorites-only is enabled and favorites become empty, the filter auto-disables with HUD feedback.
  - Locked preset state now pauses automatic preset rotation and auto-skip (manual preset changes still work).
  - Track/feedback line now shows action feedback briefly, then returns to track label.
- Validation:
  - `./gradlew :app:assembleDebug` successful on 2026-02-13 after this change.
- Goal 3 `pack/download UX simplification`: in progress, first implementation landed in `src/quest-openxr/main.cpp`.
- Implemented behavior:
  - Bottom row now defaults to `UTILS / AUDIO MODE / PROJECTION` so `PACK` is not on the primary surface.
  - `UTILS` opens a secondary utility state where `PACK` is exposed; right button becomes `BACK`.
  - `PACK` request auto-closes the utility state after triggering download request.
  - Existing quick shortcut path (`LT`) is preserved when not consumed by HUD interaction.
- Validation:
  - `./gradlew :app:assembleDebug` successful on 2026-02-13 after this change.

## Snapshot
- App target: Meta Quest OpenXR `NativeActivity` now, with portability to other standalone OpenXR headsets and Linux-based targets later.
- UI decision: stay with internal in-world C++ HUD for next release. Do not add external toolkit yet.
- Reason: best portability/weight/complexity tradeoff for current scope.

## UX Direction (Approved)
- Keep internal HUD and extend it.
- Preserve consistent spacing and touch-first ergonomics.
- Maintain full parity across:
  - hand direct touch
  - ray + trigger/pinch
  - existing controller shortcuts

## Priority Backlog
1. Preset name readability
- Show full names with marquee or focused reveal on overflow.
- Remove hard truncation where practical while keeping HUD legible.

2. Preset state controls
- Add `favorite` toggle.
- Add `lock current preset` toggle to pause automatic/random preset changes.

3. Preset selection behavior
- Change from strict sequential rotation to weighted selection so favorites appear more often.
- Keep deterministic fallback path and avoid repeats clustering.

4. Slow preset management UX
- Keep auto-marking.
- Add visible `SLOW` indicator for current preset.
- Add archive/move action for marked presets so users can clean preset folders.

5. Pack/download UX simplification
- Move pack download off primary controls into a tab/sub-panel.
- Keep quick access possible, but not front-and-center.

## Portability Guidance
- Keep user-facing state logic in C++ (`src/quest-openxr/main.cpp`).
- Keep Java as Android service bridge only (downloads/audio/media in `QuestNativeActivity.java`).
- Avoid toolkit migration until internal HUD no longer supports required workflows.

## If Toolkit Is Revisited Later
- First candidate: Dear ImGui minimal subset.
- Exclude docking/multi-viewport.
- Keep OpenXR world-panel render + input routing owned by app.
- Do not adopt FlatUI (archived/stale risk).

## Existing Systems To Preserve
- SGSR upscaling + adaptive render scale runtime controls.
- Slow preset persistence (`slow_presets.txt`) and auto-skip path.
- Runtime property tuning flow via `adb shell setprop`.

## Regression Checklist
- Hand direct touch activation/hover/latch behavior.
- Hand ray + pinch fallback behavior.
- Controller ray + trigger interactions.
- Legacy direct controller bindings (`A/B/X/Y`, `L3/R3`, triggers).
- HUD spacing/alignment consistency after any new controls.

## Workspace Notes
- Ignore build/runtime noise when committing:
  - `apps/quest-openxr-android/app/.cxx/...`
  - `logs/...`
