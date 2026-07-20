<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# CMake Guide

Follow root `AGENTS.md` dependency, SDK-boundary, versioning, and build/test rules before changing
CMake dependency or build behavior.

## CMake Rules

- Use CMake and vcpkg manifest mode.
- Do not add SDK-specific configure/build/test presets back to the native app.
- Keep network dependency fetches opt-in and backed by license evidence under `thirdparty/`.
- When adding `.cpp` files, wire them into the correct target rather than relying on broad globs
  unless the target already intentionally uses one.
- Preserve the one canonical multi-config tree and its presets: `vs2026`, `vs2026-debug`,
  `vs2026-release`, and `ctest --preset vs2026-debug`. Do not add alternate native build trees.
- Keep Tracy availability forced on at configure time, then gate its compile definitions and link
  dependency with `CONFIG:Debug`. A cache-wide Tracy setting must never instrument Release.
- Keep Debug optimized for representative Tracy captures while retaining embedded symbols and
  Tracy. It must map imported dependencies to Release, use the non-Debug CRT, define `NDEBUG` and
  unchecked STL iterators, and link Filament `md` rather than `mdd`/`matdbg`; otherwise captures
  measure Debug-library overhead instead of product behavior. Keep Release free of Tracy and
  configured for whole-program, maximum-speed optimization. Keep optional Sentry instrumentation
  disabled in the canonical preset unless the user explicitly changes the build contract.
- Suppress vcpkg's configuration-name-based Debug app-local hook for `quader`. Package both native
  artifacts from the Release dependency directory with the recursive vcpkg app-local resolver and
  `windeployqt --force --release`; `$<TARGET_RUNTIME_DLLS>` alone does not include Qt's complete
  private dependency closure.
- Treat the `Debug` configuration name as the canonical Profile-build compatibility name. It is not
  a traditional checked Debug configuration in the two-artifact workflow; Release remains the only
  shipping artifact.
