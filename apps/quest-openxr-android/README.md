# projectM Quest OpenXR App (Prototype)

This Android Studio project packages an experimental Quest-compatible `NativeActivity` that:

- runs an OpenXR render loop on Meta Quest,
- renders projectM into an offscreen texture,
- maps that texture onto the inside of a sphere for full-surround visuals,
- supports a front-dome projection mode.

## Status

This is an integration prototype, not a production-ready headset app yet.

- Audio input pipeline:
  - Supports explicit source switching in-headset: `system sound`, `internal player audio`, `microphone`, `synthetic`.
  - On startup it still prefers global output capture first.
  - Internal player beat detection prefers system-sound capture and falls back to media-session capture.
  - If no capture source is available, native synthetic audio fallback remains active.
- Presets:
  - Bundled starter presets still exist in APK assets.
  - Auto-downloads `presets-en-d` (around 50 presets) on first run.
  - Optional `presets-cream-of-the-crop` download can be enabled with a flag file.
- Runtime target: `arm64-v8a`.

## Prerequisites

1. Android Studio with SDK Platform 34 and NDK installed.
2. OpenXR SDK for Android (Khronos) installed with CMake package files available, and `OPENXR_SDK` exported in your shell.
3. A Quest headset in developer mode.

## Build

1. Open `apps/quest-openxr-android` in Android Studio.
2. Set the environment variable before launching Android Studio:

```bash
export OPENXR_SDK=/path/to/openxr-sdk-install-prefix
```

3. Build and run the `app` module on the headset.

The Gradle project invokes the root CMake build and enables `-DENABLE_QUEST_VR_APP=ON`.

## Release Build + Signing

Use the repository helper script for consistent Java/Gradle environment:

```bash
cd /home/here/Documents/swirl
export OPENXR_SDK=/path/to/openxr-sdk-install-prefix
./scripts/release-build.sh :app:assembleRelease
```

Release signing is optional and driven by environment variables (or Gradle properties with the same names):

```bash
export QUESTXR_RELEASE_STORE_FILE=/absolute/path/to/release-keystore.jks
export QUESTXR_RELEASE_STORE_PASSWORD='your-store-password'
export QUESTXR_RELEASE_KEY_ALIAS='your-key-alias'
export QUESTXR_RELEASE_KEY_PASSWORD='your-key-password'
```

When all four variables are set, `:app:assembleRelease` produces a signed APK:

- `apps/quest-openxr-android/app/build/outputs/apk/release/app-release.apk`

When signing values are missing, release output is unsigned:

- `apps/quest-openxr-android/app/build/outputs/apk/release/app-release-unsigned.apk`

The script also writes a checksum file next to the artifact (`.sha256`).

## GitHub Release Deployment

Build + publish in one command:

```bash
cd /home/here/Documents/swirl
./scripts/release-and-publish.sh v1.0.0
```

One-command upload helper:

```bash
cd /home/here/Documents/swirl
./scripts/github-release.sh v1.0.0
```

Optional explicit title/notes:

```bash
./scripts/github-release.sh v1.0.0 "QuestXR v1.0.0" "Production release"
./scripts/release-and-publish.sh v1.0.0 "QuestXR v1.0.0" "Production release"
```

Behavior:

- Prefers signed APK (`app-release.apk`) when present.
- Falls back to unsigned APK (`app-release-unsigned.apk`).
- Uses existing `.sha256` if present, otherwise generates one.
- Creates the release if tag does not exist; otherwise uploads/replaces assets on the existing release.

## Projection Modes

- Default: full-sphere (`360 x 180` mapping).
- Dome mode: set a debug property before launch:

```bash
adb shell setprop debug.projectm.quest.projection dome
```

To revert to full sphere:

```bash
adb shell setprop debug.projectm.quest.projection sphere
```

## Runtime Tuning (No Rebuild)

These properties are polled at runtime (about once per second):

