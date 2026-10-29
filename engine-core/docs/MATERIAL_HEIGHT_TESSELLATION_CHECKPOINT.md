# Material Height and DX12 tessellation checkpoint

Implemented on `codex/material-height-tessellation` in Engine and the shared
renderer. The renderer revision is
`4a5338644f20b0cf86807edc4192fa7058a19273`; material ABI/generator is 31,
graph schema is 8 inside native material v6, and bgfx remains
`1.129.8940-496#14` with native interop ABI 1.

## Material workflow

Select Output in the existing Material Editor, enable Material → Tessellation,
set the height Texture 2D node's Sample Type to Linear Data, and connect R to
Height. Amount defaults to 0.1 mesh-local units, Midpoint to 0.5, uniform Factor
to 8 and Advanced → Height Texture LOD to 0. Height is clamped to [0,1].
Disabling preserves the authored branch and restores ordinary geometry.

The implementation changes mesh geometry in Scene, Game, mesh previews, depth,
point/directional shadows and entity/material-slot picking. It preserves smooth
neutral shading, conservative affine bounds and ordinary material sample roles.
Original LOD0 is requested, with resident fallback while geometry streams.
DX11 and particles retain ordinary geometry. CPU collision stays unchanged.

Uniform subdivision, per-occurrence generated graph draws and unstitched height
seams are current limits. Adaptive tessellation, PN smoothing and instanced
tessellated draws are not implemented. Matching UVs/normals/height values at a
shared boundary remain an asset responsibility.

The shared [implementation and reference findings](../third_party/crimson-renderer/docs/MATERIAL_HEIGHT_DISPLACEMENT.md)
record Godot parallax versus Unreal material displacement, compilation and
cross-pass contracts. [Shader startup](MATERIAL_SHADER_STARTUP.md) documents
the optional native domain stage and coherent cache publication.

## Verification

Release integration build and project cache preparation passed. Fourteen focused
checks passed: material graph, document, editor, generated shaders, publication,
compiler, project cache, preflight, editor render frame, DX12/DX11 material
height, DX12/DX11 material mapping and Forward+ rendering.

The native height fixture verifies independent perspective silhouettes, signed
parameter edits, depth/selection/material-slot picking, two-triangle varying
UV0/UV1 relief, single-sided culling, preview displacement, neutral smooth
lighting under nonuniform scale, and point/directional shadow geometry against
exact translated references. It does not measure persistent shadow-cache
invalidation. CPU preparation checks domain cache repair and authored-file
preservation. The final inactive-material uniform-submission optimization was
followed by a repeated DX11 fixture and the full DX12 fixture within both timing
runs. Final code changes were limited to that submission guard; remaining
measurement edits affect only the fixture/report.

## GPU measurement — 2026-10-20

Owned DX12 on NVIDIA GeForce RTX 4090, vendor `0x10de`, device `0x2684`, driver
`32.0.16.1074`, Windows x64 Release. Resolution 1920×1080; one 512-base-triangle
plane with a PBR material, one directional light and one point light. Enabled
height uses a 64×64 linear RGBA8 texture (16 KiB), one bilinear LOD-0 fetch per
domain invocation, and Amount 0.02 to keep screen coverage comparable.

The report measures the Game opaque PBR view's GPU timestamp elapsed time.
It excludes presentation, uploads and shadow passes; it is not active-execution
time or an editor FPS measurement. Additional depth and shadow views can repeat
the tessellation work in actual scenes. Results do not predict other hardware,
base mesh density, material complexity or scene populations.

A three-second common warmup precedes forward/reverse phases:
Off, 4, 8, 16, 16, 8, 4, Off. Each phase warms for 240 frames and retains 600
unique, completed, frame-coherent GPU samples. Medians agreed between directions.

| Factor | Median GPU ms | Worst phase p95 ms | Added median vs Off |
| --- | ---: | ---: | ---: |
| Off | 0.053248 | 0.054272 | — |
| 4 | 0.075776 | 0.077824 | 0.022528 |
| 8 (default) | 0.090112 | 0.092160 | 0.036864 |
| 16 | 0.114688 | 0.116736 | 0.061440 |

The initial single-direction run had a cold Off median of 0.151552 ms while
later tessellated phases were already warm. It is retained as diagnostic data
and is not used as evidence of a tessellation speedup. The repeated baseline
after common warmup resolves that phase-order bias.

Local raw results/captures are under
`build/Release/diagnostics/material-height-balanced/`, including
`material-height-benchmark.json` with 4,800 sample records. Build and test logs
are `build/material-height-{corrected-build,final-tests,measurement-build,final-dx11,balanced-build,balanced}.log`.

To reproduce after a Release build, pass absolute shader/compiler paths:

```powershell
& './build/Release/bin/material_height_render_tests.exe' `
  (Resolve-Path './build/vcpkg_installed/x64-windows/tools/bgfx/shaderc.exe') `
  (Resolve-Path './third_party/crimson-renderer/src/shaders') `
  (Resolve-Path './build/vcpkg_installed/x64-windows/include/bgfx') `
  --benchmark './build/Release/diagnostics/material-height-balanced' `
  --warmup 240 --samples 600
```
