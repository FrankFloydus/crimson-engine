# Crimson Engine — crimson-renderer

C++20, SDL3, bgfx Direct3D 11, EnTT, Jolt, Recast/Detour and Dear ImGui docking. The editor provides native scenes, static glTF/GLB models, particles, lights, camera entities and a Play mode with controllable characters. A dockable Outliner, transform gizmos, Quader navigation and one shared asset library support scene authoring. Dear ImGui uses its default colors, font and sizing.

Project folder: `C:\Users\Drako\Desktop\_CRIMSON_ENGINE\crimson-engine`. The renderer is named **crimson-renderer**. Its C++ sources, shader sources, ImGui/bgfx adapter, and rendering diagnostics live together in [`src/crimson-renderer`](src/crimson-renderer).

The authoritative [engine architecture guide](docs/ENGINE_ARCHITECTURE.md) separates implemented features from agreed architecture and future work. Scene authoring, resource management and the first Play/controller slice are implemented and awaiting broader user acceptance. Native materials have an independent editor with a live 3D preview and can be assigned to built-in Boxes. Imported originals remain read-only; Save As creates an editable native material. Editing imported originals remains the next material-authoring slice. Character animation, jumping, dynamic rigid bodies and particle collision remain future work.

## Run and develop

Double-click **Launch Renderer.cmd** to open the existing Release executable immediately. The launcher builds only when that executable is missing.

**Dev.cmd** incrementally builds Debug and opens it. Close the renderer before rebuilding code that changes its executable. From a command prompt:

```bat
Build.cmd Release
Build.cmd Release test
"Launch Renderer.cmd" Release
```

The `test` suffix runs focused CPU checks. Use `Build.cmd Release configure` after changing dependency or preset settings. Building requires MSVC x64, CMake and vcpkg; the script finds Visual Studio and uses `%VCPKG_ROOT%` or `%USERPROFILE%\vcpkg`. Routine builds reuse the local dependency cache. The executable is `build\Release\bin\crimson-renderer.exe`, with its runtime DLLs beside it.

The application embeds [`assets/branding/crimson-engine.ico`](assets/branding/crimson-engine.ico) unchanged for Explorer, desktop shortcuts, the taskbar, and the title bar. A user-supplied ICO is authoritative; replacing it requires a rebuild. The transparent 512×512 PNG in [`assets/branding/crimson-engine.png`](assets/branding/crimson-engine.png) is also retained. Use `.\scripts\Convert-AppIcon.ps1` only when generating a new ICO from a PNG is explicitly intended. Successful links notify Windows to refresh the application's cached icon. Run `.\scripts\Create-DesktopShortcut.ps1` to create or refresh the **Crimson Engine** desktop shortcut to the Release executable; this also refreshes its cached icon. `.\scripts\Refresh-AppIcon.ps1` repeats just the targeted icon notification when needed.

## Editor panels

- **Viewport:** rendered scene, model/light selection, transform gizmo and navigation, following its dock size.
- **Game:** the active scene Camera's live view, available in Edit mode and during Play. Dock it beside the scene or drag its title bar onto another monitor. It has its own camera and image size, without editor grid, gizmos or collider outlines.
- **Outliner:** rootless scene hierarchy, multiple selection, inline renaming and drag-to-reparent. Right-click an entity or empty space for creation, grouping, duplication and deletion.
- **Inspector:** selected entity name, local **Position**, **Rotation** and **Scale**, each with explicit **X**, **Y** and **Z** fields. Rotation uses degrees. Drag a number or Ctrl+click to type; a completed edit is one undo operation. Light controls, built-in Box material assignment, imported-model information and Environment controls live here. Stored affine transforms and imported model hierarchies remain preserved.
- **Asset Browser:** one **Import** button and one collection for models, textures, environment images, imported read-only materials, native materials, saved scenes and native particle effects. **Create → Particle Effect** creates a saved starter effect in the current virtual folder and opens its editor; **Create → Material** creates an editable material, initially Unlit/Additive.
- **Particle Effect:** a floating, non-dockable effect document with an isolated preview, constant emitter properties, gravity, playback and its own save/undo history. Drag its title bar outside the main window to move it onto another monitor; its preview and controls stay active there.
- **Material Editor:** a floating, non-dockable material document with properties on the left and a live 3D preview on the right. Its File, Edit and Environment menus own material document actions and preview lighting. It can move onto another monitor independently of the docked panels.

