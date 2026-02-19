#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECTM_SRC="${PROJECTM_SRC:-$ROOT_DIR/projectm}"
PROJECTM_BUILD_DIR="${PROJECTM_BUILD_DIR:-$PROJECTM_SRC/build-android-arm64}"
PROJECTM_ANDROID_SDK_DIR="${PROJECTM_ANDROID_SDK_DIR:-$ROOT_DIR/projectm-android-sdk}"
ANDROID_API_LEVEL="${ANDROID_API_LEVEL:-29}"

usage() {
  cat <<'EOF'
Usage:
  ./scripts/build-projectm-android-sdk.sh

Environment overrides:
  PROJECTM_SRC=/path/to/projectm-source
  PROJECTM_BUILD_DIR=/path/to/build-dir
  PROJECTM_ANDROID_SDK_DIR=/path/to/install-prefix
  ANDROID_NDK_ROOT=/path/to/android/ndk
  ANDROID_API_LEVEL=29
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ ! -f "$PROJECTM_SRC/CMakeLists.txt" ]]; then
  echo "ERROR: projectM source not found at $PROJECTM_SRC" >&2
  echo "Set PROJECTM_SRC to your projectM checkout root." >&2
  exit 1
fi

if [[ ! -f "$PROJECTM_SRC/vendor/projectm-eval/CMakeLists.txt" ]]; then
  echo "ERROR: Missing projectM submodule 'vendor/projectm-eval'." >&2
  echo "Run: cd $PROJECTM_SRC && git submodule update --init --recursive" >&2
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "ERROR: cmake is required but not found on PATH." >&2
  exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
  echo "ERROR: ninja is required but not found on PATH." >&2
  exit 1
fi

if [[ -z "${ANDROID_NDK_ROOT:-}" ]]; then
  if [[ -n "${ANDROID_NDK:-}" ]]; then
    ANDROID_NDK_ROOT="$ANDROID_NDK"
  elif [[ -d "$HOME/Android/Sdk/ndk" ]]; then
    ANDROID_NDK_ROOT="$(ls -1d "$HOME"/Android/Sdk/ndk/* 2>/dev/null | sort -V | tail -n1)"
  fi
fi

if [[ -z "${ANDROID_NDK_ROOT:-}" || ! -f "$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" ]]; then
  echo "ERROR: ANDROID_NDK_ROOT is not set to a valid NDK path." >&2
  echo "Set ANDROID_NDK_ROOT to a directory containing build/cmake/android.toolchain.cmake." >&2
  exit 1
fi

echo "Using projectM source: $PROJECTM_SRC"
echo "Using build dir:       $PROJECTM_BUILD_DIR"
echo "Using install prefix:  $PROJECTM_ANDROID_SDK_DIR"
echo "Using NDK:             $ANDROID_NDK_ROOT"
echo "Using Android API:     $ANDROID_API_LEVEL"

mkdir -p "$PROJECTM_BUILD_DIR" "$PROJECTM_ANDROID_SDK_DIR"

cmake -S "$PROJECTM_SRC" -B "$PROJECTM_BUILD_DIR" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM="android-${ANDROID_API_LEVEL}" \
  -DCMAKE_INSTALL_PREFIX="$PROJECTM_ANDROID_SDK_DIR" \
  -DBUILD_SHARED_LIBS=ON \
  -DENABLE_SDL_UI=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_DOCS=OFF \
  -DENABLE_PLAYLIST=ON

cmake --build "$PROJECTM_BUILD_DIR" --target install --config Release

if [[ ! -f "$PROJECTM_ANDROID_SDK_DIR/include/projectM-4/projectM.h" ]]; then
  echo "ERROR: install finished but headers were not found at expected path." >&2
  exit 1
fi

if [[ ! -f "$PROJECTM_ANDROID_SDK_DIR/lib/libprojectM-4.so" ]]; then
  echo "ERROR: install finished but libprojectM-4.so was not found at expected path." >&2
  exit 1
fi

echo
echo "projectM Android SDK ready:"
echo "  $PROJECTM_ANDROID_SDK_DIR"
echo
echo "Next:"
echo "  export PROJECTM4_SDK=\"$PROJECTM_ANDROID_SDK_DIR\""
echo "  ./scripts/release-build.sh :app:assembleRelease"
