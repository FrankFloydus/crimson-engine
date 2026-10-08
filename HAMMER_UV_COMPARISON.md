<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Hammer UV comparison and complete function review

**Historical implementation review.** The structured UV Peel traversal and coordinate writer
described here were replaced on 8 October 2026. Current ownership, independently authored scope
and retained compatibility are documented in [Guided UV Peel ownership](src/sdk/core/modeling/src/uv_peel.md).
This report preserves the earlier research record; its recovered implementation descriptions
are not instructions to restore that code.

Reviewed **9 September 2026**, against Quader **0.1.66**, including the current uncommitted UV remediation. The Git base is `c7a6e9a93b96e920c02be10ca199bb3155cee16b`; it identifies the starting commit, not a clean snapshot containing every reviewed change.

The review accounts for **every candidate in the supplied study: 295 complete successful pseudocode bodies and one failed export, 296 unique addresses total**. It compares actual algorithms, UI/API entry points, publication owners and relevant Quader test bodies. It does not equate 295 functions with 295 features.

Quader already implements the main texture parameter operations, planar alignment, primitive projections, affine face-to-face continuation and a substantive guided UV solver. The clearest additions demonstrated by the study are UV-only bounds flipping, edge/vertex UV stitching, general selected-face unwrap modes, automatic neighbor propagation, a broader 2D mapping workspace and multiple UV sets. These are **capability gaps**, not automatically bugs in Quader's existing commands. Confirmed defects and their authorized corrections remain in [UV_SEMANTICS_AUDIT.md](UV_SEMANTICS_AUDIT.md).

The full ledger is [HAMMER_UV_FUNCTION_REVIEW.json](HAMMER_UV_FUNCTION_REVIEW.json). Each record includes its address, export outcome, review extent, responsibility, evidence, Quader references, assessment, confidence, source file/hash and direct callees from the study manifest.

## Coverage, provenance and limits

| Coverage check | Actual result |
| --- | ---: |
| Study candidates | 296 |
| Successfully exported complete bodies reviewed | 295 |
| Failed export separately reviewed/accounted | 1 |
| Unique ledger addresses | 296 |
| Missing, extra or duplicate addresses | 0 |
| Successful pseudocode bytes reviewed | 1,781,693 |
| Quader reference file/line checks | All referenced files exist; lines are within their current files |

Six nonoverlapping shards reviewed 46, 49, 50, 50, 51 and 50 functions. Every successful body was read completely, including long constructors, detached cleanup branches and supporting containers. The consolidating reviewer read all six final ledgers and findings and reconciled overlapping claims. Classifications total **238 support, 24 differs, 18 missing, 14 matches and 2 unresolved**. Several missing records are stages of the same Square/Conforming workflow.

“Matches” means the relevant operation family or responsibility exists, without certifying identical numerics, settings, supported inputs or runtime behavior. “Support” identifies a reviewed body, not an omitted review: topology access, sparse algebra, import indexing, allocation, registration, metadata, history and runtime routines are all included.

| Source identity | Recorded value |
| --- | --- |
| DLL | `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\tools\hammer.dll` |
| Size / preferred image base | 38,791,832 bytes / `0x180000000` |
| SHA-256 | `b4755b0a909d0d19ca35f14346643765b32eb4bc18951a777991549ce913dd78` |
| Tools | Ghidra 12.1.3, Temurin JDK 21.0.12.1+1, Capstone 5.0.7 |
| Study | [README](C:/Users/Drako/Desktop/hammer-uv-analysis/README.md), [binary metadata](C:/Users/Drako/Desktop/hammer-uv-analysis/binary-metadata.json), [tool provenance](C:/Users/Drako/Desktop/hammer-uv-analysis/tooling/PROVENANCE.md) |
| Coverage authority | [decompilation-summary.json](C:/Users/Drako/Desktop/hammer-uv-analysis/decompilation-summary.json) |

The exports are machine-generated pseudocode, **not Valve's original C++**. No recovered routine was compiled or executed; no controlled Hammer input/output comparison was performed. This covers the **supplied targeted export set**, not the whole editor or every UV-related function. Direct-call lists do not resolve all virtual/function-object targets. No decompiled implementation was copied into Quader.

The failed candidate is [0x1812d5e60](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1812d5e60.c), whose flow exceeded Ghidra's instruction limit. Its failure stub and full supplied assembly were reviewed; it manages typed variant payloads, but jump tables and ownership cases remain incompletely reconstructed. It is explicitly `failed`/`unresolved`, never counted as a successful body. The other unresolved record, [0x18039a620](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/18039a620.c), is a successfully exported preview/callback wrapper whose opaque dispatch does not establish the exact interactive projector semantics.

## Capability comparison

