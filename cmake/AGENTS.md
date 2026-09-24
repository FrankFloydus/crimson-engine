<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# CMake Guide

Root [AGENTS.md](../AGENTS.md) owns the Release-only default handoff and version contract.
[tools/AGENTS.md](../tools/AGENTS.md) lists operational commands; [tests/AGENTS.md](../tests/AGENTS.md)
owns verification selection.

## CMake Rules

- Use CMake and vcpkg manifest mode.
- Do not add SDK-specific configure/build/test presets back to the native app.
- Keep network dependency fetches opt-in and backed by license evidence under `thirdparty/`.
- When adding `.cpp` files, wire them into the correct target rather than relying on broad globs
  unless the target already intentionally uses one.
- Preserve the one canonical multi-config tree and its presets: `vs2026`, `vs2026-debug`,
  `vs2026-release`, and `ctest --preset vs2026-debug`. Do not add alternate native build trees.
- Build the Release app for normal handoff; build the Debug/Profile app only on explicit request.
  Debug test targets remain available without building the Debug app. Configure when CMake inputs
  change. Source-list-only edits need the affected targets/tests, not a full SDK install fixture.
- `QuaderBuildOptions.cmake` owns options and dependency locations. `QuaderModeling.cmake` owns
  portable SDK targets, focused modeling test targets, SDK installation, and native-only Tracy
  integration. `QuaderSdkOnlyBuildCheck.cmake` verifies an isolated SDK install and external consumer.
- Gate Tracy definitions and links with `CONFIG:Debug`, despite configure-time availability being
  forced on. Map Debug imports to Release and preserve `/MD`, `NDEBUG`, unchecked iterators, `/O2`,
  symbols, and Filament `md`. Release needs whole-program optimization, no debug information, and
  no Tracy link or definitions. Canonical presets disable Sentry.
- `quader` is the Full executable. `quader_public_app` produces `quader-0.1.exe`; its release
  number is independent of `VERSION`. Shared native libraries link the release catalog/policy,
  never a profile. Each final native consumer links exactly one `quader_release_profile_full`
  or `quader_release_profile_public` object target. The portable SDK has neither dependency.
- Suppress vcpkg's configuration-name-based Debug app-local hook for both native app targets. Package both native
  artifacts from the Release dependency directory with the recursive vcpkg app-local resolver and
  `windeployqt --force --release`; `$<TARGET_RUNTIME_DLLS>` alone does not include Qt's complete
  private dependency closure.
- Public app deployment depends on Full app deployment to serialize shared runtime-directory
  cleanup. The restricted test executable likewise depends on `quader_tests`; do not remove
  that ordering while their deployment steps share a directory.
- Build `quader_fatal_diagnostics` with both native hosts and deploy it beside their executables.
  It links only Windows diagnostics/system libraries; SDK-only configurations must exclude it.
- `QuaderFilamentSdk.cmake` validates the pinned Quader patch and installed header/library hashes
  before native linking. Rebuild matching headers, tools, and libraries together; follow
  [thirdparty/filament/README.md](../thirdparty/filament/README.md) for dependency upgrades.
- SDK-only checks may use their isolated verification builds. They must remain free of native app
  dependencies and must not introduce alternate native handoff trees.
