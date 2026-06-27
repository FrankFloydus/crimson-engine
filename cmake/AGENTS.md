<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# CMake Guide

Read `../agents/dependencies.md` before changing CMake dependency or build behavior.

## CMake Rules

- Use CMake and vcpkg manifest mode.
- Do not add SDK-specific configure/build/test presets back to the native app.
- Keep network dependency fetches opt-in and backed by license evidence under `thirdparty/`.
- When adding `.cpp` files, wire them into the correct target rather than relying on broad globs
  unless the target already intentionally uses one.
- Preserve the expected build and test presets: `vs2026`, `vs2026-debug`, and `ctest --preset
  vs2026-debug`.
- Optional local acceleration presets such as `ninja-msvc` must use their own `build/` subdirectory
  and must not hardcode machine-specific compiler paths.
