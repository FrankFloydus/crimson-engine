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
- Perspective camera pose is explicit-save metadata, not authored content: navigation does not alter
  `SceneContentRevisionStamp`, trigger autosave, or require a close prompt. Explicit Save still writes
  the current persisted pose (or the pre-entry pose while Local Visibility is active).
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

- Implicit tool, selection-mode, save, and scene transitions accept valid pending work through
  its existing owner before changing the target state. Only explicit Cancel discards an authored
  draft. Invalid meaningful work rejects the transition with the draft and its ownership intact;
  an untouched starter exits without phantom geometry or history. Preflight unavailable targets
  before accepting anything. Same-tool activation in the same pane preserves pending work, and
  pane focus alone is nonmutating; acquiring another pane's tool settles the previous operation.
  Undo accepts valid pending work, then undoes it so Redo can restore it. Native tool-local history
  keeps its established gesture/session semantics. Selection edits that leave the operation settle
  it; compatible same-mode selection edits for Bevel/Slice Quads still update their live preview.
  API and viewport acceptance must share the
  existing commit/publication owners, never synthesize an Apply binding or silently fall back to
  Cancel. Coverage includes `ExtrudeToolServiceTests.SwitchingToolsAcceptsPendingExtrusionAsOneUndoStep`,
  `PaneFocusRetainsPendingWorkUntilAnotherPaneAcquiresItsTool`, and
  `UnavailableActivationDoesNotAcceptPendingExtrusion` in `tests/extrude_tool_service_tests.cpp`.