A scale axis may be zero. This collapses the model and hides its draw; it remains an editable scene entity. After dragging a scale value to zero, release the mouse and press **Ctrl+Z** to restore the prior transform. The diagnostics panel can appear without taking focus from the Inspector. Confirm a typed value with Enter or leave the field before using scene undo; **Edit → Undo** also restores the prior transform. **Ctrl+Y** reapplies it. The collapsed-transform diagnostic clears when a nonzero scale is restored; it is separate from invalid/nonfinite data diagnostics. Active text fields retain their own text undo.

Use **View** to show/hide Game, Outliner, Inspector and Asset Browser; **Ctrl+Space** toggles the Asset Browser. The scene Viewport remains present. Single-panel dock panes hide their tab bar automatically; tabbed stacks retain their tabs. Splitters resize panes and docking/layout remain saved in `crimson_ui.ini`. **View → Reset layout** restores the default arrangement. **Edit → Preferences** opens a draggable, resizable window without dimming or blocking the editor. Background and major/minor grid changes appear immediately; preferences are saved application-wide.

## Scenes and entities

Create a scene with **File → New scene** or the Asset Browser's **New scene** action, then drag model cards into the viewport to add independent entities. New scenes start empty with the built-in environment. Repeated placements share the imported model and GPU geometry while keeping separate transforms. Importing an asset only adds it to the browser. An imported model's internal node hierarchy remains unchanged.

Use **File → Save** or **Ctrl+S** to save the current scene; **Save as...** creates a separate scene asset. Scenes are stored under `assets/scenes` as versioned `.crimson-scene` files and appear beside other assets. Double-click a scene card, or choose **Open scene** from its context menu, to open it. Browser deletion removes the selected scene file while shared models, textures and other scenes remain available. Switching or closing a dirty scene offers Save, Discard and Cancel.

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

Right-click the Outliner and use **Add** to create Box, Collider, Character, Camera, empty, directional-light, point-light or Environment entities. Add up to four directional lights and sixteen point lights; edit their color, intensity and enabled state in Inspector. Point lights also have **Radius (m)**, initially 20 m: illumination falls smoothly to zero at that world-space distance, independently of entity scale. Existing scenes without a radius use the same default. Light position/direction comes from its transform. Directional lighting and its viewport arrow use the same normalized transformed local -Z axis. Scene loading preserves missing model references with diagnostics so saving cannot silently erase them. `assets/editor-session.json` stores the last saved scene, panel visibility and editor camera per scene, separate from scene content.

## Play, characters and cameras

Use **Add → Box** for a visible unit cube with a shared built-in mesh and the protected default PBR material. Its transform can stretch, rotate and place it as a floor or obstacle. A mesh does not add collision automatically. Right-click the Box and choose **Add → Collider** to create a child Collider entity, or create/reparent a Collider through the Outliner. Its Inspector offers **Shape: Box** and **Size (m)**; resize and offset this child independently of the visual. Collider dimensions are local, so parent scale also applies. The scene grid is a visual guide and never acts as a physics floor.

Select a Box to change its **Material** in Inspector using the picker or an Asset Browser drag-and-drop. Choose a Standard PBR material with Opaque or Mask blending; **Clear** restores the protected default. Each Box keeps its own assignment while sharing geometry. Assignment supports scene save, duplication and undo, and committed material edits update both Scene and Game. To create a compatible material, use **Create → Material**, switch **Shading** to **Lit** and blending to Opaque or Mask, then save it. Imported model surface assignment remains a later slice.