| Capability | Hammer evidence from reviewed bodies | Current Quader comparison |
| --- | --- | --- |
| Grid / Face / View | Panel exposes separate choices; the view generator projects transformed positions onto camera axes. UI evidence alone does not establish complete Face/Grid mathematics. | All three exist. The authorized correction gives them consistent per-image-axis density units; Grid retains its reset policy, Face/View retain chosen settings. Sloped projection can still legitimately foreshorten. |
| Scale, shift, rotation | Separate scale multiplication, uniform corner-UV translation and normal-axis basis rotation bodies. | All exist. Face scale is a reciprocal mapping divisor; material UV tiling is a separate coordinate multiplier. Named rotation direction was corrected under the local audit. Exact Hammer angle/default parity is not asserted. |
| Fit / Justify / Treat as One | Fit and edge/center justification execute on per-face or supplied combined bounds. | All exist with a shared-chart combined-bounds owner. Quader single-axis Fit preserves the other component; Hammer's parameter fit additionally adjusts both offsets. |
| Texture flip | Stored U/V four-component axis, including offset, is negated. | Existing Flip Texture negates signed scale while keeping shift. Both mirror, but their pivots differ. |
| Explicit UV bounds flip | Separate dispatcher calls `min + max - coordinate` and publishes UV changes. | **Missing distinct operation.** Quader Flip Face reflects geometry; it is not this UV-only action. |
| Planar / cylindrical projection | ModelDoc execution accepts projection-frame/output-stream settings; cylinder and normalized planar arithmetic are present. | View/face/grid and Cube/Cylinder/Sphere exist. Quader's cylinder derives local bounds and handles caps; configurable frame/output channel and alternate height-coordinate choice differ. Cube/Sphere numerical parity is not established by registry labels. |
| Continue / wrap texture | Basis transport rotates across planes with offset compensation; shared/near-parallel edge and plane fallbacks exist. | Already implemented for affine planar charts, including non-adjacent faces. Quader preserves analytic world phase and rejects non-affine/nonplanar sources. Anchor and obtuse-angle policies differ. |
| Edge-guided Peel | 3D chain length becomes a UV coordinate; transverse stations normalize toward a common maximum span. | General two-sided quad traversal and direct face-corner arc-length writes; transverse lanes normalize to the maximum span. The September 10 correction below supersedes the incident-only/physical-width implementation. Non-quad-only guides retain a conformal extension. |
| Conforming / Square face unwrap | Actual mode dispatch, triangulation, pins, conformal equations, square reference triangles and four-sided constraints. | **Missing general selected-face modes.** Conformal math inside Guided UV Peel does not expose this workflow. |
| Follow Quads | Traverses selected quads into rows/columns, averages dimensions and forms cumulative rectangular coordinates. | **Missing general selected-quad mode.** Quader's strip specialization requires explicit guides and has a different metric. |
| Weld / stitch UVs | Edge/vertex paths align charts by integer repeats, then anchor-copy or average incident UVs. | **Missing UV-only operations.** Mesh Auto Merge and internal seam repair are different responsibilities. |
| Fill UVs from neighbors | Pending-face frontier repeatedly chooses resolved neighbors and invokes wrap. | **Missing bulk workflow.** Explicit pairwise continuation already supplies part of the math. |
| 2D mapping workspace | Constructor creates a UV widget with connected mapping, picked-edge orientation, rectangle/inset, fit/tiling/aspect and display controls. | **Missing integrated workspace and some control surfaces.** Existing actions cover substantial arithmetic. Controls alone do not prove manual pin editing, packing or relaxation. |
| Multiple UV sets | FBX import publishes independent UV value/index streams; mutation wrappers edit one or all available channels. | **Missing multichannel storage/editing/interchange capability.** Current authored corner and compiled vertex payloads contain one UV set. |
| FBX / named attributes | Complete importer handles corner indices, multiple UV sets, colors, user data, materials and transforms. | No FBX importer in inspected source/API/dependencies. Quader does preserve supported authored seams and material slots. This is an interchange boundary, not another unwrap defect. |
| Locks / modifier metadata | Preferences persist texture, scale and component locks; ModelDoc exposes named-stream and UV2 properties. | Quader exposes movement/rotation and scale locks. Metadata bodies do not establish third-lock execution, lightmap charting, hotspots or UV-derived hard-edge algorithms. |

Quader owners: [action catalog](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/mesh/face_texture_actions.hpp:19), [texture implementation](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/face_texture_actions.cpp:635), [native facade](C:/Users/Drako/Desktop/quader-windows/quader-app/src/api/quader_modeling_api.hpp:124), [Peel API](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/mesh/uv_peel.hpp:24), [solver](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/uv_peel_solver.cpp:609), [tool settings](C:/Users/Drako/Desktop/quader-windows/quader-app/src/ui/qt_tool_settings_panel.cpp:643), [texture-lock plan](C:/Users/Drako/Desktop/quader-windows/quader-app/src/editor/commands/document/scene_texture_mapping_plan.hpp:24).

