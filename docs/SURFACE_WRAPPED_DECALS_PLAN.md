# Surface-wrapped PBR decals: implementation plan

Status: implemented and Release integration built, 2026-10-23. The CPU solver, separate-Quader-mesh proof, renderer, assets, persistence and editor integration are implemented. The real cylinder/drop/texture acceptance diagnostic passes on DX11 and DX12; interactive feature acceptance belongs to the user. The original plan was prepared after checkpoint `cb7e108` was pushed to `origin/codex/material-atomic-nodes`, against renderer baseline `881de829bb37ce46fb043c6bd775691bf6b46ef8`.

## 1. Intended result and decisions

An artist places one reusable decal on level geometry. One image follows a local surface patch across bends and supported contacts between separate Quader mesh entities, without the long streaks caused by planar projection. Color, coverage, normal detail and roughness modify the receiver before its ordinary lighting. The same surface builder and rendering packets later serve spawned blood stains.

| Decision | Implementation direction |
| --- | --- |
| Mapping | A local intrinsic surface chart, measured along the surface; no planar/triplanar substitution on failure |
| Geometry | Rebuildable conforming patch with source-triangle provenance; source meshes remain independent |
| Cross-mesh joins | Temporary contact topology, including partial edges and T-junctions; only unambiguous oriented surface connections |
| PBR integration | Patch rasterization into material decal buffers, then publication through the existing Forward+ data atlas |
| Initial material authoring | Texture-based native decal surface: color, coverage, normal and roughness, with independent channel strengths |
| Scene representation | One ordinary entity with a Decal capability; normal Transform is the sole authored placement authority |
| Initial receivers | Static, full-opacity Lit Opaque/Masked world geometry, including separate Quader meshes and supported ordinary models/primitives |
| Later blood | Same asset, seed resolution, chart builder, cache and renderer; Play-owned spawn/lifetime management |

Surface wrapping is a mapping problem; clustering alone does not solve it. Generated patches make a DBuffer-style integration a better first implementation than the previously considered clustered-image-atlas approach. Each patch can bind its own small texture set, while receiving materials retain all twelve graph texture bindings. The patch is an attribute carrier, not a separately lit transparent object.

Large sphere/saddle patches cannot generally flatten without distortion. This feature promises bounded, validated local wrapping, not an arbitrary distortion-free global unwrap. Skinning/morphs, material Height/tessellation, Unlit/Additive/translucent/partially faded receivers, flowing liquid, thick liquid geometry, full live decal material graphs and manual seam editing are outside this first slice. These exclusions must be visible as concise placement diagnostics, not approximated with floating geometry or planar fallback.

## 2. Implementation anchors

These were the baseline constraints for the implementation; the new surface-pick and decal paths extend these owners:

| Existing code | Consequence |
| --- | --- |
| `third_party/crimson-renderer/include/crimson/renderer/forward/ForwardTypes.hpp`, `PickResult` | GPU picking identifies entity/material slot, not surface triangle or barycentrics |
| `src/physics/PhysicsWorld.h`, `PhysicsRayHit` | Collider hit position/normal cannot serve as authoritative render-surface provenance |
| `third_party/crimson-renderer/include/crimson/renderer/forward/ModelData.h`, `ModelPrimitive`/`ModelAsset::draws` | Receiver identity includes draw/node occurrence and primitive, not just asset UUID |
| `src/resources/StreamingModel.*`, `src/editor/CollisionResources.cpp` | Original LOD CPU geometry can be loaded on existing workers; flattened collision output loses the provenance needed here |
| `src/quader/QuaderImport.cpp`, `src/scene/SceneDocument.cpp` | Reimport preserves occurrence EntityId by source key, but replaces generated subtrees and geometry generations |
| `third_party/crimson-renderer/src/forward/ClusteredLightingGpu.cpp` | Combined RGBA32F atlas already publishes scene data without consuming more material samplers |
| `third_party/crimson-renderer/src/forward/RenderPassPlan.cpp`, `RenderTypes.h` | Numeric views require a deliberate layout revision to insert raster then compute between readable depth and lighting |
| `material_graph_model.sh`, `material_graph_reflection.sh`, `fs_model.sc` | All paths must consume the same decal-modified surface, including SSR receiver evaluation |

Follow [the architecture](ENGINE_ARCHITECTURE.md), [lighting guide](RENDERER_AND_CHARACTER_LIGHTING.md) and [shader publication contract](MATERIAL_SHADER_STARTUP.md). In particular, preserve independent Scene/Game resources, exact asset versions, existing queue retirement and the DX11/DX12 SM5 family.

