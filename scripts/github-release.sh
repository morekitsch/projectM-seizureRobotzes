#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RELEASE_DIR="$ROOT_DIR/apps/quest-openxr-android/app/build/outputs/apk/release"
SIGNED_APK="$RELEASE_DIR/app-release.apk"
UNSIGNED_APK="$RELEASE_DIR/app-release-unsigned.apk"

usage() {
  cat <<'EOU'
Usage:
  ./scripts/github-release.sh <tag> [title] [notes]

Examples:
  ./scripts/github-release.sh v1.0.0
  ./scripts/github-release.sh v1.0.0 "QuestXR v1.0.0" "Production release"

Optional:
  GH_REPO=owner/repo ./scripts/github-release.sh v1.0.0
EOU
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

TAG="${1:-}"
TITLE="${2:-}"
NOTES="${3:-Production release}"

if [[ -z "$TAG" ]]; then
  echo "ERROR: Missing tag." >&2
  usage
  exit 1
fi

if [[ -z "$TITLE" ]]; then
  TITLE="QuestXR $TAG"
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "ERROR: GitHub CLI (gh) is not installed or not on PATH." >&2
  exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "ERROR: GitHub CLI is not authenticated. Run: gh auth login -h github.com" >&2
  exit 1
fi

resolve_repo() {
  if [[ -n "${GH_REPO:-}" ]]; then
    echo "$GH_REPO"
    return 0
  fi

  local origin_url repo
  if ! origin_url="$(git -C "$ROOT_DIR" remote get-url origin 2>/dev/null)"; then
    echo "ERROR: Could not read git origin remote. Set GH_REPO=owner/repo." >&2
    return 1
  fi

  case "$origin_url" in
    git@github.com:*)
      repo="${origin_url#git@github.com:}"
      ;;
    https://github.com/*)
      repo="${origin_url#https://github.com/}"
      ;;
    http://github.com/*)
      repo="${origin_url#http://github.com/}"
      ;;
    ssh://git@github.com/*)
      repo="${origin_url#ssh://git@github.com/}"
      ;;
    *)
      echo "ERROR: Unsupported origin URL '$origin_url'. Set GH_REPO=owner/repo." >&2
      return 1
      ;;
  esac

  repo="${repo%.git}"
  if [[ "$repo" != */* ]]; then
    echo "ERROR: Could not parse owner/repo from '$origin_url'. Set GH_REPO=owner/repo." >&2
    return 1
  fi

  echo "$repo"
}

REPO="$(resolve_repo)"

if [[ -f "$SIGNED_APK" ]]; then
  APK="$SIGNED_APK"
elif [[ -f "$UNSIGNED_APK" ]]; then
  APK="$UNSIGNED_APK"
else
  echo "ERROR: No release APK found. Run ./scripts/release-build.sh :app:assembleRelease first." >&2
  exit 1
fi

SHA_FILE="$APK.sha256"
if [[ ! -f "$SHA_FILE" ]]; then
  echo "Checksum file not found for $APK, generating $SHA_FILE"
  sha256sum "$APK" | tee "$SHA_FILE" >/dev/null
fi

echo "Using APK: $APK"
echo "Using checksum: $SHA_FILE"
echo "Using repository: $REPO"

if gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
  echo "Release $TAG exists. Uploading assets..."
  gh release upload "$TAG" "$APK" "$SHA_FILE" --clobber -R "$REPO"
else
  echo "Creating release $TAG..."
  gh release create "$TAG" "$APK" "$SHA_FILE" --title "$TITLE" --notes "$NOTES" -R "$REPO"
fi

echo "Done."