## Most useful missing workflows

This ranking is an engineering judgment for Quader's polygon-texturing workflow, considering frequency, benefit and integration scope. It is a proposed order, **not a claim these additions were implemented or every Hammer behavior should be copied**.

### 1. UV-only bounds flip

A chart spanning U = 0.2 to 0.7 reflects as `U' = 0.9 - U`, preserving that image interval and all mesh positions. The dispatcher gathers explicit corner values; the helper computes actual bounds rather than assuming 0–1.

Quader Flip Face changes positions and winding. Preserve that identity and add an explicitly UV-only action if approved. Bounds scope—face, chart or whole selection—must be defined; the helper does not prescribe every caller's scope.

Evidence: [dispatcher 0x1807c7130](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1807c7130.c), [arithmetic 0x180bfb340](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180bfb340.c), [Quader mirror](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/face_texture_actions.cpp:1334), [geometry-flip test](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/sdk/modeling_sdk_consumer_tests.cpp:7244).

### 2. Stitch or weld UVs on selected edges and vertices

The edge helper aligns chart midpoints modulo whole repeats, then copies an anchored seam or averages endpoint pairs. The vertex helper aligns incident charts before using a reference UV or mean.

These repair texture seams without merging geometry. Auto Merge changes topology while preserving separate charts; the internal one-axis seam helper repairs mappings during topology operations. Neither supplies public UV stitching. An addition needs explicit affected-chart/reference policy and one atomic UV transaction.

Evidence: [edge wrapper 0x1807ad830](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1807ad830.c), [edge arithmetic 0x1807db790](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1807db790.c), [vertex arithmetic 0x1807ae450](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1807ae450.c), [Auto Merge](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/operations/auto_merge_vertices_operation.cpp:324), [seam helper](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_uv_helpers.cpp:681).

### 3. General face unwrap: Conforming, Square and Follow Quads

This family has three distinct strategies:

- **Conforming:** constructs a patch, initializes a chart, chooses two extremal-U pins and solves conformal equations.
- **Square:** adds right-isosceles reference triangles and strategies constraining four sides of a rectangle; fallback pins derive from near-extreme boundary coordinates.
- **Follow Quads:** traverses four-edge faces into rows/columns and uses averaged opposite-edge dimensions to form rectangular cells.

Quader already has conformal equations, robust sparse LSQR, chart validation and a strip specialization. Reuse appropriate foundations, but preserve Guided UV Peel's edges as flow guides. Its API requires a guide; it should not silently become unguided unwrap. The earlier variable-width policy was superseded by source-matched transverse lane normalization in the September 10 correction.

Evidence: [mode dispatcher](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180311940.c), [patch adapter](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180befba0.c), [triangle frame](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180bf6cc0.c), [square constraints](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180bfc340.c), [fallback](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180bfde60.c), [Quads](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180bf06d0.c), [guide requirement](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/uv_peel_solver.cpp:54), [variable-width test](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/sdk/modeling_sdk_consumer_tests.cpp:8333).

### 4. Automatically continue textures across a selected face patch

Quader already wraps an affine chart onto a target plane. Hammer adds a frontier: pending targets choose resolved neighbors by normal agreement and a tie term, then are removed as they resolve; passes repeat while progress is possible.

This avoids individually naming every source/target pair on adjacent architecture. Deterministic source priority, seam/reference policy, partial-failure behavior and atomic publication must be specified beyond the wrap formula.

Evidence: [propagation](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1807b9a10.c), [ranking](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1807c4700.c), [pair wrapping](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1807dc1b0.c), [Quader transfer](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/face_texture_actions.cpp:1704), [tests](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/sdk/modeling_sdk_consumer_tests.cpp:5310).

### 5. A focused 2D mapping workspace and richer region controls

The constructor establishes connected mapping modes, Use Existing, picked-edge U/V orientation, rectangle fit, world scale, single-axis tiling, insets, aspect/repeat and display/navigation controls. The orientation helper actually rotates a chart around its first anchor to align the anchor edge with U or V.

This supports a missing integrated workflow, not a recovered full DCC UV editor. Manual pins, packing, relaxation, arbitrary vertex editing and every region-evaluation rule are not proven by constructors. Existing Quader API/actions should remain the modeling owners rather than introducing parallel UI UV truth.

Evidence: [workspace 0x18026b710](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/18026b710.c), [bindings](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/18030e920.c), [anchor orientation](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180bf54a0.c), [current actions](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/mesh/face_texture_actions.hpp:19).

Two smaller control differences need separate specification:

- **Fit X/Y integers, range 1–128:** Quader targets a one-tile span and exposes no requested extents. Exact downstream interpretation of the Hammer integers was not established by the reviewed callback body; do not advertise a verified repeat-count formula yet.
- **Cylinder height-to-U versus height-to-V:** Quader currently offers height-to-V. Hammer's Z-up label versus Quader's local Y-up is a coordinate convention, not a bug.

