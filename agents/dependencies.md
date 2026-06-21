# Dependencies

- Use CMake and vcpkg manifest mode.
- Do not vendor SDL, ImGui, spdlog, GoogleTest, or a new Filament SDK into this repo.
- Use the Filament SDK path provided by `FILAMENT_SDK_ROOT`.
- Default Filament SDK path:
  `C:\Users\Drako\Desktop\quader-windows\_SOURCES\filament-v1.71.6-windows`.

## License Rules

Prefer proven existing libraries for geometry, mesh processing, spatial indexing, serialization,
editor tooling, and scripting when they solve the problem better.

Before choosing a dependency, verify its license permits use in the native Quader app without
requiring source or binaries to be released.

Avoid GPL, AGPL, LGPL with unclear linking obligations, SSPL, BUSL, copyleft, or source-available
dependencies unless explicitly approved.

Lua/sol2 or any scripting dependency must pass this check before integration.
