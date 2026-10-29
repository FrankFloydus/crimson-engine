# Qt application and Engine ownership

The complete Quader modeling application at the repository root owns the native application. `engine-core` supplies the Engine's asset, document, scene, simulation and renderer adapters. The application target and executable are `crimson-renderer` and `crimson-renderer.exe`.

## Composition and lifetime

| Owner | Responsibility |
| --- | --- |
| Root `src/main.cpp`, `src/app/app.*`, `src/ui/ui_system.*` | The single QApplication, ordinary main window, original modeling workspace, native event loop and application scheduling |
| Root `src/render/crimson/crimson_render_host.*` and `src/ui/qt_vulkan_composition_widget.*` | The shared renderer, Qt composition, native surface/backend selection, frame submission and presentation lifetime |
| `engine-core/src/integration/LevelSession.*` | Managed Engine level document, resources, scene extraction, isolated Play, physics and immutable prepared render packets |
| `engine-core/src/integration/LevelPanels.*`, `LevelViewportTools.*` | Engine docks, native viewport input, camera pilot/bookmarks, context actions and document transactions inside the same main window |
| `engine-core/src/integration/AssetDocuments.*`, `LevelDocumentPreviews.*` | Original typed asset-document controllers, File/Edit/save/conflict ownership and weakly bound document previews |
| `engine-core/src/integration/widgets/` | Full component Inspector, strict document properties, material graph and collision-mask widgets |
| `engine-core/src/integration/browser/` | Native catalog/folder/search/drag views, bounded CPU card images, explicit reference reports and guarded deletion/replacement |
| `engine-core/src/integration/paint/`, `src/tools/VertexPaintTool.*` | Qt event/painter adaptation, the retained paint gesture, native brush controls, geometry/visibility caches and immediate stroke release |
| `engine-core/src/integration/LevelNavigationPreview.*` | Authoring-only static navigation bake, supported-profile selection and cached Scene debug overlays; Play supplies its own borrowed navigation world |
| `engine-core/src/integration/settings/LevelPreferences.*`, `src/editor/EditorPreferences.*` | Native preference drafts and the existing application-wide preference file |
| `engine-core/src/profiling/`, `src/integration/profiling/` | On-demand capture/process policy, configured source identities and native menu/status controls; no independent application loop |
| Root `thirdparty/crimson-renderer` | The canonical renderer, resources, shaders and backend implementations shared with Quader |

The Engine borrows the renderer through `crimson-renderer/Renderer.h`. That header exports the canonical class alias. Native windows and devices are constructed by the root host. The retired Engine App, SDL host/window adapters and ImGui panels have been removed from `engine-core/src`.

Detach viewport gestures, Inspector callbacks, browser/reference requests, typed-document previews and material callbacks before replacing a LevelSession or its renderer. Asset document controllers outlive their child widgets until widget callbacks are detached. Release preview bindings and prepared resource leases before the root renderer shuts down. Surface resize and destruction keep the canonical renderer's queue-completion and retirement rules.

## Input and document ownership

`input/InputEvent.h` is a CPU value transport. It carries a logical surface identity, physical key identity, independent layout-aware character, exact modifiers, local logical-pixel coordinates and raw relative motion. It includes no toolkit window registry or native event-pump ownership. Quader's existing Qt relative-pointer implementation owns raw/fallback deltas and native capture.

`runtime/GameInput` retains physical WASD, jump and owned LMB trace edges. `input/Navigation` retains RMB flight, stationary-RMB context location, MMB orbit/pan, wheel speed and immediate cancellation. Capture acquisition discards transition motion. Focus/surface interruption releases the capture owner before the next render frame. Key repeats cannot restore canceled movement. Editor widgets, Scene gestures and Game input have separate explicit owners.

Persistent scene IDs, affine transforms, original imported hierarchy, scene history and all typed native asset formats retain their domain owners. Quader modeling documents remain complete modeling documents; Engine level data has its own SceneDocument and managed asset identity. Model import/drop uses the retained hierarchy and one document transaction for a multi-model delivery.

Engine project folders may open without a modeler manifest through `--project-root` or Engine → Open Engine Project. The accepted Engine root is independent of the current modeling project, so Link Current Modeling Scene may reference a different root. Original v1 `.crimson/editor-session.json` scene and free-camera entries restore without writes. Explicit saved metadata preserves the original panel/overlay fields. Project replacement resolves Play, document and level ownership before binding new owners.