Evidence: [texture panel](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180237880.c), [one-tile bounds affine](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/face_texture_actions.cpp:635), [cylinder](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/face_texture_actions.cpp:1092), [options](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/include/quader/modeling/mesh/face_texture_actions.hpp:89).

### 6. Multiple UV sets, with interchange considered separately

The FBX writer publishes distinct float UV values and corner-index maps for primary/secondary and higher named streams. Mutation wrappers address one channel or enumerate all available channels. Selector **3 means all available channels**, not a three-channel limit.

Quader has one authored and compiled UV set. Secondary/named sets affect storage, persistence, APIs, import/export, editing scope and rendering. FBX also handles attributes and materials beyond UVs; its implementation is a separate larger scope.

Evidence: [UV import](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/18058b560.c), [FBX converter](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1805907e0.c), [channel-aware rotation](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1807d5dc0.c), [authored face](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/neutral/include/quader/modeling/neutral/polygon/document_model.hpp:90), [native vertex](C:/Users/Drako/Desktop/quader-windows/quader-app/src/api/quader_api_types.hpp:1146), [existing preservation test](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/modeling_domain/qdr_document_mesh_uv_tests.cpp:41).

## Differences to document or test before changing policy

These reconcile narrower “matches” and “differs” records. Matching a basic operation does not erase differences in axis scope, phase or normalization.

| Topic | Actual difference | Implication |
| --- | --- | --- |
| Flip pivot | Hammer negates axis and offset. Quader keeps shift: `U' = 2*shift - U`. | Preserve the parametric command unless deliberately changing its pivot; bounds flip is separate. |
| Single-axis Fit | Hammer fits one scale then justifies both offsets. Quader preserves the other UV component. | Quader behavior is tested; cross-axis movement is a product choice. |
| Guide width | Hammer normalizes transverse spans to a common maximum. Quader retains widths unless extra guides constrain them. | Valid variable-width charts can differ. |
| Solver | Hammer uses float CG; Quader uses normalized, column-scaled double LSQR with bounded work and validation. | Same conformal family can use different solvers. Replacement alone does not establish parity. |
| Boundary metric | Hammer square pins measure the current UV path; Quader guide lengths use 3D geometry. | Constraints belong to different workflows. |
| Wrap orientation | Hammer folds obtuse angles and can choose nearest near-parallel edges; Quader uses oriented planes and analytic/world-phase policy. | Matched runtime fixtures are needed before changing the convention. |
| Transfer domain | For a shared edge Quader uses its endpoint UVs and a conditioned third source corner; otherwise it requires an affine source chart. Hammer can reuse separately stored parameters when reconstruction is skipped or fails. | Shared-edge continuation now follows the recovered local-chart rule. Quader does not store a second independent set of full 3D texture axes. |
| Whole-tile phase | Hammer may normalize phase modulo one; Quader retains exact phase. | Repeating images can look identical with different stored coordinates. |
| Projection | ModelDoc normalizes a configurable frame and named output stream. Quader separates density-preserving alignment from Fit and defined bounds/caps. | ModelDoc parameters are not automatically the Hammer map viewport contract. |

Arithmetic: [parameter flip](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1807c6d20.c), [fit/justify](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1809c8ae0.c), [Peel](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1807b2af0.c), [CG](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180bf3850.c), [conformal coefficients](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180bf50a0.c), [boundary pins](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180bfe530.c), [wrap](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/1809c9070.c), [basis reconstruction](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180be6da0.c), [cylinder](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180b4c250.c), [planar](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180b4cbf0.c).

Quader contracts: [transform formula](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/internal/mesh/polygon/quader_poly_document_uv_helpers.cpp:860), [Fit](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/face_texture_actions.cpp:635), [Fit tests](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/sdk/modeling_sdk_consumer_tests.cpp:6866), [strip metrics](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/uv_peel_solver.cpp:684), [conformal system](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/uv_peel_solver.cpp:609), [LSQR](C:/Users/Drako/Desktop/quader-windows/quader-app/src/sdk/core/modeling/src/uv_peel_solver.cpp:855), [world-phase tests](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/sdk/modeling_sdk_consumer_tests.cpp:5390), [invalid transfer](C:/Users/Drako/Desktop/quader-windows/quader-app/tests/sdk/modeling_sdk_consumer_tests.cpp:5561).

## What this study does not establish

