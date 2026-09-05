<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# CMake Guide

Root [AGENTS.md](../AGENTS.md) owns the dual-configuration build and version contract.
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
- `QuaderBuildOptions.cmake` owns options and dependency locations. `QuaderModeling.cmake` owns
  portable SDK targets, focused modeling test targets, SDK installation, and native-only Tracy
  integration. `QuaderSdkOnlyBuildCheck.cmake` verifies an isolated SDK install and external consumer.
- Gate Tracy definitions and links with `CONFIG:Debug`, despite configure-time availability being
  forced on. Map Debug imports to Release and preserve `/MD`, `NDEBUG`, unchecked iterators, `/O2`,
  symbols, and Filament `md`. Release needs whole-program optimization, no debug information, and
  no Tracy link or definitions. Canonical presets disable Sentry.
- Suppress vcpkg's configuration-name-based Debug app-local hook for `quader`. Package both native
  artifacts from the Release dependency directory with the recursive vcpkg app-local resolver and
  `windeployqt --force --release`; `$<TARGET_RUNTIME_DLLS>` alone does not include Qt's complete
  private dependency closure.
- Build `quader_fatal_diagnostics` with both native hosts and deploy it beside their executables.
  It links only Windows diagnostics/system libraries; SDK-only configurations must exclude it.
- `QuaderFilamentSdk.cmake` validates the pinned Quader patch and installed header/library hashes
  before native linking. Rebuild matching headers, tools, and libraries together; follow
  [thirdparty/filament/README.md](../thirdparty/filament/README.md) for dependency upgrades.
- SDK-only checks may use their isolated verification builds. They must remain free of native app
  dependencies and must not introduce alternate native handoff trees.
