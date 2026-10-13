# Crimson Engine — crimson-renderer

C++20, SDL3, bgfx Direct3D 11, and Dear ImGui 1.92.8 docking. This slice provides a PBR sphere with image-based lighting (IBL) and normal maps, Quader navigation and grid, and a managed texture/environment library.

Project folder: `C:\Users\Drako\Desktop\_CRIMSON_ENGINE\crimson-engine`. The renderer is named **crimson-renderer**. Its C++ sources, shader sources, ImGui/bgfx adapter, and rendering diagnostics live together in [`src/crimson-renderer`](src/crimson-renderer).

The authoritative [engine architecture guide](docs/ENGINE_ARCHITECTURE.md) separates implemented features from agreed architecture and future work. IBL and normal maps are implemented. The next milestone is **static glTF/GLB import**, followed by **a static imported player/controller with WASD movement and mouse aiming**. EnTT, Jolt, and Play/Stop are planned runtime foundations; they are not implemented in this renderer slice yet.

## Run and develop

Double-click **Launch Renderer.cmd** to open the existing Release executable immediately. The launcher builds only when that executable is missing.

**Dev.cmd** incrementally builds Debug and opens it. Close the renderer before rebuilding code that changes its executable. From a command prompt:

```bat
Build.cmd Release
Build.cmd Release test
"Launch Renderer.cmd" Release
```

The `test` suffix runs focused CPU checks. Use `Build.cmd Release configure` after changing dependency or preset settings. Building requires MSVC x64, CMake and vcpkg; the script finds Visual Studio and uses `%VCPKG_ROOT%` or `%USERPROFILE%\vcpkg`. Routine builds reuse the local dependency cache. The executable is `build\Release\bin\crimson-renderer.exe`, with its runtime DLLs beside it.

The flat crimson emblem is available as a transparent 512×512 PNG in [`assets/branding/crimson-engine.png`](assets/branding/crimson-engine.png). Its multi-resolution Windows ICO is embedded in the executable for Explorer, desktop shortcuts, the taskbar, and the title bar. The ICO exporter fits the visible artwork closely to each icon size while preserving its proportions; the PNG's presentation margins do not shrink the taskbar icon. After replacing the PNG, run `.\scripts\Convert-AppIcon.ps1`, then rebuild. Successful links notify Windows to refresh the application's cached icon. Run `.\scripts\Create-DesktopShortcut.ps1` to create or refresh the **Crimson Engine** desktop shortcut to the Release executable; this also refreshes its cached icon. `.\scripts\Refresh-AppIcon.ps1` repeats just the targeted icon notification when needed.

## Editor panels

- **Viewport:** rendered image and navigation, following its dock size.
- **Settings:** material factors and maps, environment and direct lights, exposure and camera reset.
- **Asset Browser:** import, thumbnails, selection and drag-and-drop.

Drag tabs to rearrange or float panels inside the main window. Splitters resize them. Layout is retained in `crimson_ui.ini`; **Layout → Reset layout** restores the center viewport, right sidebar and bottom browser.

## Quader navigation

| Input over the viewport | Action |
| --- | --- |
| Middle-button drag | Orbit, using a sphere surface anchor when available |
| Shift + middle-button drag | Pan in the camera's right/up plane |
| Right-button drag | Fly look with a captured, hidden cursor |
| W/S while holding right button | Fly forward/back |
| A/D while holding right button | Strafe |
| Wheel | Zoom toward/away from the pivot |
| Wheel while flying | Adjust fly speed; default 25 units/second |
| Middle button while flying | Reset fly speed to 25 |
| Escape / focus loss | Cancel navigation and release capture |

Rotation, upright behavior, pan scale and reciprocal zoom follow Quader. The sphere rests at `(0,1,0)` on the XZ grid at `y=0`. The copied grid uses one-unit minor lines, two-unit major lines and Quader's colored axes. Its colors are independent of exposure.

## Import and assign textures

