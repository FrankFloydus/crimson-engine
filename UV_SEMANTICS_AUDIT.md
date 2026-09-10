<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# UV semantics audit — 9 September 2026

This document records the original audit and the subsequent authorized remediation. The findings below describe the audited starting state; they are retained as the rationale for the fixes, rather than a list of current open defects.

The original audit examined the **0.1.66 worktree**, including its existing uncommitted changes, based on commit `c7a6e9a93b96e920c02be10ca199bb3155cee16b`. The remediation preserves that version and the existing work.

## Remediation

| Finding | Implemented correction |
| --- | --- |
| D1 | Position edits retain authored face-corner UVs, including untouched charts; no-op transforms remain history-free. |
| D2 | Combine rebases displayed sampling and preserves source material assignments in the existing atomic transaction. |
| D3 | Grid, Face and View use the same per-image-axis texel units. Face/View retain the chosen settings; Grid retains its explicit reset policy. |
| D4 | Topology edits retain or interpolate each source face's corner chart. Shared sampling matches the renderer's eligible bilinear quads. Face merging rejects incompatible charts atomically instead of silently replacing them. |
| D5 | Named Clockwise/Counterclockwise actions follow visible image rotation; the numeric rotation convention remains unchanged. |
| D6 | Array previews and accepted copies resolve member/occurrence sampling consistently and apply the same texture-lock settings. |
| D7 | Projection availability shares cached canonical target validation with execution, including mixed unsupported selections. |

English and Italian help now describe projection bases, reset behavior, units, pivots, shared-chart behavior and shared-material scope. Numeric face rotation is labeled **Rotation Offset**, material scale is **UV Tiling**, and **Guided UV Peel** explicitly describes its guide and density policy. Stable command IDs and shortcuts are retained.

The extended review also corrected rectangular-image rotation after alignment, including numeric rotation and its continuous-edit history. Affine Combine rebases, reflections and complete-face native transforms retain interior UV sampling as well as corner values through shared source/result-aware triangulation handling. Native acceptance stages corner-order correction and optional Move welding together, recording full documents only when needed. Derived previews use the same correction with retained render caches; authored previews remain position-only. [Acceptance and preview regressions](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/uv_transform_acceptance_tests.cpp).

The later [complete Hammer-study comparison](HAMMER_UV_COMPARISON.md) accounts for all 295 successful exported functions and the additional failed export. It separates existing-operation correctness from missing UV capabilities and unresolved binary details; missing workflows are not silently implemented as part of these fixes.

## Remediation verification

The complete registered regression suite ran once. Five checks exposed obsolete UV/label expectations and a dependency-boundary violation; those were corrected and their affected suites rerun. The Combine conversion now lives in [MeshProjectionService](C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/projection/mesh_projection_service.cpp:589), called by the existing publication adapter, without expanding the private-header allowlist.