- **Full Hammer source/runtime parity:** this set includes ModelDoc and FBX consumers; some relevant callees and indirect targets are outside it.
- **Atlas packing or UV relaxation:** tile-origin subtraction only translates a chart; it does not separate islands, add gutters or adjust density.
- **A lightmap generator:** UV2/density/high-precision metadata proves settings exposure, not generation or packing. Multiple UV sets are independently established by import/mutation code.
- **Manual pin editing:** solver anchors and pin arrays are internal constraints, not proof of an editable pin UI.
- **Third component-lock, hotspot or UV-derived hard-edge algorithms:** preferences prove persisted names/values; their consumers were not reconstructed.
- **Alignment-shrink diagnosis from a label:** Quader's image-dimension mismatch is grounded in its own owner (D3 of the local audit), not a claimed matched Source 2 Face/Grid runtime fixture.
- **A reason to remove Quader protections:** affine/planarity checks, unchanged-frame gates, atomic publication and guided-chart validation should not disappear merely because Hammer uses different storage/fallbacks.

Evidence: [preference loader](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180759080.c), [writer](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/18075c2b0.c), [UV2 metadata](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180c73d90.c), [tile normalization](C:/Users/Drako/Desktop/hammer-uv-analysis/decompiled/180311400.c), [local audit](UV_SEMANTICS_AUDIT.md).

Assembly corrected misleading pseudocode rather than turning it into alleged bugs: camera projection returns XYZ through SIMD registers; the normalizer returns a float length; an endpoint thunk preserves two output registers; an unsupported FBX mode callback is a no-op; and the chart scorer returns an averaged float despite Ghidra's `void` signature. Exact addresses and evidence remain in the ledger.

## Storage and continuation follow-up

The reported tapered-side-to-lip failure triggered a targeted extension of the source study:
**46 additional successful exports**, bringing the external study to **341 successful bodies**.
The original 295-function ledger above remains the inventory for the original audit. The new
[storage handoff](C:/Users/Drako/Desktop/hammer-uv-analysis/UV-STORAGE-HANDOFF.md) traces the actual
Alt+right-click route, parameter/corner storage, forward evaluation, reverse reconstruction and
fallbacks. These are static decompilation findings; Hammer runtime parity was not measured.

Hammer persists both full U/V axis-and-offset parameters and corner UVs. They can differ for a
non-affine face. Same-mesh continuation across a shared edge reconstructs from its two endpoint
UVs and one source corner selected by an angle/length conditioning score, without auditing all
remaining source corners. Quader's shared-edge transfer now follows that sampling rule while
retaining exact seam phase, source selection and atomic target publication. Other transfers retain
the affine-source contract because Quader has no independent stored axis parameters to fall back to.

The exact saved lip regression also exposed conflicting target precision checks, independently of
wrap orientation. The shared correction separates semantic equality, affine-source fit uncertainty
and reconstruction error, improves thin-chart conditioning, and evaluates affine sampling without
subtracting rounded world positions. Additional UV-only validation and local-coordinate fixes
prevent unrelated collapsed geometry or distant occurrences from blocking ordinary UV edits.
Regressions live in [lip workflows](tests/project_material_behavior_tests.cpp),
[shared-edge wrapping](tests/sdk/face_texture_wrap_tests.cpp),
[UV action reliability](tests/sdk/uv_action_reliability_tests.cpp) and
[UV precision reliability](tests/sdk/uv_precision_reliability_tests.cpp).

## Original audit verification results

The **static review and coverage checks are complete**: exact manifest address equality, complete-body accounting for every successful export, separate failed-export accounting, and existing/in-range Quader references. This verifies the review inventory, not runtime equivalence.

Shard reviewers did not execute Hammer or run builds. The coordinator completed the authorized Quader remediation and its verification. The full registered regression run was performed once; its five failed checks were corrected and their affected suites passed on rerun. The extended native UV/transform/preview/history/merge/repeat and command run passed 110 checks and exposed one remaining preview-cache/test-contract failure. That correction and its affected preview/transform/curve/command suites then passed **76/76**, retaining successful unrelated results. The updated shared helper also passed the isolated SDK configure/build/tests/install/external-consumer fixture. Ten opt-in native tests remained skipped in the full run. Full chronology, individual results and logs are recorded in [UV_SEMANTICS_AUDIT.md](UV_SEMANTICS_AUDIT.md).

Canonical **Release built and passed deployment verification**, preserving version **0.1.66**: [quader.exe](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/Release/quader.exe). SHA-256: `7738865f3b54d603cb48ebcb824b3ae35758d499f8222066e0f38a3fa82cda56`. Vulkan startup/window smoke passed, and the captured Recent-project window was visually inspected. The specific brick/Grid-to-Face GUI workflow was not manually replayed; automated UV/render-payload tests do not establish GPU-pixel or Hammer runtime parity.

## Continuation and shared UV reliability verification

The follow-up reproduced both rejected horizontal lips from the saved scene before changing the
shared UV owner. Its portable and native regressions now pass, including diagonal continuation
from the tapered side, interior render UVs, source preservation, repeat no-op, single-step
undo/redo and publication rollback. Coverage also includes thin and oblique charts, distant
sampling, signed/rotated/sheared mappings, non-affine shared-edge sources and unrelated collapsed
authored geometry.