1. Click **Import textures…** in the Asset Browser. PNG, JPEG, TGA, BMP and Radiance HDR are supported; multiple files can be selected. HDR images are environment sources.
2. Files are copied into `assets/textures`. Collisions receive a suffix rather than overwriting a different file.
3. Use the texture picker directly under **Albedo**, **Roughness**, **Metalness**, or **Normal strength** in Settings. The picker shows searchable project textures; choosing one assigns it immediately. Dragging a thumbnail from the Asset Browser onto the texture field also assigns it.
4. Adjust tint/factors. Albedo, roughness and metalness maps multiply their factors; first assignment to an empty slot makes the factor neutral (white or 1). Replacing a map preserves the factor; **Clear** removes only the map. Normal assignment preserves its strength.

Albedo samples sRGB color. Roughness and metalness sample red-channel linear data. Normals sample linear RGB tangent-space data with **+Y/OpenGL green**, oriented along the mesh's increasing V direction. **Normal strength** ranges from 0 to 2; zero gives the original smooth surface, and a flat `(128,128,255)` map is neutral. **Reset material** clears maps and restores original values. Imports, material factors, normal strength and assignments survive restart through `assets/material.json` with project-relative paths; older material files remain readable.

Three tiny example textures are included from the earlier smoke check: checker albedo, roughness ramp and metalness stripes. Textures upload on first use and are cached; thumbnails preserve aspect ratio. HDR thumbnails use tone mapping for display without changing the imported source.

## Environment lighting

Open **Settings → Lights → Environment**. A built-in studio lights the sphere by default, including when all direct lights are disabled. Use the adjacent picker or drag an imported equirectangular panorama onto **Environment image**. Radiance HDR supplies linear HDR radiance; PNG/JPEG/TGA/BMP panoramas are decoded as sRGB color. **Clear** returns to the built-in studio.

Use **Enabled**, **Intensity** (0–10), and **Rotation** (−180° to 180° around world Y) to control the environment. Roughness changes specular reflection sharpness. Environment assignment and controls are stored separately from the material in `assets/environment.json`. Preparation happens on startup or source changes, with derived data in `assets/cache/ibl` for reuse; intensity/rotation changes reuse those resources. Bright HDR sources are filtered during preparation to avoid scattered reflection artifacts. Incompatible caches regenerate automatically after a filtering update, so the first load may take longer; source images and assignments are preserved.

Environment and direct lights accumulate in linear HDR before exposure and tone mapping. The grid and tools keep their own brightness. IBL supplies distant illumination and reflections; local reflected geometry, occlusion and shadows are outside this slice. Rough metals may darken from the GGX model's missing multiple scattering, and the split-sum approximation loses accuracy at grazing angles.

## Focused verification

Run `Build.cmd Release test` for focused CPU checks. For one capture after building, run `.\scripts\Verify-Renderer.ps1` from PowerShell. It captures the current project once; full image/scaling matrices are separate work. Actual build, test and visual results are recorded in [VERIFICATION.md](docs/VERIFICATION.md).

For the IBL/normal-map acceptance captures, run `.\scripts\Verify-Ibl.ps1 -Configuration Release`. It creates a disposable project under `build/verification`, checks native imports/persistence, captures the focused scenarios, and verifies that the authored material/environment/layout files remain unchanged. The latest recorded Release verification passed all 23 focused CPU check groups plus native and pixel comparisons, including a concentrated HDR emitter regression.

The executable accepts `--project-root PATH` to use another project's assets, persistence and layout. Native `--ibl-smoke` diagnostics require an explicit disposable project root because they create fixtures and change assignments. Run them in a scratch folder, followed by `--scenario ibl-reopen` with the same root to check reopening. Focused capture scenarios include `ibl-dielectric`, `ibl-metal`, `ibl-rough`, `ibl-neutral`, `ibl-normal`, `ibl-zero-normal` and `ibl-extreme`; normal scenarios use the smoke fixtures. Pair a scenario with `--frames COUNT` and optionally `--capture PATH`. The separate `--editor-smoke` mode also changes material assignments; it is intended for import/assignment verification.
