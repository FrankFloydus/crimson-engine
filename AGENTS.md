# Crimson Engine repository instructions

Native Windows x64 ARPG engine foundation: C++20, MSVC, CMake, SDL3, bgfx/Direct3D 11, and Dear ImGui docking. The official renderer target/executable is `crimson-renderer` / `crimson-renderer.exe`.

## Task context

- Use the relevant sections of [ENGINE_ARCHITECTURE.md](docs/ENGINE_ARCHITECTURE.md) for renderer, asset, scene, controller/input, simulation, and lifetime changes. It is authoritative for ownership contracts, implementation status, and the milestone marked **Next**.
- Keep work within the requested slice. Add later systems when their slice needs them; a planned interface is not evidence that it already exists.
- Implement only features and UI the user requested. Do not add unsolicited features or explanatory text to panel bodies. When help is necessary, use a short tooltip; omit long explanations or put them in documentation.
- Follow [Editor UI conventions](docs/ENGINE_ARCHITECTURE.md#editor-ui-conventions) for every new or modified editor. Reuse the shared document-editor menu component; keep File/Edit actions, property sections and shortcuts consistent across editors.
- Follow [Material node authoring conventions](docs/MATERIAL_NODE_AUTHORING.md) for material graph nodes, inline controls, color editing, typed inputs and preview behavior.
- Use [README.md](README.md) for setup and user-facing controls. `docs/VERIFICATION.md` is historical evidence, not an active workflow or a required handoff document.

## Build, run, and verify

Run from this project root in PowerShell. `Build.cmd` initializes MSVC x64; use it instead of assuming an ordinary shell has the compiler environment.

| Purpose | Command |
| --- | --- |
| Incremental Release build | `.\Build.cmd Release` |
| Build and run focused CTest checks | `.\Build.cmd Release test` |
| Reconfigure and build after dependency/preset changes | `.\Build.cmd Release configure` |
| Open the existing Release application | `& '.\Launch Renderer.cmd' Release` |
| Optional manual native capture, after building | `.\scripts\Verify-Renderer.ps1 -Configuration Release` |
| Regenerate the Windows ICO after changing the 512×512 logo PNG | `.\scripts\Convert-AppIcon.ps1` |
| Create/refresh the desktop shortcut, after building | `.\scripts\Create-DesktopShortcut.ps1 -Configuration Release` |
| Refresh Windows' cached application icon | `.\scripts\Refresh-AppIcon.ps1 -Configuration Release` |
| Optional manual IBL/normal-map diagnostic captures | `.\scripts\Verify-Ibl.ps1 -Configuration Release` |
| Optional manual legacy model-preview diagnostic captures | `.\scripts\Verify-Models.ps1 -Configuration Release` |

- `VCPKG_ROOT` selects vcpkg; the build wrapper falls back to `%USERPROFILE%\vcpkg`. Preserve the pinned manifest baseline and project-local dependency caches.
- `test` and `configure` are alternative second arguments, not combinable flags. The executable is `build/Release/bin/crimson-renderer.exe`.
- The launcher builds only if its executable is missing. It does not check source freshness; build changed code before launching it.
- `Dev.cmd` builds/launches Debug. Use Release for ordinary handoffs and Debug when the task needs it. Close the running renderer before rebuilding its executable.
- Ask the user to save and close a running renderer only when implementation is finished and the integration build is ready to start. Never ask at task start or while implementation is still underway; the user may continue using the editor during that work.
- For code changes, use one incremental integration build after implementation owners finish, followed by focused CPU tests. Expand or repeat checks when failures or unresolved concerns justify them. The user performs feature acceptance after the implementation is ready.
- Computer control, including mouse/keyboard automation and interacting with the user's applications, requires the user's permission. Noninteractive screenshots are allowed. Do not run routine native acceptance/capture suites or maintain `VERIFICATION.md` as part of feature completion.
- Documentation-only changes need consistency/link checks, not an application build. Routine handoffs do not need Debug/Release matrices, UI scaling matrices, or broad image suites.
- The capture script uses the current project and an existing binary. The separate `--editor-smoke` mode imports fixtures and changes saved material assignments; use it only for an import/assignment task that needs it.
- `Verify-Ibl.ps1` uses `--project-root` to keep its fixtures, material/environment settings and layout under `build/verification`. Its `--ibl-smoke` import mode requires a disposable project root and rejects the authored project root. Run this focused suite for IBL/normal changes, not every routine handoff.
- `Verify-Models.ps1` isolates mixed imports, model replacement/reopening and return-to-sphere checks under `build/verification`. `--model-smoke` requires a disposable project root; `--scenario model-sphere` clears that project's saved preview and also rejects the authored root.

## Source layout

| Location | Responsibility |
| --- | --- |
| `src/App.*`, `src/main.cpp` | Thin application composition, SDL lifetime, frame loop, startup/options |
| `src/editor/` | Editor session, scene actions, selection/viewport interaction and document orchestration |
| `src/scene/` | CPU EnTT scene data, hierarchy, document storage, command history and render extraction |
| `src/math/` | Engine-owned vectors, matrices, bounds and math helpers; private GLM implementation |
| `src/crimson-renderer/` | Renderer C++, mesh types, GPU resources, ImGui/bgfx adapter, rendering diagnostics |
| `src/crimson-renderer/shaders/` | Shader sources and varying definitions; matching bgfx `shaderc` generates embedded headers |
| `src/tools/` | Docked editor panels and property controls |
| `src/assets/` | Managed asset imports and persistence; current texture ownership is documented in the architecture |
| `src/resources/` | CPU resource stores, immutable versions, workers, dependency watching, mip/LOD caches and shader compilation; GPU texture streaming remains AssetLibrary-owned |
| `src/input/` | Editor navigation and input handling |
| `tests/`, `docs/` | Focused automated checks, authoritative architecture and user documentation |

## Repository constraints

- Put rendering implementation and shaders in `src/crimson-renderer/`. Keep editor panels and asset import/storage in their own boundaries; follow the architecture's current-versus-target ownership distinction.
- Keep `App` a composition/lifetime/frame-loop boundary. Scene editing belongs in `src/editor/`, dock views in `src/tools/`, and CPU world/history/storage in `src/scene/`; do not accumulate those implementations in the application loop.
- Public math values use Crimson types. GLM is an implementation detail behind `src/math/`; do not expose GLM, bx, bgfx, EnTT entity handles or ImGui types in serialized scene data. Keep explicit RAII owners and concrete dependencies rather than introducing a service locator or engine framework.
- Preserve imported assets, material factors/assignments, and `crimson_ui.ini`. Changes to their storage must retain existing project data.
- Application branding lives in `assets/branding`; `src/platform/CrimsonResources.rc.in` embeds the ICO. A user-supplied ICO is authoritative: embed it unchanged, and do not overwrite it by running the PNG converter. Use conversion only when the user requests generation from a PNG. SDL owns the native window-class icon lifetime. Keep branding out of the managed texture library.
- Treat generated headers and cached dependency sources under `build/` as read-only to manual edits. Change project sources/manifests/build configuration and let tools regenerate outputs.
- Rendering changes must preserve linear HDR lighting, one final sRGB encoding, and the grid/tools' independence from scene exposure. Texture roles determine color decoding and channels; follow the material contract in the architecture.
- Environment preprocessing belongs outside frame submission. Its derived cache is disposable; update preprocessing/BRDF versions when their equations or output conventions change. Preserve the managed source and `assets/environment.json`.
- Preserve explicit GPU/resource owners and release their resources before bgfx shutdown. Keep viewport input ownership explicit so editor widgets, navigation, and future player controls do not consume the same gesture.
- Construct ResourceManager before GPU owners. Worker jobs never call bgfx/ImGui or mutate documents; publish coherent CPU/GPU replacements on the main thread. Persistent AssetRef values are separate from live handles/snapshots and GPU leases. Count pending/retired allocations and drain readbacks before shutdown.
- Keep authored reference/deletion policy separate from runtime residency. Deletion is a staged project transaction; preserve stable IDs, protected role-correct defaults, project-wide saved-scene updates and unrelated unsaved edits. Legacy material/environment/preview files and virtual browser organization remain compatible.
- Use familiar asset fields adjacent to their property: picker, thumbnail/name, clear, and drag-and-drop with immediate assignment.
- Keep one Asset Browser catalog and one **Import** button for supported imports. Models, textures, environments and native scene assets share the browser; typed asset fields filter compatible assets.
- Scene entity IDs are persistent; EnTT handles are temporary. Preserve full affine local matrices and imported model hierarchies. Scene edits use transactional undo/redo, and scene saves atomically replace only the selected managed scene file.
- Keep Dear ImGui's default colors, font and sizing. Preserve docking state; do not introduce custom editor themes unless requested.
- Quader at `C:\Users\Drako\Desktop\quader-windows\quader-app` is the authorized navigation/grid reference. Read/copy the relevant code without changing that checkout.

## Completion

Complete authorized local implementation, necessary fixes, builds and focused CPU checks without adding routine approval checkpoints. Report the result, actual checks and concrete limitations, then leave feature acceptance to the user. Update architecture status and user-facing controls when features land; do not add or refresh a `VERIFICATION.md` workflow. Ask for permission before controlling the user's PC.