The full regression gate ran once. Four individual failures were resolved: two new test setup or
expectation errors, an OBJ test requiring exact zero text for roundoff-sized UV values, and a
command fingerprint containing raw carrier bits. Explicitly stored generated charts now retain
their exact procedural carrier after the same rank check. The remaining Merge Edges sheared
chart has a cross coefficient of approximately `-2.04e-17` instead of zero; only its geometry and
composite hashes changed. That single command entry was reviewed and updated through the
baseline tool; authored UVs, topology, selection and history behavior are unchanged.

Final affected verification passed **140 native checks across the correction runs**, **313 SDK
consumer tests**, **566 modeling-domain tests**, **4 parity tests**, and the isolated SDK
configure/build/test/install/external-consumer fixture. Successful unrelated full-gate results
were retained; its ten opt-in native/GPU tests remained skipped. Logs are in
`build/vs2026/texture_lip_*`; the final command baseline comparison is
`texture_lip_baseline_review.json`.

That follow-up's canonical **Release** built and passed deployment verification on 2026-09-09,
preserving **0.1.66**. Executable:
[quader.exe](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/Release/quader.exe),
SHA-256 `c216b33a07d5f48f4adb3c1ca6de690510b040bd06f362afc50891ef6e92dc12`.
The new Filament materials loaded during startup smoke and the Recent-project window screenshot
was visually inspected. The lip gesture was not manually replayed in the viewport; automated
render-payload checks do not establish GPU-pixel or Hammer runtime parity.

## Cylinder Peel follow-up (September 9; traversal superseded below)

The saved 32-sided cylinder reproduced the reported rejection on its middle ring. Target **156**
identifies the modeling object; it is not an error number. Its independently reconstructed face
corners differed within the shared UV owner's certified storage precision, but Peel imposed a
second, smaller continuity tolerance. The stored-triangle overlap check also capped the permitted
contact around a shared corner too tightly for narrow triangles.

Peel now uses the canonical per-axis reconstruction bounds for corner continuity. The overlap
check uses measured corner displacement and the incident angle to bound shared-corner contact,
while still rejecting folds, unrelated overlap, invalid stitched topology and contact extending
beyond that geometric neighborhood. Semantic no-op comparisons remain separate. The exact saved
geometry, UVs, occurrence translation and scale are covered by 36 axis/density/active-edge variants;
repeat remains an exact no-op.

The authorized source task recovered **24 more successful Peel bodies**, bringing the external
study to **365 successful bodies** plus one earlier unrelated failed candidate. Its
[Peel handoff](C:/Users/Drako/Desktop/hammer-uv-analysis/UV-PEEL-HANDOFF.md) and
[topology evidence](C:/Users/Drako/Desktop/hammer-uv-analysis/UV-PEEL-TOPOLOGY-NOTES.md) establish a
quad-strip walk on both sides of the selected guide. The walk stops before a non-quad cap; an empty
cap-side lane paired with a populated wall-side lane is valid. A single edge, an open arc and a
closed rim are accepted guide classes. The writer stores separate face-corner UVs and preserves
the intentional transverse seam. This is static source evidence and synthetic reference testing,
not a Hammer runtime comparison.

Quader previously included the incident n-gon cap, turning a cylinder rim's wall chart into a disk.
Recognized coherent rim strips now traverse the wall quad rows and leave cap UVs untouched. Tests
cover top and bottom rims with 1, 16 and 32 selected edges, both axes, without a middle loop. The
user's second exact selection/error was not captured; these fixtures isolate the verified cap
inclusion defect. Ambiguous topology retains the existing incident-patch fallback, and irregular
guided conformal charts remain a Quader extension.

Verification passed **315 SDK consumer tests**, **4 SDK/native parity tests**, and **51 native
workflow/command/publication tests**, including saved-cylinder continuity, cap preservation,
selection preservation, repeat, stale-token rejection, single-step undo/redo and rollback. Source
guardrails passed. This bounded Peel correction used the affected verification path, retaining the
previous shared-foundation full-gate results. Regression owners:
[portable cylinder cases](tests/sdk/modeling_sdk_consumer_tests.cpp),
[native cylinder workflows](tests/programmatic_behavior_scenario_tests.cpp), and
[publication rollback](tests/modeling_session_service_split_tests.cpp).
Logs are in `build/vs2026/uv_peel_cylinder_*`.

Canonical **Release** built and passed deployment verification on 2026-09-09, preserving
**0.1.66**:
[quader.exe](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/Release/quader.exe),
SHA-256 `531afcada365ff3874a379de8f6b2f2377ff24ce420c6af8856c663ce196e01c`.
Startup/window smoke passed and its Recent-project screenshot was visually inspected. The Peel
gesture was not manually replayed in the viewport; the saved-cylinder regressions exercised the
portable and public native API paths. Only documentation comments changed after this build.

