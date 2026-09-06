<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Regression memory

Read [AGENTS.md](AGENTS.md) first, then the relevant package guide. This is a map of current,
costly-to-rediscover failure modes, not a changelog, architecture plan, or task queue.
Current code, tests, package instructions, and the user request take precedence.

Update the matching topic when a regression is resolved. Keep its invariant, failure mechanism,
owner, and test pointer; replace superseded details. Do not append build logs, machine measurements,
completed work lists, routine feature descriptions, or duplicate policy.
The test links below are regression entry points; select verification using the root guide.

## Fatal diagnostics

`MiniDumpWriteDump` in the failing process intermittently hung before any text report existed.
Write the initial report/log with raw stack addresses first, without Logger or DbgHelp locks.
The standalone [helper](src/platform/fatal_diagnostics_main.cpp) owns dump/symbol collection;
[fatal handling](src/platform/fatal_error_handler.cpp) bounds its wait and records missing,
failed or timed-out diagnostics without losing the original report or exit code. Publish complete
dump files atomically. Both native hosts require the companion executable beside them.
Exclude the waiting fatal thread with Windows' [IncludeThreadCallback](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/ne-minidumpapiset-minidump_callback_type)
so dump collection cannot suspend the thread enforcing its deadline; preserve the exception context.
[Fatal behavior tests](tests/fatal_error_handler_behavior_tests.cpp) cover real dump creation,
missing/failing helpers, and a stalled helper that observes the initial report before being reaped.

## Project and runtime replacement

- A synchronous project/scene mutation can destroy the SceneSession while the caller is still on
  its stack. Install the replacement runtime binding before publishing lifecycle events. Failed
  and repeated no-op closes do not rebind.
- UI rebind abandonment must not call outgoing owners. Clear retained panel pointers, pending input,
  cameras, and trailing releases; increment ViewportRuntimeBindingToken. Check that token immediately
  after public mutations, command/tool dispatch, and nested presenters before reusing old state.
  Ordered keyboard dispatch stops after a rebind; App reacquires the scene after viewport input.
- Stable public API facades resolve the active session on every invocation. Entity/object handles
  carry SceneSessionId so equal numeric IDs in another tab cannot satisfy an old handle.
- Loaded SceneSession construction hydrates the modeling runtime after services are wired and before
  establishing the clean baseline. Renderable loaded meshes alone do not prove picking is hydrated.

Owners: [SceneSession](src/editor/session/scene_session.cpp),
[UISystem](src/ui/ui_system.cpp), [binding token](src/viewport/viewport_runtime_binding_token.hpp).
Tests: [Qt workspace](tests/qt_project_workspace_tests.cpp),
[command event filter](tests/qt_command_event_filter_tests.cpp).

## Identity, structural edits, and history

- Session object/element allocation high-water marks survive undo, cancel, and branched history.
  Normal undo/redo publishes monotonic revisions; exact rollback is reserved for failed-transaction
  compensation. Reusing a retired ID can make an old public handle address a different object.
- SceneStructuralLifecycleService owns mixed scene/modeling/Curve duplicate, destroy, group, and
  ungroup. Capture registered subselections, preflight the complete operation, and record repeat
  state only after commit. A failure must not leave partial geometry, bindings, assets, or history.
- Scene-aware delete recreates the entire modeled/native/instance forest before restoring parent
  and sibling placement. Orphan definitions can have late sibling ordinals even though their
  occurrence references require recreating them first. Generic restore stays strict; never clamp
  ordinals to accommodate missing siblings. Failed-transition compensation retains creation
  receipts before inverse modeling can remove provisional entities and detaches provisional
  occurrence roles that would block removing their last modeled member. Existing occurrences stay
  intact; definitions may be removed only when every surviving reference is restored elsewhere or
  removed within the rollback. Inverse recreation of an original entity ID still needs a new
  allocation receipt, even when that entity is a captured participant rather than a removal target.