- Dedicated Extrusion activation starts a pending preview; releasing a viewport drag ends
  pointer input but retains that preview with Apply enabled. Apply accepts it, while
  Escape/Cancel restores its source and preserves existing undo/redo.
  Activation uses the remembered Amount (initially zero) and locks preselected faces, or the first face picked after
  activation. Selection edits and tool switches first accept the pending step before replacing
  its source; internal preview/history publication still restores its own selections.
  Repeated E retains the pending step. Settings regenerate from its immutable source.
  Apply records one history entry and releases the source while retaining Amount;
  it does not start another preview. The next amount edit or drag creates a new step from the selected caps.
  A new pointer drag starts at zero displacement; canceled samples do not replace accepted options. Accepted
  geometry has no adjustment window. Leaving the idle tool retains accepted geometry.
  Physical E needs the descriptor's repeated-activation capability at the viewport dispatch
  gate. Native QWindow focus must reserve ShortcutOverride like its widget container so
  Alt+E reaches the canonical resolver before the Edit-menu mnemonic.
  The yellow disk remains visible during dragging; it and the settings edit only the
  pending step. Cancellation restores the document from before that step.
  Extrusion dragging is free by default; event-time Ctrl snaps the total amount to the
  grid and Shift scales new movement for precision. Automatic scene snapping must not
  override free movement; the explicit vertex-snap hold remains available. Mouse-up
  retains its own canonical held-key snapshot, so a later V release in the same frame
  cannot change the final preview snap state (including remapped bindings).
  Preview baselines must therefore be the live document, not the immutable source.
  Nonzero drag samples reuse SDK-prepared topology and IDs; publishing zero retires
  that generation. Dynamic render previews keep assets unchanged until Apply.
  Their projection revision must reach every retained gate, including Qt's persistent
  selection source stamp and compensated failures that clear the preview. A fresh
  `selection_overlay_frame()` getter does not prove the cached viewport submitted it;
  test `ViewportOverlaySubmissionCoordinator` with the owner-provided signature.
  Follow the [preview projection contract](src/editor/modeling/runtime/README.md#live-preview-projection-contract).
  Region adjustment handles follow the saved axis.
  Extrusion validation permits structurally valid thin/tangent side walls and unchanged
  authored defects, but rejects properly crossing new wall boundaries and validates
  affected caps in anchored, scale-normalized coordinates. Every extrusion mode maps
  pointer motion along the visible handle axis in the press-time camera frame; fixed
  upward-positive motion reverses Individual/Along Normals when the handle projects down.
  Near the view axis, retain the projected direction with bounded sensitivity; only a
  degenerate projection uses upward-positive motion. Projection stays linear during the
  drag: per-pointer ray/axis intersections can diverge and reverse near the vanishing point.
  Owners: `ExtrudeToolService` and the SDK projection adapter. Tests:
  `tests/extrude_tool_service_tests.cpp`, `tests/qt_extrude_tool_settings_tests.cpp`,
  `tests/qt_input_adapter_tests.cpp`, `tests/qt_command_event_filter_tests.cpp`.

- Polygon operation success and mutation are separate. A rejected operation must carry explicit
  failure through `polygon_document.cpp` and native adapters; `changed=false` also describes valid
  no-ops. Combine Faces around an opening exposed a lost failure that reached the UI as normal text.
  SDK consumer and `QtProjectWorkspaceTests` cover that geometry, preserved state/history, and
  one error announcement per attempt.

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
- Viewport geometry acceptance uses the API-backed commit port and the existing modeling memento
  command owner, including implicit transitions. SDK history alone does not register an editor Undo
  step; transient ports must not provide a history-bypassing fallback. A valid authored draft is
  accepted before leaving it; an idle hover-only preview is retired without geometry or history.
  Returning to Select without changing content must preserve the redo branch. Insert Edge Loop
  and Knife activation enter Edge mode from every selection mode; shortcuts and menu eligibility
  must not require that mode in advance. Loop retains its tool after each single-cut acceptance.
  Pane focus alone leaves pending work intact. The explicit Cancel
  Active Tool command (Escape by default) clears transient loop state and returns to Select while
  retaining Edge component mode, content, history, and redo. Normal undo/redo clears its stale seed
  after successful restoration without exiting the tool; failure compensation preserves transient
  state. Shared cancellation reports actual tool changes instead of assuming every modal
  cancellation returns to Select.
- Insert Edge Loop acquisition, retained hover, and dragging share the projected cursor factor,
  including edges crossing the camera plane; grid snapping remains measured along the world edge.
  Preview vertices are deduplicated by authored edge identity and use the normal hover-vertex
  presentation. Clear, commit, and cancel must retire both lines and points in retained submission.
  See [loop input tests](tests/qt_input_adapter_tests.cpp) and
  [retained submission tests](tests/viewport_overlay_submission_coordinator_tests.cpp).
- Knife/Loop surface hover exposes authored source wires without selecting the mesh. Keep the
  actual surface candidate separate from valid Knife seeds and retained Loop seeds. The existing
  persistent source-wire ID gates topology rebuilds; cursor movement only changes volatile cut
  feedback. Tool-owned hover survives the viewport selection pass and camera navigation (including
  its captured release sample); ordinary pointer leave and hard interruption still clear it.
  Component topology retains its normal edit-wire depth classification during hover and navigation;
  tagging it as generic Tool geometry makes hidden edges appear through occluders and disappear
  again when hover is cleared. Only Object-mode silhouette replacement needs that exemption.
  See [retained topology tests](tests/selected_mesh_outline_presentation_tests.cpp) and
  [Vulkan captures](tests/selection_overlay_runtime_tests.cpp).
- Native point selection carries a screen aperture and camera clip range; ray-only API requests
  retain world-distance tolerances. Transformed component candidates must be measured in world
  space before ranking, never with one scalar inverse-scale approximation. Runtime pick records
  already return world positions in the historical `local_position` field when occurrence
  transforms are supplied; adapters must not transform them a second time.

Owners: [publication transaction](src/editor/modeling/services/modeling_host_publication_transaction.cpp),
[object bindings](src/editor/modeling/runtime/modeling_object_binding_registry.cpp).
Tests: [SDK consumers](tests/sdk/modeling_sdk_consumer_tests.cpp),
[preview submission](tests/tool_preview_submission_tests.cpp), [editor API](tests/editor_api_tests.cpp).
Viewport acceptance: [commit adapter](src/editor/viewport/viewport_modeling_tool_commit_adapter.cpp),
[history regressions](tests/viewport_tool_catalog_tests.cpp).

## Persistent primitive tool history

- The viewport samples the embedded Face Texture browser at each accepted primitive footprint
  and passes an explicit material reference into that shape's transaction. Missing/deleted choices
  use the reserved project Default Material. Tool activation, regeneration and Apply do not resample;
  the windowed management browser and other creation APIs do not supply this choice.
- Accepted footprints have per-shape before/after mementos and a local cursor. Undo/redo operates
  within the active primitive tool, preserving final edited geometry and identities; new shapes
  retire only the local redo branch. Undo of an unfinished footprint clears that gesture.
- Public history routes through the tool's local history before generic preview cancellation even
  when its history is empty. Retire the old held/release gesture without canceling a fresh press.
- Apply validates every rebased memento before recording one editor-history item per accepted shape.
  Cancel restores the session baseline and pre-existing document redo branch. Rejected implicit
  completion preserves the draft instead of taking that rollback path. Local restore observes the
  scene preview checkpoint; acceptance/Cancel finishes it.
- Direct primitive launchers finish the previous shape session before starting a different kind:
  reject unavailable kinds first, accept an active resize at its current value, and apply released
  shapes. Incomplete pointer-down drawing rejects the transition and retains the gesture; an empty
  starter exits without geometry or history. Same-kind, same-pane launch is unchanged. The explicit
  legacy type setter still morphs the live shape; launchers must not route through that setter.
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
  geometry. Primitive settings and preferred shape persist for the application lifetime across
  tool and scene sessions, without changing accepted geometry or its history snapshots.
  Rejected edits restore the last accepted options before preview retirement.
- Cylinder uses the same creation interaction, with top/bottom dots, one camera-facing radial
  dot and one upper-rim segment button (3–128 sides). Radial drags scale both footprint radii about
  their center from an immutable baseline, retaining ellipse proportions. Cylinder F/C change
  Flip Faces/caps for the live shape or only the pending shape during footprint drawing; neither
  command retires a held resize. Cone uses the same ends/radius controls with its radius on the base.
- Sphere defaults to UV topology with radial/ring buttons. Quad topology uses one Subdivision
  level control (1–6, default 3), generating welded spherified-cube quads; its hemisphere base is
  also filled with quads. Switching type retains each topology's settings and authored bounds,
  retires old pointer gestures, and preserves the settings overlay origin. Both types share the
  contextual H hemisphere toggle. See `tests/primitive_creation_tool_service_tests.cpp` and
  `tests/modeling_domain/qdr_document_primitive_tests.cpp`. Torus adds outer/hole
  boundary dots, a bottom-anchored height dot, and major/minor segment buttons. Torus outer drags
  retain the hole, inner drags retain the outside, and both retain XZ ellipse proportions. Its
  resize baseline includes tube ratio as well as bounds: cancellation/Undo must restore both.
  All five shapes share bounds publication, pending-setting isolation, and application option memory. Hover
  labels and highlighted loops use the shared projected controls and interaction revisions; the
  native label must remain transparent to viewport pointer hit testing. Dimension feedback stays
  in world units until the Qt presenter converts with `viewport_host_defaults().map_units_per_world_unit`,
  matching edge measurements (one world unit displays as 32 map units); segment counts are unitless.
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
  cache during creation and remove targeted hover/selection triangles.
  Guide loops use `ToolPreviewLine`, below the `ToolHandle` triangles, so neither guides nor mesh
  edges can paint over the dots and +/- glyphs.

Owner: [PrimitiveCreationToolService](src/editor/modeling/services/primitive_creation_tool_service.cpp).
Tests: [primitive service](tests/primitive_creation_tool_service_tests.cpp),
[cube interaction](tests/primitive_cube_interaction_tests.cpp),
[cube overlays](tests/primitive_cube_overlay_tests.cpp),
[wire presentation](tests/viewport_overlay_submission_coordinator_tests.cpp).

## Component picking and sticky selection

- Selection and hover fills must test against scene depth without writing it. A two-sided
  backface highlight must not become an occluder for otherwise visible editing wires or vertices.
  Keep authored alpha, two-sided color rendering, and normal solid-surface occlusion; do not
  repair this with global draw-on-top wires. See the face-fill builder coverage in
  [render extraction tests](tests/render_extraction_tests.cpp) and the actual Vulkan
  [selection overlay tests](tests/selection_overlay_runtime_tests.cpp).
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
  Captured navigation keeps hover/picking suppressed through its release frame: that sample still
  contains the navigation cursor, and only the next frame has the restored real mouse position.
  A transient crosshair hit must never become a retained candidate; see the navigation-release
  regressions in [Qt input tests](tests/qt_input_adapter_tests.cpp).
  Gizmo hover owns immediate feedback separately from gesture consumption: the shared interaction
  controller suppresses scene picks and clears the entire hover candidate while a handle is hovered
  or consumed. Hover-only mesh wires disappear; meshes with selected components retain their wires.
  This also clears candidates retained by a prior empty-space sample or viewport leave.
  Clear once and re-arm the hover work key so hiding a handle under a stationary pointer restores
  scene hover. Preserve short-click selection/depth cycling; coverage:
  [interaction tests](tests/viewport_interaction_controller_tests.cpp).
- Component source topology remains complete in semantic overlays. The revision-gated
  [submission coordinator](src/render/overlay/viewport_overlay_submission_coordinator.cpp) removes
  a source-wire draw beneath an opaque selected edge or face boundary with matching object,
  edge IDs and world endpoints. Otherwise cyan blends into antialiased yellow at thin widths.
  Translucent user selection colors retain normal source-wire compositing. Coverage:
  [submission tests](tests/viewport_overlay_submission_coordinator_tests.cpp).
- A Vertex/Edge mesh-body click outside component radii is a typed edit-target hit. Replace clears
  target components but retains the mesh; Add adopts it; Remove is inert. Background misses keep
  ordinary Replace clearing. This fallback is click-only and respects active instance-edit scope.
- Backface filtering uses authored winding adjusted by the world determinant sign. It is independent
  of depth, select-through, culling, and marquee policy. Viewport requests inject the persisted
  preference; independent tools and public modeling defaults retain their explicit policy.
- Lasso bypasses occluders and includes back-facing projected components while retaining visibility,
  lock, clipping, and edit-scope eligibility. Full containment checks complete projected geometry;
  groups require all eligible descendants. Selection and its overlay share the boundary-inclusive
  even-odd region contract. Ordinary pick and hover continue to honor the backface preference.
- Transform drag owns the active preview presentation once it begins. Clear semantic selection hover
  on a successful drag begin so ordinary hover fills and edges cannot remain composited over the
  selected transform preview; failed drag starts must leave hover feedback intact.
- Inward-room radius hits may survive same-surface arbitration only for the exact rendered occurrence
  and geometry member. A sibling instance, different member, or different object remains an occluder.
- Editable vertex markers and their outlines remain depth-tested inside inward-facing rooms.
  Camera containment must never enable X-ray rendering; retain inward boundary points for GPU
  occlusion. Wireframe panes remove source-face CPU occlusion metadata only from submitted copies;
  shared sources and solid panes retain it. Shading switches invalidate both overlay streams without
  rebuilding semantic sources or changing the distance-safe point shader bias.
  Coverage: `OverlayPointMeshBuilderTests.InwardRoomVertex*` in
  [render extraction](tests/render_extraction_tests.cpp).
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

- Linear Array constraints restrict subsequent endpoint movement; switching X/Y/Z never projects
  or rotates the existing span. Lock the other coordinates to the current endpoint, including
  during snapping, and rebase pointer motion on constraint changes. Its axis guide passes through
  that endpoint and is clipped per pane; camera-only navigation invalidates the guide projection.
  Coverage: [array input](tests/linear_array_tests.cpp),
  [retained guide submission](tests/viewport_overlay_submission_coordinator_tests.cpp).
- Array dimensions use the accepted render preview, independently of the selection-dimensions
  preference. Linear/Radial include their original visible occurrences; Curve Array replaces
  its source and must not include the old position. Keep array bounds separate from selected
  bounds so sibling panes cannot evict the cache. Camera movement and the per-tool visibility
  checkbox must not rebuild array geometry. Coverage:
  [bounds submission](tests/viewport_overlay_submission_coordinator_tests.cpp),
  [Vulkan previews](tests/selection_overlay_runtime_tests.cpp).
- Freeze the resolved physical chord and semantic action at press through deferred release, gizmo
  handoff, and click/double-click disambiguation. A modifier release must not reinterpret the gesture.
- Edge loops use topological continuation; rings cross opposite quad edges. Picked-face context does
  not choose a loop side. If an interior loop stays at its seed, use the perimeter of its adjacent
  face with the most corners (ties keep authored adjacency order): cylinder rims select their cap,
  longitudinal edges select a side quad. Preserve the ordinary walker when it finds a longer path.
  Repeated plain loop selection on an open-boundary seed can cycle to its boundary component;
  ring and toggle behavior stay deterministic. Coverage: `EdgeLoopSelection*` in
  [loop tests](tests/modeling_domain/operation/qdr_document_operation_insert_edge_loop_tests.cpp).
- Orthographic mesh-depth traversal shares the canonical occurrence collector and stable ordering.
  Selection changes do not reset its cursor; geometry, scope, visibility, ray direction, and policy
  changes do. Its edge-on fallback is specific to mesh depth cycling, not all-component picking.
- Enabled exact-match global commands execute at the Qt event boundary so Save precedes synchronous
  window-close preflight. A Save resolved while a main-editor field has focus settles that field
  before serialization; ordinary text editing and auxiliary/modal window input remain locally owned.
  Stateful chords and viewport input remain with the canonical router.
- Editor panels resolve registered scene/tool shortcuts through the canonical dispatcher; the
  Outliner also supports hover ownership. Material-browser type-ahead must yield to mapped commands.
  Viewport frame focus-loss cleanup preserves another panel's pending sequence, while real focus
  loss, text editing, or changing input owners clears it. Test a sequence across an idle viewport
  frame, not just consecutive Qt key events; see [command filter tests](tests/qt_command_event_filter_tests.cpp)
  and [keybinding API tests](tests/editor_api_tests.cpp). Hover feedback preserves panel keyboard focus;
  the first viewport press both focuses and draws. The native pane border tracks actual focus,
  including single/maximized panes; see [focus indicator tests](tests/qt_viewport_focus_indicator_tests.cpp).
- Knife resolves each accepted segment on a shared existing face, rebasing from the last accepted
  point. Host hits/misses are authoritative; a second differently configured picker breaks locality.
  Stroke partitioning follows the source face's winding to retain bounded cells. An exterior walk
  can visit every placed boundary vertex while omitting all requested cuts; vertex coverage alone
  must not choose the partition. See the repeated-boundary regressions in
  [Knife stroke tests](tests/modeling_domain/operation/qdr_document_operation_knife_stroke_tests.cpp).
- Radial placement hover must not require keyboard focus after toolbar activation. Retain the
  last valid center during UI editing/navigation, but resolve the queued press position again
  before locking placement; an invalid click cannot lock the retained preview. A placement click
  only fixes the center: settings continue updating the transient preview until Apply (or the
  finalize shortcut) commits the operation. Ordered wheel events must clamp separately so
  outward motion at a limit does not swallow a later inward step. Switching between copy and
  repeat wheel adjustments clears the pending fraction in the native interaction owner.
  See [native interaction](src/editor/tools/viewport_native_tool_interaction.cpp) and
  [Qt gesture tests](tests/qt_input_adapter_tests.cpp).

Owners: [selection interaction](src/editor/viewport/viewport_selection_interaction.cpp),
[Qt command filter](src/ui/qt_command_event_filter.cpp).
Tests: [viewport interaction](tests/viewport_interaction_controller_tests.cpp),
[Knife](tests/modeling_domain/tool/qdr_modeling_runtime_tool_knife_tests.cpp).
Bindings and scope rules: [INPUT_KEYBINDINGS.md](INPUT_KEYBINDINGS.md).

## Transform frames, pivots, and texture mapping

- Explicit mesh Bake operations reset only the chosen stored TRS channels while
  preserving world geometry. Rotation baking with retained nonuniform scale uses
  `inverse(S) * R * S`; selected hierarchies require original world snapshots.
  Keep geometry, shared-asset isolation, texture mapping and child compensation in
  one undoable publication. See the [bake owner contract](src/editor/transform/README.md)
  and [behavior tests](tests/mesh_transform_bake_tests.cpp).

- Object rotation fields author absolute local Euler values; retain the primary's requested
  values through preview/commit, with quaternion differences for selected peers. Subtracting
  Euler components and treating the difference as a world rotation breaks mixed-axis edits
  and zero resets. Gizmo quaternion extraction uses one shared singularity decision for X/Z
  near Y = +/-90 degrees; independent float pitch/roll branches can introduce a half-turn.
  Object edits retain mesh-local geometry and authored scale signs through rotation;
  translation copies captured rotation/scale without decomposing them. Reject world gizmo
  matrices whose local TRS recomposition loses shear instead of silently changing shape/scale.
  Contracts: [object transform workflows](tests/scene_transform_tests.cpp),
  [Inspector edit completion](tests/qt_settings_dialog_tests.cpp).
- Inspector transform fields follow live gizmo and numeric-tool previews through their
  retained controls. Gate on transform/selection/binding revisions before querying values;
  keep the full material/property refresh deferred during drags. Synchronize the row's edit
  callback values under blocked widget signals so the next axis edit cannot restore stale
  sibling values. Contracts: [live UI workflow](tests/qt_project_workspace_tests.cpp),
  [retained Inspector fields](tests/qt_settings_dialog_tests.cpp).
- Use the displayed affine world basis, including hierarchy shear/reflection and instance transforms.
  Local Move shares the active frame; Rotate/Scale use each target's pinned frame/pivot. Use full
  inverse-basis math, not orthonormal dot-product shortcuts. Freeze active component frames at press.
- Rotate's trackball (`All`) freezes the camera right/up frame at press and uses a shared world pivot,
  even with Local selected. Preserve its complete rotation matrix through scene/component previews
  and repeat; the scalar angle is only a summary. Visible axis arcs take priority over the ball.
  Contracts: [rotation math](tests/modeling_domain/tool/qdr_modeling_runtime_tool_rotate_tests.cpp)
  and [public transform workflows](tests/programmatic_behavior_scenario_tests.cpp).
- The visible/custom pivot is not necessarily the raw scene origin. Repeat records the original
  world affine and pivot; component replay applies W^-1 * A * W. Shift-duplicate has one placement
  owner, and repeat is recorded only after duplicate plus transform commits.
- Move auto-merge runs only at accepted component-move completion, including zero-distance
  acceptance, within the same publication and undo transaction. Capture the setting for repeat;
  retain sparse position history unless a mesh actually welds, then retain its topology and selection.
  Duplicate-face cleanup must transfer selected/active cap identity to its surviving face. Undo/redo
  preserves allocator high-water without strict topology reimport, including nonmanifold welds.
  Contracts: [Move regressions](tests/move_auto_merge_tests.cpp).
- TextureMappingComponent preserves occurrence-specific sampling without changing shared geometry.
  Materializing base UVs first inverts retained FaceTextureTransform; otherwise the edit is applied
  twice. Texture numeric gestures use the captured editing context and absolute targets, never
  accumulated displayed deltas.
- Face Texture canvas gestures retain one planar chart and prepare every candidate from the
  original UVs. Shared preview cancellation/commit must route through its gesture owner to clear
  presentation state and dirty checkpoints; selection/scene replacement cannot leave a hidden
  UV preview. Scale handles follow the signed inverse UV axes; drag deltas use the same captured
  chart rather than world-up canvas axes. This preserves the opposite repeat length and pivot
  across differently oriented faces, rotation and mirroring. Canvas U/V badges and guides denote
  texture width/height, not screen directions; scale hover/drag reveals a bounded texture grid,
  a dashed axis across the canvas and the opposite fixed pivot. Hover uses the open hand and
  dragging uses the closed hand. Visible U/V names retain the existing X/Y API and persistence IDs.
  Contracts: [canvas workflows](tests/face_texture_canvas_api_tests.cpp),
  [gesture math](tests/face_texture_gesture_math_tests.cpp), [Qt canvas](tests/qt_face_texture_canvas_tests.cpp).
- Selecting a single face reveals its assigned material in the embedded browser without assignment.
  Gate this synchronization by selection/material identity, independently of catalog refresh and UV
  previews, so subsequent browsing remains available for primitive creation. Revealing clears only
  constraints hiding the target. Deselecting clears the embedded highlight once; later manual
  browser choices survive unchanged refreshes, and multiple selected faces retain the choice.
- The canvas plane uses projected world Y as up and world X as the horizontal-face fallback,
  independently of boundary ordering, UV rotation or the viewport camera. Usable affine mappings
  supply the absolute texture angle. Scale modifiers and pixel shifts retain their existing units;
  canvas gestures update those settings in the same prepared preview, so numeric fields stay live
  and retain their values after commit. Queries do not rewrite stored settings.
  The canvas/controls divider has its own saved position inside the browser divider, with legacy
  two-part layout restoration. Contracts: [canvas projection](tests/sdk/face_texture_canvas_tests.cpp),
  [panel presentation](tests/qt_face_texture_panel_tests.cpp).
- Texture scale clicks double/halve signed magnitudes between exact `1/8192` and `1024`;
  four-decimal display rounding must never feed the next step. Align Grid resets world-axis
  mapping to `0.125` map units per texel, zero shift/rotation, using each texture's dimensions
  (64-pixel fallback), independently of snap spacing and face size. Keep occurrence compensation.
  Owner/tests: [texture actions](src/sdk/core/modeling/src/face_texture_actions.cpp),
  [SDK texture workflows](tests/sdk/modeling_sdk_consumer_tests.cpp).
- Flip Face UVs retains the legacy `FlipFaceHorizontal/Vertical` IDs but mirrors effective
  U/V coordinates using `min + max - coordinate`, leaving geometry and scalar settings intact.
  Treat as One uses the union of existing selected UV charts, without active-face reprojection.
  Solve the mirrored coordinates back through the original transform and occurrence sampling;
  preserve adjacent faces and return a publication no-op for a collapsed axis. Separate
  Flip Texture actions retain scale-sign semantics. Contracts:
  [native mirror workflows](tests/face_texture_mirror_api_tests.cpp),
  [portable texture actions](src/sdk/core/modeling/src/face_texture_actions.cpp).
- Face/View alignment retains settings but uses the same image-axis texel units as Grid:
  rotate/scale the generated chart, normalize by image width/height, then add UV shift.
  Subsequent rotation must use image-axis units too. Numeric canvas rotation sets an absolute
  effective angle; action rotation retains its incremental command semantics. Preserve negative/no-op
  edits and merged numeric history.
  Test non-square images and front-view feature movement for named CW/CCW actions.
- Continue Texture retains the selected source; target material and UVs publish in one history
  step. A shared source/target edge supplies its endpoint UVs plus a conditioned third corner,
  following Hammer's recovered same-mesh operation. Other corners need not fit that local chart.
  Keep exact seam phase and self-transfer no-ops; separately placed occurrences are not shared edges.
  Fit transient charts in double precision with normalized axes. CPU and both mesh shaders
  difference affine sampling coefficients before evaluating positions, preserving small changes
  at large coordinates. Semantic no-op comparisons depend on UV span/ULP, while reconstruction
  and source-fit checks use bounded float uncertainty. Do not reuse one tolerance for all three.
  UV-only publication and Peel validate authored connectivity/attributes/selection, allowing
  unrelated finite collapsed faces; geometric edits and public strict validation remain strict.
  Cover diagonal lip continuation, thin local and oblique geometry, transferred faces reused as
  sources, distant local edits, million-tile phase edits, rendered interiors and publication rollback.
  Contracts: [portable transfer](tests/sdk/modeling_sdk_consumer_tests.cpp),
  [native continuation](tests/project_material_behavior_tests.cpp).
- Position edits retain authored face-corner UVs; procedural faces remain procedural. Topology
  edits keep surviving corners, sample new corners in their source chart, and preserve seams
  that remain in the resulting topology. Merged boundaries receive UVs before UV-aware
  simplification. Dissolve Edges deliberately removes an interior edge and any seam on it:
  inherit stored boundary corners from outgoing edge donors in the survivor's texture frame,
  then remove captured affected vertices with two remaining edges after the complete edge batch.
  UV breakpoints and bends do not block this cleanup. Capture triangle-pair and incidental
  untouched-face corner protections before merging; do not sweep unrelated vertices. Preserve
  surviving corner values, combine hard-edge flags, and return only live face IDs. Quader retains
  a candidate if removing it would leave a neighboring face with fewer than three corners.
  Do not require the removed faces' entire interior UV fields to survive retriangulation;
  that rejects valid peeled, discontinuous and nonplanar joins. Keep finite and topology checks.
  Stored-corner preservation does not certify displayed UVs under nonidentity occurrence mapping
  when a merge changes the face carrier. Generic topology publication currently refits that
  carrier; do not claim occurrence-space invariance from local UV tests.
  Operations promising exact chart preservation validate after the final loop is established
  and reject incompatible mappings atomically, without clearing charts or taking a hull fallback.
  Sampling and rendering share
  neutral quad interpolation math; prepare lookup/triangulation once per source face.
  Retaining corner values alone does not preserve nonaffine interior UVs. Quads retain their
  diagonal on winding reversal; affine n-gon rebases compare source/result triangulation before
  choosing an anchor. Proper rotations can reverse dominant-plane projection too; test them
  alongside face-plane/oblique reflections and negative scales.
  Contracts: [preservation](tests/sdk/uv_preservation_foundation_tests.cpp),
  [quad sampling](tests/mesh_quad_uv_interpolation_tests.cpp).
- Complete-face native transform acceptance preserves n-gon UV interiors on detached documents,
  staging any Move weld in the same publication. Cyclic corner-order changes need exact document
  history; ordinary transforms retain sparse positions, and full-memento previews must not retain
  a second sparse delta. Keep authored drag previews position-only. Derived render correction is
  gated by captured n-gon eligibility and changed frames; changed packing refreshes its layout key
  and source-to-compiled vertex lookup together. Eligibility belongs to the existing preview
  baseline so explicit Object geometry previews share it, and clears on non-transform transitions.
  Group transforms remain scene-owned; Group selection intentionally has no SDK geometry-preview
  participants and that low-level preview API rejects it unchanged.
  Contract: [transform UV acceptance and preview](tests/uv_transform_acceptance_tests.cpp).
- Combine Mesh bakes each source's effective sampling into survivor coordinates and remaps source
  material slots into the survivor table in the same transaction. Array preview and publication
  share texture-lock plans; retain both definition-member and occurrence identity when resolving
  generated sampling, including recentered linked sources and independent materialization.
  Contracts: [native sampling](tests/uv_native_sampling_tests.cpp),
  [array preview equality](tests/uv_instance_preview_tests.cpp).
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
- Radial Array converts explicit source roots only inside acceptance, one linked family per
  source. Conversion recenters occurrences: apply captured preview placement deltas to the new
  occurrence frame, using the original visible pivot for radial repeats. Independent copies of
  existing instances materialize generated placements directly, preserving the original's links;
  canonical hierarchy duplication must begin before the caller enlists any other command.
  Failure compensation must restore exact modeling, scene, and asset checkpoints, including
  explicitly created mesh assets that ordinary undo retains for redo, to keep the preview
  retryable. See [array owner](src/editor/array/radial_array_tool_service.cpp) and
  [instance array tests](tests/radial_array_instance_tests.cpp).
- Local Visibility is a transient additional allow-filter, never durable visibility or history.
  Persistent object/ancestor/layer visibility still wins. Picking, overlays, preview suppression,
  bounds, statistics, and camera caches include its revision; removal of the last member exits it.
- Tight world bounds transform actual vertices, not only a local AABB, and include every visible
  instance member. Multi-mesh selection retains each topology outline alongside the aggregate cage.
- Camera target is the canonical navigation pivot. A visible gesture anchor may repair a stale target
  inward, but a farther surface must not inflate an already-valid nearby orbit radius. Wheel depth
  comes from actual clipped surfaces: ordinary zoom prefers view center and falls back to the
  packet's cursor surface; cursor zoom uses the reverse priority. Never use whole-scene bounds
  (including offscreen objects and entity origins) as
  navigation depth. Wheel repair handles severely oversized and collapsed distances; orthographic
  wheel input preserves the view's depth. Empty-space drags can use the center surface to recover
  a nearby subject. Cover mixed-scale navigation without resetting the pose between gestures in
  `ViewportCameraTests` and `QtViewportSurfaceInputTests`.
- Perspective/orthographic shading choices are session-only per-pane slots; temporary Alt-orbit orthographic
  snaps retain the perspective slot. Explicitly selecting that same axis still transitions to a
  manual orthographic view.
- Orthographic ground-grid bounds must project the actual camera right/up basis onto the grid plane.
  Top/Bottom Alt-orbit snaps can retain a quarter-turn roll, exchanging the visible width and height;
  using only the named axis clips wide viewports. The ground-grid intensity follows the actual
  projection, including temporary snaps, while the perspective rectangle and Mesh Grid stay separate.
  See [placement policy](src/viewport/viewport_grid_placement_policy.hpp) and
  `ViewportGridPlacementPolicyTests` / `RenderGroundGridRuntimeTests`.

Owners: [scene occurrence projection](src/scene/scene_mesh_occurrence_projection.cpp),
[viewport workspace](src/viewport/viewport_workspace_service.cpp).
Tests: [occurrences](tests/scene_mesh_occurrence_projection_tests.cpp),
[viewport workflows](tests/viewport_interaction_controller_tests.cpp).

## Render binding and resource lifetime

- Scene fog belongs to the document's scene settings and its existing document-command history.
  It applies only to Rendered viewport scene views, independently of the Post Process toggle;
  overlay views stay clear. Keep every effective fog field in the viewport presentation gate so
  edits invalidate rendering while unchanged frames skip Filament setters. Fog color is linear
  and uses the Environment popout's color-preview transaction, including cancel and scene-rebind fences.
  QDR schema 16 stores document settings; schema 15 introduced fog, and earlier scenes default
  to disabled fog. Zero cutoff means unlimited.
  Owners: [scene settings](src/scene/components.hpp), [viewport fog](src/render/viewport_renderer.cpp),
  [QDR compatibility](src/io/qdr/README.md).
  Tests: [color preview](tests/scene_color_preview_tests.cpp),
  [Environment popup](tests/qt_environment_settings_popup_tests.cpp), [fog pixels](tests/render_fog_runtime_tests.cpp).
- Keep postprocessed viewport and preview grading in Rec709-sRGB-D65: Filament's dithering,
  integer LUT and LDR intermediates need display-encoded values. Linear grading amplifies dark
  noise and loses shadow precision. The maintained presentation patch decodes only the final
  blit into an sRGB swapchain, whose hardware encoding restores those display values; direct
  shaded rendering and linear overlays bypass that conversion. Verify actual native windows
  with dithering and FXAA both on and off; texture-only checks miss the presentation contract.
  Owners: [viewport color grading](src/render/viewport_renderer.cpp),
  [material preview grading](src/render/material_preview_renderer.cpp),
  [Filament patch](thirdparty/filament/README.md).
  Test: [native dark-pixel presentation](tests/render_vsync_runtime_tests.cpp).
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
- Mesh and material-preview tangent packing must preserve handedness through Filament's mediump
  varying. A float-sized quaternion bias underflows on half-turn frames, erasing the bitangent
  and normal-map detail on particular face orientations. Use the shared render packer with a
  normal FP16 minimum; derivative normal mapping retains packed-frame +Y = -dP/dV. Reflected
  or nonuniform world transforms must also select UV-derived tangents. Verify
  independent world directions and near-identity UV continuity, not only Shaded/Rendered parity.
  Coverage: [tangent precision](tests/render_tangent_frame_tests.cpp),
  [windowless Vulkan normals](tests/render_normal_mapping_runtime_tests.cpp).

Owners: [render binding](src/render/render_scene_binding.hpp),
[preview cache](src/render/tool_preview_renderable_cache.cpp), [lit material](resources/materials/quader_mesh_lit.mat).
Tests: [render extraction](tests/render_extraction_tests.cpp),
[render guardrails](tests/render_source_policy_guardrail_tests.cpp),
[texture allocation/lifetime and Vulkan stress](tests/render_texture_lifetime_tests.cpp).

## VSync and accepted presentation

- The pinned Filament main snapshot `aab101b4c038` with quader-vsync6 supplies opt-in Immediate
  presentation and a backend-confirmed mode callback. FIFO is the default/unsupported-mode fallback. Rebuild matching
  headers/libraries together; CMake verifies provenance/hashes. Keep the maintained patch through
  upgrades; the installer consumes compiled artifacts, not a compiler or end-user SDK download.
  Revision 6 preserves upstream's `VulkanSync` constructor and `setFrameRate` during the rebase.
  Recompile all materials with the matching rebuilt `matc`: the main snapshot retains material
  format 76 from v1.76.1 but changes material variants and shaders.
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
[Filament patch](thirdparty/filament/filament-main-aab101b4c038-quader-vsync6.patch).
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
  or structural rebuild. Interpret focused numeric text before flushing on Apply or the canonical
  settings completion key: deferred spinboxes may not have queued their visible draft yet.
  Lifecycle bindings belong to the whole settings panel, including buttons; Space on a focused
  Cancel button must resolve Apply rather than trigger the button's native click on release.
  Flush the exact final value on release/Apply. Discard on button press before
  focus loss can emit editingFinished. An idle numeric scrub performs no owner work.
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

## Polygon face and edge normals

- Ordinary faces default to `Authored`: generated normals use connected geometric fans without
  requiring Shade Smooth or Soften. Every valid incident face contributes its unit geometric normal
  once, including explicitly Flat faces; explicit Flat changes only that face's output normals.
  Do not restore a per-face eligibility gate in the fan builder. Polygon, Bridge, primitive and
  split faces share this policy; topology edits regenerate fans rather than copy compiled normals.
- Edge-normal mode is a separate authored attribute: Hard and Soft live in canonical disjoint edge
  vectors; Default is their absence. All three edge commands leave face shading unchanged.
  Authored compilation never joins boundary/nonmanifold edges. Hard splits, Soft joins, and Default
  joins only when `dot(normalA, normalB) > cos(40 degrees) + 1e-5`. Do not treat every non-Hard edge
  as Soft or gate the edge policy on an initial face-shading operation.
- Topology operations transfer exact modes. Split descendants inherit their source edge; an existing
  merge survivor keeps its mode; otherwise ordered donors apply in operation order and the last valid
  donor wins, including Default. New connectors, walls, rails and caps begin Default. Never combine
  modes with a Hard-wins rule, and prune both override vectors after removed topology.
- QGEO 1.3 stores the existing face-mode bytes and sorted Hard rows followed by Soft rows. Pre-1.3
  encodings conflate constructor-default Flat with explicit Shade Flat: both migrate to Authored.
  This intentionally repairs ordinary saved geometry; historical explicit Flat intent cannot be
  recovered from those bytes. New explicit Flat overrides round-trip unchanged. Before that
  migration, QGEO 1.0/1.1 and legacy CSR materialize Soft for non-Hard edges shared by two Smooth
  faces to retain their old smooth behavior. Lazy native saves canonicalize migrated geometry.

Owners: [normal compiler](src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_mesh_api.cpp),
[edge-mode helpers](src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_topology_helpers.cpp),
[QGEO codec](src/sdk/core/modeling/src/qdr_document.cpp).
Tests: [normal behavior](tests/modeling_domain/operation/qdr_document_operation_normal_shading_tests.cpp),
[primitive normals](tests/modeling_domain/qdr_document_primitive_tests.cpp),
[SDK persistence](tests/sdk/modeling_sdk_consumer_tests.cpp).

## Curves and UV geometry

- Curve controls share canonical component selection and the transform gizmo. The native Curve
  handler owns construction/Alt conversion, not a second modifier/marquee policy. Selected anchor
  plus handle movement must not translate the handle twice.
- Path arrays use portable CurveArcLengthPath geometry and publish one complete compound preview.
  Failed candidates retain previous coverage; Apply uses the structural lifecycle owner and
  restores Curve subselection exactly. Layout-only edits do not rescan source hierarchy/texture plans.
- Solidified Curves participate in the shared material-surface policy. Material-only changes rebind
  without retessellation; eligibility changes advance material-binding revision, point edits do not.
- Guided UV Peel is a native toolbar tool: activate into Edge mode, select guides, preview,
  then Apply or Cancel through the shared lifecycle. Selection edits rebuild the UV preview
  from committed coordinates; invalid or empty guides restore them without ending the tool.
  Apply records one UV history entry and captured repeat options; Cancel keeps selected guides
  and application-lifetime options. Guide tokens and preview geometry remain scene-local;
  rejected candidates cannot replace accepted options. Returning the candidate to original UVs
  must preserve redo history.
  Contracts: [tool workflows](tests/uv_peel_tool_service_tests.cpp).
- Guided UV Peel orders selected connected edge guides and walks opposite edges through quads on
  both sides, including front/back bands and quad end caps. Stop at a boundary, non-quad or the
  starting directed edge. Never restrict structured Peel to incident faces or n-gon rim cases.
  Direct face-corner lanes preserve seam identities, normalize transverse spans by H/Hj, and write
  A then B in guide order; later writes can intentionally replace earlier cyclic-strip values.
  Mixed guides retain the length of edges with empty quad walks; those stations make no stores.
  Do not send structured output through a global chart-overlap veto or authored transverse-cut
  search. Non-quad-only guides retain the irregular guided-LSCM extension and its validation.
  World Density uses Quader's world metric and one tile per 64 displayed map units before repeat;
  these public control units differ from Hammer's local metric/material-based reciprocal scales.
  Fit keeps the guide at its offset with signed transverse sides, without island packing.
  Preserve canonical inverse-sampling certification, per-axis semantic no-op tests and exact
  repeated-output stationarity. Corner storage conversion must not weld intentional UV seams.
  Inverse occurrence compensation fits in double before inversion and certifies actual float
  storage through the existing forward evaluator. Rotated occurrences can require bounded
  residual refinement; preserve an already certified candidate and the canonical error ceiling.
  Saved-scene edge sweeps protect this boundary in
  [uv_peel_saved_scene_tests.cpp](tests/sdk/uv_peel_saved_scene_tests.cpp).

Owners: [Curve tool](src/editor/curve/curve_tool_service.cpp),
[path arrays](src/editor/array/path_array_tool_service.cpp), [UV Peel](src/sdk/core/modeling/src/uv_peel.cpp).
Tests: [Curve workflows](tests/curve_modeling_behavior_tests.cpp),
[path arrays](tests/path_array_tool_service_tests.cpp), [SDK consumers](tests/sdk/modeling_sdk_consumer_tests.cpp).