## General quad-walk Peel correction — September 10

The September 9 cylinder-only expansion did not implement the general source traversal. It still
used incident-face patches and a global disk/annulus chart requirement for many valid quad strips.
The user's arch screenshot and the saved object159/169 failures exposed this architectural gap.
The earlier claim that the loop-cut issue was generally fixed was too broad.

The replacement orders selected connected guides, then walks opposite edges across quads in both
directions from every selected edge. It stops at a boundary, a non-quad face, or the initial directed
edge. Materials, normal changes, existing UV seams and semantic front/back/cap labels do not stop
it. Consequently a longitudinal inside-arch guide can reach the front, back and outer profile;
a single width edge can travel along the arch and through quad end caps. Cylinder n-gon caps stop
because of their topology. This does not flood every connected face or complete unselected guide
edges.

The writer retains authored face-corner identities and the recovered ordered A/B lane stores.
Cyclic transverse paths can address the same corners twice. An exact matching endpoint handle
makes their combined span one circuit, then B intentionally overwrites A. The final seam uses
separate corners at the same position. Each lane normalizes to the maximum transverse span using
H/Hj, including World Density mode. Fit mode keeps the guide at its offset, with signed transverse
coordinates; axis V transposes longitudinal/transverse coordinates as in the source writer.
Structured output does not pass through the old global non-overlap or authored transverse-cut
search. Finite span, bounded traversal, valid topology, local face mapping and canonical inverse
storage safeguards remain. Guides without a usable quad-side representation retain Quader's
separate conformal extension.

Source evidence and independent reference cases:
[arch/cyclic walk notes](C:/Users/Drako/Desktop/hammer-uv-analysis/UV-PEEL-ARCH-NOTES.md),
[corner output formulas](C:/Users/Drako/Desktop/hammer-uv-analysis/UV-PEEL-OUTPUT-NOTES.md),
[reference replay](C:/Users/Drako/Desktop/hammer-uv-analysis/scripts/peel_arch_reference.py).
Seven synthetic source replays include full inside rails, single width edges, unequal profiles and
triangle end-cap stops. This is static decompilation and mathematical reference evidence, not
execution of Hammer or bit-exact SSE equivalence.

Quader retains its published control conventions: World Density measures world-space distances
with a fixed density, repeats multiply the output, offsets are normalized tiles, and U is the
default. Hammer's inspected controls use local geometry, material dimensions and reciprocal
world-mode repeat scales. Matching the quad walk and corner writer does not claim equivalence of
those different UI/API units. English/Italian help now describes the actual traversal and lane
normalization.

The complete [consolidated handoff](C:/Users/Drako/Desktop/hammer-uv-analysis/FINAL-HANDOFF.md)
and [precision matrix](C:/Users/Drako/Desktop/hammer-uv-analysis/PRECISION-AUDIT-HANDOFF.md)
were reconciled after the investigation explicitly completed. This caught one further mixed-guide
case: empty quad walks perform no writes, but their selected-edge lengths still contribute to the
guide coordinate. Direct traversal applies when any selected edge supports a quad walk; only
entirely unsupported guides use the conformal extension.

The exact saved object159 exposed a separate occurrence-compensation error: an approximately
0.00079 UV phase residual exceeded the existing 0.0005 certification ceiling. The inverse helper
now retains double fit coefficients before inversion and performs bounded refinement against the
actual stored float values and forward evaluator. It retains previously certified candidates and
rejects nonfinite refinements. The forward mapping, public storage contract and certification
limits are unchanged. Embedded saved object159/169 fixtures sweep every quad-adjacent authored
edge, both axes and density modes, including exact repeat no-op and attribute preservation.

The full regression gate ran once. It initially exposed the saved159 inverse failure and the
intentional Peel command fingerprint change. After correction, failed and affected checks passed:
319 SDK consumer tests, 566 modeling-domain tests, four SDK/native parity tests, 194 native
UV/workflow/command tests (one opt-in Vulkan stress test skipped), source guardrails, and the
isolated SDK install/consumer fixture. The command-baseline comparison against the saved pre-task
baseline changed only `modeling.uv_peel` UV-containing geometry and composite fingerprints plus
the approval reason; outcomes, selection, other domains and lifecycle behavior were unchanged.
Logs are in `build/vs2026/uv_peel_arch_*`.

All source findings refer to the preserved analyzed DLL SHA-256
`b4755b0a909d0d19ca35f14346643765b32eb4bc18951a777991549ce913dd78`.
The installed Steam DLL differed at final inspection and was not reanalyzed. The completed study
contains 368 emitted bodies from 369 candidates; its precision pass manually reviewed 66
addresses, with 27 floating-signaling bodies not re-audited. These boundaries preclude claims of
exhaustive current-installed Hammer parity.

