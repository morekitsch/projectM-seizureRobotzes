#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
  cat <<'EOF'
Usage:
  ./scripts/release-and-publish.sh <tag> [title] [notes]

Examples:
  ./scripts/release-and-publish.sh v1.0.0
  ./scripts/release-and-publish.sh v1.0.0 "QuestXR v1.0.0" "Production release"
EOF
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

cd "$ROOT_DIR"

echo "Building release APK..."
./scripts/release-build.sh :app:assembleRelease

echo "Publishing GitHub release..."
if [[ -n "$TITLE" ]]; then
  ./scripts/github-release.sh "$TAG" "$TITLE" "$NOTES"
else
  ./scripts/github-release.sh "$TAG"
fi

echo "Done."
