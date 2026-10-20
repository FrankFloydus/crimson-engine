# Per-mesh shading checkpoint

Implemented 2026-10-20 on `codex/mesh-shading`. Canonical renderer source is
`6449dc6f8f15a2de2a0b35295621c62102c5e91b`; the Engine host adds occurrence
authoring, persistence, resource demand and focused verification.

## Authoring and behavior

Inspector → Model → Shading offers Use Mesh Normals (0, default), Flat (1), and
Smooth (2). The choice belongs to the occurrence, including occurrences sharing
one asset. Scene v23 reads missing `meshShading` as Use Mesh Normals, validates
integer values 0–2 and writes the current choice on explicit Save. Loading does
not rewrite data. Equality, extraction, undo/redo/cancel, duplication, Play,
matching-ID Quader reimport and saved-scene thumbnails preserve it.

Use Mesh Normals preserves authored normals and the existing displacement-plane
adjustment. Flat derives the rasterized triangle normal and a matching normal-map
frame. Smooth regenerates angle-weighted normals within existing position and
authored-normal charts. UV duplicates can join; original hard edges stay separate.
Skin and morph identities split charts when coincident vertices deform differently.

The original normals remain the displacement directions for every mode. Changing
shading changes no vertices, indices, bounds, silhouette or CPU collision. Smooth
DX12 tessellation evaluates four neighboring complete Height expressions and
rotates the selected normal/tangent by the displaced differential. Base derivatives
are analytical and only height offsets are subtracted, avoiding large-coordinate
cancellation. Color and material-coverage passes agree, including world-triplanar
sampling. Opaque geometry-only passes omit unnecessary normal work.

The central-difference interval is 1/512 in source-triangle barycentric coordinates.
Extremely high-frequency Height still needs sufficient geometric/sampling detail.
This feature does not bevel hard edges, stitch displaced seams, add adaptive
refinement, or enable hardware tessellation on DX11/Vulkan.

## Resources and consumer interface

The primary 108-byte vertex/cache ABI remains unchanged. Stream 1 TEXCOORD5 binds
a zero-copy alias of original normals for ordinary modes or an optional 12-byte
Smooth normal. Static Smooth data is derived only on demand, shared per geometry
LOD, uploaded through the existing byte/time budget and retained by existing GPU
leases. Pending data retains authored-normal shading until publication. Normal
generation is a one-time CPU request, not work repeated for each static draw.

Animated Smooth reconstructs normals from current deformed positions. One compute
thread processes each hard chart and writes all its members, using linear shared
CSR adjacency. Outputs reuse immutable-pose/deformation pooling. Memory accounting
includes normal outputs and source adjacency. Batch/population, shadow-content
and GI-history signatures distinguish the effective shading mode.

Consumers set `RenderModelInstance::meshShading`. Streamed consumers request the
optional stream through `queueModelLod(..., bool smoothNormals = false)` and
`modelLodReady(..., bool smoothNormals = false)`; existing calls remain valid.
Source normals, asset identities and material documents require no migration.
Material ABI/generator 32 adds `u_meshShading`, the separate vertex attribute,
native control-point shading normals and `cs_model_smooth_normals` in the coherent
embedded/hot-reload family. Deploy the rebuilt matching shader tools/cache family.
The twelve material texture slots, four material MRTs, 208 parameter rows, graph
schema 8, material file v6, bgfx `1.129.8940-496#14` and native interop ABI 1 remain.

## Verification

The Release integration build passed and prepared all seven project material
topologies without a shader failure. Twenty-one selected checks passed:

- `mesh_normals_core`, `scene_gameplay_core`, `scene_extraction_core`,
  `quader_scene_core`, `editor_render_frame_core`, `scene_component_ui_core`,
  `mesh_instance_update_core`.
- `shader_preflight_core`, `shader_compiler_core`, `material_graph_shader_core`,
  `material_graph_publication_core`, `project_material_cache_core`,
  `material_preview_resource_core`.
- `native_resource_core`, `forward_plus_render_core`, both DX12/DX11
  `mesh_instancing_render` and `material_mapping_render` checks, and both
  DX12/DX11 `material_height_render` checks.

The GPU oracle uses a two-triangle plane, R32F `u²` Height and factor 8, independent
camera-ray intersection and inverse-transpose analytic normals. Identity normal
RMS fell from 0.043121 (Flat) to 0.007475 (Smooth), over 2,325 interior probes.
With shear/nonuniform scale it fell from 0.056559 to 0.007267, over 2,256 probes.
These are RMS lengths of normal-vector differences, not angular degrees. Independent cube side
normals preserve hard boundaries. Ordinary custom normals, independent shared
asset occurrences, positive/negative GPU morphs, mode-invariant displaced
silhouettes, depth, entity/material picking and both shadow families also passed.

A first neutral-morph oracle compared whole images and found RGB RMS 1.072245
from two silhouette pixels. The 14,268 interior pixels and decoded center normals
matched exactly. The corrected oracle independently requires over 1,500 inset
pixels, interior RGB RMS ≤1, analytic center-normal error <0.015 and at most eight
foreground-mask differences. The negative morph matched the whole image exactly.

## GPU and CPU measurements

