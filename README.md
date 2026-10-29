# Crimson Engine with Quader modeling

This application starts from the complete `quader-crimson` modeling application.
Quader owns its Qt main window, native event loop, projects, modeling documents,
topology, UVs, tools, history and input. Crimson Engine adds native levels, assets,
physics and first-person Play through the same window and renderer host.

The official application target is `crimson-renderer`; its executable is
`build/Release/crimson-renderer.exe`. The implementation boundary and source
provenance are documented in [ENGINE_INTEGRATION.md](ENGINE_INTEGRATION.md).

## Build

Run `Build.cmd` from a Windows x64 checkout with Visual Studio C++ tools, CMake
and the pinned vcpkg dependencies available. The wrapper initializes MSVC,
configures the `windows-Release` preset, builds the application and material-cache
commandlet, stages Qt and the paired native reader, and prepares material shaders.
Wait for cache preparation to finish before launching the result.

The matching renderer and maintained bgfx overlay come from the canonical
[`thirdparty/crimson-renderer`](thirdparty/crimson-renderer) submodule. Generated
shader headers and extracted dependencies are build outputs. Update the owning
source or overlay and rebuild through the wrapper.

## Documents and controls

Quader's ordinary File, project, modeling and document workflows remain in the
main window. The Engine menu adds level creation/open/save, native asset imports,
scene linking and engine authoring commands. Engine Scene, Game, Level Outliner,
Level Inspector and Engine Assets are native docks in that window.

Use Engine → Open Engine Project to open an existing Engine folder containing
`Content` and `.crimson` data. A Quader project manifest is not required. The
selected Engine project and its accepted level/editor camera restore at startup;
the modeling project can remain in a separate folder. `--project-root DIR`
selects the Engine project for one launch.

In Engine Scene, hold RMB to look and use physical WASD to fly; MMB orbits and
Shift+MMB pans. Use T, R and E for transform operations. A stationary RMB click
opens the scene context menu. Drag model, material, particle or decal assets from
Engine Assets onto the scene. Material and decal placement uses the exact visible
surface, preserving native asset and history ownership.

Edit → Preferences → Rendering → Renderer selects Direct3D 12 or Direct3D 11
for the next launch. Direct3D 12 is the default. `--renderer dx11` or
`--renderer dx12` overrides that preference for one process without saving it.
Both backends use Qt-owned composition devices and the same native windows.

The Play toolbar owns Play, Pause, Step and Stop. Click Game to acquire gameplay
input; physical WASD, relative mouse look and Space belong to the first-person
controller while it owns input. Escape or focus interruption releases capture.

Modeling scenes and engine levels retain their separate native documents and
identities. Link a saved modeling scene through Engine → Link Current Modeling
Scene. Source changes use the paired Quader native reader and the engine's
existing publication and synchronization contracts.

Engine domain contracts remain under [`engine-core/docs`](engine-core/docs).
Current native application and presentation ownership is defined by
[ENGINE_INTEGRATION.md](ENGINE_INTEGRATION.md); older ImGui or SDL windowing
descriptions in retained checkpoint documents are historical.

The Profiling menu starts and saves explicit editor or gameplay captures. A
gameplay capture requires Play; ending Play stops that recording. CLI modes
`--profile startup|editor|gameplay`, `--profile-duration SECONDS` and
`--profile-output DIR` provide bounded captures using the staged Tracy tools.
See [Profiling](engine-core/docs/PROFILING.md) for tool preparation, capture
status, output files and the optional frame-profile export.
