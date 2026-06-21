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

SDK tests:

```powershell
cmake --preset vs2026-sdk-tests
cmake --build --preset vs2026-sdk-tests-debug
ctest --preset vs2026-sdk-tests-debug --output-on-failure
```

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
- Viewport panel docks/moves and renders Quader SDK-created meshes with the default material.
- Create and select a cube, capture the viewport, and verify selected/source mesh wires are visible
  with no giant viewport-sized overlay lines.
- MMB orbits, Shift+MMB pans, mouse wheel zooms.