- SceneDocument is the sole live object/component truth. SceneEntityState is the shared durable
  component inventory for history, clipboard, rollback, and persistence; ProjectObjectRecord is gone.
- Undo retains 32 steps by default (Blender factory default); 0 disables retention. The limit bounds
  total undo/redo transitions, evicting oldest applied entries before the most distant redo entries.
  Pruning never replays content or changes clean stamps; ProjectSession propagates it to every tab
  and SceneSession preserves it across document replacement. Native mementos exclude SDK history;
  release redundant SDK snapshots only after guarded publication/preview completion.

Owners: [structural lifecycle](src/editor/commands/scene_structural_lifecycle_service.cpp),
[entity state](src/scene/document/scene_entity_state.hpp), [SDK session](src/sdk/core/modeling/src/session.cpp).
Tests: [mixed hierarchy history](tests/grouping_public_api_tests.cpp),
[strict restore](tests/modeling_session_service_split_tests.cpp), [SDK consumers](tests/sdk/modeling_sdk_consumer_tests.cpp).
Retention tests: [editor limits](tests/editor_history_limit_tests.cpp),
[SDK retention](tests/editor_modeling_history_retention_tests.cpp).

## Project persistence and compatibility

- Workspace create/open and structural manifest edits stage their replacements. A failed write keeps
  the current tabs, identity, path, and active session. Canonical/equivalent physical paths detect
  case and junction/symlink aliases; string equality is insufficient for duplicate-open/Save As.
  Absolute-to-project conversion must handle Windows short-name aliases for existing files and
  missing Save As targets; canonical containment alone does not make lexical relativization safe.
- Save As stages and verifies a same-directory QDR file, reversibly publishes it, then commits the
  manifest. Recovery rolls back the manifest before the scene. If manifest recovery itself fails,
  retain the published scene/backup and expose the candidate live identity as dirty for repair.
  Windows atomic replacement is not a scene-before-manifest power-loss durability guarantee.
- Write snapshots own authored polygon values. Copying a MeshAsset can retain mutable shared data.
  Current QDR writers emit Entity records and omit legacy ObjectRecord vectors; legacy reads may
  fill a missing editor identity but must reject an explicit identity conflict. Old CSR topology is
  reader-only compatibility data and goes through MeshProjectionService.
- Material components with face slots and no primary material are valid: untouched faces remain
  unassigned. Retain these components on decode so slot IDs survive and invalid references still
  reach scene validation. Covered by the public save/reopen/re-save material persistence tests.
- Persistence preserves finite authored topology even when collapsed faces produce no triangles.
  Authored import and QDR use structural validation; public geometric validation remains strict.
  Missing/repeated IDs, invalid connectivity, nonfinite values and malformed UV data remain errors.
  Component collapse must survive undo/redo, save/open and subsequent editing without dropping faces.
- Document replacement advances binding/revision epochs beyond the outgoing document. Preview dirty
  masking applies only to an uncontaminated checkpoint; unrelated edits remain dirty.
- ProjectAutosaveService writes changed open scenes, including inactive/unsaved tabs, beneath
  `.quader/autosave/` with readable scene/time filenames. Legacy UUID subfolders remain readable.
  Defaults: enabled, 2 minutes (Blender), 3 copies per scene.
  QDR and metadata publish atomically; failed writes preserve previous copies and authored files.
  App uses a separate coarse timer; interval/dirty gates precede snapshots and active edits defer.
  Recovery validates a copy and opens a fresh dirty unsaved tab. Copies survive project close;
  ordinary Open/Save As cannot treat autosave storage as an authored scene destination.

Owners: [ProjectSession](src/editor/session/project_session.cpp),
[atomic QDR writes](src/io/qdr/qdr_atomic_write.cpp), [QDR codec](src/io/qdr/qdr_scene_binary_codec.cpp).
Tests: [workspace transactions](tests/project_session_workspace_tests.cpp),
[storage](tests/project_workspace_storage_tests.cpp), [QDR compatibility](tests/qdr_binary_scene_codec_tests.cpp).
Authored topology: [native component persistence](tests/component_transform_persistence_tests.cpp),
[portable persistence](tests/sdk/modeling_component_persistence_tests.cpp).
Recovery owner/tests: [autosave service](src/editor/session/project_autosave_service.cpp),
[autosave workflows](tests/project_autosave_tests.cpp).