| Verification | Actual result | Evidence |
| --- | --- | --- |
| Complete regression run | SDK consumer 296 passed; parity 4 passed; native 2,864 passed / 10 opt-in skipped / 2 failed; modeling domain 564 passed / 2 failed; guardrails 451 passed / 1 failed. The isolated SDK fixture encountered the same guardrail failure. | [Full output](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_full_last_test.log) |
| Corrected native expectations and affected Combine / command contracts | 29 passed; no failures | [Native rerun](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_repair_native.log) |
| Corrected Slice / Thicken UV expectations | 12 passed; no failures | [Domain rerun](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_repair_domain.log) |
| Corrected ownership / neutral-header guardrail and dependency checks | 9 passed; no failures | [Guardrail rerun](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_repair_guardrails.log) |
| Extended transform, UV, preview, history, merge, repeat and command checks | 110 passed; one failure exposed repeated identical Object preview publication and an unsupported Group geometry-preview expectation | [Extended native run](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_final_native_tests.log) |
| Final preview correction and affected transform / curve / command checks | 76 passed; no failures. Includes all seven new transform acceptance/preview regressions, the corrected Object unchanged-input gate, and the existing Group rejection contract. Unaffected passing results from the extended run are retained. | [Final affected rerun](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_final_cache_tests.log) |
| Final shared-helper SDK isolation | Configure, SDK build/tests, install, and external consumer passed (189.28 s) | [SDK fixture](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_final_sdk_fixture.log) |
| Final source/dependency/type-documentation checks | 10 passed; no failures | [Source checks](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_final_source_checks.log) |
| Canonical Release build and deployment contract | Passed; version remains 0.1.66. Optimization, symbol policy, recursive runtime dependencies and fatal-diagnostics companion verified. | [Build](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_final_release_build.log), [verification](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_final_release_verify.log) |
| Native startup / window smoke | Passed with Vulkan; materials, swapchain and Qt event loop initialized without error/critical/fatal log entries. Screenshot inspected: the Recent-project window rendered correctly. | [Application log](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_final_release_app.log), [screenshot](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_final_release_smoke.png) |
| Intentional command baseline | All 206 command records reviewed. The UV task changes 23 geometry/overlay fingerprints; existing radial shortcut preference and tool-state changes are retained. IDs and outcome policies remain unchanged. | [Semantic review](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_baseline_semantic_review.txt), [full diff](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_remediation_full_baseline.diff) |

All failures from the full regression run are resolved by the recorded affected reruns; successful unrelated results are retained. The initial full run is not represented as an uninterrupted pass. Ten opt-in native tests remained skipped in that run.

The delivered executable is [build/vs2026/Release/quader.exe](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/Release/quader.exe), SHA-256 `7738865f3b54d603cb48ebcb824b3ae35758d499f8222066e0f38a3fa82cda56` (18,241,024 bytes). This is the rebuilt Release artifact, not the earlier executable.

Visual verification is limited to the real startup window. The reported brick/Grid-to-Face workflow and an asymmetric checker were not manually replayed in the GUI. Automated tests check actual compiled UVs, triangle-interior samples and transient render payloads, including preview/acceptance equality; those results are not a claim of GPU-pixel comparison or Hammer runtime parity. The app has no existing CLI/script startup route for that deterministic visual scenario, and native UI automation was unavailable in this session.

Scope covers all 28 exposed face-texture actions, five numeric face fields, Treat as One, six UV Peel options and Peel, Continue Texture, seven material UV fields, both texture-lock policies, and material assignment. Interacting geometry, instance, preview, persistence, and export paths were inspected or exercised where relevant. This is a source audit with existing headless tests, not an exhaustive visual certification of every modeling operation.

## Comparison standard

There is no single universal contract for every button called Align, Reset, Scale, or Peel. These distinctions matter:

