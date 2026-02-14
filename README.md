# projectM SeizureRobotzes

`projectM-seizureRobotzes` is a fork of [projectM](https://github.com/projectM-visualizer/projectm) focused on a playable Meta Quest OpenXR experience.

This repo keeps the upstream `libprojectM` codebase and adds an experimental Quest host app with in-headset controls, audio input switching, and runtime tuning.

*Status: It works for my needs. it's a vibecoded app. Not sure how long i'll need mention that before it's vibecoding is asumed for all things, but hey ChatGPT5.3-high codex originally wrote all the stuff. I mostly complained about choices and tested. I was going to learn C++ to do this. Well, I guess there isn't a point to that now is there. I can retain my ignorance of memory management :) enjoy!*

## Photosensitivity warning

projectM visuals can include high-contrast, rapidly changing patterns and flashes. If you are sensitive to flashing lights, use caution and stop immediately if you feel unwell. 

***LIKE SERIOUSLY: If you use this you will be essentially swimming in flashing patterns.***

## What is in this fork

- Meta Quest OpenXR `NativeActivity` app (`apps/quest-openxr-android`)
- Full-sphere and front-dome projection modes
- In-headset HUD with controller and hand-touch interaction
- Audio mode switching:
  - global output capture
  - Internal media player
  - microphone mode with beat-assist fallback
  - synthetic fallback
- Preset pack downloads in-app (`presets-en-d` by default, optional Cream pack)
- Runtime performance controls:
  - slow preset auto-marking/skip
  - SGSR upscaling toggle
  - adaptive render scaling

## Quick start (Quest app)

### 1. Prerequisites

- Android Studio with Android SDK Platform 34
- Android NDK installed (this project is currently configured and tested with NDK 27.x)
- OpenXR SDK for Android installed locally
- Quest headset in developer mode
- `adb` available on your host machine

### 2. Configure OpenXR SDK path

Use one of the following:

```bash
export OPENXR_SDK=/path/to/openxr-sdk-install-prefix
```

Or set `apps/quest-openxr-android/local.properties`:

```properties
openxr.sdk=/path/to/openxr-sdk-install-prefix
```

### 3. Build + install debug APK

```bash
cd apps/quest-openxr-android
./gradlew :app:installDebug
```

### 4. Launch on headset

```bash
adb shell am start -n com.projectm.questxr/.QuestNativeActivity
```

## Add Music For Internal Player

Copy your music files into the app music folder on Quest:

```bash
adb shell "mkdir -p /sdcard/Android/data/com.projectm.questxr/files/Music"
adb push "/path/to/song1.flac" "/sdcard/Android/data/com.projectm.questxr/files/Music/"
adb push "/path/to/song2.mp3" "/sdcard/Android/data/com.projectm.questxr/files/Music/"
```

Verify files are present:

```bash
adb shell ls -lh "/sdcard/Android/data/com.projectm.questxr/files/Music"
```

Notes:

- Internal player scans this folder first each time you switch to internal player mode.
- Supported file types include: `mp3`, `m4a`, `aac`, `ogg`, `wav`, `flac`.
- If you see `internal_player_unavailable`, it usually means no readable audio files were found.

## In-headset controls

Primary interaction:

- Touch HUD tiles directly with tracked hands
- Aim + pinch (hands) or ray + trigger (controllers) also work

Controller bindings:

- Right `A`: next preset
- Left `X`: previous preset
- Left `Y`: play/pause media fallback
- Right `B`: next media track
- Left thumbstick click (`L3`): previous media track
- Right thumbstick click (`R3`): cycle audio input source
- Right trigger (off-HUD): toggle sphere/dome projection
- Left trigger (off-HUD): request optional Cream preset download + rescan

## Runtime tuning (no rebuild)

Properties are polled at runtime (about once per second):

```bash
# Projection mode
adb shell setprop debug.projectm.quest.projection dome
adb shell setprop debug.projectm.quest.projection sphere

# HUD tuning
adb shell setprop debug.projectm.quest.hud.enabled 1
adb shell setprop debug.projectm.quest.hud.distance 0.72
adb shell setprop debug.projectm.quest.hud.v_offset -0.27
adb shell setprop debug.projectm.quest.hud.scale 1.0

# Performance + render scaling
adb shell setprop debug.projectm.quest.perf.sgsr 1
adb shell setprop debug.projectm.quest.perf.render_scale 0.85
adb shell setprop debug.projectm.quest.perf.auto_scale 1
adb shell setprop debug.projectm.quest.perf.auto_scale.min_render_scale 0.60
adb shell setprop debug.projectm.quest.perf.auto_scale.step 0.04
adb shell setprop debug.projectm.quest.perf.auto_scale.down_fps 66
adb shell setprop debug.projectm.quest.perf.auto_scale.up_fps 72
adb shell setprop debug.projectm.quest.perf.auto_scale.hold_seconds 1.0
adb shell setprop debug.projectm.quest.perf.auto_scale.cooldown_seconds 2.5

# Slow preset handling
adb shell setprop debug.projectm.quest.perf.auto_skip 1
adb shell setprop debug.projectm.quest.perf.skip_marked 1
adb shell setprop debug.projectm.quest.perf.min_fps 42
adb shell setprop debug.projectm.quest.perf.bad_seconds 2.0
adb shell setprop debug.projectm.quest.perf.cooldown_seconds 8.0

# Clear all slow-preset marks
adb shell setprop debug.projectm.quest.perf.clear_marked 1
adb shell setprop debug.projectm.quest.perf.clear_marked 0
```

Optional Cream preset pack enable flag:

```bash
adb shell "mkdir -p /sdcard/Android/data/com.projectm.questxr/files && touch /sdcard/Android/data/com.projectm.questxr/files/enable_cream_download.flag"
```

## Logs and troubleshooting

- Live logs:

```bash
adb logcat -s projectM-QuestXR
```

- Optional rotating log capture helper in this repo:

```bash
./scripts/quest_logcat_capture.sh start
./scripts/quest_logcat_capture.sh status
./scripts/quest_logcat_capture.sh stop
```

- If Gradle fails with OpenXR path errors, set `OPENXR_SDK` or `openxr.sdk` as shown above.

## Building libprojectM (non-Quest)

This fork still contains the full upstream `libprojectM` build system.

- Quick start: `BUILDING.md`
- Detailed CMake options: `BUILDING-cmake.md`

## Upstream and licensing

- Upstream project: [projectM-visualizer/projectm](https://github.com/projectM-visualizer/projectm)
- Main license text: `LICENSE.txt`
- Historical licensing notes: `COPYING`

If you use this fork, please link back to upstream projectM and credit the original maintainers.

## Syncing with upstream

Use this flow to pull updates from `projectM-visualizer/projectm` into this repo:

```bash
git fetch upstream
git checkout master
git merge --ff-only upstream/master
git push origin master
```

## More docs

- Quest app integration notes: `apps/quest-openxr-android/README.md`
- Core library build docs: `BUILDING.md`, `BUILDING-cmake.md`
