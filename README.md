# Crimson Engine with Quader modeling

Crimson Engine uses the `quader-crimson` application with its Public 0.1 profile as its native Qt
base. One native QDR scene owns the geometry, hierarchy, selection and history.
Its viewport, Outliner and Inspector also present Engine components and game
properties. Quader owns the public modeling tools, topology, input and the native window; physics
and first-person Play use the same scene through the shared renderer host.

The official target is `crimson-renderer`; its executable is
`build/Release/crimson-renderer.exe`. Implementation ownership and provenance are
in [ENGINE_INTEGRATION.md](ENGINE_INTEGRATION.md).

## Build

Run `Build.cmd` from a Windows x64 checkout with Visual Studio C++ tools, CMake
and the pinned vcpkg dependencies available. The wrapper initializes MSVC,
configures the `windows-Release` preset, builds the application and material-cache
commandlet, and stages Qt, runtime resources and the paired shader payload.
Project-specific material caches belong to the selected project folder.

The renderer and maintained bgfx overlay come from the canonical
[`thirdparty/crimson-renderer`](thirdparty/crimson-renderer) submodule. Generated
shader headers and extracted dependencies are build outputs. Update the owning
source or overlay and rebuild through the wrapper.

## Open a project

Launch the application to reach **Projects**. Choose **New Project** or
**Open Project**, then select the project folder. That folder is the complete
project filesystem and the root of the single Asset Browser. Existing asset files
may already be in the folder; a new project does not require an empty directory.

A folder with project metadata opens that project. A folder without it receives
new project metadata and an initial scene. The application keeps the chosen root
and its existing files in place. Recent projects return to the same folder.

The workspace opens in **Default** mode. Use the **Default / Modeling** selector
to enable Quader Public 0.1's modeling toolset in the same workspace viewport.
Default keeps object placement, selection, Move/Rotate/Scale, lights, game
components and Play. It hides geometry launchers, component selection,
geometry menus and the modeling Commands Library. Full-only Face Texture and UV
tools are unavailable in both modes.
Modeling reveals the Public 0.1 modeling controls in the same scene and layout.
Changing scene tabs retains the selected mode; opening a project starts in Default.

Use the single Asset Browser to navigate project folders, open documents and
place assets. File → Save and Ctrl+S save the native `.qdr` scene, including its
Engine components, through the ordinary document and history owners. Native
General/Transform/light fields and Engine component fields share one Inspector.
The current Quader UI uses flat tabs and dockable Tool Settings and Commands
Library panels. Mode changes retain panel placement and visibility preferences.

Use **Create** in the top menu to add Player, Enemy, Point Light, Directional
Light, Spot Light, Camera, Environment, Collider or Navigation Volume to the
current scene. The viewport's shading menu includes **Rendered**, using Crimson
scene lighting and materials in either workspace mode.

To create a Ragdoll asset, import a skinned model through the Asset Browser,
right-click its model-package folder and choose **Create Ragdoll**. The source
finishes loading before the rig opens in its editor. The browser's **Create**
submenu also contains Engine materials, particles, decals and gameplay assets.

## Viewport and Play

Hold RMB to look and use physical WASD to fly; MMB orbits and Shift+MMB pans.
Use Move, Rotate and Scale with their configured shortcuts. A stationary RMB click opens the scene
context menu. Drag model, material, particle or decal assets from the browser
onto the viewport. Material and decal placement uses the visible surface and the
owning document's history.

Game is the transient runtime view of the same authored scene. The Play controls
provide Play, Pause, Step and Stop. Click Game to acquire first-person input: physical WASD, relative mouse look and Space jump. Escape or
focus interruption releases capture. Stop returns to the authored scene.

Edit → Preferences → Rendering → Renderer selects Direct3D 12 or Direct3D 11
for the next launch. Direct3D 12 is the default. `--renderer dx11` or
`--renderer dx12` overrides that preference for one process without saving it.
Both backends use Qt-owned composition devices and the same native windows.

The Profiling menu starts and saves explicit editor or gameplay captures. A
gameplay capture requires Play; ending Play stops that recording. CLI modes
`--profile startup|editor|gameplay`, `--profile-duration SECONDS` and
`--profile-output DIR` provide bounded captures using the staged Tracy tools.
See [Profiling](engine-core/docs/PROFILING.md) for preparation and capture output.

Engine domain contracts remain under [`engine-core/docs`](engine-core/docs).
[Qt application ownership](engine-core/docs/QT_APPLICATION_OWNERSHIP.md) defines
current project, input and presentation ownership. Older checkpoint descriptions
of the replaced ImGui/SDL shell are historical.