Canonical Release built and passed deployment verification on 2026-09-10 at 15:18 local time,
preserving version **0.1.66**:
[quader.exe](C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/Release/quader.exe),
18,289,152 bytes, SHA-256
`db25087bce1100f926b2460c769784df46fffafaa2f33c0f5f099c112d32a179`.
Startup/window smoke passed, its Recent-project screenshot was visually inspected, and its log
contained no error/critical/fatal entries. Peel was verified through portable and native public
workflows; the gesture was not manually replayed in the viewport or compared with running Hammer.

## Dissolve rejection correction — September 10

The preceding UV preservation audit added an overly strict Dissolve Edges postcondition: every
original face's entire interior UV field had to survive merging and retriangulation. This blocked
valid geometry edits on UV seams and piecewise charts. The saved `Arch` front-band edge 16–1297
reproduced the reported rejection using exact faces 1266–1268. Independent seam and piecewise
chart fixtures failed for the same reason before correction.

Dissolve now accepts that removing an interior edge can remove its UV seam and change interior
interpolation. It retains the existing outgoing-boundary-edge donor rule, survivor material and
texture frame, UV-aware simplification, finite-value checks and geometry validation. The change
removes only Dissolve's whole-interior certification veto; shared UV helpers and transaction
publication are unchanged. Regression owners are
[operation cases](tests/modeling_domain/operation/qdr_document_operation_dissolve_edges_tests.cpp)
and [saved Arch SDK/history case](tests/sdk/dissolve_saved_scene_tests.cpp).

Preservation here means stored boundary UVs in the face texture frame. Generic native topology
publication refits per-face carriers and does not guarantee unchanged displayed UVs under a
nonidentity occurrence mapping; the removed local-chart check did not certify those either.
Removing an interior seam can visibly change the merged face's texture.

Final affected verification passed 569 modeling-domain tests, 320 SDK consumer tests, four parity
tests, 247 native command/workflow/publication tests and three source guardrails. The command
baseline remained byte-for-byte unchanged. The operation-specific scope and test registration
used affected checks, retaining the prior shared-foundation regression results. Reproduction and
verification logs are in `build/vs2026/dissolve_uv_*`.

Release built and passed deployment verification on 2026-09-10 at 15:33 local time; version
remains **0.1.66**. The canonical executable is 18,288,640 bytes, SHA-256
`d6b1972e8c7a80d05d1fcc0dbb0346d1d87e01663d1b59c954085a1d20d17eaf`.
Startup/window smoke passed and its Recent-project screenshot was visually inspected. The
Dissolve gesture was verified headlessly through the saved SDK case and native contracts, not
manually replayed in the viewport.

### Blender vertex cleanup follow-up

The user's remaining mid-edge points exposed another mismatch in the old shared simplification
rule: it required both geometric collinearity and interpolated UVs at every removed corner.
Blender's dedicated Dissolve Edges instead defaults to `use_verts=true`, `angle_threshold=180`,
`use_face_split=false`, and `use_preserve_quads=true`. Its operation captures selected-edge
cleanup candidates before joining faces, then collapses affected survivors with exactly two
incident edges. UV breakpoints and bends do not veto the default cleanup.
References: [editor defaults](C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/blender-main/source/blender/editors/mesh/editmesh_tools.cc:5965)
and [BMesh sequence](C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/blender-main/source/blender/bmesh/operators/bmo_dissolve.cc:455).

Quader's Dissolve Edges now follows that sequence, including original triangle-pair protection,
incidental untouched-face corner protection, common subdivided boundary chains, and deferred
endpoint cleanup. Surviving corner data stays intact; affected hard-edge flags are combined and
removed references are pruned. Result selection contains only live faces. Cleanup is scoped to
affected geometry, retaining unrelated degree-two and loose vertices. Shared Combine Faces and
explicit Dissolve Vertices behavior is unchanged. Quader's polygon-only validity contract keeps
a candidate when collapse would leave a neighboring face with fewer than three corners, instead
of producing Blender's possible wire/degenerate-face result.

Before correction, four operation cases reproduced retained endpoint or batch failures. Final
verification passed 572 modeling-domain tests, 321 SDK consumer tests, four parity tests, 247
native command/workflow/publication tests and three source guardrails. The exact saved Arch
mid-depth subset checks two edge orders, removal of vertices 2465/2466, retention of junction
2468, surviving UVs and attributes, and exact one-step undo/redo. The command baseline remains
unchanged. Logs are in `build/vs2026/dissolve_vertices_*`.

Canonical Release built and passed deployment verification on 2026-09-10 at 15:46 local time,
version **0.1.66**, 18,297,344 bytes, SHA-256
`45c300582513447236d7af8dc63bcfb6c2b76de4626df2b7112ffd37cf1ed34e`.
This follow-up was verified through headless document and native command workflows; no manual
viewport replay or running-Blender comparison was performed.