Hardware: NVIDIA GeForce RTX 4090 (`10de:2684`), driver `32.0.16.1074`, DX12,
1920×1080. Static source: 512 triangles, 289 vertices, 64×64 RGBA8 linear-data
Height, Amount 0.02, one directional and one point light. All tessellated modes
use factor 8. Each phase has 240 warmup frames and 600 unique coherent completed
GPU samples, after three seconds of common warmup. Forward/reverse phase order
controls warmup/order bias.

Final static view elapsed medians agreed in both phase directions:

| Mode | Ordinary opaque PBR | Factor-8 tessellated opaque PBR |
| --- | ---: | ---: |
| Use Mesh Normals | 0.054272 ms | 0.092160 ms |
| Flat | 0.054272 ms | 0.092160 ms |
| Smooth | 0.054272 ms | 0.083968 ms |

These timestamp durations exclude presentation, uploads and shadows. Smooth's
domain reconstruction also bypasses the old per-fragment displacement-plane
rotation. This workload does not establish that Smooth is universally faster or
that its extra Height evaluations are free; graph complexity, tessellation factor
and mesh/fragment density change the balance.

The static optional stream adds 3,468 bytes: residency grows from 34,284 to
37,752 bytes, with no duplicate allocation on repeated requests. First request
CPU time was 0.1212 ms. Across 100 warmed CPU normal-generation samples, median
was 0.0936 ms and p95 0.0987 ms. This is the 289-vertex request cost, not per-frame
CPU time or a large-mesh bound.

The animated stress slice uses 16 visible occurrences of one shared 128×128 grid,
each with a fresh independent position-morph pose per frame. There are 16,641
source vertices/32,768 source triangles and 266,256 total vertices/524,288 total
triangles. One directional light is enabled; tessellation, point lighting,
procedural updates, shadows, GI and AO are disabled. Use Mesh Normals/Flat dispatch
16 deformations; Smooth dispatches those plus 16 normal reconstructions. All six
phases collected 600 positive compute/opaque timestamps from matching completed
GPU frames, with the same warmups and forward/reverse mode order.

| Mode | Forward compute | Reverse compute | Forward compute + opaque | Reverse compute + opaque |
| --- | ---: | ---: | ---: | ---: |
| Use Mesh Normals | 0.386048 ms | 0.252928 ms | 0.657408 ms | 0.446464 ms |
| Flat | 0.356352 ms | 0.251904 ms | 0.627712 ms | 0.446464 ms |
| Smooth | 0.506880 ms | 0.371712 ms | 0.759808 ms | 0.555008 ms |

Relative to Use Mesh Normals in the corresponding phase direction, Smooth adds
0.120832/0.118784 ms to the compute view and 0.102400/0.108544 ms to the per-frame
sum of compute and opaque view durations. These are observed median differences,
not a confidence interval or an isolated kernel measurement. The latter includes
the changed barrier placement between deformation, normals and rendering.

Absolute times drifted between the two directions. Complete GPU-frame medians
(including submission gaps/presentation) ranged from 1.549056 to 1.744704 ms;
their variation is too large to infer a reliable whole-editor FPS change. The
raw report retains all per-frame and view durations, dispatch counts and p95s.
The much smaller initial one-occurrence run had submission-gap-dominated compute
times; it is retained under `mesh-shading-balanced` and is not used for a
reconstruction speedup claim.

The installed DX12 timer was audited: it spans both dispatches within one compute
view. It closes the view query after the view-transition command-list kick, so
its elapsed interval can include queue/submission gaps. Neither these view times
nor the complete-frame times represent isolated active GPU execution.

Animated adjacency occupies 986,132 bytes once per shared source. Each live
16,641-vertex Smooth pose output adds 199,692 bytes; multiple in-flight pooled
pose generations retain their own outputs. The existing deformation/geometry
buffers are additional. Static occurrences share their one optional normal
stream rather than duplicating it for each instance.

Final local artifacts, produced by the Release fixture:

- `build/Release/diagnostics/mesh-shading-final/mesh-shading-benchmark.json`:
  7,200 static and 3,600 animated complete samples.
- `build/Release/diagnostics/mesh-shading-final/mesh-shading-correctness.json`,
  analytic-normal/neutral-morph/hard-cube captures in the same directory.
- `build/mesh-shading-core-tests.log`, `build/mesh-shading-render-tests.log`,
  `build/mesh-shading-height-final.log`, `build/mesh-shading-height-tests.log`
  (the latter includes the earlier DX12 oracle failure and passing DX11 check).
- `build/mesh-shading-build.log`, `build/mesh-shading-benchmark-build.log`,
  `build/mesh-shading-benchmark-final.log`.

Raw final report SHA256: `ecd6902fb5cfa356005c0d770b92c4593bd14ceccb0c1ae896849aa57d1195ed`.
Fixture source SHA256: `870fb3f02703b714fd2b0cd823726c215e0409f8ae59c541272b333ba3340b1c`.

To reproduce the complete correctness and performance fixture after building:

```powershell
& './build/Release/bin/material_height_render_tests.exe' './build/vcpkg_installed/x64-windows/tools/bgfx/shaderc.exe' './third_party/crimson-renderer/src/shaders' './build/vcpkg_installed/x64-windows/include/bgfx' --benchmark './build/Release/diagnostics/mesh-shading-final' --warmup 240 --samples 600
```
