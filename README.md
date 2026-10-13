# Crimson Engine — crimson-renderer

C++20, SDL3, bgfx Direct3D 11, EnTT and Dear ImGui docking. The editor provides native scene assets, independently placed static glTF/GLB models, directional/point-light entities, a dockable Outliner, ImGuizmo transforms, Quader navigation and one shared asset library. Dear ImGui uses its default colors, font and sizing.

Project folder: `C:\Users\Drako\Desktop\_CRIMSON_ENGINE\crimson-engine`. The renderer is named **crimson-renderer**. Its C++ sources, shader sources, ImGui/bgfx adapter, and rendering diagnostics live together in [`src/crimson-renderer`](src/crimson-renderer).

The authoritative [engine architecture guide](docs/ENGINE_ARCHITECTURE.md) separates implemented features from agreed architecture and future work. **Scene authoring with EnTT, hierarchy and undo/redo** is implemented and awaiting user acceptance. Editable imported materials are next, followed by **a static imported player/controller with WASD movement and mouse aiming**. Jolt and Play/Stop remain planned runtime work.

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

- **Viewport:** rendered scene, model/light selection, transform gizmo and navigation, following its dock size.
- **Outliner:** rootless scene hierarchy, multiple selection, inline renaming and drag-to-reparent. Right-click an entity or empty space for creation, grouping, duplication and deletion.
- **Inspector:** selected entity name and editable local transform matrix. Matrix fields edit the primary entity and preserve shear/reflection; its final affine row remains fixed.
- **Settings:** selected light properties and read-only model information, plus scene environment and exposure.
- **Asset Browser:** one **Import** button and one shared collection for models, textures, environments and saved scenes.

Drag tabs to rearrange or float panels inside the main window. Splitters resize them. Layout is retained in `crimson_ui.ini`; **Layout → Reset layout** restores the default editor arrangement.

## Scenes and entities

Create a scene with **Scene → New scene** or the Asset Browser's **New scene** action, then drag model cards into the viewport to add independent entities. New scenes start empty with the built-in environment. Repeated placements share the imported model and GPU geometry while keeping separate transforms. Importing an asset only adds it to the browser. An imported model's internal node hierarchy and materials remain unchanged.

Use **Scene → Save** to save the current scene; **Save as...** creates a separate scene asset. Scenes are stored under `assets/scenes` as versioned `.crimson-scene` files and appear beside other assets. Double-click a scene card, or choose **Open scene** from its context menu, to open it. **Delete scene...** removes its scene file while shared models, textures and other scenes remain available. Switching or closing a dirty scene offers Save, Discard and Cancel.

Select models by clicking the viewport or Outliner. Directional and point lights are entities with selectable viewport icons. Ctrl-click toggles individual entities; Shift-click in the Outliner selects a range. Use F2 or Rename to edit a name inline. Drag selected Outliner entries onto a parent to build a hierarchy, or onto empty background space to unparent. Group/Ungroup are context actions and preserve world placement. Reparenting preserves world placement, including mirrored and nonuniform transforms. Deleting a parent deletes its subtree; undo restores the subtree and its stable IDs.

| Input in the active scene editor | Action |
| --- | --- |
| T | Move the current selection |
| R | Rotate the current selection |
| E | Scale the current selection |
| F | Focus the camera on the current selection |
| Delete | Delete selected entities and their descendants |
| Ctrl + D | Duplicate selected subtrees |
| Ctrl + Z | Undo |
| Ctrl + Y or Ctrl + Shift + Z | Redo |
| Escape during a gizmo drag | Cancel the drag |

Shortcuts respect text entry, widgets and viewport navigation. A single selected entity uses its origin as the gizmo pivot; multiple selections use their combined bounds center. Selected descendants follow a selected ancestor once. Each completed gizmo/property drag is one undo operation. Undo/redo keeps up to 256 scene commands, and the dirty marker follows the saved state even when undo creates a different editing branch. Selection, camera movement and focusing do not change saved scene content.