Use **Add → Character** for a kinematic capsule with its origin at the feet. Inspector controls **Height (m)**, **Radius (m)** and **Speed (m/s)**. The first character becomes the player; select another and use **Make Player** to change control. Parent an imported model or a Box beneath the character as its visual, and adjust that child's scale, position and orientation. Particle emitters can also be children. The character owns its capsule independently of these visuals; keep the character's world scale at one and upright, and scale the children instead.

Use **Add → Camera** for an elevated perspective camera. The first camera becomes active; use **Make Active** in Inspector or the Game window's camera selector to choose another. **View → Game** previews this camera before Play. Move and rotate the camera in the scene to choose the game view, then adjust **Vertical FOV**, **Near (m)** and **Far (m)** in Inspector. Pick an entity through **Follow Target**, or drag its Outliner row onto the field. Assigning, replacing or clearing the target preserves the current camera view. Follow copies the target's translation while keeping the camera's angle and world-space offset; moving the camera changes that offset. Target rotation alone does not turn the camera. Ordinary camera parenting can still affect its orientation. Deleting the target clears follow without moving the camera; deleting the active camera shows **No active camera** until another is selected.

A quick first scene needs a wide, thin Box floor with a child Box Collider, a Character just above it, a child visual, and an active Camera following that Character. A second Box with a child Collider makes a wall to test sliding and collision. Visual meshes alone, including imported models, have no collision.

For a zoomed-out 16:9 ARPG starting view, try a root Camera with **Vertical FOV 35°**, **Rotation X/Y/Z (-50°, 45°, 0°)** and **Position = Character world position + (9, 14.5, 9) m**, with that Character selected as **Follow Target**. This frames a roughly 1.7–1.8 m visual at about 10% of the image height. Position edits update the follow offset automatically. To show more ground, increase all three offset values by the same factor while keeping FOV and rotation fixed. These are approximate composition settings, not a required camera configuration.

For a flatter classic Diablo II-style approximation, try **Vertical FOV 15°**, **Rotation (-30°, 45°, 0°)** and a position offset of **(30, 24.5, 30) m** instead. The narrower FOV and greater distance reduce perspective distortion, while the shallower angle produces approximately 2:1 ground diamonds near the image center. A 1.7–1.8 m character occupies roughly 12% of the image height. This uses the existing perspective camera; orthographic projection remains unimplemented.

Press **Play** to create a temporary runtime copy of the current scene and open/focus Game. Gameplay input activates automatically, including in a detached Game window on another monitor; no extra focus click is needed. **Left-click a walkable Collider surface** to move there, or **hold LMB and move the cursor** to keep steering toward the ground beneath it. Holding also continues travel with a stationary cursor as the follow camera moves. Recast/Detour finds a route around obstacles using the capsule's height and radius. Releasing LMB keeps the last accepted destination; the character turns smoothly toward the next point on its route and stops within 1 cm of the destination. With LMB released, mouse motion does not change the destination or facing. Moving the held cursor outside Game pauses steering, and bringing it back resumes it. Holding over empty space, a steep wall or unreachable ground retains the last accepted route. WASD is not bound to player movement. Jolt supplies gravity, ground contact, wall sliding and basic step/slope handling. Live Collider edits rebuild navigation and replan the current destination; unreachable destinations are rejected without moving the goal to a distant surface. Clicking empty space or a steep wall face does not set a destination; visual meshes need a Collider to receive movement clicks. Click Game to acquire control again after editing; that same click can issue a destination. **Escape**, another panel, widget capture or native focus loss cancels the destination and releases gameplay input. A canceled hold needs a fresh press to resume. Scene navigation and player input do not consume the same gesture.

The Game window's **Click marker** debug checkbox shows a small wire sphere touching the latest clicked or held ground point, including a point that has no reachable route. During a hold it follows sampled ground destinations; after release it stays at that world position and moves across the image naturally as the follow camera moves. Disable the checkbox for a clean Game view. The marker is temporary and is never saved in the scene.

