<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Dependencies

- Use CMake and vcpkg manifest mode.
- Do not vendor SDL, UI backend libraries, spdlog, GoogleTest, or a new Filament SDK into this repo.
- Use the Filament SDK path provided by `FILAMENT_SDK_ROOT`.
- Default Filament SDK path:
  `C:\Users\Drako\Desktop\quader-windows\_SOURCES\filament-v1.71.6-windows`.
- Keep dependency version notes and license evidence under `thirdparty/`, especially
  `thirdparty/THIRD_PARTY_MANIFEST.md` and `thirdparty/licenses/`. Do not add a separate SDK
  license-evidence tree.

## License Rules

Prefer proven existing libraries for geometry, mesh processing, spatial indexing, serialization,
editor tooling, and scripting when they solve the problem better.

Before choosing a dependency, verify its license permits use in the native Quader app without
requiring source or binaries to be released.

Avoid GPL, AGPL, LGPL with unclear linking obligations, SSPL, BUSL, copyleft, or source-available
dependencies unless explicitly approved.

Lua/sol2 or any scripting dependency must pass this check before integration.