| Reference | Verified semantics and limits |
| --- | --- |
| Valve Source 1 | The actual compiler reads VMF scale as world units per texel and divides the texture axis by it. Thus `0.125` is a legitimate physical texel scale, independent of axis direction. This does not establish every Hammer UI button's reset behavior. [VMF parser](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/utils/vbsp/map.cpp#L2863), [mapping conversion](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/utils/vbsp/textures.cpp#L651). |
| Classic Hammer | The archived Valve Hammer 3.4 manual separates scale, shift, rotation, justification, world/face coordinates, and view projection. Negative scale mirrors. This is GoldSrc-era documentation, not current Source 2 evidence. [Face properties](https://documentation.help/Valve-Hammer-Editor-3.4/Face_Properties.htm). |
| TrenchBroom | Edge alignment rotates; justification offsets; fitting scales. Transfer distinguishes projection from rotating the source axes onto a target. Ordinary texture lock and component UV lock are distinct policies. [Manual](https://trenchbroom.github.io/manual/latest/#working-with-materials). Both face and world **Reset** explicitly restore default scale and zero offsets/rotation; their projection axes differ. Reset is therefore not synonymous with scale-preserving alignment. [Reset implementation](https://github.com/TrenchBroom/TrenchBroom/blob/c7ec4e3551805bfa828aadb6096328e4450998e4/lib/TbMdlLib/src/UpdateBrushFaceAttributes.cpp#L159), [UV editor wiring](https://github.com/TrenchBroom/TrenchBroom/blob/c7ec4e3551805bfa828aadb6096328e4450998e4/lib/TbUiLib/src/UvEditor.cpp#L154). |
| Source 2 Hammer | Exact current Face/Grid button contracts could not be independently verified: relevant Valve documentation returned HTTP 403. The user's observation is useful evidence, but this report does not claim a tested Source 2 implementation match. Facepunch documents related face/grid/wrap APIs, but explicitly distinguishes its scene tools from Hammer maps. [Wrapping API](https://sbox.game/api/Sandbox.PolygonMesh/TextureWrapFromFace), [mapping guide](https://sbox.game/dev/doc/editor/mapping/). |
| Blender | Primitive projections conventionally regenerate mappings and may overlap islands; axis, center, and aspect options affect the result. Mapping-node Point scaling and inverse Texture scaling intentionally have opposite visible effects. [UV operators](https://docs.blender.org/manual/en/5.0/modeling/meshes/editing/uv.html), [Mapping node](https://docs.blender.org/manual/en/latest/render/shader_nodes/utilities/vector/mapping.html). |
| Autodesk 3ds Max | Peel belongs to a seam/pin-controlled unwrap workflow. Quader's selected-edge flow mapping is a specialized operation with a different selection contract. [Unwrap UVW](https://help.autodesk.com/cloudhelp/2017/ENU/3DSMax/files/GUID-EA10E59F-DE7F-497E-B399-6CF213A02C8D.htm). |

## Confirmed correctness findings

Priority P1 means loss of existing mapping data during routine editing; P2 means an incorrect visible result; P3 means a smaller availability/presentation defect. Findings are source-confirmed. Unless explicitly stated in the verification section, their proposed reproductions were not manually performed in the GUI.

### D1 — P1: Component edits erase authored UVs, including untouched faces

`translate_selection`, `transform_selection`, and `set_vertex_positions` call `clear_face_uvs(document)` after positions change. Edge snapping and Radial Align have the same broad reset policy. Moving one vertex can therefore replace custom mappings on disconnected, untouched parts of the same mesh with procedural UVs. Native component transformations and drag-position updates use these paths without restoring the charts.

The existing test `PositionChangingOperationsRestoreProceduralFaceUvs` explicitly expects every face UV array to become empty after moving one vertex. It passed during this audit. This is an implemented and tested destructive policy, not an intermittent render-cache problem. [Portable owner](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/polygon_document.cpp:2203), [test](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/sdk/modeling_sdk_consumer_tests.cpp:384), [native transform](C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/modeling/services/modeling_sdk_projection_adapter.cpp:4732).

**Required correction:** preserve authored corner data on unaffected faces. Decide separately whether affected faces retain attached UVs or compensate deformation under an explicit component UV-lock policy. Neither policy requires deleting unrelated charts. Preserve cancellation, zero-change behavior, and exact undo/redo.

### D2 — P1: Combine Mesh loses source charts and mapping compensation

Each nonsurviving mesh is rebased through `working.transform_selection`, triggering D1 even for an identity transform. In addition, Combine does not bake each source entity's effective texture-sampling transform into the survivor's coordinates. The survivor keeps its mapping, while removed entities lose theirs. Merely removing the UV-clear call will not fix the second issue. [Combine publication](C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/modeling/services/modeling_sdk_projection_adapter.cpp:9560).

**Required correction:** capture each source's displayed mapping, preserve/rebase its corner UVs, and compensate for the survivor's retained mapping before publication. Joining two meshes with different object transforms or texture-lock compensation should leave their visible textures unchanged. Blender's Join explicitly merges UV layers as object data. [Blender Join](https://docs.blender.org/manual/en/5.0/scene_layout/object/editing/join.html).

Existing native Combine tests inspect geometry and winding rather than effective UV equality. [Tests](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/programmatic_behavior_scenario_tests.cpp:3080).

### D3 — P2: Align to Face/View uses different density units from Grid

This explains the reported Grid → Face texture shrink. Grid applies material-resolution normalization using `64 / width` and `64 / height`. Face and View omit it and use the fixed 64-map-unit generated chart while retaining the same numeric face scale. [Alignment implementation](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/face_texture_actions.cpp:893).

For a **512 × 512 image**, an axis-aligned face, identity material tiling, and face scale `0.125`:

| Operation | Resulting repeat distance | Repeat density |
| --- | --- | --- |
| Grid | 64 displayed map units | One tile per 64 units |
| Face or View | 8 displayed map units | Eight tiles per 64 units |

The scale field remains `0.125` in both cases. The screenshot's actual texture dimensions were not measured; eightfold is the verified mathematical example, not a claimed measurement of that image.

**Required correction:** use consistent material-axis resolution units for all three alignment actions. Grid may retain its documented reset to scale `0.125`, zero rotation and zero shift. Face/View should retain their chosen scale, rotation, and shift while changing projection basis. For non-square images, convert rotated/scaled chart coordinates per image axis **before adding the retained normalized shift**; scaling the base axes before rotation mixes width and height incorrectly. Reuse the existing effective-UV solving and occurrence compensation.

World projection and face-normal projection can legitimately differ on sloped surfaces through foreshortening. The defect is the material-resolution-dependent conversion mismatch, not every visual difference between projection bases. Existing Face/View tests use default 64-pixel extents, which mask it.

### D4 — P2: Several topology operations discard preservable charts

| Operation | Source-confirmed behavior |
| --- | --- |
| Edge Split | Duplicates vertex identities at identical positions with unchanged face-corner order, then clears UVs. Detach performs a comparable identity separation while retaining them. [Split](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/operations/split_edges_operation.cpp:118). |
| Insert Edge Loop | Clears both resulting charts instead of retaining old corner values and interpolating new corners. Its test seeds authored UVs and explicitly expects their removal. [Operation](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/operations/insert_edge_loop_operation.cpp:111), [test](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/modeling_domain/operation/qdr_document_operation_insert_edge_loop_tests.cpp:163). |
| Connect Vertices | Replacement faces copy material and transform settings but omit UVs, even though their corners use existing vertex positions. [Operation](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/operations/connect_vertices_operation.cpp:85). |
| Connect Edges | Clears expanded neighboring faces receiving midpoint corners, then clears the resulting split charts. [Operation](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/operations/connect_edges_operation.cpp:130). |
| Dissolve Vertex | Removes redundant straight-line corners but clears the whole remaining chart. Shared loop simplification also does this. [Operation](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/operations/dissolve_vertices_operation.cpp:102), [helper](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_bridge_surface_helpers.cpp:3127). |
| Dissolve Edges / Combine Faces | Chart inheritance checks only nondefault numeric texture transforms. Authored UVs with default numeric settings are overlooked, so compatible custom charts can disappear when faces merge. [Inheritance guard](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_bridge_surface_helpers.cpp:523). |
| Vertex / Edge Bevel | Retained portions of original faces lose their existing UVs. New caps/strips need a mapping policy separately; their newness alone is not a data-loss defect. [Vertex bevel](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/operations/bevel_vertices_operation.cpp:131), [edge bevel](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/operations/bevel_edges_operation.cpp:1159). |
| Knife / Cut tool | Clears charts on split faces and neighboring faces that receive a boundary vertex, without later restoration. [Knife helper](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_knife_helpers.cpp:446), split output at lines 1769 and 1900. |
| Element Extrude / Inset | Shared implementation clears the moved cap's chart despite retained corner correspondence. Ordinary face extrusion, in contrast, retains its cap UVs. [Shared helper](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_surface_operations.cpp:387), [ordinary extrusion](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/operations/extrude_faces_operation.cpp:91). |
| Remove Doubles / merge repair | Clears touched charts and tries affine reconstruction. Nonaffine authored charts fail that fit and fall back to generated UVs, including surviving corners. Auto Merge already has a narrower restoration path for unchanged remapped corner correspondence. [Repair](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_hull_plane_helpers.cpp:893), [fallback](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_uv_helpers.cpp:297), [Auto Merge restoration](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/operations/auto_merge_vertices_operation.cpp:324). |

**Required correction:** preserve retained corners, interpolate inserted corners in each source face's own chart, and preserve seams rather than treating one geometric vertex as having one universal UV. For face merging, define rejection or seam behavior when the two mappings cannot be represented by one resulting face. Do not silently regenerate compatible custom charts just because their numeric transform happens to be identity.

Blender's Loop Cut exposes UV correction to avoid distortion; even its uncorrected alternative subdivides UV faces rather than discarding the mapping. [Loop Cut](https://docs.blender.org/manual/en/latest/modeling/meshes/tools/loop.html), [detailed option](https://docs.blender.org/manual/it/4.5/modeling/meshes/editing/edge/loopcut_slide.html).

### D5 — P2: Clockwise and Counterclockwise actions are reversed

The Clockwise action adds a positive angle. On the canonical front-facing XY test wall, the chart is `(x/2, -y/2)` and the transform applies rotation to UV coordinates before dividing by scale. Tracking an asymmetric image feature initially at world `(2, 1)`, a positive 15° moves it to approximately `(1.673, 1.484)`: counterclockwise when viewed from the face front. Native action mapping performs no compensating sign reversal. [Action](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/face_texture_actions.cpp:804), [UV evaluation](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_uv_helpers.cpp:658), [front-wall test fixture](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/sdk/modeling_sdk_consumer_tests.cpp:5686).

**Required correction:** fix the named action directions, with a test following an image feature in front-view coordinates. Existing tests check angle values/formula composition rather than visible CW/CCW direction. Preserve the established numeric rotation convention unless that is deliberately redesigned too.

### D6 — P2: Linear/Radial instance previews resolve the wrong texture frame

For instance sources, preview geometry comes from the definition member, but `source_entity` is set to the occurrence. Render extraction consequently reads occurrence-only sampling instead of composing member and occurrence sampling in member coordinates. A transformed member or nonidentity member mapping can therefore show different UVs in the preview and accepted array, with either independent or linked copies. [Radial source](C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/array/radial_array_tool_service.cpp:1060), [render extraction](C:/Users/Drako/Desktop/quader-windows/quader-app/src/api/quader_render_api.cpp:336).

**Required correction:** carry the member and occurrence identity through the existing preview/render owner and resolve effective sampling exactly as real scene occurrences do. Test equality of effective UV sampling, not only geometry matrices and materials. Existing array tests do not cover this distinction.

### D7 — P3: Projection commands appear available for ineligible entities

Mesh-mode projection enablement checks only whether any entity is selected. Curves or lights can therefore enable Cube/Cylinder/Sphere UV Projection even though execution rejects them. [Command predicate](C:/Users/Drako/Desktop/quader-windows/quader-app/src/api/quader_commands_api.cpp:2362).

**Required correction:** use the operation's existing eligibility policy for presentation without adding a second mutable selection truth or expensive per-frame scans.

## Complete exposed-operation inventory

The following covers every face-texture action. Grouped rows name all variants explicitly. Main ownership is the public modeling API → modeling service → portable face-texture action owner. These should remain shared implementations.

| Operation(s) | Current meaning | Semantic assessment |
| --- | --- | --- |
| Align Grid | World-axis projection; resets scale to `0.125` map units/texel, rotation/shift to zero; resolves image size. | Valid **reset** convention, but should say that it resets settings. See D3. |
| Align Face | Geometric face-plane projection, anchored at a face corner; retains face settings. | Recognizable operation; density-unit bug D3. Phase can change with the origin. |
| Align View | Captured camera-plane projection; retains settings. | Recognizable operation; density-unit bug D3. Projection can foreshorten. |
| Scale X Up/Down; Scale Y Up/Down | Double/halve signed scale magnitude within directional bounds. Larger magnitude enlarges the visible texture. | Sound image-size convention. Negative signs remain mirrors. |
| Shift Left/Right/Up/Down | Move by one texture pixel using each face material's image dimensions. | Sound. Directions follow texture coordinates after rotation/mirroring, not universally camera axes. |
| Rotate CW/CCW | Increment by the configured rotation step. | Direction names reversed: D5. |
| Flip Texture Horizontal/Vertical | Negate the respective face-scale sign, retaining shifts. | Conventional mirror via signed scale. Pivot is the base chart origin, not the face-bounds center. |
| Flip Face Horizontal/Vertical | Reflect actual geometry in face/shared-plane axes and carry UVs with corner identities. Reject conflicting shared vertices or incompatible partial winding changes. | Geometry operation, not merely UV mirroring or normal reversal. Its placement under Edit Texture needs an explicit explanation or relocation. |
| Fit Both/X/Y | Rescale effective UV bounds to one tile on chosen axes. Individual fitting updates face fields; other axis is preserved for X/Y. | Conventional fit/stretch. Both does not promise preserved image aspect. |
| Justify Left/Right/Top/Bottom/Center | Offset individual effective UV bounds to the chosen tile boundary/center. | Conventional justification. See Treat as One caveat below. |
| Cube Projection | Dominant local-axis box mapping using selected local bounds and largest extent; sides overlap; resets face transforms. | Conventional box projection with fixed options. |
| Cylinder Projection | Local Y axis; bounds-normalized X/Z ellipse; longitude/height sides, separate planar caps and seam handling; resets face transforms. | Valid cylindrical convention. Fixed axis and cap policy should be stated. |
| Sphere Projection | Local Y poles; bounds-normalized ellipsoid; longitude/latitude with seam and pole handling; resets face transforms. | Valid spherical convention. Not a world-density-preserving action. |

The remaining surfaces complete the inventory:

| Surface | Current meaning | Assessment / recommended clarification |
| --- | --- | --- |
| Numeric face Scale X/Y | Absolute stored face-transform coefficients; mixed selections can receive absolute values. | Valid modifiers, **not universal measurements of effective texel density**. |
| Numeric face Shift X/Y | Pixel offsets converted to normalized UV; only one face can edit them numerically. | Show `(px)`. Multi-face shift buttons work although numeric fields show `--`; explain this restriction or provide a relative multi-face edit. |
| Numeric Rotate (°) | Delta from rotations captured when the selection changed. A fresh selection displays zero, even if already rotated. | Intentional and tested, but misleading as an absolute property. Rename to **Rotation Offset (°)** or add a separately defined absolute field. |
| Treat as One | Shared reference projection/axes and combined bounds: a shared UV chart for Fit/Justify, shared geometric reflection axes for Face Flip; other actions remain per face. | Custom projection behavior, not merely treating existing islands as one set. Grouped Justify can change another face's density/orientation along the edited axis. Grouped Fit can bake density into UVs while retaining scale fields. |
| UV Peel | Selected edge chains guide flow across their incident faces. Supports disk/annular patches, with a transverse chart cut where needed; not arbitrary selected-face unwrap. | Useful specialized mapping. **Guided UV Peel** or **UV Flow** sets a clearer expectation than unqualified Peel. |
| Peel Axis U/V | Selects which chart axis follows the guide. | Sound guided-flow parameter. |
| Peel World Space | Builds a fresh physical chart at one tile per 64 displayed map units before repeat factors. | Tooltip incorrectly implies preservation of incoming density. Say **Use World Density** and state the base distance, or implement preserve-density as a distinct policy. |
| Peel World Space off | Independently stretches each patch's U and V into one tile. | Intentional normalization; changes aspect and overlaps disconnected patches. It does not pack an atlas. |
| Peel U/V Repeat | Multiplies each resulting chart axis. | Conventional tiling multiplier; larger values mean more repetitions. |
| Peel U/V Offset | Adds normalized UV offsets after repeats. | Label in **tiles**, not pixels. |
| Continue Texture | Copies material/settings and continues an affine mapping across face planes, using displayed occurrence frames; selection/geometry remain unchanged. | Sound wrap/transfer semantics. Rejects nonaffine sources and nonplanar faces rather than approximating silently. |
| Material assignment / replacement | Changes material references, separately from Continue Texture. | Valid distinction. An image of different dimensions or a different material UV transform can change appearance without changing face fields. |
| Material Center U/V | UV transform pivot, default `(0.5, 0.5)`. | Conventional. State normalized tile units and shared-material scope. |
| Material Scale U/V | Multiplies UV coordinates before material rotation. Larger values shrink the visible texture and add repeats. | Valid coordinate-scale convention, opposite to face image-size scale. Rename **UV Tiling U/V** to remove ambiguity. |
| Material Offset U/V | Adds normalized UV translation. | Label in tiles. Applies through the material, not just the selected face. |
| Material Rotation | Rotates material UV coordinates around its center. | Conventional coordinate transform. Distinguish from selection-relative face Rotate. |
| Texture Lock | Scene object/group translation and rotation keep mapping attached when enabled; default on. | Sound independent transform policy, captured at gesture start. It does not automatically define component deformation behavior. |
| Texture Lock Scale | Independently keeps mapping attached during scene scaling; default off. | Sound separate policy. Both switches and their interaction should be described together. |

Projection bounds are per selected mesh in local coordinates, not one world-space set across the selection. Treat as One does not change that. Cube/Cylinder/Sphere do not account for material image aspect or preserve prior physical density. These are documented-default/control gaps, not automatically defective algorithms. [Projection owner](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/face_texture_actions.cpp:948).

Peel preserves numeric face settings by solving compensating base UVs; those numbers can stay unchanged while a new chart changes apparent density. [Peel owner](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/uv_peel.cpp:718). Treat as One's tooltip already mentions a shared projection, but the practical consequence for Justify is easy to miss. [Existing labels/help](C:/Users/Drako/Desktop/quader-windows/quader-app/resources/locales/en-US.lang:1046).

General seam marking, UV-island editing, stitching, and atlas packing were not found as exposed UV operations. These are absent capabilities, not incorrect implementations of existing commands.

## Why the same Scale value can show different texture sizes

The visible mapping has several layers: the authored/generated UV chart, the face transform, object/instance sampling compensation, and the material's UV transform. The face panel reports one layer, not their combined derivative in world space. [Face summary](C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/modeling/services/face_texture_edit_context.cpp:165), [material shader](C:/Users/Drako/Desktop/quader-windows/quader-app/resources/materials/quader_mesh.mat:373).

For arbitrary authored UVs, one face scale value cannot describe every local density or distortion. Keeping the coefficient visible is legitimate. The UI should identify it as a modifier; a future physical-density readout should report measured units and mixed/nonuniform cases honestly. This does **not** excuse D3, where closely related alignment operations themselves mix density units.

Most action tooltips currently repeat the label/shortcut without explaining projection basis, reset behavior, units, or pivots. A coordinated English/Italian tooltip pass would resolve much of the confusion without changing good algorithms. [Tooltip assembly](C:/Users/Drako/Desktop/quader-windows/quader-app/src/ui/qt_tool_settings_panel.cpp:1700).

## Additional consistency and coverage concerns

- **Array lock policy differs by tool.** Linear/Radial preserve source sampling regardless of lock preferences, while Path Array and duplicated transforms consult them. Either copying appearance or evaluating world locks can be a valid product policy; use one explicit rule across array owners. This is separate from the incorrect instance preview composition in D6. [Direct array publication](C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/modeling/services/modeling_session_service.cpp:8410), [Path Array plan](C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/array/path_array_tool_service.cpp:1478).
- **Grouped one-axis Fit/Justify rejects some degenerate cases accepted individually.** The grouped reference requires a full-rank two-axis chart even when only one axis is changing. Add a narrowly scoped case with the untouched axis collapsed before deciding whether to relax it. [Grouped bounds](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/face_texture_actions.cpp:843).
- **Texture transfer and locks have substantial good coverage.** Affine seam continuation, mirrored/transformed occurrences, material-only changes, rollback, independent lock policies, captured settings, and history are covered. Instance Flatten correctly bakes effective sampling while compensating retained face settings. No concrete defect was found in those inspected paths.
- **Existing preservation helpers provide useful precedents.** Plane Cut retains existing corner UVs and interpolates intersections, unlike the Knife paths above. Detach preserves corner arrays, and Auto Merge restores them when remapped corner correspondence permits it. Reuse these policies where applicable rather than adding unrelated UV truth. [Plane Cut](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/operations/plane_cut_operation.cpp:203).
- **Do not assert global preservation from isolated good paths.** Detach and Flatten preserving UVs does not establish preservation for Connect, Bevel, Combine, or every deformation. Geometry-operation review here supplements the full exposed UV-command inventory; it is not a runtime test of every possible topology.

## Original audit verification (before remediation)

At the original read-only audit stage, existing test binaries in the canonical `build/vs2026/Debug` test configuration were reused. No application build was needed at that stage, and the Release executable was not replaced or newly visually verified. These historical results do not validate the later corrections; see the remediation verification above.

| Run | Actual result | Evidence |
| --- | --- | --- |
| Native UV/texture, material-document, material-editor, pivot, and Combine checks | 181 selected: **180 passed**, **1 skipped**, 0 failures | [Native log](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_audit_native_tests.log) |
| Portable SDK texture/UV checks | **67 passed**, 0 failures | [SDK log](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_audit_sdk_tests.log) |
| Modeling-domain Insert Edge Loop, Edge Split, and Detach owner suites | **19 passed**, 0 failures | [Topology log](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/uv_audit_topology_tests.log) |

Total: **266 passed, 1 skipped**. The skipped test was the opt-in `RenderTextureLifetimeTests.VulkanMaterialEditAndRetirementStress`. Machine-readable results are adjacent `uv_audit_*_tests.json` files.

Passing tests characterize current contracts. In particular, the component-transform and loop-cut tests explicitly expect authored UV deletion. They must be changed deliberately with the preservation fix; their present success is not evidence against these findings. The additional Connect/Dissolve/Bevel/Knife/Extrude/merge source findings and the proposed visible-direction/instance-preview reproductions were not added as new runtime tests during this read-only implementation audit. No independent Subdivide operation was found in the scoped portable sources; primitive subdivision settings were not treated as existing-surface UV edits.

## Recommended correction order and acceptance criteria

1. **Preserve authored mapping first: D1, D2, D4.** Centralize corner preservation/interpolation in the existing portable topology owners. Cover disconnected untouched faces, identity numeric transforms with nondefault charts, seams, identity/nonidentity Combine transforms, instance compensation, cancellation, failed publication, and exact single-step undo/redo. New surfaces need a declared mapping policy; retained surfaces must not silently lose one.
2. **Correct alignment units and named rotation: D3, D5.** Test 64/128/512 and non-square images; unequal signed scales, 37°/90° rotation, shifts, missing dimensions, transformed/mirrored occurrences, and repeated no-ops. Track an asymmetric image feature for direction. Compare Grid/Face density on an axis-aligned face; separately test legitimate slope foreshortening.
3. **Unify preview sampling and explicit array lock policy: D6.** Compare preview and accepted effective UVs for transformed definition members and occurrences, independent/linked copies, multiple sources, all lock combinations, and undo/redo.
4. **Clarify controls together.** Explain Grid reset, Face/View axes, face scale modifiers, material UV tiling, shift/offset units, relative Rotate, guided Peel, shared-chart Justify, and actual geometry Face Flip. Preserve stable IDs and update both English and Italian, API documentation, tooltips, and intended command contracts together. Fix D7 using the existing eligibility owner.
5. **Verify the resulting implementation under the full regression gate once**, because fixing shared topology/UV preservation reaches mutation, history, and preview mechanisms. Review intentional command-baseline differences, then build and visually verify canonical Release with a labeled asymmetric checker and the reported brick workflow. Do not reinterpret this audit's passing checks as that future validation.