## 3. CPU chart solver and dependency gate

`DecalChart.cpp` first attempts exact intrinsic triangle unfolding on developable patches, accepting it only when all edge metrics and cycle coordinates agree. Nondevelopable patches use geometry-central's integer-coordinate intrinsic Delaunay triangulation and `VectorHeatMethodSolver::computeLogMap(seed, LogMapStrategy::AffineAdaptive)`. The seed is inserted into the derived patch; common-subdivision transfer returns coordinates to original source triangles and aligns them to the authored tangent direction. Neither path uses planar projection. The library documents intrinsic geometry and improved local coordinates with Delaunay triangulations. [Algorithm documentation](https://geometry-central.net/surface/algorithms/vector_heat_method/)

Selected and tested source revision: `019669ddabda05e0f71fa3587cfb3c1dadf19cb8`. The overlay installs its MIT license and notices. [Pinned source](https://github.com/nmwsharp/geometry-central/tree/019669ddabda05e0f71fa3587cfb3c1dadf19cb8), [license](https://github.com/nmwsharp/geometry-central/blob/019669ddabda05e0f71fa3587cfb3c1dadf19cb8/LICENSE)

Add a project overlay port with exact archive SHA512 values for that source revision and its `happly` gitlink `88f79725d32d78e0e637ebf42d8432d519e1c7d0`. Keep the existing vcpkg baseline. Use one static library, existing vcpkg Eigen, `SUITESPARSE=OFF`, no example viewer/upstream test suite and no configure-time downloads. Ensure `Eigen3::Eigen` exists before configuring the library and reject fallback downloads. Geometry-central supports this dependency resolution, although its documented Windows verification predates our current toolchain. [Dependencies](https://geometry-central.net/build/dependencies/), [Windows build instructions](https://geometry-central.net/build/building/)

The dependency gate passed with MSVC/C++20 and the existing baseline's Eigen 5.0.1. A narrow package-config patch removes the restrictive Eigen 3.3 compatibility request while preserving the already installed `Eigen3::Eigen` target. No baseline upgrade or configure-time dependency download is used. `Build.cmd Release decal-proof` builds and runs the isolated solver, source adapter, resolution and cache fixtures without linking the official application. `Build.cmd Release decal-compile-proof` checks changed host translation units without linking it; `decal-host-proof` builds the focused CPU integration fixtures. The ordinary Release/test/configure meanings remain unchanged.

The contingency considered during planning was a narrow C++20 port of the Boost-1.0 Robust ExpMap core. It was not needed and is not included as a runtime fallback. [Author's implementation and downloads](https://www.dgp.toronto.edu/~rms/software/expmapdemo.html)

No Eigen or geometry-central types escape the adapter. Public data uses Crimson math and plain indices; derived topology/solves use double precision. Existing resource workers own CPU preparation, with cancellation checks between bounded stages and stale-result rejection after a non-interruptible solver stage. No new worker pool or renderer threading rewrite is needed.

## 4. Data model and ownership

### Reusable surface asset

Introduce `.crimson-decal`, native version 1, with a UUID and typed texture references. This is a small texture-surface document, not a new graph editor.

| Field group | Initial contract |
| --- | --- |
| Color | Optional sRGB color texture, linear RGB multiplier, enable and strength |
| Coverage | Optional Linear Data texture/channel; otherwise color alpha when present, otherwise constant 1 |
| Normal | Optional Normal-role texture, strength; absent/disabled means exactly zero added perturbation |
| Roughness | Optional Linear Data texture/channel or constant in [0,1], enable and strength |
| Shared | Finite values, role-correct decoding/mips, existing texture asset IDs and import/picker behavior |

Channel coverage is `textureCoverage * instanceOpacity * enabledChannelStrength`, clamped to [0,1]. Instance tint affects color only; opacity fades all enabled channels. Missing maps must not accidentally paint white or reset normals/roughness: channel enable state is explicit. A roughness-only wet patch is valid without a color image. Do not add metallic/emission/AO channels until their feature slice requests them.

Extend ContentStorage recognition, BrowserCatalog types, creation/rename/move/duplicate, resource dependency scanning, thumbnails and project reference/deletion transactions. Decal texture references must be visited in saved scenes/assets and relevant undo history. Reuse the shared document-editor menu and concise property controls; preview on a simple flat receiver.

### Authored scene capability

`SceneDecal` contains asset reference, surface seed, instance tint/opacity, edge feather, stable layer priority and receiver policy. There is no new special entity class or extra source component row.

The surface seed records scene receiver EntityId, Quader occurrence/source key where applicable, asset UUID and geometry fingerprint, draw/primitive/triangle identity, barycentrics, and receiver-local fallback point/normal. Triangle indices are exact only while the fingerprint matches. They are not permanent semantic face IDs.

Normal entity Transform owns placement, orientation and dimensions. Define local decal footprint as a one-metre square before the full affine transform; displayed footprint dimensions derive from that frame, with no competing independent size field. Project its two in-plane axes into the seed tangent frame and retain scale, in-plane shear and handedness. Require a well-conditioned, nonsingular projected 2x2 footprint matrix: a valid 3D transform can still collapse when projected onto this plane. Its inverse maps metre-valued chart coordinates to normalized decal UVs once. The third axis selects the initial surface search direction; it does not extrude the footprint through the world.

Seed provenance never adds another transform or snaps the authored Transform back. A surface drag changes Transform and seed in one transaction and may explicitly choose a new seed receiver. A normal Transform edit revalidates/reprojects only within the saved seed occurrence around the new frame and regenerates wrapping; it may become unresolved. Receiver edits invalidate wrapping, but a receiver moving away does not drag a static world decal with it. A common authored parent can move the decal and its receivers together.

Keep authored decals outside Quader-generated mesh subtrees, so reimport does not erase them. Default to the scene root or an ordinary authored group. References to receiver occurrences supply the association; generated parentage does not.

Add the component to scene copy/equality/history/extraction and component-kind/Inspector ordering. Introduce scene schema 24 for the new authored capability, read v1-v24, and preserve clean loading of v1-v23. No old scene is rewritten or dirtied merely by loading. Confirm the schema number against concurrent work when implementation begins.

### Derived resources

Use immutable, revisioned `DecalSurfaceSnapshot`, `DecalPatch` and prepared GPU bindings. A patch contains per-receiver spans, source-triangle/barycentric provenance, decal UVs, patch geometry and bounds. One authored decal can produce spans on several entities without merging their assets, slots, collision or Outliner rows.

Cache under `.crimson/cache/decals/`. Keys include builder/solver version, seed/frame/settings, ordered receiver occurrence identities, exact geometry generations, relative affine transforms, unperturbed mesh-shading mode and eligibility changes. Color/roughness/tint edits do not require a geometric solve; receiver changes that alter eligibility or the base normal frame invalidate the relevant prepared data. Keep derived bytes disposable and regenerate from durable Content/bundles; never store expanded import paths as identity. Retain exact immutable payload/GPU leases until all queued users retire.

## 5. Surface extraction and joining separate Quader meshes

1. **Resolve a rendered seed.** Extend on-demand surface placement picking with a detached RGBA32F visible-world-position target and a numeric dense draw/primitive token in W, alongside existing entity picking. Keep token values exactly representable and reject capacity overflow. Use original LOD0 and the actual GPU material coverage expressions. Freeze the request camera/viewport, material clock, document generation and resource signature. On readback, CPU ray queries recover triangle/barycentrics at that returned visible position on the matching occurrence, rejecting ambiguous coplanar matches. Load missing LOD0 on workers. Entity/material ID plus the nearest CPU triangle is insufficient when a nearer masked triangle reveals a deeper triangle of the same entity/material. Do not attempt to reimplement arbitrary graph coverage on CPU. A physics hit is a search hint that must be refined to the eligible visual surface; do not identify a simplified collider triangle with a render triangle.
2. **Gather a bounded neighborhood.** Query immutable triangle BVHs and full occurrence transforms around the maximum distance to the four transformed footprint corners plus a working halo, rather than assuming orthogonal footprint axes. Include offscreen eligible geometry; the patch must not depend on the Scene/Game camera. Use the current decal frame and stable receiver policy, not a global scan on every draw.
3. **Construct analysis topology.** Rejoin duplicated render vertices across proven geometric edges despite UV/normal/material splits. Preserve source normal/tangent/UV data separately. Never weld or edit the source asset. Clip/refine oversized input triangles only in the derived patch.
4. **Find seam segments.** Match coincident edge segments with compatible geometric orientation, split partial overlaps and handle boundary-to-face contact by subdividing the touched derived triangle. Complete endpoint matching alone is insufficient for modular Quader levels.
5. **Choose the exposed surface sheet.** A 90-degree bend is allowed. At floor-wall T contacts, candidate branches must pass local geometric visibility/free-side tests using unperturbed geometry. Continue only when a unique oriented two-sheet continuation is established. Do not assume watertight closed solids or infer that every nearby polygon is connected.
6. **Stop at ambiguity.** Duplicated faces, nonmanifold branches, opposing coincident sheets or inconclusive T contacts form a boundary. Report `Incomplete footprint` and retain a valid seed-connected portion. An explicit receiver set can remove unrelated objects; ambiguous branches within the same receiver still stop. No automatic nearest-surface jump and no manual seam editor in v1.

Initial seam tolerance candidate: `min(0.001 m, 0.001 * min(footprintWidth, footprintHeight))`, with numerical tolerance tracked separately. Freeze it through fixtures before shipping. It permits numerical contact recognition, not arbitrary gap filling. No rendered connector triangles bridge open air; output remains on original receiver triangles. Source occurrences retain independent UUIDs even when their meshes are byte-identical.

The eligibility policy defaults to static world receivers in the seed's Quader linked scene; ordinary meshes outside that source require explicit inclusion. Material-slot changes do not automatically break a connected surface. Source/receiver exclusions are independent of collision layers and the lighting World/Character masks.

Cache shared local contact neighborhoods and acceleration structures. A bounded dirty queue invalidates only affected neighborhoods/decals on receiver publication or transform edits. No full-level topology reconstruction per decal per frame.

## 6. Intrinsic chart construction and validation

1. Insert the seed into the derived sheet, retaining barycentric provenance.
2. Adaptively refine coarse triangles and seam segments in the analysis patch. A small mark on a two-triangle wall must not inherit the wall's enormous sampling scale. Retain a working halo to reduce boundary influence.
3. Build the intrinsic triangulation, flip to Delaunay and solve the selected local log map. Align its tangent direction to the authored frame and apply the inverse in-plane affine footprint matrix once to obtain normalized UVs.
4. Transfer the chart through the common subdivision back to source triangles. Every generated output point must be reconstructible barycentrically on its own receiver; intrinsic edges may not become straight chords across the rendered surface. The implementation uses integer-coordinate intrinsic triangulation and computes orientation from reconstructed source geometry/barycentrics and the chart rather than relying on incomplete signpost common-subdivision orientation metadata. [Integer-coordinate implementation](https://github.com/nmwsharp/geometry-central/blob/019669ddabda05e0f71fa3587cfb3c1dadf19cb8/include/geometrycentral/surface/integer_coordinates_intrinsic_triangulation.h)
5. Validate finite coordinates, orientation, source correspondence, seam continuity, per-triangle Jacobian singular values and nonadjacent UV overlaps. Reject local foldovers and degenerate mappings relative to the expected authored handedness; a consistent intentional mirror is valid. Measure solver distortion in the intrinsic metre chart, accounting for the intentional footprint scale/shear when checking final UVs. Curvature/cut loci can require a smaller footprint.
6. Clip output to the footprint and apply a short UV-edge feather plus texture coverage. Feathering must not conceal a discontinuous seam. Emit one or more render chunks per source receiver identity.

Proposed first gate thresholds: on developable fixtures, metre-scale error below 2% and chart seam disagreement below `0.0025 * min(width, height)`; no UV sign reversals relative to intended handedness or unintended overlap. On curved fixtures, cap local intrinsic stretch/anisotropy at a configurable internal validation threshold, initially 1.25, rather than claiming zero distortion. Measure and revise these engineering targets before promoting the solver; do not present them as guarantees from the papers.

Use explicit triangle/vertex/memory caps and deterministic tie-breaking. Start the CPU proof with a maximum of 16,384 derived triangles per patch and record build/solve timings; exceeding the cap is a diagnostic, never truncation of arbitrary triangles. A common world rigid transform should reuse relative geometry where possible; changed nonuniform scale changes the metric and invalidates the solve.

New invalid placement shows a repairable marker. Preserve an older preview only while its source geometry and display frame remain compatible; otherwise suppress stale geometry. A late completion from an old drag, reimport, document or Play session cannot publish.

## 7. Renderer integration and texture budget

### Material decal buffers

Introduce a camera-owned `DecalGpu` owner in the canonical renderer. Rasterize the conforming patches with real chart UVs, explicit gradients computed before discard/divergence, and no scene-depth writes. The decal shader binds its own small map set plus readable depth/receiver identity. It needs no light/environment textures.

| Target | Format | Payload |
| --- | --- | --- |
| Color | RGBA16F | Premultiplied linear RGB and accumulated color coverage |
| Normal | RGBA16F | Premultiplied world surface-gradient perturbation and normal coverage |
| Roughness | RGBA16F | Premultiplied roughness in R, coverage in A, unused G/B zero |

Clear to zero. Draw lower priority first, then higher priority, with stable persistent decal-ID ties; runtime creation sequence supplies a stable later-spawn ordering. Use premultiplied OVER independently per target, with each output's own alpha. The first implementation uses RGBA16F for all three targets to keep stored alpha and blend semantics explicit on both APIs; narrower formats are a later measured storage change.

Before any blend, require the matching dense per-frame receiver occurrence/primitive token and compatible visible depth. Encode the token in an unfiltered RGBA8 prepass target; zero is ineligible/background. Draw ineligible opaque foreground blockers into depth with token zero. Do not paint characters or unrelated props because they overlap a patch on screen. Mask coverage and strict depth-winner ordering must agree with main opaque shading, including coplanar ties and SSR stencil ownership. Test this explicitly.

All supported receiver shaders read the published resolved values through the existing BRDF/data atlas binding. Apply:

```text
color     = decalColorPremult + baseColor * (1 - decalColorCoverage)
roughness = decalRoughPremult + baseRoughness * (1 - decalRoughCoverage)
gradient  = baseSurfaceGradient + accumulatedDecalSurfaceGradient
normal    = resolveSurfaceGradient(unperturbedReceiverNormal, gradient)
```

Use a shared, tested surface-gradient convention and the receiver's actual unperturbed shading frame, including its mesh-shading mode. A flat decal normal adds zero perturbation and preserves substrate normal detail; the geometric normal/plane remains geometric. Zero channel coverage preserves the original value exactly; do not reconstruct an untouched base normal unnecessarily. Apply specular AA after combining the normals. The mathematical normal-composition reference is [Mikkelsen's surface-gradient framework and demo](https://jcgt.org/published/0009/03/04/), not arbitrary lerping of incompatible tangent frames.

### Atlas publication and resource bounds

Start with three ordinary RGBA32F atlas texels per covered pixel; preserve numerical values without raw half-pair-to-float bitcasts. Raw packing can create subnormal float encodings that a typed float path may flush. A future compact numeric-integer packing can be considered only with cross-API round-trip validation.

Allocate raster targets and publish only a tile-aligned screen rectangle containing visible decal coverage. Keep its origin/extent explicit, crop the raster projection consistently, and return exact zero influence outside the rectangle. Separate distant decals can still produce a large rectangle. No half-resolution approximation in the first correctness implementation.

Append the decal range after existing light/GI/AO/contact/shadow-geometry ranges. Existing shadow/contact float offsets have an exact-address guard below 2^24; do not move those allocations or extend that addressing scheme blindly. Supply the decal base as exact row/column components and compute its addresses with integer arithmetic. Validate combined texture dimensions/capacity before allocation, preserve other ranges on replacement and retire old atlases normally.

Fullscreen upper bounds, before row/tile padding and existing renderer resources:

| Per-view allocation | Bytes/pixel | 1920x1080 | 3840x2160 |
| --- | ---: | ---: | ---: |
| Three raster buffers | 24 | 47.46 MiB | 189.84 MiB |
| Conservative atlas publication | 48 | 94.92 MiB | 379.69 MiB |
| Decal payload subtotal | 72 | 142.38 MiB | 569.53 MiB |
| Receiver identity | 4 | 7.91 MiB | 31.64 MiB |
| Readable depth + existing receiver scope, when otherwise absent | 8 | 15.82 MiB | 63.28 MiB |

Scene/Game allocations are independent; existing SSR/GI allocations are additional. Fullscreen 4K costs are substantial. The renderer proof must report actual ROI bytes and validate capacity with existing effects enabled before full editor rollout. Do not promise a fixed decal count or a performance improvement without measurement.

If the proven allocation budget is unacceptable, resolve publication/storage compression in the renderer milestone; do not conceal the cost or reduce receiver graph textures. Allocation failure retains ordinary scene rendering and a visible diagnostic without changing authored decal data.

### Pass order and shader publication

Required order:

```text
prepared geometry -> readable receiver depth/ID -> decal patch raster
                  -> detached decal atlas publication
                  -> existing lighting effects -> background/opaque
                  -> SSR -> refraction capture -> transparency/presentation
```

Current Scene depth/effects are 112/113; Game is 368/369. There is no slot between them for raster then compute. Explicitly revise `RenderViewLayoutVersion`, pass slots/resources/access validation and profiling names. Use reserved gaps or deliberately move affected prepass slots while preserving all dependency order. Raster and its following publication compute cannot share a physical view: bgfx compute-before-graphics sorting would reverse them. Preserve terminal native presentation slots 504-511 and readback boundaries.

Apply the same resolved surface in fixed and generated opaque shaders, SSR receiver capture/composition, diagnostics and SSGI diffuse-source capture. SSGI traces previous donors; decal appearance/change/fading must participate in source fingerprints/reactivity without resetting all stationary receivers every frame. Refraction sees decals through the completed opaque capture. Geometric SSAO/depth/shadow data stays geometric; no decal collision or shadow caster is created.

Require original LOD0 for participating receiver occurrences while visible decals need matching coverage; restore ordinary residency when released. This has an explicit residency/performance cost. Height/skinning/morphs remain ineligible until a matching geometry path exists. Unsupported surfaces still occlude eligible ones.

Scene, Game, saved-scene thumbnails and the decal preview use independent inputs/resources; ordinary material/particle/node previews allocate nothing for decals. No visible enabled decal means no decal prepass, raster, publication or retained optional allocation beyond normal retirement. Shader initialization/reload, embedded programs, manifests, source baselines and generated material cache identity change coherently. The implementation advances material ABI35/generator36 to ABI36/generator37, and render-view layout to revision 4.

## 8. Reimport, edits, deletion and recovery

- Exact geometry fingerprint: recover triangle/barycentric seed directly and reuse compatible derived geometry.
- New generation: reproject the saved local seed hint only onto the same surviving occurrence, within a footprint-relative position bound and compatible geometric orientation. Reject multiple plausible hits. Rebuild all spans with current geometry; never treat an old triangle index as a stable face ID.
- Moved/deleted receiver or lost seed: preserve the authored decal and ID, suppress invalid patch spans and show a repairable unresolved state. Never attach it to a neighboring entity merely because that surface is closer.
- Quader reimport: retain occurrence EntityId/source-key mapping, authored decal outside generated subtrees, asset/member UUIDs, bundle generations and undo references. A same-ID asset can still have a changed fingerprint.
- Asset deletion/reference replacement: include decal surface/maps and historical dependencies in existing staged project transactions. Preserve unrelated unsaved edits. Receiver deletion and its undo invalidate/recover dependent patches in the same document-generation contract.
- Cache loss: regenerate only derived decal data from durable source/bundle assets; no Quader process/original project is required. Missing/corrupt import expansions use existing bundle recovery first.
- Concurrent drag/reimport: a completion publishes only if document generation, decal recipe, seed and all receiver revisions still match. Changing a source cannot leave an old patch visible on the new source geometry.

## 9. Editor workflow

1. Create/open a Decal surface asset; assign the requested maps using existing import/picker fields. Save/Undo/Redo use `DocumentEditorMenu` and an independent document controller.
2. Drag onto visible eligible geometry. Resolve the render seed and show a pending footprint/preview without committing partial authored data. Commit one entity creation with Transform + seed + asset.
3. Surface-drag to reposition; ordinary Transform rotation/scale edits rebuild the chart around the new frame. A cancelled drag restores the original seed/frame/history state.
4. Inspector has Transform followed by one Decal section: Surface, Tint, Opacity, Edge Feather, Receivers and Layer. Width/height are the effective in-plane Transform dimensions, not an additional competing size control. Keep mapping algorithm and numerical solver settings out of panel bodies.
5. One Outliner row and ordinary selection. The center diamond and footprint edges select the decal entity; the surface-placement tool explicitly acquires the underlying receiver. These editor handles do not replace the receiver ID used by PBR composition.
6. A concise status reports Preparing, Incomplete footprint, Unsupported receiver or Missing surface. Surface re-placement is the repair path; use short tooltips for operation details.

No brush painter, scatter tool, new general attachment framework or gameplay controls are needed for this slice.

## 10. Reuse for blood later

Reserve a plain CPU request contract such as `DecalSpawnRequest { surfaceHit, asset, effectiveFrame, receiverPolicy, tint, opacity, stableOrder }`. Both editor creation and later gameplay call the same seed resolver/resource builder. Physics hits must be refined against visual geometry before binding. Runtime queries must work beyond the current viewport: the surface-query interface accepts a world ray, and a later offscreen GPU coverage query can use the same picking family for masked candidates. Do not make gameplay wait synchronously for a GPU readback or approximate arbitrary mask graphs with CPU alpha sampling.

Authored decals live in the Edit scene and are cloned into Play. Later spawned decals live only in Play-owned transient records by default. They share asset/texture resources and create no project files or per-stain material copies. Play Stop cancels pending jobs and releases patches/GPU leases through normal retirement without writing the Edit scene.

Later blood work adds `SceneDecals` lifetime/fade/population management, bounded asynchronous spawn requests, pooling where measurements justify it, and a small fixture of blood assets. Stable spawn order controls overlaps. Fade changes only material coverage, not chart generation, and updates GI source reactivity. Identical asset/placement inputs can reuse prepared resources; different placements do not share incorrect geometry.

Static floor/wall blood requires no second rendering system. A future rigid attachment is an explicit runtime frame provider from a receiver-local seed, feeding the same builder; it does not change authored world-decal semantics. Skeletal wounds, moving topology, fluid flow and thick pools remain separate slices. The implementation now should preserve this reusable boundary, without adding unrequested combat or blood spawning behavior yet.

## 11. Milestones, dependencies and acceptance

| Milestone | Deliverable and principal owners | Required exit evidence |
| --- | --- | --- |
| 1. Solver feasibility | Pinned overlay/dependency adapter and pure CPU chart fixtures | Current MSVC/C++20 build, developable-scale/orientation checks, curved patch validity, bounded solve/mesh size; no editor feature yet |
| 2. Receiver topology | `src/decals/DecalSurface.*`, `DecalTopology.*`, `DecalChart.*`; original-geometry snapshot/BVH and per-source provenance | Separate-mesh corner continuity, partial-edge/T-junction handling, no thin-wall/gap jumps, explicit ambiguity status |
| 3. PBR renderer proof | Canonical renderer `DecalGpu`, surface-pick readback, patch format/programs, prepass ID, atlas extension and pass-layout revision | DX11/DX12 buffer formats/blending, normal composition, masked/coplanar seed and receiver ownership, effect parity, real resource/capacity accounting |
| 4. Assets and persistence | Decal asset/document, scene capability/schema/history/reference/deletion/extraction | Clean old-scene load, round trip, cancellation/undo, rename/delete reference retention and derived-cache recovery |
| 5. Authoring | Browser creation/drop, surface placement, Inspector/selection, independent preview/thumbnail | One-entity workflow, cross-Quader footprint, complete gesture cancellation and actionable failure states |
| 6. Reimport/lifetime integration | Quader generation rebinding, stale-result rejection, Edit/Play owners and teardown | Exact and changed-generation recovery, receiver deletion/undo, no authored writes/cache-path identity or late Play publication |
| 7. Integration handoff | Canonical publication + Engine pin/integration, docs and focused checks | Published matching commits, one completed Release integration build and focused CPU checks; user performs feature acceptance |
| Later blood slice | Play spawn/fade/lifetime/budget policy using the same services | Transient ground/wall stains across seams, no Content writes, no Edit pollution, deterministic overlap and safe Stop |

Milestones 1-3 are the critical path. CPU asset/persistence work can run in parallel once data contracts are fixed; renderer and geometry owners use explicit packet/version boundaries. Do not build the complete editor before the solver, seam and texture-budget proofs pass.

## 12. Verification matrix and delivery rules

Meaningful CPU fixtures:

- Plane, one-mesh 90-degree hinge, separate-mesh hinge, concave wall-floor corner, curved cylinder strip, huge input triangles, mirrored/nonuniform affine transforms and UV/normal/material splits.
- Partial edge overlap, T-junction inside a coarse face, three-sheet ambiguity, duplicate coplanar faces, opposing thin-wall faces and nearby disconnected geometry. Check real source provenance, not just a visually plausible patch.
- Chart seam/scale error, degeneracy/foldovers/cut-locus growth, deterministic output, limits, cancellation and corrupted cache recovery.
- Scene/asset round trip and old-version cleanliness, gesture undo/cancel, protected/default texture roles, saved/history references and failure rollback.
- Quader same-ID/new-generation remapping, removed occurrence, explicit native Instances sharing geometry, ordinary byte-identical meshes retaining independent ownership, cache reconstruction and unrelated scene preservation.
- Pass dependencies, all12 material texture slots, no-work-when-disabled, two-camera resource isolation, atlas address boundaries above 2^24 and safe retirement.

Native diagnostic fixtures (generated/disposable; do not modify the user's Main scene): checkerboard/circular mark/crack across the seams; roughness-only wetness under direct light, IBL and SSR; flat/strong normal maps over existing normal detail; masked holes and coplanar ties; opaque foreground blockers; fog, refraction and SSGI updates; both APIs and multiple viewport extents. Record actual images/resource counters where appropriate. No user-input automation or profiling captures are needed for this planning task or routine acceptance.

During implementation, owners finish first, then perform one incremental integration build and focused CPU tests. Dependency/preset changes require the wrapper's configure path; `test` and `configure` remain alternative second arguments. Ask the user to save/close a running renderer only immediately before a build that needs its executable, never while implementing or for this plan. Do not expand into a general renderer performance suite.

Renderer work is committed and pushed to canonical shared `main`, then Engine records the exact pin and host integration. Generated shaders are regenerated, never edited manually. Update implemented architecture status, the lighting guide, shader startup contract where relevant and README controls when the feature lands. Implementation evidence follows; the design sections above retain their original milestone structure.

## 13. Implemented acceptance evidence

The first open-strip solver and manually supplied GPU patch fixtures missed two defects exposed by the user's closed cylinder. Chart preparation included the complete circumference and both caps, then discarded the entire stamp on a cap-rim distortion failure. The material raster depth gate also rejected geometrically matching clipped triangles because its slope tolerance was below Direct3D raster precision.

The implementation now bounds the local chart by intrinsic edge distance, uses AffineAdaptive, and retains only validated seed-connected output at chart boundaries. Cache recipe version 2 rebuilds older data. The raster gate uses sixteen FP32 depth ULPs or a 1/256-pixel depth-slope allowance, whichever is larger, while retaining exact receiver identity. It adds no geometry offset or scene-depth writes.

- The exact closed default Cylinder fixture retains 99.901% of UV area; the non-centered seed height 0.0441694 retains 97.4717%. Both publish source-exact curved geometry and diagnose the excluded cap-rim boundary as Incomplete footprint. The 1.25 stretch limit remains enforced.
- The curved GPU oracle previously lost 2,835 of 5,134 interior pixels; the corrected shader loses zero on both DX12 and DX11. A hidden rear patch in the same receiver primitive remains blocked.
- The explicit `decal_host_acceptance_tests` diagnostic copies the actual saved Cylinder/decal records and exact project `dirt_03.png` into a disposable project. A fresh case runs GPU Surface picking, release-state resolution, source preparation, entity creation and the ordinary immutable host-frame path. The saved case verifies the existing authored seed/frame directly. The target is excluded from ordinary builds/tests because these are project-specific inputs.
- Both host cases pass on DX11 and DX12. The saved patch has 2,868 published vertices across 32 original source triangles, a 0.252726 m departure from its placement plane and a maximum source correspondence error of 0.000002384 m. Scene/Game images match at close, oblique and distant views. The matched old/new shader comparison corrects 2,054 pixels on each API with the same actual host patch.
- Raw reports, source/patch exports and before/after images are in `build/Release/verification/decal-host-dx12` and `decal-host-dx11`; the independent geometric oracle is in `build/diagnostics/curved-decal-before-dx12` and `curved-decal-after-dx12`/`-dx11`. These are diagnostic artifacts, not authored Content.
- The final official Release executable linked successfully. Seven focused main-tree decal CPU checks and five isolated dependency/solver/source/cache checks pass. Generated-material twelve-texture PBR, normal/roughness composition and refraction-source fixtures also passed on DX11 and DX12 before the cylinder-specific correction.

The asset document/card preview is a flat plane. A flat textured footprint follows dragging; release builds the conforming patch. The comparison with [Three.js mesh decals](https://github.com/mrdoob/three.js/blob/dev/examples/jsm/geometries/DecalGeometry.js) and [Driven Decals](https://github.com/Anatta336/driven-decals) confirms that their boxes bound source-triangle clipping, while projection UVs can still distort at corners. Crimson retains intrinsic mapping and receiver-material composition. Geometry-central documents [AffineAdaptive](https://geometry-central.net/surface/algorithms/vector_heat_method/) as its highest-quality log-map strategy, with roughly similar cost for a single solve.