Scenes contain their own environment and exposure settings. Right-click the Outliner and use **Add** to create empty, directional-light or point-light entities. Add up to four directional lights and sixteen point lights; edit their color, intensity and enabled state through the selected entity. Light position/direction comes from its transform. Directional lighting and its viewport arrow use the same normalized transformed local -Z axis. Scene loading preserves missing model references with diagnostics so saving cannot silently erase them. `assets/editor-session.json` stores the last saved scene and editor camera per scene, separate from scene content.

## Quader navigation

| Input over the viewport | Action |
| --- | --- |
| Middle-button drag | Orbit around the camera target |
| Shift + middle-button drag | Pan in the camera's right/up plane |
| Right-button drag | Fly look with a captured, hidden cursor |
| W/S while holding right button | Fly forward/back |
| A/D while holding right button | Strafe |
| Wheel | Zoom toward/away from the pivot |
| Wheel while flying | Adjust fly speed; default 25 units/second |
| Middle button while flying | Reset fly speed to 25 |
| Escape / focus loss | Cancel navigation and release capture |

Rotation, upright behavior, pan scale and reciprocal zoom follow Quader. The XZ grid is at `y=0`, with one-unit minor lines, two-unit major lines and Quader's colored axes. Its colors are independent of exposure. Transform dragging owns its input so manipulating an entity does not move the camera simultaneously.

The viewport has no transform toolbar or drag-status text. T/R/E switch gizmo operations and F frames selection.

The Asset Browser uses equally sized cards for every kind, with aspect-fit thumbnails and a border only around selected cards. Ctrl/Shift extend selection, Ctrl+A selects the visible folder, and F2 renames inline. Create folders through New Folder or the context menu; drag selected assets onto folders or breadcrumbs to organize them. Double-click folders to enter and use Up/Backspace to return. Dragging several selected models into the viewport places them as one undoable operation. Folder organization and display names live in `assets/browser.json`; these operations preserve managed files and asset references.

## Import and assign textures

1. Click **Import** in the Asset Browser. PNG, JPEG, TGA, BMP and Radiance HDR are supported alongside glTF/GLB models; multiple files and mixed asset types can be selected. HDR images are environment sources. Each file imports independently, with failures reported in the browser.
2. Files are copied into `assets/textures`. Collisions receive a suffix rather than overwriting a different file.
3. Existing sphere material controls retain their texture pickers under **Albedo**, **Roughness**, **Metalness**, and **Normal strength** in the legacy preview/diagnostic mode. Picking or dragging a compatible thumbnail onto a field assigns it immediately. Scene environment fields accept compatible environment images.
4. Adjust tint/factors. Albedo, roughness and metalness maps multiply their factors; first assignment to an empty slot makes the factor neutral (white or 1). Replacing a map preserves the factor; **Clear** removes only the map. Normal assignment preserves its strength.

Albedo samples sRGB color. Roughness and metalness sample red-channel linear data. Normals sample linear RGB tangent-space data with **+Y/OpenGL green**, oriented along the mesh's increasing V direction. **Normal strength** ranges from 0 to 2; zero gives the original smooth surface, and a flat `(128,128,255)` map is neutral. **Reset material** clears maps and restores original values. Imports, material factors, normal strength and assignments survive restart through `assets/material.json` with project-relative paths; older material files remain readable.

Three tiny example textures are included from the earlier smoke check: checker albedo, roughness ramp and metalness stripes. Textures upload on first use and are cached; thumbnails preserve aspect ratio. HDR thumbnails use tone mapping for display without changing the imported source.

## Import and place models

1. Click the same **Import** button and select a `.gltf` or `.glb` file. The importer copies the model and required buffers/images into a managed package under `assets/models`. Images, environments and models appear together in the Asset Browser.
2. Drag the model's card onto the viewport to place a scene entity. The model keeps its authored scale, orientation and internal node transforms; placement changes only its scene root.
3. Drop additional models to add more entities. Select a placed model, use T/R/E to transform it and F to focus it. Save the scene to retain placements across restarts.