While playing, the scene Viewport, Outliner, Inspector and Game show the same temporary world. Moving a visual, collider, camera or character in the scene updates Game; editing a character pose teleports its capsule and clears its velocity. **Stop** discards simulation and scene edits made during Play, restores the original scene, selection, dirty state and undo history, and keeps Game open as an Edit preview. Closing Game while playing also stops Play. Scene saving is disabled during Play. New scene, Open scene and asset deletion stop Play first, then continue against the original scene with its usual unsaved-change prompts. Imported assets and GPU caches remain shared; independent material/effect assets keep their own authoring and save lifecycle.

Play currently supports static Box Collider entities and upright, unit-scale Character capsules. A Box Collider may inherit rotation and nonzero scale, but a collapsed or sheared world transform disables it with a diagnostic. Static Colliders beneath a Character are unsupported: the character capsule supplies its collision. Navigation is a bounded, automatically generated mesh for the controlled capsule; large streamed navigation worlds, crowds and avoidance remain future work. Character-to-character collision, movable platforms, dynamic bodies, mesh colliders, jumping, orthographic cameras and skeletal animation are outside this slice. No active Camera means Game displays a message and accepts no movement. Focus loss across all editor windows suspends simulation and clears gameplay input, avoiding a catch-up burst on return.

## Particle effects — Steps 1 and 2

Choose **Create → Particle Effect** in the Asset Browser. The saved starter needs no import: it emits procedural unlit additive sprites from one point emitter, starting at rest with downward gravity. Double-click an effect card to open it again. Adjust **Rate (particles/s)**, **Lifetime (s)**, **Initial velocity (m/s)**, **Gravity (m/s²)**, **Size (m)**, color, intensity and capacity. Initial velocity launches a particle once at birth; gravity accelerates it continuously. Positive initial Y velocity makes it rise before negative Y gravity reverses it. Use **Clear initial velocity** to make an existing effect start at rest, then **Frame** to fit its trajectory. Increasing the downward gravity makes the particles fall faster. Fields are grouped by emission, initialization, motion and rendering purpose; later features have no inactive placeholder controls.

The particle renderer's **Material** field uses an ordinary material asset. Choose a compatible Unlit/Alpha or Unlit/Additive material through its adjacent picker, or drop a material card from the Asset Browser onto the field. Create a material through **Asset Browser → Create → Material**, then assign it to the effect. **Edit Material** opens that material's separate document. Clear returns to the built-in white procedural additive material. The picker filters compatible sprite materials.

For sprite materials, assign a managed image through **Texture** and choose **Alpha** blending for smoke/dust or **Additive** for glowing embers. Clearing the texture restores the procedural sprite. Material color and alpha multiply texture color/alpha and the effect's tint, intensity and **Opacity**. Texture RGB decodes from sRGB; alpha remains linear. The [material editor](#materials) provides the texture, sampler and document controls.

The effect editor has **Save**, **Save As**, **Undo** and **Redo**. File/Edit menu commands and Ctrl+S, Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z follow the active document; particle and scene histories stay independent. A property drag is one undo operation; Escape cancels it. Closing or switching a dirty effect offers Save/Discard/Cancel. External file changes preserve an unsaved working revision until Reload, Overwrite or Save As is chosen. Effects are versioned `.crimson-particle` files under `assets/particles`; virtual browser folders preserve their managed identity.

The isolated preview starts automatically. **Pause** freezes it, **Play/Resume** continues it, **Restart** clears and repeats the retained seed, and **Stop** clears particles and returns time to zero. The fixed starter seed is 1; constant Step 1 values do not yet consume random numbers. MMB orbits the preview, Shift+MMB pans and the wheel zooms; **Frame** restores a useful view. The grid is a visual guide, so particles pass through it. Preview controls and camera movement do not dirty authored files.

Drag an effect card onto the scene viewport to place an emitter at the grid. It starts immediately and starts again when its scene opens. Select its marker or Outliner row, transform it with T/R/E and focus with F. The Inspector's adjacent picker, drag-and-drop and clear controls assign an effect; **Edit Effect** opens its document and **Play Effect** restarts that selected instance. Multiple placements have independent playback. Moving an emitter preserves already spawned world-space particles; later births use its new transform. Duplication, deletion, hierarchy and scene undo/redo use the same entity controls as models. Saving the scene retains authored references and transforms, independently of effect saving.

