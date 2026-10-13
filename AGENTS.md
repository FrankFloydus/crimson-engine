# Crimson Engine repository instructions

Native Windows x64 ARPG engine foundation: C++20, MSVC, CMake, SDL3, bgfx/Direct3D 11, and Dear ImGui docking. The official renderer target/executable is `crimson-renderer` / `crimson-renderer.exe`.

## Task context

- Use the relevant sections of [ENGINE_ARCHITECTURE.md](docs/ENGINE_ARCHITECTURE.md) for renderer, asset, scene, controller/input, simulation, and lifetime changes. It is authoritative for ownership contracts, implementation status, and the milestone marked **Next**.
- Keep work within the requested slice. Add later systems when their slice needs them; a planned interface is not evidence that it already exists.
- Use [README.md](README.md) for setup and user-facing controls, and [VERIFICATION.md](docs/VERIFICATION.md) for prior evidence or new slice results. Small documentation edits need only their relevant context.

## Build, run, and verify

Run from this project root in PowerShell. `Build.cmd` initializes MSVC x64; use it instead of assuming an ordinary shell has the compiler environment.

| Purpose | Command |
| --- | --- |
| Incremental Release build | `.\Build.cmd Release` |
| Build and run focused CTest checks | `.\Build.cmd Release test` |
| Reconfigure and build after dependency/preset changes | `.\Build.cmd Release configure` |
| Open the existing Release application | `& '.\Launch Renderer.cmd' Release` |
| One short native capture, after building | `.\scripts\Verify-Renderer.ps1 -Configuration Release` |
| Regenerate the Windows ICO after changing the 512×512 logo PNG | `.\scripts\Convert-AppIcon.ps1` |
| Create/refresh the desktop shortcut, after building | `.\scripts\Create-DesktopShortcut.ps1 -Configuration Release` |
| Refresh Windows' cached application icon | `.\scripts\Refresh-AppIcon.ps1 -Configuration Release` |
| Focused IBL/normal-map acceptance captures in an isolated project | `.\scripts\Verify-Ibl.ps1 -Configuration Release` |

- `VCPKG_ROOT` selects vcpkg; the build wrapper falls back to `%USERPROFILE%\vcpkg`. Preserve the pinned manifest baseline and project-local dependency caches.
- `test` and `configure` are alternative second arguments, not combinable flags. The executable is `build/Release/bin/crimson-renderer.exe`.
- The launcher builds only if its executable is missing. It does not check source freshness; build changed code before launching it.
- `Dev.cmd` builds/launches Debug. Use Release for ordinary handoffs and Debug when the task needs it. Close the running renderer before rebuilding its executable.
- For code changes, use one incremental integration build after edits finish, focused checks, and a short visual smoke when relevant. If multiple agents edit code, finish their work before the integration build. Expand or repeat checks when failures or unresolved concerns justify them.
- Documentation-only changes need consistency/link checks, not an application build. Routine handoffs do not need Debug/Release matrices, UI scaling matrices, or broad image suites.
- The capture script uses the current project and an existing binary. The separate `--editor-smoke` mode imports fixtures and changes saved material assignments; use it only for an import/assignment task that needs it.
- `Verify-Ibl.ps1` uses `--project-root` to keep its fixtures, material/environment settings and layout under `build/verification`. Its `--ibl-smoke` import mode requires a disposable project root and rejects the authored project root. Run this focused suite for IBL/normal changes, not every routine handoff.

## Source layout

| Location | Responsibility |
| --- | --- |
| `src/App.*`, `src/main.cpp` | Application composition, SDL lifetime, frame loop, startup/options |
| `src/crimson-renderer/` | Renderer C++, mesh/math types, GPU resources, ImGui/bgfx adapter, rendering diagnostics |
| `src/crimson-renderer/shaders/` | Shader sources and varying definitions; matching bgfx `shaderc` generates embedded headers |
| `src/tools/` | Docked editor panels and property controls |
| `src/assets/` | Managed asset imports and persistence; current texture ownership is documented in the architecture |
| `src/input/` | Editor navigation and input handling |
| `tests/`, `docs/` | Focused automated checks, architecture, and verification evidence |

## Repository constraints

- Put rendering implementation and shaders in `src/crimson-renderer/`. Keep editor panels and asset import/storage in their own boundaries; follow the architecture's current-versus-target ownership distinction.
- Preserve imported assets, material factors/assignments, and `crimson_ui.ini`. Changes to their storage must retain existing project data.
- Application branding lives in `assets/branding`; `src/platform/CrimsonResources.rc.in` embeds the ICO. SDL owns the native window-class icon lifetime. Keep branding out of the managed texture library.
- Treat generated headers and cached dependency sources under `build/` as read-only to manual edits. Change project sources/manifests/build configuration and let tools regenerate outputs.
- Rendering changes must preserve linear HDR lighting, one final sRGB encoding, and the grid/tools' independence from scene exposure. Texture roles determine color decoding and channels; follow the material contract in the architecture.
- Environment preprocessing belongs outside frame submission. Its derived cache is disposable; update preprocessing/BRDF versions when their equations or output conventions change. Preserve the managed source and `assets/environment.json`.
- Preserve explicit GPU/resource owners and release their resources before bgfx shutdown. Keep viewport input ownership explicit so editor widgets, navigation, and future player controls do not consume the same gesture.
- Use familiar asset fields adjacent to their property: picker, thumbnail/name, clear, and drag-and-drop with immediate assignment.
- Quader at `C:\Users\Drako\Desktop\quader-windows\quader-app` is the authorized navigation/grid reference. Read/copy the relevant code without changing that checkout.

## Completion

Complete authorized local implementation, necessary fixes, and focused verification without adding routine approval checkpoints. Report the result, actual checks, and any concrete remaining limitation. Update architecture status when features land, verification evidence when checks run, and these instructions when commands or repository constraints change.