Managed models reopen even after their original source folder moves or disappears. Failed imports leave the working scene intact. Imported materials are read-only in this slice and retain their authored appearance. The older `assets/preview.json`, sphere material and environment files remain preserved for compatibility; scenes use their own versioned persistence.

Supported models use static triangle geometry, hierarchy transforms, UV0/UV1, vertex colors, metallic-roughness PBR, normal maps, emissive, occlusion, double-sided surfaces and opaque/alpha-cutout materials. Missing normals are generated flat; missing normal-map tangents use MikkTSpace. Texture wrapping and nearest/linear filtering are retained; mipmap filters currently fall back to non-mipmapped filtering with a warning.

Translucent (`BLEND`) materials, skins, morph targets, points/lines and required unsupported extensions are rejected with diagnostics. Animation playback, imported cameras/lights and optional unsupported extensions are ignored with warnings. Draco/meshopt compression, KTX2 and advanced material extensions require later support. Imported-material editing remains the next model-authoring slice.

## Environment lighting

Use the scene environment controls in Settings. A built-in studio supplies illumination by default, including when all direct lights are disabled. Use the adjacent picker or drag an imported equirectangular panorama onto **Environment image**. Radiance HDR supplies linear HDR radiance; PNG/JPEG/TGA/BMP panoramas are decoded as sRGB color. **Clear** returns to the built-in studio.

Use **Enabled**, **Intensity**, and **Rotation** around world Y to control the environment. Roughness changes specular reflection sharpness. Each scene saves its environment assignment and settings; the legacy sphere environment remains in `assets/environment.json`. Preparation happens on startup or source changes, with derived data in `assets/cache/ibl` for reuse; intensity/rotation changes reuse those resources. Bright HDR sources are filtered during preparation to avoid scattered reflection artifacts. Incompatible caches regenerate automatically after a filtering update, so the first load may take longer; source images and assignments are preserved.

Environment and direct lights accumulate in linear HDR before exposure and tone mapping. The grid and tools keep their own brightness. IBL supplies distant illumination and reflections; local reflected geometry, occlusion and shadows are outside this slice. Rough metals may darken from the GGX model's missing multiple scattering, and the split-sum approximation loses accuracy at grazing angles.

## Build checks and user acceptance

Run `Build.cmd Release test` for focused CPU checks, including math, managed imports, scene persistence, affine hierarchy, entity history and failure handling. Implementation handoffs report the actual build/test results and leave feature acceptance to the user. There is no active `VERIFICATION.md` maintenance workflow. PC interaction and input automation require the user's permission; noninteractive screenshots are allowed.

Existing capture scripts remain optional manual diagnostics, not a feature-completion requirement. `.\scripts\Verify-Renderer.ps1` captures the current project after a build. `.\scripts\Verify-Ibl.ps1 -Configuration Release` runs isolated IBL/normal-map diagnostics under `build/verification`.

The executable accepts `--project-root PATH` to use another project's assets, persistence and layout. Native `--ibl-smoke` diagnostics require an explicit disposable project root because they create fixtures and change assignments. Run them in a scratch folder, followed by `--scenario ibl-reopen` with the same root to check reopening. Focused capture scenarios include `ibl-dielectric`, `ibl-metal`, `ibl-rough`, `ibl-neutral`, `ibl-normal`, `ibl-zero-normal` and `ibl-extreme`; normal scenarios use the smoke fixtures. Pair a scenario with `--frames COUNT` and optionally `--capture PATH`. The separate `--editor-smoke` mode also changes material assignments; it is intended for import/assignment verification.

`.\scripts\Verify-Models.ps1 -Configuration Release` retains the older isolated model-preview diagnostics. Its `--model-smoke` mode requires a disposable `--project-root`; `--scenario model-reopen` and `--scenario model-sphere` exercise the legacy preview path. They are not the scene-authoring acceptance workflow.
