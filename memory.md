# Quader Agent Memory

This file records hard-resolved tasks whose context is easy to lose and costly to rediscover.
Read it after `AGENTS.md` for non-trivial work, but treat current code, tests, AGENTS guidance, and
the user's request as authoritative.

When adding an entry, keep it concise and useful: date, task/context, root cause or invariant,
affected owners/files, resolved approach, and verification. Do not record routine edits, secrets,
private user data, guesses, or unresolved issues. Prefer updating a related existing entry over
adding a duplicate.

## 2026-08-26 - Filament Normal Inputs Precede Material Preparation

Task/context: assigning a Normal texture in the Standard Material editor updated the draft and
preview texture but produced no lighting change in the isolated material preview.

Invariant:

- `quader_mesh_lit.mat` samples, scales, normalizes, and assigns `material.normal` before calling
  Filament's `prepareMaterial(material)`. Filament consumes tangent-space normal inputs during that
  call; assigning the field afterward leaves both the material preview and Rendered viewport on the
  geometric normal.
- Texture import, draft revisioning, render snapshots, texture upload/binding, preview tangent
  frames, OpenGL/DirectX Y convention, and signed normal strength remain shared existing paths and
  require no parallel normal-map implementation.

Owners: `resources/materials/quader_mesh_lit.mat` and the render material source-policy guardrail.

Verification: the focused guardrail compiles the material through the normal build dependency and
asserts that the normal assignment remains before Filament preparation; both canonical native app
configurations rebuild the corrected `.filamat`.

## 2026-08-18 - Hammer-Style Toggle Backface Selection

Task/context: ordinary component selection was permanently two-sided, with no persisted Hammer-style
control for excluding away-facing topology.

Invariant:

- `select.toggle_backface_selection` is an always-visible, checkable Select-menu command with the
  Source 2 Hammer default binding `Ctrl+Shift+F9`. `ViewportSettings::BackfaceSelection` is the
  persisted source of truth and defaults off for interactive viewport selection.
- Backface selection is only an orientation filter. It does not change render culling, marquee
  contain/crossing rules, or depth/select-through policy. Face candidates require front-facing
  authored winding when disabled; edges and vertices remain eligible when any incident pickable
  face is front-facing, and loose components remain eligible. Object and Group selection are
  unchanged.
- Click, hover, and marquee requests all carry the same policy. Stationary hover keys include it,
  and rectangle projections retain per-candidate facing so toggling re-resolves hits without
  rebuilding geometry or topology universes.
- Facing is evaluated in object space and XORed with the sign of the world-transform determinant;
  use a double-precision sign check so tiny valid reflected scales remain correct. Edge-loop
  context and priority picking use the same handedness rule.
- Public/modeling request defaults remain two-sided for backward-compatible automation and
  tool-specific callers. Viewport request builders explicitly inject the persisted default-off
  preference. Knife and other independent topology tools retain their explicit two-sided picking.
  This supersedes the older blanket invariant that all ordinary editor selection always picks
  backfaces.

Owners: command/preferences public APIs, `ViewportSettings`, Qt Select menu and viewport request
builders, modeling selection/picking services, neutral polygon picking facts, selection-box policy,
and hover/projection caches.

Verification: focused runtime picking/selection tests pass 30/30; focused command, settings, menu,
localization, API, service, hover, and contract tests pass 21/21; focused source/persistence
guardrails pass 5/5; the complete modeling-domain suite passes 442/442; and the intentionally
regenerated 157-command behavior baseline passes. The mandatory gate still reports the recorded
unrelated six repository guardrails, two Linear Array preview tests, and three source-wire overlay
tests; its SDK-only wrapper passes the consumer suite and inherits those same guardrail failures.
Both canonical Debug/Profile and Release app builds succeed.

## 2026-08-18 - Transform Preview, Knife, Slider, And Shift-Duplicate Continuity

Task/context: component transforms temporarily distorted generated texture mapping, Tool Settings
slider drags published avoidable live work, Knife depended on Edge selection and lost its cyan
topology context outside valid hover, Shift-gizmo duplicate applied object transforms twice, and
Repeat lost the duplicate-plus-transform action or reconstructed rotations around a moving pivot.

Invariant:

- Vertex/Edge/Face transform previews derive the same canonical positions, normals, generated UVs,
  static vertex stream, and indices as commit. Dynamic preview rendering refreshes both vertex
  streams plus the index buffer; position-changing polygon operations invalidate authored loop UVs
  so generated mapping remains the single procedural result. A source-suppressing component
  preview also inherits `MeshComponent::double_sided`; source-less generated previews retain their
  two-sided default, and preview cache identity includes sidedness because Filament culling is
  material-instance state. Backface selection remains a picking policy and never changes preview
  winding, normals, or culling.
- A pointer-held Tool Settings slider publishes only the latest value at a bounded 50 ms cadence.
  Holding it idle performs no owner work, release flushes an exact newer final value once, and
  keyboard, wheel, and spin edits retain their trailing dispatch behavior.
- Knife (`C`) activates and targets topology in every persistent selection mode without changing
  that mode. Its locked-target cyan source wire/depth stamp is persistent even when hover misses;
  only the valid edge/vertex highlight is volatile, and a face-interior start remains invalid.
- A direct object Shift-gizmo duplicate has one placement owner: the portable transaction owns
  duplication/history while the scene transform owns placement. Record repeat only after every
  transaction commits, and preserve the duplicate-plus-transform repeat descriptor across its
  internal duplicate command so successive Ctrl+R operations each duplicate and transform once.
- A repeatable Rotate/Scale records the original world pivot and reapplies the same world affine on
  every Ctrl+R; it must never substitute the post-operation selection center. Component replay
  re-enters the canonical per-object preview path so each object receives `W^-1 * A * W`, and
  Shift+Rotate retains the full `TransformExtrude` matrix instead of reducing it to the affine
  origin. Ordinary scene-driven transforms of direct SDK modeling objects are repeatable too;
  lights, instances, groups without a direct modeling object, and mixed scene selections are not.

Owners: SDK polygon position mutation, the modeling projection/transform/session services, dynamic
tool-preview render cache/factory, Knife command/runtime/input policy, and the retained Qt Tool
Settings panel.

Verification: task-focused SDK, modeling, render-cache, Knife, Qt slider, Shift Move/Rotate/Scale,
repeat/undo/redo, and source-guardrail tests pass. The primitive slider regression measures one
mesh compile/asset/scene publication during a held cadence window, zero while held idle, and one
newer exact publication on release, with no delayed duplicate. An actual inward-wound gizmo drag
regression covers single- and double-sided sources in Vertex, Edge, and Face modes, with exact
preview-to-commit geometry, render-extraction culling propagation, and stale preview-cache
rejection. Repeat rotation regressions cover ordinary and Shift+drag in Edge, Face, and Object
modes with exact successive 90-degree world-pivot steps; component Shift grows topology twice and
Object Shift grows the modeling-object count from one to two to three. The mandatory gate
still reports the recorded unrelated command-baseline, Linear Array, source-wire overlay, and root
instruction/CMake failures; its SDK-only wrapper inherits the same guardrail failures. Both
canonical Debug/Profile and Release app builds succeeded.

## 2026-07-26 - Large-Scene Work Is Bulk, Indexed, And Revision-Gated

Task/context: applying Pivot in a 6,276-entity stress scene froze the UI for about ten seconds,
and a cross-owner audit found the same scalar-in-batch pattern in scene, SDK, persistence, render,
picking, selection, history, and retained UI paths.

Invariant:

- Scene-to-SDK hydration is one `replace_workspace_preserving_clipboard` publication. Host history
  captures a current-workspace checkpoint that excludes nested SDK undo/redo history, while Pivot
  apply/undo/redo restores only the affected modeling document; a local mesh operation must never
  call `reset_from_scene()`.
- Multi-object work uses owner-level bulk/indexed operations: hierarchy replacement and projection,
  parent/selection/destruction batches, SDK duplication/publication, stable-ID vertex lookup,
  component-selection adjacency, project path/catalog/manifest indexes, and one native-ID range
  reservation. Scalar convenience calls must not be nested inside a batch.
- Render extraction and retained caches use exact hash/index lookup with equality checks and stable
  first-seen ordering. Component and Knife picking prepare mesh facts and surface hits once per
  eligible object. Stationary hover/paint and Tool Settings synchronization are gated by the narrow
  owner work key or settings revision before allocation, topology work, or Qt setters.
- Deterministic scale regressions use work counters, not wall-clock thresholds. They cover scene
  hierarchy/selection, SDK history and sparse IDs, polygon selection, picking and Knife, array
  publication, project paths/materials, render extraction/caches, selection bounds, and Pivot
  target-only synchronization.

Measured on the same Debug/Profile build and exact scene: QDR read stayed effectively flat
(118.752 ms -> 116.858 ms), session open fell from 9,494.51 ms to 225.328 ms, and Pivot Apply fell
from 10,737.5 ms to 32.193 ms. The post-fix run performed one initial full-scene hydration and one
targeted document synchronization for Pivot.

Affected owners/files: `src/scene`, `src/editor` modeling/pivot/selection/viewport services,
`src/sdk/core/modeling`, `src/io/project` and QDR hydration, `src/render`, public query/layer/tool
facades, assets, the docked Tool Settings UI, and focused scale/source-policy tests.

Verification: all 438 modeling-domain tests and 73 SDK consumer tests passed. The native app suite
passed 1,222 tests with three local-stress skips and only six pre-existing command/render failures;
the guardrail suite passed 360 tests with only three pre-existing instruction/build-policy failures.
The mandatory regression gate was run and additionally exposed the existing SDK-only GoogleTest
runtime mismatch. Both canonical Debug/Profile and Release app builds succeeded.

## 2026-07-26 - Tool Settings Is A Retained Dock With Owner-Level Reset

Task/context: Tool Settings moved from a transient `Qt::Tool` window into the normal dockable editor
workspace, gained collapsible Description and Settings sections, and added one bottom reset action.

Invariant:

- `UIPanelId::ToolSettings` is a regular `QDockWidget` surface. `QMainWindow` state owns placement
  and floating geometry, while `UISettings` owns logical visibility; auxiliary-window state must not
  regain a separate Tool Settings geometry or visibility record.
- `QtToolSettingsPanel` retains its section disclosure state and descriptor widgets. `UISystem`
  queries only `active_settings_revision()` on the frame path and refreshes the panel behind that
  unchanged-state gate. Activating a tool may raise an already-visible tab, but must not reopen a
  panel the user explicitly closed.
- Tool descriptions are public target metadata with localized presentation. Reset is one public
  `QuaderToolsApi::reset_active_settings()` intent routed to the active owner, which restores only
  that target's displayed settings in one coherent owner mutation and refreshes a live preview at
  most once. Primitive reset preserves the selected primitive kind.
- Tool-owned input recognition uses `QtToolSettingsPanel` ancestry and the native window of a
  floated owning dock, not a reusable object-name marker. Settings interaction therefore remains
  inside Pivot and primitive tool lifecycles without classifying unrelated windows as tool-owned.

Affected owners/files: public tools API and active tool services, `src/ui/qt_tool_settings_panel.*`,
the dock registry/settings/main menu/`UISystem`, localization, and focused UI/API/owner guardrails.

Verification: 264 focused UI/API/owner tests, the frame-loop guardrail, and all 53 metadata-loop
tests passed. Full CTest and the cross-owner regression gate were run; unrelated existing
command/render and instruction/build-policy failures plus the SDK-only GoogleTest ABI mismatch kept
the repository-wide gate red. Both canonical native app configurations succeeded.

## 2026-07-18 - Qt Transients Paint Before Native Viewport Submission Resumes

Task/context: modal prompts and popup menus could expose a white top-level window for seconds before
their controls painted.

Invariant:

- Quader's synchronous `QMessageBox` paths fully construct their labels and buttons before
  `exec()`. The shared stall was the app-owned precise frame timer continuing UI/render sync and
  Filament swapchain submission inside Qt's nested dialog/menu event loop on the GUI thread.
- `AppFramePresentationGate` tracks newly shown top-level Qt dialog, popup, tool, sheet, and drawer
  widgets. Native frame submission yields from Show through the first Paint and resumes from a
  queued turn after that paint returns. It must not pause for the full modal lifetime because live
  color-dialog viewport preview remains supported.
- Pending transients are independent and generation tagged. `QPointer`, Hide/Close/Destroy cleanup,
  and generation-matched release keep nested, destroyed, and re-shown widgets from stranding or
  prematurely releasing the gate. Main windows and ordinary children are excluded.

Affected owners/files: `src/app/app_frame_presentation.hpp`, the `App` frame timer, and app frame
presentation/source-policy tests.

Verification: the first-paint behavior test covers modal and menu paint, continued submission while
the modal remains visible, nested transients, and close-before-paint. The focused popup, input,
settings-preview, and close-boundary tests passed, followed by all six CTest entries. Final handoff
version is `0.1.65`.

## 2026-07-18 - Viewport Preferences Own Fly Defaults And Selection Cage Style

Task/context: Settings gained a default fly navigation speed plus color and thickness controls for
the bounding cage used by rotated meshes, group selections, and aggregate multi-selection.

Invariant:

- `DefaultFlyNavigationSpeed` is a persisted descriptor-backed viewport preference with compiled
  default `25.0`. `UISystem` applies it to the workspace on runtime attachment and Settings commit.
  Workspace propagation covers current/future pane and auxiliary cameras plus local-visibility
  backups; a temporary wheel-adjusted speed is preserved until reset, while an unadjusted current
  speed follows a changed default.
- Bounding-box color and thickness are descriptor-backed Selection settings consumed only by
  `ViewportOverlayStyleRole::GroupBounds`. Scene bounds, aggregate-cage geometry, snapshot
  extraction, and persistent overlay cache ownership remain unchanged.

Verification: descriptor defaults/counts, persistence and clamping, localized dialog Apply/Cancel,
runtime style resolution, camera reset semantics, workspace propagation, metadata parity, and
source consumers passed their focused tests and all six CTest entries.

## 2026-07-18 - Selection Bounds Use Tight Geometry Extents And Include Instances

Task/context: selected irregular meshes showed oversized cages after rotation, while formal
instances and instance-backed linear/radial array results showed selected fills and wires but no
bounds cage.

Invariant:

- Scene-owned mesh bounds are the world AABB of transformed geometry vertices,
  `AABB(world * vertices)`, not the conservative `AABB(world * local AABB)`. Formal occurrence
  bounds union every visible mesh member through its definition-relative and occurrence transforms.
- Selection extraction enrolls both direct meshes and formal occurrences. Mixed selections keep
  one aggregate cage, while linked arrays contribute every selected occurrence.
- Bounds caches retain exact translation reuse. Rotation/scale recomputes vertex extrema with one
  matrix conversion per mesh, definition dependency signatures are shared across occurrences in
  one refresh, and member transform/visibility/mesh revision changes invalidate occurrence bounds.
- Copy Pivot intentionally keeps its reference add-on transformed-local-AABB dual-center formula;
  its test oracle must not be coupled to the scene/render extent implementation.

Affected owners/files: `src/scene/scene_bounds.*`,
`src/render/overlay/render_overlay_snapshot.*`, Copy Pivot compatibility, and render/array behavior
tests.

Verification: exact rotated asymmetric geometry, multi-member instances, mixed selections, local
visibility, linked linear arrays, and non-axis-aligned radial arrays passed 29/29 focused tests.
The policy-selected affected workflow passed all native guardrail/behavior/modeling-domain suites,
SDK consumer/parity suites, and all six CTest entries. Final handoff version is `0.1.64`.

## 2026-07-18 - The Modeling Refactor Ends With One Portable Owner And No Mirror Tree

Task/context: Phase 6 removed migration debt and closed the Phase 0-6 architecture refactor at
app version `0.1.62`.

Invariant:

- SDK documents, operations, typed identity, modeling selection/preview/history/dirty state,
  portable QDR, and neutral snapshots have one source under `src/sdk/core/modeling`. The 29
  low-level geometry/polygon headers live only in the source-private `neutral/include` root;
  `src/mesh` and SDK-internal header mirrors must not return or become installed API.
- The native editor remains the host facade and owns scene entities, assets/materials, project
  durability, commands, viewport policy, Qt, Win32, and Filament. Projection adapters translate
  SDK results but never retain a second modeling truth.
- Taskflow and the unused QDR parallel helper are absent. Portable stress input is explicit through
  `QUADER_STRESS_QDR`; fatal behavior is tested in an isolated UI-suppressed child. Clean CI owns
  SDK-only MSVC, GCC/Clang, sanitizer, and full pinned native Windows lanes.
- Exactly four 0.1.x SDK compatibility shims remain in `COMPATIBILITY.md`; each has an owner/test
  and is reconsidered for removal in 0.2.0. They are supported compatibility, not migration debt.
- Translate-preview mutation must not eagerly compile its renderer-neutral mesh/overlay payload.
  `ModelingSession` advances the preview revision and invalidates the cached payload on update;
  `preview_payload()` materializes it on demand. This keeps the 100,344-triangle three-update
  workload near `198.6392/205.7955 ms` instead of the observed eager `~2.8 s`, while preserving
  payload contents and checksum `65`.

Verification: the consolidated Windows graph and all six CTest entries passed; the final
lazy-payload correction then passed its affected SDK consumer/parity and guardrail route, native
link/deploy, Release fixed-host samples, and Vulkan/Qt/Filament boot smoke. Version sources match,
JSON/YAML parse, old SDK/Taskflow/include scans are clean outside negative guardrails, and
`git diff --check` reports no errors. Hosted workflow execution begins on the first authorized
commit/push; no hosted result is claimed for the uncommitted handoff.

## 2026-07-17 - Build, Header, Command, And Test Owners Match Their Packages

Task/context: Phase 5 aligned the post-migration production graph and moved source-policy checks
out of the native behavior failure domain.

Invariant:

- `quader_editor_api` owns editor-wide API implementations, `quader_editor_viewport` owns editor
  viewport integration, `quader_editor_services` owns low editor services, and
  `quader_viewport_services` owns toolkit-neutral viewport adapters. A consumer links an owner; it
  never compiles that owner's source directly.
- SDK granular API headers own their declarations and compile standalone; `modeling_api.hpp` is the
  root facade and `modeling.hpp` remains the convenience umbrella. Native result, preview, and
  selection contracts have narrow headers where consumer groups are genuinely disjoint.
- `EditorCommandRegistry` executes descriptors, `QuaderKeybindingService` owns effective
  shortcut/conflict/persistence policy, and `QuaderKeybindingTargetCatalog` owns stable target and
  scope presentation behind the unchanged public facade.
- `command_result_from_modeling` is the native modeling-to-history result adapter. ASCII-only
  normalization uses `QStr::to_ascii_lower`; wide filesystem folding, Qt Unicode lowercasing, and
  isolated parser normalization stay local because their semantics differ.
- Pure source/CMake checks run in the existing guardrail executable. Runtime assertions stay in
  native/modeling owners. Do not create another test shard unless a new measurement materially
  improves the reliable P0 link baseline; file size alone is not evidence.

Verification at v0.1.60: all separated owner targets, four standalone SDK headers, both test
executables, and the native app built. Full CTest passed 6/6, including isolated SDK-only install
and external consumption. Final enumeration was 148 suites/1,139 native tests and 47 suites/349
guardrails; the relocation lost no test and added 15 split/ownership checks. Representative
affected routes select every new owner directly.

## 2026-07-17 - The First Production Modeling Slice Has One SDK Truth

Task/context: Phase 2 proved the native/portable owner boundary by cutting the existing
box/select/component-translate workflow over to an SDK-owned session.

Invariant:

- Every `SceneSession` constructs exactly one `ModelingSdkProjectionAdapter`, which owns one
  workspace-local SDK `ModelingSession` and only identity/projection bookkeeping. Workspace or
  active-view changes never transfer its state.
- Native box entry points invoke SDK box semantics. SDK-backed object/component selection and
  component translate preview/commit/cancel remain SDK-owned; the host publishes mesh assets,
  scene bindings, material/identity, selection, history, and render revisions through
  `ModelingMeshSyncService`. The replaced native box semantic branch is absent.
- Cross-owner publication checkpoints every SDK/asset/scene/mapping boundary. Failures restore all
  semantic host state; allocator watermarks remain forward-only across SDK/native handoff and
  rollback. Primitive rollback rebases current host metadata onto its pre-tool SDK snapshot, so
  concurrent visibility edits and selection history survive.
- Unsupported object/group/multi-object workflows hand the complete relevant SDK set to the native
  runtime once. SDK selection synchronization publishes one exact scene selection and never clears
  and re-adds it during retained native-tool changes. Failed project open preserves the old
  workspace preview; successful replacement clears it once.

Performance and verification:

- Release raw A/B samples are under `build/perf-vs2026/p2-t07`. P0-T09 -> P2-T07 results were:
  transform preview `287.5700/309.2713 -> 216.6368/204.4622 ms`, object overlay
  `5195.2893/5314.4647 -> 3996.2134/3943.2595 ms`, all-face overlay
  `9069.4068/8914.4497 -> 6669.8136/6640.7399 ms`, and 5,000 navigation steps
  `1.8651/1.9055 -> 1.4971/1.4942 ms`. Every workload checksum matched.
- Render cold/unchanged snapshot pairs were `23723/45 us` and `21572/47 us`; camera-only pairs
  `21922/38 us` and `20906/41 us`; transform refresh `21877/21623 us` and `23080/20501 us`;
  cached hover/preview/wireframe reuse remained below timer resolution with matching checksums.
- Deterministic adapter counters require exactly one compile/asset/scene publication for a
  committed component move, one preview publication for an update, and no publication from 16
  repeated selection/object queries. The consolidated Debug CTest preset passed 6/6, including
  1,221 native tests, guardrails, parity, SDK consumer/modeling-domain tests, and isolated SDK-only
  configure/install/external-consumer build. Final native v0.1.57 build and Vulkan/Fusion smoke
  close the phase handoff.

## 2026-07-18 - Portable Modeling Contracts Have One Safe Transaction And Identity Model

Task/context: Phase 1 corrected the portable SDK contracts before production native cutover.

Invariant:

- Polygon vertex/face IDs are monotonic within a document lineage; session-object high-water state
  lives outside undo snapshots, old copies cannot overwrite newer lineage state, QDR persists the
  allocation cursors, and edge identity/staleness derives from its endpoint identities.
- A `ModelingSession` owns exactly one active preview transaction across direct transform and public
  handle entry points. It captures a unique typed ID and base revision, and commit/cancel/destructor
  resolution occurs through that registry exactly once.
- Checked batch callbacks operate behind one session guard, return a real receipt that must match
  the observed mutation journal, and cannot nest, preview, rewind history, create unscheduled
  objects, or mutate unrelated state. Every failure closes the guard and restores the history mark.
- Public unbound handles/facades fail deterministically, retained document access is an owned
  immutable snapshot, `Result<T>` has exclusive value/error storage, typed selections enforce their
  runtime kind, and renderer-neutral payloads support only their truthful `Owned` lifetime.
- Native QDR uses the public exact `AuthoredTopology` seam rather than an SDK private header. Scene
  bulk upsert preflights identity, allocation, index, and scene assignments off to the side and
  publishes them atomically.

Compatibility: the borrowed document pointer and void batch callback are deprecated additive
shims. Unsupported SDK pivot/profiling placeholders are deprecated for removal in 0.2.0 and are
not advertised by the supported tool catalog; Phase 6 owns the refreshed external-consumer
decision.

Verification at app version `0.1.56`: SDK consumer/parity/modeling-domain/guardrail/native test
targets built; registered CTest passed after the declaration-documentation guard was satisfied;
the isolated SDK-only install/external-consumer check passed; and the native `quader` executable
linked and deployed. The Qt translation-catalog warning remained non-fatal.

## 2026-07-17 - Phase-Zero Verification Owners Are Total And Phase-Batched

Task/context: P0-T05/P0-T07/P0-T08 closed the remaining false-green architecture, clean-build, and
test-placement gaps. Per the user's explicit execution rule, implementation is integrated for a
whole phase before one consolidated build/test pass; Phase 1 onward is coordinator-only.

Invariant:

- `tests/dependency_matrix_guardrail_tests.cpp` derives 65 live/templated first-party targets and
  93 direct project edges from the two owning CMake files. Target-policy coverage is bidirectional;
  unknown/stale/missing/forbidden edges fail. Four used reverse-owner exceptions each name an owner
  and the P4-T05/P5-T01 removal sequence.
- `.github/workflows/windows-sdk.yml` is the no-Filament clean owner: Windows Server 2025/VS2026,
  hosted-vcpkg revision plus manifest cache identity, native/OpenMesh off, explicit guardrail/SDK
  consumer builds, and unfiltered SDK-only CTest including install/external-consumer verification.
  It makes no parity/native claim; the controlled native CI artifact decision remains P6-T04.
- Exactly 17 pure source/CMake checks moved from `quader_tests` behavioral sources to
  `quader_guardrail_tests`: three math-header, four QDR/projection-owner, and ten render-owner
  checks. Runtime behavior stayed in the native executable. Architecture status must acknowledge
  live typed IDs/results, SDK viewport input DTOs, scene/render reuse gates, and existing QDR
  golden/seeded coverage rather than recommissioning them as absent.

Verification at app version `0.1.55`: configure passed; all five executable CTest owners built;
CTest passed 6/6 (including SDK-only install/external consumer); the Debug `quader` executable
linked and deployed. The Qt translation-catalog warning remained non-fatal.

## 2026-07-17 - Migration Performance Uses Raw Fixed-Host Pairs And Deterministic Gates

Task/context: P0-T09 established reproducible evidence for later modeling, viewport, render, and UI
hot-path migrations without introducing flaky shared-CI timing limits or another benchmark system.

Reference environment and protocol:

- Base revision `f41414476d023d8603919629f84293a509c70dd2` plus the active Phase 0 working tree,
  app version `0.1.51`; MSVC `19.51.36248.0`, Visual Studio 18 2026 x64 Release, Tracy OFF.
- AMD Ryzen 9 7900X (12 cores/24 logical), 31.2 GiB RAM, Windows 11 Pro `10.0.26200` build 26200.
- Configure exactly with `cmake -S . -B build/perf-vs2026 -G "Visual Studio 18 2026" -A x64
  -DCMAKE_TOOLCHAIN_FILE="$env:USERPROFILE/vcpkg/scripts/buildsystems/vcpkg.cmake"
  -DVCPKG_TARGET_TRIPLET=x64-windows -DQUADER_BUILD_TESTS=ON
  -DQUADER_BUILD_PERFORMANCE_BENCHMARKS=ON`.
- Build exactly with `cmake --build build/perf-vs2026 --config Release --target
  qdr_performance_baseline qdr_fixture_performance_baseline qdr_phase8_performance_baseline
  --parallel 24 -- /nodeReuse:false`.
- For each accepted harness, run one complete identical process with output discarded, then two
  immediate recorded processes while no compiler/build workload is active. Raw JSON stays under
  `build/perf-vs2026/p0-t09/`. The tracked QDR fixtures are legacy JSON and the fixture benchmark
  rejects them; no failed fixture sample is part of this baseline.

Recorded commands and raw A/B results:

- `qdr_performance_baseline --tier local --output <A-or-B>.json` generates four objects and
  100,344 triangles. Transform-preview totals for three updates were `287.5700/309.2713 ms`;
  5,000 viewport-navigation steps were `1.8651/1.9055 ms`; object overlay extraction was
  `5195.2893/5314.4647 ms`; all-face overlay extraction was `9069.4068/8914.4497 ms`.
- `qdr_phase8_performance_baseline --output <A-or-B>.json` uses 256 objects, grid segments 16,
  and 40 iterations. Cold/unchanged snapshot totals were `23659/51 us` and `23140/47 us`;
  camera-only snapshot totals were `23580/42 us` and `23401/60 us`; single-object transform
  refresh totals were `23186/23474 us`; cold/cached wireframe-overlay totals were `1618/0 us`
  and `1668/0 us`. Here the paired fields mean cold work versus reuse, not software-version
  before/after, and a reported `0 us` means below the timer resolution rather than zero cost.
  Every A/B deterministic checksum matched, but those checksums are workload/shape sentinels;
  ordinary behavior tests remain the semantic authority.
- Developer-iteration comparisons use `powershell -NoProfile -ExecutionPolicy Bypass -File
  tools/affected_check.ps1 -ChangedPath <path> -MeasureFull`; record its raw before/after values
  only on the same build/host rather than enforcing a shared millisecond limit.

Ordinary deterministic gates are
`ModelingSessionServiceTests.RepeatedComponentDragSampleSkipsPreviewAndSceneResync`,
`ModelingSessionServiceSplitTests.SceneSessionDirtyPollingUsesConstantTimeModelingRevision`,
`RenderOverlaySnapshotCacheTests.StableSourcesReuseAllOverlayExtractionsUntilOwnerRevisionChanges`,
`RenderOverlaySnapshotCacheTests.TransformOnlyRefreshMatchesFullSelectionSnapshotWithoutReordering`,
`RenderSceneSnapshotTests.RefreshesOnlyPureTransformSnapshotsAndRejectsOtherSourceChanges`,
`ViewportOverlayPolicyTests.QtViewportExtractsOneSceneOverlaySnapshotBeforePaneComposition`,
`QtViewportRenderSubmissionPolicyTests.NavigationSubmitsOnlyTheInteractionPane`, and
`BuildGuardrailTests.QtFrameRefreshKeepsExpensiveChromeEventDriven`.
A counter/revision/checksum regression is a test failure; timing drift is evaluated only against
repeated same-host raw observations and must be explained, optimized, rolled back, or accepted.
These are headless CPU baselines, not native Filament/GPU/FPS measurements, so later frame-loop
work still requires immediate same-build/same-scene native frame-time or FPS evidence. Because a
dirty Phase 0 worktree is not reconstructible from HEAD alone, each future hot-path task reruns the
relevant harness immediately before its change. No accepted QDR-load baseline exists: the tracked
`.qdr` files were legacy JSON and the rejected fixture attempt is excluded.

## 2026-07-17 - Native Debug Tests Use Deterministic Non-Incremental Linking

Task/context: P0-T03 addressed recurring pre-main MSVC incremental-link corruption in the large
native `quader_tests` process without changing compilation or other executable policies.

Invariant:

- Only `quader_tests` under MSVC Debug receives `/INCREMENTAL:NO`. Release and RelWithDebInfo
  policy, parallel compilation, Tracy, test names, and other targets remain unchanged.
- Generated `LinkIncremental=false` plus the link tlog are the authority for the effective option;
  source text alone is insufficient evidence.

Verification: the old incremental one-source build/link took 24.059 s and artifact-clean link took
12.935 s. Five post-fix real edit/build/list cycles took 18.021, 17.535, 17.676, 18.141, and
13.465 s (16.968 s mean), each enumerating 164 suites/1,226 tests. The post-fix artifact-clean link
took 10.040 s, and isolated native CTest passed 1/1.

## 2026-07-17 - SDK-Only Configuration Has No Native Modeling Graph

Task/context: P0-T04 made the supported no-native-app matrix genuinely independent of OpenMesh and
native modeling rather than merely omitting the final app executable.

Invariant:

- With `QUADER_BUILD_NATIVE_APP=OFF`, native profile/Tracy, OpenMesh, geometry, polygon/runtime,
  editor metadata, modeling-domain tests/loops, parity, benchmarks, and app targets are absent.
- SDK libraries, SDK examples, SDK consumer tests, guardrails, install/export, and the recursive
  external-consumer check remain available. The isolated check forces OpenMesh topology/fetch off
  and an empty source path, then validates the CMake file-api graph and CTest inventory.
- The full-native default graph is unchanged; no native-modeling-only option was added.

Owners/files: `cmake/QuaderModeling.cmake`, `cmake/QuaderSdkOnlyBuildCheck.cmake`, and the focused
build guardrail.

Verification: the isolated graph contained only SDK/consumer/guardrail/example targets, passed its
two executable CTest owners, installed the SDK, and built/ran a clean external consumer. The native
matrix retained OpenMesh and all modeling/domain owners; 630 domain and 55 consumer tests passed.

## 2026-07-17 - Dirty Polling Uses Content Revisions And Honors Preview Checkpoints

Task/context: P0-T06 removed selection enumeration from frequent scene dirty-state polling while
preserving the editor's transient-preview contract.

Invariant:

- `SceneSession::content_revision_stamp()` reads `ModelingSessionService::content_revision()`
  directly; it never builds a `selection_summary()`.
- Selection-only changes do not change the content stamp. A live owned preview may advance its
  projected scene/asset/modeling revisions but remains clean only while its checkpoint began clean,
  matches the expected stamp, and is uncontaminated.
- Commit removes preview masking and reports dirty. An unrelated edit during preview remains dirty;
  cancel restores the prior clean state.

Verification: the focused source/behavior filter passed 5/5 and isolated `quader_tests` CTest
passed 1/1 after running all 1,226 tests.

## 2026-07-17 - Modeling Test Prerequisites Stop At The Caller Boundary

Task/context: P0-T02 repaired modeling-domain prerequisites whose fatal assertion returned only
from a helper and allowed the calling scenario to continue into unsafe indexing or dereferences.

Invariant:

- `require_test` returns `::testing::AssertionResult`; the caller owns fatal control with
  `ASSERT_TRUE` or explicitly propagates and checks the result before using dependent state.
- Value-producing fixture helpers use checked result/out-parameter flow. They do not hide fatal
  assertions in non-void helpers.
- Independent workflows have independent GoogleTest registrations, so one failed prerequisite
  cannot suppress unrelated scenarios in the same aggregate test body.

Owners/files: `tests/modeling_domain/test_support.hpp`, its modeling-domain callers, the dedicated
prerequisite regression fixture, and `cmake/QuaderModeling.cmake` registration.

Verification: an injected failure left its following sentinel untouched and the next test ran;
registrations increased from 529 to 630; the main, polygon-loop, and runtime-loop executables
passed; the complete CTest matrix passed 6/6; the native v0.1.51 target built.

## 2026-07-17 - Affected Checks Build And Execute Every Selected SDK Owner

Task/context: P0-T01 closed false-green routing where portable SDK sources omitted SDK tests and
`tests/sdk` filters ran against the unrelated native test executable.

Invariant:

- Every `src/sdk/core/modeling` implementation or public-header change builds and executes the SDK
  consumer and native parity owners; public headers also run the native-owner-free guardrail, while
  canonical geometry/polygon paths retain their focused native loops.
- Each `tests/sdk` source runs only its owning executable and parsed suite. Full verification builds
  all five executable CTest owners before unfiltered CTest executes all six entries, including the
  script-driven SDK-only external consumer.
- A focused command cannot pass with a missing executable, a missing generic command, a stale exit
  code, or a GoogleTest filter that selects zero tests.

Owners/files: `tools/affected_check.ps1` and `tests/build_guardrail_tests.cpp`.

Verification: seven table-driven PlanOnly cases and deliberate missing/zero fixtures passed; a
temporary SDK-header compile failure stopped the SDK consumer build; a real SDK source run executed
55 consumer and 2 parity tests; the full affected workflow built five owners and passed CTest 6/6;
the native v0.1.48 target built.

## 2026-07-17 - Architecture Direction And Refactor Execution Have One-Way Authority

Task/context: P0-T00 reconciled the legacy AR-0 through AR-10 migration roadmap with the approved
P0 through P6 implementation plan before production refactoring began.

Invariant:

- `architecture-review.md` owns fixed architecture direction, boundaries, and explicit deferrals;
  its `AR-*` labels are historical evidence and never executable work IDs.
- Root `REFACTORING_PLAN.md` is the sole implementation/progress tracker. Every retained legacy
  phase and numbered action maps to a canonical `P*-T*` task, Complete status, or explicit
  rejection/defer decision.
- Root/nested instruction routing and `tools/affected_check.ps1` treat both authority files as
  guardrail-owned inputs. The completed `refactor.md` ledger is historical only.

Owners/files: `AGENTS.md`, `architecture-review.md`, `REFACTORING_PLAN.md`, `refactor.md`,
`tools/affected_check.ps1`, and the instruction/build guardrail tests.

Verification: PlanOnly selected the focused guardrail workflow for both authority documents; the
focused governance suite passed 14/14, full CTest passed 6/6, and the native v0.1.46 target built.

## 2026-07-16 - Home Chrome Is Project-Only And Startup Readiness Is Position-Agnostic

Task/context: adding movable Qt toolbars left projects stuck behind the Home page because startup
presentation treated dock/tool-bar contact with the client edges as a readiness condition. The Home
page also exposed editor chrome before a project existed.

Invariant:

- Startup readiness validates only that the visible shell widgets have valid geometry; it never
  approves a layout from toolbar/dock positions, edge contact, or ordering. The geometry signature
  may still observe positions solely to determine that Qt has stopped changing them.
- Home shows only New/Open, recent projects, and Remove. Menu/status bars, editor toolbars, and docks
  are hidden until a project workspace is active.
- Entering Home captures the workspace toolbar visibility and serialized `QMainWindow` state.
  Closing Quader from Home persists that captured workspace state, not Home's temporary hidden
  chrome. Returning to a project restores the user's toolbar visibility and panel settings.

Affected owners/files:

- `src/ui/ui_system.*`
- `src/ui/qt_project_home_page.*`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/qt_project_workspace_tests.cpp`

Verification:

- Focused Home/workspace and toolbar guardrails: 5/5.
- Full CTest: 6/6 targets passed.
- Vulkan/Fusion boot smoke confirmed the project-only Home presentation.

## 2026-07-16 - Snapping Toolbar Is Presentation-Only And Master-Gated

Task/context: the top workspace needed retained grid/angle controls and five snapping buttons,
including a global snapping master, without creating another UI-owned settings source or restoring
the per-frame toolbar performance regression.

Invariant:

- `ViewportSettings` owns the persisted global, grid, vertex, and rotation snap toggles plus the
  rotation increment. The master gates every snap mode without overwriting the individual choices.
- Rotation increments come from the exact canonical catalog `0.25, 0.5, 1, 5, 5.625, 11.25, 15,
  22.5, 30, 45, 90`; grid sizes come through the viewport host-contract adapter. Qt never reaches
  into editor metadata directly.
- `QtSnappingToolbar` dispatches grid preset and preference intent through `QuaderEditorApi`. Its
  retained snapshot returns before shortcut lookup, formatting, or Qt property setters when owner
  state is unchanged; it never enumerates the command catalog during frame refresh.
- The toolbar is ordinary movable/floatable Fusion chrome. It participates in workspace window
  state, localization, readiness geometry, and the Panels menu, and remains hidden on Home.

Owners: `ViewportSettings`, `host_viewport_contract_adapter.*`, `QuaderPreferencesApi`,
`QtSnappingToolbar`, and `UISystem`.

Verification: focused toolbar/API/settings/guardrail coverage passes, full CTest passes 6/6, the
native v0.1.29 target builds, and boot smoke confirms the project-only Home presentation.

## 2026-07-13 - Pivot Apply And Cross-App State Ownership Audit

Task/context: the native Pivot tool visually moved its preview but Apply left the transform gizmo
at the original origin; Tool Settings Previous/Next canceled the tool, repeated `P` activation was
unreliable, and activating Pivot on a newly created off-origin mesh put its handle at the raw entity
origin instead of the Move gizmo's visible bounds center. After restoring the ImGui-main lifecycle,
ten parallel audit passes reviewed state truth, history atomicity, tools/selection, input/cache
invalidation, scene/assets, persistence, render snapshots, Qt/public routing, build guardrails, and
end-to-end determinism.

Pivot invariant:

- The retained Tool Settings QWidget and its exact top-level native `QWindow` are internal Pivot
  interaction targets. Their mouse events never trigger outside-tool cancellation; unrelated
  windows remain external.
- `P` remains a canonical keybinding/typed-tool activation. A handled activation request is
  independent of a tool-ID change, so it can restore a stale native session without restarting a
  live preview. Apply rebases the mesh/group origin through `PivotToolService`, commits one history
  item, resets modeling projection, and immediately refreshes the transform-gizmo pivot. Undo and
  redo move the gizmo with the persisted pivot while world geometry and descendants stay fixed.
- `scene_transform_pivot.*` is the one committed visible-pivot owner used by Move queries, persistent
  Pivot activation, Copy Pivot validation, and scene transform previews: a custom pivot is the
  parent-aware world origin; otherwise it is the visible entity/hierarchy bounds center. Fresh
  meshes may bake off-origin coordinates into vertices while retaining an identity transform, so
  the raw entity origin is never a valid substitute for their default visible pivot.
- Persistent Pivot initializes its preview at the exact currently visible Move pivot and keeps its
  no-drag comparison against the committed visible pivot. A released Quick Pivot can therefore be
  baked intentionally by Apply; successful Apply clears the temporary override so it cannot mask
  the persisted pivot or Undo. Cancel preserves the temporary Quick Pivot.
- Persistent Pivot sessions retain the captured entity UUID, parent, mode, active target,
  visibility, lock, and editability contract. Scene or asset revision changes revalidate those
  target-specific invariants instead of canceling merely because unrelated state changed. Project
  replacement, selection-mode changes, Undo, or explicit cancel clear native and modeling
  previews before another owner mutates state.

State ownership invariant:

- Project New/Open/Exit/native close share Save/Discard/Cancel policy. Open preflights before its
  read, Save never persists an uncommitted preview, and replacement cancels old-document transient
  state before installing the new document. Scene and asset replacement epochs advance beyond the
  old document, default assets are rebound by their complete semantic contract, and history is
  cleared once.
- Transform, hierarchy, asset, and history mutations validate completely before application.
  Non-finite/singular transforms, malformed asset payloads, failed recursive duplicates, and
  multi-command cancel failures roll back atomically. Authored polygon payloads are deep snapshots;
  shared scene meshes use clone-on-write; mixed mesh/light duplicate history preserves both runtime
  object count and exact scene selection.
- Runtime tool activation preflights before canceling competing state, then cancels previews only
  through their native/modeling owners. Invalid activation preserves the current preview. Scene
  transform capture distinguishes an empty selection from an unsafe singular hierarchy and never
  falls back to an identity inverse; Group/Ungroup preflight every reparent before mutation.
- Modeling-to-scene projection prebuilds and validates the complete plan before publishing assets,
  entities, or selection. Creation failures restore the runtime, scene, assets, selection, preview,
  and history checkpoint. Sync validity checks the exact modeling ID, material slot, installed mesh
  asset, and known signature; asset allocators never publish the invalid ID sentinel.
- Native and SDK geometry, polygon, and portable QDR targets compile the one canonical
  `src/sdk/core/modeling/internal` implementation. Duplicate native `.cpp` owners are removed,
  compatibility headers are parity-guarded, and the richer plane-cut behavior is canonical. The
  SDK-only manifest disables native host dependencies and its clean install/consumer check rejects
  Qt or other host leakage.
- QDR writes validate document identity/references before replacement and reject multiple active
  selections. Multi-file OBJ export validates/stages every destination and restores all prior
  files on failure. Ordered viewport input processes every same-frame edge, pending chords own
  their continuation, wheel bindings precede zoom fallback, and hover cache keys include resolved
  grid-snap state.

Verification:

- 69 focused Pivot/Copy/activation/projection/asset/hierarchy regressions passed.
- Native `quader_tests` passed 876/876, modeling-domain tests passed 508/508, guardrails passed
  183/183, and full CTest passed 6/6, including SDK consumer/parity checks.
- The final native `quader` v0.0.104 executable and dependency-tracked runtime resources built
  successfully after this record was added.

## 2026-07-12 - Pivot Tool Settings And Repeated Activation Preserve Its Lifecycle

Task/context: applying the persistent Pivot tool appeared to restore its original position,
Previous/Next in Tool Settings closed the tool, and pressing `P` could not reactivate Pivot.

Root cause:

- `QtViewportPanel` installs an application-wide mouse event filter. It treated Tool Settings
  controls as outside-pointer events, so a frame could cancel the native Pivot session between a
  button press and its Apply, Previous, or Next click handler.
- Viewport shortcut handling reported only whether the selected tool ID changed. A valid repeated
  `P` activation was handled while Pivot was already selected, but Qt received no request to
  activate the native tool owner again.

Invariant:

- QObject descendants of the retained `floating.tool_settings` window are tool-owned interaction
  targets and never queue outside-modal cancellation. Other dialogs, docks, menus, and outside
  pointer events remain external and keep their normal cancellation behavior.
- A handled `ActivateTool` shortcut emits `tool_activation_requested` independently of
  `tool_changed`. Qt dispatches editor tool activation only when the selected ID changed or the
  editor's active tool does not match the requested tool. This restarts a stale/ended native Pivot
  session without resetting a live moved preview when `P` is pressed twice. `P` remains the
  catalog/keybinding-system shortcut; no raw Qt key branch owns Pivot activation.

Affected owners/files:

- `src/ui/qt_viewport_panel.*`
- `src/editor/tools/viewport_tool_runtime.cpp`
- `src/viewport/viewport_input_types.hpp`
- `tests/qt_viewport_pane_activation_tests.cpp` and
  `tests/viewport_tool_catalog_tests.cpp`

Verification: 15 focused Pivot lifecycle/keybinding/policy tests passed, as did the 263-test
affected viewport/input/UI set and 45 affected UI/public-API guardrails. Full CTest passed 6/6
entries, and the final v0.0.102 native `quader` executable built successfully.

## 2026-07-12 - Copy Pivot From Active Is A Native Atomic Pivot Rebase

Task/context: Object mode needed a native operation that transfers the active mesh's visible pivot
offset to every other selected editable mesh without moving geometry, descendants, or selection.

Invariant:

- `PivotToolService` owns this workflow. It is exposed through `QuaderModelingApi`, the typed
  `modeling.copy_pivot_from_active` command, and a native public action; it is not an
  `EditorOperation`, portable SDK operation, runtime metadata entry, QDR feature, or Lua binding.
- The source pivot is the custom world origin when present and otherwise Quader's visible
  world-bounds center. Its offset from the average of the world bounds center and world
  mean-vertex center is transferred through source inverse rotation and target rotation. World
  centers include scale, the rotation basis excludes scale, and the offset is never normalized by
  mesh size. Authored polygon vertices are preferred for the mean, with stable CPU vertices as the
  fallback.
- This is the reference add-on's absolute-offset rule: congruent translated/rotated meshes map an
  authored AABB lower-left pivot to the corresponding target lower-left. Differently sized targets
  retain the same absolute offset from their hybrid center rather than normalizing to target size.
- Object mode, one visible active mesh, and at least one other selected mesh are required. Selected
  non-mesh entities are ignored; every mesh target is validated before mutation. A locked source
  is valid because it is read-only, while a locked, hidden, missing, non-finite, or non-editable
  target rejects the entire operation.
- Targets are applied parent-before-child with entity ID as the stable tie-break. Each rebase plan
  is resolved immediately before application, reuses geometry and child compensation plus
  shared-asset clone-on-write, and participates in one atomic history item named
  `Copy Pivot From Active`. Undo and redo preserve stable asset IDs, world geometry, descendant
  world transforms, scene selection, and the active entity. Effective-pivot epsilon no-ops create
  no history.

Affected owners/files:

- `src/editor/pivot/pivot_tool_service.*`
- `src/api/quader_modeling_api.*`, `src/api/quader_command_ids.hpp`, and
  `src/api/quader_commands_api.cpp`
- `src/ui/qt_viewport_context_menu.*`, locales, and native app tests

Verification: 15/15 `CopyPivotFromActiveTests.*` passed, including the congruent AABB lower-left,
hierarchy-visible-pivot, mirrored-parent, nonuniform-scale, atomic-failure, and undo/redo cases. The
89 affected public-API/UI guardrails and 14 localization tests passed; all three native test targets
built, full CTest passed 6/6 entries, and the final v0.0.101 `quader` executable built successfully.

## 2026-07-12 - Settings Uses An Explicit Apply Transaction

Task/context: committing a viewport numeric setting with Enter appeared to randomly reset every
viewport preference to its default. The Settings window also needed conventional Apply, OK, and
Cancel actions instead of a destructive Reset button.

Invariant:

- The Settings window has no Reset button. Its stock `QDialogButtonBox` exposes Apply, OK, and
  Cancel; OK is the only default action, while Apply, Cancel, and color swatches can never become
  implicit dialog defaults.
- Controls edit a staged per-field draft. Apply merges only dirty fields into the authoritative
  settings, persists each changed domain once, rebases the draft, and remains open. OK performs the
  same merge and closes. Cancel, Escape, and title-bar close discard changes since the last Apply.
- External settings changes merge into non-dirty draft fields, so an open dialog cannot overwrite
  unrelated menu or runtime changes with a stale whole-object snapshot.

Root cause: Qt propagates the spin box's handled-and-ignored Enter key to its containing `QDialog`.
The first auto-default `QPushButton` was Reset, so Qt clicked it; the controller then reset all
viewport descriptors and `UISystem` correctly saved that new all-default state. With the classic
button row, Enter intentionally activates OK instead.

Affected owners/files:

- `src/ui/qt_settings_dialog.cpp`
- `tests/qt_settings_dialog_tests.cpp`

Verification:

- The widget regression reproduced two Reset clicks before the fix (Return and keypad Enter).
- The transaction regression covers Apply without close, OK, clean OK, Cancel, Escape, title-bar
  close, both Enter keys, reopen/rebase, locale, and per-field external-change merging.

## 2026-07-12 - Grid Width And Tier Composition Preserve Color

Task/context: reducing ground-grid or axis line size made the axis colors turn gray and made color
alpha appear unreliable. Ground-grid and mesh-surface-grid controls were also easy to confuse, and
mesh major lines blended with coincident minor lines, producing a light fringe around dark majors.

Invariant:

- Ground-grid `lineThickness`, `majorLineThickness`, and `axisLineThickness` affect only geometric
  anti-aliased width. They must not multiply line coverage amplitude; the corresponding RGBA color
  alpha is the sole opacity control.
- Ground-grid and mesh-surface-grid colors remain separate settings. Their visible labels and show
  toggles must say which grid they control.
- Mesh-surface major slots exclude minor coverage before RGBA composition. The exclusion reserves
  both current and next LOD footprints, uses the wider configured tier, and is independent of the
  major color alpha; a major line therefore anti-aliases directly against the mesh surface.
- Qt color swatches remain stock Fusion palette-only buttons. Their tooltip and accessibility text
  expose localized numeric opacity without adding button text, icons, QSS, or custom painting.

Affected owners/files:

- `resources/materials/ground_grid.mat`
- `resources/materials/quader_mesh.mat` and `resources/materials/quader_mesh_lit.mat`
- `src/viewport/viewport_settings.cpp`
- `src/ui/qt_settings_dialog.cpp`
- `resources/locales/*.lang`
- `tests/core_tests.cpp`, `tests/qt_settings_controller_tests.cpp`, and UI guardrails

Verification:

- Focused ground-grid, viewport-settings, Qt-settings, mesh-signature, and UI guardrail tests:
  14/14.
- Mesh-material shader contract tests: 4/4; both shaded and rendered mesh materials compiled to
  Filament packages during the test build.
- Full CTest: 6/6 targets passed.
- `cmake --build --preset vs2026-debug --target quader` produced the v0.0.99 executable, and the
  GPU/window boot smoke completed without runtime material or log errors.

## 2026-07-12 - Half-Cut Inserts Or Separates Per-Mesh Center Cuts

Task/context: Half-Cut needed to operate on one or more selected meshes, default to adding only a
centered edge loop, and optionally separate and cap both halves through live tool settings.

Invariant:

- Half-Cut is Object-mode only. Each selected mesh uses its own local AABB center and local X/Y/Z
  axis; multi-mesh execution is atomic.
- The default (`Separate Parts = false`) routes through `plane_surface_cut`, keeps one object, and
  only inserts the centered topology cut. `Fill Parts` is ignored in this mode.
- `Separate Parts = true` keeps both plane-cut outputs as distinct selected mesh objects. `Fill
  Parts = true` caps both halves; false leaves their cut boundaries open while still rejecting
  non-manifold output.
- Every preview update rebuilds from the original base state. Cancel restores the exact state and
  history; Apply creates one undo step. New separated scene entities inherit their corresponding
  source's parent, transform, visibility, lock, mesh metadata, and material.
- The action is metadata-driven as `half_cut`, exposes generic X/Y/Z, Separate Parts, and Fill
  Parts controls, and owns the typed public command `quader_command::kModelingHalfCut`.

Affected owners/files:

- `src/mesh/polygon/operations/plane_cut_operation.cpp` and the mirrored SDK-internal operation
- `src/editor/modeling/operations/half_cut_operation.cpp`
- `src/editor/modeling/runtime/session.*`
- `src/editor/modeling/services/modeling_session_service.cpp`
- modeling metadata, public API tool settings, command IDs, locales, CMake, and focused tests

Verification:

- Half-Cut, Plane Cut, command-schema, and geometry focus: 22/22.
- Half-Cut editor API, multi-source scene identity, and command catalog focus: 4/4.
- Full CTest: 6/6 targets passed.
- `cmake --build --preset vs2026-debug --target quader` produced the v0.0.97 executable.

## 2026-07-12 - Saved-Maximized Qt Startup Reveals Only The Final Shell

Task/context: a saved maximized launch briefly painted the retained normal-size
viewport and dock layout, leaving a white strip and apparent duplicate docks
until Qt completed the maximized layout.

Invariant:

- `UISystem` disables painting for the complete `QMainWindow` before its first
  show. It reveals the shell with one synchronous repaint only after the HWND
  is exposed, Windows reports the expected maximized state, the Qt client
  physical size matches `GetClientRect`, and the central widget, menu/status
  bars, and dock geometry cover the final client for three stable samples.
- A timeout may skip only the extra stability samples after those hard
  invariants pass. It must never reveal an internally consistent saved-normal
  layout inside a larger native client.
- Embedded native viewport surfaces stay hidden through the shell gate. If
  their pre-presentation geometry or exposure changes, they hide again and
  restart stabilization before Filament submission.
- Apply the maximized Qt state while hidden; `UISystem::show()` remains the
  sole owner of mapping the top-level window.

Affected owners/files:

- `src/app/app.cpp`
- `src/platform/window_placement.*`
- `src/ui/ui_system.*`
- `src/ui/qt_startup_presentation.hpp`
- `src/ui/qt_viewport_panel.*`
- `tests/qt_viewport_pane_activation_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/build_guardrail_tests.cpp`

Verification:

- startup presentation and pane activation tests: 3/3.
- affected UI/app tests: 212/212.
- affected build, window, dependency, and UI guardrails: 80/80.
- `cmake --build --preset vs2026-debug --target quader` produced the v0.0.96
  executable.

## 2026-07-12 - Stock Fusion Uses Its Own Neutral Palette And Button Faces

Task/context: selecting the Fusion style alone still allowed the Windows
accent color to replace alternating-row and highlight roles, and a manually
painted color swatch looked like a square block inside a button.

Invariant:

- `main.cpp` selects Fusion and applies exactly
  `fusion_style->standardPalette()`. This is Qt's uncustomized Fusion palette,
  not an app-authored color theme, and prevents OS accent colors from leaking
  into editor tables.
- `QtColorSwatchButton` remains a normal `QPushButton`. It changes only its
  local `QPalette::Button` role and lets Fusion draw the rounded face, border,
  hover, pressed, and focus states; do not custom-paint a rectangular swatch.
- The Outliner Name section stretches to consume remaining header width while
  Visible and Locked size to their labels, so resizing the dock never leaves a
  dead header strip.

Affected owners/files:

- `src/main.cpp`
- `src/ui/qt_color_swatch_button.*`
- `src/ui/outliner_panel.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/build_guardrail_tests.cpp`

Verification:

- focused Fusion/Outliner/swatch guardrails: 5/5.
- `OutlinerPanelTests.*`: 2/2.

## 2026-07-12 - Qt Outliner Collapse Does Not Resynchronize Hidden Selection

Task/context: collapsing an Outliner branch could freeze the Qt shell while the
selected primary entity was inside that branch.

Invariant:

- Collapse/expand signals only update retained `collapsed_entities_`; they do
  not mutate the scene, rebuild the model, or synchronize selection.
- Qt selection synchronization runs only after a model rebuild or a real
  `SceneDocument::selection_revision()` change. It must not call
  `setCurrentIndex()` for a primary row hidden below a collapsed ancestor,
  because Qt can reveal/re-expand that row and re-enter layout/event work.
- The stock-Fusion Outliner is a three-column hierarchy table. Name stretches
  across remaining width; Visible and Locked size to content. Partial check states mean an ancestor determines
  the effective visibility/lock. Rename, visibility, and lock changes route
  only through `QuaderEditorApi::scene()`.

Affected owners/files:

- `src/ui/outliner_panel.*`
- `tests/outliner_panel_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/programmatic_api_guardrail_tests.cpp`

Verification:

- `OutlinerPanelTests.*`: 2/2.
- Outliner/API focused guardrails: 3/3.
- `UiLayoutGuardrailTests.*`: 21/21.
- `cmake --build --preset vs2026-debug --target quader`.

## 2026-07-12 - Qt Fly HUD And Focused Pane Use Retained Fusion Widgets

Task/context: the Qt viewport migration had no fly-navigation HUD and no
visible distinction between the command-targeted pane in multi-viewport
layouts.

Invariant:

- `ViewportInputResult::fly_hud_active` is the fly HUD authority. It shows a
  centered crosshair for the whole fly session; the localized speed/reset
  banner appears only when the pane camera's fly speed differs from default,
  matching the legacy behavior.
- The reset hint names the canonical Middle Mouse binding in every locale; do
  not hardcode an unrelated key in the Qt presentation.
- `ViewportWorkspaceService::focused_pane()` remains the command-target owner.
  A stock `QFocusFrame` identifies it only when multiple panes are visible,
  and surface, viewport-combo, and navigation-gizmo interactions update that
  owner before subsequent commands.
- The HUD and focus cue use stock Fusion widgets and the inherited Qt palette.
  Do not add QSS, app-authored viewport palette overrides, or app-specific theme colors.

Affected owners/files:

- `src/ui/qt_viewport_fly_navigation_hud.*`
- `src/ui/qt_viewport_panel.*`
- `resources/locales/*.lang`
- `tests/qt_viewport_fly_navigation_hud_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Verification:

- `QtViewportFlyNavigationHudTests.*`: 3/3.
- affected UI/localization app tests: 189/189.
- affected Fusion/UI layout guardrails: 20/20.
- `cmake --build --preset vs2026-debug --target quader`.

## 2026-07-11 - Qt Viewport X Retains The Legacy Delete Choice Policy

Task/context: the Qt viewport routed the canonical X/DeletePopup shortcut but consumed it without
presenting the legacy deletion choices.

Invariant:

- X remains owned and gated by `ViewportInputRouter`; Qt must not add a QAction shortcut or raw key
  check. Viewport focus, text input, navigation, operation-preview, and modal-tool gates stay intact.
- Object mode, Group mode, or any selected scene root uses a stock Qt confirmation before the
  scene-aware `edit.delete` command. Component mode uses a stock Qt menu with Delete
  Vertices/Edges/Face and Dissolve Vertices/Edges enabled only for the applicable nonempty mode.
- Commands execute through `QuaderEditorApi`, then synchronize the router's selection-mode
  snapshot. A changed command invalidates hover/overlay caches, and closing either surface restores
  viewport focus. Do not add custom styling or rendering work.

Affected owners/files:

- `src/ui/qt_viewport_delete_popup.*`
- `src/ui/qt_viewport_panel.*`
- `tests/qt_viewport_delete_popup_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `QtViewportDeletePopupTests.*`: 5/5.
- `ViewportInputRouterTests.DeletePopupPreservesViewportFocusTextAndModalToolGates`.
- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests quader`.

## 2026-07-11 - Qt Component Drags Run At Presentation Cadence

Task/context: after the Qt shell migration, moving a face or vertex on an otherwise empty box could
feel dramatically slower even though the raw polygon transform was cheap. Diagnostics showed the
app timer attempting roughly 700-900 callbacks per second for about 165 actual submissions, while
component drags also rebuilt the same preview first in world space and then again in object-local
space.

Invariant:

- The Qt app timer follows the active screen refresh rate, clamped to 30-240 Hz. Do not restore a
  continuously visible viewport to a 1 ms timer; Qt input still wakes through its event loop and
  the next presentation tick consumes the latest state.
- Component transform drags let the runtime update drag math without mutating the session, then the
  host applies the preview once in object-local space. An unchanged computed sample must not rebuild
  the local preview, invalidate overlays, or force scene synchronization.
- A forced modeling mesh sync bypasses the coarse content-revision gate so changing preview geometry
  reaches the scene, but it still honors per-object projected mesh signatures. Unaffected mesh
  objects must not be recompiled merely because another object's component preview changed.
- Preserve frame ordering: viewport input, modeling-to-scene sync, overlay preparation, render.

Affected owners/files:

- `src/app/app.cpp`
- `src/editor/interactions/transform_drag.hpp`
- `src/editor/interactions/move_tool.cpp`
- `src/editor/modeling/services/modeling_transform_drag_service.cpp`
- `src/editor/modeling/services/modeling_mesh_sync_service.cpp`
- `tests/core_tests.cpp`

Verification:

- Empty-box runtime transform preview benchmark: 5,000 updates in 105.883 ms median-like sample,
  about 0.021 ms/update; the observed stall was not polygon transform math.
- `quader_tests`: 698/698.
- `quader_modeling_domain_runtime_loop_tests`: 281/281.
- `cmake --build --preset vs2026-debug --target quader`.

## 2026-07-11 - Object-Mode Mesh Alignment And Distribution

Task/context: Quader needed native world-space arrangement for selected mesh objects without
changing polygon topology or adding host transforms to the portable modeling SDK.

Invariant:

- `MeshArrangementService` owns host arrangement through scene world AABBs and one batched document
  transform command. It operates only in Object mode on topmost selected mesh roots.
- Alignment promotes a nested active mesh to its selected root and keeps that root fixed.
  Distribution sorts initial centers by world axis then entity ID and keeps both endpoints fixed;
  equal gaps remain signed so overlap is valid.
- World deltas are converted through the inverse parent transform by changing local position only;
  local rotation, scale, custom pivot, selection, and topology remain unchanged.
- The 15 typed actions come from `quader_mesh_arrangement_catalog.hpp`. Menu commands read the
  shared persisted Grid Snap preference and current viewport grid preset, while direct API calls
  default to exact unsnapped results.

Affected owners/files:

- `src/editor/transform/mesh_arrangement_service.*`
- `src/api/quader_modeling_api.*`, `src/api/quader_commands_api.*`, and public arrangement types
- `src/ui/main_menu_bar.*`, `src/ui/ui_system.cpp`, and `resources/locales/*.lang`
- `tests/mesh_arrangement_tests.cpp`, localization tests, and UI guardrails

Verification:

- `tools/affected_check.ps1` passed 81 affected guardrails and 333 affected app tests.
- `quader_guardrail_tests` passed all 164 tests.
- `MeshArrangementTests.*` passed all 7 focused behavior tests.
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-10 - Hovered Face Plane Cut Splits And Deselects

Task/context: Alt-hovering a face and Alt+RMB cutting selected meshes originally reused the existing
Plane Cut split operation incorrectly, then briefly routed through an in-place surface cut. The
final viewport gesture is intended to use the hovered face as one infinite plane, keep both sides,
separate the pieces into mesh objects, and leave nothing selected.

Invariant:

- `cut_selected_meshes_by_hovered_face_plane` must route through the object-separating
  `plane_cut` KeepBoth path using per-selected-object local plane requests.
- The shortcut must keep both sides of every cut mesh, create separate mesh objects for split-off
  halves, and finish with no selected objects, vertices, edges, or faces.
- `plane_surface_cut` must directly classify each face against the infinite plane and split by the
  face/plane intersection segment. Do not route it through `knife_stroke`; knife repair can create
  off-plane connector diagonals on subdivided meshes.
- Alt face-plane hover uses dedicated red plane hover overlay roles and must not reuse the blue
  remove-selection styling.
- New mesh objects created by the shortcut must inherit the source mesh scene transform/material
  identity so the separated half appears in-place and can be selected independently.

Affected owners/files:

- `src/mesh/polygon/operations/plane_cut_operation.cpp`
- `src/editor/modeling/runtime/session.cpp`
- `src/editor/modeling/services/modeling_session_service.cpp`
- `src/editor/viewport/viewport_selection_interaction.cpp`
- `tests/modeling_domain/operation/qdr_document_operation_plane_cut_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.*`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=QdrDocumentOperationPlaneCutTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-08 - Bridge Boundary Winding Uses Adjacent Face Votes

Task/context: bridging the preselected open edge loops from `test.qdr` could generate many bridge
faces with reversed normals. Normal Bridge and Interpolated Bridge share the same boundary bridge
surface owner, so both paths needed the same winding fix.

Invariant:

- Boundary Bridge must not rely on shortest-distance loop alignment alone. Before creating bridge
  faces, choose the loop orientation whose candidate quads get the strongest opposite-winding vote
  from adjacent source faces, matching Blender's bridge-loop winding vote behavior.
- `append_bridge_face` must preserve adjacent-edge winding when existing faces can vote; the
  expected-normal fallback is only for isolated faces with no adjacent winding evidence.
- Regression coverage must assert shared edges have opposite winding for both `steps=1` and
  interpolated `steps>1` bridge-edge-boundary operations.

Affected owners/files:

- `src/mesh/polygon/operations/bridge_edge_boundaries_operation.cpp`
- `src/mesh/polygon/quader_poly_document_bridge_surface_helpers.cpp`
- `tests/modeling_domain/operation/qdr_document_operation_bridge_edge_boundaries_tests.cpp`

Verification:

- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_filter=QdrDocumentOperationBridgeEdgeBoundariesTests.*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_filter=QdrModelingRuntimeOperationBridgeTests.*`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_filter=*Bridge*`
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/affected_check.ps1 -ChangedPath src/mesh/polygon/operations/bridge_edge_boundaries_operation.cpp,src/mesh/polygon/quader_poly_document_bridge_surface_helpers.cpp,tests/modeling_domain/operation/qdr_document_operation_bridge_edge_boundaries_tests.cpp`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-07 - Occluded Maximized Windows And Face-Aware Edge Loops

Task/context: a maximized background Quader window could return fully dark, including custom chrome,
until restored/unmaximized; edge double-click on an open boundary/fan shape failed to select the
Blender-like loop. The first boundary-only pass still failed on the screenshot class because the
selected edge can be a shared corner/manifold edge; the loop side must come from the picked surface
face.

Invariant:

- `SDL_WINDOW_OCCLUDED` is not a hard non-drawable state for `PlatformWindow::can_render_frames()`.
  Hidden/minimized and zero-sized windows still suspend GPU submission; stale occlusion on a
  maximized focused window must not block the ImGui/custom-chrome frame.
- Polygon edge-loop selection owns boundary fan traversal. A boundary seed follows adjacent face
  fans across manifold edges until the next boundary edge, while disconnected same-vertex fan faces
  are not selected. Quader has one double-click edge-loop action, so this boundary path is used
  directly instead of Blender's separate loop/boundary cycling UI.
- Runtime edge picks must preserve or re-query the adjacent picked face for double-click edge-loop
  expansion. Shared corner edges use that face to select the connected coplanar surface boundary
  loop; edge-only topology is ambiguous and can stop at the seed.

Affected owners/files:

- `src/platform/platform_window.cpp`
- `src/mesh/polygon/quader_poly_document_selection_api.cpp`
- `src/mesh/polygon/quader_poly_document_picking_overlay.cpp`
- `src/editor/modeling/runtime/session_picking.cpp`
- `tests/core_tests.cpp`
- `tests/build_guardrail_tests.cpp`
- `tests/modeling_domain/operation/*`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=PlatformWindowChromeTests.RenderFrameAvailabilityRejectsHiddenOrMinimizedWindowStates:AppLoopTests.SuspendsGpuSubmissionForNonDrawableWindowsAndIdlesBetweenFrames`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=QdrDocumentOperationInsertEdgeLoopTests.*:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadePickSelectDoubleClickDelegatesBySelectionMode:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeShiftAndCtrlDoubleClickEditWholeEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickSelectsBoundaryFanEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickUsesPickedFaceForSharedCornerEdgeLoop`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_filter=*Chrome*:*Window*:*App*`

## 2026-07-07 - Bridge, Viewport Stats, Gizmo Selection Regressions

Task/context: bug-fix pass for normal Bridge inheriting Interpolated Bridge steps, unreliable FPS
and draw-call overlay values, transform gizmos disappearing during navigation or clearing edge
selection on short clicks, Shift/Ctrl edge-loop double-click selection, Quick Pivot leaking through
RMB fly navigation, settings modal close affordance, and same-mesh grouped edge Bridge rejection.

Invariant:

- Normal/instant Bridge always uses one segment. Interpolated Bridge remains the operation that owns
  user-configurable `BridgeOperationSettings::steps`.
- Runtime same-object edge Bridge accepts exactly two connected selected edge groups on one mesh and
  routes through the same boundary-alignment bridge owner as cross-object edge Bridge.
- Viewport info FPS is sampled from completed render frames over the render system's sample window;
  CPU/GPU frame timings stay render-owned. Viewport draw-call/object/primitive counts are scene
  info estimates owned by `src/render/viewport_info_stats.*` and are camera-visible, not raw
  Filament submission counters.
- Transform gizmo frames survive orbit/fly camera movement, but hover is suppressed while
  navigation/camera changes are active. Deferred gizmo clicks preserve existing component
  selection on selected hits and misses.
- Quick Pivot must clear its temporary override if RMB navigation/fly navigation starts before the
  `D` key is released.
- Edge double-click Shift/Ctrl selection must apply Add/Remove to every edge returned by the
  topology edge-loop helper, not just the picked edge.
- `ViewportSettingsPanel` is intentionally a floating settings surface without a close button.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_runtime_loop_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInfoStatsTests.*:RenderSystemTests.ExposesCompletedFramePerformanceStatsFromRendererHistory:ViewportRendererTests.ViewportInfoOverlayUsesRendererTimingAndVisibleMeshSubmissionStats:ModelingSessionServiceSplitTests.*:ModelingSessionServiceTests.NormalBridgeFacesIgnoresInterpolatedBridgeSteps:ViewportInteractionControllerTests.ShortMoveGizmoClickKeepsEdgeSelection:ViewportInteractionControllerTests.TransformGizmoFrameSurvivesNavigationFrames:ViewportToolGuardrailTests.*:UiLayoutGuardrailTests.ViewportSettingsFloatingPanelHasNoCloseButton`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_filter=QdrModelingRuntimeOperationBridgeTests.BridgeSelectedEdgesBridgesTwoEdgeGroupsOnOneCombinedMesh:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeShiftAndCtrlDoubleClickEditWholeEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadePickSelectDoubleClickDelegatesBySelectionMode`

## 2026-07-07 - Shift Rotate Component Drag Extrudes

Task/context: Shift+dragging a selected face with the Rotate transform gizmo regressed to a plain
component rotation instead of creating extruded topology first.

Invariant:

- In face/edge component modes, transform gizmo Shift+drag maps Move and Rotate to `Extrude`, and
  Scale to `Inset`; object mode Shift remains duplicate-selection behavior.
- `TransformDragOperation::Extrude` is valid for Move and Rotate. Move extrudes with an offset;
  Rotate extrudes selected elements through the rotate `Transform3` so the selected cap is copied
  and rotated before commit/repeat-last.
- Component transform previews must convert world-space gizmo deltas through each selected object's
  scene transform before mutating polygon documents. The runtime documents are local to their scene
  object; applying a world pivot or world affine directly moves transformed, mirrored, or pivot-edited
  component selections.
- Cross-object Bridge executes in the survivor object's local space. Before appending another
  selected object document, convert that source document by `inverse(survivorWorld) * sourceWorld`;
  record undo from the original session state, not from the temporary normalized bridge input.
- Cross-object Bridge preview updates cannot re-query live scene transforms after begin-preview,
  because the preview may have temporarily removed the merged source entity. Capture participant
  world transforms when the scene-workspace preview begins and reuse them for every Bridge preview
  settings update and commit.
- Runtime operations that combine selected object documents, currently Bridge and Combine Meshes,
  must route through `ModelingSessionService::execute_runtime_operation_command()` so the editor
  scene-space adapter supplies survivor-local/common-local inputs and the history command records
  the original scene-authored state.
- When the scene-space adapter maps a participant by `inverse(survivorWorld) * sourceWorld`, it
  must reverse that participant's face loops and loop UVs when the relative transform has negative
  determinant. Otherwise mirrored sources keep transformed positions but enter Bridge,
  Interpolated Bridge, or Combine Meshes with inverted winding.
- Repeat-last for component transforms must also convert the repeated world affine or offset into
  object-local transform payloads before calling runtime transform previews; repeat-last for
  runtime operations should re-enter `execute_runtime_operation_command()` instead of bypassing the
  scene-space adapter.
- `src/mesh/polygon` owns copied-vertex extrusion topology. Viewport and runtime drag layers only
  choose and route the semantic operation.

Verification:

- `cmake --build --preset vs2026-debug --target quader_guardrail_tests quader_modeling_domain_metadata_loop_tests quader_modeling_domain_polygon_loop_tests quader_modeling_domain_runtime_loop_tests quader_tests`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_filter=QdrDocumentOperationExtrudeElementsTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ProgrammaticBehaviorScenarioTests.ShiftFaceMoveAndRotateExtrudeAndShiftFaceScaleInsetsThroughGizmoDrag:ViewportInteractionControllerTests.ShiftRotateGizmoDragExtrudesSelectedFace`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=ProgrammaticApiGuardrailTests.*Viewport*`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_filter=QdrDocument*:QdrDocumentOperation*:QdrDocumentOperationExtrudeElementsTests.*:QdrPoly*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntime*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.*BoxSelect*:EditorApiTests.*Input*:EditorApiTests.*Keybinding*:EditorApiTests.*RightClickPick*:EditorApiTests.*Viewport*:GroupingPublicApiTests.*:ProgrammaticBehaviorScenarioTests.*:SelectionModeAdapterTests.*:Viewport*:ViewportBoxOverlayBuilderTests.*:ViewportInteractionControllerTests.*:ViewportSelectionMarqueeOverlayBuilderTests.*:ViewportTool*:ViewportTransformGizmoOverlayBuilderTests.*`
- `cmake --build --preset vs2026-debug --target quader_tests quader_modeling_domain_runtime_loop_tests quader_modeling_domain_polygon_loop_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ProgrammaticBehaviorScenarioTests.ShiftFaceMoveAndRotateExtrudeAndShiftFaceScaleInsetsThroughGizmoDrag:ProgrammaticBehaviorScenarioTests.ShiftRotateExtrudeUsesWorldPivotForTransformedComponentSelection:ProgrammaticBehaviorScenarioTests.BridgeAfterShiftRotateExtrudeKeepsTransformedSourceInWorldPlace:ViewportInteractionControllerTests.ShiftRotateGizmoDragExtrudesSelectedFace`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ProgrammaticBehaviorScenarioTests.BridgeFacesPreservesWindingForMirroredSceneSource:ProgrammaticBehaviorScenarioTests.InterpolatedBridgePreviewPreservesWindingForMirroredSceneSource:ProgrammaticBehaviorScenarioTests.CombineMeshesPreservesWindingForMirroredSceneSource`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_filter=QdrDocumentOperationExtrudeElementsTests.*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntimeOperationBridgeTests.*:QdrModelingRuntimeOperationTransformPreviewTests.*:QdrModelingRuntimeRepeatLastActionTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=*Bridge*:*ShiftRotate*:*CombineMeshes*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntimeOperationBridgeTests.*:QdrModelingRuntimeOperationCombineMeshTests.*:QdrModelingRuntimeOperationTransformPreviewTests.*`

Note: `QdrEditorMetadataToolCatalogTests.HostContractToolCatalogMirrorsEditorCoreRegistryIdentity`
was already red in the affected metadata loop because the viewport descriptor catalog includes
Pivot while the legacy editor-core tool registry has 12 tools.

## 2026-07-07 - Rotated Mesh Selection Uses World AABB Cage

Task/context: Source/Hammer-style object selection for non-world-aligned meshes should keep the mesh
geometry rotated while the editor outline, grid snap reference, and transform drag bounds use a
world-aligned AABB cage.

Invariant:

- `src/render/overlay/render_overlay_snapshot.*` records direct selected mesh world bounds through
  `entity_world_bounds`; group/empty hierarchy bounds stay separate.
- `src/render/overlay/viewport_scene_overlay_builder.*` draws selected mesh AABB cages with the
  existing `GroupBounds` style only in object/group selection contexts. Component modes do not add
  the selected mesh AABB over edit topology.
- `src/editor/modeling/services/modeling_session_service.cpp` makes object-mode transform drag
  bounds prefer `selected_transform_world_bounds` before runtime selection bounds, keeping scale and
  snap math aligned with the visible world-space cage.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportSceneOverlayBuilderTests.AddsSelectedMeshWorldAabbBoundsInObjectMode:ViewportSceneOverlayBuilderTests.AddsHierarchySelectionWiresAndGroupBounds:RenderOverlaySnapshotTests.CapturesSelectionGroupLightHierarchyAndToolOverlayRecords:ViewportOverlayPolicyTests.ObjectTransformDragBoundsPreferSceneWorldAabb`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-07 - Outliner Inspection Selection Can Target Locked Entities

Task/context: locked meshes could no longer be selected from the outliner, making the inspector
lock checkbox unreachable and preventing normal unlock from the UI.

Invariant:

- Normal user/editor selection (`select_object`, viewport picking, box select, select-all, invert)
  still filters missing, effectively locked, and non-selectable scene entities.
- Outliner row clicks use `QuaderSelectionApi::select_objects_for_inspection`, which records an
  exact existing-entity scene selection through `EditorSelectionService::set_inspection_scene_selection`
  so locked/non-pickable rows can be inspected and unlocked.
- Selection-driven hierarchy mutations (`DeleteSelection`, duplicate, group, ungroup) use
  `document_command_selected_root_entities`, which ignores effectively locked roots. Selecting a
  locked row for inspection must not become a delete/duplicate/group back door.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.LockedEntitiesCannotBeUserSelectedButCanBeInspectedAndUnlocked:EditorApiTests.LockingParentClearsSelectedModelingDescendantAndUndoRestoresIt:EditorApiTests.ObjectSelectAllAndInvertUseSceneEntitiesAndExcludeLockedAndRoot`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=ProgrammaticApiGuardrailTests.SceneSelectionApplicationOwnerCoversCommandServiceAndProjection:ProgrammaticApiGuardrailTests.OutlinerUsesDocumentCommandRootSelectionOwner`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=GroupingPublicApiTests.*:EditorApiTests.DeleteCommandRouteRemovesSameObjectAndExposesUndoLabel:EditorApiTests.DeleteSelectionDeletesSelectedLightsThroughSceneAwareRoute:EditorApiTests.PublicDuplicateSelectionDuplicatesLightEntityThroughSceneHierarchy:EditorApiTests.SceneRootSelectionCanEditSettingsButCannotBeDeletedOrDuplicated:EditorApiTests.DuplicateMeshesHistoryRestoresSceneOnlyDuplicateState:EditorApiTests.DuplicateMeshesActionCommandPreservesSceneOnlyDuplicateState`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-07 - Object-Mode Mesh Selection OBJ Export

Task/context: File > Export OBJ needed to export only the direct object-mode selected meshes,
without adding Assimp or another mesh export dependency for v1.

Invariant:

- `src/io/mesh/obj_writer.*` owns geometry-only Wavefront OBJ serialization and validation.
- `QuaderProjectApi::export_selected_meshes_as_obj` owns export intent: require object mode, collect
  direct selected mesh entities, prefer live modeling mesh compilation, then fall back to
  `AssetRegistry` mesh data for plain mesh entities.
- OBJ export writes local mesh coordinates only; scene transforms, materials, MTL files, textures,
  hierarchy, history, dirty state, and project path are not mutated.
- Single selection writes the chosen path exactly; multi-selection writes sibling
  `Base_<EntityName>.obj` files with sanitized names and numeric collision suffixes.

Verification:

- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.AggregatesDocumentApisAndCommandRouting:EditorApiTests.CommandRegistryCoversEveryAppOperationAndViewportTool:ObjExportTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=LocalizationResourceTests.*:LocalizationServiceTests.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-07 - Edge Bevel Corners Fill Local Open Profile Loops

Task/context: beveling adjacent L-shaped opening edges could leave a black terminal hole at the
opposite unshared endpoint; beveling opposite side plus bottom opening edges could leave two bottom
holes at two-selected-edge corners. Both matched missing pieces of Blender's bevel boundary rebuild
around selected endpoints. The opposite side plus bottom opening case also needed straight wall
support edges: the source wall face must not connect old support vertices directly into bevel
profile vertices when a selected edge leaves that wall plane.

Invariant:

- `src/mesh/polygon/operations/bevel_edges_operation.cpp` owns edge bevel topology. Runtime/UI
  bevel paths should not patch terminal holes outside the polygon operation.
- After source-face rebuilds, strip faces, and normal corner/terminal support faces are emitted,
  selected endpoints must close any remaining local open-edge loop connected to that endpoint's
  bevel profile, offset vertices, and unselected support/boundary vertices.
- The local close pass must be constrained to the affected bevel profile component so it does not fill
  unrelated document holes.
- For non-terminal selected corners on an unselected source face, insert the same boundary support
  vertices only when a selected edge leaves that face plane. Flat in-plane L selections should keep
  their existing miter path; mixed wall/extrusion selections need boundary vertices to prevent
  converging or diverging wall support lines.

Verification:

- `cmake --build --preset vs2026-debug --target quader_modeling_domain_polygon_loop_tests`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_filter=QdrDocumentOperationBevelEdgesTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Component Source-Wire Stickiness Is Mode-Specific

Task/context: after source-wire overlay changes, component mode either stuck the cyan wire to the
previous object after any click or hid the wire when switching from selected object mode into
component mode.

Invariant:

- Entering component mode from a selected mesh shows that mesh's fallback `SourceWire` immediately,
  even with no selected components.
- A component hover candidate replaces that fallback wire when the active object has no selected
  component for the current mode.
- Sticky source-wire ownership requires selected components for the exact mode: faces for face mode,
  edges for edge mode, vertices for vertex mode. Object selection, `active_object_id`, and component
  selections from another mode are not sticky.
- Removing the last selected component leaves the active mesh fallback wire visible until another
  hover candidate replaces it.

Affected owners/files:

- `src/editor/modeling/runtime/session.cpp`
- `src/editor/modeling/runtime/session_selection_overlay.cpp`
- `src/editor/modeling/services/modeling_session_services.cpp`
- `tests/modeling_domain/qdr_modeling_runtime_overlay_tests.cpp`
- `docs/overlay-handling.md`

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_runtime_loop_tests`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntimeOverlayTests.QdrEditorCoreOverlayTests`

## 2026-07-04 - Component Handles Depth-Test Without Source-Wire Depth Testing

Task/context: component selected/hover handles needed to stop x-raying through opaque geometry, but
a broader attempt to depth-test `SourceWire` revived old inverted-normal visibility failures.

Invariant:

- `SourceWire` remains draw-on-top editable topology and keeps `SourceWireDepthStamp` as
  non-rendered visibility metadata.
- Component-mode base-frame `SourceWire` and `SourceWireDepthStamp` emission is active-edit-mesh
  only. Other selected meshes must not emit full-mesh source wire just because they are selected.
- Component selected/hover edge and vertex handles are projected with component-specific
  `ViewportOverlaySource` values.
- `ViewportOverlayPolicy` depth-tests only those marked component selected/hover primitives.
  Object-mode selected wires and unmarked helper overlays keep their layer-level draw-on-top
  behavior.
- `OverlayRenderableBuilder` must split line and point batches by primitive depth mode so a single
  native layer can contain both component depth-tested handles and object/helper draw-on-top
  handles.

Affected owners/files:

- `src/editor/projection/selection_overlay_projection.*`
- `src/editor/modeling/runtime/session_selection_overlay.cpp`
- `src/editor/modeling/services/modeling_session_services.cpp`
- `src/render/viewport_overlay_frame.hpp`
- `src/render/viewport_overlay_policy.*`
- `src/render/overlay/overlay_renderable_builder.*`
- `tests/core_tests.cpp`
- `tests/modeling_domain/qdr_modeling_runtime_overlay_tests.cpp`
- `tests/render_extraction_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ModelingSessionServiceTests.ComponentModeSourceWireUsesActiveEditMeshOnly:ModelingSessionServiceTests.SwitchingSelectedObjectToEdgeModeShowsHandlesImmediately:ModelingSessionServiceTests.SwitchingSelectedObjectToFaceModeShowsHandlesImmediately:ViewportOverlayPolicyTests.*:SelectionOverlayProjectionTests.*:OverlayLineMeshBuilderTests.*:OverlayPointMeshBuilderTests.*:SceneRenderBridgeTests.MeshRenderablesReceiveShadowsAndViewportEnablesShadowing`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_runtime_loop_tests`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntimeOverlayTests.*:QdrModelingRuntimePickingTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Point Lights Use Selectable Billboard Gizmos

Task/context: point lights needed a larger upright PNG viewport gizmo, visible in shaded and
rendered viewports, selectable from the viewport, and governed by the existing Show Gizmos toggle.

Invariant:

- Visible point lights are represented by `ViewportOverlayIconKind::PointLight` billboard icons,
  not the old selected-light cross/line marker.
- Point-light billboard icons are shown only when Show Gizmos is enabled and the pane shading is
  `Shaded` or `Rendered`; wireframe panes do not show them.
- Point-light icons are scene gizmos, so they are emitted for all effectively visible point lights,
  selected or not, and use screen-facing icon quads with `OverlayDepthFunc::Always`.
- Viewport selection must include matching screen-space billboard pick candidates so clicking or
  context-clicking the icon selects the light through the public selection API.

Affected owners/files:

- `src/ui/viewport_panel.cpp`
- `src/render/overlay/render_overlay_snapshot.*`
- `src/editor/viewport/viewport_interaction_frame.hpp`
- `src/editor/viewport/viewport_selection_interaction.cpp`
- `tests/render_extraction_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportRendererTests.MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame:RenderOverlaySnapshotTests.CapturesSelectionGroupLightHierarchyAndToolOverlayRecords:ViewportSceneOverlayBuilderTests.AddsSelectedDirectionalLightMarkerAndRetiresPointLightCross:ViewportSceneOverlayBuilderTests.AddsPointLightBillboardGizmosWhenRequested:ViewportInteractionControllerTests.ClickFrameSelectsPointLightBillboardThroughViewportFallback:ViewportInteractionControllerTests.ContextMenuPointLightBillboardPreservesSelectedHitSelection`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Post-Processed Viewport Textures Are Display Encoded

Task/context: rendered viewport dithering looked like heavy noise in black areas, while disabling
dithering made textured surfaces look posterized compared with Filament's material sandbox.

Invariant:

- `ViewportRenderer` owns the scene viewport texture presentation color-space. When Filament
  post-processing is enabled, the scene target has already gone through color grading/gamma output
  and must be registered as `TextureSampleColorSpace::DisplaySrgb` for the ImGui composite pass.
- When post-processing is disabled, the scene target remains linear and must be registered as
  `TextureSampleColorSpace::LinearSrgb` so the ImGui material performs the one required
  `linearToSrgb` encode.
- Overlay viewport targets remain separate premultiplied display-sRGB textures with post-processing
  and dithering disabled.
- The default albedo PNG is 16-bit, but preserving 16-bit PNG precision is a separate texture-loader
  follow-up because sRGB 16-bit content needs a deliberate high-precision linear upload path.

Affected owners/files:

- `src/render/viewport_renderer.*`
- `src/render/viewport_render_target.*`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_guardrail_tests quader_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportRendererTests.PresentsSceneAndOverlayViewportTexturesThroughUiEncoder:OverlayMaterialTests.ImGuiMaterialCanCompositePremultipliedDisplayTextures:TextureRegistryTests.*`

## 2026-07-04 - Fill Hole Filters Selected Boundary Loops

Task/context: Fill Hole failed on a valid hole when the selection also contained an unrelated
non-boundary edge, matching Blender's more tolerant Fill Holes behavior.

Invariant:

- `src/mesh/polygon/operations/fill_hole_from_edges_operation.cpp` owns polygon Fill Hole topology.
- The operation should fill closed components made from selected open-boundary edges and ignore
  extra selected non-boundary or wire edges when at least one valid boundary loop exists.
- If selected open-boundary edges do not themselves form a closed loop, the operation should expand
  to the containing open-boundary ring when at least three selected edges belong to that ring.
- Fully non-boundary edge loops remain rejected; valid fills switch selection to generated face IDs.

Verification:

- `cmake --build --preset vs2026-debug --target quader_modeling_domain_polygon_loop_tests`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_filter=QdrDocumentOperationFillHoleTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - App Loop Consumes Typed Platform Events

Task/context: `App::run()` classified SDL window event enums directly for quit, resize, native
chrome refresh, idle wait, and live-resize redraw handling.

Invariant:

- Backend event polling, waiting, event-watch installation, and SDL event classification live in
  `src/platform/platform_event.*`.
- `App::run()` consumes `PlatformEvent` flags (`quit_requested`, `mouse_motion`,
  `window_pixel_size_changed`, `window_chrome_state_changed`) and passes only the opaque
  `native_event` payload to UI/chrome backend adapters.
- App headers must not expose `SDL_Event`; live-resize event watches use `PlatformEventWatch` and
  `PlatformEvent` callbacks.

Affected owners/files:

- `src/platform/platform_event.*`
- `src/platform/platform_window.*`
- `src/app/app.*`
- `tests/build_guardrail_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.UiSystemKeepsSdlBehindAppPlatformAdapter:BuildGuardrailTests.AppLoopConsumesTypedPlatformEvents`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=AppLoopTests.*:SdlEventUtilsTests.*`
- `ctest --preset vs2026-debug --output-on-failure`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Component Transforms Keep Generated UVs Procedural

Task/context: an attempted UV regression fix materialized generated face-loop UVs before component
transforms, which made point/vertex manipulation stretch the checker texture from the base shape.

Invariant:

- Component-mode transforms (`Vertex`, `Edge`, `Face`) must preserve the pre-refactor runtime
  behavior: after geometry changes they clear stored face-loop UVs and leave checker coordinates
  procedural/generated at mesh compile time.
- Only object-mode texture-lock paths materialize and preserve loop UVs during transforms. Do not
  extend those texture-lock semantics to component transforms unless an explicit UV-editing feature
  changes the owner invariant.

Affected owners/files:

- `src/editor/modeling/runtime/session_transform_bounds.cpp`
- `tests/modeling_domain/operation/qdr_modeling_runtime_operation_translate_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntimeOperationTranslateTests.*`
- `python tools\check_cpp_style.py --paths-only`
- `git diff --check`
- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests quader_modeling_domain_tests`
- `ctest --preset vs2026-debug --output-on-failure`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - QDR Project Writes Consume Export Snapshots

Task/context: Phase 7 architecture work prepared project export for future background workers.

Invariant:

- QDR project serialization and atomic write internals consume `QdrProjectWriteSnapshot`, not live
  `ProjectDocument`, `SceneDocument`, or `AssetRegistry` state. Compatibility overloads may still
  accept `const ProjectDocument&`, but they must immediately build a snapshot and delegate.
- Snapshot mesh records own authored polygon documents by value; copying `MeshAsset` directly would
  preserve a shared pointer to mutable live polygon data.

Affected owners/files:

- `src/io/qdr/qdr_project_write_snapshot.*`
- `src/io/qdr/qdr_document_writer.*`
- `tests/io_qdr_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QdrDocumentIoTests.ProjectWriteSnapshotOwnsCopiesForBackgroundExport:QdrDocumentIoTests.ProjectWriterConsumesImmutableExportSnapshot:QdrDocumentIoTests.AtomicProjectWriteUsesCompactJsonWhileDebugSerializerStaysPretty:QdrDocumentIoTests.ProjectAtomicWritePolicySkipsHotSaveSelfParseAndKeepsRoundTripCoverage`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QdrDocumentIoTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Render Snapshot Boundary Owns Scene/Asset Extraction

Task/context: Phase 5 architecture migration moved render scene, overlay, preview, and UI icon
inputs behind render-owned snapshots and services.

Invariants:

- `SceneRenderBridge`, `SceneRenderSyncService`, mesh caches, light caches, overlay caches, and tool
  preview cache consume render snapshot records, not `SceneDocument` / `AssetRegistry` pointers.
- `ViewportPanel` may submit viewport presentation requests and overlay frames, but must not call
  `build_render_overlay_*` or `ViewportSceneOverlayBuilder` directly. Runtime render access builds
  scene/asset overlay snapshots from the project session and hands them to the render bridge.
- UI icon textures are allocated and destroyed by `UiIconTextureService` under `src/render`; UI
  receives only ImGui texture IDs through `RenderSystem::load_ui_icon_textures()` and clears them
  through `RenderSystem::clear_ui_icon_textures()`.

Affected owners/files:

- `src/render/render_scene_snapshot.hpp`
- `src/render/scene_render_extraction.*`
- `src/render/scene_render_bridge.*`
- `src/render/scene_render_sync_service.*`
- `src/render/overlay/render_overlay_snapshot.*`
- `src/render/ui_icon_texture_service.*`
- `src/editor/render/quader_render_api_runtime_access.hpp`
- `src/api/quader_render_api.cpp`
- `src/ui/viewport_panel.cpp`
- `src/ui/ui_system.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=RenderSceneSnapshotTests.*:RenderOverlaySnapshotTests.*:SceneRenderBridgeTests.UsesRenderSceneSnapshotsForIncrementalMeshSync:SceneRenderBridgeTests.UsesDepthStampedFaceFillForOverlayTriangles:RenderProxyLifecycleTests.*:TextureRegistryTests.*:ViewportRendererTests.PresentsSceneAndOverlayViewportTexturesThroughUiEncoder`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=OopArchitectureGuardrailTests.UiShellLeavesSceneOverlayExtractionToRenderBuilder:OopArchitectureGuardrailTests.RenderSnapshotBoundaryOwnsSceneAndOverlayExtraction:OopArchitectureGuardrailTests.UiDoesNotOwnFilamentResourceAllocation:OopArchitectureGuardrailTests.RenderCodeDoesNotCallDocumentOrAssetMutators:BuildGuardrailTests.UiIconTexturesStayBehindRenderSystemAdapters:BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:ProgrammaticApiGuardrailTests.ReadOnlyApiPathsUseConstProjectSessionAccess`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Viewport Selection Overlay Cache Follows Scene Preview Transforms

Task/context: scene-transform drags moved the shaded mesh immediately, but the semantic wire cage
could stay at the pre-drag position until release.

Invariant:

- `ViewportPanel`'s `ViewportPaneOverlayCacheKey` must include `SceneDocument::revision()` and
  `AssetRegistry::revision()`. Modeling overlay extraction already keys on scene revision, but the
  viewport pane cache can otherwise skip asking the modeling owner for a fresh frame during live
  scene-transform previews.
- Persistent selected-object render overlays still live behind render snapshot/cache ownership; UI
  should only decide whether its pane-level semantic overlay frame is stale.

Affected owners/files:

- `src/ui/viewport_panel.*`
- `src/ui/viewport_pane_overlay_cache.*`
- `src/editor/modeling/services/modeling_session_services.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportPaneOverlayCacheTests.*:ViewportOverlayPolicyTests.NativeViewportSkipsTransientObjectSelectionOverlayBeforeProjection:ModelingSessionServiceTests.TransformDragObjectSelectionUsesSceneTransformPreview:ModelingSessionServiceTests.ObjectTransformDragRefreshesViewportOverlayCacheKeyDuringScenePreview`
- `ctest --preset vs2026-debug --output-on-failure`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Scene Selection Application Has One Owner

Task/context: Phase 3 architecture work consolidated scene entity selection writes that had drifted
between `SelectEntitiesCommand`, `EditorSelectionService`, and `SelectionProjectionService`.

Invariants:

- Scene entity selection application lives in `src/editor/selection/scene_selection_application.*`.
- `SceneSelectionApplicationMode::UndoableCommand` preserves command undo/redo clear-then-set
  behavior; `SceneSelectionApplicationMode::RuntimeSync` uses diff-style sync for modeling/runtime
  projection so unchanged projection does not churn scene selection revisions.
- `EditorSelectionService` still owns user selection intent, filtering, and modeling projection
  orchestration, but uses the shared owner for selectability, edit resolution, and command payloads.
- Component selection remains modeling-runtime owned; this owner only applies `SceneDocument`
  entity selection.

Affected owners/files:

- `src/editor/selection/scene_selection_application.*`
- `src/editor/selection/editor_selection_service.*`
- `src/editor/selection/selection_projection_service.cpp`
- `src/editor/commands/document/select_entities_command.hpp`
- `src/editor/commands/document/document_command_scene_helpers.hpp`
- `src/editor/commands/document_command_service.*`
- `tests/programmatic_api_guardrail_tests.cpp`
- `tests/modeling_session_service_split_tests.cpp`

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=ProgrammaticApiGuardrailTests.PublicSelectionApiDelegatesEntityEditPolicyToEditorSelectionService:ProgrammaticApiGuardrailTests.ObjectAndGroupSelectionCommandsUseSceneDocumentSelectionOwner:ProgrammaticApiGuardrailTests.SceneSelectionApplicationOwnerCoversCommandServiceAndProjection`
- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests` was attempted but
  `quader_tests` was blocked by unrelated dirty `tests/core_tests.cpp` compile errors around
  `ProjectSessionTests.PublicHistoryIgnoresUntrackedModelingSessionHistory`.

## 2026-07-04 - ProjectObjectRecord Scene Components Stay Canonical

Task/context: Phase 3 migration revisited duplicated live fields on `ProjectObjectRecord`.

Invariant:

- For records bound to live scene entities, mesh, material, modeling object, visibility, and material
  slot fields are projections of `SceneDocument` components. Stale object-record copies must not
  become a second source of truth during indexing, synchronization, validation, or QDR load/save.
- Detached object records without a live scene entity may still carry their own stored fields.

Affected owners/files:

- `src/scene/document/project_document.*`
- `src/io/qdr/qdr_document_reader.cpp`
- `src/io/qdr/qdr_document_writer.cpp`
- `tests/io_qdr_tests.cpp`

Resolved approach:

- `ProjectDocument` rehydrates scene-linked component fields before indexing and per-record asset
  validation, so stale QDR object-record component copies are ignored when valid scene components
  exist.
- Added document and QDR tests proving stale record fields do not dirty a second sync pass and are
  rewritten from scene components.

Verification:

- `cmake --build --preset vs2026-debug --target quader_document quader_project_qdr_io`
- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_tests` currently blocked by unrelated dirty
  `tests/core_tests.cpp` compile errors in another worker's ProjectSession read-view assertions.

## 2026-07-03 - Property Table Numeric Sliders And Grid Defaults

Task/context: inspector/property settings were using mixed ImGui numeric semantics: bounded
properties used `DragFloat` while settings used `SliderFloat`/`SliderInt`, and ImGui sliders did not
open exact text input on double-click in the vendored version. The property table and outliner
hierarchy guide lines also drew full-height vertical segments for last children, producing broken
T branches.

Invariants:

- Bounded non-transform numeric UI controls should route through `quader_slider_float` /
  `quader_slider_int`; those wrappers preserve slider visuals and add double-click exact input via
  ImGui's temp-input activation path. Transform axis rows stay as raw drag inputs, including bounded
  scale, because transform editing needs drag semantics.
- Do not duplicate viewport grid default numbers between runtime settings and host metadata. Shared
  grid defaults live in `src/viewport/viewport_settings_defaults.hpp`; host metadata may project
  those constants but must not depend on runtime `ViewportSettings` objects.
- Property-table child guide lines draw the current row's L connector immediately, then the contained
  widget state flushes the previous child row's extra vertical segment only when another child
  follows. Outliner guide drawing must carry ancestor/last-sibling state through tree recursion.

Affected owners/files:

- `src/ui/imgui_quader_widgets.*`
- `src/ui/inspector_panel.cpp`
- `src/ui/tool_settings_panel.cpp`
- `src/ui/viewport_settings_panel.cpp`
- `src/ui/outliner_panel.cpp`
- `src/viewport/viewport_settings_defaults.hpp`
- `src/viewport/viewport_settings.cpp`
- `src/editor/metadata/host_contract.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/core_tests.cpp`
- `tests/modeling_domain/editor_metadata/qdr_editor_metadata_preference_catalog_tests.cpp`

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_tests quader_modeling_domain_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportSettingsTests.DefaultsMatchNativeViewportSettingsState:ViewportSettingsTests.SavesLoadsAndClampsViewportPreferenceKeys:GroundGridMaterialTests.UsesUserWorldAnchoring:DocumentCommandTests.SceneTransformDragServicePreviewsInspectorPrimaryTransformsBeforeCommit`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=QdrEditorMetadataPreferenceCatalogTests.HostContractPreferenceCatalogExposesViewportSettingDefaultsAsEngineNeutralDescriptors`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_filter=UiLayoutGuardrailTests.PropertyTableWidgetIsResizableStripedTwoColumnTable:UiLayoutGuardrailTests.NumericSlidersUseSharedDoubleClickInputWrapper:UiLayoutGuardrailTests.HierarchyGuideBranchesTerminateAtLastChild:BuildGuardrailTests.AppVersionMetadataUsesRootVersionSource`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-01 - Selected Overlay Navigation Must Stay Visible And Cheap

Task/context: `14.tracy` showed selected-all navigation dropping to roughly 10 FPS with 395 objects.
Navigation input was cheap; the cost came from selected overlays, outliner drawing, and transform
preview invalidation. A temporary UI suppression experiment was rejected because it made overlays
disappear during interaction.

Invariants:

- Do not hide selected overlays during navigation or transform drags to paper over CPU cost.
- Object-mode selected overlays can be dense, so render-side cache invalidation must avoid repeated
  whole-frame/per-layer signature scans and must reuse compatible line/point proxy resources on
  camera-only rebuilds.
- `SourceWireDepthStamp` visibility filtering is only meaningful when the frame actually contains
  source-wire depth-stamp triangles; object-mode selected edges must not scan selected face-fill
  triangles to build an empty visibility filter.
- Scene transform previews for large object selections must batch scene transform writes and avoid
  invalidating semantic/base selection overlay caches when only object transforms change.
- The outliner must cache scene tree records by scene revision and skip custom drawing for rows
  outside the visible clip rect.

Affected owners/files:

- `src/render/render_signature_builder.*`
- `src/render/overlay/overlay_renderable_builder.*`
- `src/render/overlay/overlay_renderable_cache.*`
- `src/render/overlay/overlay_proxy_factory.*`
- `src/editor/history/scene_transform_preview_transaction.hpp`
- `src/editor/transform/scene_transform_drag_service.cpp`
- `src/editor/modeling/services/modeling_session_service.cpp`
- `src/scene/scene_document.*`
- `src/ui/outliner_panel.*`
- `src/ui/viewport_panel.*`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=RenderSignatureBuilderTests.*:OverlayLineMeshBuilderTests.*:OverlayRenderableBuilderTests.*:OverlayRenderableCacheTests.*:OverlayProxyFactoryTests.*:SceneRenderBridgeTests.SkipsOverlayProxyRebuildWhenOverlayPayloadIsUnchanged:SceneDocumentTests.BatchedTransformAssignmentsDirtyOnceAndSkipNoOps:DocumentCommandTests.SceneTransformPreviewTransaction*:DocumentCommandTests.SceneTransformDragServiceFiltersTargetsAndRecordsScenePreviewThroughApi:ModelingSessionServiceTests.TransformDragObjectSelectionUsesSceneTransformPreview:ViewportToolGuardrailTests.TransformGizmoAndSelectionUseSharedViewportRuntime`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.Outliner*:ProgrammaticApiGuardrailTests.OutlinerUsesDocumentCommandRootSelectionOwner`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=LocalStressTests.MyFirstProjectLargeWorkflow`

## 2026-07-01 - Dense Object Selection Uses Batched Wires

Task/context: `17.tracy` and manual testing showed viewport performance still collapsed when many
objects were selected, especially in rendered mode, and wireframe mode could not visually distinguish
selected meshes from unselected scene wire.

Invariants:

- Do not replace selected mesh topology with a bounding cage during navigation or transforms.
- Persistent selected-object rendering must submit selected mesh wires as a batched renderable, not
  one wire proxy per selected mesh.
- Batched selected wires must cache local edge topology per selected entity/mesh revision. Transform
  drags should stream the transformed line vertex buffer and update the renderable bounds in place,
  not re-run authored polygon edge extraction or recreate the Filament proxy every drag tick.
- Dense object selections suppress translucent selected face-fill proxies and keep the selected wire
  as the stable selection signal; small selections may keep the face fill.
- Wireframe viewport scene-wire entries must use the selected-edge layer/color/width for selected
  meshes, because the normal selected-object overlay layer is not visible in wireframe mode.

Affected owners/files:

- `src/render/overlay/selected_object_overlay_cache.*`
- `src/render/overlay/scene_wireframe_overlay_cache.*`
- `src/render/viewport_overlay_policy.*`
- `src/scene/scene_document.*`
- `docs/overlay-handling.md`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportOverlayPolicyTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-30 - Tracy Profiling Boot Convention And Diagnostic Build

Task/context: Tracy 0.13.1 was installed for local optimization captures. The user-provided profiler
GUI bundle is at `C:\Users\Drako\Desktop\quader-windows\_SOURCES\windows-0.13.1`; the matching
source checkout needed by the app client was installed at
`C:\Users\Drako\Desktop\quader-windows\_SOURCES\tracy-0.13.1`.

Invariant:

- When the user says to "boot up Tracy", launch both
  `_SOURCES\windows-0.13.1\tracy-profiler.exe` and
  `build\vs2026\Debug\quader.exe`, with the profiler first.
- The VS debug build cache is configured with `QUADER_ENABLE_TRACY=ON`,
  `QUADER_TRACY_ON_DEMAND=ON`, and `QUADER_TRACY_SOURCE_DIR` pointing at the 0.13.1 source checkout.
- Tracy zones are diagnostic-only through `src/diagnostics/profile.hpp`; normal builds remain no-op
  unless `QUADER_ENABLE_TRACY` is enabled.
- First capture `C:\Users\Drako\Desktop\quader-windows\logs\one.tracy` showed the largest stall as
  `TransformGizmoInteraction::beginPendingDrag` (~31.4s inside one frame). Add or preserve deeper
  zones in `QuaderModelingApi::beginTransformDrag`, `ModelingSessionService::beginTransformDrag`,
  preview transaction setup, scene sync, snap cache generation, and first preview apply before
  optimizing that path; otherwise the stall appears as opaque viewport wrapper self-time.
- Capture `three.tracy` showed command availability was no longer the duplicate/selection stall,
  but `Viewport refresh selection overlay` still rebuilt every frame because
  `ViewportPanel::drawPaneContents` unconditionally dirtied `selectionOverlayCache_` after active
  tool interaction. Keep selected overlay refresh driven by content/selection/scene/asset revisions
  plus `selectionOverlaySignature()`, not by every viewport frame.
- The same capture showed transform/duplicate preview begin still spending ~880ms in
  `Modeling mesh sync reconcile scene nodes`, while mesh projection was only ~23ms. Preserve the
  sub-zones in `modeling_mesh_sync_service.cpp` around scene identity lookup, name resolution,
  entity creation, parent/identity/material application, and object binding until the remaining
  scene-node stall is named and fixed.
- Capture `seven.tracy` showed the next transform/render stall came from forced modeling mesh sync
  replacing unchanged mesh assets. `AssetRegistry::replaceMesh` stamps a new mesh revision even when
  the compiled payload is identical, which made render sync destroy/recreate mesh proxies for
  untouched objects during component transform previews. `ModelingMeshSyncService` now compares the
  compiled mesh payload and authored polygon document before calling `replaceMesh`; unchanged assets
  keep their revisions so render sync can skip their proxies.
- Capture `eight.tracy` showed the fixed mesh-revision path was working but the dominant frame stall
  moved to `UISystem::beginFrame` self-time. The ImGui Filament renderer was destroying the previous
  frame's UI vertex/index buffers, material instances, and entities at the start of the next frame,
  which can force CPU/GPU synchronization. Capture `nine.tracy` proved merely buffering destruction
  moved the stall to `ImGui destroy retired frame resource batch`; the renderer now reuses persistent
  UI draw resource slots and only rebuilds a slot when vertex/index/primitive capacity grows. The
  same captures showed save spending seconds parsing the just-serialized QDR payload; hot project
  saves now skip self-parse validation and rely on QDR round-trip tests for writer coverage.
  Authored/modeling mesh assets are persisted through their `polygonDocument` only, not redundant
  compiled vertices/indices/primitives; the QDR reader already regenerates compiled render geometry
  from authored topology on load.

Affected owners/files:

- `src/diagnostics/profile.hpp`
- `CMakeLists.txt`
- `cmake/QuaderBuildOptions.cmake`
- `src/editor/modeling/services/modeling_mesh_sync_service.cpp`
- `src/render/imgui_filament_renderer.*`
- `src/io/qdr/qdr_document_reader.cpp`
- `src/io/qdr/qdr_document_writer.cpp`
- `src/scene/document/project_session.cpp`
- app/UI/viewport/render/save/selection/duplicate owner paths instrumented with `QDR_PROFILE_*`

Verification:

- `cmake --preset vs2026 -DQUADER_ENABLE_TRACY=ON -DQUADER_TRACY_SOURCE_DIR=C:\Users\Drako\Desktop\quader-windows\_SOURCES\tracy-0.13.1`
- `cmake --build --preset vs2026-debug --target quader`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ModelingSessionServiceTests.ComponentTransformSyncKeepsUntouchedMeshAssetRevision:ModelingSessionServiceTests.FaceTransformDragKeepsComponentSelectionAndMovesOnlyFaceGeometry:ModelingSessionServiceTests.TransformDragObjectSelectionUsesSceneTransformPreview:ModelingSessionServiceTests.PublicTransformDragFaceSelectionRecordsModelingHistory`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings:QdrDocumentIoTests.WritesAndReadsUnreferencedProjectAssets:ProjectSessionTests.ReplaceDocumentAdvancesSceneRevisionPastPreviousDocument:ModelingSessionServiceTests.ComponentTransformSyncKeepsUntouchedMeshAssetRevision`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QdrDocumentIoTests.SharedPolygonDocumentCodecPreservesAuthoredNormalShadingAcrossProjectAndLegacyQdr:QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings:QdrDocumentIoTests.WritesAndReadsUnreferencedProjectAssets:ProjectSessionTests.ReplaceDocumentAdvancesSceneRevisionPastPreviousDocument:ModelingSessionServiceTests.ComponentTransformSyncKeepsUntouchedMeshAssetRevision`
- Launched `tracy-profiler.exe` and `build\vs2026\Debug\quader.exe`; both processes were alive and
  responding.

## 2026-06-30 - ImGui Combo Icons Must Preserve Popup Stack Ownership

Task/context: replacing native combo/collapsing arrows with Phosphor glyphs regressed the top menu
and left/right sidebar visibility. The custom combo helper had recreated ImGui combo popups by hand,
which could desynchronize global popup/menu state used by `BeginMenu`; the top menu helper also
contained a manual "click an already-open menu to close it" path that could consume menu-bar popup
opens. A later viewport pass-through helper also closed any open popup after the menu bar drew, so
top-level menus could open and then immediately disappear in the same frame.
The rightmost `Layout` menu could also fall past the native borderless chrome's left interactive
strip and be treated as title-bar drag.

Invariant:

- Custom combo drawing may own the frame, preview text, caret glyph, and toggle decision, but popup
  creation must go through ImGui's combo popup owner (`BeginComboPopup`) so callers can keep using
  `ImGui::EndCombo()`.
- Main menu bars must use ImGui's normal `BeginMenu`/`EndMenu` popup lifecycle. Do not add manual
  `IsPopupOpen`/`CloseCurrentPopup` toggle logic to top-level menus unless the behavior is verified
  against real menu-bar clicks.
- Viewport navigation pass-through may auto-close viewport combo popups, but it must be scoped to
  combo popup windows (`##Combo_`) and to clicks inside the viewport pane. It must not close menu
  popups created by the top chrome, sidebars, or other non-viewport surfaces.
- Borderless chrome hit testing must leave the whole top menu row in the left interactive strip. If
  menu labels change, update `PlatformWindowChromeHitTest::leftInteractiveWidth` and the chrome
  hit-test regression so rightmost menu entries such as `Layout` are not draggable title bar.
- Strip small combo height flags for Quader icon combos when the current option lists should auto-fit
  without scrollbars.
- Local panel visibility lives in `%LOCALAPPDATA%\Quader\ui_settings.ini`; if outliner/inspector are
  false, source defaults will not make them appear until settings are restored or changed through UI.

Affected owners/files:

- `src/ui/imgui_quader_widgets.cpp`
- `src/ui/imgui_popup_utils.cpp`
- `src/ui/imgui_popup_utils.hpp`
- `src/ui/main_menu_bar.cpp`
- `src/ui/viewport_panel.cpp`
- `src/platform/platform_window.hpp`
- `tests/core_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/build_guardrail_tests.cpp`
- local user config `%LOCALAPPDATA%\Quader\ui_settings.ini`

Verification:

- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:UiLayoutGuardrailTests.ViewportChromeCombosSharePopupPassThroughClickHandling:UiLayoutGuardrailTests.MainMenuDropdownsUsePopupPaddingAndVerticalSpacing`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.AppVersionMetadataUsesRootVersionSource`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-30 - Vertex Snap Must Not Steal Plain Gizmo-Overlapped Selection Clicks

Task/context: with vertex snap enabled, clicking a vertex that was overlapped by the move gizmo could
start transform-drag preview on mouse-down and show the snap target indicator instead of behaving like
a normal component selection click.

Invariant:

- Transform gizmo press is only a pending grab until the pointer crosses the shared selection-box drag
  threshold. Release before that threshold must route back through viewport selection fallback.
- Vertex snap target resolution belongs to active transform-drag update frames only. The press/begin
  preview must use a zero snap radius so snap indicators cannot appear on ordinary clicks.
- Selection fallback owns the actual pick; gizmo code only reports a deferred click through
  `ViewportBeforeToolInteractionResult`.

Affected owners/files:

- `src/editor/viewport/transform_gizmo_interaction.*`
- `src/editor/viewport/viewport_selection_interaction.*`
- `src/editor/viewport/viewport_interaction_frame.hpp`
- `tests/viewport_interaction_controller_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.VertexSnapDoesNotStealPendingMoveGizmoVertexClick:ViewportInteractionControllerTests.MoveGizmoComponentAxisDragKeepsPickedAxisUnderMouse:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedPointLightThroughViewportFrames:ViewportInteractionControllerTests.RotateGizmoDragEmitsPieAngleIndicator`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.*`
- `build\vs2026\Debug\quader_tests.exe`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-30 - Component Move Gizmo Axis Drags Must Use Axis-Ray Distance

Task/context: Godot-style gizmo visual refactoring exposed bad move-drag behavior in vertex/edge/face
component modes: Z-axis drags could visibly drift away from the mouse in perspective, and axis/plane
hit overlap made it too easy for a visually axis-like click to use another constraint.

Root cause/invariant:

- Single-axis move drags must derive distance from the current mouse ray projected against the picked
  world axis, relative to the press-time axis distance. Pixel-to-world fallback is only for synthetic
  callers whose rays do not change with screen position.
- Vertex snap targets on a constrained axis must project onto `state.world_axis`, not recompute by
  enum component, so visual/world direction stays authoritative.
- Transform gizmo picking must give visible axes priority before plane handles when the cursor lies on
  an axis shaft or tip.

Affected owners/files:

- `src/editor/interactions/move_tool.*`
- `src/editor/modeling/runtime/transform_gizmo.cpp`
- `tests/viewport_interaction_controller_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.MoveGizmoComponentAxisDragKeepsPickedAxisUnderMouse`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=*TransformDrag*:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedPointLightThroughViewportFrames`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.*:ViewportTransformGizmoOverlayBuilderTests.*`

## 2026-06-29 - Undo Must Not Be Swallowed By Idle Modal Tools

Task/context: after moving entities with the viewport transform gizmo, Ctrl+Z could appear to do
nothing because undo was allowed to route through the broad "cancel active tool or preview" path.

Root cause/invariant:

- `QuaderApiMutationRunner` owns editor-history undo routing.
- Ctrl+Z may cancel live operation previews and transform drags because those mutate temporary
  editor state before a document history item exists.
- Idle modal tools such as Box/Cut/Mirror owning the viewport must not consume document undo. Escape
  and the explicit cancel command own modal-tool cancellation.
- If a pre-undo cancel attempt reports `changed=false`, the undo route must fall through to the
  shared document history stack when one is available.

Affected owners/files:

- `src/api/quader_api_mutation_runner.cpp`
- `src/editor/modeling/services/modeling_session_service.*`
- `tests/editor_api_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`

Resolved approach:

- Added `ModelingSessionService::canCancelActivePreviewForUndo()` for the narrower preview/drag
  predicate used by history undo.
- `QuaderApiMutationRunner::undo()` now uses that predicate and falls through to document history
  when cancel returns unchanged.
- Added regression coverage for idle modal tool + document undo, and for viewport gizmo drags on
  both mesh objects and point lights restoring through `HistoryUndo`.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.HistoryUndoUsesDocumentHistoryWhenOnlyIdleModalToolIsActive:EditorApiTests.HistoryUndoCancelsActiveBevelPreviewBeforeDocumentHistory:EditorApiTests.MixedUndoRedoPreviewStackPreservesTexturedMaterial:EditorApiTests.PublicHistoryUndoRedoMatchesCommandApiStatus:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedPointLightThroughViewportFrames:ModelingSessionServiceTests.PublicTransformDragObjectSelectionRecordsSceneTransformHistory`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-29 - Scene Settings Fields Must Stay Undoable

Task/context: scene-root Environment/IBL, Camera, and Post Process settings are edited from the
inspector and must each participate in normal undo/redo.

Root cause/invariant:

- Scene-global render settings live on the real scene root as one `SceneSettingsComponent`.
- UI edits must copy current settings, change the intended field, and route through
  `context.editor.scene().setSceneSettings(...)`; UI must not call `SceneDocument::setSceneSettings`
  directly.
- `SetSceneSettingsCommand` must compare and restore the full settings struct, so adding a new
  environment, camera, or post-process field requires updating API conversion, equality, QDR IO, and
  `EditorApiTests.SceneSettingsPropertiesUndoAndRedoIndividually`.

Affected owners/files:

- `src/scene/components.hpp`
- `src/api/quader_api_type_conversions.hpp`
- `src/editor/commands/document/set_scene_settings_command.hpp`
- `src/ui/inspector_panel.cpp`
- `tests/editor_api_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Verification:

- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.SceneSettingsPropertiesUndoAndRedoIndividually`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.InspectorSceneSettingsEditThroughPublicSceneApi:UiLayoutGuardrailTests.InspectorLightEditsRouteThroughPublicSceneApi`

## 2026-06-29 - Undo/Redo Shortcuts Are Editor-Global

Task/context: Ctrl+Z/Ctrl+Y worked only after focusing the viewport. Inspector/property edits were
undoable through history, but keyboard dispatch never reached the history command while a panel
owned focus.

Root cause/invariant:

- `QuaderCommandsApi` owns default shortcut contexts. Editor-global command shortcuts must use a
  global `!textInputActive` context, not `viewportFocus`.
- Viewport-only commands and router shortcuts still require `viewportFocus && !textInputActive &&
  !navigation.active`.
- `UISystem` owns top-level shell input dispatch. When a non-viewport panel owns focus, it must
  route pressed chords through the command API and execute only command matches. Do not hard-code
  Ctrl+Z/Ctrl+Y in UI code.
- Active text input must keep blocking editor-global shortcut dispatch so text widgets keep normal
  editing behavior.

Affected owners/files:

- `src/api/quader_commands_api.cpp`
- `src/ui/ui_context.hpp`
- `src/ui/ui_system.cpp`
- `src/ui/viewport_panel.cpp`
- `src/ui/viewport_input_frame_builder.*`
- `tests/editor_api_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.GlobalCommandKeybindingsResolveWithoutViewportFocus:EditorApiTests.GlobalCommandKeybindingsDoNotStealTextInputOrViewportOnlyShortcuts:EditorApiTests.ViewportKeybindingsResolveContextAndUserOverrides:EditorApiTests.InputBindingsSupportPunctuationAndMouseChords:EditorApiTests.ViewportHostShortcutCatalogDrivesCommandKeybindings:EditorApiTests.DefaultKeybindingProjectionDoesNotDuplicateRuleTuples`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-29 - Continuous UI Edits Are One Undo Gesture

Task/context: dragging inspector values such as IBL intensity created one undo command per ImGui
change frame, so Ctrl+Z walked intermediate values instead of restoring the pre-drag state.

Root cause/invariant:

- Public API/document commands intentionally record one history item per call unless an owner
  explicitly opts into coalescing. Do not add blind history merging that collapses separate scripts,
  commands, or user intents.
- Continuous inspector controls (`Drag*`, `Slider*`, `ColorEdit*`, live text-style inputs) must apply
  live changes through the authoritative public API path and pass one stable merge key for the
  active gesture. History keeps the first `before` value and replaces the final `after` value, so one
  undo restores the pre-drag state without delaying viewport/render updates.
- `IsItemDeactivatedAfterEdit()` is a lifecycle/reset/final-value signal for the UI helper; it must
  not be the only moment when document/render state changes.
- Discrete controls such as checkboxes, combos, buttons, and menu items can commit immediately.
- Merge keys are gesture-scoped and are stored with undo stack entries. Commands of the same
  type/entity must not merge when the previous stack entry was created with a different key.
- Query history snapshots must use the same public history availability owner as keyboard undo, so
  active previews that Ctrl+Z can cancel are reported as undoable.

Affected owners/files:

- `src/ui/ui_continuous_edit_tracker.hpp`
- `src/ui/inspector_panel.*`
- `src/core/i_undoable_command.hpp`
- `src/core/undo_redo_execute_options.hpp`
- `src/core/undo_redo_manager.*`
- `src/editor/history/editor_history_service.*`
- `src/api/quader_commands_api.cpp`
- `src/api/quader_query_api.*`
- `tests/core_tests.cpp`
- `tests/editor_api_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Resolved approach:

- Added `UIContinuousEditTracker` and converted scene-root Environment/Camera/Post Process drags and
  light color/intensity/range controls to apply live via `setSceneSettings` or `setLightComponent`
  while passing the active gesture merge key.
- Added opt-in history coalescing with `UndoRedoExecuteOptions::mergeKey`; transform, light, and
  scene-settings before/after commands can merge only with the same command type/entity when the
  previous stack entry and the new call use the same non-empty key.
- Default undo/redo keybinding contexts now include `canUndo`/`canRedo`, including host-provided
  `Ctrl+Shift+Z`, so disabled shortcuts do not dispatch failed commands.
- `QuaderQueryApi::history()` now uses `QuaderApiMutationRunner::canUndo()` for public undo
  availability.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=DocumentCommandTests.UndoRedoManagerCoalescesOnlyExplicitMergeKeys:DocumentCommandTests.EditorTransactionsCoalesceExplicitMergeKeysBeforeCommit:DocumentCommandTests.SceneBeforeAfterCommandsSupportExplicitMergeKeys:EditorApiTests.ScenePointLightLiveEditsApplyImmediatelyAndCoalesceByMergeKey:EditorApiTests.SceneSettingsLiveEditsApplyImmediatelyAndCoalesceByMergeKey:EditorApiTests.ScenePointLightApiCreatesSnapshotsAndEditsThroughHistory:EditorApiTests.SceneSettingsPropertiesUndoAndRedoIndividually`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.InspectorContinuousInputsLiveApplyWithMergeKeys:UiLayoutGuardrailTests.InspectorSceneSettingsEditThroughPublicSceneApi:UiLayoutGuardrailTests.InspectorLightEditsRouteThroughPublicSceneApi:ProgrammaticApiGuardrailTests.EditorHistoryBoundaryHidesCoreUndoRedoManager:ProgrammaticApiGuardrailTests.PublicHistoryDoesNotFallbackToModelingHistory`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.DefaultKeybindingProjectionDoesNotDuplicateRuleTuples:EditorApiTests.PublicHistoryUndoRedoMatchesCommandApiStatus:EditorApiTests.HistoryUndoUsesDocumentHistoryWhenOnlyIdleModalToolIsActive`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-29 - Point Lights Are Scene Entities With Viewport Gizmos

Task/context: the first Filament point-light implementation rendered a selected light in the
inspector, but W/R/S activation could clear the scene-only selection, the transform gizmo did not
appear, and the selected light had no viewport marker. User-facing intensity also needed values in
the thousands to affect the viewport.

Root cause/invariants:

- Point lights are scene entities, not modeling runtime objects. Selection/query/gizmo paths must
  route through the scene-aware editor selection service for scene-only entities while preserving
  component selection summaries for modeling mesh selections.
- Activating transform tools must not sync away scene-only transform selections. Preserve selected
  transformable scene entities through runtime tool activation when the modeling runtime has no
  active selection.
- Transform drag begin must accept scene-only transform selections and use scene bounds when the
  modeling runtime has no selection bounds.
- Selected point lights need an editor overlay marker even when they have no mesh renderable.
- User-facing punctual light intensity is Godot-style energy; viewport Filament lumens use the
  Godot base conversion plus a viewport exposure compensation factor until engine-specific export
  mappings are introduced.

Affected owners/files:

- `src/editor/selection/editor_selection_service.cpp`
- `src/api/quader_query_api.cpp`
- `src/editor/modeling/services/modeling_session_service.cpp`
- `src/render/overlay/viewport_scene_overlay_builder.cpp`
- `src/render/light_renderable_cache.cpp`
- `tests/editor_api_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`
- `tests/render_extraction_tests.cpp`

Resolved approach:

- `QuaderQueryApi` now asks `ProjectSession::selection()` for summaries/centers, and the editor
  selection service reports `Light` only for scene-only selections so component selections keep
  their modeling summaries.
- Runtime tool activation preserves scene-only transform selections; transform drag begin routes
  scene-only drags as object-mode scene transform previews and falls back to selected scene bounds.
- The scene overlay builder adds a selected point-light marker and small axis ticks at the light
  transform origin.
- Point/spot intensity `1.0` maps to `10,000,000` Filament lumens in the viewport:
  `1000` Godot-equivalent lumens times `10000` viewport exposure compensation.

Verification:

- `cmake --build --preset vs2026-debug --target quader quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.ScenePointLightSelectionFeedsQuerySummaryAndCenterInComponentMode:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedPointLightThroughViewportFrames:ViewportSceneOverlayBuilderTests.AddsSelectedLightMarker:LightRenderableCacheTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames:ViewportSceneOverlayBuilderTests.*:EditorApiTests.SceneCreatePointLightCommandCreatesSelectedPointLight`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportToolGuardrailTests.TransformGizmoEligibilityLivesInViewportToolMetadata:ModelingSessionServiceSplitTests.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.InspectorLightEditsRouteThroughPublicSceneApi:UiLayoutGuardrailTests.MainMenuDropdownsUsePopupPaddingAndVerticalSpacing`

## 2026-06-30 - Transform Gizmo Visuals Follow Godot Proportions

Task/context: Quader transform gizmo visuals looked heavier and less like Godot's editor gizmos.
The reference source was `C:\Users\Drako\Desktop\_QUADER_DEV\resources\_SOURCE_CODE\godot-master`.

Root cause/invariants:

- Runtime gizmo pick geometry and render overlay geometry must share the same proportions, otherwise
  visual handles drift from interactive handles.
- Godot-style constants are the source of truth for visual proportions: circle size `1.1`, plane
  size `0.2`, plane distance `0.3`, arrow offset `1.4`, arrow tip `1.75`, scale tip `1.554`,
  view rotation scale `1.14`, and viewport gizmo size `80` pixels.
- Plane handle colors follow the omitted normal axis, not the visible two-axis label: XY is Z/blue,
  YZ is X/red, XZ is Y/green.
- Active rotate drags must carry a rotate angle indicator through `TransformGizmoFrame`: the
  overlay draws the restored pie/wedge fill, the radial line from the center to the original grab
  direction, and the Godot-style center-to-current-mouse line.

Affected owners/files:

- `src/editor/modeling/runtime/transform_gizmo.cpp`
- `src/render/overlay/viewport_transform_gizmo_overlay_builder.cpp`
- `src/render/viewport_overlay_policy.hpp`
- `tests/modeling_domain/tool/qdr_modeling_runtime_tool_transform_gizmo_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`

Resolved approach:

- Updated runtime gizmo frame generation to use Godot-style pixel proportions for move, scale,
  plane, and rotate primitives.
- Reworked overlay rendering to use slim arrow cones, square-prism scale handles, full rotate rings
  for the neutral trackball/view ring, front-only colored rotate axis arcs, Godot axis colors,
  normal-axis plane colors, and no move center dot or black axis underpaint.
- Restored the previous rotate drag pie indicator as overlay geometry owned by the transform gizmo
  frame/builder instead of UI-only immediate drawing.
- Added guardrail coverage for the Godot-style dimensions, colors, and emitted overlay primitives.

Verification:

- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntime*:QdrModelingRuntimeToolTransformGizmoTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=GroundGridMaterialTests.*:LightRenderableCacheTests.*:ModelingSessionService*Hover*:ModelingSessionService*Overlay*:ModelingSessionService*Pick*:Overlay*:QuaderMeshMaterialTests.*:Render*:RenderExtractionTests.*:SceneRenderBridge*:TextureRegistryTests.*:Viewport*:ViewportBoxOverlayBuilderTests.*:ViewportInteractionControllerTests.*:ViewportSelectionMarqueeOverlayBuilderTests.*:ViewportTransformGizmoOverlayBuilderTests.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime`
- `build\vs2026\Debug\quader_modeling_domain_metadata_loop_tests.exe --gtest_color=no --gtest_filter=QdrEditorMetadata*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-29 - ImGui Viewport Layouts Require MultiPane Public Workspace

Task/context: the ImGui shell layout menu and viewport panel still contained the restored multi-pane
draw path, but choosing any layout other than Single stayed single-pane.

Root cause/invariant:

- The earlier ImGui path owned `ViewportPanel::setViewportLayout()` directly and rendered
  placements from `ViewportLayoutState`.
- After `ViewportWorkspaceService` became the viewport owner, `QuaderViewportApi` was left
  constructing its workspace with `ViewportWorkspaceLayoutPolicy::SinglePaneOnly`, a policy from the
  temporary single-pane host. The current ImGui menu routes through
  `editor.viewports().setLayout(...)`, so the API rejected multi-pane before `ViewportPanel` could
  draw the restored placements.
- Keep `SinglePaneOnly` available only for hosts that actually expose one embedded viewport surface;
  the active native ImGui editor API workspace must be `MultiPane`.

Affected owners/files:

- `src/api/quader_viewport_api.cpp`
- `src/viewport/viewport_workspace_service.hpp`
- `src/ui/main_menu_bar.cpp`
- `src/ui/viewport_panel.cpp`
- `tests/editor_api_tests.cpp`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Changed the public/editor viewport API workspace default back to `ViewportWorkspaceLayoutPolicy::MultiPane`.
- Kept layout state in `ViewportWorkspaceService`; `MainMenuBar` continues to call the public API,
  and `ViewportPanel` continues to consume `activePanePlacements()` and `activeSplitHandles()` from
  the workspace.
- Updated the public API viewport test expectation from rejected FourQuad/single-pane to accepted
  FourQuad/four active panes, and added a focused build guardrail for the active ImGui layout route.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiViewportApiKeepsMultiPaneLayoutWorkspace`
- Older affected `quader_tests` verification was previously blocked by stale test sources; the
  native UI cleanup removed those stale backend references.

## 2026-06-29 - Win32 Resize State And Window Placement Persistence

Task/context: nightly window slice B tightened borderless Win32 resize behavior and added persisted
native window placement.

Root cause/invariants:

- Borderless Win32 resize hit testing must use the current DPI-scaled system frame and padded-border
  metrics, not only the fixed fallback hit band.
- Native resize state must clear on normal exit and on cancellation paths such as `WM_CANCELMODE`,
  `WM_CAPTURECHANGED`, focus loss, and destruction; otherwise viewport resize suspension can stay
  active after Windows aborts the modal resize.
- Persist the normal window bounds separately from maximized state. When restoring, recover the
  normal frame into a visible display work area before applying maximize.

Affected owners/files:

- `src/platform/platform_window.*`
- `src/app/app.*`
- `src/app/app_window_settings.*`
- `src/core/user_config_paths.*`
- `src/ui/ui_settings.cpp`
- `tests/build_guardrail_tests.cpp`
- `tests/core_tests.cpp`

Resolved approach:

- Added `PlatformWindowPlacement`, native `GetWindowPlacement`/`SetWindowPlacement` use on Win32,
  display-work-area recovery, DPI/system-metric resize borders, duplicate live-resize frame
  coalescing, and forced final redraws on resize end/cancel/maximize/restore.
- Added app-owned `window_settings.ini` serialization for normal bounds and maximized state, while
  moving the shared product config directory lookup into `quaderUserConfigDirectory`.
- Added focused guardrail coverage plus behavior tests for settings round-trip, resize border
  metrics, and offscreen recovery.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=WindowGuardrailTests.WindowPlacementPersistenceAndResizeRecoveryStayOwned`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- Older `quader_tests` verification was previously blocked by stale test sources; the native UI
  cleanup removed those stale backend references.

## 2026-06-28 - ImGui Borderless Resize Must Redraw During Native Live Resize

Task/context: the `feature/imgui-shell` branch restored custom borderless chrome with ImGui. Window
resize had two regressions: growing from a small window lagged or snapped on mouse-up, while
shrinking later clipped the UI until mouse-up.

Root cause/invariants:

- On Windows, native move/resize runs a modal loop where normal `SDL_PollEvent()` rendering can
  stall. Growing needs redraws for newly exposed pixels; shrinking needs redraws from native size
  messages even when no expose event is generated.
- Do not drive live borderless resize with per-motion `SDL_SetWindowPosition` plus
  `SDL_SetWindowSize` on Windows. Use native hit testing (`WM_NCHITTEST`) so Windows owns resize
  geometry, and keep the SDL size path only as fallback.
- `SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION)` is wrong for this path: it drops live resize input and
  couples resize progress to render cadence.
- During live resize, avoid exact viewport render-target reallocation on every pixel; keep drawing
  the current viewport texture and exact-resize again when the resize loop ends.

Affected owners/files:

- `src/app/app.cpp`
- `src/app/app.hpp`
- `src/platform/platform_window.cpp`
- `src/platform/platform_window.hpp`
- `src/ui/ui_context.hpp`
- `src/ui/ui_system.cpp`
- `src/ui/ui_system.hpp`
- `src/ui/viewport_panel.cpp`

Resolved approach:

- Added one guarded `App::renderFrame()` path used by both the normal loop and live resize redraws.
- Added `SDL_AddEventWatch` handling for `SDL_EVENT_WINDOW_EXPOSED` with `data1 == 1` so growing
  windows repaint during SDL/Windows live resize.
- Enabled Win32 `WM_NCHITTEST` for resize edges and the draggable titlebar (`HT*` and `HTCAPTION`)
  so native Windows resize owns live geometry.
- Added a platform live-resize callback from `WM_SIZE`, `WM_WINDOWPOSCHANGED`, and
  `WM_EXITSIZEMOVE` so shrinking windows repaint during the native modal resize loop.
- Added `PlatformWindow::chromeResizeActive()` and UI render-resize suspension so viewport render
  targets are not reallocated every pixel while the chrome resize is active.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- Manual smoke in `build\vs2026\Debug\quader.exe`: grow from small window and shrink from large
  window both repaint during drag instead of lagging, clipping, or snapping only on mouse-up.

## 2026-06-28 - ImGui Shell Flush Viewport, Visible Checkboxes, And Work-Area Maximize

Task/context: the ImGui shell had visible gutters around docked viewport/panels, clipped viewport
combo labels, invisible unchecked checkboxes, mismatched checked checkbox colors, and a black strip
near the Windows 11 taskbar when maximized.

Root cause/invariants:

- The ImGui dockspace host must push zero `WindowPadding` before `DockSpace`; otherwise the dock node
  inherits the global 8px window padding and leaves viewport/panel gutters, including a bottom strip.
- Viewport chrome combo widths must be measured from localized labels with the active frame padding,
  not fixed constants such as 112/104px.
- Global ImGui checkbox/input frames need a visible 1px border and a frame background distinct from
  the panel background; checked ticks should be neutral/light on a coherent dark frame, not a purple
  tick on a blue/purple active frame.
- Borderless Win32 maximize must handle `WM_GETMINMAXINFO` and `WM_NCCALCSIZE`: the maximize bounds
  come from the monitor `rcWork`, and the maximized client area must be computed during non-client
  sizing so drawable content never extends behind the taskbar.
- Do not fix borderless maximize by calling `SetWindowPos` after `ShowWindow(SW_MAXIMIZE)` or from
  `WM_WINDOWPOSCHANGED`; it fights the native maximize transaction, can flicker, and can fail on
  later maximize/restore cycles.
- A fixed shell bottom bar must be a separate viewport-aligned ImGui window and the dockspace host
  height must reserve its height. Do not draw it as content inside the dockspace host with `Dummy`,
  because that creates whole-app scrolling and gaps between panels and the bar.

Affected owners/files:

- `src/ui/ui_system.cpp`
- `src/ui/viewport_panel.cpp`
- `src/ui/ui_style_tokens.cpp`
- `src/platform/platform_window.cpp`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added local zero-padding style scope around the main ImGui dockspace host.
- Replaced fixed viewport combo widths with measured max widths across localized view/shading labels.
- Set `FrameBorderSize = 1`, raised frame border/background contrast, and changed checkmarks to a
  near-white color.
- Added Win32 `WM_GETMINMAXINFO` handling for borderless chrome maximized work-area sizing.
- Added Win32 `WM_NCCALCSIZE` handling, matching the Electron/Chromium pattern, so the maximized
  frameless client area is the monitor work area instead of a later-corrected outer frame.
- Added a fixed static ImGui bottom bar for visual maximize/taskbar verification, with the dockspace
  host reserving the bar height and disabling host scrolling.
- Added focused guardrails for the active ImGui shell/style invariants and work-area maximize path.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:WindowGuardrailTests.BorderlessMaximizeUsesMonitorWorkArea`
- Native Win32 smoke: launch `build\vs2026\Debug\quader.exe --borderless-window`, run four
  `ShowWindow(SW_MAXIMIZE)` / `ShowWindow(SW_RESTORE)` cycles, and verify the screen-space client
  rect stays inside `MONITORINFO.rcWork` on every maximize.
- Screenshot/pixel smoke: maximized client rect equals `MONITORINFO.rcWork`, the bottom bar accent
  is visible at the client bottom, and no whole-app scrollbar or gap appears above the bar.

## 2026-06-28 - Undo Cancels Active Tool Previews Before Document History

Task/context: Ctrl+Z during an active modeling preview could consume document history first, making
the object disappear; finalized preview operations such as Slice Quad could undo only to the
post-preview selected faces instead of the pre-operation mesh and selection.

Root cause/invariants:

- Public undo is a command/history intent, but the API mutation boundary must first cancel active
  runtime ownership: operation previews, transform previews/drags, scene transform previews, and
  modal viewport tools. Command registration should still ask the history API for availability.
- Preview commit history must capture the preview transaction base state as the undo "before"
  memento. Capturing after the preview has already mutated the live session records the wrong
  boundary and makes undo restore the committed preview state.
- Texture/material identity must survive preview cancel, finalized topology undo/redo, and
  delete/restore stacks.

Affected owners/files:

- `src/api/quader_api_mutation_runner.*`
- `src/api/quader_history_api.cpp`
- `src/api/quader_commands_api.cpp`
- `src/editor/history/modeling_memento_command.hpp`
- `src/editor/history/modeling_preview_command_service.hpp`
- `src/editor/modeling/runtime/session.*`
- `src/editor/modeling/services/modeling_session_service.*`
- `tests/editor_api_tests.cpp`

Resolved approach:

- Added active-preview/tool cancellation to `QuaderApiMutationRunner::undo()` and surfaced the same
  decision through `QuaderApiMutationRunner::canUndo()` so Ctrl+Z is enabled while a runtime cancel
  is possible without adding split modeling-history routes in command registration.
- Exposed the active preview transaction history state through the modeling session service and let
  preview/transform commit commands override their before-memento capture with that base state.
- Added public query accessors for material and texture assets so behavior tests can assert texture
  preservation through history stacks.
- Added focused editor API regressions for bevel-preview undo cancel, Slice Quad finalized undo, and
  a mixed undo/redo stack covering texture assignment, preview cancel, slice undo/redo, delete,
  redo, and restore.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.HistoryUndoCancelsActiveBevelPreviewBeforeDocumentHistory:EditorApiTests.HistoryUndoAfterSliceQuadFinalizeRestoresPreOperationMeshAndSelection:EditorApiTests.MixedUndoRedoPreviewStackPreservesTexturedMaterial`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=*:ProgrammaticApiGuardrailTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=*:EditorApiTests.*:EditorApiTests.*History*:EditorApiTests.*Modeling*:EditorApiTests.*Selection*:GroupingPublicApiTests.*:ModelingSessionService*:ModelingToolCatalogTests.*:ProgrammaticApiTests.*:ProgrammaticBehaviorScenarioTests.*:SelectionModeAdapterTests.*`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no --gtest_filter=*`
- `build\vs2026\Debug\quader_modeling_domain_metadata_loop_tests.exe --gtest_color=no --gtest_filter=QdrEditorMetadata*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntime*`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Modeling Tests Are GoogleTest-Only

Task/context: the modeling-domain test harness was migrated permanently to GoogleTest and the old
test framework was removed as a CMake and third-party dependency.

Root cause/invariant:

- Native tests must use GoogleTest only. Do not add parallel test frameworks or fallback source-fetch
  policy for modeling-domain tests.
- App/runtime behavior tests live in `quader_tests`, source-policy scans live in
  `quader_guardrail_tests`, and modeling-domain tests are one aggregate executable,
  `quader_modeling_domain_tests`, registered through explicit categorized source lists in
  `cmake/QuaderModeling.cmake`.
- Project-owned files must not reintroduce retired test-framework include paths, macros, dependency
  options, source-policy docs, or license evidence.

Affected owners/files:

- `cmake/QuaderBuildOptions.cmake`
- `cmake/QuaderModeling.cmake`
- `tests/modeling_domain/**`
- `tests/build_guardrail_tests.cpp`
- `agents/testing-build.md`
- `agents/dependencies.md`
- `docs/absorbed_modeling/README.md`
- `thirdparty/THIRD_PARTY_MANIFEST.md`

Resolved approach:

- Removed the old dependency option, commit pin, source-dir cache variable, FetchContent path, and
  test-support compile definition.
- Rewired `quader_modeling_domain_tests` to `GTest::gtest` and `GTest::gtest_main`.
- Converted modeling-domain test sources to GoogleTest syntax and moved shared approximate
  comparison support into `tests/modeling_domain/test_support.hpp`.
- Removed old third-party manifest/license entries and added guardrails that scan project-owned
  files for retired test-framework tokens.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.*:DependencyLicenseGuardrailTests.*`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Source-Wire Overlays, Flipped Mesh Picking, And Inverted Face Fill

Task/context: source-wire and selection overlays had a long-running set of regressions: large wire
gaps, fading or missing edges at distance/angles, hidden internal source-wire edges on inverted
meshes, hover/deselect colors failing for inverted internal edges, occluded internal handles being
pickable from outside, and face-fill hover/selection not appearing on inverted faces.

Root cause/invariants:

- Source-wire topology is editor interaction geometry and should render as crisp draw-on-top lines.
  `SourceWireDepthStamp` is metadata only and must not render or delete source-wire line topology.
- Picking and rendering are separate concerns. Render extraction should not CPU-filter source-wire
  edges to solve pickability; source-wire interaction is accepted or rejected by picking/hover
  occlusion policy.
- Editor picking needs nearest visible two-sided surface behavior for flipped/backfacing meshes:
  backfaces are pickable, but authored front-facing faces must not beat nearer visible backfaces.
- Selected/hover face-fill overlays need two-sided triangle render state so inverted faces show fill;
  duplicating reversed geometry is not the owner fix.

Affected owners/files:

- `src/editor/modeling/runtime/session_picking.cpp`
- `src/mesh/polygon/document.hpp`
- `src/mesh/polygon/quader_poly_document_picking_overlay.cpp`
- `src/editor/projection/picking_cache_projection.cpp`
- `src/editor/modeling/runtime/session_selection_overlay.cpp`
- `src/render/viewport_overlay_policy.cpp`
- `src/render/overlay/overlay_renderable_builder.cpp`
- `src/render/overlay/overlay_renderable_builder.hpp`
- `src/render/overlay/overlay_face_fill_builder.cpp`
- `tests/modeling_domain/qdr_modeling_runtime_picking_tests.cpp`
- `tests/modeling_domain/qdr_poly_geometry_characterization_tests.cpp`
- `tests/render_extraction_tests.cpp`
- `tests/core_tests.cpp`
- `docs/overlay-handling.md`
- `agents/rendering-overlays.md`

Resolved approach:

- Kept `SourceWire` draw-on-top and kept `SourceWireDepthStamp` non-rendered.
- Added `PickOptions::prefer_front_facing_faces`, preserving the old low-level default while opting
  editor picking into nearest-surface behavior.
- Included the new pick option in face-geometry cache signatures.
- Routed editor hover/pick paths through one shared editor surface pick policy.
- Added modeling-domain tests for outside-vs-inside picking on flipped geometry and source-wire
  occlusion.
- Made selected and hover face-fill overlay layers two-sided in `ViewportOverlayPolicy`, preserving
  the existing depth-stamped two-pass fill pipeline.
- Added render extraction and policy tests asserting selected/hover face fills use no triangle
  culling and two-sided transparency.
- Added `docs/overlay-handling.md` and explicit overlay status tests for the final object-mode
  x-ray, component-mode source-wire, source-wire depth-stamp, inside-out visibility, and
  outside-vs-inside picking contracts.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `ctest --preset vs2026-debug --output-on-failure`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-27 - Project-Owned C++ File Naming Invariant

Task/context: the native app source tree had mixed PascalCase and snake_case C++ filenames, plus
project headers using mixed extensions. The repository now standardizes project-owned C++ filenames
so future refactors, CMake entries, includes, and tests have one naming convention.

Invariant:

- Project-owned C++ files under `src`, `tests`, `examples`, `benchmarks`, and `tools` use
  snake_case basenames only.
- Project-owned C++ source/header extensions are only `.cpp` and `.hpp`.
- Third-party/vendor headers keep their upstream names and extensions.

Affected guardrails:

- `tests/build_guardrail_tests.cpp` enforces snake_case `.cpp`/`.hpp` project filenames, rejects
  stale PascalCase or old-extension project path references, rejects broken extension rewrite
  artifacts, and rejects temporary rename files.
- `tests/support/guardrail_file_scan.cpp`, `tests/programmatic_api_guardrail_tests.cpp`, and
  `tests/oop_architecture_guardrail_tests.cpp` scan `.hpp` project headers instead of old header
  extensions.

Resolved approach:

- Renamed project-owned C++ files to snake_case and changed project headers to `.hpp`.
- Updated CMake, docs, examples, source includes, and tests to the new paths.
- Repaired mechanical rewrite fallout in member names such as `history_status`, `has_selection`,
  `has_projection`, and `hard_normal_edges`.
- Restored a mechanically rewritten external vendor include path during the header-extension rename.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `cmake --build --preset vs2026-debug --target quader`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.*:OopArchitectureGuardrailTests.*:ProgrammaticApiGuardrailTests.*`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Affected Build/Test Workflow

Task/context: prototype loops were paying the full app/test cost for narrow source changes. The
fast path now uses an affected workflow before full verification, while still printing a concrete
before -> after timing when requested.

Invariant:

- Use `tools/affected_check.ps1` for scoped build/test checks during prototyping and agent loops.
- Pass explicit comma-separated `-ChangedPath` values in dirty worktrees so the plan reflects the
  intended edit, not unrelated local changes.
- Route pure source-policy scans through `quader_guardrail_tests`; do not force `quader_tests` to
  compile/link for docs, tooling, CMake policy, or API/source-boundary guardrails.
- Modeling-domain loop targets are developer-only `EXCLUDE_FROM_ALL` executables used by the
  affected workflow. Do not register them with CTest; full verification stays on the aggregate
  `quader_modeling_domain_tests` entry.
- Use `-MeasureFull` when a task needs a concrete before -> after comparison.
- Use `-IncludeApp` when manual in-app verification or runtime resource copying is part of the
  change.

Affected owners/files:

- `tools/affected_check.ps1`
- `CMakeLists.txt`
- `cmake/QuaderModeling.cmake`
- `agents/testing-build.md`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added path-based target and GoogleTest filter planning for render, UI, viewport, modeling,
  polygon/geometry, QDR, resources, docs, tools, CMake, and direct test-source edits.
- Split pure source-policy scans into one lightweight `quader_guardrail_tests` executable so docs
  and tooling changes do not pay the app/runtime test link cost.
- Kept the workflow in the existing `build/vs2026` tree and added one lightweight guardrail
  executable plus three modeling-domain loop executables instead of per-test artifacts or extra
  artifact folders.
- Split modeling-domain test sources into shared category object buckets and assembled developer
  loops for polygon/document, runtime/QDR runtime, and editor metadata checks while preserving the
  aggregate `quader_modeling_domain_tests` CTest target.
- Added a guardrail so the tool keeps printing the full baseline, affected workflow, filtered
  GoogleTest commands, and before -> after comparison support.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\affected_check.ps1 -NoUntracked -ChangedPath src/render/overlay/overlay_renderable_builder.cpp -MeasureFull`
- Render-overlay sample timing: full baseline 51.36s -> affected workflow 4.22s, 47.14s saved.
- Tooling-only sample after the guardrail split and filter tightening: full baseline 54.03s ->
  affected workflow 2.43s, 51.60s saved.
- Runtime modeling test-source sample after loop targets:
  `tests/modeling_domain/operation/qdr_modeling_runtime_operation_command_tests.cpp`, full baseline
  52.13s -> affected workflow 3.24s, 48.89s saved.
- Polygon operation source sample after operation-family filters:
  `src/mesh/polygon/operations/bevel_edges_operation.cpp`, full baseline 46.86s -> affected
  workflow 3.08s, 43.78s saved.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\affected_check.ps1 -NoUntracked -ChangedPath tools/affected_check.ps1,agents/testing-build.md,tests/build_guardrail_tests.cpp`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Target-Local Standard-Library PCH

Task/context: manual in-app prototype builds still paid repeated C++ standard-library parse cost in
large runtime/modeling targets after affected checks selected the right target.

Invariant:

- `QUADER_ENABLE_PCH` owns the opt-out switch in `cmake/QuaderBuildOptions.cmake`.
- Common PCH is private and target-local. Do not publish PCH through target interfaces.
- Keep the common PCH list to stable C++ standard-library headers. Do not add project owner
  headers, Filament/SDL SDK headers, or GoogleTest without a separate measured change.

Affected owners/files:

- `CMakeLists.txt`
- `cmake/QuaderBuildOptions.cmake`
- `cmake/QuaderModeling.cmake`
- `agents/testing-build.md`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added `quader_enable_common_pch(target)` and applied it to app/runtime/modeling libraries, app
  tests, guardrail tests, and modeling-domain test object libraries.
- Kept PCH behind `QUADER_ENABLE_PCH=ON` so a developer can disable it for local diagnosis.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader`
- Runtime app incremental sample after PCH warm-up: touching
  `src/render/viewport_renderer.cpp`, `quader` build 7.87s -> 5.72s, 2.15s saved.

## 2026-06-27 - Optional Ninja Multi-Config Prototype Build

Task/context: after target-local PCH, Visual Studio/MSBuild still spent avoidable time in warm
incremental app rebuilds used for manual in-app prototyping.

Invariant:

- Keep `vs2026` and `vs2026-debug` as the default expected presets.
- Optional Ninja/MSVC acceleration must use a single contained tree under `build/ninja-msvc`.
- Do not hardcode local Visual Studio or compiler paths in presets. Ninja/MSVC runs from a Visual
  Studio Developer PowerShell/CMD, or an equivalent environment where `cl.exe` is already on `PATH`.
- `tools/affected_check.ps1` derives focused test executable paths from the selected configure
  preset `binaryDir`; it must not assume `build/vs2026`.

Affected owners/files:

- `CMakePresets.json`
- `tools/affected_check.ps1`
- `agents/testing-build.md`
- `cmake/AGENTS.md`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added optional `ninja-msvc`, `ninja-msvc-debug`, and `ninja-msvc-release` presets.
- Updated affected-check preset resolution so passing `-BuildPreset ninja-msvc-debug` also derives
  `ninja-msvc` and `Debug` when those parameters are not supplied explicitly.
- Added guardrails for Ninja preset presence, no local compiler paths, and no hardcoded focused
  test executable dir.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.BuildOptionsAndDependencyPinsHaveSingleCMakeOwner:BuildGuardrailTests.FormerSdkBoundaryIsAbsorbedIntoAppBuild:BuildGuardrailTests.LocalMachinePathsUseEnvironmentBackedDefaults:BuildGuardrailTests.AffectedCheckToolPrintsBeforeAfterAndUsesFilteredGoogleTestRuns`
- Through `VsDevCmd.bat`: `cmake --preset ninja-msvc`
- Through `VsDevCmd.bat`: `cmake --build --preset ninja-msvc-debug --target quader_guardrail_tests`
- Through `VsDevCmd.bat`: `build\ninja-msvc\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FormerSdkBoundaryIsAbsorbedIntoAppBuild:BuildGuardrailTests.LocalMachinePathsUseEnvironmentBackedDefaults:BuildGuardrailTests.AffectedCheckToolPrintsBeforeAfterAndUsesFilteredGoogleTestRuns`
- Runtime app warm incremental sample after the Ninja tree was populated: touching
  `src/render/viewport_renderer.cpp`, `vs2026-debug` `quader` build 7.90s ->
  `ninja-msvc-debug` `quader` build 2.98s, 4.92s saved. The first-time Ninja tree build is not
  included in this warm prototyping comparison.

## 2026-06-29 - ImGui Shell Interaction And Startup Fill Regressions

Task/context: native ImGui shell polish exposed regressions where dragging the sidebar dock splitter
also started viewport box selection, a top-menu popup swallowed the first click on panel dropdowns,
and restoring a wider saved window could show a transparent right strip during boot.

Invariant:

- Viewport pane hover/input must respect active ImGui items. Do not use
  `ImGuiHoveredFlags_AllowWhenBlockedByActiveItem` for normal viewport pane input hit-testing, or
  dock splitters/panel drags can leak into viewport selection.
- Main-menu popups should close for pass-through panel clicks before panels draw, while preserving
  clicks inside the menu popup itself.
- App startup must create the SDL window at the saved normal placement size before applying the full
  placement, so the first rendered frame covers the restored window bounds instead of defaulting to
  1600 px.

Affected owners/files:

- `src/ui/viewport_panel.cpp`
- `src/ui/main_menu_bar.*`
- `src/ui/ui_system.*`
- `src/app/app.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Switched viewport pane hover collection to plain `ImGui::IsItemHovered()`.
- Tracked whether main-menu popups are open and call `ImGui::ClosePopupsExceptModals()` on a
  click-through target outside the menu bar and outside existing popup rects before panel widgets
  are drawn.
- Reused saved window placement to choose the initial `PlatformWindow::create` size, then applied
  the recovered placement as before.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_filter=UiLayoutGuardrailTests.*:WindowGuardrailTests.WindowPlacementPersistenceAndResizeRecoveryStayOwned`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-29 - Native Window Shell, Layout Restore, And Stable Entity Identity

Task/context: native standalone app needed Windows 11 borderless maximize fixes, persisted window
placement, restored multi-pane viewport layouts, UI chrome/sidebar polish, RMB context selection
preservation, literal default group naming, stable entity UUIDs, and removal of the retired retained
UI stack references.

Invariant:

- Borderless maximize is owned by `PlatformWindow` on Windows. Keep `WM_GETMINMAXINFO` clamped to
  the monitor work area and `WM_NCCALCSIZE` constrained for zoomed client bounds so the app does not
  cover the Windows taskbar.
- Persisted window placement is owned by `AppWindowSettings`; recover saved normal bounds to a
  visible display work area before applying them.
- The native editor uses the ImGui shell only. Do not reintroduce generated UI controller/resource
  paths or stale retained-UI documentation.
- Viewport layout restoration belongs to `ViewportWorkspaceService` with the standalone app using
  `ViewportWorkspaceLayoutPolicy::MultiPane`.
- RMB context picking with a valid component selection must preserve the existing component
  selection when the hit is on another unselected component, then open the context menu against that
  preserved selection.
- New groups default to the literal name `Group`.
- Every scene entity has a stable UUID component; QDR I/O and public snapshots preserve and expose
  it, while duplicated entities receive distinct UUIDs and undo/redo restores created identities.

Affected owners/files:

- `src/platform/platform_window.*`
- `src/app/app_window_settings.*`
- `src/core/user_config_paths.*`
- `src/ui/ui_system.*`, `src/ui/main_menu_bar.*`, `src/ui/viewport_panel.cpp`,
  `src/ui/inspector_panel.*`, `src/ui/outliner_panel.*`
- `src/ui/viewport_selection_controller.*`, `src/ui/viewport_shortcut_controller.cpp`
- `src/viewport/viewport_workspace_service.hpp`, `src/api/quader_viewport_api.cpp`
- `src/scene/scene_document.*`, `src/scene/components.hpp`, `src/io/qdr/qdr_document_*`
- `src/editor/commands/document/*`
- UI/docs guardrails and AGENTS/docs files.

Resolved approach:

- Matched Electron's Windows frameless-window lesson locally: work-area max bounds plus client-area
  insets/constraints for maximized borderless windows, without relying on one-time resize hacks.
- Added placement persistence under the user config directory.
- Restored multi-pane workspace policy for the standalone app.
- Made the custom chrome 35 px, moved the menu beside the logo, removed the old menu strip, kept a
  static bottom bar for maximize verification, and tightened sidebar/dropdown/checkbox sizing and
  colors.
- Added `stduuid` through vcpkg/CMake/license manifest and stored UUIDs on scene entities and QDR.
- Replaced stale docs with current native ImGui/app guidance and removed explicit retired UI stack
  references.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=UISettingsTests.*:AppWindowSettingsTests.*:PlatformWindowChromeTests.*:SceneDocumentTests.CreatesStableEntitiesAndComponents:DocumentCommandTests.TypedLifecycleCommandsRestoreSceneSnapshots:DocumentCommandTests.DuplicateMintsEditorObjectIdsOnceAndRedoReusesThem:DocumentCommandTests.DestroyUndoRestoresEditorObjectIdsForSubtrees:DocumentCommandServiceTests.DefaultGroupNameStaysLiteralGroup:DocumentCommandServiceTests.CoversEntityHierarchySelectionAndLifecycleCommands:QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings:EditorApiTests.ViewportHandlesUseWorkspaceWithoutRuntimeHooks:EditorApiTests.ViewportSelectionModeShortcutsApplyThroughPublicSelectionApi:EditorApiTests.RightClickPickWithExistingFaceSelectionPreservesComponentSelectionBeforeContextDelete:ModelingSessionServiceTests.RmbMeshPickWithExistingFaceSelectionPreservesSelectionAndAllowsContextMenu`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ProjectOwnedTextDoesNotReferenceOldCppPathNames:BuildGuardrailTests.SourceBuildAndTestsDoNotUseOldSdkIncludeOrPackagePaths:BuildGuardrailTests.TestingDocsExplainPlacementAndSelectionRules:BuildGuardrailTests.LocalizationResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:BuildGuardrailTests.ImGuiViewportApiKeepsMultiPaneLayoutWorkspace:BuildGuardrailTests.RuntimeUiUsesNativeImGuiStackOnly:WindowGuardrailTests.BorderlessWindowModeIsDefault:WindowGuardrailTests.BorderlessMaximizeUsesMonitorWorkArea:WindowGuardrailTests.WindowPlacementPersistenceAndResizeRecoveryStayOwned:DependencyLicenseGuardrailTests.*:ProgrammaticApiGuardrailTests.PublicBehaviorTestsUseApiOrScenarioDsl:ProgrammaticApiGuardrailTests.PublicDocsExamplesManifestClassifiesEveryDocsAndExamplesFile:SelectionModeAdapterGuardrailTests.*:ViewportToolGuardrailTests.ViewportPanelProvidesPointToolContextToModalTools`
- Explicit retired UI stack string search over source/docs returned no matches outside ignored build
  and third-party/license folders.

## 2026-06-29 - Daily DRY/OOP Drift Audit Completion

Task/context: exhaustive daily DRY/OOP drift audit across API/commands, modeling/projection,
viewport/input/tools, UI/layout/localization, render/assets/scene, QDR I/O, docs/build/tests, with
six parallel audit subagents and main repo-wide inspection.

Invariant:

- `refactor.md` is the current-run source of truth and must not contain unchecked actionable items
  before completion.
- Inspector one-shot transform edits must route through `SceneTransformDragService`; `InspectorPanel`
  may stage UI field values but must not build `QuaderSceneTransformEdit` batches, compute selected
  transform pivots, filter locked/selected-descendant targets, or pin local pivots itself.
- Viewport shading conversion helpers that include viewport owners live under `src/viewport`, not
  public API headers.
- Modeling selection, drawn-plane lifecycle, overlay/projection, scene render extraction, material
  texture mutation, QDR root/header/project codecs, scene-batch reparenting, and docs/build
  guardrail policy each have one owner recorded in `refactor.md`.

Resolved approach:

- Completed all actionable `refactor.md` items A01-A09, U01-U07, M01-M05, R01-R05, and T01-T07.
- Final U07 fix moved inspector-style move/rotate/scale commits into `SceneTransformDragService`,
  including selected-target filtering, selected-bounds pivot resolution, scale ratio/clamp policy,
  parent-aware pivot pinning, and public API batch commit. `InspectorPanel` now only calls the
  transform owner.
- Moved `quaderApiToViewportShadingMode`/`quaderApiFromViewportShadingMode` to
  `src/viewport/viewport_shading_mode_conversions.hpp` after the public API guardrail caught the
  internal viewport include under `src/api`.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug`
- `ctest --preset vs2026-debug`
- `powershell -ExecutionPolicy Bypass -File tools\boot_smoke.ps1 -Exe build\vs2026\Debug\quader.exe -ScreenshotPath build\vs2026\Debug\daily-dry-oop-boot-smoke.png -LogCopyPath build\vs2026\Debug\daily-dry-oop-boot-smoke.log -TimeoutSeconds 30`

## 2026-06-29 - Native Point Light First Implementation

Task/context: first native Filament point light path with movable scene entity, inspector-editable
properties, shadow caster state, and rendered viewport material/shadow visibility.

Invariant:

- `SceneDocument` owns durable light state via `LightComponent`; UI/API must edit lights through
  `QuaderEditorApi` and document commands, not `SceneDocument::setLight` directly.
- Filament light entities and their position/falloff/shadow mapping stay owned by
  `LightRenderableCache`; public API/UI never cache Filament handles.
- Mesh material rendering uses separate compiled materials: `resources/materials/quader_mesh.mat`
  stays unlit for Shaded/tool-preview editor rendering, while
  `resources/materials/quader_mesh_lit.mat` is the lit/PBR material for Rendered mode lights and
  shadows.
- Shaded viewport mode must not depend on scene lighting. The shared Filament scene contains
  separate rendered and shaded mesh proxies; viewport panes select a scene layer instead of mutating
  shared material state.
- Current configured Filament SDK exposes point-light position/falloff and `castShadows`; its public
  docs list shadow casting support for directional/sun/spot/focused spot, so true point-shadow
  output depends on the SDK internals/version while Quader preserves the caster state and passes it
  through.
- Quader stores light intensity as a Godot-style user-facing energy scalar. For point/spot lights,
  `1.0` uses Godot's default `1000 lm` as the base, then applies a `10000x` viewport exposure
  compensation before calling Filament `Builder::intensity(float)`, for `10,000,000` Filament
  lumens. Do not apply `1 / (4*pi)` because Filament converts point-light lumens to candela
  internally. Directional `1.0` maps to Godot's default `100000 lx` at the same render boundary.

Affected owners/files:

- `src/scene/components.hpp`
- `src/editor/commands/document/*`, `src/editor/commands/document_command_service.*`
- `src/api/quader_api_types.hpp`, `src/api/quader_scene_api.*`, `src/api/quader_commands_api.cpp`,
  `src/api/quader_query_api.cpp`
- `src/render/light_renderable_cache.cpp`, `src/render/mesh_renderable_factory.cpp`,
  `src/render/mesh_renderable_cache.*`, `src/render/material_binding_service.*`,
  `src/render/filament_material_library.*`, `src/render/viewport_renderer.cpp`,
  `src/render/viewport_render_pass_coordinator.*`, `src/render/render_signature_builder.cpp`
- `src/ui/inspector_panel.cpp`, `src/ui/main_menu_bar.*`, `src/ui/viewport_panel.cpp`,
  `resources/locales/*.lang`
- `src/io/qdr/qdr_document_reader.cpp`, `src/io/qdr/qdr_document_writer.cpp`

Resolved approach:

- Added point-light creation and light-component edit commands with undo/redo and optional
  select-created behavior.
- Exposed `QuaderLightComponent`, `QuaderCreatePointLightArgs`, `scene.createPointLight`,
  `scene.setLightComponent`, light snapshots, and typed command `scene.create_point_light`.
- Added light range/falloff persistence in scene, render signatures, public snapshots, and QDR.
- Updated `LightRenderableCache` to set Filament point/spot position from entity world transform and
  range through `falloff`, while keeping rebuild/transform signatures separate.
- Added a separate lit mesh material, bound roughness/metallic only for Rendered proxies, enabled
  Rendered mesh shadow receiving, and made viewport shadowing explicit.
- Added dual mesh proxy layers so Shaded mode renders textured material color through a true unlit
  material while Rendered mode uses the lit/shadowing material.
- Added inspector controls for light type, color, intensity, range, and cast shadows through the
  public scene API.
- Added `LightRenderableCache::filamentIntensity` as the single Godot-style intensity to Filament
  lumen/lux conversion point, including the punctual-light viewport exposure compensation.
- Added the main-menu `Create > Point Light` entry, routed through
  `QuaderCommand::SceneCreatePointLight`.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/affected_check.ps1 -PlanOnly -ChangedPath src/render/light_renderable_cache.cpp,src/ui/inspector_panel.cpp,src/api/quader_scene_api.cpp,resources/materials/quader_mesh.mat,tests/editor_api_tests.cpp`
- `cmake --build --preset vs2026-debug --target quader quader_guardrail_tests quader_tests quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime:BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:ProgrammaticApiGuardrailTests.*:UiLayoutGuardrailTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=Console*:EditorApiTests.*:GroundGridMaterialTests.*:GroupingPublicApiTests.*:LightRenderableCacheTests.*:Localization*:ModelingSessionService*Hover*:ModelingSessionService*Overlay*:ModelingSessionService*Pick*:NavigationGizmo*:Overlay*:PlatformWindow*:ProgrammaticApiTests.*:ProgrammaticBehaviorScenarioTests.*:QuaderMeshMaterialTests.*:Render*:RenderExtractionTests.*:SceneRenderBridge*:Sdl*:TextureRegistryTests.*:UI*:Viewport*:Window*`
- `ctest --preset vs2026-debug --output-on-failure`
- Follow-up shaded/menu fix:
  `cmake --build --preset vs2026-debug --target quader quader_guardrail_tests quader_tests quader_modeling_domain_tests`
- Follow-up shaded/menu fix:
  `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.*`
- Follow-up shaded/menu fix:
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QuaderMeshMaterialTests.*:SceneRenderBridgeTests.*:ViewportRenderPassCoordinatorTests.*:ViewportRendererTests.*:EditorApiTests.ScenePointLightApiCreatesSnapshotsAndEditsThroughHistory:EditorApiTests.SceneCreatePointLightCommandCreatesSelectedPointLight:QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBinaryAssetLinks:DocumentCommandServiceTests.CreatePointLightAndSetLightComponentAreUndoable`
- Follow-up shaded/menu fix: `ctest --preset vs2026-debug --output-on-failure`
- Follow-up shaded runtime fix:
  `cmake --preset vs2026`
- Follow-up shaded runtime fix:
  `cmake --build --preset vs2026-debug --target quader quader_guardrail_tests quader_tests quader_modeling_domain_tests`
- Follow-up shaded runtime fix:
  `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:UiLayoutGuardrailTests.MainMenuDropdownsUsePopupPaddingAndVerticalSpacing`
- Follow-up shaded runtime fix:
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QuaderMeshMaterialTests.*:SceneRenderBridgeTests.*:ViewportRenderPassCoordinatorTests.*:ViewportRendererTests.*:EditorApiTests.SceneCreatePointLightCommandCreatesSelectedPointLight`
- Follow-up shaded runtime fix: `ctest --preset vs2026-debug --output-on-failure`
- Follow-up shaded runtime fix:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\boot_smoke.ps1 -Exe build\vs2026\Debug\quader.exe -ScreenshotPath build\vs2026\Debug\shaded-rendered-material-split-boot-smoke.png -LogCopyPath build\vs2026\Debug\shaded-rendered-material-split-boot-smoke.log -TimeoutSeconds 30 -AllowMissingLog`
- Follow-up light intensity unit fix:
  `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- Follow-up light intensity unit fix:
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=LightRenderableCacheTests.*:RenderExtractionTests.*:EditorApiTests.ScenePointLightApiCreatesSnapshotsAndEditsThroughHistory:EditorApiTests.SceneCreatePointLightCommandCreatesSelectedPointLight:QdrDocumentIoTests.*`
- Follow-up light intensity unit fix:
  `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.InspectorLightEditsRouteThroughPublicSceneApi:UiLayoutGuardrailTests.MainMenuDropdownsUsePopupPaddingAndVerticalSpacing`
- Follow-up light intensity unit fix:
  `cmake --build --preset vs2026-debug --target quader`
- Follow-up light intensity unit fix:
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=DocumentCommandServiceTests.CreatePointLightAndSetLightComponentAreUndoable:LightRenderableCacheTests.*:EditorApiTests.ScenePointLightApiCreatesSnapshotsAndEditsThroughHistory:EditorApiTests.SceneCreatePointLightCommandCreatesSelectedPointLight`

## Real Scene Root, Scene Settings, IBL, And Directional Lights

Invariant:

- New projects and migrated QDR documents must contain exactly one real scene-root entity named
  `scene`, marked by `SceneSettingsComponent`. The root is selectable and transformable, cannot be
  destroyed/reparented/renamed by editor commands, and public API snapshots expose it with
  `isSceneRoot`.
- Public editor create paths parent new scene entities, modeling objects, point lights, and
  directional lights under the root when callers do not pass an explicit parent. Low-level
  `SceneDocument` creation stays owner-level and does not silently consult `ProjectDocument`.
- Root-level user entities are children of `scene`, so behavior-test `noParent()` means no
  user-authored parent, not `InvalidEntityId`.
- Scene-wide render settings live on the real root and are edited through the public scene API and
  undoable document commands. Rendered mode applies IBL, skybox background, camera exposure, and
  post-process settings per viewport; Shaded mode remains the editor's unlit material view.
- Directional lights use the same light component/API/persistence path as point lights and are
  represented in selected viewport overlays by a marker plus direction line.

Affected owners/files:

- Scene/data ownership: `src/scene/components.hpp`, `src/scene/scene_document.*`,
  `src/scene/document/project_document.*`, `src/scene/document/project_session.cpp`
- Command/API ownership: `src/editor/commands/document_command_service.*`,
  `src/editor/commands/document/set_scene_settings_command.hpp`, `src/api/quader_api_types.hpp`,
  `src/api/quader_scene_api.*`, `src/api/quader_commands_api.cpp`,
  `src/api/quader_query_api.cpp`
- Render ownership: `src/render/filament_environment_library.*`, `src/render/render_system.*`,
  `src/render/viewport_renderer.*`, `src/render/overlay/viewport_scene_overlay_builder.cpp`
- UI/persistence/resources: `src/ui/inspector_panel.cpp`, `src/ui/main_menu_bar.*`,
  `src/ui/outliner_panel.cpp`, `src/ui/viewport_panel.cpp`, `src/io/qdr/qdr_document_reader.cpp`,
  `src/io/qdr/qdr_document_writer.cpp`, `resources/ibl/*`, `resources/locales/*.lang`

Resolved approach:

- Added `SceneSettingsComponent` as the root marker plus Environment, Camera, and Post Process
  settings structs. `ProjectDocument::ensureSceneRoot()` creates/migrates the real root and
  reparents existing top-level entities during project/session/QDR hydration.
- Added undoable scene-settings command and inspector sections on the selected root for
  Environment, Camera, and Post Process.
- Added Filament environment library using bundled KTX IBL/skybox assets from the Filament material
  demo asset set, copied under runtime `ibl/`, and wired it to Rendered-mode viewports.
- Added public directional-light create args/API/command/menu item/localization and selected
  directional-light overlay representation.
- Updated QDR read/write so scene-root markers and settings persist, while legacy payloads migrate
  into a document with the new root.
- Updated tests and behavior helpers so API snapshots include the root and root-level entities
  parent under `scene`.

Verification:

- `cmake --build --preset vs2026-debug`
- `ctest --preset vs2026-debug`

## 2026-06-29 - Default Rendered Material Roughness And Lighting Notes

Task/context: Rendered viewport material looked like glass and an outward-normal, single-sided open
box still appeared lit inside.

Root cause/invariants:

- `resources/modeling_assets/materials/default/default_roughness.png` is an engine-neutral packed
  material asset whose red channel is black while useful default gloss/smoothness data lives outside
  the Filament roughness-red convention. Binding it directly to `quader_mesh_lit.mat`, which samples
  `roughnessTexture.r`, forces the default material to near-mirror roughness even though
  `MaterialDocument::roughness` defaults to `1.0`.
- The startup default Quader material should bind only the default albedo texture and rely on the
  render fallback white roughness texture plus scalar roughness `1.0`. Do not rewrite QDR material
  loading or saved material texture slots, because explicit persisted roughness data must remain
  authored data.
- Mesh material culling follows the authored `MeshComponent::doubleSided` state through a single
  Filament material culling mode. Do not reintroduce separate shadow-only no-culling unless there is
  a dedicated, tested engine-export reason for it.
- Filament 1.71 SDK documentation does not promise point-light shadow maps; direct shadow
  validation should use directional/sun/spot lights unless Quader adds a separate point-shadow
  strategy. IBL/environment lighting is not shadowed by mesh shadow maps, so enclosed interiors can
  still receive environment light unless environment intensity/AO is adjusted.

Affected owners/files:

- `src/app/app.cpp`
- `src/render/material_binding_service.cpp`
- `tests/core_tests.cpp`

Resolved approach:

- Startup still registers the default roughness texture asset for metadata/package consistency but
  no longer binds it to the startup default material. The default material binds the albedo texture
  only, leaving rendered roughness to the white fallback and scalar factor.
- `MaterialBindingService` keeps the previous single culling-mode behavior for mesh material
  instances.
- Added guardrail assertions that startup default material creation does not bind the packed
  roughness texture and that material instances avoid the reverted separate color/shadow culling.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QuaderMeshMaterialTests.*:SceneRenderBridgeTests.*:ViewportRendererTests.*:LightRenderableCacheTests.*:RenderExtractionTests.*:RenderSystemTests.*:RenderLifecycleTests.*:RenderProxyLifecycleTests.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime:WindowGuardrailTests.*`
- `cmake --build --preset vs2026-debug --target quader` was blocked by a running
  `quader.exe` holding the output executable open.

## 2026-06-29 - Viewport Overlays Are Post-Process-Free Editor Passes

Task/context: Rendered mode post-processing and FXAA degraded transform gizmos and editor overlays.

Invariants:

- Gizmos, selection wires/fills, source wires, light markers, marquee overlays, and the editor grid
  are editor presentation, not scene content. Scene lights, shadows, bloom, tone mapping, dithering,
  FXAA, and IBL intensity must not affect them.
- Viewport panes use a shared Filament scene, so per-pane overlays stay on the same per-pane
  editor/grid layer mask. Do not move them to the single global overlay layer unless a separate
  multi-viewport isolation strategy is added.
- Filament visibility layers are an 8-bit public/internal contract (`View::setVisibleLayers`,
  `RenderableManager::Builder::layerMask`, renderable/scene SoA `LAYERS`, skybox masks, and shadow
  visibility paths all use `uint8_t`). Do not fork Filament just to expose 32 editor/category bits;
  future overlay categories should use Quader-owned pass/scene/policy structure instead of more
  Filament layer bits.
- `ViewportRenderPassCoordinator` owns pass ordering and visible layer masks. Scene view renders
  scene/tool-preview layers first with the background clear. Overlay view renders the per-pane
  editor layer second with `clear=false`, then restores the renderer clear state. Filament clears
  depth/stencil at the start of every `Renderer::render(view)` call, so the overlay view cannot
  inherit scene depth through `Renderer::ClearOptions`. The overlay pass must instead receive
  invisible depth-only scene mesh proxies before drawing grid/editor geometry; otherwise the grid
  draws over opaque scene meshes and makes materials look transparent.
- `ViewportRenderer` owns the two Filament views. Scene settings are applied only to the scene view;
  the overlay view must keep post-processing, anti-aliasing, dithering, color grading, and shadowing
  disabled, and it must use translucent view blending when composed over the scene view.

Affected owners/files:

- `src/render/viewport_renderer.*`
- `src/render/viewport_render_pass_coordinator.*`
- `tests/core_tests.cpp`
- `tests/render_extraction_tests.cpp`

Resolved approach:

- Added a second Filament `View` per viewport for overlays, sharing the same scene, camera, viewport
  size, and render target as the scene view.
- Split scene and overlay visible layer masks. The scene view sees only rendered/shaded scene layers
  plus tool preview; the overlay view sees the pane's editor/grid layer when the grid or overlays are
  visible.
- Rendered scene pass runs first with normal clear/post-process. Overlay pass runs second with no
  clear, translucent view blending, and no post-process/shadowing, then restores renderer clear
  options. `MeshRenderableCache` creates overlay depth-only mesh proxies on the existing per-pane
  editor layer bits and early render channel 0; those proxies write depth with color writes disabled,
  receive no shadows, and cast no shadows. The grid remains in this editor pass so it is not lit or
  post-processed, but it depth-tests against reconstructed scene depth.
- Reverted the experimental separate color/shadow material culling behavior; mesh material culling
  again follows a single Filament material culling mode.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests quader`
  rebuilt runtime/tests; the final `quader.exe` link was blocked by a running user-launched
  `quader.exe` instance holding the output executable open.
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QuaderMeshMaterialTests.*:SceneRenderBridgeTests.*:ViewportRendererTests.*:ViewportRenderPassCoordinatorTests.*:OverlayRenderableCacheTests.*:RenderExtractionTests.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime:WindowGuardrailTests.*:UiLayoutGuardrailTests.*`

## 2026-06-30 - Viewport Overlays Use Separate Display-Space Composite

Task/context: Rendered mode camera exposure and combined viewport texture presentation still made
editor overlays look dark/noisy. The first failure was reusing the scene Filament camera for the
overlay view; the remaining failure was compositing transparent overlays into the scene texture
before the UI presentation pass.

Invariants:

- Filament camera exposure is scene render state. Editor overlay colors must remain stable across
  scene camera aperture, shutter speed, and sensitivity changes.
- The overlay camera is not a separate user camera. It must mirror the scene camera look-at and
  projection every frame, then keep neutral exposure for editor-space compositing.
- Texture-backed viewports must keep scene color and editor overlay color in separate render targets.
  The scene target is sampled as linear sRGB by the UI. The overlay target is transparent,
  display-sRGB, premultiplied-alpha editor presentation sampled over the scene image.
- When overlays are hidden, the separate overlay target must still be cleared transparent so stale
  editor pixels cannot remain from a previous frame.

Affected owners/files:

- `src/render/viewport_renderer.*`
- `src/render/viewport_render_target.*`
- `src/render/viewport_render_pass_coordinator.*`
- `src/render/texture_registry.*`
- `src/render/imgui_filament_renderer.cpp`
- `resources/materials/imgui.mat`
- `src/ui/viewport_panel.cpp`
- `tests/core_tests.cpp`

Resolved approach:

- `ViewportRenderer` owns a second Filament camera for the overlay view. The overlay camera shares
  the scene camera transform/projection but resets to neutral exposure on initialization, camera
  updates, and scene-setting application.
- `ViewportRenderer` owns a second `ViewportRenderTarget` for overlays. The scene view renders into
  the linear viewport texture; the overlay view renders into a transparent premultiplied display
  texture. `ViewportPanel` draws the scene texture first, then the overlay texture with ImGui.
- `TextureRegistry` and the ImGui material distinguish straight-alpha display/linear textures from
  premultiplied display textures so the overlay target is not premultiplied a second time.
- Added a source guardrail test so future changes cannot route the overlay view back through the
  scene camera, scene exposure path, or combined viewport texture path.

## 2026-06-29 - Selection Locks, Scene Delete, And Environment Clear Color

Task/context: Editor selection, outliner delete, inspector defaults, and viewport background behavior
regressed across scene entities, lock state, and environment settings.

Invariants:

- `DeleteSelection` must route any selected non-scene-root root scene entity through the scene-aware
  document command path. Lights are scene entities, so keyboard delete, context delete, and outliner
  delete must not depend on modeling object/group predicates.
- User/editor selection filters must reject missing entities, effectively locked entities, and
  entities with `VisibilityComponent::selectable == false`. Viewport picking keeps the existing
  effective-visible filter in addition to this shared selectability rule.
- Locking an entity removes that entity and effectively locked descendants from the current
  selection inside the same undoable command or batch. Undo restores the previous selection; normal
  unlock does not auto-select anything.
- `SceneEnvironmentSettings::clearColor` owns the wireframe/shaded clear color. Rendered mode clears
  black, and skybox background attachment requires environment enabled, `showBackground == true`,
  and `backgroundIntensity > 0.0f`. IBL lighting intensity remains independent of background
  intensity.
- Outliner non-leaf nodes start expanded on first presentation; inspector collapsing sections start
  collapsed unless a later product decision explicitly changes that default.

Affected owners/files:

- `src/editor/commands/modeling/scene_aware_delete_selection_command.*`
- `src/editor/modeling/services/modeling_session_service.*`
- `src/editor/selection/editor_selection_service.cpp`
- `src/editor/commands/document_command_service.cpp`
- `src/api/quader_scene_api.cpp`
- `src/api/quader_api_mutation_runner.cpp`
- `src/scene/components.hpp`
- `src/io/qdr/qdr_document_reader.cpp`
- `src/io/qdr/qdr_document_writer.cpp`
- `src/render/viewport_renderer.cpp`
- `src/render/filament_environment_library.cpp`
- `src/viewport/viewport_settings.*`
- `src/ui/inspector_panel.cpp`
- `src/ui/outliner_panel.cpp`

Resolved approach:

- Replaced group/object-only scene-aware delete checks with
  `documentCommandSelectedRootEntities(...)` and exposed that through `ModelingSessionService` for
  command execution, availability, viewport context menus, and outliner delete.
- Centralized user-selectable scene filtering in `EditorSelectionService` and projected scene
  selection back into modeling selection after lock and undo/redo paths that mutate scene selection.
- Appended selection cleanup commands after lock edits in `DocumentCommandService`, including batched
  scene lock edits, so lock selection cleanup is part of the same undoable transaction.
- Added `environment.clearColor` to internal/public scene settings, API conversions, QDR
  serialization, inspector editing, and render clear behavior; removed the old viewport
  `BackgroundColor` setting.
- Updated UI guardrails so inspector sections remain collapsed by default and outliner branch nodes
  default open.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\affected_check.ps1 -NoUntracked`

## 2026-06-30 - Entity Selection Owns Scene Transform And Duplicate Paths

Task/context: Selecting a light after mesh component selection could leave stale modeling runtime
selection active, so immediate transforms and duplicate/delete command routing could operate on the
old mesh instead of the selected scene entity.

Invariants:

- Object/Mesh selection is editor entity selection: selected meshes, lights, groups, empties, and
  future scene entities are canonical `SceneDocument` `EntityId`s in `EditorSelectionService`.
- Modeling runtime object selection is only a projection/cache for editable mesh workflows.
  Selecting any scene entity through editor selection forces Object/entity mode; component modes are
  polygon-only.
- Entity-mode transforms route through scene/document transform commands and
  `SceneTransformDragService`. Runtime polygon transforms stay only for component modes.
- Public `DuplicateSelection` duplicates selected scene entity hierarchies. The mesh-only
  `duplicate_meshes` action remains available as the generated modeling command id.

Affected owners/files:

- `src/editor/selection/editor_selection_service.cpp`
- `src/editor/selection/selection_projection_service.cpp`
- `src/editor/modeling/services/modeling_session_service.*`
- `src/editor/modeling/services/modeling_hierarchy_duplicate_service.*`
- `src/editor/transform/scene_transform_drag_service.*`
- `src/api/quader_modeling_api.*`
- `src/api/quader_commands_api.cpp`
- `src/api/quader_api_mutation_runner.cpp`

Resolved approach:

- Forced editor scene selection to project as Object/entity mode and clear stale runtime selection
  when the selected scene entities do not map to modeling objects.
- Routed immediate entity-mode `moveSelection` and `transformSelection` through document transform
  edits, while preserving runtime modeling transforms for component modes.
- Added public entity-hierarchy duplicate routing behind `edit.duplicate` and kept mesh duplication
  on `modeling.duplicate_meshes`, wrapping public duplicate in an editor transaction so undo/redo is
  one coherent history step.
- Reprojected runtime selection after undo/redo and light creation when scene selection changes to
  non-modeling entities.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no`

## 2026-06-30 - Undo/Redo And Entity Hierarchy Architecture Pass

Task/context: Six-agent architecture pass on history, entity handling, public command routing, OOP
boundaries, stale code, and DRY cleanup found that redo availability, scripts, duplicate routing,
scene-aware delete predicates, batch lock planning, and repeated QDR/cut/component helpers could
drift across owners.

Invariants:

- `QuaderApiMutationRunner` owns public undo/redo availability. Redo is unavailable while an active
  modeling preview can be canceled by undo, and history availability stays false while an editor
  transaction is active.
- `QuaderScript` records through `EditorTransaction`; history steps are rejected inside a running
  script, and a changed script records the final modeling memento so redo restores component
  selection as well as document edits.
- Public `edit.duplicate` is the scene-aware duplicate command. Mesh-only duplication stays on
  `modeling.duplicate_meshes`; UI/host shortcuts must target `DuplicateSelection`.
- Document hierarchy redo must reuse mesh clone entities already restored by modeling mementos
  instead of recreating them.
- Scene-aware delete route checks, QDR tone-mapping names, plane-cut request conversion, and
  component-mode predicates each have one owner helper.
- Scene batch transform validation plans against simulated parent/lock state, not only live
  `SceneDocument` state before the batch.

Affected owners/files:

- `src/api/quader_api_mutation_runner.*`
- `src/api/quader_script.cpp`
- `src/api/quader_command_ids.hpp`
- `src/api/quader_modeling_api.cpp`
- `src/editor/history/editor_history_service.*`
- `src/editor/commands/document_command_service.cpp`
- `src/editor/commands/document/duplicate_selected_hierarchy_command.hpp`
- `src/editor/commands/modeling/scene_aware_delete_selection_command.*`
- `src/editor/modeling/services/modeling_hierarchy_duplicate_service.*`
- `src/editor/modeling/runtime/session_operations.cpp`
- `src/editor/modeling/services/selection_mode_adapter.hpp`
- `src/editor/selection/editor_selection_service.cpp`
- `src/io/qdr/qdr_project_codecs.*`
- `src/ui/outliner_panel.cpp`
- `src/ui/viewport_panel.*`
- `src/viewport/host_viewport_contract_adapter.cpp`
- `tools/affected_check.ps1`

Resolved approach:

- Added transaction-safe redo checks and script memento recording while keeping editor transactions
  as the source of truth for grouped history.
- Routed outliner and host shortcut duplicate actions through `QuaderCommand::DuplicateSelection`.
- Kept direct document duplication from using selected modeling roots, while preserving document
  group hierarchy duplication and clone reuse for mixed mesh/light redo.
- Centralized repeated delete, QDR, cut, and component-mode helpers and removed stale viewport
  pass-through APIs plus the obsolete cut policy header.
- Added guardrails and behavior coverage for redo preview blocking, script history rejection,
  mixed hierarchy duplicate/delete undo/redo, root protection, scene batch simulated locks, typed
  command catalog uniqueness, and affected-check transform routing.

Verification:

- `cmake --build --preset vs2026-debug --target quader_guardrail_tests quader_tests quader_modeling_domain_metadata_loop_tests quader_modeling_domain_polygon_loop_tests quader_modeling_domain_runtime_loop_tests quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=*:BuildGuardrailTests.*:DependencyLicenseGuardrailTests.*:ProgrammaticApiGuardrailTests.*:WindowGuardrailTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=*:Console*:EditorApiTests.*:GroupingPublicApiTests.*:ProgrammaticApiTests.*:Qdr*:Viewport*:Window*`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=*`
- `build\vs2026\Debug\quader_modeling_domain_metadata_loop_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=QdrEditorMetadata*`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=QdrIoDocumentTests.*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=QdrModelingRuntime*:QdrRuntimeIoCompatTests.*`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-30 - Scene Root Remains Selectable For Settings

Task/context: after scene/entity selection filtering was centralized, the outliner scene root could
no longer be selected, so the inspector scene settings were unreachable.

Invariants:

- Direct user selection of the scene root is allowed so the inspector can edit
  `SceneSettingsComponent` through the public scene API.
- Bulk object/group selection commands still exclude the scene root; root is an editor settings
  entity, not a normal selectable transform/delete/duplicate target.
- Public duplicate/delete command roots continue to come from
  `documentCommandSelectedRootEntities(...)`, which filters the scene root.

Affected owners/files:

- `src/editor/selection/editor_selection_service.cpp`
- `src/api/quader_commands_api.cpp`
- `tests/editor_api_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=EditorApiTests.SceneRootSelectionCanEditSettingsButCannotBeDeletedOrDuplicated:EditorApiTests.ObjectSelectAllAndInvertUseSceneEntitiesAndExcludeLockedAndRoot:EditorApiTests.AggregatesDocumentApisAndCommandRouting:EditorApiTests.PublicDuplicateSelectionDuplicatesLightEntityThroughSceneHierarchy`
- `build\vs2026\Debug\quader_tests.exe`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-30 - Minimized Or Occluded Windows Must Not Submit Frames

Task/context: minimizing Quader or covering it with another app could leave the restored window in a
broken internal render state.

Invariants:

- Focus loss is not a render-suspension signal; minimized, hidden, occluded, and zero-pixel window
  states are.
- `PlatformWindow` owns drawable/presentable window state through `canRenderFrames()` and the
  testable `platformWindowCanRenderFrames(...)` helper.
- `App::renderFrame()` must check presentability before UI/GPU frame prep and again after chrome
  actions, because the custom minimize button can make the swapchain non-drawable mid-frame.
- If a frame was begun but the window becomes non-drawable before `ImGui::Render()`, `UISystem`
  must close the ImGui frame with `endFrameWithoutRender()` instead of submitting draw data.
- CPU/power idling belongs in the app loop: active viewport input uses a short wait, normal visible
  idle uses a low-FPS wait, and suspended windows wait longer while waking immediately on SDL events.

Affected owners/files:

- `src/platform/platform_window.*`
- `src/app/app.*`
- `src/ui/ui_system.*`
- `src/ui/viewport_panel.hpp`
- `tests/core_tests.cpp`
- `tests/build_guardrail_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_guardrail_tests quader_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.*:BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:DependencyLicenseGuardrailTests.*:WindowGuardrailTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=AppFrameClockTests.*:AppLoopTests.*:AppStartupTests.*:AppWindowSettingsTests.*:AssetProjectArchitectureTests.*:AssetRegistryTests.*:BakeServiceTests.*:Console*:ConsoleModelTests.*:DocumentCommandServiceTests.*:DocumentCommandTests.*:EditorCommandServiceTests.*:EditorHistoryServiceTests.*:FatalErrorHandlerTests.*:GroundGridMaterialTests.*:Localization*:LoggerTests.*:MaterialDocumentTests.*:MathTypesTests.*:ModelingInteractionTests.*:ModelingSessionServiceOverlayTests.*:ModelingSessionServiceTests.*:NavigationGizmo*:OverlayMaterialTests.*:PlatformWindow*:PlatformWindowChromeTests.*:PolygonOperationRegistryTests.*:ProjectDocumentTests.*:ProjectSessionTests.*:QuaderMeshMaterialTests.*:RenderLifecycleTests.*:RenderProxyLifecycleTests.*:RenderSystemTests.*:SceneBoundsTests.*:SceneDocumentTests.*:SceneEntityNamingTests.*:SceneRenderBridgeTests.*:Sdl*:SdlEventUtilsTests.*:TextureRegistryTests.*:TransformPivotTests.*:Ui*:UISettingsTests.*:UndoRedoManagerTests.*:Viewport*:ViewportCameraAdapterTests.*:ViewportCameraTests.*:ViewportContextMenuPolicyTests.*:ViewportGridPlacementPolicyTests.*:ViewportInputRouterTests.*:ViewportLayoutTests.*:ViewportOverlayPolicyTests.*:ViewportOverlayStyleResolverTests.*:ViewportRendererTests.*:ViewportSettingsTests.*:ViewportWorkspaceServiceTests.*:Window*`
- `cmake --build --preset vs2026-debug --target quader`
- Manual smoke: launched `build\vs2026\Debug\quader.exe --borderless-window`, minimized, restored,
  maximized, restored, and confirmed the process/window stayed alive.

## 2026-06-30 - Project Save/Open Restored Through QDR Owners

Task/context: File menu Save, Save As, and Open needed to be restored with `Ctrl+S`,
`Ctrl+Shift+S`, and `Ctrl+O`.

Invariants:

- UI and shortcuts must execute `QuaderCommand::ProjectOpen`, `ProjectSave`, and `ProjectSaveAs`;
  the File menu does not read or write project files directly.
- `QuaderProjectApi` owns project path state and routes durable persistence through
  `QdrDocumentReader::readProjectDocument(...)` and
  `QdrDocumentWriter::writeProjectDocumentAtomic(...)`.
- `ProjectSession::replaceDocument(...)` replaces the live `ProjectDocument` in place so existing
  public API services keep valid references, clears history, rehydrates modeling runtime state from
  authored polygon mesh assets, and marks the opened document clean.
- Document replacement must advance the loaded `SceneDocument` revision past the previous live scene
  revision. The QDR payload can be correct while renderer/UI revision subscribers miss the replacement
  if an opened document's scene/asset counters collide with the prior document.
- QDR project serialization is scene/entity/component based: import-specific Unity/Godot runtime junk
  belongs in import adapters, while durable placeable scene state belongs in `SceneDocument` components
  and `src/io/qdr` component codecs.
- Empty project paths are supplied by runtime hooks from the native shell. Headless/API callers must
  pass explicit paths or install hooks.

Affected owners/files:

- `src/api/quader_project_api.*`
- `src/scene/scene_document.*`
- `src/scene/document/project_session.*`
- `src/editor/modeling/services/modeling_session_service.*`
- `src/platform/platform_file_dialog.*`
- `src/app/app.cpp`
- `src/ui/main_menu_bar.cpp`
- `resources/locales/*.lang`
- `src/editor/metadata/host_contract.cpp`
- `src/viewport/host_viewport_contract_adapter.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=EditorApiTests.AggregatesDocumentApisAndCommandRouting:EditorApiTests.ProjectDocumentCommandsRouteThroughProjectApi:EditorApiTests.ProjectSaveAsSaveAndOpenUseQdrDocumentIo:EditorApiTests.ProjectCommandsUseRuntimePathHooksForEmptyPaths:EditorApiTests.GlobalCommandKeybindingsResolveWithoutViewportFocus`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=LocalizationResourceTests.*:LocalizationServiceTests.*`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=QdrEditorMetadata*.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_filter=UiLayoutGuardrailTests.MainMenuDropdownsUsePopupPaddingAndVerticalSpacing`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=EditorApiTests.ProjectSaveAsSaveAndOpenUseQdrDocumentIo:ProjectSessionTests.ReplaceDocumentAdvancesSceneRevisionPastPreviousDocument:QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-30 - Wireframe Viewport Mode Uses X-Ray Authored Topology

Task/context: Wireframe viewport mode was showing an unselected mesh as a mostly solid depth-tested
shape, with hidden topology and the grid masked by invisible mesh depth.

Invariants:

- Wireframe viewport mode is a viewport mode, not selected-object wire styling. It must emit
  unselected scene topology on `ViewportOverlayLayer::WireframeXray` so back/hidden edges remain
  visible and are not occluded by mesh depth proxies.
- `WireframeBase` remains the depth-tested selection/base-wire policy layer. Do not reuse it for
  wireframe viewport scene topology.
- Scene wireframe extraction prefers `MeshAsset::authoredPolygonDocument` when present so authored
  quads/ngons display their real topology instead of baked triangle diagonals. Triangle-index
  extraction is only the fallback for meshes without authored polygon data.
- Depth-only mesh proxies belong to the shared `kViewportOverlayLayerMask` overlay-depth layer, and
  `ViewportRenderPassCoordinator` must include that layer only while the shaded/rendered scene is
  visible. Wireframe mode hides the scene and must exclude the depth-proxy layer so grid and x-ray
  wires are not masked by invisible mesh depth.

Affected owners/files:

- `src/render/viewport_overlay_frame.hpp`
- `src/render/viewport_overlay_policy.*`
- `src/render/overlay/viewport_scene_overlay_builder.cpp`
- `src/render/overlay/viewport_overlay_style_resolver.cpp`
- `src/render/viewport_renderer.cpp`
- `tests/render_extraction_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportSceneOverlayBuilderTests.*:ViewportOverlayPolicyTests.*:ViewportRendererTests.MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame:ViewportRendererTests.PresentsSceneAndOverlayViewportTexturesThroughUiEncoder:ViewportRendererTests.OverlayPassUsesNeutralExposureCamera`

## 2026-06-30 - Overlay Cache Camera Changes Must Not Rebuild World-Space Face Fills

Task/context: `ten.tracy` showed that after selecting hundreds of objects, navigation stayed slow
even after ImGui frame-resource destruction was fixed. The steady-state cost was dominated by
viewport rendering plus overlay submission. `OverlayRenderableCache::rebuildSlot` ran on camera-only
changes, rebuilt every overlay layer, and repeatedly scanned/rebuilt selected face-fill triangle
layers even though their world-space geometry did not depend on the viewport camera.

Invariants:

- Overlay face-fill triangle layers are world-space render geometry. Camera-only viewport navigation
  must not rebuild their proxies or even rescan their layer signatures.
- Overlay line and point layers are camera-dependent because the builder expands them in screen
  space and source-wire visibility can depend on depth-stamp camera rays.
- `OverlayRenderableCache::rebuildSlot(..., rebuildCameraIndependentLayers)` is the layer-aware
  invalidation boundary. Frame/layer-mask changes rebuild all layers; camera-only changes rebuild
  only `OverlayRenderableBuilder::layerBuildDependsOnCamera(...)` layers.
- `OverlayRenderableBuilder::buildLayer(...)` must skip primitive-kind collectors for layers that
  cannot emit that primitive kind. Large selected-object overlays must not scan all triangles for
  every line/point-only layer.

Affected owners/files:

- `src/render/overlay/overlay_renderable_builder.*`
- `src/render/overlay/overlay_renderable_cache.*`
- `tests/render_extraction_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=OverlayRenderableBuilderTests.*:OverlayRenderableCacheTests.*:OverlayProxyFactoryTests.*:ViewportRenderPassCoordinatorTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportRendererTests.*`

## 2026-07-01 - Native Window Shrink Resize Needs Size-Only Live Repaint

Task/context: During Windows live resize, shrinking the app clipped the old larger UI frame until
mouse-up. A prior timer/move repaint attempt made title dragging lag badly.

Invariants:

- `PlatformWindow` owns Win32 move/size tracking through the subclassed HWND proc. Install the
  message hook for native-window mode too, but apply custom chrome styles only when borderless
  chrome is enabled.
- Live repaint is allowed for committed native client-size changes only: `WM_SIZE` and
  `WM_DPICHANGED`. `WM_SIZING` tracks that the native move/size loop is resizing, but it must not
  present. `WM_WINDOWPOSCHANGED` must not present; it can arrive around non-client and restore/move
  transitions where the drawable is not stable enough for flicker-free GPU presentation.
- Do not repaint continuously for `WM_MOVING`, and do not use a Win32 timer to force frames during
  move/size. That reintroduces drag lag.
- `requestLiveResizeFrame()` must coalesce against the last successfully presented client size, not
  merely the last requested size. If Filament skips `beginFrame()`, the pending size must remain
  eligible for a later live-resize draw instead of being swallowed until mouse-up.
- Live-resize callbacks are success-aware (`bool` return). Size-bearing native messages schedule an
  invalidate-only `WM_PAINT` with erase suppressed; do not use `RDW_UPDATENOW` or directly dispatch
  the render callback from inside `WM_SIZE`/`WM_DPICHANGED`. If Filament declines `beginFrame()`,
  restore the pending size so the repaint remains eligible instead of validating the client area and
  dropping the frame.
- Prime live-resize coalescing with the current client size on `WM_ENTERSIZEMOVE`; that prevents the
  first move-only `WM_WINDOWPOSCHANGED` from repainting while still allowing the first actual size
  change to redraw.
- Do not force Filament `beginFrame()` during live resize. That experiment made resize sizes stick
  unpredictably and introduced drag lag.
- `App::renderFrame()` returns whether a Filament frame was submitted. Only set
  `liveResizeFrameRendered_` after a true return; a skipped `beginFrame()` must not suppress the
  next normal loop frame.
- `App::renderFrame()` snapshots framebuffer width/height once after `refreshPixelSize()` and uses
  that same size for ImGui frame setup, render-data preparation, and `noteFramePresented(...)`;
  mixing separately queried sizes can create clipped UI/scissor state during rapid resize.
- The Win32 subclass must handle `WM_ERASEBKGND` by returning handled for the GPU-backed client
  surface. Letting `DefWindowProc` erase between live-resize frames causes whole-window flicker
  even when Filament is successfully presenting during the drag.
- The main swapchain and ImGui clear pass must use `discard = false`; discarded backbuffers can show
  undefined contents when Windows live resize causes skipped frames.
- During native whole-window live resize, suspend viewport render-target reallocations when stable
  scene/overlay textures already exist. Keep panel splitter drags on the exact resize path and keep
  renderer-capacity UVs so the viewport aspect ratio does not regress.
- The ImGui dockspace must not force both sidebars to the full preferred 340 px width while the
  window is at its 800 px minimum. Compress the per-frame sidebar minimum before the viewport or
  sibling dock node collapses, and route panel minimums through that effective frame style.

Affected owners/files:

- `src/platform/platform_window.*`
- `src/app/app.cpp`
- `src/render/render_system.*`
- `src/ui/ui_system.*`
- `src/ui/viewport_panel.cpp`
- `tests/core_tests.cpp`
- `tests/build_guardrail_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=AppLoopTests.SuspendsGpuSubmissionForNonDrawableWindowsAndIdlesBetweenFrames:PlatformWindowChromeTests.*:OverlayMaterialTests.ImGuiMaterialCanCompositePremultipliedDisplayTextures:ViewportRendererTests.PresentsSceneAndOverlayViewportTexturesThroughUiEncoder`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=WindowGuardrailTests.*:UiLayoutGuardrailTests.ViewportUsesRendererCapacityUvsDuringPanelSplitterDrag`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Pressed Shortcut Chords Must Use SDL Event Modifiers

Task/context: `Ctrl+C` and debug-only `Ctrl+Shift+C` copied selection data but also triggered
viewport focus behavior. The root cause was split input ownership: command keybindings used the
frame's pressed chords, the viewport router also received a placeholder shortcut after command
execution, and the host adapter treated an empty viewport shortcut as a command target that could
resolve to focus selection.

Invariants:

- SDL keydown events own exact pressed shortcut chords because `SDL_KeyboardEvent::mod` is the
  modifier state at the keydown event. ImGui remains the source for UI/text focus, mouse/wheel, and
  held per-frame navigation state.
- A resolved command keybinding or pending chord must mark the `ViewportInputFrame` keybinding input
  as consumed. The viewport router must not interpret a command shortcut as a viewport action in the
  same frame.
- `ViewportShortcutKind::None` is a sentinel only. It must not map to a command target and must not
  request focus/document navigation.
- User shortcuts for viewport commands with no existing binding must use the command's default
  keybinding context, so Focus Selection remains viewport-scoped instead of becoming a global
  shortcut.

Affected owners/files:

- `src/platform/sdl_input_adapter.*`
- `src/platform/platform_event.*`
- `src/ui/ui_system.*`
- `src/ui/viewport_input_frame_builder.*`
- `src/ui/viewport_panel.cpp`
- `src/viewport/viewport_input_router.cpp`
- `src/viewport/host_viewport_contract_adapter.cpp`
- `src/api/quader_commands_api.cpp`
- `tests/core_tests.cpp`
- `tests/editor_api_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportInputFrameBuilderTests.ModifierHelperPreservesCanonicalKeyMods:SdlInputAdapterTests.KeyEventModifiersBuildPressedShortcutChord:ViewportInputFrameBuilderTests.EventPressedChordsOverrideImGuiPressedFallback:ViewportInputRouterTests.ConsumedCommandKeybindingDoesNotRouteViewportShortcut:ViewportInputRouterTests.EmptyShortcutSentinelDoesNotRouteViewportAction:ViewportHostContractAdapterTests.EmptyShortcutDoesNotMapToCommandTarget:EditorApiTests.GlobalCommandKeybindingsResolveWithAndWithoutViewportFocus:EditorApiTests.DebugCopySelectionJsonShortcutWinsOverFocusSelectionBind:EditorApiTests.FocusSelectionUserShortcutUsesViewportContext`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-06 - Mesh Surface Grid Shader Is A Mesh Material Uniform Path

Task/context: Add an optional TrenchBroom-style grid drawn on mesh surfaces while keeping the
ground grid and overlay systems as separate owners. The feature is toggled from View > Mesh Grid and
uses dedicated mesh-grid minor/major color-alpha settings plus existing viewport grid line-size
settings.

Invariants:

- Mesh surface grid drawing belongs in the mesh Filament materials and `MaterialBindingService`
  uniforms. Do not add separate overlay geometry for this path; overlay depth proxies must remain
  depth-only and must not receive visible grid updates.
- `ViewportSettings` owns the persisted `ShowMeshGrid` toggle. The UI menu should route through the
  normal viewport bool setting helper, while `ViewportPanel::prepare_viewports()` passes the full
  current settings and grid preset to `SceneRenderBridge::update_mesh_surface_grid(...)`.
- `ViewportSettings` also owns `MeshGridMinorColor` and `MeshGridMajorColor`; the settings panel
  exposes them from the normal descriptor model and `MaterialBindingService` maps them to
  `surfaceGridMinorColor` / `surfaceGridMajorColor`.
- Mesh grid line thickness changes width only. Do not multiply mesh-grid line coverage by
  `min(thickness, 1.0)`, because that makes selected RGB colors unable to reach the chosen color
  even when color alpha is 1.0.
- Default mesh albedo still resolves through `materials/default/default_albedo.png`; temporary
  source swaps should preserve the previous image beside it instead of changing catalog package
  paths.

Affected owners/files:

- `resources/materials/quader_mesh*.mat`
- `src/render/material_binding_service.*`
- `src/render/mesh_renderable_factory.*`
- `src/render/mesh_renderable_cache.*`
- `src/render/scene_render_bridge.*`
- `src/ui/main_menu_bar.cpp`
- `src/ui/viewport_panel.cpp`
- `src/viewport/viewport_settings.*`

Verification:

- `cmake --build --preset vs2026-debug --target quader_mesh_material quader_mesh_lit_material`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportSettingsTests.*:QuaderMeshMaterialTests.*:SceneRenderBridgeTests.UsesExplicitTextureColorSpaceForMaterialUploads:ViewportRendererTests.MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame:GroundGridMaterialTests.*:LocalizationTests.*:Localization*`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.*:BuildGuardrailTests.LocalizationResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime:BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:ProgrammaticApiGuardrailTests.*Viewport*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-06 - Object Selection Wire Uses Transient Camera-Clipped Overlay Lines

Task/context: Object/mesh selection mode orange wires disappeared or faded near the viewport camera
because they used the persistent selected-object shader-expanded line cache, while component-mode
cyan/blue wires used CPU-clipped per-viewport overlay line quads.

Invariants:

- Selected object wire lines must stay in the transient `ViewportOverlayFrame` path so
  `OverlayLineMeshBuilder` clips them with the active `ViewportOverlayCamera`, matching component
  wire behavior in every viewport.
- `SelectedObjectOverlayCache` is face-fill-only. Do not reintroduce selected-object wire proxies,
  `SelectedObjectLineVertex`, or selected-object `nearPlane` shader uniforms there.
- `ModelingSelectionOverlayOptions::suppress_persistent_object_selection` suppresses selected object
  face-fill triangles from the transient frame, not the selected object wire lines. The persistent
  cache supplies the face fills for object mode; the transient frame supplies the wires.
- The remaining persistent shader-expanded line path is the scene wireframe cache. Keep it separate
  from object selection wire behavior unless wireframe mode itself is being changed.

Affected owners/files:

- `src/editor/modeling/services/modeling_session_services.cpp`
- `src/render/overlay/selected_object_overlay_cache.*`
- `src/render/scene_render_bridge.cpp`
- `tests/core_tests.cpp`
- `tests/render_extraction_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportOverlayPolicyTests.PersistentObjectSelectionCacheOwnsFaceFillsOnly:ViewportOverlayPolicyTests.NativeViewportKeepsTransientObjectSelectionWireBeforeProjection:ModelingSessionServiceTests.PersistentObjectSelectionSuppressionKeepsTransientWire:RenderOverlaySnapshotTests.PersistentSceneWireframeLineCacheUsesSnapshotClipPlanes:RenderOverlaySnapshotTests.CapturesSelectedObjectAndSceneWireframeRecordsWithDirtyRevisions`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-06 - Ground Grid Far Clip Is A Shader Fade

Task/context: The scene camera far clip affected scene geometry, but the ground grid did not fade
like Blender when the far clip was reduced. The grid is a render-owned overlay plane with a custom
unlit material, not normal scene mesh material, so it needs its own far-plane uniform and alpha
fade.

Invariants:

- `ViewportGridRenderer` must pass the active perspective camera far plane into `ground_grid.mat`.
- `ground_grid.mat` should keep its existing large-distance and plane-edge fades, then multiply by
  a Blender-style far fade from half the far clip to the far clip using full world distance to the
  camera.
- Do not apply that distance fade in orthographic mode; orthographic grid size is viewport-derived
  and camera-to-plane distance can be unrelated to visible grid coverage.

Affected owners/files:

- `resources/materials/ground_grid.mat`
- `src/render/viewport_grid_renderer.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target ground_grid_material`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=GroundGridMaterialTests.UsesUserWorldAnchoring`

## 2026-07-06 - Superseded: Vertex Handles Drew On Top

Task/context: Flipped-normal meshes made vertex handle dots disappear unless the camera was very
close, while the same vertices on normal exterior faces stayed visible. The points were normal
depth-tested billboards, so two-sided or flipped surface depth could win over the dot quad.

Superseded: the 2026-07-07 vertex leakage fix below replaces this draw-on-top policy with
depth-tested vertex points plus a small point depth bias.

Former invariants, kept only as historical context:

- `UnselectedVertex`, `SelectedVertex`, and `HoverVertex` are editable viewport handles and should
  draw on top. Do not route component selected/hover vertex points through a separate depth-tested
  policy.
- Component wires/edges still use the depth-tested edit-wire batch path. This change is for point
  handles only.
- Do not reintroduce component-point private depth bias helpers in
  `OverlayRenderableBuilder`; avoiding the surface-depth fight is the owner policy.
- The per-object selection overlay cache must strip `SourceWire` and `SourceWireDepthStamp`
  primitives for inactive selected objects in component modes. Building one object in isolation can
  otherwise re-infer that inactive selected object as the source-wire owner.

Affected owners/files:

- `src/render/viewport_overlay_policy.cpp`
- `src/render/overlay/overlay_renderable_builder.cpp`
- `src/editor/modeling/services/modeling_session_services.cpp`
- `tests/core_tests.cpp`
- `tests/render_extraction_tests.cpp`

Verification:

- Superseded; use the 2026-07-07 depth-tested vertex point verification below.

## 2026-07-07 - Vertex Points Depth-Test With Bias And Inside-Out Room Exception

Task/context: after vertex points were made draw-on-top to avoid flipped-normal depth fights, normal
outward meshes still showed back/hidden vertex dots through shaded surfaces. CPU source-wire
depth-stamp filtering was not enough because the real point batches still used an always-pass depth
state. After switching editable vertex point batches to depth-tested, inside flipped-normal rooms lost
their visible/selectable vertex handles because the room surface depth could hide them from inside.

Invariant:

- `UnselectedVertex`, `SelectedVertex`, and `HoverVertex` point batches are depth-tested by policy;
  they must not use `OverlayDepthFunc::Always`.
- Use the existing point post-projection depth bias (`kOverlayPointDepthBias`) to keep same-surface
  vertex handles from z-fighting. Do not move point X/Y and do not add private component-point depth
  hacks.
- `SourceWireDepthStamp` remains non-rendered metadata. `OverlayRenderableBuilder` may use it to
  pre-filter outward-facing same-object vertex point inputs before building point batches. GPU depth
  testing is the normal visibility authority except for the camera-inside inside-out room exception.
- Inside-out source-wire depth stamps must not CPU-cull internal vertex point inputs. When the camera
  is inside the inside-out stamped volume, editable vertex point batches draw on top so inverted-room
  vertices remain visible/selectable. Outside the stamped volume they keep the normal depth-tested
  point path.
- Ordinary `SourceWire` line batches remain crisp draw-on-top topology; do not use this fix to
  CPU-delete source-wire lines.

Affected owners/files:

- `src/render/viewport_overlay_policy.cpp`
- `src/render/overlay/overlay_renderable_builder.cpp`
- `src/editor/metadata/host_contract.cpp`
- `docs/overlay-handling.md`
- `tests/core_tests.cpp`
- `tests/modeling_domain/editor_metadata/qdr_editor_metadata_overlay_catalog_tests.cpp`
- `tests/render_extraction_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportOverlayPolicyTests.MatchesDepthAndHoverSuppressionContract:ViewportOverlayPolicyTests.ComponentSelectionWiresAndVertexPointsUseDepthTestedState:OverlayLineMeshBuilderTests.PerspectiveDepthBiasDoesNotMoveLineOrPointScreenPosition:OverlayPointMeshBuilderTests.*`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no --gtest_filter=QdrEditorMetadataOverlayCatalogTests.HostContractOverlayLayerCatalogStatesRenderDepthSemantics`

## 2026-07-11 - Device-Domain Overlay Bias Must Account For Filament Reversed Z

Task/context: after the Qt direct-swapchain migration, outward-facing convex box corners could be
pickable while their vertex dots were completely hidden by the adjacent shaded faces. CPU overlay
emission and picking tests passed, so they did not prove the real pixel result.

Invariant:

- `OverlayCameraProjector` outputs OpenGL-domain NDC Z for Filament `vertexDomain : device`
  materials. Filament then maps that input with `z * -0.5 + 0.5`, producing reversed device depth.
- A positive toward-camera bias must therefore be **subtracted** from the projector's GL-domain Z
  before Filament performs that mapping. Adding it makes the later reversed depth smaller, pushes
  `GreaterOrEqual` point quads behind the surface, and can hide an entire convex-corner billboard.
- Clamp the biased GL-domain Z at `-1.0f`. Do not turn ordinary editable vertex points into
  draw-on-top geometry; the existing outward hidden-point filtering, GPU occlusion, and inside-out
  room exception remain authoritative.
- Headless projection/emission tests protect the sign and hidden/flipped policy, but final
  verification for this class of regression requires the Vulkan app. The user visually confirmed
  the formerly missing top-corner marker in the rebuilt Qt app.

Affected owners/files:

- `src/render/overlay/overlay_camera_projector.cpp`
- `tests/render_extraction_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=OverlayCameraProjectorTests.*:OverlayLineMeshBuilderTests.PerspectiveDepthBiasMovesTowardCameraWithoutMovingScreenPosition:OverlayPointMeshBuilderTests.*`
- Manual Vulkan smoke: create/view a box in vertex mode from a diagonal camera; the outward top
  convex corner dot is visible while occluded back vertices remain governed by the existing policy.

## 2026-07-07 - Pivot Tool Uses Tool Settings And Origin Rebasing

Task/context: Pivot needed to become a first-class viewport tool on `P`, with hold-`D` Quick Pivot,
generic tool settings, snapping, quick-align actions, undoable permanent pivot changes, and no
visible model jump when rebasing origins.

Invariant:

- Tool settings are exposed through `QuaderToolsApi::active_settings()` as generic
  `QuaderToolSettingsTarget` controls. The UI panel must render these controls and write back
  through `editor.tools()`, not switch on modeling operation kinds.
- Persistent Pivot is owned by `PivotToolService` under `ProjectSession`. Applying it rebases the
  target origin by moving the scene transform and compensating mesh vertices or group child local
  transforms so world bounds/child world matrices stay fixed. Mesh data must be editable CPU data;
  shared mesh assets use clone-on-write.
- Pivot quick-align action labels are axis-stable: Top/Bottom use world Y, Left/Right use world X,
  and Front/Back use world Z. The AABB corner actions must keep that mapping.
- Quick Pivot is a temporary selection pivot override. It survives `D` release, feeds
  `selection_transform_pivot()` for subsequent transform gizmo drags, and clears on selection
  change or deselect. It must not resolve while WASD fly navigation is active.
- Transform gizmos must ask `QuaderQueryApi::selection_transform_pivot()`: quick-pivot override
  first, single selected object/group custom-pivot world origin second, otherwise the normal
  selection center. Default created/imported meshes are not custom-pivoted, so their gizmo stays at
  their selected bounds center; only an applied Pivot rebase marks the transform origin as the later
  transform pivot.
- The Pivot handle overlay is a small yellow `ToolHandle` line-ring sphere, not an overlay point
  billboard. Pivot editing/dragging must mark before-tool interaction as gizmo-consumed so viewport
  selection fallback and box marquee do not run while the pivot handle is being moved.
- Pivot settings expose only Prev/Next align-cycle actions. The cycle has one center preset only;
  Bounds Center is not a separate preset because it resolves to the same position.

Affected owners/files:

- `src/editor/pivot/pivot_tool_service.*`
- `src/api/quader_tools_api.*`
- `src/api/quader_query_api.*`
- `src/ui/tool_settings_panel.cpp`
- `src/ui/viewport_panel.cpp`
- `src/viewport/host_viewport_contract_adapter.cpp`
- `src/editor/tools/base/viewport_tool_descriptor_catalog.cpp`
- `tests/editor_api_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.Pivot*:EditorApiTests.QuickPivot*:ProjectSessionTests.QuickPivotOverrideFeedsTransformPivotAndClearsOnSelectionChange:EditorApiTests.ViewportKeybindingContextsSeparateNavigationFromToolShortcuts`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-07-08 - Selected Source-Wire Edge Loop Face Context

Task/context: double-click edge-loop selection on an already selected open/shared lip edge could
select only the seed or the wrong adjacent side. The selected source-wire path synthesized edge
hits without a `face_id`, and shared-edge face picks could depend on document face order when the
ray hit exactly on the edge. A later open-box repro showed a true boundary seed could still expand
through the picked face perimeter instead of the Blender-style hole loop because face-context
selection ran before boundary fan traversal.

Invariant:

- Runtime source-wire edge hits must carry the same picked-surface context as normal polygon edge
  hits before loop expansion.
- For shared-edge context, face order must not decide the loop side. Choose the incident pickable
  face most facing the pick ray, then use `edge_loop_edges(document, edge, face_id)` so shared lip
  edges expand over the intended picked surface region.
- True open-boundary seed edges must run the boundary fan loop before picked-face context. The
  picked face is still needed for manifold/shared lip edges, but it must not turn a hole boundary
  seed into the surrounding face perimeter.
- Edge-mode `Alt + left click` is an explicit loop-select gesture and routes to the same
  `pick_select_edge_loop` owner as edge double-click. Shift/Ctrl still decide add/remove; Alt only
  chooses the loop gesture.
- Keep regression coverage for a selected/source-wire shared lip edge where the vertical drop face
  is ordered before the top face, plus an open-box missing-side boundary edge whose pick carries
  the top-face context.

Affected owners/files:

- `src/editor/modeling/runtime/session_picking.cpp`
- `src/editor/interactions/runtime_command.cpp`
- `src/editor/modeling/services/modeling_session_services.cpp`
- `src/mesh/polygon/quader_poly_document_picking_overlay.cpp`
- `src/mesh/polygon/quader_poly_document_selection_api.cpp`
- `tests/modeling_domain/operation/operation_test_helpers.hpp`
- `tests/modeling_domain/operation/qdr_document_operation_insert_edge_loop_tests.cpp`
- `tests/modeling_domain/operation/qdr_modeling_runtime_command_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=QdrDocumentOperationInsertEdgeLoopTests.EdgeLoopSelection*:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadePickSelectDoubleClickDelegatesBySelectionMode:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeShiftAndCtrlDoubleClickEditWholeEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeAltClickSelectsWholeEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickSelectsBoundaryFanEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickUsesPickedFaceForSharedCornerEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickSelectedSharedLipEdgeUsesPickedSurfaceBoundary:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickSelectedOpenBoxBoundaryUsesHoleLoop`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.AltClickEdgeSelectsLoopThroughViewportFallback:ViewportInteractionControllerTests.ShortMoveGizmoClickKeepsEdgeSelection:ViewportInteractionControllerTests.TransformGizmoFrameSurvivesNavigationFrames`

## 2026-07-11 - Linear Array Uses Fixed A/B Origins And Atomic Duplication

Task/context: Linear Array needed to become a first-class native viewport tool: select one mesh,
drag endpoint B while A stays anchored to the selected mesh origin, preview evenly spaced copies in
real time, and commit the result as one undoable edit.

Invariant:

- Linear Array activates only for exactly one visible, unlocked modeling mesh in Object mode. Count
  is source-inclusive, defaults to 4, and stays within 2..128. A is the source world origin; B is the
  last copy origin. Changing count keeps B fixed, and zero-length arrays intentionally overlap.
- `LinearArrayToolService` owns transient source validation, count, endpoint, axis constraint, and
  preview state. X/Y/Z are semantic, remappable constraint actions; pressing the active axis again
  returns to Free. Space/Enter apply, Escape cancels, and Ctrl inverts global grid snapping during
  endpoint drag. UI code only projects the handle and dispatches intent.
- Editor-owned viewport tools route through `NativeViewportToolCoordinator`. Their descriptors set
  `editor_owned_native_lifecycle`; do not add per-tool exceptions to host-contract lookup.
- Commit runs one `LinearArrayHistoryCommand` and one runtime `duplicate_objects` batch. Results are
  source first then copies A-to-B; copies keep the source parent and durable scene properties, do not
  copy source children, own independent modeling documents/mesh assets, select source plus copies,
  and make B active.
- Modeling mementos preserve entity ID, editor object ID, and UUID across undo/redo. Scene/asset sync
  mutations, including the batch transform assignment, stay behind `ModelingMeshSyncService`.
- Array preview uploads source geometry once and uses shared Filament mesh/material resources for
  transform-only instances. Moving B updates instance transforms without re-uploading geometry.

Affected owners/files:

- `src/editor/array/linear_array_tool_service.*`
- `src/editor/tools/native_viewport_tool_coordinator.*`
- `src/editor/modeling/runtime/session.*`
- `src/editor/modeling/services/modeling_session_service.*`
- `src/editor/modeling/services/modeling_mesh_sync_service.*`
- `src/api/quader_modeling_api.*`
- `src/api/quader_tools_api.*`
- `src/ui/viewport_panel.cpp`
- `src/render/tool_preview_renderable_cache.*`
- `src/render/mesh_renderable_factory.*`
- `tests/editor_api_tests.cpp`

Verification:

- `build\vs2026\Debug\quader_tests.exe --gtest_filter=EditorApiTests.LinearArray*` (6/6)
- `build\vs2026\Debug\quader_tests.exe` (692/692)
- `build\vs2026\Debug\quader_guardrail_tests.exe` (164/164)
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=-QdrGeometryTests.QdrGeometryHelpersRemainStable` (498/498)
- Full CTest still exposes the pre-existing `QdrGeometryTests.QdrGeometryHelpersRemainStable` float
  ray/plane precision failure; no geometry source is changed by Linear Array.

## 2026-07-12 - Qt Viewport Navigation Gizmo Keeps Per-Pane Input Ownership

Task/context: the Qt Widgets migration preserved the Navigation Gizmo preference and View menu
toggle but omitted the rendered, interactive per-pane view cube that the ImGui viewport exposed.

Invariant:

- Every retained Qt viewport pane owns one `QtNavigationGizmo`. Visibility follows the existing
  public preference; the widget uses the stock Fusion palette and frame rather than QSS, palette
  overrides, custom fonts, or custom window chrome. Semantic X/Y/Z colors remain editor visuals.
- Cube geometry and hit testing stay in `quader_editor_core` through
  `build_view_cube_primitives()` and `pick_view_cube()`. Qt only paints those primitives and adapts
  pointer events.
- A face click applies the same unlocked orthographic axis view as the legacy gizmo. Crossing the
  three-pixel drag threshold converts the gesture to orbit and suppresses the click snap.
- A gizmo press focuses its owning workspace pane and grabs the mouse until release or cancel.
  While grabbed, viewport routing remains locked to that pane and modeling hover, selection,
  transform gizmos, pivot, and linear-array interactions are suppressed.
- The gizmo is a native sibling above each embedded Filament child surface and is positioned below
  that pane's floating view/shading controls. Do not move this behavior into render overlays or
  alter render-owned overlay depth policy.

Affected owners/files:

- `src/ui/qt_navigation_gizmo.*`
- `src/ui/qt_viewport_panel.*`
- `tests/qt_navigation_gizmo_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QtNavigationGizmoInteractionTests.*:ViewportCameraTests.ViewCube*` (6/6)
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.QtViewportRestoresPerPaneInteractiveNavigationGizmos:UiLayoutGuardrailTests.QtShellSelectsFusionAsDefaultBaseline:BuildGuardrailTests.RuntimeUiUsesQtWidgetsWithFusionBaseline` (3/3)
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-12 - Qt Settings And Tool Windows Retain Stock Auxiliary State

Task/context: the Qt migration needed localized Tool Settings chrome, retained auxiliary-window
placement, and a scannable Settings layout without reintroducing custom theme code.

Invariant:

- `QtAuxiliaryWindowState` persists Settings and Tool Settings geometry plus logical visibility in
  `qt_auxiliary_windows.ini` through `QSettings`. The versioned `QMainWindow::saveState` binary
  remains dedicated to dock layout so existing main-window state stays compatible.
- Settings remains a normal non-modal `QDialog` with the stock Fusion/platform close chrome. Tool
  Settings remains a stock `Qt::Tool` window whose visibility follows the active public tool target.
- Tool Settings title, active-target title, Apply, and Cancel text resolve through
  `LocalizationService`; stable Qt object names remain untranslated.
- Settings categories use a stock `QListWidget` sidebar and `QStackedWidget` right-hand pages.
  Reusable `QtColorSwatchButton` controls set only the local `QPalette::Button` role to the selected
  color, so Fusion draws the normal rounded button face, border, hover, press, focus, and disabled
  states across the complete fluid width. Exact `#AARRGGBB` values remain in tooltip/accessibility
  data, and `QColorDialog::ShowAlphaChannel` preserves alpha editing. Do not replace this with QSS,
  custom painting, icon previews, or custom window chrome.

Affected owners/files:

- `src/ui/qt_auxiliary_window_state.*`
- `src/ui/qt_color_swatch_button.*`
- `src/ui/qt_settings_dialog.cpp`
- `src/ui/qt_tool_settings_window.*`
- `src/ui/ui_settings.*`
- `src/ui/ui_system.*`
- `tests/qt_auxiliary_window_state_tests.cpp`
- `tests/localization_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QtAuxiliaryWindowStateTests.*:LocalizationResourceTests.*:LocalizationServiceTests.*` (17/17)
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.*` (19/19)

## 2026-07-12 - Qt Settings Stay Live, Reversible, And Event Driven

Task/context: after the Qt migration, settings controls could display or persist a value without
updating the live viewport/render behavior. Color buttons also needed preview, accept, and cancel
semantics without reintroducing a custom theme.

Invariant:

- The retained Settings dialog covers every viewport preference descriptor: locale, 20 colors,
  16 floating-point values, and 7 booleans. A control write must update the owner state immediately,
  notify retained consumers, and persist through the existing settings owner; the dialog is not a
  second source of truth.
- `QtSettingsController` emits a typed change kind. `UISystem`, the Settings dialog, menus, and
  viewport presentation synchronize from that event, not from per-frame dialog reconstruction or
  polling.
- A `QColorDialog` live preview applies through the same owner route. Accept keeps and persists the
  chosen color; cancel restores the exact pre-dialog value and live presentation. Alpha remains
  part of the value.
- Persistent selected-object and scene-wireframe overlay caches consume narrow immutable style
  snapshots/signatures. A settings edit invalidates only the affected cache; it must not alter
  overlay layer order, depth, blend, visibility, or flipped-mesh policy.
- Settings persistence failures are logged. Legacy/missing semantic-axis values migrate to the
  canonical X/Y/Z defaults without overwriting valid user values.

Affected owners/files:

- `src/ui/qt_settings_controller.*`
- `src/ui/qt_settings_dialog.*`
- `src/ui/qt_color_swatch_button.*`
- `src/ui/ui_system.*`
- `src/viewport/viewport_settings.*`
- `src/render/overlay/viewport_persistent_overlay_style.hpp`
- `src/render/overlay/selected_object_overlay_cache.*`
- `src/render/overlay/scene_wireframe_overlay_cache.*`

Verification:

- Headless control-contract coverage exercises all 44 controls plus color preview/accept/cancel,
  dialog/menu synchronization, and persistence.
- `QtSettingsControllerTests.*`, `ViewportSettingsTests.*`,
  `ViewportPersistentOverlayStyleSnapshotTests.*`, and the relevant UI/render guardrails pass.
- Manual Vulkan verification confirmed a grid-color edit updates the open viewport immediately and
  cancel restores it; the user's original settings file was restored afterward.

## 2026-07-12 - Large-Scene Navigation And Transforms Gate Stable Work

Task/context: `my_first_project.qdr` was the real stress fixture. With 403 selectable entities,
all-selected navigation and transform interaction became effectively unusable even though the GPU
timing was small. The bottleneck was repeated CPU extraction, projection, uniform, retained-widget,
and per-entity history work ahead of presentation.

Invariant:

- The fixture identity for this regression is 2,703,006 bytes with SHA-256
  `05604D3C885792458C0B748F25DC12E19DC7C59949AE0B522B1853CD65D1AB4B`: 418 entities, 416 mesh
  entities, 14 locked entities, and 403 entities selected by Select All. Benchmarks must never write
  the input and must verify its identity after the run.
- Stable owner revisions stop work before scene extraction. `RenderOverlaySnapshotCache` owns the
  renderer-neutral selected-object, scene-wireframe, and scene-overlay snapshots and their narrow
  revision/style signatures; `SceneRenderBridge` remains a snapshot consumer. One prepare pass
  updates the cache once and all visible panes reuse it.
- Mesh surface-grid uniforms use a narrow signature and are not rewritten on unchanged frames.
  Camera projection constants and the exact sRGB-byte conversion table are precomputed/reused for
  overlay projection. These are mechanical hot-path reductions; overlay draw/depth/blend/visibility
  policy is unchanged.
- Transform gizmo selection pivots are cached by selection/scene revisions. Qt navigation-gizmo
  setters ignore identical pointer/pose/visibility state. Outliner and Inspector refresh only for
  relevant scene/modeling/locale revisions and defer retained rebuilds during an active transform.
- Multi-entity transform preview applies one batch assignment per update and records one
  `ApplyEntityTransformsCommand` at commit. Cancel, accept, commit, undo, and redo preserve exact
  transforms while revision growth stays O(1), not O(selected entities).
- Expensive transformed picking projections are lazy. Only an active, previewable, valid Cut or
  Mirror consumer requests them, and a selection-overlay cache hit returns before projection.
  `ModelingSessionService` remains a thin adapter; tool-state gates stay in
  `ModelingOverlayService`.
- The opt-in `qdr_fixture_performance_baseline` target measures the exact load/select/navigation/
  unchanged-snapshot/overlay/transform workload, emits JSON, restores transforms bit-for-bit, and
  rejects any output path that resolves to the input QDR.

Affected owners/files:

- `src/render/scene_render_extraction.*`
- `src/render/scene_render_bridge.*`
- `src/render/overlay/render_overlay_snapshot.*`
- `src/render/material_binding_service.*`
- `src/render/mesh_renderable_cache.*`
- `src/render/overlay/overlay_camera_projector.*`
- `src/core/color_space.hpp`
- `src/editor/viewport/transform_gizmo_interaction.*`
- `src/editor/history/scene_transform_preview_transaction.hpp`
- `src/editor/commands/document/apply_entity_transforms_command.hpp`
- `src/editor/modeling/services/modeling_overlay_service.*`
- `src/editor/modeling/services/modeling_selection_service.*`
- `src/ui/qt_viewport_panel.*`
- `src/ui/qt_navigation_gizmo.*`
- `src/ui/ui_system.*`
- `benchmarks/modeling/qdr_fixture_performance_baseline.cpp`

Verification:

- Release exact-fixture p50: Select All 0.1056 ms; navigation input 0.0003 ms; unchanged snapshot
  baseline 0.1654 ms; 4,824-line camera overlay build 0.3197 ms; 403-entity transform preview
  0.1321 ms. All 403 transforms changed during preview and cancel restored the original checksum.
- Debug 403-entity repeated-drag median is about 6.5-6.7 ms; the dedicated 200-sample fixture run
  measured 6.5006 ms p50 and 10.6451 ms p95.
- In the real Vulkan app, ten all-selected zoom inputs completed in 174 ms and the immediate UI
  state capture returned in 45 ms, replacing the prior multi-second/tens-of-seconds apparent freeze.
- Focused render, viewport, modeling-session, document-command, UI, guardrail, and native-app build
  checks pass; benchmark JSON lives under `build/perf/`.

## 2026-07-12 - All-Selected Gizmo Drag End-To-End Frame Fix

Task/context: the modeling-only stress benchmark was green while the real Qt viewport still let the
403-object selection trail the move gizmo. A genuine red-axis drag and Undo first proved that the
gizmo handle, transform transaction, and restored selection were the path under test. The missing
cost was the rest of the same frame: scene extraction, Filament proxy transforms, selection bounds,
semantic cage generation, camera projection, and GPU buffer updates.

Invariant:

- Qt may deliver press, threshold-crossing move, and release in one input frame. Transform gizmo
  interaction must establish the pending hit before release handling, begin/apply/commit a collapsed
  drag in that frame, and apply the newest pointer sample before any active-drag commit.
- Pure Move previews cache each target's parent-space world basis and reference-aware decomposition
  template at begin. Per-sample work updates local positions only. Rotate/Scale keep the generic
  matrix/decomposition path. Parented transforms, custom pivots, cancel, commit, undo, and redo must
  remain equivalent.
- `SceneDocument::render_component_revision()` covers render-structural/material/light changes.
  A retained `RenderSceneSnapshot` may refresh transforms in place only when asset, selection,
  visibility, mesh-binding, and render-component revisions prove all other sources stable.
- Filament mesh transform updates are dirty-record-only and run inside one local-transform
  transaction. Mesh/material/visibility/light rebuild paths remain conservative fallbacks.
- `RenderOverlaySnapshotCache` retains local bounds by mesh revision, world bounds by entity, and
  stable selected/group ordering. Pure affine translation moves cached AABBs; linear changes use the
  exact eight-corner fallback. Transform-only frames update the retained selection snapshot in place.
  Selection, visibility, hierarchy, mesh, render-component, asset, or mixed scene changes force a
  full rebuild.
- Generic line and edit-wire proxies retain canonical indices when kind and vertex/index counts are
  unchanged, uploading changed projected vertices only. Overlay line count/order, style, depth,
  blend, visibility, and picking policy are not performance knobs and must not change.
- The fixture revision used for this follow-up is 2,703,035 bytes, SHA-256
  `D5516A98E4C4222B2EA11207C066444C88B2D0A701FB1E9C2E8BC949FDE556B0`, FNV-1a64
  `0x28d995351cafb8d6`: 418 entities, 416 mesh entities, 14 locked entities, and 403 Select-All
  transform targets. The benchmark must restore transforms exactly and leave this input unchanged.

Affected owners/files:

- `src/editor/viewport/transform_gizmo_interaction.cpp`
- `src/editor/transform/scene_transform_drag_service.*`
- `src/editor/modeling/services/modeling_transform_drag_service.cpp`
- `src/scene/scene_document.*`
- `src/scene/scene_bounds.*`
- `src/render/render_scene_snapshot.hpp`
- `src/render/scene_render_extraction.*`
- `src/render/scene_render_sync_service.*`
- `src/render/overlay/render_overlay_snapshot.*`
- `src/render/overlay/overlay_proxy_factory.cpp`
- `benchmarks/modeling/qdr_fixture_performance_baseline.cpp`

Verification:

- Debug exact-fixture 200-sample end-to-end CPU median improved from 24.4116 ms to 14.6155 ms;
  final p95 was 18.7171 ms. Final medians: transform preview 2.6150 ms, scene snapshot refresh
  3.0657 ms, modeling overlay refresh 0.0491 ms, selection-overlay snapshot refresh 3.1339 ms,
  semantic/style build 1.4160 ms, and camera line build 4.2698 ms.
- The final run refreshed the transform-only overlay snapshot 210/210 times, reused translated world
  bounds 84,057 times, emitted the same 4,824 cage lines, changed all 403 target transforms, restored
  the original transform checksum exactly, and reported `inputUnchanged=true`.
- Focused render/overlay/scene/transform/viewport coverage passed 60/60 after a fresh full link. An
  earlier pre-main `0xC0000005` was corrupted concurrent MSVC incremental-link state; deleting only
  the test EXE/ILK/PDB and exclusively relinking fixed it without source rollback.
- Final report: `build/perf/my_first_project_debug_transform_frame_final.json`.

## 2026-07-12 - Qt Captured Fly Relative-Pointer Parity

Invariant:

- Captured right-mouse fly navigation drains retained genuine motion once per viewport input frame.
  A relative raw packet contributes its delta and suppresses its following duplicate Qt move; an
  absolute raw packet selects its following translated Qt delta. Packet-mode changes never discard
  genuine deltas already retained in the current frame.
- Legacy Qt mouse events remain enabled and continue to own button/focus state plus physical local
  coordinates used for picking. Failed raw registration uses accepted Qt deltas after cursor-warp
  suppression while the native filter remains installed to observe capture theft.
- A failed Qt mouse-grab enable must never advertise logical capture or hide/recenter the cursor.
  Verify Win32 transitions through `GetCapture()`; on the portable fallback use the boolean
  operation-success result returned by `QWindow::setMouseGrabEnabled()`. Disable failure still
  clears logical capture.
- Focus loss releases capture immediately and restores the pre-capture cursor position. External
  `WM_CAPTURECHANGED` follows the same release/restore lifecycle but cancels the RMB drag chord
  without synthesizing a click release that could open the context menu. Capture transitions and a
  queued generation check guard against synchronous focus re-entry and stale capture-loss delivery.

Owners:

- `src/platform/raw_mouse_input.*` owns Win32 registration and complete-packet decoding.
- `src/ui/qt_relative_pointer_input.*` owns the Qt native-event bridge, packet-ordered frame
  accumulator, capture transition policy, and warning gate.
- `src/ui/qt_viewport_surface.*` drains captured delta input, preserves Qt picking coordinates, and
  owns the queued capture-loss release/restore lifecycle.

Verification:

- `QtRelativePointerInputTests.*` passes 12/12, including native capture postconditions,
  mixed relative/absolute packet routing, per-frame reset, zero raw packets, and failure policy.
- Focused Qt input, fly camera, and viewport-router coverage passes 61/61.
- The two captured-pointer source guardrails pass with the native `quader` target built
  successfully.

## 2026-07-12 - Qt Widgets / ImGui Feature-Parity Closure

Task/context: compare the Qt Widgets editor against the last ImGui editor baseline at `8d47a86`
and restore user-facing behavior without changing the render-owned overlay policy or replacing
stock Qt Fusion styling.

Invariant:

- Parity covers the full editor workflow rather than visual imitation: the menu/command surface,
  outliner and inspector actions, console and status surfaces, settings descriptors and live
  consumers, one-to-four resizable viewport layouts, maximize/restore, per-viewport view and
  shading controls, navigation gizmo and fly HUD, focus, selection marquee, transform/pivot/array
  tools, context/delete popups, and tool preview/commit/cancel lifecycles.
- Narrow viewport chrome must compact and then hide controls instead of clipping them. Numeric
  tool settings use paired Fusion sliders and spin boxes; both paths dispatch the same representable
  value and floating controls preserve at least the old ImGui four-decimal precision.
- Non-viewport custom keybindings preserve the active tool and current selection state. Captured
  RMB fly navigation consumes retained relative-pointer motion while Qt remains responsible for
  button/focus state and physical picking coordinates.
- ImGui-only chrome, theme colors, and resources are not parity requirements. The Qt shell remains
  stock Fusion with no application stylesheet or custom global palette.
- Modeling and render semantics are not UI migration knobs. In particular, box footprint/cross
  selection remains dashed exactly as in the ImGui baseline, and overlay visibility, ordering,
  depth, blend, picking, and stale-geometry policy remain render-owned and unchanged by this pass.

Verification:

- Focused Qt viewport chrome, numeric tool settings, keybinding dispatch, captured-pointer input,
  input adapter/router, and navigation-gizmo coverage passes 75/75.
- Focused Fusion/UI/version guardrails pass 32/32; dependency-matrix and OOP documentation
  guardrails pass 3/3.
- Full CTest passes five of six executables. The only failure is the pre-existing
  `QdrGeometryTests.QdrGeometryHelpersRemainStable` ray-plane floating-point precision assertion
  (the modeling-domain executable otherwise passes 498/499 tests).
- The native `quader` v0.0.93 target builds successfully. A live stock-Fusion smoke check rendered
  all four viewports, resized both split regions interactively, and restored single-view before
  exit. Continuous RMB holding is not supported by the desktop automation API, so that hardware
  interaction remains covered by the captured-pointer tests and requires human smoke confirmation.

## 2026-07-12 - Four-Quad Native Pane First-Presentation Fix

Task/context: after switching from Single to Four Quad, Top/Front/Side displayed their Fusion
controls but their native Filament surfaces stayed blank until a splitter was dragged. The user
reserved the PC, so diagnosis and verification were source-, test-, and build-based only.

Invariant:

- `ViewportWorkspaceService` remains the layout source of truth. A Qt layout refresh first applies
  visibility to every pane, then assigns geometry to active panes. Newly activated native children
  must never be geometrized while hidden and shown only after their one cached geometry pass.
- `QWidget::createWindowContainer()` exclusively owns its embedded `QWindow` geometry and
  visibility. `QtViewportPanel` sizes the container; it never directly calls `resize()` on the
  embedded `QtViewportSurface`.
- Qt native resize delivery may be deferred. Before workspace render size, camera projection, or
  ground-grid preparation consumes dimensions, `QtViewportSurface` explicitly synchronizes the
  Filament `ViewportRenderer` with the physical-pixel QWindow size. The same guarded synchronization
  runs before readiness and submission; equal dimensions are a no-op.
- Layout placement caching is an optimization only after the activation pass has made every pane
  presentable. No splitter mutation may be required to initialize a newly visible pane.
- This fix changes Qt presentation lifecycle only. Swapchain ownership, render/scene dirty gates,
  grid policy, overlay ordering/depth/blend/picking, and modeling state remain unchanged.

Owners:

- `src/ui/qt_viewport_pane_activation.hpp` owns the visibility-before-geometry ordering primitive.
- `src/ui/qt_viewport_panel.cpp` applies the workspace layout and synchronizes active pane sizes
  before camera/grid preparation.
- `src/ui/qt_viewport_surface.*` owns idempotent physical render-size synchronization.

Verification:

- `QtViewportPaneActivationTests.SingleToFourMakesEveryPanePresentableWithoutSplitMutation` covers
  Single -> Four activation followed by the cached unchanged-layout path without splitter input.
- UI lifecycle guardrails enforce activation ordering, container-owned QWindow geometry, and
  renderer-size synchronization before camera preparation and Filament submission.
- Native GPU first-presentation smoke remains intentionally deferred until the user releases the PC.

## 2026-07-12 - Qt Four-Quad Lower-Pane Navigation Latency Fix

Task/context: Four Quad made RMB pan in Front and Right trail badly while Perspective remained much
more responsive. The Qt migration had changed the old single Filament frame into four native-window
frames per application tick, all driven through one Renderer in fixed Perspective, Top, Front, Side
order. That put the lower panes third and fourth in one frame-pacing queue.

Invariant:

- A Filament `Renderer` represents one operating-system window and owns that window's frame-latency
  state. Every Qt native viewport swapchain therefore owns a dedicated Renderer; render begin,
  viewport draw, end, performance-history access, move/reset, and failure cleanup use that paired
  surface context. The primary RenderSystem APIs remain available for non-Qt presentation paths.
- While `ViewportInputRouter::navigating()` is true, Qt submits only the active
  `interaction_pane_id_`. Unchanged native panes retain their last presented pixels. Release,
  cancel, or capture loss clears navigation before presentation, so every active pane refreshes in
  its original layout order on that same release frame.
- A failed active-pane `beginFrame` remains a zero-submission frame and retries the same pane next
  tick; it never falls through to static panes. A stale interaction pane conservatively falls back
  to all active panes.
- This is presentation scheduling and native-renderer ownership only. Camera math, scene/mesh
  extraction, grid behavior, overlay visibility/order/depth/blend/picking, and modeling state are
  unchanged.

Owners:

- `src/render/render_system.*` owns each native swapchain plus its dedicated Filament Renderer.
- `src/ui/qt_viewport_surface.cpp` presents through the paired native-surface context.
- `src/ui/qt_viewport_render_submission_policy.hpp` owns the camera-navigation submission rule.
- `src/ui/qt_viewport_panel.cpp` supplies router state, interaction pane, and active placements.

Verification:

- Held camera navigation changes native submissions from 4 to 1 per app tick: 3 fewer submissions,
  a 75% reduction. Focused policy coverage proves Front-only submission, no failure fallthrough,
  full release refresh, and stale-pane fallback.
- `RenderSystemTests.*`, `ViewportInputRouterTests.*`, `QtRelativePointerInputTests.*`, and
  `QtViewportRenderSubmissionPolicyTests.*` pass 59/59. The relevant Fusion/UI guardrails pass
  32/32, and the full guardrail executable passes 176/176 when run alone.
- Native `quader` v0.0.95 builds and boots with Vulkan/Fusion. Four Quad presents all panes
  immediately; rapid lower-left and lower-right navigation input updates both panes, and the log
  records four successful paired native surfaces with no errors.
- Full CTest still reproduces the unrelated pre-existing
  `QdrGeometryTests.QdrGeometryHelpersRemainStable` large-dot-product precision failure. Its first
  concurrent guardrail run also hit a temporary recursive-directory race with the SDK-only build;
  the complete standalone guardrail rerun passed. Desktop automation cannot hold RMB continuously,
  so final physical held-pan feel remains a human smoke check.

## 2026-07-13 - Transformed Knife, Edge Loop, and Qt Fly Regression Closure

Task/context: Knife snapped away from transformed surfaces, Insert Edge Loop highlighted far from
the pointer and crossed doorway/non-quad topology, RMB/WASD fly navigation could jump, and its
crosshair presented as an opaque native gray quad. Comparison with `main` showed that the intended
tool behavior still belonged to the established modeling operations; the regressions were in the
current host projection/input integration. A first attempted topology fix was
too strict and blanked every preview that reached mixed topology; live flipped/interior testing
exposed that regression and the traversal was restored exactly to `main`.

Invariant:

- Discrete tool actions consume the physical coordinate captured with the click. Knife also
  resolves a fresh hover from that click ray before accepting a point, so a later move in the same
  semantic frame cannot commit stale hover state.
- Scene picking is authoritative in world space. The host resolves the exact object/component,
  transforms the hit and ray into its modeling document, and attaches the object ID to overlays so
  render projection reapplies the scene transform. Grid/edge/face snapping and Knife axis
  constraints are solved in world space; affine camera projection uses the proper inverse basis and
  covector axes, including rotation and non-uniform scale.
- Insert Edge Loop retains the working `main` traversal: it previews the valid connected quad
  segment and stops when continuation reaches triangle/ngon topology instead of erasing the whole
  preview. Host picking remains two-sided, so a camera inside a mesh with flipped face winding can
  still acquire the edge and publish the preview.
- Captured motion has exactly one authoritative source. A confirmed nonzero relative raw packet
  persists across frame drains, discards any Qt delta queued before native classification, and
  excludes later legacy/warp moves; an absolute raw packet explicitly switches to Qt fallback.
  Fallback recenter state preserves physical motion that overtakes a delayed synthetic center event
  without later applying the inverse warp as a camera snap.
- The fly crosshair remains a native child but owns pixels only under its four masked arms. Fusion
  palette colors draw a narrow outline and inner line; the center, arm gaps, and surrounding quad
  are transparent.

Owners:

- `src/editor/modeling/services/modeling_session_service.cpp` and
  `src/editor/interactions/*` own host world/local adaptation and overlay identity.
- `src/editor/tools/base/*` owns captured semantic click consumption.
- `src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_knife_helpers.cpp` owns loop-strip
  topology traversal policy.
- `src/ui/qt_relative_pointer_input.*` owns Windows raw/Qt relative-motion reconciliation;
  `src/ui/qt_viewport_fly_navigation_hud.cpp` owns native crosshair composition.

Verification:

- Focused live-path coverage passes 22/22, including transformed Insert picking, mixed-topology
  preview, inside/flipped-winding preview, queued physical clicks, and held RMB+WASD with delayed
  Qt/raw delivery.
- Insert Edge Loop operation coverage passes 14/14; runtime Knife/Insert/affine-projection coverage
  passes 15/15; relevant guardrails pass.
- Full CTest passes 6/6 executables, and native `quader` v0.0.106 builds successfully.
- Real Windows held-RMB raw-input feel and native crosshair composition remain a final human GPU
  smoke check because the headless harness cannot reproduce continuous physical capture.

## 2026-07-13 - Native Radial Array Operation

Task/context: add a Plasticity-style total-sweep Radial Array as a native editor tool and public
modeling facade operation, including shared point placement, live multi-source preview, repeats,
atomic commit/history, and Shift+Wheel copy adjustment.

Invariant:

- Radial Array is editor-owned, requires Object mode plus visible, selectable, unlocked modeling
  meshes, and captures one shared center and plane normal. It never adds a portable modeling SDK
  operation.
- Center placement ranks vertex, edge midpoint, face center, arbitrary surface, then active-grid
  candidates. Arbitrary surface hits use Box-compatible, world-anchored planar grid snapping so a
  coplanar wall remains one continuous lattice; mesh hits retain a resolved world normal.
  Perspective grid placement uses XZ and orthographic placement uses the displayed plane. Pivot
  uses the same cached placement owner with its original nearest-candidate policy preserved.
- Total-sweep layout includes both endpoints for a partial sweep and omits the duplicate seam for
  360 degrees. Copies rotate their orientation around the captured normal. Rings are source-major,
  use projected visible-pivot directions with deterministic on-axis fallback, and preserve each
  source as base-ring angle zero.
- Preview and commit consume the same radial-layout matrices. Every desired child-local transform
  is precomputed and round-tripped before mutation; parent scaling that would require shear rejects
  the whole request. Linear and Radial Array share the atomic transformed-duplicate owner for
  identity/property copying, batching, rollback, selection synchronization, and result assembly.
- Tool sessions reject stale selection, mode, visibility/selectability/lock, parent, identity,
  asset revision, material revision, or world-transform state. Source-inclusive output is capped at
  1024 elements, with dynamic UI maxima and consumed Shift+Wheel clamping; over-limit API requests
  fail without mutation.
- The pre-click quad marker is interaction-pane-local. Once locked, the marker and transform-only
  previews render in every pane. Apply remains visible but disabled until lock; Escape cancels and
  Space/Enter/Apply commits one history command.

Owners:

- `src/editor/modeling/radial_array_layout.hpp` owns renderer-neutral radial matrix generation.
- `src/editor/array/radial_array_tool_service.*` owns the placement/preview session and source
  snapshots; `src/editor/placement/viewport_point_placement_service.*` owns shared cached snapping.
- `src/editor/modeling/services/modeling_session_service.cpp` owns generalized atomic transformed
  duplication; `src/api/quader_modeling_api.*` owns the native public facade.
- Viewport tool catalogs, the native coordinator, Qt input/settings surfaces, render extraction,
  and localized resources adapt those editor-owned records without becoming modeling owners.

Verification:

- Focused radial layout, placement, lifecycle, API, history, 1024-boundary, shear, and preview/commit
  parity suites pass.
- Dependency-matrix and type-documentation architecture guardrails pass, and full CTest passes 6/6
  executables with 905/905 native app tests.
- Native `quader` v0.0.107 builds and boots with Fusion/Vulkan, four native swapchains, and no
  error/critical/fatal log entries. Both desktop capture paths returned an all-black frame and the
  Windows controller repeatedly failed to activate the freshly discovered Quader window, so the
  physical grid/tilted-face/multi-source/repeats/Shift+Wheel pass remains a human smoke check; those
  workflows are covered by the synthetic viewport, placement, tool-service, and API tests.

## 2026-07-14 - Qt Callback And Menu Ownership Follows Controller Lifetime

Invariant:

- A Qt functor that captures a non-`QObject` controller must use a `QObject` connection context
  whose lifetime is owned by that controller. A longer-lived widget or application object is not a
  safe context merely because it owns the signal sender.
- `QtMainMenu` explicitly releases its tracked top-level menus before rebuilding and when its
  controller is destroyed. `QMenuBar::clear()` alone does not release bar-owned menu hierarchies.
- `App` scopes its `QApplication::aboutToQuit` callback through the app-owned frame timer, so
  shutdown destroys the connection before the captured `App` object can become stale.

## 2026-07-14 - Native Numeric Tool Controls Preserve Precision And Active Edits

Invariant:

- Tool-setting synchronization updates numeric bounds and button state in place. It does not
  rebuild an unchanged control row, overwrite a slider while its handle is down, or overwrite a
  focused spin box while the user is editing; otherwise the next frame cancels native Qt capture
  and makes a drag appear to stick.
- The shared numeric slider keeps integral domains exact and ordinary floating domains linear.
  Wide floating domains use one monotonic signed nonlinear mapping with native style geometry, so
  useful low values receive meaningful physical travel without changing the modeled value range.
- A groove press is a continuous absolute scrub until release, while a handle press retains stock
  `QSlider` dragging. Repeat spacing remains reversible through zero; zero collapses outer rings but
  later nonzero edits regenerate them from the unchanged source snapshot.
- Tool UI limits come from operation schemas rather than duplicated panel constants. Edge Bevel
  width currently owns the `0..512` contract in its operation schema.

Owners:

- `src/ui/qt_tool_settings_number_slider.*` owns numeric-to-slider mapping and pointer behavior.
- `src/ui/qt_tool_settings_window.cpp` owns non-destructive widget synchronization.
- `src/editor/modeling/operations/operation_schema.hpp` owns operation parameter limits.

Verification:

- The merged focused pass succeeds for 17 placement/radial/slider/menu/API tests, 2 source
  guardrails, all 4 Edge Bevel runtime tests, and all 53 editor-metadata tests. The slider tests
  dispatch real Qt press/move/release events to both the groove and handle paths.
- Full CTest passes 6/6 executables, and native `quader` v0.0.108 builds successfully.
- Automated boot reaches the Qt event loop with four Filament native swapchains and no
  error/critical/fatal log entries. Its screen-copy step captured a different foreground window,
  so that PNG is not accepted as visual evidence; a final physical feel check remains appropriate.

## 2026-07-14 - Live Numeric Previews And Box Selection Are Revision-Gated

Task/context: Edge Bevel width/segment drags stalled the Qt event loop, and a live selection box
lost substantial frame rate as soon as it crossed a dense mesh. Tracing proved there was no Qt
signal loop or per-frame control reconstruction: each raw slider sample synchronously rebuilt one
topology preview, while every marquee sample rebuilt screen-space mesh candidates and repeatedly
resolved an unchanged component selection.

Correction (2026-07-18): the earlier box-selection regression and timing covered repeated direct
`box_select` calls whose hit set stayed effectively unchanged; it did not model a real viewport
drag accumulating new component hits. The viewport now follows a commit-on-release gesture:
mouse-motion samples update only the marquee and suppress hover, while release resolves the final
rectangle once against the press-time baseline. The direct selection-service caches remain useful
for API callers, but their counters are not evidence that viewport drag motion is cheap.
This cadence matches Blender's box gesture: mouse movement updates/redraws only the rectangle, and
the 3D-view selection operator executes once when the modal gesture commits.

Invariant:

- Slider visuals remain immediate, but continuous numeric drags publish only their latest value at
  a 16 ms presentation interval. Release synchronously flushes the exact final value. Spin-box and
  keyboard edits remain immediate, and a retained active editor is not overwritten by frame
  synchronization.
- A viewport box-selection drag is presentation-only until release. Motion must not project
  rectangle candidates, resolve hits, mutate SDK or scene selection, advance selection revisions,
  rebuild selection overlays, or refresh selection-dependent Qt panels. Release performs one
  selection transaction using the baseline captured at press time.
- Picking geometry is keyed only by modeling content, object-map identity, world transforms,
  visibility/selectability, mesh binding, and camera state. Selection-only and broad scene
  revisions never retransform unchanged mesh vertices.
- Rectangle candidates are projected once for a stable camera and geometry state. The builder
  emits only the candidate kind needed by the active selection mode, reuses the topology universe,
  and broad-phases projected face/edge/object bounds before exact crossing tests.
- A changed marquee endpoint with the same baseline, edit operation, projected candidates, hit set,
  and current selection revision does not reconstruct selection state, resynchronize the scene, or
  invalidate overlay caches. Camera/content/transform/visibility/selectability/object-map changes
  invalidate the appropriate cache immediately.
- Selection-overlay construction indexes document vertices/faces and selected element membership
  once per build. Triangle-only consumers do not construct full source-wire and face-border
  payloads that they immediately discard.
- `SceneDocument::visibility_revision()` includes selectability changes and remains stable for
  no-op assignments, so visibility-sensitive caches neither miss real changes nor churn on repeats.

Owners:

- `src/ui/qt_tool_settings_window.cpp` owns latest-value scheduling and final-value flushing for all
  descriptor-driven numeric tool controls.
- `src/editor/viewport/viewport_selection_interaction.*` owns the commit-on-release box gesture and
  marquee-only motion path.
- `src/editor/modeling/services/modeling_selection_service.*` owns transformed geometry,
  screen-space rectangle candidate, universe, and hit-resolution caches.
- `src/editor/modeling/runtime/session_selection_overlay.cpp` and portable polygon
  `build_overlay` own per-build ID/membership indexes and requested overlay payload filtering.
- `src/editor/modeling/runtime/modeling_object_runtime_map.*` and `src/scene/scene_document.*`
  publish the narrow identity and visibility revision gates used by picking.
- `src/editor/interactions/select_tool.cpp` owns exact containment/crossing semantics after the
  cached broad phase.

Verification:

- In Debug, one raw Edge Bevel width move fell from 6.6-7.3 ms to 0.47-0.51 ms; three segment moves
  fell from 8.2-8.6 ms each to 0.15-0.21 ms each. The single final preview flush remains explicit
  and measured at 6.4-6.7 ms on the simple cube fixture.
- On a 24x24x24 segmented cube, the earlier direct-service benchmark reported 32 stable
  face-marquee calls falling from 2,324 ms to 121 ms, a 19.2x reduction. This characterizes cache
  reuse for repeated API calls, not the current viewport gesture.
- The corrected viewport regression expands across the same dense cube for 24 distinct motion
  samples and materializes the overlay after every selection revision. Before the correction it
  took 58,620 ms in Debug, committed all 24 samples, and selected 3,456 faces before release.
  Commit-on-release plus linear-time overlay lookup/payload construction takes 2,969 ms end to end
  (19.7x faster); all motion samples perform zero rectangle projections, hit resolutions, or
  selection revisions, and release performs exactly one of each.
- On the fixed 100,344-triangle Release benchmark, object overlay construction fell from
  3,893.111 ms to 1,512.289 ms and all-face overlay construction fell from 6,386.490 ms to
  2,352.326 ms. These are one-shot CPU construction costs, not per-motion work.
- All 8 native Qt numeric-slider tests, 7 focused scene/box/preview tests, and all 287 runtime
  modeling tests pass. Full CTest passes 6/6 executables, and native `quader` v0.0.109 builds and
  reaches the Qt/Fusion/Vulkan event loop with four swapchains and no logged errors. The automated
  screenshot captured a different foreground application, so it is not accepted as visual proof.

## 2026-07-14 - Linear Array Uses One Shared Span Across Multiple Sources

Invariant:

- Interactive Linear Array accepts one or more visible, unlocked modeling meshes in Object mode.
  Sources are ordered by entity ID with the prior primary last; one A/B handle remains anchored to
  that primary, and its world displacement is applied independently to every source. This preserves
  the selected formation and makes the final primary-derived copy active after commit.
- Preview and commit consume the same renderer-neutral linear-layout matrices. One preview record is
  published per source, originals are omitted, and commit uses one atomic transformed-duplicate
  batch and one history item while retaining each source's parent, identity, material, durable
  properties, and custom pivot without copying children.
- Every source snapshot guards selection, primary, UUID, parent, visibility, locking, runtime map,
  mesh/material identity and revisions, and world transform. A change to any source cancels the
  whole session before mutation.
- Linear Array is capped at 1,024 source-inclusive elements. The count setting maximum is dynamic,
  and Shift+Wheel Up/Down resolves through `activeTool.linear_array`, accumulates a signed semantic
  delta, clamps at the current limits, and remains consumed instead of also zooming.
- Array preview revisions are monotonic across tool sessions. Unchanged frames borrow the cached
  transform arrays and skip render extraction/submission; asset or renderer submissions, tool
  edits, cancellation/reactivation, and source changes invalidate that fast path. Source validity
  scans are gated by session generation plus scene, asset, runtime-object-map, and selection-mode
  revisions, so preview and handle reads do not repeatedly walk every source on a stable frame.
- Visible-pivot policy is part of Radial Array source validity. Custom-pivot changes and
  hierarchy/bounds changes cancel the captured preview before commit can recompute a different
  pivot.

Owners:

- `src/editor/modeling/linear_array_layout.hpp` owns deterministic shared-span world matrices.
- `src/editor/array/linear_array_tool_service.*` owns source ordering, snapshots, the shared handle,
  dynamic limits, preview caching, and count adjustment.
- `src/editor/modeling/services/modeling_session_service.cpp` owns atomic source-major duplication;
  viewport input and render adapters only translate semantic actions and preview records.
- `src/api/quader_render_api.*` and `src/render/scene_render_bridge.*` own revision-gated native-array
  preview submission and renderer-lifecycle invalidation.

Verification:

- The focused pass succeeds for 14 Linear/Radial layout, stale-source, render-cache, multi-source,
  API, and Shift+Wheel binding tests. It includes cancel/reactivate without an intermediate render
  frame, asset revision invalidation, exact tiny-transform cancellation, and visible-pivot policy
  changes.
- The affected workflow and final full CTest both pass all 6 executables; the complete native test
  executable passes 922 tests. The full Debug preset and native `quader` v0.0.110 build succeed.

## 2026-07-14 - Project Workspaces Preserve Identity Across UI, Storage, APIs, Rendering, And QDR

Task/context: The one-file project model became a folder-backed project with multiple resident
scene sessions and binary scene persistence. The transition exposed several boundaries where a
valid local identifier or revision is not sufficient without its owning project/scene context.

Invariant:

- Install a Qt item-view model before calling section-indexed `QHeaderView` operations such as
  `setSectionResizeMode(logicalIndex, ...)`. Before the model supplies sections, Qt has no valid
  logical-to-visual mapping and its Debug runtime asserts in `adjustedVisualIndex`.
- Do not reset the recent-project model when the projected entries are unchanged. Home is refreshed
  every app frame, and an unconditional model reset clears the current row between pointer events,
  disabling removal and preventing double-click/Return activation from completing.
- Project replacement is staged: a failed create/open leaves the current workspace, tabs, active
  session, and identities intact. Structural workspace mutations update the manifest atomically;
  the in-memory manifest is committed only after the write succeeds, and tab order, activation,
  scene path/ID, and catalog mutations roll back on failure. Path uniqueness includes physical
  identity through canonical/equivalent locations, so case variants and in-root symlink/junction
  aliases focus an existing tab or reject a colliding Save As instead of creating parallel owners.
- Stable public API facade objects resolve the active `SceneSession` for every invocation; they do
  not retain a prior scene owner. Entity/modeling references and `QuaderObjectHandle` values carry
  their `SceneSessionId`, so an inactive, closed, or generation-reused scene cannot accidentally
  address an equal numeric ID in another session.
- Every renderer-neutral cache boundary uses `RenderSceneBindingKey`, composed from the packed
  `SceneSessionId`, project activation generation, and render binding revision. Scene snapshots,
  sync early-outs, material/texture bindings, overlays, and proxy reuse must compare this key so
  two scenes with equal document revisions and equal entity/asset numbers cannot reuse each
  other's records when the shared viewport switches tabs.
- A scene-binding change is also a strict Filament resource-lifetime boundary. Destroy tool-preview
  proxies and retire every outgoing mesh proxy/material instance before releasing scene-qualified
  textures; `FEngine::prepare` commits all live surface material instances, including detached
  ones, and rejects any texture pointer that has already been destroyed. Without explicit texture
  reference tracking, replaced texture revisions remain cached until the safe scene-bind or
  shutdown boundary instead of being evicted while paced proxy retirement is pending.
- Constructing a `SceneSession` from an existing `SceneContentDocument` must immediately call
  `ModelingSessionService::reset_from_scene()` after all session services are wired. QDR hydration
  restores scene entities, authored mesh assets, and modeling components, but rendering those
  owners does not populate the separate portable modeling runtime or its entity map. An installed
  session with an empty runtime renders loaded meshes while viewport object picking silently misses
  every modeled entity.
- Every `SceneSession` owns an independent viewport workspace. A new Perspective camera keeps the
  canonical controller pose (eye `6,5,8`, distance `sqrt(125)`); the host's 64-unit focus distance
  is for axis/orthographic resets and must not overwrite Perspective framing. A positive distance
  alone is not a sufficient regression assertion because it can still reduce the finite grid to a
  barely visible patch.
- QDRB separates logical mesh identity from immutable geometry payload identity. Snapshot encoding
  emits one `QDRG` chunk per semantically unique geometry and lets independent mesh assets refer to
  it. Reading owns one shared immutable file buffer; uncompressed chunks borrow spans from it and
  compressed chunks decompress/checksum once on first access. Hydration decodes and compiles each
  referenced unique geometry once, while unreferenced geometry chunks remain untouched.
- Portable modeling QDR is binary-only: its public APIs accept and return byte spans/vectors, and
  its one compiled owner is `src/sdk/core/modeling/src/qdr_document.cpp`. Do not restore the
  retired JSON document/root/migration codecs, runtime compatibility adapters, mirrored internal
  headers, or `quader_qdr_shared_io`/`quader_qdr_document_io`/`quader_qdr_runtime_io` targets.
  Native durable replacement belongs in `src/io/qdr/qdr_atomic_write.cpp`; JSON emitted by
  `qdr_dump --json` is write-only developer inspection output, never a persistence reader.
- Native tests that exercise `RecentProjectStore` through editor APIs replace `LOCALAPPDATA` in the
  test executable's custom main before any project/API construction, reject an unsandboxed run,
  and remove only that isolated profile afterward. Tests must never write disposable project paths
  into the user's real recent-project file.

Owners:

- `src/ui/qt_project_home_page.cpp` owns recent-project view/model construction order.
- `src/editor/session/project_session.*` and `src/io/project/` own transactional workspace state,
  atomic manifests, and physical project-path identity.
- `src/api/` and `src/editor/session/scene_session.*` own per-call active-session resolution and
  scene-qualified public references. `SceneSession::rebuild_services` also owns the invariant that
  an existing document's modeling runtime and entity map are hydrated before the session is
  exposed.
- `src/render/render_scene_binding.hpp`, render extraction/sync/binding caches, and
  `src/api/quader_render_api.*` own activation-safe renderer identity. `SceneRenderBridge`,
  `SceneRenderSyncService`, and `TextureBindingService` own the corresponding proxy-before-texture
  destruction order.
- `src/viewport/viewport_workspace_service.*` owns canonical per-scene camera initialization;
  `tests/quader_tests_main.cpp` owns native-test profile isolation.
- `src/io/qdr/qdr_scene_binary_codec.*`, `qdr_binary_container.*`, and the portable modeling QDR
  byte codec own geometry deduplication, backing-store lifetime, lazy chunk decoding, and the
  binary-only SDK/native format boundary.

Verification:

- `QtProjectWorkspaceTests` constructs the Home page under a Debug Qt application, protecting the
  model-before-header ordering that previously asserted at startup. It also sends real Qt mouse
  and Return events and re-submits an unchanged recent projection, proving selection survives the
  app refresh loop.
- `ProjectSessionWorkspace` and `ProjectWorkspaceStorage` cover manifest-write rollback, failed
  project replacement, physical path aliases, per-call cached facades, and wrong/closed scene
  handle generations. The save-close-reopen selection regression creates two spatially separated
  modeled boxes and ray-picks both after reopening, proving the installed runtime/map is populated
  rather than merely checking that the scene still renders.
- `RenderExtractionTests` cover binding-key propagation and equal-revision cross-scene cache
  isolation, full retired-proxy draining before texture invalidation, preview cleanup ordering, and
  safe retention of old texture revisions. `QdrBinarySceneCodecTests` cover equivalent-geometry
  deduplication, copy-on-write splitting, and unreferenced compressed geometry remaining
  undecoded.
- Perspective reset tests compare the complete camera state and pose, and the GPU smoke verifies a
  newly created tab renders the normal grid before any Home command. The saved-textured-scene to
  New Scene GPU regression also keeps running at presentation rate, renders the new tab's grid,
  survives repeated tab cycling, and produces no new Sentry dump. The full native test process
  verifies its recent-project store lives below the isolated test profile.
- Binary-boundary cleanup passes all 32 SDK consumer tests, 512 modeling-domain tests, and 189
  guardrails; the SDK-only install/external-consumer configure/build test also passes without the
  former SDK-bundled JSON dependency.
- The refreshed standard benchmark reports are
  `build/qdr-scene-workspace-standard-{debug,release}.json`. The repetitive fixture is 156,112
  bytes versus the 2,703,035-byte JSON baseline (94.22% smaller, 17.31x), with load p50 improving
  from 3,715.057 ms to 63.134 ms in Debug (58.84x) and from 229.369 ms to 3.806 ms in Release
  (60.27x). The referenced/unreferenced fixture contains 12 unique payloads but decodes and compiles
  only the five referenced payloads; seven remain lazy.

## 2026-07-14 - Project Materials Are External Stable Assets

Invariant:

- Project materials and textures remain external project files. Scenes persist `MaterialReference`
  UUIDs and dependency path hints, never QMAT or PNG payloads; embedded numeric materials remain a
  separate compatibility case for standalone and QDR 1.0 scenes.
- `ProjectMaterialCatalog` validates and deterministically scans project QMAT files. The reserved
  default QMAT/albedo pair is provisioned transactionally, is fatal when corrupt or conflicting,
  and is never overwritten. QPROJ v2 owns the active material UUID and persists it before the
  in-memory active material changes.
- Material/render/preview caches are qualified by project identity, catalog/material content
  revision, and preview revision. A texture-byte cache may not key only by texture UUID because
  separate materials can legally point that UUID at different project paths.
- Slot zero is represented by the component's primary `MaterialReference`; nonzero face slots are
  stored in `material_slots`. Object-mode Apply Material resets every face to zero and removes
  obsolete overrides; Face mode reuses the lowest matching slot or allocates the lowest free slot.
  One command captures one material for undo/redo across every selected object.
- Public `edit.apply_material` must route to the custom `assign_material` command tool. The legacy
  `apply_active_material` host descriptor remains face-oriented metadata and will incorrectly make
  the Object-mode shortcut unavailable if used as the public command's execution tool.
- The retained Material Browser consumes `QuaderProjectAssetsApi` only. It is a parent-owned
  `Qt::Tool`, shares one model/proxy/selection across Grid and List, resets search when shown, and
  persists only geometry, view mode, and tile size.
- Hydrating a saved scene must establish mesh-sync baselines without marking it dirty; otherwise
  restored selection/picking can regress even though the rendered mesh is visible.

Owners:

- `src/assets/material_document.*`, `src/io/project/qmat_codec.*`,
  `project_material_catalog.*`, and `default_project_material_provisioner.*` own material data,
  validation, discovery, and transactional defaults.
- `src/editor/session/project_session.*`, `src/api/quader_project_assets_api.*`, and
  `src/io/qdr/qdr_scene_binary_codec.*` own active state, public intent, and stable scene
  references/dependencies.
- `src/render/project_material_snapshot_resolver.*`, `src/app/app.cpp`, and
  `src/ui/qt_material_browser_window.*` own qualified rendering, encoded preview bytes, and Qt
  presentation respectively.

Verification:

- QMAT/catalog/provisioning, manifest migration, QDR 1.0/1.1, dependency failure, creation/apply,
  undo/redo, Qt model/lifecycle, and project-replacement tests pass in the full six-target CTest.
  The C++ style gate passes all 517 translation units and the native Debug `quader` v0.0.113 build
  succeeds.
- The Vulkan/Fusion smoke creates the default QMAT/albedo layout, opens Grid/List/Search, activates
  the material, creates and applies it in Object mode, saves/reopens a scene, ray-picks its restored
  mesh, creates a second scene without a crash, switches resident tabs, and restores both saved
  tabs in order with the prior active scene. The final log contains no warnings, errors, asserts,
  or Sentry envelopes.

## 2026-07-15 - Scene-Local Instances And Shared Geometry Batching

Invariant:

- A formal instance is one hidden scene-local definition root containing canonical Mesh/Empty
  members plus one or more leaf occurrence roots. Occurrences own only outer scene state and refer
  to the definition; definitions cannot nest, cross scenes, become empty while referenced, or be
  destroyed while a surviving occurrence refers to them.
- Instance editing is transient `SceneSession` state. Entry clears the occurrence selection and
  scopes selection/picking/tools to canonical members displayed through the active occurrence
  transform. Exit restores Object mode and the occurrence selection. Tab switches retain each
  scene's context; invalid occurrence/definition state performs the same automatic cleanup.
- Create/Flatten are document commands. Duplicate, Alt-drag, arrays, and same-scene paste preserve
  the definition link. Cross-scene paste clones an independent destination definition. Flattening
  the final occurrence adopts the canonical members; partial flatten makes independent COW member
  copies so surviving occurrences remain linked.
- `PolygonDocument` and the native runtime wrapper share immutable authored topology with separate
  selection. Selection and no-op operations do not detach; the first real topology edit publishes
  a new content identity. Scene hydration must preserve that shared pointer instead of copying the
  SDK document.
- Logical mesh assets keep distinct `AssetId`s while sharing immutable compiled geometry payloads.
  QDR snapshots, picking caches, render snapshots, and history pin those payloads. QDR scene schema
  3 stores definition/occurrence components, keeps schema 1/2 readable, memoizes geometry identity,
  and restores shared payloads across logical assets.
- Base meshes, formal occurrences, and compatible ordinary duplicates are planned by primitive
  payload/material/pass state and submitted through explicit Filament `InstanceBuffer`s. The
  device instance limit applies per compatible chunk, not globally: a cap of 64 produces
  `64 + 64 + 64 + 8` for 200 participants. Transform-only changes update instance transforms and
  aggregate bounds without geometry uploads.
- Selected occurrences retain the existing orange/yellow instanced wire and add the transient
  muted-red `#C95C58` fill at 35% opacity. The depth-tested/no-depth-write fill draws before the
  wire and never mutates materials, persistence, history, or dirty state. The wire is generated
  from authored polygon boundaries, never render triangles, so transforming an occurrence cannot
  expose triangulation diagonals. Persistent overlay caches include occurrence transforms in their
  signatures and active-member overlays compose through the edited occurrence transform.
- Shift-drag and Alt-drag of an occurrence create one linked occurrence before movement, select
  that new occurrence immediately, and leave the source stationary. Undo restores the source and
  its selection. Scene-aware selection summaries keep an occurrence-only selection actionable so
  the viewport RMB menu exposes Flatten even when no runtime modeling object is selected.
- Instance edit mode applies a whole-viewport background treatment and renders context geometry
  with a configurable translucent/desaturated color; the active definition remains normally lit
  and all regular modeling/component overlays remain available. Both edit background and context
  colors are user viewport settings.
- A single-root custom name survives Create Instance and final Flatten. Partial flatten transfers
  the occurrence name to its independent result. Inspector instance-name edits commit on Enter or
  focus loss. Linked flatten may temporarily extract only a redundant direct definition member;
  the scene invariant still rejects extracting the last canonical member.

Owners:

- `src/editor/instances/scene_instance_service.hpp` owns instance commands and edit context;
  `src/editor/instances/project_clipboard_service.*` owns project-scoped linked/cloned clipboard
  behavior; `src/scene/scene_document.*` enforces the live graph invariants.
- `src/sdk/core/modeling/.../polygon_document.*`,
  `src/editor/modeling/runtime/cow_polygon_document.hpp`, `src/assets/mesh_asset*`, and
  `src/editor/modeling/services/modeling_mesh_sync_service.*` own authored/compiled COW state.
- `src/render/instance_batch_planner.*`, `instance_renderable_cache.*`,
  `mesh_renderable_factory.*`, and the render-owned instance/wire overlay caches own explicit GPU
  batching, device chunking, shared buffers, transform updates, and diagnostics.
- `src/io/qdr/qdr_scene_binary_codec.*` owns schema-3 persistence and validation;
  `src/ui/qt_scene_workspace.*` owns the separate lower breadcrumb row while the existing scene tab
  bar remains unchanged.

Verification:

- Focused SDK/COW, scene-instance, clipboard, array, selection/edit, render planner/overlay,
  malformed/round-trip QDR, guarded double-click, authored-edge wire, Shift/Alt duplication,
  naming/focus-loss, context-menu, settings-color, and edit-overlay tests pass. The complete native
  executable passes 1085/1085 tests; repeated `SceneInstance*` and `EditorApiTests.*` runs pass
  260/260 and 770/770 respectively.
- The complete six-entry CTest preset and full Debug build pass, including SDK-only configuration,
  SDK consumer/parity, modeling-domain, guardrail, native app, and all three modeling performance
  benchmark targets. The final native v0.1.0 build and Vulkan/Fusion smoke are recorded in the task
  handoff.

## 2026-07-14 - Standard Material Editing Uses Typed Draft And Render Boundaries

Invariant:

- QMAT v2 identifies `materialType: standard` and versions its typed payload independently from
  common material identity. QMAT v1 remains an implicit Standard Material input, while future
  Graph Materials extend the tagged document and editor registry without reusing compatibility
  fields or changing UUID-based scene references.
- `StandardMaterialEditSessionService` owns one generational draft, staged source images, decoded
  preview cache, optimistic source revision, and dirty checkpoint. Imported texture bytes remain
  untouched until Save; QMAT/catalog/manifest installation is transactional, and rollback restores
  the original QMAT and removes only texture files created by that attempt.
- Public material APIs expose Quader value types instead of asset-owner definitions. Invalid public
  enum values fail rather than falling through to Albedo/OpenGL. Retargeting publishes the closing
  document identity before the replacement opens, and saved/closed/assets/active events retain
  their project, material, session, and path payloads.
- Both saved materials and unsaved drafts adapt into `RenderMaterialEvaluationSnapshot`, whose
  tagged payload contains Standard PBR data now and reserves Graph dispatch without admitting Qt,
  QMAT, project paths, or editor owners into render code. Project/material/texture identities and
  content revisions qualify renderer caches.
- The retained Standard Material preview is a real isolated Filament surface. Its QWindow container
  exists before renderer initialization, hidden-window initialization does not require an immediate
  swapchain, and SurfaceCreated/expose/render paths retry swapchain creation. Scene viewports and the
  visible preview submit within one presentation accounting cycle; minimized or unexposed previews
  do not request high-refresh continuous frames.
- Preview navigation is semantic and isolated: MMB tumbles the test object, Shift+MMB pans,
  Ctrl+MMB rotates the environment rig, the wheel zooms within clamps, and MMB double-click resets
  pose/pan/zoom/environment rotation. Scene selection/tools never enter the preview. Environment
  selection and background visibility are retained per-user presentation state, defaulting to the
  packaged Lightroom environment with its background hidden.
- Preview tumble rotates the inspected object rather than the camera, so its transform must be the
  inverse of the main viewport camera orbit: both object yaw and pitch add their screen-space
  deltas. An upward drag has negative Y, producing negative object X rotation and moving the
  camera-facing `+Z` surface upward. Tests must assert that transformed surface direction rather
  than inferring visible motion from the pitch scalar alone.
- The inspection rig composes object rotation as `qPitch * qYaw`, with screen-X pitch outside the
  inner vertical-axis yaw. This keeps the sphere's displayed local-up axis invariant during a
  horizontal-only MMB drag after pitching; the generic Euler helper uses the opposite order and
  must not be used for this preview pose.
- One Standard Material texture transform is shared by all four samplers. QMAT v2 stores its
  optional center/scale/offset/rotation fields additively, older files default to identity, public
  APIs mutate the complete typed value, and both unlit/lit render paths evaluate
  `center + rotate((uv - center) * scale) + offset`. Normal strength is a signed `[-16, 16]`
  control throughout validation, draft editing, preview, and scene rendering.
- The retained stock-Qt editor uses compact sliders, left-elided paths, stock icon actions, and
  square decoded texture previews. Clicking a valid preview opens one retained tool inspector with
  zoom/pan/reset and filename, detected container/RGBA8 interpretation, color space, dimensions,
  and encoded byte size. The browser, editor, and inspector tool windows omit maximize behavior.
- Staged texture rows display only the native filename; their full source path remains diagnostic
  metadata. The albedo color swatch is visually text-free while retaining an accessible Albedo
  name and the localized color-dialog title.
- Every Standard Material slider is integer-backed at 100 steps per unit with an explicit single
  step of one, so scalar and texture-transform dragging can emit only exact `0.01` increments.
- Standard Material File actions are text-only like the main app menu. Every texture Browse button
  copies one cached Fusion `SP_DialogOpenButton` icon, and every Clear button copies one cached
  `SP_DialogCancelButton` red-X icon; do not substitute directory or window-close pixmaps.
- Quader installs `QLocale::c()` before constructing `QApplication`. This is the process-wide
  numeric-input contract: every current and future Qt decimal control and default `QLocale()`
  formatter uses `.` regardless of the Windows regional locale, while translated UI catalogs
  remain independently selectable.
- `QtStandardMaterialPreviewImageCache` performs the required RGBA8-to-`QImage` ownership copy once
  per material/edit-session/slot content revision. Scalar and transform refreshes reuse that image
  and its scaled icon; replacing or clearing the inspected texture updates or hides the retained
  inspector without resetting its zoom/pan for unrelated material edits.
- Project image ingestion preserves exact encoded source bytes, converts readable still images
  (including the packaged 16-bit SDR PNG) to immutable RGBA8 for rendering, and rejects animation,
  HDR/EXR or floating-point images, malformed data, inputs over 64 MiB, and dimensions over 8K.

Owners:

- `src/assets/project_material_document.hpp`, `src/io/project/qmat_codec.*`, and
  `src/editor/material/standard_material_edit_session_service.*` own typed QMAT data, staged edits,
  and atomic persistence.
- `src/api/quader_standard_material_documents_api.*` owns public intent and lifecycle events;
  `src/ui/qt_standard_material_editor_window.*` and `src/ui/ui_system.*` own retained stock-Qt
  presentation and dirty-draft decisions.
- `src/render/material_evaluation_snapshot.hpp`, `material_preview_renderer.*`,
  `material_preview_mesh_library.*`, and `src/ui/qt_material_preview_surface.*` own the typed render
  seam, isolated preview scene, deterministic test meshes, and native child-surface lifecycle.

Verification:

- Version `0.0.115` passes the affected UI/guardrail workflow (39 guardrails and 280 focused native
  tests), the complete six-target CTest preset, the 532-file C++ style gate, and the final native
  `quader` build. A native launch log confirms Fusion, Vulkan, Filament materials, the scene bridge,
  and an additional native swapchain initialized without warnings/errors; Windows accessibility
  also exposes the Home surface and `v0.0.115`. The Windows control helper could not activate the
  hidden startup window on the active desktop and captured black pixels, so visual confirmation of
  environment/background output and native title-bar double-click behavior remains a manual smoke
  item rather than claimed automated proof.

## 2026-07-15 - Primitive Creation Is One Persistent Multi-Shape Transaction

Invariant:

- `Create Primitive` is the sole primitive viewport tool and activates with Shift+B. Its first
  Tool Settings control is the `primitive.type` dropdown; selecting Box, Cylinder, Sphere, Cone,
  or Torus rebuilds the remaining controls from that type's settings descriptor. The chosen type
  persists across tool sessions just like each type's numeric and toggle settings.
- Cone exposes Sides (`3..128`), Base Cap, and Flip Faces. Torus exposes Major Segments and Minor
  Segments (`3..128`), Tube Radius Ratio (`0.01..0.49`, default `0.2`), and Flip Faces. The
  footprint supplies the torus's oriented outer X/Y/Z envelope; the ratio controls tube thickness
  relative to the outer X/Z radii, and its upper bound preserves a positive center hole.
- Stable primitive values remain Box `0`, Cylinder `1`, and Sphere `2`; Cone `3` and Torus `4` are
  append-only additions. `primitive_creation_default_name` is the one scene-name source used by
  the service, public tools API, and viewport naming path.
- Each released footprint creates a shape and keeps that newest shape live-editable while it
  remains the sole active object at the same selection revision. Numeric, toggle, and primitive
  type changes rebuild that object in place, preserving its modeling/entity identity and
  footprint. Creating another shape or any other selection transition permanently freezes the
  prior live shape; later setting changes then affect only future shapes.
- Accepted shapes do not create editor-history entries independently. Apply or Enter finalizes all
  shapes as one before/after modeling memento, while Cancel, Escape, any footprint-preview failure,
  or any per-shape creation failure restores the session-start memento and exits the tool.
- Primitive Apply/Cancel rebases the session boundary onto the current non-primitive document so
  unrelated scene edits made while the tool is active survive in their original history order.
  Public selection changes remain transient during the session, modeling-content mutations are
  rejected, and document commands may not target a session-created entity until Apply commits it.
- Historical mesh projection must reuse a modeling object's remembered mesh asset when its scene
  entity needs recreation. Restore cleanup removes only assets displaced by the restored state;
  never sweep every asset stored in a memento, because an unreferenced snapshot can still be owned
  by a deeper document-history entry.
- The scene preview checkpoint begins and finishes exactly once per primitive session. Rollback
  first observes the current scene revision, then finishes it as restored-by-cancel so unrelated
  intervening scene edits remain dirty and no stale post-failure projection can be published.
- Restoring the session-start memento must retain the native session's object/light allocation
  high-water marks. A shape created after cancel must receive an ID above every transient primitive
  accepted and then retired by that session, preserving stale public-handle invalidation.
- The shared two-stage footprint interaction must preserve the draw-time snap mode and reactivate
  after each accepted shape. Settings cannot mutate while a footprint is in progress.
- Primitive completion uses dedicated semantic Apply/Cancel commands so Enter and Escape remain
  available from Tool Settings numeric fields without enabling unrelated global shortcuts during
  text input. Those commands are owner-only while a primitive session is active and must never
  fall through to generic modeling-operation previews. Their viewport-scoped bindings retain the
  explicit Tool Settings text-input exception.
- Generic modal cancel routes through the native viewport-tool owner before generic operation
  previews. Public tool status preserves the exact Box/Cylinder/Sphere/Cone/Torus kind, and every
  terminal failure requests active-tool synchronization after the primitive owner exits.

Owners:

- `src/editor/modeling/services/primitive_creation_tool_service.*` owns dynamic primitive kind,
  persistent settings, the selection-revision-gated live shape, the session memento, rollback,
  and the single Apply history transaction. `ModelingSessionService::replace_mesh_document` and
  `EditorSession::replace_object_document` perform validated in-place topology replacement without
  changing selection or object identity.
- `src/editor/selection/editor_selection_service.*`,
  `src/editor/commands/document_command_service.*`, and the public modeling mutation runner enforce
  the modal transaction boundary. `modeling_mesh_sync_service.cpp` preserves remembered historical
  mesh identities and limits cleanup to genuinely displaced assets.
- `src/editor/tools/base/two_stage_box_tool.*` owns the reusable footprint gesture;
  `src/editor/tools/native_viewport_tool_coordinator.*` and `src/api/quader_tools_api.*` adapt
  editor intent; `src/ui/qt_viewport_panel.cpp` and `src/ui/qt_command_event_filter.cpp` present
  settings and route semantic completion.
- `src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_primitives.cpp` owns canonical
  subdivided box, cylinder, cone, full-sphere, hemisphere, torus, cap, material, and flipped-face
  geometry.

Verification:

- Cone/Torus topology, bounds, winding, cap, validation, material, SDK/API, persistent lifecycle,
  live morphing, one-entry history, dynamic Qt settings, stable values, and canonical naming
  regressions pass. The SDK consumer suite passes 66/66, the focused primitive/API/Qt suite passes
  15/15, and the complete `vs2026-debug` CTest preset passes 6/6, including native app, modeling
  domain, SDK consumer, SDK parity, SDK-only configure/build, and 350 guardrails.

## 2026-07-15 - Primitive Placement Uses Scene-Aware Surface Hits

Invariant:

- Create Primitive hover and footprint-begin resolve surfaces through the
  native scene-aware picking projection. Hidden or non-selectable entities are
  excluded, world and hierarchy transforms are applied, and back-facing/open
  mesh planes remain valid construction surfaces.
- The runtime command receives both an optional resolved world-space hit and
  an explicit resolution flag. A host-resolved miss must fall back to the
  ground plane and must never re-query raw modeling geometry, which may still
  contain hidden objects or authored coordinates from before a scene transform.
- Continuous hover uses the selection owner's cached local face geometry plus
  object transforms; it does not rebuild world-baked triangles every frame.

Owners: `ModelingSelectionService::pick_surface_hit`,
`RuntimeToolCommandService`, and the Box runtime command seam. Regressions live
in `PrimitiveCreationToolServiceTests` and `QdrModelingRuntimeCommandTests`.

## 2026-07-15 - Cross-Domain Stability And Transaction Boundaries

Invariant:

- SDK object identities belong to the lifetime of `ModelingSession`, not an undo snapshot. Undo may
  restore an old identity, but branching after undo never reuses it. Session content, selection,
  and preview-payload cursors are monotonic outside undo state; mesh sync emits coalesced exact
  dirty records and deletion tombstones for every add/edit/remove/undo/redo transition without
  recompiling meshes for selection-, name-, or overlay-only changes.
- Failed or unchanged SDK document/selection operations do not mutate selection, history, redo, or
  revision state. Checked facades use a per-context error epoch so `StoreLastError` fallbacks cannot
  be wrapped as successful `Result` values and stale prior errors cannot poison a later success.
  Batch callbacks roll back for reported failures and thrown standard or unknown exceptions.
- Native `EditorSession` history and service-memento restores preserve the session-lifetime
  high-water marks for object and light IDs while advancing content/selection revisions without
  unsigned wraparound. Undo branches and canceled multi-shape sessions therefore cannot recycle an
  ID into a stale public mesh handle. A changed terminal preview cancel uses this same restore
  contract; exact provisional preview replay continues to use `set_state` while the preview is
  active.
- Project Save As writes and validates a private same-directory QDR staging file, atomically
  publishes it while retaining any prior destination as a rollback backup, and only then persists
  the prepared candidate manifest. Windows holds a read handle that denies write/delete across the
  manifest window; POSIX has exact seal verification and parent-directory flushes but no equivalent
  cross-process guard. Manifest failure restores the prior scene. A post-manifest failure first
  restores the prior manifest and then rolls back the scene; if prior-manifest recovery itself
  fails, the published scene/backup pair is deliberately preserved, its paths are reported, and
  the candidate live identity/path is installed as dirty for repair. Initially absent targets use
  atomic no-replace publication, and successful cleanup failures remain warnings.
  Windows `ReplaceFileW` provides process-visible atomic replacement and recovery, but its
  write-through flag is unsupported and there is no supported parent-directory fsync here; do not
  claim scene-before-manifest power-loss durability on Windows.
- Scene component writes return acceptance truth, reject protected targets, and keep revisions
  stable for identical values. Creation commands reject non-finite transforms before creating an
  entity or history entry. In-place mesh payload replacement has one explicit notification path,
  and mesh synchronization prevalidates all targets before mutating bindings. The scene-owned
  selection revision also advances when the active selection changes Object/Light classification.
- Native modeling camera conversion and scene-owned selection summary projection each have one
  adapter. Logger consumers hold synchronized shared-ownership snapshots across shutdown, and fatal
  UTF-8 conversion sizes its wide buffer without a terminator overrun.

Owners:

- `src/sdk/core/modeling/src/session.cpp`, `public_api_detail.hpp`, and
  `mesh_sync_io_api.cpp` own session identity, monotonic change publication, preview cursors,
  checked-facade diagnostics, and renderer-neutral delta materialization.
- `src/io/qdr/qdr_atomic_write.*` owns private staging, reversible publication, exact verification,
  rollback, and cleanup warnings; `src/editor/session/project_session.cpp` owns the
  QDR/manifest/live-session transaction and recovery order.
- `src/scene/scene_document.*`, `src/editor/commands/document/`, and
  `src/editor/selection/scene_selection_summary.hpp` own truthful scene mutation, selection-summary
  revision semantics, and explicit render invalidation. `modeling_mesh_sync_service.cpp` owns the
  all-target prevalidation and binding projection boundary.
- `src/editor/modeling/modeling_viewport_camera_adapter.hpp` owns native-to-runtime camera
  conversion; call sites must not recreate local converters.
- `src/editor/modeling/runtime/session_bookkeeping.cpp` owns native historical restoration,
  allocator high-water marks, and saturating revisions. `ModelingSessionService` delegates memento
  replay to that owner instead of rewriting revision rules.
- `src/core/logger.*` and `src/platform/fatal_error_handler.*` own the bounded native safety fixes.

Verification:

- The SDK consumer suite passes 52/52 identity, change-cursor, tombstone, checked-facade, batch,
  preview, and render-cache regressions. The focused QDR/ProjectSession/ProjectIo/manifest suites
  pass 40/40, and architecture guardrails pass 195/195. Native runtime, service-memento, terminal
  preview-cancel, and stale-public-handle allocation regressions also pass. Preset-wide CTest and
  the final native app build are recorded in the task handoff.

## 2026-07-16 - Transform Snap, Camera Pivot, And Knife Face Locality

Invariant:

- Transform vertex snapping must test the same scene-transformed, visibility-filtered, and
  instance-aware object projection used by viewport picking. World-space bounds and face caches
  must never be paired with raw authored/local object documents.
- `ViewportCameraState::target` is the canonical navigation pivot after every runtime camera
  operation. Cursor zoom, fly look, pan, orbit snap, axis views, and orthographic-to-perspective
  transitions may not retain a second off-axis pivot; perspective distance is capped before a
  pose is reconstructed.
- Every accepted Knife segment must resolve both topology targets on one existing shared face.
  The allowed region is rebased from the last explicitly accepted point, so a boundary-edge point
  opens its incident faces for the next segment; a single click never auto-inserts a crossing point
  or routes across an intermediate edge. Non-shared topology clears the current hover and preview
  instead of drawing a red invalid line.
- Scene-aware vertex/edge/face candidates are authoritative for Knife hover. A viewport miss must
  not trigger a second runtime pick with different radii. With grid snap active, edge points
  quantize world distance along the transformed edge and face points use the stable projected
  world-plane lattice; neither path rounds world XYZ and reprojects a displaced point back onto
  topology. Out-of-face grid points fall back to the raw face hit.
- Shift straightening derives its axes from the one active shared face, intersects constrained
  lines with hovered edges exactly, and rejects vertices, edges, or face points that do not lie on
  that constrained topology. Grid snap quantizes only the scalar distance along the chosen Shift
  direction, while an edge intersection remains the exact topology boundary. It never lets a
  lower-priority pick bypass the constraint.
- Activating Knife (`C`) preserves the persistent app/runtime selection mode across activation,
  cancel, and commit. Its topology picker remains mode-independent: a new stroke may begin on an
  existing edge or an existing topology vertex, including a high-valence junction, but never in a
  face interior. A vertex seed keeps every incident face available until the next target resolves
  the segment's shared face. Grid snaps that land on endpoints or face boundaries become exact
  canonical vertex/edge targets immediately; they never retain a 1% endpoint offset.
- Knife picking is two-sided and nearest-surface driven. Reversed face winding must not disable
  outside picking, and an edge shared by multiple faces takes its context from the nearest incident
  surface under the ray before using orientation as a silhouette fallback. When authored-facing
  preference is disabled, equal screen-distance components resolve by ray depth.
- Interactive Knife component tolerances are physical screen-space radii, so perspective distance
  and orthographic zoom do not change the usable hover width. The projected edge factor is converted
  back to its perspective-correct topology factor, and visibility is resolved with a ray through the
  candidate itself rather than the offset cursor ray. Headless/API callers with zero screen radius
  retain the legacy world-radius path.
- Knife surface occlusion follows the default single-sided renderer: it prefers authored-front
  surfaces but retains backfaces as a fallback. On an inside-out closed mesh viewed from outside,
  the culled near backface must not hide the farther interior topology that Filament actually draws.
- Hovering before the first accepted Knife point never locks an object; the first accepted topology
  seed establishes the stroke owner. Knife current points are explicit tool overlays drawn on top,
  are excluded from generic selection-point projection, and participate with target topology in the
  overlay signature. A hover command's `changed` flag describes a state transition, not whether the
  current target is valid.
- Activating a modal tool clears stale Select hover, synchronizes the viewport tool before the same
  input frame is dispatched, and an eligible Knife frame alone may preserve its authored component
  hover. Suppressed/error/miss frames clear stale presentation. Failed stationary hover keys remain
  cached until pointer, camera, tool, or document state changes, preventing retry/log loops.
- The polygon Knife Stroke operation rejects a segment whose endpoints do not share one face, so
  non-interactive callers cannot bypass the per-segment topology invariant.

Owners:

- `src/editor/interactions/move_tool.cpp` and
  `src/editor/modeling/services/modeling_transform_drag_service.*` own the scene-aware transform
  snap participant seam.
- `src/viewport/viewport_camera_controller.cpp` owns the single visible camera pivot.
- `src/editor/interactions/knife_tool.cpp` owns segment-local shared-face interaction policy;
  shared tool snapping owns edge-distance and face-lattice quantization; `ModelingSessionService`
  owns authoritative scene-aware candidate ordering, world-space resolution, and topology-derived
  straight constraints; tool activation and shortcut policy preserve the persistent selection
  mode while Knife owns its independent topology targets; the polygon Knife Stroke operation
  enforces the same one-face boundary.

Verification:

- Focused camera and Move suites pass 26/26 and 8/8. The topology-only Knife pass adds 8/8
  interaction, 4/4 polygon-operation, and 2/2 host-picking regressions; the affected workflow
  passes, including 294 runtime tests.
- The complete `vs2026-debug` CTest preset passes 6/6, all six previously exposed architecture
  guardrails pass, and the native `quader` v0.1.6 target builds successfully.
- The subsequent edge-only seed and topology-derived Shift-constraint follow-up used explicit fast
  iteration mode: automated tests were intentionally skipped and native `quader` v0.1.7 builds
  successfully for manual viewport verification.
- The grid-correction follow-up passes 9/9 focused runtime Knife tests, 10/10 scene/service Knife
  tests, the complete affected workflow and CTest 6/6. Native `quader` v0.1.8 builds successfully.
- The vertex/backface follow-up passes 12/12 runtime Knife tests, 11/11 scene/service Knife tests,
  and the focused polygon picking characterization. It covers valence-4 vertex seeds, exact initial
  endpoint canonicalization, two-sided reversed-winding picks, and nearest incident surface
  attribution; broad verification and the native v0.1.9 build are recorded in the task handoff.
- The screen-space/lifecycle follow-up passes the complete native app and modeling-domain suites.
  Focused coverage exercises near/far perspective and orthographic zoom, flipped meshes,
  high-valence vertex seeds, screen-space grid snapping, marker removal/restoration, and suppressed
  hover clearing. The v0.1.11 renderer-aligned regression specifically selects and continues from
  the rendered far interior edge/face of a flipped closed box while rejecting its culled near shell;
  the user also confirmed outside hover and navigation recovery in the native viewport. Final CTest
  passes 6/6, including SDK-only, SDK consumer/parity, guardrail, app, and modeling-domain targets.

## 2026-07-16 - Shaded Mode Uses Shadow-Free Modeling Lighting

Invariant:

- Shaded mode remains a separate unlit Filament material/proxy layer and never consumes the scene
  IBL, scene light entities, or shadows. Rendered mode remains the PBR/IBL/scene-light owner.
- The Shaded material evaluates a view-space directional key at normalized `(-0.35, 0.55, 0.75)`
  with wrapped Lambert diffuse plus a world-up hemispheric fill. It multiplies linear albedo by the
  resulting lighting scalar and preserves the source alpha/premultiplication path. Shaded does not
  evaluate specular, reflection, roughness, metallic, or normal-map inputs.
- Viewport Shading settings temporarily expose Key Direction X/Y/Z, Key Intensity, Diffuse Wrap,
  Ground Fill, and Sky Fill. The white-material Source reference is matched with defaults
  `(-0.09, 0.47, 0.88)`, `0.60`, `0.0`, `0.08`, and `0.21`; storage, binding, and shader evaluation
  clamp them to safe domains. Ordinary meshes, explicit instances, modeling tool previews, and the
  isolated Shaded material preview use the same parameters. Wireframe preview remains unlit. Load
  migrates only the exact prior preset (including the older form without direction keys), preserving
  user-customized values.
- Do not apply the requested `0.18` AO strength until Shaded has a usable AO source. Current
  Filament SSAO is a Rendered post-processing input and the unlit modeling material has no AO
  texture or neutral AO payload, so reusing it would create a false or mode-coupled signal.

Owners: `resources/materials/quader_mesh.mat`, `ViewportSettings`,
`MaterialBindingService`, mesh/instance/tool-preview render caches, and
`MaterialPreviewRenderer`.

Verification: the original implementation used explicit fast iteration mode and native `quader`
v0.1.12 built successfully. A rejected reflective experiment was removed completely. The restored
diffuse-only material with temporary direction controls compiles and passes its focused
Shaded/render/settings contracts. The white-material tuning follow-up adds exact-preset migration;
final affected/CTest/native v0.1.15 results are recorded in the task handoff.

## 2026-07-16 - Unified Viewport Display Menu And PBR Debug Views

Invariant:

- Each embedded viewport has one top-right display menu. Wireframe, Shaded, and Rendered remain
  top-level mutually exclusive modes; Camera is a checked submenu for the pane's view; Debug View
  is a mutually exclusive submenu containing Diffuse Only, Specular Only, Albedo, Roughness,
  Metalness, and Normal. Camera selection does not change the active shading/debug mode.
- Debug selection is owned independently by each `ViewportWorkspacePaneState` and exposed through
  `QuaderViewportApi`. Every debug mode consumes the Rendered material proxy layer. Diffuse Only
  and Specular Only retain Rendered scene/IBL lighting and shadows while removing the opposite PBR
  lobe. Albedo, Roughness, Metalness, and world-space Normal replace post-lighting output and bypass
  scene post processing.
- `ViewportRenderer` owns Filament material-global slot 0 for the per-view debug mode. Shared
  material instances must not be mutated to select a pass. The lit material preserves source alpha
  in every debug output and excludes editor surface-grid/context dimming from diagnostic data.

Owners: `ViewportWorkspaceService`, `QuaderViewportApi`, `QtViewportPanel`, `ViewportRenderer`, and
`resources/materials/quader_mesh_lit.mat`.

Verification: Filament `matc` accepts the diagnostic material; focused material, renderer,
workspace, editor API, viewport chrome, and UI/API guardrail tests pass. Final affected and native
v0.1.16 build results are recorded in the task handoff.

## 2026-07-16 - Reliable Array Pivots, Aggregate Selection, And Tool Cleanup

Invariant:

- Linear Array captures each source's canonical visible world pivot and anchors its shared handle
  to the explicit primary source. A scene origin is not a valid substitute for baked off-origin
  geometry. The captured pivots are revalidated with the rest of the session snapshot before any
  refresh or apply, including hierarchy bounds and effective instance-occurrence transforms.
- A successful persistent Pivot Apply immediately activates Move so the rebased transform origin
  is visible. A failed Pivot Apply must leave the active tool unchanged.
- Object-mode selection of more than one mesh suppresses per-object selected faces and edges and
  presents one union world-AABB cage. Render extraction retains one bounds record per mesh for
  transform-only cache refresh, and hover/tool overlays remain intact.
- Grid and Angle combo popups retain the native Fusion row presentation: never set
  `Qt::CheckStateRole`, because Fusion renders it as a checkbox column. On open, reset only the
  popup view's current/selected row to the combo's owner-committed index and scroll that row into
  view; transient popup row movement must not change the committed combo value.
- Extend is not a Quader tool. Its public/internal enums, command ID, runtime implementation,
  catalogs, SDK activation API, CMake entries, localization keys, docs, and dedicated tests remain
  deleted. The former `extend` tool ID resolves to Unknown. The unrelated
  `shift:extend_selection` selection modifier remains valid.

Owners:

- `LinearArrayToolService` and `scene_transform_pivot.*` own array source-pivot capture and
  validation; `NativeViewportToolCoordinator` owns the Pivot-to-Move handoff.
- `ViewportSceneOverlayBuilder` owns aggregate selection composition while
  `RenderOverlaySelectionSnapshot` keeps per-entity bounds; viewport overlay policy admits
  persistent object face fills only for a single selected mesh.
- `QtSnappingToolbar` owns native popup selection restoration. Viewport tool catalogs/runtime identities and
  the portable modeling SDK jointly enforce the absence of Extend, backed by a programmatic API
  source guardrail.

Verification:

- Focused Linear Array, Pivot, selection-overlay, snapping toolbar, runtime catalog/selection, SDK
  tool, and permanent-removal guardrail regressions pass. The complete `vs2026-debug` CTest preset
  passes 6/6, including SDK-only, SDK consumer/parity, guardrail, native app, and modeling-domain
  targets. The native-style popup follow-up passes its focused Windows UI regression; the final
  native `quader` v0.1.32 build is recorded in the task handoff.

## 2026-07-17 - Surface-Aware Poly Placement And Precise Fly Timing

Invariant:

- Poly resolves its first viewport ray against the same visible/selectable, scene-transformed
  surface-picking owner used by native modeling placement. A hit captures a world-space
  construction plane; every later point intersects and snaps within that locked plane so vertical,
  tilted, and offset support meshes remain coplanar. An authoritative host miss retains the active
  viewport-grid fallback and never silently re-picks untransformed runtime geometry.
- Poly acceptance always re-resolves the queued physical click ray before accepting a point. A
  same-frame mouse move cannot commit a stale hover sample. Preview placement remains transient;
  commit still creates one independently selected polygon object and remains one undoable action.
- Viewport navigation consumes `FrameTiming::delta_seconds` from the app frame. Do not restore a
  per-panel whole-millisecond `QElapsedTimer`: at high refresh rates, alternating integer movement
  steps are visible as fly-navigation micro-stutter.
- Retained toolbar synchronization checks cheap scene/modeling revisions before building a public
  selection summary. Descriptor lookup, shortcut formatting, and Qt setters remain behind each
  toolbar's unchanged-state gate.

Owners: `ModelingSelectionService`, `ModelingSessionService`, Poly runtime/tool interaction,
`PointSequenceTool`, `UISystem`, and `QtViewportPanel`.

Verification: focused transformed-surface, plane-local snapping, queued-click, Poly history,
viewport-display toolbar, public preference-command, UI hot-path, and precise-delta regressions
pass. Final full CTest and native v0.1.33 build results are recorded in the task handoff.

## 2026-07-17 - Persistent Maya-Style Scene Layers

Invariant:

- `SceneDocument` owns persistent layer records and one exclusive membership per eligible public
  entity. Layer IDs are monotonic across undo branches; group assignment includes only the current
  eligible descendants, instance-definition internals are forbidden, and empty/delete operations
  unassign members without deleting scene objects. Document and modeling reconstruction paths must
  preserve membership through duplicate, destroy, undo, redo, and mesh synchronization.
- Layer visibility is an independent effective-visibility mask and never rewrites an entity's
  local `VisibilityComponent`. The shared hierarchy policy gates rendering, lights, bounds,
  overlays, picking, placement, and instance expansion. Layer RGB affects only unselected
  Wireframe geometry; selection style wins, and cache/batch identities include resolved color.
- Native scene QDR schema 4 persists deterministic layer records and entity memberships, validates
  IDs, names, colors, and eligibility, loads schemas 1-3 as unassigned, and rejects newer schemas.
  The portable modeling QDR and project manifest remain unchanged.
- `LayerPanel` is a retained Qt view routed through `QuaderLayersApi` and document history. Its
  allocation and widget synchronization paths are gated by layer, selection, and localization
  revisions; it remains a default-visible right-side dock below Inspector.

Owners: `SceneDocument`, document commands, `QuaderLayersApi`, native QDR scene metadata,
scene/render visibility extraction, wireframe overlay caches, and `LayerPanel`/`UISystem`.

Verification: focused domain/API/history, QDR, rendering, instance, visibility, UI interaction,
localization, and hot-path guardrail tests pass. The complete `vs2026-debug` CTest preset passes
6/6, including SDK-only, SDK consumer/parity, guardrail, native app, and modeling-domain targets.
Final native v0.1.36 and boot-smoke results are recorded in the task handoff.

## 2026-07-17 - Layer Lock And Panel Interaction Follow-Up

Invariant:

- Layer lock is an independent effective-interaction mask. It never rewrites an entity-local
  `LockComponent`, applies through ancestors and instance occurrences, blocks document batches and
  picking, and projects any selection removal into the runtime modeling selection. Point and
  rectangle picking caches include `scene_layer_revision` so a warm cache cannot retain locked
  participants or omit newly unlocked participants.
- `QuaderLayersApi` must reject every mutation before resolving document commands when no scene is
  active; the inert fallback scene, its layer revision, and its history must remain unchanged.
- `LayerPanel` columns are Color, Name, Visible, and Locked. Only name-column clicks select rows;
  color, visibility, lock, and empty-area clicks clear the highlight. Visibility and lock are
  focusable native buttons with keyboard activation and localized accessibility text.
- Runtime eye/lock artwork comes from Google Material Icons pinned at commit
  `abd7f5c0e179c83f068c770650bd14ebac5d5a09`, with Apache-2.0 provenance. The standalone layer-add
  icon is a generated Quader-style beveled plus and does not replace existing icon artwork.

Owners: `SceneDocument`, document commands, `QuaderLayersApi`, modeling picking caches,
`LayerPanel`, packaged icon resources, and third-party provenance.

Verification: focused layer UI/API/history/runtime-selection/cache tests pass, all native test
targets build, and the complete `vs2026-debug` CTest preset passes 6/6. Final native v0.1.37 and
boot-smoke results are recorded in the task handoff.

## 2026-07-17 - High-Contrast Dock Title Controls

Invariant:

- A Qt stylesheet `color` declaration does not recolor Fusion's fixed dock-title raster glyphs.
  Fusion also shrinks a framed dock button's 16 px small-icon metric to 5/8, producing the
  unreadable 10 px default seen against Quader's dark dock title bars.
- Dock title contrast is owned explicitly by app-authored, near-white close/float PNGs embedded
  under `/quader/icons`. The scoped dock stylesheet uses Qt's `titlebar-close-icon` and
  `titlebar-normal-icon` properties plus `icon-size: 24px`, yielding an approximately 15 px glyph
  inside an 18 px control while preserving Qt's built-in drag, float, close, and accessibility
  behavior.
- Default and active title colors remain distinct. Hover keeps the background transparent and
  adds a high-contrast outline; pressed uses a darker fill and brighter outline so neither state
  muddies the glyph.

Owners: `UISystem::create_dock`, scoped dock-title QSS, embedded dock-chrome resources, and UI/build
guardrails.

Verification: focused dock-title presentation and resource-packaging guardrails pass, all affected
native test targets build, and the complete `vs2026-debug` CTest preset passes 6/6. Final native
v0.1.38 build result is recorded in the task handoff.

## 2026-07-17 - Scoped Singular Editable Keybindings

Invariant:

- `QuaderCommandsApi::keybinding_catalog()` is the complete public presentation source for every
  command and semantic viewport target that the resolver can map. A stable
  `action_id + scope_id` identifies one user-facing row without exposing resolver aliases. Scope is
  semantic editor state, not the coarse display category: modeling availability metadata derives
  scopes such as `viewport.selection.vertex`, `viewport.selection.edge`, and
  `viewport.selection.component`, while app-owned read-only `when` expressions remain the dispatch
  authority. This permits `M` to mean Merge Vertices in vertex mode and Merge Edges in edge mode
  without reporting a hard conflict.
- Each action/scope owns zero or one physical mapping, although that mapping may be a multi-chord
  sequence. Compiled defaults and user edits are normalized across every alias in the row. Legacy
  target-level JSON remains loadable; the latest legacy entry in a group wins and its stored `when`
  is rebound to the current app-owned predicate. Persistence elides default-equivalent groups,
  represents intentional unassignment with target removals, and writes an empty override set for
  Reset All. Candidate JSON is published through the shared atomic-write owner before resolver
  activation; a late flush/replace failure preserves both the live resolver and last-good disk
  file.
- Assignment analysis classifies mutually exclusive reuse, exact overlap, and prefix overlap.
  Public mutations default to `Reject`; Settings offers only Replace or Cancel for a hard conflict.
  Replace unassigns the complete colliding action while leaving unrelated actions untouched.
  Resolver dispatch checks command availability in precedence order and falls through to the next
  context-compatible target instead of allowing a disabled alias to swallow input.
- Settings consumes only public APIs. Double-clicking a mapping or pressing Enter opens the single
  rebind dialog; there is no separate Add/Replace/Remove list. Capture supports keyboard,
  multi-chord sequences, wheel direction, stationary Click, press-and-hold Press, DoubleClick, and
  thresholded Drag through canonical Qt translation. The conflict prompt names only the clashing
  action and has no Keep Both choice. Successful edits save immediately and emit
  `KeybindingsCommit`, refreshing retained menus/tooltips outside `UISystem::refresh`; Settings
  Apply/Cancel continues to govern only interface and viewport drafts.
- The retained Keybindings page is intentionally table-first: it has no save-immediately banner,
  rebind instruction banner, or selected-row default/current/`when` details block. Those internal
  diagnostics stay in the public catalog but are not repeated in the view. Its visible columns are
  Command, Context, and Input Mapping only; Source remains catalog metadata and is not presented.
  Every visible column uses case-insensitive natural numeric ordering, so labels such as grid sizes
  sort `1, 2, 4, 8, 16, 32, 64, 128, 256, 512` rather than lexically. Keybinding-facing localization
  uses complete action names, separate from terse menu leaves; every rendered EN/IT command label
  is unique, and generic rows such as `Center`, `X`, or `Move` are forbidden by regression
  coverage.
- A canonical mouse Click is emitted on release only when motion remained below Qt's drag
  threshold. Existing immediate Drag edges remain unchanged, which keeps orbit/fly navigation
  responsive while allowing `Alt+MiddleMouseClick` to be a real binding. Capture keeps a quick
  click pending for the double-click interval, disables acceptance while unresolved, and flushes
  it before any later key/click so sequence order cannot change or lose an input.
- Qt's Windows QPA rewrites `Alt` plus a physical vertical wheel into a horizontal-only
  `QWheelEvent::angleDelta`. Canonical Qt translation reverses that representation before choosing
  WheelUp/WheelDown and calculating the signed step. Tests must inject the real X-axis event through
  the production surface -> panel -> resolver/router -> workspace route; a synthetic Y-axis event
  would reproduce neither the native bug nor its fix. Genuine Alt-modified horizontal hardware
  wheel input is indistinguishable at this boundary and is intentionally claimed by the vertical
  grid gesture on Windows.
- Compiled defaults are singular: Redo is `Ctrl+Shift+Z`; alternate Ctrl+Y, Backspace delete,
  keypad selection-mode, and Enter finalize mappings are removed. Grid preset rows no longer own
  `F1`-`F10`. `H`/`Alt+H` hide/unhide scene objects in the object/group scope; Quader does not claim
  persistent vertex/edge/face hide support. `F1`-`F4` select Top/Side/Front/Perspective,
  `F5`-`F7` select Wireframe/Shaded/Rendered, `Ctrl+1`-`Ctrl+4` select the requested viewport layouts,
  `Alt+WheelUp/Down` changes grid preset, and `Alt+MiddleMouseClick` restores the 32-unit default.
  These three mouse/wheel mappings project to semantic `SetGridPreset` viewport shortcuts using
  the synchronized current preset; they must not bypass the runtime viewport router as immediate
  commands. Unassigned grid rows name their actual sizes (`Set Grid Size to 1` ... `512`) rather
  than opaque preset indices.

Owners: command and semantic viewport registration metadata, `QuaderCommandsApi`,
`QuaderKeybindingService`, canonical Qt input translation/surface accumulation, and
`QtSettingsDialog` presentation/localization.

Verification: focused scoped-dispatch, grouped-alias, singular-mapping, conflict, persistence,
rollback, mouse/capture, Settings, localization, retained-UI guardrails, and the Windows-shaped
surface-to-workspace grid-input regression pass. The complete CTest preset passes 6/6; the final
native v0.1.42 build succeeds, and a native Settings smoke check confirms three visible columns and
grid rows ordered `1, 2, 4, 8, 16, 32, 64, 128, 256, 512` under the production C locale.

## 2026-07-17 - Transient Local Visibility

Invariant:

- Local Visibility is scene-session runtime state, never `SceneDocument`, QDR, dirty-state, or
  undo/redo state. `ViewportWorkspaceService` owns the immutable membership snapshot, monotonic
  revision, and pre-entry cameras; `ViewportLocalVisibilityService` maps object, group, component,
  and formal-instance selection into public scene entities and reconciles scene lifecycle changes.
- Persistent object, ancestor, and layer visibility always remains authoritative. The local
  snapshot is only an additional allow-filter, so entering never unhides an already-hidden entity
  and exiting reveals exactly the persistently visible scene. Entities created during the mode
  join it; deletion of the last live member exits synchronously and restores the saved cameras.
- Every viewport-facing render, overlay, picking, hover, bulk-selection, placement, statistics,
  and frame-all cache includes the Local Visibility revision in its unchanged-state gate. Formal
  instance content is filtered by its public occurrence; tool previews retain canonical source
  identity while using that occurrence as their local-visibility owner.

Owners: `ViewportWorkspaceService`, `ViewportLocalVisibilityService`, `SceneSession`, public
viewport commands/API, render/overlay extraction, modeling selection/picking, and retained Qt
viewport caches.

## 2026-07-17 - Multi-Mesh Selection Keeps Per-Mesh Outlines

Invariant:

- Object-mode selection of multiple meshes draws one world-AABB cage around the complete
  selection and retains the normal selected topology outline for every participating mesh.
- The aggregate cage may replace per-mesh bounds cages, but it never replaces selected mesh
  outlines. Multi-selection may still suppress persistent selected face fills, and hover feedback
  remains intact.
- `ViewportSceneOverlayBuilder` owns this composition policy. The modeling overlay cache continues
  to provide cached per-object outline lines without adding per-frame mesh extraction.

Owners: `viewport_scene_overlay_builder.*`, modeling selection overlay caching, and render
extraction tests.

Verification: 7/7 focused scene-overlay builder tests, 378/378 affected render/viewport tests,
the affected render guardrail, and the final native v0.1.45 build pass.

## 2026-07-17 - Phase 4 Keeps Render And Qt Behind Neutral Revision-Gated Owners

Invariant:

- Render overlay snapshots consume asset-owned neutral wire payloads and semantic host/SDK records;
  they never traverse polygon documents or editor modeling state. Render alone resolves color,
  width, depth, material, and draw policy. Interned compiled topology shares one lazy wire payload;
  authored mesh mutation invalidates its MeshAsset-local payload.
- Qt viewport code samples raw events, focus, geometry, widgets, and native surfaces, then delegates
  semantic routing, pane resolution, picking/selection, tool/gizmo, pivot/array, overlay, framing,
  and submission decisions to viewport/editor owners. There is no second panel-owned interaction
  state machine.
- Material controls dispatch atomic editor/public scalar or texture operations and synchronize from
  a cheap revision snapshot. The unchanged-state check precedes catalog work, formatting, and Qt
  setters.
- Modeling services may map runtime tool IDs through the modeling-runtime descriptor catalog; they
  must not consult the UI-owned ViewportTool object catalog. This keeps fixed-host and other
  non-UI consumers linkable without `quader_ui`.

Owners: `MeshGeometryPayload`/`MeshAsset`, neutral render-overlay records and render style builders,
viewport/editor interaction owners, material editor/public services, and the modeling-runtime tool
descriptor catalog.

Verification: all six `vs2026-debug` CTest entries pass (244 guardrails and 1,228 native tests), the
native v0.1.59 executable builds, and the Vulkan/Fusion boot smoke passes. Two Release fixed-host
samples preserve every Phase 2 checksum and all retained reuse paths; the bounded SDK box-preview
cold-generation increase is accepted because it is outside unchanged frame work.

## 2026-07-18 - Qt Defaults Own Widget Interaction

Invariant:

- Qt model/view current state and selection are independent. Item-view actions consume the actual
  selection, while blank clicks, Shift/Ctrl ranges, drag selection, keyboard navigation,
  activation, editing, type search, and context-menu delivery remain owned by Qt.
- Configure selection cardinality, models, delegates, focus proxies, and edit triggers instead of
  replacing Qt gestures with panel event filters or parallel state machines. An override is valid
  only for explicit Quader behavior and must preserve the base path. The outliner's narrow range-
  anchor reset exists only after programmatic owner selection synchronization; a complete native
  drag gesture is coalesced into one owner/history commit.
- The application command filter yields documented control keys and auxiliary-window input to the
  focused Qt widget. Retained UI synchronization gates broad queries, formatting, and setters on
  owner revisions or explicit events before doing work.

Owners: `OutlinerPanel`, `LayerPanel`, Qt settings/home/material/inspector/console surfaces,
`QtCommandEventFilter`, `UISystem`, public tool/modeling/command revision APIs, and UI guardrails.

Verification: focused native Qt interaction tests, the complete affected guardrail/application/
modeling-domain workflow, full CTest, and the final native v0.1.66 build.

## 2026-07-18 - Surface Grid Projection Is Stable Per Discrete Face

Invariant:

- The mesh surface grid is procedural API-level world-space coverage, independent of material UVs.
  Filament's `getWorldGeometricNormalVector()` is the interpolated authored vertex normal in this
  pipeline, not a constant raster-triangle or polygon-face normal. It is safe for blended smooth-
  surface projection but must never directly drive a hard dominant-axis branch inside a smooth-
  shaded discrete face. Derivative-reconstructed normals are also forbidden for that branch because
  symmetric axis ties can fluctuate per fragment and become stippled noise.
- `build_mesh_surface_grid_vertex_projections` consumes original, uncoalesced primitive spans.
  Unshared triangle faces and unshared flat-normal faces receive one area-weighted local face normal
  and `FaceProjected`; smooth multi-vertex surfaces, malformed spans, uncovered vertices, and shared
  vertex conflicts retain `SmoothSurface`. This keeps pyramids on a stable two-coordinate face grid
  while smooth multi-vertex patches such as torus and cylinder sides use coverage-blended
  triplanar projection.
- `CUSTOM0` carries `{local projection normal.xyz, face-projected flag}`. Both mesh materials
  transform the normal through Filament's world-from-model normal matrix in the vertex stage and
  pass it through a custom varying. Face projection mirrors TrenchBroom after Z-up to Y-up
  conversion: strict dominant-axis ties resolve Y, then Z, then X. Smooth projection blends plane
  coverages, never coordinates, and the grid LOD uses the effective four-pixel footprint.
- Every vertex-buffer producer used with the mesh materials must bind `CUSTOM0`; material previews
  bind a smooth-fallback value even though their grid is disabled. Geometry identity must include
  original primitive partitioning because that partition affects projection mode; render code must
  not derive the hint from coalesced submission spans.

Owners: `mesh_surface_grid_projection.*`, `MeshRenderableFactory`, material preview GPU vertices,
both Quader mesh materials, render extraction tests, and render source-policy guardrails.

Verification: both Filament mesh materials compile, 4 focused projection/cache tests pass, 46
affected render guardrails pass, 320 affected render/viewport tests pass, and the native v0.1.66
executable builds successfully. Final pixel comparison remains a manual GPU smoke check.

## 2026-07-18 - Selection Commands Use Explicit Component Scope And Atomic Mesh Targets

Invariant:

- The canonical command family is `select.*`; removed `edit.select_all`, `edit.invert_selection`,
  and `selection.*` spellings have no aliases, typed constants, registry routes, keybinding rows, or
  menu entries. The retained Qt Select menu changes its specialized rows by Object, Vertex, Edge,
  and Face mode while Select All/Inverse remain common actions.
- In component modes, Select All operates on meshes that currently contain at least one selected
  component in that mode plus the current live sticky source-wire candidate. The sticky candidate
  is additive, survives an empty immediate hover, and must still belong to the pickable
  local-visibility/instance-edit universe; stale IDs never force a different scope. Select Inverse
  remains limited to meshes with selected current-mode components, so once inversion empties a
  mesh, a later inverse cannot rediscover it through scene selection or sticky hover.
- Cross-mesh component commands normalize every selected target's empty portable selection to the
  active component mode before comparison. This lets world-space Direction, Coplanar, and Normal
  include secondary meshes without manufacturing seed components. Grow/Shrink keep Blender's
  default one-face-step behavior (`use_face_step=true`) and never cascade through newly added
  elements in the same invocation.
- Transient SDK-to-native selection projections carry the real content and selection revisions
  before entering cached picking/overlay owners. A selection expansion therefore rebuilds the
  per-object topology wire for every newly selected mesh. Persistent whole-mesh selected scene wire
  is an Object/Group presentation only; component modes use semantic face/edge/vertex overlays, so
  disconnected scene-wire edges cannot appear selected merely because their mesh is selected.
- Select Similar compares exact local vertex positions and face incidence, including coincident or
  unwelded vertices. Direct meshes and formal instance occurrences are atomic targets: one-member
  occurrences can match direct geometry, multi-member occurrences compare each visible definition
  member plus its exact definition-relative arrangement, outer occurrence transforms are ignored,
  and synthetic definition members never enter public scene selection.
- Select Normal is same-facing and has no user threshold; it uses inverse-transpose world normals,
  rejects degenerate faces, and permits only machine-roundoff equality. Mesh-mode Select N-Gons
  captures the selected directly editable meshes, enters Face mode, and replaces their component
  selections with faces having more than four sides; occurrence-only targets remain atomic and are
  excluded outside instance edit.

Owners: canonical command IDs/registry and keybindings, `EditorSelectionService`,
`ModelingSessionService`/`ModelingSelectionService`, the SDK projection adapter, portable polygon
selection and exact-geometry helpers, Qt Select-menu presentation, and render overlay snapshot
policy.

Verification: the 9-test portable selection suite and focused native command/scope/Direction/
Normal/N-Gon/Similar/overlay regressions pass. Sticky Select All regressions cover Vertex, Edge,
and Face modes without a seed selection, additive multi-mesh scope, unchanged inverse semantics,
stale candidates, Local Visibility, and instance-edit isolation. The complete `vs2026-debug` CTest
preset passes 6/6; the final native build result is recorded in the task handoff.

## 2026-07-18 - Every Visible Command Has An Executable Behavior Contract

Invariant:

- Every visible registry command has exactly one explicit `CommandBehaviorContract`; new, removed,
  or renamed commands fail the catalog equality test until their fixture, permitted observable
  domains, outcome, undo/redo, repeat, and preview policies are deliberately declared. Successful
  fixtures are the default. Only commands whose result depends on an external runtime integration
  may be contractually allowed to reject.
- `CommandObservableState` is the deterministic whole-editor observation boundary. Its canonical
  payload covers documents, scene, geometry, selection, overlays, assets, history, tools,
  viewports, preferences, and revisions. Non-scene domains use semantic entity labels so a pure
  hierarchy mutation cannot masquerade as unrelated geometry or selection changes.
- The command sweep enforces allowed-domain mutation, result/change/revision consistency,
  undo/redo exact restoration, repeat determinism, preview cancel restoration, and preview commit
  equivalence. A checked-in per-command fingerprint baseline catches approved behavior drift, while
  seeded command sequences catch cross-command state leaks.
- Normal verification is read-only: `tools/regression_gate.ps1` refuses every baseline-update
  environment variable, builds every registered test owner, runs complete CTest, and builds the
  native executable. Baseline changes use the separate reason-required updater; it refuses to write
  after any law failure and replaces the baseline atomically before presenting the diff for review.
- Project-material behavior fingerprints use decoded public material properties, never QMAT byte
  size or its raw-byte content hash. Git LF/CRLF conversion changes both storage values without any
  editor-visible material change, so including them makes a clean native build invalidate an
  otherwise identical behavior baseline.
- Vertex welds follow Blender's split-before-weld topology rule. If a removed source corner and its
  survivor are non-adjacent corners of one face, recursively split that face into the two source
  boundary arcs before remapping IDs. After remapping, keep each valid three-or-more-corner arc and
  discard only degenerate arcs. Never remove one repeated survivor occurrence and close the
  remaining loop: that joins unrelated neighbors and manufactures a diagonal. In the `one.qdr`
  bridge case, face 27 becomes the exact eight-corner surviving arc, its zero-area hairpin is
  discarded, and the nonexistent 25-30 diagonal remains absent.
- Face mode emits selected face fill and `SelectedFaceEdge` boundary records, never the generic
  `SelectedEdge` layer. A disconnected or coincident vertex/edge elsewhere in the mesh therefore
  cannot inherit a highlight from face selection.
- Object/Group Create Outer Hull preview temporarily supplies the operation with the whole object's
  faces and clears the temporary selection afterward. Preview cancel restores geometry and
  selection exactly; an empty component selection must not turn the operation into a no-op.

Owners: command behavior contract/fixture/observable-state support, command behavior and regression
gate tests, regression gate/updater scripts, polygon hull-plane merge repair, modeling selection
overlay extraction, and the SDK projection adapter's whole-object face-selection helper.

Verification: the explicit Debug catalog contains 157 commands (Release contains 156); the full
fingerprint sweep passes twice consecutively after a clean native resource deployment, four seeded
160-step sequences pass, and both the exact `one.qdr` merge-distance case and the two-valid-arcs
weld case pass. The face-overlay suite, outer-hull preview regression, and regression-gate
guardrails pass. The complete read-only gate passes 6/6 registered CTest owners, including the
SDK-only configure/build check, and the native v0.1.66 `quader` executable builds successfully.

## 2026-07-19 - Navigation Reuses Dense Selection And Overlay Projections

Invariant:

- Camera navigation must not rebuild, copy, hash, or resubmit an unchanged dense semantic overlay.
  A pane submission key covers every semantic overlay owner revision and style/input source; when
  it is unchanged, the render cache receives only the new camera and rebuilds camera-dependent GPU
  groups if required.
- Native SDK-to-editor selection projection is revision-cached. Read-only picking, hover, summary,
  bounds, export, and overlay queries borrow that projection; mutating command paths retain an
  independent copy.
- The modeling overlay owner caches the expensive world-space projection of the persistent
  selection base by content, selection, scene, local visibility, object-map, selection mode,
  instance-edit scope, and suppression policy. Hover and insert/poly tool records are projected as
  small deltas, then composed with the retained base while preserving selected-hover filtering and
  fallback source-wire replacement.
- Active navigation suppresses hover picking. Orbit, pan, wheel zoom, fly look, and WASD camera math
  remains instrumented independently from input routing, overlay preparation, and render work.
- Retained Qt UI profiling must preserve the complete causal chain from menu/panel/event entry to
  API queries and Qt presentation. Main-menu refresh, command enumeration, individual availability
  predicates, shortcut-rule construction, repeated arrangement bounds, context menus, retained
  toolbar/panel/status refresh, key dispatch, and Focus All world-bounds traversal have distinct
  Tracy zones; modal `exec()` zones intentionally include the nested event-loop lifetime.

Owners: `QtViewportPanel`, `ViewportPaneOverlayCache`, `OverlayRenderableCache`,
`SceneRenderBridge`, `ModelingSdkProjectionAdapter`, `ModelingSessionService`,
`ModelingOverlayService`, `QtMainMenu`, `UISystem`, command/keybinding APIs, mesh arrangement,
context-menu presenters, and viewport camera/input routing.

Verification: `navigation_log_2.tracy` reduced dense-selection navigation from a 98.04 ms median
frame-start cadence to 15.48 ms (about 10.2 to 64.6 FPS); navigation math itself remained below
0.016 ms per operation. The trace still exposed stable-overlay submission and hover-triggered full
selection projection, which the second-pass caches address. Under the explicitly requested Fast
Iteration Workflow, automated suites were skipped and the final `vs2026-debug` native `quader`
executable built successfully; a new Tracy capture is required for post-fix measurements.

`navigation_log_3.tracy` validates the second pass: active navigation reaches a 10.72 ms median
frame-start cadence (about 93.3 FPS), hover falls from 21.36 ms to 0.227 ms median, and full
selection-overlay projection falls from 39.75 ms to 0.015 ms. Camera-only overlay updates replace
semantic submission on 3621 of 3724 frames. The same trace exposes a separate UI defect: each of
seven top-level menu openings spends 1.56-1.63 seconds evaluating all 157 command predicates;
arrangement, local-visibility, instance, and pivot availability dominate. The native executable
builds with the new UI attribution zones; automated suites remain skipped under Fast Iteration.

## 2026-07-19 - Dense UI And Transform Work Follow Narrow Revisions

Invariant:

- Retained UI command presentation must reuse cached effective keybinding rules and narrow command
  availability. Dense array and arrangement predicates use structural preflights; actual command
  activation keeps complete validation. Transform drags defer selection/tool toolbar refresh, and
  unchanged toolbar properties never call Qt setters.
- Object-mode transform snapping consumes cached local picking geometry plus object transforms.
  When every object is selected there is no stationary vertex target, so the snap geometry cache
  must not be built. Native selection projections are independent of scene transforms and are
  invalidated by mesh bindings, assets, object mappings, SDK content, or selection instead.
- A pure scene-selection revision updates selected occurrence flags in the retained render scene
  snapshot without rebuilding geometry. Outliner content refresh first compares its display
  projection and never rebuilds the `QStandardItemModel` for transform-only changes.
- Box selection is profiled end to end: baseline capture, gesture routing, rectangle-universe and
  projected-candidate cache hits/misses, hit testing, mode-specific policy, SDK/native publication,
  scene selection commit, mutation publication, and overlay invalidation have distinct Tracy zones.

Owners: keybinding and array services, mesh arrangement, modeling transform drag/selection/SDK
projection services, render scene extraction/bridge, retained Qt toolbar/outliner/UI refresh, and
box-selection API/editor/runtime policy.

Verification: `ui_trace_2.tracy` confirms the preceding menu fix reduced menu `aboutToShow` from a
1502.09 ms median to 1.17 ms. It exposed 220.43 ms mean transform snap-face reconstruction,
82.84 ms mean selection-only render extraction, 80-98 ms Outliner rebuilds, and dense array/
arrangement predicate stalls addressed here. The combined `vs2026-debug` native `quader` target
builds successfully. Automated suites remain skipped under the explicitly requested Fast
Iteration Workflow; a post-fix trace is required for measured confirmation.

## 2026-07-19 - Dense Box Selection Publishes Once And Reuses Stationary Bounds

Task/context: `last_trace.tracy` showed component box selection spending 10.62-11.28 seconds in
SDK/native selection publication across 784 meshes, object selection spending roughly 174-181 ms
in scene/SDK synchronization, and transform snapping rebuilding every participant's world AABB on
each drag update.

Invariant:

- A multi-mesh component selection gesture stages exact per-object selections, then commits one
  atomic portable SDK transaction, one undo record, one change-journal publication, and one
  delta-based scene selection sync. It never copies/checkpoints and commits the complete SDK
  session once per mesh. Invalid, duplicate, or noncanonical batches leave state, history, redo,
  and revisions unchanged.
- SDK object-selection membership, transition lookup, persistent change-record lookup, and native
  reverse-ID projection are hash-indexed. Production selection publication does not take expensive
  rollback snapshots after all fallible input has been staged and preflighted; injected failure
  tests retain exact rollback checkpoints.
- Scene selection-only revisions do not invalidate the rectangle-selection universe or projected
  picking participants. Scene content changes still invalidate lock and instance participation.
- World pick bounds retain each participant's topology identity/signature and exact world matrix.
  A transform revision recomputes only changed participants; layout, visibility, topology, and
  hierarchy changes remain conservative.

Owners: portable `ModelingSession` and `PolygonDocument`, the SDK projection adapter,
`ModelingSelectionService` picking/rectangle caches, and focused SDK/native/source-policy tests.

Verification: atomic success/undo/redo, no-op redo preservation, invalid-batch atomicity, exact
rollback, repeated rectangle reuse, lock/instance invalidation, and one-of-200 world-bound rebuild
tests pass. The broad regression gate was stopped when the user reaffirmed the ongoing Fast
Iteration Workflow; the native executable is the handoff gate, and a new Tracy capture is required
for measured wall-time confirmation.

## 2026-07-19 - Dense Component Transforms Reset Only Portable Working State

Task/context: `profiling.tracy` showed three component-transform pointer updates taking 7.47-7.53
seconds each across 784 meshes. Static layer identity publication alone consumed 14.642 seconds
over 2,352 calls; the remaining path restored the whole host projection and created one complete
SDK undo snapshot per object on every pointer update. Component box publication remained dominated
by repeated document lookup/application, with Edge selection additionally rebuilding all live edges
for every requested edge.

Invariant:

- Reapplying an entity's existing scene layer is a constant-time no-op. It must not clone,
  normalize, validate, and reinstall the whole scene layer state.
- A dense workspace transform update restores the frozen portable SDK base directly when every
  target owns a unique mesh binding, stages all selected documents from that base, commits one
  atomic multi-document transition, and reprojects every target so a nonzero-to-zero update restores
  baseline geometry. Shared mesh bindings retain the conservative full-projection restore path.
- Multi-document reads index the SDK session once. Multi-document commits validate first, retain one
  undo state, replace all documents, and publish one change transition; per-object history snapshots
  are forbidden in this hot path.
- Dense Edge selection materializes the live edge universe once per document, not once per selected
  edge. Object-versus-component participant caches remain stable across Vertex, Edge, and Face mode
  switches, and rectangle policy containers reserve from the known universe/hit sizes.
- Tracy separates portable transform update, SDK-only versus full reset, atomic document commit, and
  host document projection so the next capture immediately identifies the remaining mesh-compilation
  floor.

Owners: `ModelingTransformDragService`, `ModelingSdkProjectionAdapter`, portable
`ModelingSession`/`PolygonDocument`, `SceneDocument`, and modeling rectangle-selection caches/policy.

Verification: under the explicitly requested Fast Iteration Workflow, automated suites remain
skipped. The `vs2026-debug` native `quader` target builds successfully; a new same-scene Tracy
capture is required to measure the post-fix transform and component-box latency.

## 2026-07-19 - Dense Preview Projection And Statistics Use Indexed Work

Task/context: `profiling_2.tracy` confirmed the previous transform pass reduced dense component
updates from roughly 7.5 seconds to 1.08 seconds, then isolated 948 ms/update in host projection.
It also showed component box publication averaging 578 ms and selection-only viewport-info
snapshot rebuilds taking 20-30 ms.

Invariant:

- A transform preview that already proved every target has a unique mesh binding carries that proof
  into host publication. It must not rescan every scene mesh once per projected document. Independent
  document mesh compilation uses a bounded parallel batch for dense workloads; asset replacement,
  failure injection, scene binding, and notification remain sequential and deterministic.
- Exact multi-object Replace selection goes directly to the portable atomic commit. The native
  adapter does not copy and apply every selection before asking the SDK to validate and apply it
  again. Dense vertex/face selection validation builds one live-ID index per document, and edge
  validation uses the already sorted edge universe rather than an edge-by-edge linear scan.
- Viewport topology statistics count authored vertices, unique edges, faces, and Blender-style
  triangles (`sum(face corners - 2)`), falling back to compiled triangle facts when authored
  topology is unavailable. Per-mesh facts are reused across occurrences within one snapshot.
- The retained info panel is invalidated by visibility, transforms, mesh bindings, render
  components, layers, assets, and local visibility—not the broad scene revision—so selection-only
  changes do not rebuild scene geometry statistics. Qt text remains unchanged-value gated.

Owners: portable polygon/session selection, SDK projection adapter, asset mesh topology facts,
render viewport-info extraction, and the Qt viewport info presentation.

Verification: Fast Iteration Workflow skips automated suites. The native Debug `quader` target
build is the handoff gate. A new same-scene trace is required to quantify these post-`profiling_2`
changes; the trace also establishes that a future Blender-style partial preview workspace and
retained selection-mask overlays are the next architectural floor.

## 2026-07-19 - Modal Transforms Stream Positions And Dense Points Stay On GPU

Task/context: `profiling_3.tracy` showed four dense transform samples averaging 457.70 ms in the
portable preview and roughly 1.24-1.33 seconds end to end. Every sample recompiled and republished
557 documents. Navigation with the dense component overlay averaged about 150 ms/frame while the
renderer itself used only about 4.84 ms; CPU point visibility and quad rebuilding dominated.

Invariant:

- A modal affine transform resolves durable participants, selected source vertex IDs, compiled
  corner slots, base positions, topology layout keys, materials, and world transforms once at drag
  begin. Pointer samples update only selected preview positions. They do not mutate the portable
  documents, compile topology, replace assets, notify the scene, or create history.
- Render-owned transform previews use uncached dynamic vertex buffers with position in its own
  stream. Tangents, UVs, surface-grid data, colors, indices, primitives, and materials remain
  static. Preview layout checks are O(participants); unchanged frames are revision-gated. Direct
  and instanced source renderables are suppressed while their previews are live, and all shared
  preview instances receive updated local bounds.
- Mouse release applies the stored portable transform to all participating documents in one atomic
  batch, performs the complete compile/projection once, and squashes the drag to one history entry.
  Returning to the base transform commits nothing. Cancel clears transient renderer state and
  retains the exact pre-drag restore contract.
- Dense vertex points remain world-space retained geometry. The point material performs clip-space
  projection and fixed-pixel billboard expansion from camera uniforms, and Filament depth testing
  replaces per-frame CPU depth-stamp visibility. Camera navigation therefore does not rebuild point
  quads or test every point against scene triangles.
- Compiled render vertices preserve their portable source vertex IDs. This identity is the stable
  bridge for position streaming and component-overlay remapping; it is not a resurrected external
  SDK boundary.

Reference: Blender's edit-mesh transform path creates stable `TransData` once and its overlay path
keeps edit points in GPU batches. A BVH does not address either measured hot path and is deliberately
not added; Filament's dynamic vertex-buffer and depth-culling facilities are the existing owners.

Owners: portable polygon compilation payloads, `ModelingSdkProjectionAdapter`, modeling overlay
service, tool-preview extraction/cache, mesh/instance renderable caches and factory, point-overlay
builder/material, render API preview synchronization, and retained viewport overlay submission.

Verification: under the explicitly requested Fast Iteration Workflow, automated suites were
skipped. The Tracy-enabled `vs2026-debug` native `quader` target builds successfully with
`QUADER_ENABLE_TRACY`, `TRACY_ENABLE`, and `TRACY_ON_DEMAND` enabled. A new same-scene trace is
required to measure the new position-stream path and commit spike separately.

## 2026-07-19 - Dense Selection And Transform Publication Stay Retained

Task/context: `profiling_5.tracy` showed transform release reaching 3.019 seconds, canonical
compilation using 3.415 seconds of aggregate CPU across 1,094 documents, box selection spending
3.106 seconds across nine SDK publishes, and persistent overlay construction rebuilding selected
face tessellation twice. Tool preview suppression also hid every occurrence of an instance member
instead of only the edited occurrence.

Invariant:

- Component selection batches use dense object indexing and may share one immutable canonical
  selection payload across identical documents after every target independently validates its
  requested IDs. Sparse history and atomic failure behavior remain exact.
- A topology-stable sparse transform publishes one prevalidated asset batch and one scene mesh
  notification batch. It never deselects/reselects unchanged scene entities and never increments
  asset/scene revisions once per transformed object.
- Dense authored IDs use their append-only slot as an O(1) lookup hint with a correctness fallback.
  Flat-shaded transform commits update only affected compiled face ranges while recomputing exact
  normals, generated UVs, tessellation, winding, source IDs, and primitive spans; smooth or changed
  layouts use the full compiler. Compiler output and per-face scratch allocations are retained.
- Persistent selection and volatile hover/tool overlays have distinct invalidation streams. Face
  fill and mask layers share one tessellation pass, dense overlay lookup avoids per-document hash
  tables, and open-edge diagnostics compute normals/ticks only for selected boundary edges.
- Tool-preview suppression is occurrence-scoped for instance edits. Direct meshes retain broad
  source suppression; instance fills, wires, and scene wireframes match the exact source member or
  definition plus occurrence.

Reference: Blender's BMesh path retains indexed element tables and uses partial edit-mesh
tessellation/normal updates from changed vertices. Quader follows those ownership principles while
retaining its portable immutable document boundary.

Owners: portable polygon/session selection and compilation, mesh projection, SDK projection,
asset/scene batch publication, modeling overlay construction, render extraction, instance cache,
and scene render bridge.

Verification: Fast Iteration Workflow uses the Tracy-enabled native app build as the handoff gate.
`cmake --build --preset vs2026-debug --target quader` succeeds with Tracy and on-demand capture
enabled. A new `profiling_6.tracy` is required for measured confirmation.

## 2026-07-19 - P6 Equal Domains, Shared Projection, And Moving Overlays

Task/context: `profiling_6.tracy` proved that sustained navigation had reached roughly 7-13 ms
frames, but component-box completion and transform release still produced 0.9-1.8 second
main-thread frames. Selection replacement preparation consumed 99.58% of component publication;
transform release compiled 2,490 mostly identical mesh results and then rebuilt the complete
semantic selection overlay. The dense-transform fallback also incorrectly hid overlays above
4,096 positions.

Invariant:

- One canonical component-selection payload validated against a document may be transferred to
  another document only after an exact live vertex, edge, or face domain comparison. Equal domains
  do not repeat selected-ID validation or canonical vector comparison; different domains retain the
  full stale-ID checks.
- Topology-stable transform publication groups inputs by the exact retained geometry/authored
  payload identity and exact position updates. One representative is canonically projected and its
  immutable result is shared by every identical input; hash matches are always collision-checked.
- Dense transform overlays never disappear as a performance fallback. Point, line, source-wire,
  and face proxies retain immutable topology/style streams and separate mutable Filament position
  buffers; drag ticks update only retained position slots.
- Vertex overlay membership is O(V), including an all-selected fast path, never O(V * selected-V).
  Identical per-object semantic overlays reuse one exact local-geometry/selection template before
  world projection, while object IDs and transforms remain occurrence-correct.
- Performance captures use the Tracy-enabled optimized Release executable by default. Debug `/Od`
  captures remain useful for correctness diagnostics but are not valid Blender performance
  comparisons.

Remaining architectural floor: Blender retains dense edit flags, adjacency/partial-update state,
integer selection-ID buffers, and indexed GPU edit batches. Quader still needs those owners to
remove CPU rectangle candidate DTOs, immutable-vector selection publication, and full semantic
overlay aggregation for arbitrary non-identical dense scenes.

Owners: portable polygon/session selection, SDK projection adapter, modeling overlay service,
render overlay builders/cache/factory, and Tracy launcher tooling.

Verification: Fast Iteration Workflow skips automated suites. Debug built successfully during the
retained-overlay implementation; the optimized Tracy Release native build is the final handoff
gate, followed by a same-scene `profiling_7.tracy` capture including vertex, edge, face, translate,
rotate, and scale samples.

## 2026-07-19 - P7 Moving Depth, Linear Overlay Aggregation, And Atomic Duplication

Task/context: optimized Release `profiling_7.tracy` confirmed low-millisecond navigation,
component box selection, transform ticks, and ordinary transform commits, but exposed three
discrete regressions: moving component meshes appeared X-ray, semantic overlay acquisition reached
1.861 seconds, and linked duplication of 1,568 selected objects blocked transform start for 3.216
seconds.

Invariant:

- The overlay Filament view has an independent depth buffer. When a transform preview suppresses a
  source mesh's static overlay-depth proxy, the moving preview must provide a depth-only companion
  on `kViewportOverlayLayerMask` / `kViewportOverlayDepthChannel`. The companion shares the visible
  preview's dynamic vertex/index buffers and receives matching bounds, transforms, instances, and
  visibility, so one position upload feeds both passes.
- `SourceWireDepthStamp` remains non-rendered visibility metadata. Persistent component source wire
  and a non-sticky component hover candidate emit it by direct face tessellation with authored
  vertex IDs, while the old full `build_overlay` path stays removed. Point-layer visibility must
  construct the stamp filter and retain the full camera signature because its CPU occlusion result
  changes with camera pose.
- Filament `vertexDomain : device` materials consume OpenGL NDC and then map Z to reversed device
  depth. GPU ribbon and point shaders divide clip XYZ by clip W, convert clipFromView's already
  reversed `[0, 1]` Z back with `1 - 2 * z`, apply the existing GL-domain depth bias, and submit
  `w = 1`. Passing the already-reversed Z through DEVICE causes a second remap, making rear
  component wires/vertices win `GreaterOrEqual` and appear X-ray.
- Many-object semantic overlay aggregation totals triangle, line, and point records first, reserves
  each combined vector once, and then appends linearly. Never call exact
  `reserve(current_size + source_size)` once per object; that reallocates and recopies the complete
  accumulated frame quadratically.
- Ordered multi-object duplication is one SDK session mutation with one source index, one undo
  snapshot, one selection publication, and one state transition. The host adapter preserves
  source/result order and existing rollback checkpoints; it must not call per-object duplication
  in a loop because each call copies the growing session state.

Reference: Blender's edit overlay draws a current-position depth prepass before edit faces, edges,
and vertices, and its selected-object duplication/transform setup processes the selected set in one
dense pass rather than publishing one full state transition per object.

Owners: tool-preview renderable cache and mesh factory, component semantic overlay construction,
portable modeling session duplication, and the SDK-to-native projection adapter.

Verification: under Fast Iteration Workflow, automated suites were skipped. The combined
Tracy-enabled `vs2026-release` native `quader` target builds successfully. A same-scene follow-up
trace must cover vertex, edge, and face box selection plus translate, rotate, scale, and linked
duplicate drag; `profiling_7.tracy` had no edge/face box samples.

## 2026-07-20 - Retained Material Uniforms Invalidate At Cache Ownership

Task/context: `profiling_new_2.tracy` captured two experiments that were subsequently removed. Its
camera-driven persistent point rebuild produced roughly 61 ms navigation frames; the known-good
baseline path was restored before any new optimization. The baseline still recorded 17,950,472
`MeshRenderableFactory::update_shaded_lighting` calls and 6.820 seconds inside
`SceneRenderBridge::update_mesh_surface_grid` across 7,937 frames.

Invariant:

- Viewport grid and Shaded-lighting parameters are global presentation state, not per-frame mesh
  work. `InstanceRenderableCache` and `ToolPreviewRenderableCache` retain the last applied narrow
  signature and return before traversing proxies while it is unchanged.
- The unchanged-state gate belongs inside each proxy-owning cache. Every path that destroys or
  creates material instances marks that cache dirty, so newly created transform-preview and
  explicit-instance proxies receive the current settings before their first render even when the
  settings signature itself did not change. A bridge-wide gate is forbidden because it cannot see
  later cache-local proxy generations.
- Dynamic-position, transform-only, and visibility-only updates retain existing material instances
  and do not invalidate global material parameters.
- Blender uses one Workbench world UBO and one Overlay theme UBO per engine/pass sync rather than
  rewriting global lighting/theme parameters per object. Quader keeps Filament material-instance
  parameters but follows the same frequency principle through cache-local revision gates.
- Transform preview material fidelity is a separate correctness issue: the baseline preview DTO
  narrows the full `MaterialComponent` to one asset ID, loses material-slot overrides/project
  material resolution, and always uses the Shaded material family. Do not conflate that follow-up
  with global uniform invalidation.

Owners: `InstanceRenderableCache`, `ToolPreviewRenderableCache`, `MeshRenderableCache`,
`SceneRenderBridge`, `QtViewportPanel`, material binding, and render source-policy guardrails.

Verification: the optimized Tracy Debug/Profile and Tracy-free Release `quader` targets both build
successfully. Automated tests and desktop launch remain intentionally skipped under the user's
standing manual-verification instruction. The next same-scene trace should show unchanged frames
entering both cache update functions without `Instance material uniforms apply` or
`Tool preview material uniforms apply`, and factory lighting calls should scale with cache rebuilds
or settings changes rather than frame count.

## 2026-07-20 - P9 Transactional Preview Generations And Topology-Stable Rebind

Task/context: `profiling_new_3.tracy` showed that an ordinary one-object drag could still trigger
large main-thread spikes even though steady navigation and dense drag ticks had improved. The
remaining work was renderer lifecycle, not modeling math: preview extraction narrowed the scene's
material contract, changing preview cardinality synchronously destroyed thousands of Filament
objects, suppressing one instance source rebuilt complete retained batches, and a topology-stable
dense commit replaced all canonical mesh proxies then left a large deferred-destruction backlog.

Invariant:

- Every modeling preview carries the complete `MaterialComponent`, including project material
  references and per-slot overrides. Render extraction resolves that component through the same
  project-aware resolver as scene meshes and includes resolver/material revisions in its cache
  signatures. Rendered, Shaded, and overlay-depth preview siblings share immutable geometry but
  use their pass-correct material binding; a drag must not visibly replace authored materials.
- Preview publication is transactional. A hidden candidate generation is completely created and
  validated before it is made visible; replacements become visible before the previous current
  generation is hidden. Allocation or validation failure preserves the previous preview and source
  coverage, does not advance the bridge submission revision, and is retried by the next sync.
- The preview cache retains the current generation and one warm predecessor. Exact structural and
  topology comparison is collision-safe and permits dense -> small -> dense reuse. The ordered
  active-generation path updates dense previews without building a per-tick hash map. A third
  generation is detached from the Filament scene with one bulk Scene call, moved to a retired
  queue, and destroyed only through explicit idle maintenance, one proxy per idle slice.
  Navigation and modal/modeling drags never pay retired-preview destruction. The cache does not
  retain transient dynamic-position payload ownership after submission: it trusts the monotonic
  position revision so the modeling workspace buffer pool can reuse its buffers on the next tick.
- Instance suppression is explicit visibility state on a stable occurrence/chunk plan. Suppression
  lookup is sorted and evaluated once per logical occurrence, not once for each render variant.
  Unchanged visible membership uploads only retained transforms/bounds. A changed active count
  rebinds the affected RenderableManager component while preserving its entity, geometry,
  InstanceBuffer, transform, and material instances; unrelated chunks are untouched. Hidden
  occurrences are never represented by singular, far-away, or duplicate transforms.
- A scene mesh update with the same mesh identity, material/pass family, primitive count, index
  layout, and material-slot layout rebinds replacement immutable geometry through
  `RenderableManager::setGeometryAt` on all three canonical proxies. The operation prevalidates
  ownership and live Filament bindings and acquires every replacement reference before mutation.
  It preserves entities and material instances and enqueues no retired proxy. Any topology,
  material, ownership, or layout mismatch keeps the full replacement fallback.

Regression risks:

- Never move Filament destruction back into preview publication or an interaction frame. Retired
  objects must already be detached before their budgeted destruction begins.
- Never retain the transient dynamic-position payload in a preview generation or submission-state
  cache; doing so pins a workspace-pool buffer and restores per-tick allocation pressure.
- Never infer topology equality from a layout hash alone; exact indices, primitive spans, and
  material-slot structure remain mandatory collision checks.
- Do not advance render-preview revision gates after a failed candidate publication, or the old
  source can stay suppressed without a visible replacement.
- Dynamic preview uploads currently update positions. Normal/tangent deformation fidelity is a
  separate follow-up and must not be hidden inside these lifecycle paths without a measured design.

Owners: modeling preview DTO/projection services, render extraction and material resolution,
`ToolPreviewRenderableCache`, `SceneRenderBridge`, `InstanceBatchPlanner`,
`InstanceRenderableCache`, `MeshRenderableCache`, `MeshRenderableFactory`, and the viewport's
explicit idle-maintenance gate.

Verification: the canonical Tracy-enabled optimized Debug/Profile and Tracy-free Release native
targets both build successfully after this change. Automated tests and desktop launch remain intentionally skipped by
the user's standing manual-verification instruction. The follow-up capture should use the same
784-mesh scene and include select-all component translate/rotate/scale, dense -> one-object -> dense
drag cardinality changes, a single instance-occurrence drag, and commit. During interaction,
Tracy should show no retired-preview destruction, suppression should touch only affected instance
chunks, and topology-stable commit should report geometry rebinds without growing retired-proxy
debt.

## 2026-07-27 - Retained Qt Presentation And Narrow Frame Queries

Task/context: tool UI and a pivot operation in a roughly 6,000-mesh scene exhibited multi-second
stalls. The broad Qt/UI audit found that retained widgets were cheap; repeated owner projection,
model construction, string allocation, Qt setters, application-wide event filtering, and render
state submission from continuous frame paths were not.

Invariant:

- Put the cheapest owner revision check before catalog/selection projection, descriptor and string
  construction, model resets, geometry work, and Qt setters. Active-tool presentation uses a
  dedicated revision. The Outliner uses a hierarchy-presentation revision that does not advance
  for transform/pivot, material, mesh-payload, or neutral layer metadata changes.
- Dock and auxiliary-window state is event-driven. Use `QDockWidget::visibilityChanged` and
  `QDialog::finished`; debounce durable `QSettings` writes. Do not poll `visibleRegion()` or save
  window state from each frame/presentation callback.
- Application-wide event filters reject irrelevant event types before QObject casts or owner
  queries, and the viewport pointer filter exists only while a modal tool owns viewport input.
- Stable HUD, gizmo, pane, split-handle, material-preview, camera, scene-setting, grid, shadow, and
  layer presentation retains its last effective values. Do not repeat unchanged geometry,
  visibility, z-order, repaint, or Filament setter work.
- Console presentation consumes incremental log deltas, batches row insertion/removal, and
  debounces search. Do not snapshot and rebuild the complete retained model for every new entry or
  keystroke.
- High-frequency tool paths expose narrow queries. Viewport operation-preview routing consumes one
  boolean, not a full settings target containing labels and IDs. Stable Cut hover borrows the
  projected session and retains its compiled two-point overlay until a semantic owner revision
  changes.
- Coalesced slider edits are transaction-aware: flush at semantic commit/lifecycle boundaries and
  discard pending values when an authoritative document replacement or clear occurs.

Owners: `UISystem`, `UIPanelManager`, Outliner and Console panels, `QtViewportPanel` and its native
surfaces/HUD/gizmo, Standard Material editor/preview, `SceneDocument`,
`ModelingSessionService`, renderer viewport/grid presentation, and source-policy guardrails.

Verification: 37 focused UI/behavior tests and 148 focused architecture/source-policy tests pass.
The Cut regression performs 32 distinct hover moves without another mesh compile after initial
preview construction; hierarchy tests prove transform/pivot-relevant changes retain Outliner rows.
On `one.qdr`, the Debug/Profile stress harness loads 6,276 entities and applies the selected-mesh
pivot in a five-run median of 20.91 ms (20.15-21.89 ms), with one targeted document sync, no
additional full-scene reset, and zero hierarchy-presentation revision change.
The complete app suite passes 1,234 tests with six unrelated existing failures in command-baseline,
linear-array render-preview, and source-wire overlay contracts. The complete guardrail suite passes
361 tests with three unrelated existing root instruction/CMake contract failures. Modeling-domain
and SDK consumer/parity suites pass; the nested SDK-only check still crashes in its existing
isolated test configuration.

## 2026-07-27 - Unified Selection Overlays And Persisted Perspective Camera

Task/context: disabling viewport outlines left selection cages and retained wires visible, grouped
meshes used a different-looking expanded outline, multi-mesh selection lost its face fill, and
reopening a scene reset its Perspective camera. Experiments with a separate active-selection color
were ultimately removed in favor of the original uniform selection presentation.

Invariant:

- `ShowOverlays` gates every selection-produced presentation path: modeling selection frames,
  scene hierarchy wires and cages, selected-light markers, retained object/instance wires,
  selection fills, marquee feedback, and scene/modeling tool overlays. Gizmos and unrelated tool
  handles keep their separate policy switches.
- A selected group outlines descendant mesh topology with the normal `SelectedEdge` layer and
  Selection Edge Line Size. Prefer authored polygon wire segments over triangle-derived edges.
  Formal instance occurrences expand their definition-member meshes, and transform-only cache
  refresh composes occurrence, inverse-definition, and member transforms exactly. The group bounds
  cage remains the independently configured bounding-box presentation.
- Selecting a scene-owned group hides latent component-selection presentation without destroying
  the component state. Undo exposes the restored component selection; redo hides it again. The
  per-frame ownership decision uses the primary scene selection and constant-time modeling
  ownership lookup rather than allocating or sorting the selected-entity set.
- Active selection remains semantic tool/document state but has no separate visual color or
  setting. Active and non-active selections share the configured Selection Wire, Bounding Box
  Wire, and Selected Vertex colors. Do not add active-only overlay style roles or persistence keys.
  Retained instance/wire caches still consume the normal configured wire color and width and prune
  unused resources on success and failure paths.
- Retained selected-object face fills remain enabled for every non-empty mesh selection, not only
  a single selected mesh. Transient object-fill triangles stay stripped when the retained cache is
  their owner so the two paths never draw duplicate fills.
- QDR scene schema v5 appends an optional validated Perspective camera pose (`eye`, `target`, and
  `up`). Scene save captures it before local-visibility projection, older schemas remain readable,
  open/new/switch restores it through `ViewportWorkspaceService`, and camera signatures participate
  in scene dirty tracking. Direct viewport input publishes a dirty transition only when the pose
  signature changes.
- Canonical Profile tests link the Release-mapped GTest DLLs. Every GTest executable deploys its
  mapped runtime closure explicitly, and the native app's shared-output cleanup must preserve
  `gtest.dll` and `gtest_main.dll` whenever tests are enabled. Do not rely on vcpkg's configuration-
  named app-local hook here because the canonical `Debug` path intentionally uses Release imports.

Owners: modeling selection projection and session services, render overlay policy/builders/caches,
Qt viewport orchestration, viewport camera/workspace services, scene/project sessions, native QDR
reader/writer/codec, public viewport/project APIs, and command behavior contracts.

Verification: focused grouping, overlay, cache, camera, project-workspace, codec,
public API, source-policy, dependency-matrix, command behavior contract, and direct executable
loader suites pass. The final Debug regression gate runs 1,261 app tests and passes 1,253 with
three local-stress skips; its five failures are the pre-existing linear-array preview and
source-wire depth/signature regressions recorded above. Modeling-domain and main SDK
consumer/parity tests pass. The isolated SDK-only tree now configures, deploys the mapped GTest
runtime, and executes its consumer test instead of failing at process load; its wrapper inherits
only the existing root guardrail failures. After reconciling the selection-cage helper with the
architecture contract, the guardrail binary has three remaining instruction/CMake failures out of
366 tests, all predating this task.

## 2026-08-04 - Cold Qt Presentation, Coalesced Tool Edits, And Material Gestures

Task/context: the first native viewport reveal retained a stale child-window size until input,
Align disappeared after another menu synchronized component selection, RMB popup labels reused
verbose keybinding prose, synchronous tool previews starved retained Qt controls, first-use warning
dialogs painted white while cold resources were created, Project Home lacked recent-file metadata
and actions, and viewport material workflows required direct gestures.

Invariant:

- A newly revealed `createWindowContainer` must reassert its owning frame rect exactly once after
  `show()` and before the startup exposure turn. Do not turn this into a steady-state geometry
  setter. Align remains top-level-visible in every selection mode; its children use their narrow
  command predicates. Viewport popup wording has its own `ui.viewport.context_menu.action.*`
  localization namespace, separate from verbose command/keybinding labels.
- Numeric tool edits retain the latest local slider/spin value and use one restarted trailing
  dispatch. A pending edit captures the settings revision, tool revision and identity, and preview
  lifecycle; discard it if any owner changed before expiry, and rebase the remaining queue after a
  successful write. Exact release, editing-finished, and Apply boundaries flush. Reset and Cancel
  discard on button press before focus loss can emit `editingFinished`; unbind and structural rebuild
  also discard. While an edit is pending, Inspector and retained toolbar projection stay frozen,
  then catch up once. Standard Material polling is behind the workspace event gate, and hidden
  Material Browser windows do not query catalogs or previews.
- Reusable native confirmation dialogs own their layout, buttons, icons, and exact native window for
  the UI lifetime. Prewarm the hidden platform window only after startup presentation is enabled;
  each invocation refreshes localized text and resets its result. Their `done()` path records the
  result, hides the retained dialog, and emits the normal signals without destroying the HWND.
- Project Home uses one equality-gated recent-project model and one shared actions menu, never
  per-row widgets or filesystem polling. The recent-project store captures stable manifest size and
  timestamp metadata; platform-owned reveal selects the manifest or falls back to its nearest
  existing parent.
- Material RMB actions are canonical hover-owned viewport input targets: Shift+Click samples the
  project-backed material under the pointer; Ctrl+Click captures the hovered object and face at
  click time, then assigns the active material to that face in Face mode or the whole hovered mesh
  in Vertex, Edge, Object, and Group modes without requiring or changing selection. Ctrl+Double
  Click normalizes the clicked face's existing material across its mesh. Single/double
  disambiguation follows the effective editable click pair and a steady-clock deadline. A pending
  single captures scene, mode, object, face, and active project material and revalidates scene,
  mode, target, and material at expiry. Bare RMB is reserved by fly navigation and the viewport
  popup and must be rejected by keybinding assignment, not silently accepted.
- Whole-mesh material normalization is one SDK mutation, not repeated immutable per-face edits. It
  preserves object/component selection, publishes one content revision, and records one exact
  undo/redo memento. Embedded references are valid for clicked-face normalization; only project
  identities can become the ProjectSession-owned active material.

Owners: Qt viewport/startup presentation, main menu and context menu localization, Tool Settings
and `UISystem` revision gates, Project Home/recent-project/platform shell, viewport input resolver
and interaction controller, native project/modeling APIs and history, and SDK polygon documents.

Verification: focused retained-dialog, Tool Settings, menu, Project Home, project/material, input,
and viewport suites passed (86/86 and 81/81); focused UI/source guardrails passed 8/8; the bevel,
project-material, SDK consumer/parity, and modeling-domain suites passed. The mandatory regression
gate built all targets and accepted the task-owned command baseline, then stopped in full CTest on
eight pre-existing unrelated instruction/build, linear-array, and overlay failures. Both canonical
`quader` builds succeeded. A fresh Debug launch confirmed the Project Home layout and recent actions,
an immediately filled viewport, and Align remaining visible with File open in Edge mode. On the same
dense scene, the steady sample stayed effectively flat: 57.6 FPS / 1.25 ms CPU / 0.34 ms GPU before,
versus 58.7-58.8 FPS / 1.09-1.30 ms CPU / 0.34 ms GPU after.

## 2026-08-25 - Accessibility-Aware Obsidian Qt Palette

Task/context: the user explicitly requested a modern, original, Obsidian-like application color
scheme while retaining the native Qt Widgets shell and later required verification without visual
inspection.

Invariant:

- This entry supersedes the current-sounding palette restrictions in the 2026-07-12 Stock Fusion
  and Qt Widgets/ImGui parity entries. `Fusion` remains the single application style, but its
  unmodified `standardPalette()` is now only the input to the app-owned palette factory, not the
  final application palette. The prohibition on an application-wide stylesheet remains.
- `qt_application_theme.*` is the one normal-contrast theme owner. It defines layered warm
  near-black surfaces, warm neutral text, restrained borders, and amethyst interaction roles for
  active, inactive, and disabled groups. Normal text, selection text, focus/selection accents,
  semantic axis labels, and relevant control-state boundaries retain explicit contrast tests.
- A Windows High Contrast transition unsets Quader's forced dark color scheme and installs an
  unresolved `QPalette` so Qt continues resolving live system colors. Returning to normal contrast
  restores the deterministic Obsidian palette. The transition is signal-driven and does not poll
  from the UI frame loop.
- Toolbar hover, checked, pressed, inactive, disabled, and keyboard-focus presentation derives from
  inherited palette roles. Dock-title styling is scoped to each dock and equality-gated on palette
  events; packaged light/dark PNG glyphs remain readable across both title states, with a
  contrasting button tile for mixed system surfaces, and a palette fingerprint invalidates Qt's
  cached stylesheet brushes even when glyph URLs stay the same. Pressed dock borders use the
  contrasting `Highlight` role. Project Home and
  navigation-gizmo chrome consume inherited palette roles rather than copying app colors.
- The fly-navigation crosshair keeps its pre-theme white inner stroke over the dark outline by
  consuming `BrightText`. It must not use `HighlightedText`, which is intentionally dark for
  readable selection text over the amethyst highlight.

Owners: `qt_application_theme.*`, `qt_dock_title_presentation.*`,
`qt_toolbar_button_presentation.*`, `UISystem`, `QtProjectHomePage`, and `QtNavigationGizmo`.

Verification: the focused theme, palette-transition, dock-resource, toolbar, Home, and navigation
tests pass 19/19; the focused UI/build guardrails pass 50/50. The affected-check full CTest matrix
still reports only the repository's pre-existing instruction/build, linear-array preview, and
overlay failures. Both canonical `quader` Debug/Profile and Release builds succeed. No visual
inspection was performed at the user's request.

## 2026-08-25 - Shift Extrusion Closes Poly-Created Open Face Regions

Task/context: Shift+Move extrusion of the single face created by the Poly tool moved the face and
created its perimeter walls, but left the original footprint open.

Invariant:

- Move/rotate extrusion through `extrude_selected_faces_with_copied_vertices` retains a
  reverse-wound source cap when the selected region has a non-empty boundary and every boundary
  edge had exactly one incident document face before mutation. This closes free face regions such
  as Poly output.
- The original selected face ID remains the moved cap, so selection, preview, and undo/redo
  identity remain stable. The added source cap preserves the original material, normal-shading,
  and reversed loop UV metadata.
- A face extruded from a closed mesh must not gain an overlapping internal source cap, and inset
  continues to discard its source region.

Owner: `src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_surface_operations.cpp`.

Verification: direct polygon tests cover the closed 8-vertex/6-face result, coherent winding,
metadata, transform extrusion, and the closed-mesh exclusion; the SDK consumer test starts from
the public Poly tool and reports zero open authored edges; the BehaviorScenario test exercises the
semantic Shift+Move drag and verifies closed compiled geometry plus undo/redo and selected-cap
identity. The polygon loop (212 tests), SDK consumer (76 tests), SDK parity (4 tests), and focused
BehaviorScenario pass. Both canonical Debug/Profile and Release `quader` builds succeed. The full
regression gate remains red only on the repository's unrelated instruction/build guardrails,
linear-array preview, and overlay tests. No visual inspection was performed.

## 2026-08-25 - Global And Local Transform Space

Task/context: the user requested a toolbar control and `T` shortcut for Global/Local transforms,
with active-mesh axes for multi-mesh Move, each mesh's own axes for multi-mesh Rotate/Scale, and
active-component orientation in vertex, edge, and face modes.

Invariant:

- `ViewportWorkspaceService` owns one transient, revisioned transform-space value. The public
  viewport API, `viewport.toggle_transform_space` command, `T` viewport binding, View menu, and
  checkable toolbar action project that value; `Shift+T` remains Apply Material. UI synchronization
  is revision-gated before command lookup, string formatting, or Qt setters.
- A Local mesh frame uses the normalized columns of the exact displayed affine world matrix. This
  keeps gizmo axes parallel to the rendered mesh through hierarchy scale/shear and instance
  occurrence transforms. Oriented Box/Poly/primitive creation keeps canonical local geometry and
  stores its construction frame in the scene transform instead of baking an unqueryable rotation
  into vertices.
- Local Move maps one logical delta through the active mesh frame for every selected mesh. Plane
  movement and repeat use the full basis inverse, rather than dot products that assume an
  orthogonal frame. Rotate rings remain perpendicular to their exact rendered axes.
- Local Rotate and Scale apply the same logical-axis delta in each selected mesh's own authored
  frame. Each target pins its own visible/custom pivot, including off-origin geometry in a
  multi-selection; the hidden scene position may change to compensate, while the displayed mesh
  center stays fixed. Cancel, history, signed scale, selection, and repeat semantics are preserved.
- Component frames are deterministic: face Z is its normal and X its first winding edge; edge X is
  canonical endpoint order and Z derives from incident normals; vertex Z derives from incident
  normals and X from its lowest-ID usable neighbor. The explicitly active component drives every
  selected component, the frame is converted through nonuniform/mirrored and occurrence transforms,
  and it is frozen when a drag starts.
- Component selection must preserve a valid explicit active element. Rectangle selection,
  full-object compression, Select All, and Invert publish active edge/vertex indices; additive box
  selection keeps the prior active component while it remains selected. Exact multi-object SDK
  projection preserves explicit actives and synthesizes a per-object canonical active only where a
  nonempty native selection lacks one.
- Repeat stores transform space and logical orientation. Local Move remaps its logical delta through
  the current active mesh/component frame; Local Rotate/Scale replay the recorded frame and affine
  semantics.

Owners: viewport workspace/public command APIs, Qt tool toolbar/menu, viewport transform-gizmo
interaction, scene/modeling transform drag services, modeling component-orientation runtime,
selection policy/projection adapter, and SDK polygon selection/transform operations.

Verification: the corrected Local-transform repro set passes 17/17 across app and modeling-runtime
tests, including rendered-axis alignment and single/multi off-origin visible-pivot rotation/scale;
the command behavior contract also passes after its intentional baseline update. In the mandatory
regression gate, SDK consumer, SDK parity, and modeling-domain suites pass, and the app suite passes
1328 tests with 3 skipped. The gate remains red only on seven pre-existing instruction/build
guardrails and five unrelated linear-array/overlay tests.

## 2026-08-26 - Legacy Local Frames And Sub-Grid Uniform Scale

Task/context: saved boxes created before canonical Local transforms could carry the construction
frame and center in authored vertices as well as a scene rotation. Their rendered outline therefore
disagreed with the gizmo/texture frame and Local rotation orbited far from the visible mesh. The
uniform center scale handle also treated one grid interval as a hard minimum.

Invariant:

- QDR scene schema 6 persists `ModelingObjectComponent::local_frame_version`; older schemas decode
  as unknown/legacy and all newly projected objects are marked canonical. Tombstones, duplication,
  extraction, mirroring, and SDK history preserve the marker.
- Legacy migration runs only while `SceneSession` installs or replaces a document, before modeling
  hydration and the clean baseline. Generic runtime resets never migrate scene/assets outside
  history. Both legacy `sdk_owned` values are eligible because hydration owns that transition.
- Automatic recovery accepts only the exact old unsegmented Quader cube topology and a rigid
  source replay. Old schemas contain no creation provenance, so this exact geometry signature is
  the deliberate compatibility policy; deformed or unrecognized meshes remain untouched.
- For recovered frame `B`, authored vertices become `B^-1 p`, the object local matrix becomes
  `L * B`, and every direct child becomes `B^-1 * C`. Every target/child TRS is validated before
  mutation, shared assets use clone-on-write, and failure rolls back atomically. Generated UVs are
  materialized before rebasing so the existing texture mapping does not move.
- Grid-snapped shrinking remains quantized while a full grid interval fits, then uses the raw
  positive factor down to the existing safety floor. The center `All` handle can therefore shrink a
  fresh one-unit box below creation size. Outward snapping is clamped to never reverse direction
  and shrink non-grid-aligned bounds.

Owners: `SceneSession`, `ModelingLegacyLocalFrameUpgradeService`,
`ModelingMeshSyncService`, scene/QDR local-frame metadata, and transform-tool math.

Verification: the saved-frame preservation/load regressions pass 4/4, including world geometry,
texture UVs, children, visible center/pivot, first-load hydration, clean history, marker-only,
single-reference replacement, and repeated-reset idempotency. Focused Local-transform app tests pass
17/17; focused runtime orientation/transform/scale tests pass 19/19; command/UI/guardrail and SDK
consumer/parity filters pass. The mandatory gate remains red only on six pre-existing
instruction/build guardrails and five unrelated linear-array/overlay tests.

## 2026-08-26 - Material Browser Presets And Transactional Texture Import

Task/context: the Material Browser's continuous tile slider stopped producing useful preview growth
at large values and could hide the remaining grid content. The browser also needed a native,
multi-file texture-import workflow that creates one persistent Standard Material per image without
disturbing the active material, an open editor draft, or project history.

Invariant:

- Tile size remains persisted in pixels, while the retained `QSlider` exposes indices `0..5` mapped
  exactly to `16, 32, 64, 128, 256, 512`. Legacy values normalize to the nearest preset, exact ties
  choose the smaller preset, and the default remains `128`. Tick interval and movement steps are
  one index, with ticks below the control.
- Browser previews are decoded into aspect-preserving thumbnails bounded to 512 pixels before icon
  construction. Grid icon/cell geometry uses the mapped pixel preset and scrolling remains enabled
  at 512 so labels and later materials stay reachable.
- The browser remains a parent-owned `Qt::Tool`, implemented as a retained `QMainWindow`, with a
  localized `File > Import...` menu. The widget emits only one semantic import callback; `UISystem`
  owns the native picker and public project-assets API call, and catalog refresh remains behind the
  existing asset event/revision gate rather than frame-loop polling.
- Batch texture import validates the established 64 MiB, 8192-pixel, still-image, RGBA8 rules,
  preserves exact encoded bytes, and creates one canonical Standard Material per selected source
  with an sRGB albedo and UUID-qualified material/texture paths. Repeated source paths are processed
  independently.
- Each material/image pair is a retained atomic transaction until the single final catalog
  scan/install. A per-file failure rolls back that pair and processing continues; a global catalog
  failure rolls back every pair created by the batch. A successful batch publishes exactly one
  `project.assets_changed` event and never changes the active material, Standard Material draft, or
  project/scene history.
- TGA decoding explicitly prefers TGA when its footer identifies the format because Qt can confuse
  the leading bytes with CUR, while footerless TGA retains a controlled fallback. Release image
  plugins and codec closures for GIF, JPEG, TGA, TIFF, and WebP deploy to both app and test runtime
  outputs; debug-suffixed Qt/plugin DLLs remain rejected and dependency/license evidence stays out
  of SDK-only builds.

Owners: Material Browser Qt window, `UISystem`, native platform dialogs,
`QuaderProjectAssetsApi`, `ProjectMaterialImportService`, shared material persistence helpers,
`ProjectImageDecoder`, project catalog/session owners, and native deployment/license guardrails.

Verification: focused UI, decoder, storage, API, and project-material suites pass, including all six
tile presets, the TGA regression, partial import/rollback behavior, exact-byte persistence, one-event
publication, and state preservation. Both canonical Debug/Profile and Release app builds pass, and
their runtime directories contain the required Release image plugins and codec dependencies with no
debug-suffixed plugins. The mandatory regression gate and full CTest remain red only on the same six
pre-existing instruction/build guardrails and five unrelated linear-array/overlay tests; manual UI
smoke was started but interrupted before visual confirmation.

## 2026-08-26 - Hover-Target Materials And Structured Viewport Context Menu

Task/context: material sampling and assignment needed to act on the mesh under the pointer without a
selection, while the viewport RMB menu needed to separate mode-compatible modeling operations from
scene/object and general editor commands.

Invariant:

- Shift+RMB samples the project material on the hovered face without requiring or changing
  selection. Ctrl+RMB captures the hovered object/face at click time; Face mode assigns only that
  face, while Vertex, Edge, Object, and Group modes assign the whole hovered mesh. The delayed
  single-click intent never retargets to a later hover or selection change, and Ctrl+double-click
  still normalizes the clicked face's existing material across its mesh.
- Direct active-material assignment validates a live, editable object and face independently from
  whether the face already has a material. It stages a missing native material component when
  necessary, preserves unrelated object/component selection, and records one exact modeling
  memento for undo/redo.
- A qualifying RMB release evaluates `available_action_set()` exactly once. Object mode presents
  mesh-compatible modeling commands under `Mesh`; component modes use `Vertex`, `Edge`, or `Face`;
  scene/hierarchy intent lives under `Object`. Group mode has no modeling submenu. Utility material
  and framing actions and the Cut/Copy/Paste block remain separated by top-level dividers.
- The material under the pointer is captured before the context-menu selection pick. Set Material
  uses that captured project identity; Edit Material passes it to the UI-owned retained-document
  lifecycle that opens or raises the Material Editor, without changing the active material.
- Context-menu shortcut text comes from the current command/keybinding owner and is appended as
  display-only `\t` text. Exact click tokens are abbreviated there as `RMouse` and `LMouse`, while
  canonical `RightMouseClick` and `LeftMouseClick` binding/persistence strings remain unchanged.
  The popup never installs `QAction` shortcuts or enumerates the broad command catalog.
- Top-level dividers use a menu-local `palette(mid)` separator rule. This makes them visible with
  the Obsidian/Fusion palette while preserving normal item, hover, selected, disabled, focus, and
  submenu styling and remaining compatible with alternate/high-contrast palettes.
- `edit.cut` is the typed Ctrl+X clipboard command. It preflights both copy and deletion, copies
  through the project/instance-aware clipboard path, then delegates deletion to the existing
  scene-aware/component command path. Undo/redo owns the deletion while the copied clipboard
  payload remains pasteable.

Owners: viewport input/controller picking, public modeling and command APIs, modeling session and
history services, Qt viewport panel/context-menu presenter, `UISystem` Material Editor document
lifecycle, localization/keybinding owners, and command behavior contracts.

Verification: the focused integrated material/API/input/context/localization suite passes 26/26;
the focused UI source guardrails pass 2/2; and the complete command behavior contract passes 4/4.
The final affected UI/viewport workflow passes 410/410. Its live Fusion/Obsidian popup regression
renders the menu and verifies both divider regions contain the `QPalette::Mid` separator color,
checks the exact `Shift+RMouse` display label with an empty `QAction` shortcut, and selects Edit
Material through Qt's popup event path to verify the hovered project ID reaches the Material Editor
callback.
The intentionally regenerated baseline adds only `edit.cut` (158 -> 159 commands); existing entries
change only aggregate/preference fingerprints from the new default Ctrl+X binding. In the mandatory
gate, modeling-domain, SDK consumer/parity, and all task-owned app tests pass. The complete app suite
passes 1359 tests with 3 skipped and remains red only on two unrelated Linear Array plus three
overlay tests; guardrails pass 368/374 and retain the same six pre-existing failures, including in
the SDK-only wrapper. Both canonical Debug/Profile and Release `quader` builds pass. No manual GPU
or popup visual smoke was performed.

## 2026-08-26 - Inspector Materials And Find/Replace

Task/context: Mesh, Group, and Face selections needed a material-focused Inspector surface, while
material-reference replacement also needed a standalone active-scene workflow that handled native,
Graph, and embedded identities without moving mutation or topology traversal into Qt UI code.

Invariant:

- `QuaderMaterialsApi` is the public user-intent boundary. `used_materials()` returns distinct
  live-face references for a typed selection or active-scene scope, and
  `replace_material_references()` performs one preflighted history operation. Mesh replacement
  follows selected meshes, Group follows recursive descendants and deduplicated canonical linked
  definitions, Face rewrites only selected matching faces, and Active Scene includes hidden meshes
  and canonical definitions.
- Object, Group, and scene replacement update matching primary and slot bindings. Face replacement
  reuses or allocates a destination slot without disturbing unrelated faces. Any effectively locked
  or read-only match rejects the complete operation; missing/identical matches are history-free
  no-ops, and invalid destinations fail before mutation.
- One editor-owned history command restores exact modeling face slots, native material components,
  selection, and active component state. Replacement preserves the active material and any retained
  Standard Material Editor draft.
- Inspector Material rows are presentation-only: Object/Mesh and Group retain their normal
  properties plus a default-expanded Material category; Face presents only Material; Vertex and
  Edge present no property rows. Each distinct material uses a 56-pixel square preview, elided name,
  and fixed-square Edit/Replace buttons. Unsupported material kinds keep Edit visible but disabled.
- The retained Find/Replace dialog has two arrow-free 128-pixel preview columns. Inspector launches
  pin the selection context and preload Find; Edit-menu launches use Active Scene and start empty.
  The reusable browser keeps live activation in normal mode, while picker mode never changes the
  active material and commits only through Accept.
- Inspector, browser, and dialog share one presentation/thumbnail cache. Asset material-catalog and
  mesh-usage revisions plus scene material-binding, hierarchy, object-map, and modeling-content
  revisions gate refresh before any catalog, topology, or preview work. Project, scene, and pinned
  contextual-selection changes clear retained Find/Replace state.

Owners: `QuaderMaterialsApi`, `MaterialReferenceService`, modeling session/mesh-sync history owners,
`AssetRegistry` and `SceneDocument` narrow revisions, Inspector presentation, reusable Qt Material
Browser, retained Qt Find/Replace dialog, `UISystem`, and localization/menu presentation.

Verification: focused material/API/modeling/Qt/localization suites pass 96/96 and the new ownership,
layout, public-API, localization, and frame-loop guardrails pass 6/6. Both canonical Debug/Profile
and Release app builds pass. The mandatory regression gate and full Debug CTest remain red only on
seven pre-existing instruction/build/architecture guardrails and five unrelated Linear Array and
overlay tests; all task-owned tests pass. Native UI smoke was interrupted by the user before visual
confirmation.

## 2026-08-26 - Snap To Floor Surface Queries

Task/context: object and vertex selections needed deterministic world-down placement against the
visible authored scene, including shared instances and imported meshes, without introducing a
simulation dependency for an editor-only geometric query.

Invariant:

- `src/editor/placement/SceneSurfaceQuery` owns native renderer-neutral CPU-triangle surface
  queries. It freezes one immutable receiver snapshot per operation, shares geometry-keyed local
  BLAS data across identical payloads and formal occurrences, builds a revision-keyed world TLAS,
  performs exact two-sided nearest-hit tests with stable identity ties, and reuses one query scratch
  buffer for a projection batch. Selected source geometry is excluded from the frozen snapshot.
- `SnapToFloorService` resolves both workflows against that snapshot. Object mode promotes nested
  sources, samples each visible descendant AABB at its world bottom-center, and commits one atomic
  transform command. Vertex mode resolves every selected SDK vertex in world space and commits one
  atomic portable position batch through a captured modeling memento. Exact contact is a no-op;
  misses use world Y=0; redo restores captured results and never reruns the query.
- Overlay picking and placement share the neutral `quader::modeling::neutral::Bvh`; do not create a
  second triangle acceleration implementation. Cache construction stays lazy in command execution,
  never command availability or UI refresh.
- Jolt remains deferred until Quader owns persistent rigid bodies, collision response, settling, or
  simulation-driven placement. A later native physics domain must cache collision shapes from
  scene/asset geometry and still commit placement through scene transform commands. This geometric
  feature deliberately makes no Jolt, vcpkg, CMake dependency, or third-party license change.

Owners: native editor placement services and scene/session revisions for query participation and
caching; the portable modeling core for vertex state mutation; document/modeling history owners for
atomic undo/redo; render overlay code as a consumer of the shared neutral BVH.

Verification: the focused Snap/API/query/command contract suite passes 30/30. The opt-in dense
projection benchmark casts 4,096 rays per batch, reports exact brute-force parity, reuses one
scratch buffer, rebuilds no snapshot/TLAS/BLAS on warm runs, and measured a 1.312 ms warm average
versus 14.074 ms cold average on this workstation without imposing an absolute-time gate. The
mandatory regression gate and explicit full Debug CTest keep all modeling-domain, SDK consumer,
SDK parity, command-contract, and task-owned tests green; they remain red only on the same seven
pre-existing instruction/build/architecture guardrails and five unrelated Linear Array/overlay
tests.

## 2026-08-27 - Face Texture Settings And Transform Texture Locks

Task/context: selected faces needed independent texture scale, pixel shift, and relative rotation,
with persistent texture placement policies for object transforms and linked occurrences.

Invariant:

- The portable SDK owns `FaceTextureTransform`. Authored/generated UVs remain the base; evaluation
  rotates about the projection origin, divides by signed nonzero scale, then adds normalized shift.
  Operations that inherit source UV/material data also inherit these settings. Materializing UVs
  must invert the retained face transform before storing the new base, avoiding double application.
- `QuaderModelingApi::face_texture_settings()` and `set_face_texture_setting()` are the native
  user-intent boundary. `FaceTextureEditContext` owns selection tokens and per-face rotation
  baselines; changing selection resets the displayed offset, while Scale/Shift edits do not.
  Entering 10 then 20 degrees means 20 total from the baselines. Preserve accepted double offsets
  through float-backed storage; undo/redo resynchronization uses float rounding intervals rather
  than a fixed tolerance. Multi-object writes preflight atomically and merge only within a numeric
  editing gesture, preserving visibility, locking, shared-mesh, and explicit instance scope rules.
- Shift is stored only in normalized UVs. Its pixel presentation uses the face material's base-color
  dimensions, or 64x64 when unavailable. Project material rendering and face editing share the
  immutable, revision-gated `ProjectSession::material_texture_image()` cache; readout changes after
  replacing a texture never rewrite UV placement or dirty the document.
- Texture Lock defaults on for translation/rotation; Texture Lock Scale defaults off so scaling
  preserves density. Scene-owned `TextureMappingComponent` stores sampling-from-local C without
  modifying shared geometry. `SceneTextureMappingPlan`, owned by document commands, captures
  hierarchy and preferences once and computes C1 = C0 * inverse(M0) * inverse(L) * M1. Semantic
  rotation/scale include their pivot translations; combined edits apply scale, rotation, translation.
  Instance sampling composes member correction D with inverse(B) * Coccurrence * B. Pivot rebases
  preserve effective UVs, and flattening bakes the mapping before clearing it.
- The neutral texture evaluator is shared by compilation, rendering adaptation, flattening, and
  OBJ export. Compiled UV carriers retain authored residuals and use canonical geometric normals,
  independent of smooth shading. Filament binds mapping/cofactor arrays in instance-transform order,
  keeps geometry sharing, and derives normal-map frames from effective UV derivatives. Identity
  mappings bypass correction exactly. Legacy compiled-only assets reconstruct missing triangle
  carriers on load without adopting authored topology or changing UV/shading data.
- QGEO 1.1 and native scene schema 7 persist the new data; older records default to identity.
  Conversion, validation, copy/history snapshots, mesh hashes, and command fingerprints include it.
- The retained Texture Settings section is independent of the active tool. Mixed scale fields show
  editable `--`; multiple-face Shift fields show disabled `--`. Refresh preserves drafts and fold
  state, rejects stale queued edits, and never reopens a user-closed dock. Expensive summaries and
  Qt setters remain behind revision/value gates. The separate toolbar follows Toggle Overlays with
  a divider and two generated PNG icons; both commands deliberately have no default shortcut.
  Keybinding revisions invalidate tooltip caches without broad command-catalog polling. Numeric
  widget tests must install the same C locale as `main.cpp`; translated labels do not change it.

Owners: portable SDK/neutral polygon texture helpers; native modeling API, edit context, history and
SDK projection adapter; scene mapping/transform owners; ProjectSession material image cache;
Filament extraction/material binding; Qt Tool Settings and texture toolbar; native QDR/OBJ adapters.

Verification: 407 focused native/command tests, 50 adjacent material/render tests, all 475 modeling
domain tests, 82 SDK consumer tests, and 4 SDK parity tests pass. The intentional command baseline
adds two commands (161 -> 163); existing command outcomes and revision counts stay unchanged, with
fingerprint changes confined to the added texture state and one compiled-mesh preview. The full
regression gate and affected-file CTest run remain red only on exactly the same seven guardrails and
five Linear Array/overlay failures preserved in the pre-task CTest log. Both canonical Debug/Profile
and Release builds pass, producing `build/vs2026/Debug/quader.exe` and
`build/vs2026/Release/quader.exe`; the app version is unchanged. Automated Debug/Profile Vulkan
startup smoke passes with both mesh materials loaded. Interactive UI verification was stopped by
the user's Escape key, so face-control layout and texture pixel output have no manual verdict.

## 2026-08-27 - Restored Texture Toolbar Visibility

Task/context: Texturing could be checked in View but both texture-lock buttons were invisible;
the Shift labels also needed to omit the pixel unit text.

Invariant/fix: Qt persists a dragged toolbar's preferred extent. A wide saved Viewport Display
toolbar could squeeze the following Texturing toolbar to its stock 36-pixel minimum, which reserves
only its first item (the divider) and overflows both buttons. `QtTextureToolbar::minimumSizeHint()`
must reserve `QToolBar::sizeHint()` so the complete group remains visible in either orientation.
This content-derived minimum needs no fixed pixel width, layout reset, visibility override, or
frame-loop synchronization. Keep user-hidden toolbars and closed docks closed. Shift X/Y labels
omit `(px)` in English, Italian, and fallback text; their values still represent texture pixels.

Owners: `src/ui/qt_texture_toolbar.*`, retained Tool Settings labels, localization resources, and
the existing Qt workspace/Tool Settings test suites.

Verification: the native restored-layout regression reproduced both hidden buttons at 36 pixels
before the fix and passes afterward. It covers legacy and already-collapsed saved layouts,
Home-to-Workspace transitions, View-menu hide/show, hidden-state restore, vertical docking, and a
closed Tool Settings dock. All 44 focused workspace/Tool Settings/localization tests and all 490
affected checks (73 guardrails plus 417 app tests) pass.
The mandatory regression gate reports exactly the same 12 pre-existing failures as the saved
pre-follow-up CTest log (seven guardrails, five Linear Array/overlay tests), with no new failures.
Both canonical Debug/Profile and Release app builds pass; version 0.1.66 is unchanged. Desktop
interaction was canceled with Escape, so this follow-up has native Qt test verification rather
than a manual visual verdict.

Pre-commit style verification: seven local constexpr names were corrected without changing values
or behavior. The full clang-tidy scan of 548 translation units has no remaining naming violations,
but the gate still reports MSVC `xutility`'s `unexpected size` assertion while analyzing the existing
`std::_Find_vectorized<EdgeKey>` selection lookup in SDK `polygon_document.cpp`. A read-only Clang
VFS overlay using that file from commit `734c1350` reproduces the same diagnostic; it is unrelated
to the texture changes and does not occur in the canonical native builds.
After the cleanup, all nine UV tests and 202 focused native/API tests pass. The full regression
gate still has exactly the same 12 failures, and both canonical app configurations build again.

## 2026-08-27 - Discrete Edit Texture Actions

Task/context: add the independent, foldable Edit Texture subsection with 25 Hammer-style actions,
shared-bounds editing, geometric face flips, and repeatable atomic history.

Invariants:

- `QuaderModelingApi::apply_face_texture_action()` is the user-intent boundary for both retained
  buttons and the 25 `modeling.texture.*` commands. Commands are Face-scoped and deliberately
  unassigned. Requests carry the selection token; the native host captures Treat as One, the
  rotation increment even when snapping is disabled, and `ViewportCameraAdapter::basis()`.
- The scene's `FaceTextureEditContext` owns Treat as One independently of document/history state.
  It survives selection changes and undo/redo. Each project scene tab has its own `SceneSession`
  and value; replacing a standalone session's document retains that session's value. A `QuaderApi`
  stays tied to its session, so use `QuaderEditorApi` when inspecting another activated scene tab.
- The portable `prepare_face_texture_action()` prepares every candidate before one native batch
  publication. Per-face dimensions come from native material owners, with the existing 64x64
  fallback. Shift Right/Down subtract U/V; Left/Up add U/V in Quader's signed texture chart.
  Scale edits change signed magnitude by 0.1 and reject zero crossings selection-wide.
- Incremental edits and individual Fit/Justify compose affine UV changes before occurrence C.
  Grouped Fit/Justify reproject through the active face's world chart and combined bounds,
  including gaps; missing active references fall back to the smallest object/face ID. A single
  axis operation preserves the other effective UV axis, and collapsed shared charts fail.
- Reprojection and geometric flips use the neutral carrier inverse: K(f) = K(E) * inverse(I+K(d)),
  followed by f_i = E_i - K(f)d_i and inversion of the retained face transform. Keep C unchanged,
  allow rank-one/constant authored targets, and verify stored float UVs with the real evaluator.
- Geometric flips use geometric plane axes, not texture rotation, and compute shared destinations
  from the original snapshot. Conflicts use the fixed geometry epsilon rather than absolute
  world-position magnitude. Preserve exact stationary vertices, leave their untouched neighbors'
  generated UVs procedural, reverse only selected winding/corner order, and validate facing,
  triangulation, loop intersections, and shared-edge orientation before publication.
- Fit/Justify compare the desired effective corners before writing rounded transform values, so
  repeated fits do not oscillate between float values or create spurious undo entries. Successful
  clicks have distinct descriptive history items; failures/no-ops leave history and redo intact.
  Repeat captures action settings but resolves current selected faces/material dimensions.
- Texture Settings and Edit Texture share the retained snapshot/revision gate. Locale, keybinding,
  and rotation-preference changes invalidate presentation narrowly. Flush valid numeric drafts
  before actions; reject invalid/stale drafts before focus-out, and never reopen a closed dock.

Owners: portable action header/source and neutral UV helper; native modeling API, SDK projection
adapter, edit context and memento/repeat history; retained Qt Tool Settings; command/localization
catalogs and explicit command behavior contracts. No format, dependency, or version change.

Behavior references: Valve's [Source 2 Mesh Texturing](https://developer.valvesoftware.com/wiki/Source_2_Docs/Level_Design/Basic_Construction/Mesh_Texturing)
and [Material Alignment](https://developer.valvesoftware.com/wiki/SteamVR/Environments/Environment_Tutorial:_Materials,_Details_and_Props#Material_Alignment).
The 0.1 scale step, one-pixel shift, geometric flip axes, and connected-vertex rejection policy
were explicit task decisions where the manual did not specify the details.

Verification: all 348 tests in the affected native suites, 103 SDK consumer tests, 253 polygon-loop
tests, and 52 UI source guardrails pass. The reviewed command baseline adds exactly 25 records;
existing records change only the preference-catalog fingerprint and its composed hashes. The full
regression gate reproduces exactly the saved 12 pre-existing failures (seven guardrails and five
Linear Array/overlay tests), with unchanged diagnostics after normalizing locations and addresses.
The SDK-only build succeeds; its nested test run reports the same seven existing guardrail failures.
Automated startup logs verify Vulkan initialization, swapchain creation, and material loading.
Desktop interaction was stopped with Escape before the face controls could be inspected, so there
is no manual layout or texture-pixel verdict. The temporary deployed diagnostic texture was restored
and hash-checked against the unchanged source asset; app settings were isolated from user settings.

Pre-commit verification: renamed 40 new local constexpr test constants to the required `kCamelCase`
style without changing values or behavior. The full scan covered 549 translation units; both
corrected test files pass their clang-tidy recheck. The only remaining style-gate diagnostic is the
previously documented MSVC `xutility` `unexpected size` assertion in unchanged `polygon_document.cpp`.