## Preferences and recovery presentation

- `SessionPreferences` is the shared undo/autosave value. UISettings persists it; the public
  preferences API and settings dialog commit the same value. Save preferences before lowering
  history retention. Apply merges fields; Cancel discards staged changes. External changes must
  not erase unrelated draft fields.
- Settings uses seven functional categories with groups, separating appearance from editing and
  recovery. This follows the category approach in [Maya preferences](https://help.autodesk.com/cloudhelp/2026/ENU/Maya-Customizing/files/GUID-A62CF59A-3600-4709-A3B7-0F2DB83EC829.htm)
  and [Houdini preferences](https://www.sidefx.com/docs/houdini/ref/windows/mainprefs.html).
  Only include preferences without equivalent controls elsewhere in the UI. Workspace toggles
  (snapping, backface selection, texture locks, display visibility) stay in menus/toolbars; picking
  tolerances and gizmo size belong to Viewport, and selection colors belong to Selection Appearance.
  File > Recover > Auto Save opens the native file browser in the owner-provided recovery folder;
  cancellation or a changed runtime binding prevents dispatch. Recovery actions do not belong in
  Settings. Normal startup does not auto-restore, matching Blender's normal launch workflow.
  Blender's Windows crash-dialog Restart is a separate path that can reopen its latest autosave.
  File > Open Recent retains five saved scenes in per-user `recent-scenes.json`. Successful
  opens/saves and committed edits promote them; autosaves and unsaved tabs do not. Cross-project
  opens stage the requested scene before Save/Discard/Cancel and workspace replacement.
  Portable paths and JSON names use explicit UTF-8; Windows native paths remain wide. Do not use
  `path.string()` to serialize names or paths: the Windows code page can corrupt accented/CJK names.
  Atomic-write diagnostics also use UTF-8; an ACP conversion failure while preparing backup
  cleanup messages previously prevented otherwise valid Unicode saves.

Tests: [settings transactions/categories](tests/qt_settings_dialog_tests.cpp),
[session preference persistence](tests/session_preferences_tests.cpp),
[scheduling boundaries](tests/session_scheduling_guardrail_tests.cpp).

## Modeling publication and preview transactions

- The portable session permits one active preview transaction, resolved exactly once. Checked batches
  reject forbidden nested/unscheduled mutations and roll back on reported failures or exceptions.
  Failed and unchanged operations preserve selection, redo, history, and observable revisions.
- ModelingHostPublicationTransaction covers SDK, scene, assets, binding, selection, and revision
  rollback. Capture baselines before active-operation tags; publish host state before acknowledging
  a preview fence. Capture asset uniqueness before writing: retain unique identity and copy shared
  assets only when needed.
- Restore local document changes through the local owner. Whole-scene reset_from_scene is hydration,
  not a convenient way to refresh one Pivot or mesh edit. Dense operations publish one validated
  batch, not a full-session snapshot/publication per object.
- A transformed preview must match committed positions, normals, UVs, indices, sidedness, material
  references, and slots. Position changes may require both vertex streams and indices to refresh;
  the old assumption that every non-position stream stays static was superseded.

Owners: [publication transaction](src/editor/modeling/services/modeling_host_publication_transaction.cpp),
[object bindings](src/editor/modeling/runtime/modeling_object_binding_registry.cpp).
Tests: [SDK consumers](tests/sdk/modeling_sdk_consumer_tests.cpp),
[preview submission](tests/tool_preview_submission_tests.cpp), [editor API](tests/editor_api_tests.cpp).

## Persistent primitive tool history

- Accepted footprints have per-shape before/after mementos and a local cursor. Undo/redo operates
  within the active primitive tool, preserving final edited geometry and identities; new shapes
  retire only the local redo branch. Undo of an unfinished footprint clears that gesture.
- Public history routes through the tool's local history before generic preview cancellation even
  when its history is empty. Retire the old held/release gesture without canceling a fresh press.
- Apply validates every rebased memento before recording one editor-history item per accepted shape.
  Cancel/failure restores the session baseline and pre-existing document redo branch. Local restore
  observes the scene preview checkpoint; only Apply/Cancel finishes it.
- The modal history obeys the same retention limit. Keep its Cancel baseline and accepted object
  identities separately from bounded shape mementos. Applying after eviction makes the evicted
  prefix permanent and retires older generic history so undo cannot cross that baseline.
- Construction starts at zero extent; zero/one-span footprints cannot commit. Natural primitive
  placement accepts ground-parallel surfaces and otherwise uses the world ground plane. An explicit
  host-resolved miss must not trigger raw-runtime repicking of hidden/untransformed geometry.
- The newest live Box exposes six face-center resize dots and three outer-edge subdivision
  buttons. Resizing changes local bounds while retaining the entity transform; every subsequent
  parametric rebuild retains that offset. Pointer cancellation restores only the resize bounds,
  and local Undo cancels an unfinished resize before undoing a shape. Flip Faces during footprint
  drawing affects the pending shape, never the previously accepted one. Drawing and hit testing
  share the revision-gated primitive-control projection. Ctrl changes the subdivision buttons to
  decrement controls (minimum one segment); the accepted press freezes the direction and active
  glyph even if modifiers change before release. Modifier-only frames do not reproject or rebuild
  geometry. Primitive settings persist between shapes in one session and reset to defaults when
  that session ends, without changing the accepted geometry or its history snapshots.
- Cylinder uses the same creation interaction, with top/bottom dots, one camera-facing radial
  dot and one upper-rim segment button (3–128 sides). Radial drags scale both footprint radii about
  their center from an immutable baseline, retaining ellipse proportions. Cylinder F/C change
  Flip Faces/caps for the live shape or only the pending shape during footprint drawing; neither
  command retires a held resize. Cone uses the same ends/radius controls with its radius on the base.
- Sphere adds radial/ring buttons and a contextual H hemisphere toggle; Torus adds outer/hole
  boundary dots, a bottom-anchored height dot, and major/minor segment buttons. Torus outer drags
  retain the hole, inner drags retain the outside, and both retain XZ ellipse proportions. Its
  resize baseline includes tube ratio as well as bounds: cancellation/Undo must restore both.
  All five shapes share bounds publication, pending-setting isolation, and session reset. Hover
  labels and highlighted loops use the shared projected controls and interaction revisions; the
  native label must remain transparent to viewport pointer hit testing.
- Footprint acceptance derives Sphere height from the smaller footprint diameter (half for a
  hemisphere), and Torus height from that diameter times its tube ratio. Explicit volume acceptance
  retains authored height. Both paths share publication/history; later resizing keeps user bounds.
- Wheel over a segment button consumes navigation, including fractional packets and limit no-ops.
  Resolve command sequences first, then hit-test each packet at its event position. Direction changes
  clear fractional accumulation. Ctrl presentation updates without hover; accepted clicks retain
  their press-time sign. Test the stationary Qt sampler as well as the tool interaction.
- Live topology uses thin, roughly 10%-opacity wires without selection or hover face tint.
  Modeling's object-selection frame still emits mesh edges when
  `suppress_persistent_object_selection` is true (it suppresses fills).
  Disabling the separate scene wire cache alone cannot hide or fade those edges. The overlay
  style context targets the live object in both retained selection and volatile hover streams;
  its identity participates in style invalidation. Disable the separate selected-object fill
  cache during creation and remove targeted hover/selection triangles, including their depth masks.
  Guide loops use `ToolPreviewLine`, below the `ToolHandle` triangles, so neither guides nor mesh
  edges can paint over the dots and +/- glyphs.

Owner: [PrimitiveCreationToolService](src/editor/modeling/services/primitive_creation_tool_service.cpp).
Tests: [primitive service](tests/primitive_creation_tool_service_tests.cpp),
[cube interaction](tests/primitive_cube_interaction_tests.cpp),
[cube overlays](tests/primitive_cube_overlay_tests.cpp),
[wire presentation](tests/viewport_overlay_submission_coordinator_tests.cpp).

## Component picking and sticky selection

- Native Object/Group selection uses a depth-tested camera silhouette in place of the semantic
  full-mesh wire used by modeling/tool consumers. Object selection retains its face fill;
  submission removes internal selection edges.
  Persistent scene and instance wire backends also honor `selected_mesh_outline_only`. Live primitive
  creation retains its faint topology, and component modes retain their editing wires. The shared
  [outline cache](src/render/overlay/selected_mesh_outline_cache.cpp) welds compiled corner splits,
  retains adjacency across camera movement and instances, and only republishes changed contours.
  Contour candidates are clipped against the combined projected coverage of selected triangles,
  using a bounds BVH. Per-mesh front/back edges alone leave interior contours on intersecting meshes
  and concave/disconnected payloads; depth bias cannot repair that. Perspective clipping restores
  world endpoints with perspective-correct interpolation. Holes in the combined silhouette remain.
  Selected groups feed their hierarchy mesh snapshots into the same cache, deduplicating nested
  selections; inherited selection also suppresses native facet wires. Group bounds remain visible.
  Coverage: [outline projection](tests/selected_mesh_outline_cache_tests.cpp) and
  [native wire policy](tests/selected_mesh_outline_presentation_tests.cpp).
- An empty spatial hover sample differs from explicit lifecycle clear. Component misses and leaving
  the viewport retain the last candidate mesh wire; meshes with selected components remain sticky
  independently. Object-mode hover emits no mesh outline and never changes click-owned selection.
- A Vertex/Edge mesh-body click outside component radii is a typed edit-target hit. Replace clears
  target components but retains the mesh; Add adopts it; Remove is inert. Background misses keep
  ordinary Replace clearing. This fallback is click-only and respects active instance-edit scope.
- Backface filtering uses authored winding adjusted by the world determinant sign. It is independent
  of depth, select-through, culling, and marquee policy. Viewport requests inject the persisted
  preference; independent tools and public modeling defaults retain their explicit policy.
- Inward-room radius hits may survive same-surface arbitration only for the exact rendered occurrence
  and geometry member. A sibling instance, different member, or different object remains an occluder.
- Inversion is XOR over the host-resolved eligible universe. Excluded components keep their state,
  an empty universe is an exact no-op, and emptied participants remain enrolled for a second inverse.
  Preserve scene-only selection companions and valid explicit active components.
- Coplanar selection builds rebased face planes and affine comparisons in double precision, with
  distance and angular roundoff derived from float-authored geometry. Double epsilon applied to
  float normals silently discarded tilted seeds. Compare anchored distances so world translation
  cannot amplify normal error; tangential extent alone must not merge nearby parallel planes.
  Coverage: [selection commands](tests/modeling_domain/operation/qdr_document_operation_selection_commands_tests.cpp)
  and `EditorApiTests.SelectCoplanarFacesExpandsTiltedMultiFaceSeedsAndPreservesHistory`.

Owners: [EditorSelectionService](src/editor/selection/editor_selection_service.cpp),
[hit arbitration](src/editor/viewport/viewport_scene_hit_policy.cpp).
Tests: [viewport workflows](tests/viewport_interaction_controller_tests.cpp),
[SDK selection](tests/sdk/modeling_sdk_consumer_tests.cpp).

## Deferred input and topology traversal

- Freeze the resolved physical chord and semantic action at press through deferred release, gizmo
  handoff, and click/double-click disambiguation. A modifier release must not reinterpret the gesture.
- Edge loops use topological continuation; rings cross opposite quad edges. Picked-face context does
  not choose a loop side. Repeated plain loop selection on an open-boundary seed can cycle to its
  boundary component; ring and toggle behavior stay deterministic.
- Orthographic mesh-depth traversal shares the canonical occurrence collector and stable ordering.
  Selection changes do not reset its cursor; geometry, scope, visibility, ray direction, and policy
  changes do. Its edge-on fallback is specific to mesh depth cycling, not all-component picking.
- Enabled exact-match global commands execute at the Qt event boundary so Save precedes synchronous
  window-close preflight. Stateful chords and viewport input remain with the canonical router.
- Knife resolves each accepted segment on a shared existing face, rebasing from the last accepted
  point. Host hits/misses are authoritative; a second differently configured picker breaks locality.

Owners: [selection interaction](src/editor/viewport/viewport_selection_interaction.cpp),
[Qt command filter](src/ui/qt_command_event_filter.cpp).
Tests: [viewport interaction](tests/viewport_interaction_controller_tests.cpp),
[Knife](tests/modeling_domain/tool/qdr_modeling_runtime_tool_knife_tests.cpp).
Bindings and scope rules: [INPUT_KEYBINDINGS.md](INPUT_KEYBINDINGS.md).

## Transform frames, pivots, and texture mapping

- Use the displayed affine world basis, including hierarchy shear/reflection and instance transforms.
  Local Move shares the active frame; Rotate/Scale use each target's pinned frame/pivot. Use full
  inverse-basis math, not orthonormal dot-product shortcuts. Freeze active component frames at press.
- The visible/custom pivot is not necessarily the raw scene origin. Repeat records the original
  world affine and pivot; component replay applies W^-1 * A * W. Shift-duplicate has one placement
  owner, and repeat is recorded only after duplicate plus transform commits.
- TextureMappingComponent preserves occurrence-specific sampling without changing shared geometry.
  Materializing base UVs first inverts retained FaceTextureTransform; otherwise the edit is applied
  twice. Rotation numeric gestures use a captured per-face baseline, not accumulated displayed deltas.
- Legacy local-frame migration runs only during scene installation before hydration/clean baseline.
  Its exact old-cube signature is deliberate because old files lack provenance; generic runtime
  resets must not silently migrate assets or arbitrary geometry.

Owners: [scene transform](src/editor/transform/scene_transform_drag_service.cpp),
[face texture edit](src/editor/modeling/services/face_texture_edit_context.cpp).
Tests: [transform/texture workflows](tests/editor_api_tests.cpp),
[legacy migration](tests/modeling_legacy_local_frame_upgrade_tests.cpp).

## Instances, local visibility, and camera state

- Formal occurrences reference scene-local definition members. All world projection and scope
  checks retain both occurrence and geometry identity. Cross-scene paste clones an independent
  definition; flattening must not sever surviving occurrence links.
- Local Visibility is a transient additional allow-filter, never durable visibility or history.
  Persistent object/ancestor/layer visibility still wins. Picking, overlays, preview suppression,
  bounds, statistics, and camera caches include its revision; removal of the last member exits it.
- Tight world bounds transform actual vertices, not only a local AABB, and include every visible
  instance member. Multi-mesh selection retains each topology outline alongside the aggregate cage.
- Camera target is the canonical navigation pivot. Perspective/orthographic shading choices are
  session-only per-pane slots; temporary Alt-orbit orthographic snaps retain the perspective slot.
  Explicitly selecting that same axis still transitions to a manual orthographic view.

Owners: [scene occurrence projection](src/scene/scene_mesh_occurrence_projection.cpp),
[viewport workspace](src/viewport/viewport_workspace_service.cpp).
Tests: [occurrences](tests/scene_mesh_occurrence_projection_tests.cpp),
[viewport workflows](tests/viewport_interaction_controller_tests.cpp).

## Render binding and resource lifetime

- RenderSceneBindingKey includes session identity, project activation generation, and binding
  revision. Equal local entity/asset/revision numbers across tabs never justify cache reuse.
- Texture identity follows image content and color space, independently of material scalar edits.
  Each material instance pins its texture keys; duplicated preview instances inherit those pins.
  Queue instance destruction before releasing its pins, then reclaim textures after the last
  consumer. Scene rebinding changes future keys without invalidating outgoing live instances.
  Drain all consumers before texture-service shutdown; detached instances still use their textures.
- Build and validate a hidden preview generation before replacing the visible one; failure preserves
  previous preview/source coverage and does not advance submission revision. Retired generations
  are destroyed through idle maintenance, not navigation or modal-drag callbacks.
- Global material-uniform gates belong inside each proxy-owning cache. New proxies invalidate that
  cache even if settings are unchanged; transform/visibility updates do not recreate materials.
- Moving preview meshes need matching overlay-depth companions. Device-domain shaders convert
  Filament's reversed [0,1] clip Z back to GL NDC before DEVICE remapping; double conversion produces
  X-ray component overlays. Normal-map inputs precede prepareMaterial.

Owners: [render binding](src/render/render_scene_binding.hpp),
[preview cache](src/render/tool_preview_renderable_cache.cpp), [lit material](resources/materials/quader_mesh_lit.mat).
Tests: [render extraction](tests/render_extraction_tests.cpp),
[render guardrails](tests/render_source_policy_guardrail_tests.cpp),
[texture allocation/lifetime and Vulkan stress](tests/render_texture_lifetime_tests.cpp).

## VSync and accepted presentation

- The pinned Filament v1.71.6 quader-vsync1 patch supplies opt-in Immediate presentation and a
  backend-confirmed mode callback. FIFO is the default/unsupported-mode fallback. Rebuild matching
  headers/libraries together; CMake verifies provenance/hashes. Keep the maintained patch through
  upgrades; the installer consumes compiled artifacts, not a compiler or end-user SDK download.
- Dependency patch commands must stop parent Git discovery: source or TEMP inside Quader can
  otherwise make Git silently skip every patch path. Keep archive-plus-patch byte verification;
  [nested staging regression](tools/test_filament_patch.ps1) protects this boundary.
- UISettings owns enabled-by-default VSync. Live Settings preview, Apply/Cancel, and public changes
  share the preference transaction. RenderSystem owns surface recreation, effective modes, stable
  renderer records, and GPU retirement. The backend callback publishes atomic state only.
- Demand-rendered material previews honor needs_initial_frame after swapchain recreation. Synchronized
  frames keep display cadence. Uncapped scheduling checks GPU capacity before UI/input/modeling/scene
  work; successful submissions may continue immediately, while busy/rejected attempts back off
  1/2/4 ms and idle work uses 16 ms. Never restore an unconditional 0/1 ms spin loop.
- FPS counts accepted presentations, without a 60 FPS clamp. Active-screen refresh metadata changes
  through screen signals, not per-frame queries. A timer-only edit does not disable Vulkan VSync.
- Command fingerprints include VSync; sparse encoding omits default true and records false so the
  baseline remains compatible while detecting preference side effects.

Owners: [RenderSystem](src/render/render_system.cpp), [frame schedule](src/app/app_frame_schedule.hpp),
[Filament patch](thirdparty/filament/filament-v1.71.6-quader-vsync1.patch).
Tests: [presentation](tests/render_presentation_tests.cpp),
[opt-in native Vulkan runtime](tests/render_vsync_runtime_tests.cpp).

## Dense-scene performance traps

- Revision gates precede selection/catalog projection, formatting, Qt setters, mesh compilation,
  topology hashing, and overlay composition. Cheap paint can conceal expensive synchronization.
- Multi-object hydration, selection, duplication, and commits use bulk indexed owners. Never call
  scalar full-session operations inside a batch, reserve exact growing overlay capacity per object,
  or validate every selected element by repeatedly scanning its entire domain.
- Camera motion reuses persistent world-space overlays and selection projections; hover/tool deltas
  have separate invalidation. Scene selection-only changes patch selected flags, not mesh geometry.
- Geometry/selection payload reuse requires exact equality after hashing, not hash-only identity.
  Preview ticks retain topology and update required dynamic streams; commit performs canonical
  projection once. Do not remove visible overlays or lower render cadence to disguise UI work.

Tests: [render source stamps](tests/render_source_stamp_tests.cpp),
[render guardrails](tests/render_source_policy_guardrail_tests.cpp),
[SDK batch behavior](tests/sdk/modeling_sdk_consumer_tests.cpp).

## Retained Qt state, scaling, and first paint

- Pending numeric edits carry tool/settings/preview identity; discard them on rebind, reset, cancel,
  or structural rebuild. Flush the exact final value on release/Apply. Discard on button press before
  focus loss can emit editingFinished. An idle held slider performs no owner work.
- Native transient windows yield viewport submission until first Paint completes, then resume while
  still open; pausing for the entire modal lifetime breaks live previews. Retain generation/QPointer
  guards across nested, hidden, destroyed, and re-shown windows.
- Fusion remains the style; qt_application_theme owns the Obsidian palette. Windows High Contrast
  removes the forced scheme and uses unresolved system palette roles. Theme transitions are events.
- User interface scale multiplies immutable logical baselines; Qt owns monitor DPR. Raster overlay
  widths and pick radii agree, but framebuffer size remains logical window size times actual DPR.
  Never rescale world geometry, pointer coordinates, saved window geometry, timers, or navigation.
- Fusion generatedIconPixmap needs a real QStyleOption/palette. Custom stroke QIconEngine::pixmap
  must clear its image first; default uninitialized pixmaps produce random opaque glyph tiles.
- Dimension HUDs above Vulkan use non-layered masked child windows. Raster and region share a cached
  binary glyph mask/DPI revision; position changes do not rebuild masks. Keep edge-face context so
  labels choose the exterior. Window restoration accounts for native frame margins before reveal.

Owners: [theme](src/ui/qt_application_theme.cpp), [scale metrics](src/ui/qt_ui_scale_metrics.cpp),
[first-paint gate](src/app/app_frame_presentation.hpp).
Tests: [theme](tests/qt_application_theme_tests.cpp), [Qt settings](tests/qt_settings_dialog_tests.cpp),
[dimension HUD](tests/qt_viewport_edge_length_hud_tests.cpp), [startup](tests/qt_startup_presentation_tests.cpp),
[first-paint scheduling](tests/app_frame_clock_tests.cpp).

## Curves and UV geometry

- Curve controls share canonical component selection and the transform gizmo. The native Curve
  handler owns construction/Alt conversion, not a second modifier/marquee policy. Selected anchor
  plus handle movement must not translate the handle twice.
- Path arrays use portable CurveArcLengthPath geometry and publish one complete compound preview.
  Failed candidates retain previous coverage; Apply uses the structural lifecycle owner and
  restores Curve subselection exactly. Layout-only edits do not rescan source hierarchy/texture plans.
- Solidified Curves participate in the shared material-surface policy. Material-only changes rebind
  without retessellation; eligibility changes advance material-binding revision, point edits do not.
- UV Peel guides specify flow, not cuts in geometry. Complete quad strips use station/width
  constraints; irregular patches use guided LSCM with fold/overlap rejection. Renderer-owned inverse
  bilinear correction fixes safe planar quad interpolation without changing persisted UV topology.
  Preserve per-axis no-op tolerances and floating-point stationarity checks.

Owners: [Curve tool](src/editor/curve/curve_tool_service.cpp),
[path arrays](src/editor/array/path_array_tool_service.cpp), [UV Peel](src/sdk/core/modeling/src/uv_peel.cpp).
Tests: [Curve workflows](tests/curve_modeling_behavior_tests.cpp),
[path arrays](tests/path_array_tool_service_tests.cpp), [SDK consumers](tests/sdk/modeling_sdk_consumer_tests.cpp).
