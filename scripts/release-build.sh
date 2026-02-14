#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ANDROID_DIR="$ROOT_DIR/apps/quest-openxr-android"

if [[ -z "${JAVA_HOME:-}" ]]; then
  if [[ -d "/opt/android-studio/jbr" ]]; then
    export JAVA_HOME="/opt/android-studio/jbr"
  elif [[ -d "/usr/lib/jvm/java-21-openjdk" ]]; then
    export JAVA_HOME="/usr/lib/jvm/java-21-openjdk"
  elif [[ -d "/usr/lib/jvm/java-17-openjdk" ]]; then
    export JAVA_HOME="/usr/lib/jvm/java-17-openjdk"
  else
    echo "ERROR: No compatible JDK found. Set JAVA_HOME to JDK 21 (preferred) or JDK 17." >&2
    exit 1
  fi
fi

export PATH="$JAVA_HOME/bin:$PATH"
export GRADLE_USER_HOME="${GRADLE_USER_HOME:-$ROOT_DIR/.gradle}"

echo "Using JAVA_HOME=$JAVA_HOME"
java -version

cd "$ANDROID_DIR"

TASK="${1:-:app:assembleRelease}"
./gradlew --stop >/dev/null 2>&1 || true
./gradlew "$TASK"

SIGNED_APK="$ANDROID_DIR/app/build/outputs/apk/release/app-release.apk"
UNSIGNED_APK="$ANDROID_DIR/app/build/outputs/apk/release/app-release-unsigned.apk"

if [[ -f "$SIGNED_APK" ]]; then
  ARTIFACT="$SIGNED_APK"
elif [[ -f "$UNSIGNED_APK" ]]; then
  ARTIFACT="$UNSIGNED_APK"
else
  echo "ERROR: Release APK not found under app/build/outputs/apk/release" >&2
  exit 1
fi

echo "Release artifact: $ARTIFACT"
sha256sum "$ARTIFACT" | tee "$ARTIFACT.sha256"

echo "Done."