Valid effect-property drags update the isolated preview in place; committing publishes the working definition and applies it to scene instances without restarting playback. Color, size, intensity and opacity change existing particles immediately, including while paused. Material assignment and committed texture/color/blend edits replace prepared appearance without restarting simulation. Failed preparation retains an available working material and reports the affected dependency; an initially unresolved appearance omits sprites while simulation continues. Gravity applies on the next simulation tick; rate and enabled state apply to subsequent emission. Initial velocity and lifetime apply to newly born particles, so existing particles keep their birth values. Capacity changes clear live particles to replace storage while preserving elapsed time, emission phase and Play/Pause/Stop state. Undo, redo and cancelling an edit use the same rules; they restore authored values without rewinding simulation. Opening a different effect or using **Restart**/**Play Effect** starts it again. Saving does not reset playback.

The editor continues playing while either its main window or a detached editor window has focus. When all editor windows lose focus or none can draw, particle time is suspended; restoration skips the suspension interval. Closing a detached particle window closes its document with the usual dirty-file prompt. Mouse/key events belong to their originating window, so navigating the particle preview cannot move the scene camera. Playback state and live arrays never enter scene or effect files. Scenes write version 4 while continuing to read versions 1, 2 and 3.

The CPU simulation uses 60 Hz ticks, at most eight catch-up ticks per frame and bounded emission. Each emitter supports a configured capacity of 1–65,536; scene runtime admits up to 256 instances and 262,144 configured particle slots. A transactional restart or capacity replacement may briefly hold one additional emitter reservation of at most 65,536 slots; reusable presentation buffers are separate. Rendering accepts up to 65,536 packets per view and may omit additional packets when the GPU transient buffer is full. Preview and scene diagnostics explain dropped work and particles cleared by capacity changes. These are work limits, not frame-rate promises. Asset deletion can replace an effect reference or clear it while retaining the emitter entity; close the effect document before deleting its managed asset.

Transparent particles sort back to front across effects, including Alpha/Additive overlap, and batch only consecutive compatible materials. They depth-test against models without writing scene depth. The editor background remains independent of exposure; dark smoke can attenuate exact white. One tone map applies to combined premultiplied HDR color, so intense translucent emission may saturate at display white. Material thumbnails show sprite-compatible materials on a quad; imported PBR materials keep their sphere previews. Particle and material files now write version 2 while retaining version-1 readers; existing files change only when explicitly saved or affected by an authorized asset transaction.

Steps 1 and 2 are implemented. Random ranges, additional shapes, multiple emitters, curves, timelines, local-space mode and collision remain in their separate [particle roadmap](docs/PARTICLE_SYSTEM_PREWORK.md). Step 3 adds common emission and motion.

## Materials

Choose **Create → Material** in the Asset Browser to create a saved native material. Double-click a material card, use **Open material** in its context menu, or choose **Edit Material** beside an assigned field to open it. The floating Material Editor keeps a resizable properties sidebar on the left and a live 3D preview on the right. Drag its title bar outside the main window to work on another monitor.

Native materials support **Unlit** shading with **Alpha** or **Additive** blending, and **Lit** PBR shading with **Opaque** or **Mask** blending. Lit properties include roughness, metallic, alpha cutoff for masked surfaces, double-sided rendering, and base color, metallic/roughness, normal, occlusion and emissive textures. Imported originals remain read-only. **File → Save As...** creates a separate native material with the current appearance, which can then be edited and assigned through compatible material fields.

Texture fields have an adjacent picker, thumbnail/name, clear button and drag-and-drop assignment. **Wrap U**, **Wrap V** and one **Filtering** selector control the sampler. **Nearest** keeps texture pixels sharp; **Linear** blends them smoothly. Existing minification, magnification and mip settings stay preserved until a Filtering option is explicitly selected. Lit texture slots also retain their UV0/UV1 selection. Removing lighting texture assignments and using base color UV0 makes a Lit material eligible to switch to Unlit.

The preview mesh selector offers **Sphere** by default, **Cube**, **Rounded Cube**, **Cylinder** and **Plane**. The **Environment** menu opens a small panel with an HDRI picker, enable, intensity, rotation and **Show background**. Clearing the HDRI uses the built-in studio. Preview mesh, view and environment settings are transient: they do not dirty or enter the material file, scene, or saved scene environment. Valid property drags update the preview immediately.

| Input over the material preview | Action |
| --- | --- |
| Middle-button drag | Tumble the object |
| Shift + middle-button drag | Pan the preview |
| Ctrl + middle-button drag | Rotate the preview environment |
| Wheel | Zoom |
| Middle-button double-click | Reset the view and environment rotation |

These gestures follow Quader's material mini-viewport. **Reset view** restores only the view. **File** contains **Save**, **Save As...** and **Close**; **Edit** contains material **Undo** and **Redo**. Each material has independent history, dirty state and save/conflict handling. Closing or switching a dirty material offers Save/Discard/Cancel; external changes offer Overwrite, Reload, Keep editing or Save As. The main File/Edit commands and Ctrl+S, Ctrl+Shift+S, Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z follow the focused scene, effect or material document.

Material graphs, master materials and child material parameter overrides remain planned. The current editor exposes the implemented properties.

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

The Asset Browser uses equally sized cards for every kind, with aspect-fit thumbnails and a border only around selected cards. Ctrl/Shift extend selection, Ctrl+A selects the visible folder, and F2 renames inline. Create folders through New Folder or the context menu; drag selected assets onto folders or breadcrumbs to organize them. A synthetic **..** card navigates to the parent folder and accepts moves upward. It cannot be selected, renamed or deleted. Multi-asset drags show a card/count badge. The size slider uses **16, 32, 64, 128, 256 and 512** pixel ticks, defaulting to 128. Dragging several models into the viewport places them as one undoable operation. Folder organization and display names live in `assets/browser.json`; these operations preserve managed files and references.

## Import and assign textures

1. Click **Import** in the Asset Browser. PNG, JPEG, TGA, BMP and Radiance HDR are supported alongside glTF/GLB models; multiple files and mixed asset types can be selected. HDR images are environment sources. Each file imports independently, with failures reported in the browser.
2. Files are copied into `assets/textures`. Collisions receive a suffix rather than overwriting a different file.
3. Existing sphere material controls retain their texture pickers under **Albedo**, **Roughness**, **Metalness**, and **Normal strength** in the legacy preview/diagnostic mode. Picking or dragging a compatible thumbnail onto a field assigns it immediately. Scene environment fields accept compatible environment images.
4. Adjust tint/factors. Albedo, roughness and metalness maps multiply their factors; first assignment to an empty slot makes the factor neutral (white or 1). Replacing a map preserves the factor; **Clear** removes only the map. Normal assignment preserves its strength.

Albedo samples sRGB color. Roughness and metalness sample red-channel linear data. Normals sample linear RGB tangent-space data with **+Y/OpenGL green**, oriented along the mesh's increasing V direction. **Normal strength** ranges from 0 to 2; zero gives the original smooth surface, and a flat `(128,128,255)` map is neutral. **Reset material** clears maps and restores original values. Imports, material factors, normal strength and assignments survive restart through `assets/material.json` with project-relative paths; older material files remain readable.

Three tiny example textures are included from the earlier smoke check: checker albedo, roughness ramp and metalness stripes. Textures upload on first use and are cached; thumbnails preserve aspect ratio. HDR thumbnails use tone mapping for display without changing the imported source.

## Import and place models

1. Click the same **Import** button and select a `.gltf` or `.glb` file. The importer copies the model and required buffers/images into a managed package under `assets/models`. Images, environments and models appear together in the Asset Browser.
2. Drag the model's card onto the viewport. A temporary model previews placement on the `y=0` grid; releasing queues creation when its resources are prepared. The model keeps its authored scale, orientation and internal node transforms. Failed models report a cause without removing existing scene content.
3. Drop additional models to add more entities. Select a placed model, use T/R/E to transform it and F to focus it. Save the scene to retain placements across restarts.

Managed models reopen even after their original source folder moves or disappears. Failed imports leave the working scene intact. Imported materials are read-only in this slice and retain their authored appearance. The older `assets/preview.json`, sphere material and environment files remain preserved for compatibility; scenes use their own versioned persistence.

Importing an unchanged model again reuses its existing managed asset, textures and materials, including models imported before this behavior was added. Identity checks include the document and every required buffer/image; changed content creates a separate asset without overwriting existing placements. Names alone do not identify models. Existing duplicate packages are retained so their saved references remain valid.

Supported models use static triangle geometry, hierarchy transforms, UV0/UV1, vertex colors, metallic-roughness PBR, normal maps, emissive, occlusion, double-sided surfaces and opaque/alpha-cutout materials. Missing normals are generated flat; missing normal-map tangents use MikkTSpace. Texture wrapping, all six glTF minification modes, magnification filtering and UV selection are retained. Derived mesh LODs and texture mips stream progressively; selecting a model requests original geometry and native texture detail. Rendering and picking share the same prepared snapshot.

Translucent (`BLEND`) materials, skins, morph targets, points/lines and required unsupported extensions are rejected with diagnostics. Animation playback, imported cameras/lights and optional unsupported extensions are ignored with warnings. Draco/meshopt compression, KTX2 and advanced material extensions require later support. Imported-material editing remains the next model-authoring slice.

## Environment lighting

Create an **Environment** in the Outliner and select it to edit its settings in Inspector. The first Environment becomes active; additional nodes and duplicates remain inactive until **Make Active** is used. The Outliner identifies the active node. Deleting it clears activation; other Environment nodes stay inactive and built-in studio defaults with exposure 1 take over. Undo restores the node and its activation together. New scenes remain empty, using those defaults without inserting an Environment entity.

Use the adjacent picker or drag an equirectangular panorama onto **Environment image**. Radiance HDR supplies linear radiance; PNG/JPEG/TGA/BMP panoramas decode as sRGB color. **Clear** returns to studio. Inspector exposes enable, intensity, explicit Y rotation and exposure; environment rotation is independent of entity transforms. Each scene saves these components and its active Environment ID. Versions 1–3 remain readable, and an explicit save writes version 4; the legacy `assets/environment.json` is preserved. Baking/cache preparation runs on workers under `assets/cache/ibl`; the current environment stays visible while a replacement is prepared. Intensity and rotation reuse the bake.

Environment and direct lights accumulate in linear HDR before exposure and tone mapping. The grid and tools keep their own brightness. IBL supplies distant illumination and reflections; local reflected geometry, occlusion and shadows are outside this slice. Rough metals may darken from the GGX model's missing multiple scattering, and the split-sum approximation loses accuracy at grazing angles.

## Asset deletion and shared resources

After dropping a model into the viewport, keyboard focus follows the placement. **Delete** in the viewport, Outliner or Inspector removes selected scene entities as one undoable edit; the model asset stays in the browser. **Ctrl+Z** restores the entities.

To remove asset files, focus the Asset Browser and press **Delete**, or use its context menu. Deleting a virtual folder includes its recursive contents. Confirmation shows asset/folder counts without listing the selected names; zero-count categories are omitted. Only surviving usable saved resources trigger a dependency warning. **Dependencies and replacements** starts collapsed and shows only the assets with those external dependencies. Multiple instances in one saved scene count as one owner; unsaved placements, undo history, editor bookmarks and archived sphere/environment settings do not count as saved dependencies. Resources being deleted together are excluded from those warnings. Cancel leaves assets and documents intact. Validation runs in the background before a staged transaction publishes the change.

Without a chosen replacement, ordinary image slots use a solid 50% gray texture, normal slots use a neutral tangent-space normal, environment images use studio, and materials use linear gray albedo `(0.5, 0.5, 0.5, 1)`, roughness 1 and metallic 0 without maps/emission. Imported material assets are read-only and retain stable IDs. Replacement material UV/tangent requirements are validated against every affected primitive. Deletion mappings persist, so deliberately removed package images/materials do not reappear on restart.

Deleting a model without a replacement removes its referencing entities from every saved project scene and from the active document separately. Unrelated descendants survive with their world placement preserved. Independently usable package images and material assets remain available. The confirmation discloses any active undo-history reset; existing unsaved edits are not written into saved scenes. Deletion is a project operation outside entity undo. Files use a recovery journal and same-volume quarantine for rollback/interrupted-operation recovery.

Models, materials and scenes have rendered thumbnails. PBR material cards show the actual material on a shared UV sphere with a fixed camera and key/fill lighting, following Godot's material-preview setup; sprite-compatible materials use a quad. Authored linear factors, packed channels, UV selection, normal maps, emission and alpha cutout use the normal model shader. Model previews use isolated studio lighting; scene saves queue a preview from the saved editor camera. Older scenes receive a bounds-framed fallback. Cold imports/previews can take several seconds to prepare; subsequent loads reuse disposable caches under `assets/cache`.

The CPU ResourceManager uses typed asset references, live-interest handles and immutable version snapshots. Two workers load/decode/process resources while GPU owners publish prepared replacements on the application thread. Texture mips begin with a complete coarse allocation of at most 64 pixels along the longest edge. Mesh caches retain exact LOD0 and safe attribute-aware derived levels. Visible demand refines shared resources; selected models request native detail. The default soft targets are 512 MiB CPU, 512 MiB aggregate managed GPU and a 64 MiB thumbnail sublimit, with five-second idle retention. Live leases remain valid above these targets. Uploads are chunked toward 4 MiB/approximately 2 ms per frame, rather than treating those targets as a hard backend-call limit.

Managed authoritative assets and engine shader sources/includes are watched with a 200 ms debounce. Caches, staging and archived import originals are excluded. Valid replacements publish coherently; invalid or disappeared files produce diagnostics and retain the last working resource. Clean externally changed scenes can reload between gestures; dirty scenes offer **Reload / Keep editing / Save As**. Saving over externally changed content requires conflict resolution. Shader compilation/interface failures retain embedded or last working programs. Shutdown stops watching/requests, cancels and joins CPU/compiler work, drains readbacks and releases GPU owners before bgfx.

## Build checks and user acceptance

Run `Build.cmd Release test` for focused checks of math, imports, scene/history, resources, streaming caches, reference-aware deletion, Preferences and shader validation/compilation, plus isolated GPU lifetime checks. Implementation handoffs report actual results and leave feature acceptance to the user. There is no active `VERIFICATION.md` maintenance workflow. PC interaction and input automation require permission; noninteractive screenshots are allowed.

Existing capture scripts remain optional manual diagnostics, not a feature-completion requirement. `.\scripts\Verify-Renderer.ps1` captures the current project after a build. `.\scripts\Verify-Ibl.ps1 -Configuration Release` runs isolated IBL/normal-map diagnostics under `build/verification`.

The executable accepts `--project-root PATH` to use another project's assets, persistence and layout. Native `--ibl-smoke` diagnostics require an explicit disposable project root because they create fixtures and change assignments. Run them in a scratch folder, followed by `--scenario ibl-reopen` with the same root to check reopening. Focused capture scenarios include `ibl-dielectric`, `ibl-metal`, `ibl-rough`, `ibl-neutral`, `ibl-normal`, `ibl-zero-normal` and `ibl-extreme`; normal scenarios use the smoke fixtures. Pair a scenario with `--frames COUNT` and optionally `--capture PATH`. The separate `--editor-smoke` mode also changes material assignments; it is intended for import/assignment verification.

`.\scripts\Verify-Models.ps1 -Configuration Release` retains the older isolated model-preview diagnostics. Its `--model-smoke` mode requires a disposable `--project-root`; `--scenario model-reopen` and `--scenario model-sphere` exercise the legacy preview path. They are not the scene-authoring acceptance workflow.
