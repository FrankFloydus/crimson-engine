# Editor Core Package

Engine-independent editor behavior.

Owns:

- selection and active-selection state
- hover and picking result state
- tool state machines
- transform previews
- viewport navigation math that does not need engine nodes
- overlay frame generation as portable primitives
- command results for undo/redo

Produces data. Engine hosts render and route UI/input.

Shared/core code is engine-clean: no Godot `Variant`, `Dictionary`, `Array`, `Packed*Array`, `String`, `Vector*`, `Transform*`, `Ref`, `Camera3D`, or `godot_cpp` types/includes. Engine adapters convert into STL, primitive IDs, and `quader_poly` / `quader_editor_core` value types before calling this package.

Current first-pass ownership:

- `PolygonObject` state over `quader_poly::Document`
- selection mode and selected/active object targeting normalization
- active-selection reporting
- object and face selection command/query policy, including source/target hit IDs, selection count/has predicates, and whole-object transform eligibility
- pick selection, face selection, and select-all-faces behavior
- document and selection bounds
- translate/transform selected geometry with texture-lock UV policy
- delete, extrude, inset, edge-loop, face-normal flip, and UV commands
- portable pick hit identity, pick-cycle candidate filtering, and pick-cycle state advancement
- SDK-only pick selection state and hit targeting helpers for selected/first-unselected hit scans
- SDK-owned click-pick and drag-seed target planning over handle and surface hit sets
- portable move, rotate, scale, extrude, extend, and box drag state/math
- portable `ExtendToolState` begin/reset behavior, snap distance, drag-pixel distance, and preview-from-distance/drag policy
- portable modal move/extend/extrude/rotate/scale drag begin, preview-route, commit-route, duplicate/no-op, and undo-label policy
- portable cut-tool state, cut mode cycling, target-object expansion, point capture/drag validation, preview request/status, and hover identity policy
- transform axis parsing, snapping, pivoted transforms, scale factors, and extrude cut-count policy
- portable transform-gizmo axis basis, visibility/alpha scoring, screen-segment distance, and move/scale/rotate pick ordering
- box construction plane normalization, footprint snapping, hover-cell preview, and height preview descriptors
- portable viewport navigation state/math for orbit, orbit snap, pan, zoom, zoom-to-cursor, fly look/move, focus framing, and view-axis stepping
- portable view-cube primitive data and picking policy for face/body hits
- portable selection edit operation policy, selection-box drag state, screen-rect tests, and selection-box overlay primitive data
- portable rectangle projected-hit classification plus rectangle-selection normalization over ordered object/face/edge/vertex universes and hit lists, including active-target fallback and full edge/vertex object compression
- portable select-all/invert normalization over the same universe data, including toggle-all behavior, complement selection, and legacy active-target policy
- portable face-mode conversion over the same selection/universe data, including full edge/vertex object face promotion, partial vertex coverage checks, deterministic face dedupe/order, and selected-object fallback
- portable object-wide face/edge/vertex promotion and full element-to-object promotion over the same universe data, including strict lossless validation, selected-object merging, universe-order output, and preferred/current/fallback active object policy

Current broad selection ownership:

- active target resolution across object/face/edge/vertex selections
- handle-vs-surface hit selection policy for click and drag starts
- selection mode switching results and empty-click cleanup
- hover identity/change policy
- selection projection/feedback and overlay-presentation policy, including dirty IDs, GPU feedback selection, visual face/wire feedback choice, selected-face x-ray eligibility, preview handle mesh selection, extend handle dominant-axis and primitive policy, transform-gizmo axis/pick policy, viewport mouse-motion/button interaction ownership, CPU-handle suppression, overlay lookup normalization, dense overlay eligibility, source-wire suppression, object-operation target IDs, selection-center route planning, handle-target object IDs, element-handle pick ordering, element-handle mode eligibility, default viewport tool intent, move-tool target eligibility, and hover highlight/selected-target feedback

Godot still owns dictionary/resource marshaling, mesh/material creation, viewport node binding, file dialogs, camera/input event conversion, view-cube drawing/mouse capture, rectangle-selection camera projection/raw candidate collection, cache record/universe marshaling for face-mode conversion and full element promotion, face-universe fallback from legacy brush planes, adapter-specific dictionary, geometry, render payload fields, cache-index lookup, projection/visibility tests, actual cached point extraction for centers, transform-gizmo camera projection/plane-handle polygons/render dictionaries, modal-drag camera math adapters, preview mesh execution, brush-kernel patch execution, snapshot restore, extend handle camera projection/screen-space conversion and render node presentation, and public GDExtension method shapes.

The viewport interaction planner decides high-level input ownership from portable state only: fly/navigation, polygon box/move/transform/select routing, fallback tool button dispatch, zoom, and extrude cut-count wheel routing. Godot adapters still perform camera rays, hit tests, mesh/cache queries, preview execution, and render updates.
