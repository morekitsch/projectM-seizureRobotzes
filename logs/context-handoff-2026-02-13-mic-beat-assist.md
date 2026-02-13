# Quest mic beat-assist handoff (2026-02-13)

## Goal
Make Quest microphone mode react to room music even when hardware voice processing suppresses far-field signal.

## What was implemented
- `QuestNativeActivity` microphone path now probes multiple sources/channels and disables AGC/NS/AEC when available.
- Added aggressive adaptive gain for very weak residual mic signal.
- Added beat-assist synthesis in mic mode:
  - Onset-triggered pulse when residual RMS rises above dynamic threshold.
  - Fallback tempo pulses (~118 BPM) after silence so visuals keep moving.
  - Injected short kick-like component into outgoing PCM so projectM receives beat transients.
- Added diagnostics logs every ~2s:
  - `Mic level rms=... peak=... floor=... thr=... pulse=... adaptive=... total=... channels=...`

## Current observed behavior
- Build is active when logs include `Using microphone capture v3 source=...` and the `Mic level ...` lines.
- On Quest 3, measured far-field RMS is extremely low (likely hardware voice gating), but beat-assist still drives visuals.
- Beat injection strength was tuned slightly up (`MICROPHONE_BEAT_INJECT_GAIN = 0.38f`).

## Primary tuning knobs
File: `apps/quest-openxr-android/app/src/main/java/com/projectm/questxr/QuestNativeActivity.java`

- `MICROPHONE_BEAT_INJECT_GAIN` (strength)
- `MICROPHONE_BEAT_DECAY_PER_SAMPLE` (pulse tail length)
- `MICROPHONE_BEAT_FALLBACK_BPM` (fallback tempo)
- `MICROPHONE_ONSET_FLOOR_MULTIPLIER` and `MICROPHONE_ONSET_FLOOR_OFFSET` (onset sensitivity)
- `MICROPHONE_GAIN`, `MICROPHONE_TARGET_RMS`, `MICROPHONE_MAX_ADAPTIVE_GAIN` (overall gain behavior)

## Quick verification
1. `./gradlew :app:installDebug`
2. Launch app and switch to microphone mode.
3. `adb logcat -c && adb logcat -s projectM-QuestXR`
4. Confirm `Using microphone capture v3 ...` and `Mic level ... pulse=...` lines.

## Known limitation
This is not true raw room-audio capture. It is a practical beat-reactive fallback for voice-gated headset mic input.