The retained renderer-only ResourceRuntime watches selected shader sources/includes and managed asset locations, acknowledges own writes and refreshes relocated sources at notification boundaries. It drains resource publication metadata on every ordinary frame. ShaderReloadSession publishes complete accepted fixed-program families atomically; failed or superseded work leaves the last accepted group intact. Native document views reload clean external edits and retain dirty drafts; an accepted Play scene retains external scene changes until Stop. No reload path advances an additional renderer frame.

## Browser, preview and authoring boundaries

Browser folder edits and imports use BrowserCatalog and `AssetLibrary::queueImportFiles`. Drag payloads carry typed stable IDs. Reference reports capture immutable inputs only for an explicit request, run on the existing resource workers and reject stale/canceled publication. Their models retain clipped cached rows. Reporting does not publish replacement policy or rewrite authored files.

Deletion calls the original plan, role-compatible fallback candidates, confirmation and asynchronous transaction owner. Dirty/live/open/history references participate in that policy. Reference navigation uses stable scene/entity IDs and the ordinary unsaved-scene open flow.

Accepted model/scene/material/decal thumbnail readbacks and validated disk restores publish detached images into a bounded 64 MiB Qt cache. Texture/environment cards share TextureStreamer's existing exact per-mip resource requests; UI demand retains only the small requested mip and releases idle request interests. Paint surface preparation and held strokes suspend thumbnail jobs and Edit presentation clocks through LevelSession, preserving stable stroke visibility. Material appearance changes retain their independent presentation behavior.

The Decal document owns a composed preview with the original five neutral receiver shapes. Shape selection, camera navigation and reset remain transient. Its chart preparation, generated Decal material and immutable draw pins reuse the retained owners; its target and cached geometry remain independent of Material previews. Closing or replacing the document expires its host binding before retiring the preview. Material assignment accepts the Decal shader domain and uses the ordinary independent material document for editing.

Material graph output previews retain a transient 2D/3D mode per open document. Static 2D outputs share the asynchronous source-exact image owner with inline images, using separate 256 px and 128 px display requests. Contextual or animated outputs use the existing node shader and a fixed plane through the same material preview target. Flat mode suppresses camera gestures without changing the retained 3D camera or mesh. Selection and layout do not retarget the pinned output or enter asset history; closing/replacing the document releases both image sizes before their controller is destroyed.

## Preferences and build entry points

Preferences keep the existing v1 file at Windows RoamingAppData `CrimsonEngine/Editor/preferences.json`. Loading writes nothing. Apply/OK atomically publish validated drafts; Cancel discards drafts since the last Apply. Persistence failure preserves the accepted in-memory values. Appearance, grid colors/widths, shadow resolution, antialiasing and the original browser-size ticks feed the active host. Rendering → Renderer selects Direct3D 12 or Direct3D 11 for the next launch. The default remains Direct3D 12; `--renderer dx11|dx12` overrides one process without saving.

Both native backends borrow Qt-owned devices and use the same application frame owner. Direct3D 11 submissions and copies run inside QRhi's external-content scope. Detached same-adapter Qt devices use the canonical bounded shared-texture/keyed-mutex bridge; zero-timeout acquisition defers presentation while retaining that allocation's accepted pixels. Copy receipts preserve the original submitted source identity. Logical singleton preview owners and Qt destination allocation generations use process-monotonic tokens, so address reuse or a document rebind cannot accept an old queued image. Native health and confirmed retirement remain separate from normal busy retries.

Configure and build from the repository root. `engine-core/CMakeLists.txt` defines reusable libraries rather than another application shell. The explicit regression aggregates are:

- `crimson-engine-core-checks`: Engine CPU/domain and isolated native Qt fixtures.
- `crimson-modeler-checks`: the original modeling-domain and SDK consumer targets plus Full-profile API/Qt suites against the canonical Crimson host.

CTest label `crimson-modeler` selects the latter tests. `QUADER_BUILD_FILAMENT_REFERENCE=OFF` keeps historical Filament libraries/app/examples and GPU-specific reference tests outside the default build. Complete Quader source remains present. Mixed older unregistered Engine fixtures retain domain/regression evidence; active native Qt and CPU checks define the current application boundary.
