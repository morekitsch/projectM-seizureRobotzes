# Handoff: projectM Library Integration (Apply to VR App)

This document captures the exact steps used in `qt6mplayer` to align with
upstream projectM guidance: link against the shared library, avoid vendoring,
and document the integration clearly.

## Goal

- Use upstream `libprojectM-4` shared library and official C API.
- Do not vendor/fork projectM source code in the app repo.
- Make it obvious in docs that projectM is linked, not embedded.
- Keep release artifacts portable by bundling the unmodified shared library.

## Checklist

### 1) Docs: Integration Policy

Create `docs/PROJECTM-INTEGRATION.md` with the following content:

```
# projectM Integration Policy

This repository is a standalone app that links to the upstream projectM shared
library (libprojectM-4). It does not vendor or fork projectM source code.

## Policy

- Use the official projectM C API and ABI for all rendering/audio integration.
- Do not copy or embed projectM source code in this repository.
- Prefer system-provided projectM packages during development.
- Release/AppImage bundles may include the shared library for portability, but the library is unmodified.
- If libprojectM changes are required, propose them upstream rather than in this repo.

## Compatibility

- The app targets the projectM 4.x C API. Rebuilds against newer 4.x releases should remain
  compatible as long as the upstream ABI contract holds.
- When upstream publishes API changes in 4.x, update integration in this repo and document
  the required minimum version in README.
```

### 2) Docs: README Section

Add a short section near the top of README:

```
## projectM Integration

- This app links against the upstream `libprojectM-4` shared library and uses the official C API.
- projectM source code is not vendored or forked in this repository.
- Releases bundle the shared library for portability, but the library is unmodified.

Details: `docs/PROJECTM-INTEGRATION.md`
```

### 3) Build System: Ensure System Linking

Confirm the build uses the system library and headers:

- `pkg-config` module: `projectM-4`
- Headers: `#include <projectM-4/...>`
- Linking via `find_library(projectM-4)` or `pkg-config` link flags
- Optional `HAVE_PROJECTM` definition for compile-time toggles

If the repo vendors projectM sources or submodules, remove them and update
build scripts to use the system library instead.

### 4) Packaging: Bundle Unmodified Shared Library

For AppImage or other portable release formats:

- Ensure `libprojectM-4.so.4` is included in the bundle (copied into `AppDir/usr/lib`).
- Confirm rpath is set to resolve bundled libs (e.g., `$ORIGIN/../lib`).

### 5) Verify

- Build locally with projectM dev package installed.
- Run the app and verify it reports a projectM renderer backend (not fallback).
- Confirm the release artifact includes `libprojectM-4.so.4`.

## Notes

- You do not need any git submodule or fork relationship with the projectM repo.
- The correct integration is linking to the shared library and using the official C API.
- This keeps upgrades easy across projectM 4.x releases.