```bash
# HUD placement/size
adb shell setprop debug.projectm.quest.hud.enabled 1
adb shell setprop debug.projectm.quest.hud.distance 0.72
adb shell setprop debug.projectm.quest.hud.v_offset -0.27
adb shell setprop debug.projectm.quest.hud.scale 1.0

# Performance guard
adb shell setprop debug.projectm.quest.perf.auto_skip 1
adb shell setprop debug.projectm.quest.perf.min_fps 42
adb shell setprop debug.projectm.quest.perf.bad_seconds 2.0
adb shell setprop debug.projectm.quest.perf.cooldown_seconds 8.0
adb shell setprop debug.projectm.quest.perf.skip_marked 1
adb shell setprop debug.projectm.quest.perf.mesh 64x48
```

Notes:

- Slow presets are auto-marked and persisted to internal app storage (`slow_presets.txt`) when FPS stays below threshold long enough.
- Marked presets are skipped during next/prev and timed auto-advance when `debug.projectm.quest.perf.skip_marked=1`.
- To clear all slow-preset marks:

```bash
adb shell setprop debug.projectm.quest.perf.clear_marked 1
adb shell setprop debug.projectm.quest.perf.clear_marked 0
```

## In-Headset UI + Controls

The app renders a head-locked control HUD in VR with color tiles plus text labels.

Primary XR interaction (recommended):

1. Touch HUD tiles directly with tracked hands (near-touch is prioritized when your hand is close).
2. You can still use aim + pinch for hand tracking, and ray + trigger for controllers.
3. For hand tracking, pinch only activates HUD-targeted input (off-HUD hand pinches are ignored so system gestures still work).
4. If HUD is hidden, controller trigger while aiming at HUD shows it; hand interactions activate when targeting/touching HUD.

Legacy controller bindings (still active):

1. Right `A`: next preset
2. Left `X`: previous preset
3. Left `Y`: play/pause internal player audio
4. Right `B`: next media track
5. Left thumbstick click (`L3`): previous media track
6. Right thumbstick click (`R3`): cycle audio input source (`global` -> `media` -> `mic` -> `synthetic`)
7. Right trigger pull (off-HUD): toggle sphere/dome projection
8. Left trigger pull (off-HUD): request optional Cream preset download and rescan presets

HUD behavior:

- HUD appears at launch and after controller/hand interaction, then auto-hides after a short timeout.
- When hand tracking is active, HUD distance is automatically reduced (up to `0.50m`) to make direct touch practical.
- When available, OpenXR hand-joint tracking (`XR_EXT_hand_tracking`) renders tracked hand skeleton/joints for direct-touch feedback.
- Left/right pointer cursors are rendered on the HUD when aim pose is active.

HUD status includes:

- audio mode (`synthetic`, `system sound`, `internal player audio`, `microphone`)
- projection mode (`sphere` or `dome`)
- playback state (`playing`/`paused`)
- current preset name
- current media track/source label
- recent controller input feedback (`INPUT: ...`) after button/trigger actions

## Optional Preset Pack

To enable download of `presets-cream-of-the-crop`, create this flag file and relaunch:

```bash
adb shell "mkdir -p /sdcard/Android/data/com.projectm.questxr/files && touch /sdcard/Android/data/com.projectm.questxr/files/enable_cream_download.flag"
```

## Internal Player Source

If global output capture is unavailable, the app tries these sources in order:

1. `media_source.txt` in app internal files (`http(s)` URL or absolute file path, first line only).
2. Audio files in app external music folder.
3. First audio file found in shared Music storage.

Recommended way to add local tracks:

```bash
adb shell "mkdir -p /sdcard/Android/data/com.projectm.questxr/files/Music"
adb push "/path/to/song1.flac" "/sdcard/Android/data/com.projectm.questxr/files/Music/"
adb push "/path/to/song2.mp3" "/sdcard/Android/data/com.projectm.questxr/files/Music/"
adb shell ls -lh "/sdcard/Android/data/com.projectm.questxr/files/Music"
```

If the HUD shows `internal_player_unavailable`, the player could not find any readable sources.

Local file scan currently includes: `mp3`, `m4a`, `aac`, `ogg`, `wav`, `flac`.

To set an explicit stream URL for internal player playback in debug builds:

```bash
adb shell "run-as com.projectm.questxr sh -c 'printf %s https://example.com/stream.mp3 > files/media_source.txt'"
```
