<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Testing And Build

## TDD

- Add or update GoogleTest coverage before implementing non-trivial core behavior.
- Keep tests fast and headless.
- Scene data, assets, descriptors, logger sinks, and viewport navigation should be testable without
  opening a window.
- GPU/window behavior is validated with manual smoke tests until a render-test harness exists.

## Expected Commands

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
ctest --preset vs2026-debug
```

Former SDK tests are migrated into the normal app test target. Do not add SDK-specific configure,
build, or test presets back to the native app.

If PowerShell inherits conflicting `PATH`/`Path` entries and CMake cannot find tools, use:

```powershell
$p = $env:Path
cmd /c "set PATH=& set Path=$p& cmake --build --preset vs2026-debug --target quader"
```

## Manual Smoke Checklist

- Vulkan backend initializes.
- Main SDL window opens without a separate console window.
- Dockspace appears.
- Console panel is docked in-app and receives log lines.
- Viewport panel docks/moves and renders app-created polygon meshes with the default material.
- Create and select a cube, capture the viewport, and verify selected/source mesh wires are visible
  with no giant viewport-sized overlay lines.
- MMB orbits, Shift+MMB pans, mouse wheel zooms.
