# Image and channel nodes

The material graph adds Levels, Invert, Blend, Grayscale, To Color, Noise,
Brightness / Contrast, Sharpen, Height to Normal, RGBA Merge, RGBA Split,
Alpha Split and Alpha Merge. They are ordinary editable nodes, available from
the graph add/search menu and compatible typed-pin creation. Existing links,
node/pin IDs, document history and the default ImGui style are retained.

Current graph schema 14 retains [Resize](MATERIAL_RESIZE.md) and the reusable image operations below, and appends Pixelization, Posterization and Dither without renumbering existing nodes. Readers accept schemas 1–14 within native material v6. Empty adaptable image inputs show grayscale/color capability; connected inputs, outputs and wires show their resolved types. Compatible endpoints connect directly, including RGB/RGBA-to-fixed-Scalar links, without automatic conversion nodes. Scalar receivers retain the existing first-channel rule and its R badge.

## Artist workflow

Connect a Texture 2D or Noise source through an adjustment, then to a material
channel. Edit disconnected input defaults inline or in Properties; connecting
an input retains its default for later disconnection. A gesture is one undo
command and Escape cancels it. Double left-click a node to start or stop its
preview, or right-click an output to preview that specific output. Node preview
defaults to the selected 3D mesh. Use **2D | 3D** above the preview to view the
same output flat or on the mesh; returning to 3D restores the chosen mesh and
camera. The mode stays selected while switching nodes within the document.
Selecting another node does not retarget the preview. The add/search popup keeps its
search field visible above a scrolling result list; Up/Down brings the selected
result into view.

Supported static image outputs show 128 px inline images and, in 2D mode, a
256 px sidebar image for the preview target. Evaluation uses the authored source resolution,
then area-reduces the completed output for display. It does not reevaluate a
1K/2K/4K procedural source on a coarse preview lattice. This preserves thin-mark
coverage and nonlinear adjustments such as Levels. Cached image Value outputs
reuse the renderer's generated source; other supported point branches retain
source-resolution CPU evaluation. Inline requests are limited to visible nodes;
edits coalesce behind one active display job per target, allowing completed
intermediate images to appear during a drag. The latest value follows without
waiting for the gesture to stop; the last complete image remains visible while
its replacement prepares. Layout, selection and warm frames do not regenerate images.

Flat **Scalar** previews display numerical data directly: 0.25, 0.5 and 0.75
appear at those grayscale display values, without an sRGB brightness lift.
This applies to procedural Value outputs, masks, height and other static scalar
branches. Vector2 likewise displays its numerical XY channels. RGB/RGBA previews
retain the single linear-to-sRGB color conversion and straight-alpha checker
composition. Choosing a different output can therefore change its display
interpretation. These are preview-only transforms; source pixels, Height,
Roughness and saved materials are unaffected. Contextual/animated outputs use
the node shader on the selected mesh in 3D or a fixed plane in 2D.

Packed channel maps should use Texture 2D **Sample Type: Linear Data**. Color
textures decode RGB for linear material calculations; alpha remains linear.
Do not interpret a packed roughness or metallic map as sRGB color.

| Node | Input/output contract | Primary controls |
| --- | --- | --- |
| Levels | Scalar, RGB or RGBA; same resolved output family, alpha preserved | Input Black/White, Gamma, Output Black/White |
| Invert | `1-RGB`; same resolved family, alpha preserved | Source/default |
| Blend | Base, Top and scalar Mask; scalar/RGB/RGBA resolution | Mode, Opacity, disconnected inputs |
| Grayscale | Explicit color/data channel conversion to one scalar | Conversion method |
| To Color | Broadcast a scalar into RGB; RGBA output is opaque | Scalar source/default |
| Noise | Original analytic Clouds/Spots, optional Mapping | Pattern, Seed, Feature size, Contrast; Clouds Detail amount |
| Brightness / Contrast | Signed adjustments neutral at zero; alpha preserved | Brightness, Contrast |
| Sharpen | Cardinal Laplacian; Amount zero is identity, alpha preserved | Amount; Advanced Detail size |
| Height to Normal | Scalar height to encoded RGB and typed Normal outputs | Strength, Flip Y; Advanced Detail size |
| RGBA Merge | Four scalar R/G/B/A inputs to RGBA | Disconnected channel defaults |
| RGBA Split | One RGB/RGBA input to four scalar R/G/B/A outputs | Source/default; choose an output to preview |
| Alpha Split | One RGB/RGBA input to opaque RGB and scalar A | Source/default |
| Alpha Merge | RGB/RGBA plus scalar A to RGBA; replaces alpha | Disconnected defaults |
| Blur | Scalar/RGB/RGBA to the same family, including independent alpha | Amount; zero is identity |
| Grow / Shrink | Scalar mask to scalar; square max/min envelope with continuous fractional support | Signed Amount; positive expands white |
| Mask Adjust | Scalar mask; input clamp, coverage shift, contrast gain, final clamp | Coverage, Contrast |
| Histogram Scan | Per-channel image selection; same family, alpha retained | Coverage, Contrast |
| Directional Warp | Source image plus scalar Intensity; periodic bilinear source sampling | Amount, Angle |
| Image Transform | Completed cached image sampled with existing Mapping | Mapping, linked Scale, Rotation, Offset; optional UV link |
| Procedural Texture | Original repeatable 2D sources to scalar | Pattern, Resolution and active family controls |
| Height Occlusion | Scalar height to scalar visibility; white is open | Strength, Radius, Depth |
| Normal Occlusion | Encoded linear RGB normal image to scalar visibility | Strength, Radius, Depth, Flip Y |
| Direction Select | Normalized encoded RGB direction to scalar selection | Signed Axis, Both directions |
| Image Input | Optional managed image override, Base image and constant fallback | Enabled, Image, Color/Linear Data, Constant |
| Normal Map | Processed encoded linear RGB to typed surface Normal | Strength, Flip Y |
| Pixelization | Completed static image to square source-pixel blocks; same Scalar/RGB/RGBA family | Pixel Size (1–128, default 4) |
| Posterization | Perceptual color quantization; same Scalar/RGB/RGBA family, alpha preserved | Levels (2–256, default 8) |
| Dither | Stable threshold pattern on the source image lattice; same family, alpha preserved | Pattern, Size (1–16), Strength (0–1), Levels (2–256, default 17) |

## Material stylization

Pixelization, Posterization and Dither process the branch connected to their Input. Connect their Result to Base Color, Emission or another material channel. Their settings belong to that material and are independent of the Environment's post-processing controls. Material effects run before lighting and tone mapping; the Environment effects process the final view, so enabling both combines their results.

Pixelization evaluates the completed source image and repeats the sample at the center of each square block. Pixel Size counts source-image pixels, not viewport pixels; size 1 preserves the image. Partial blocks at the right and bottom edges use their actual centers. The baked image retains its source dimensions and uses nearest sampling to preserve block boundaries when Pixel Size exceeds 1. Size 1 uses the ordinary linear cached-image sampler. Pixelization resamples all channels, including alpha.

Posterization clamps input RGB to 0–1, converts it to perceptual sRGB levels, rounds each channel to the nearest of the authored Levels, and converts the result back to linear material values. It preserves RGBA alpha. This uses the same level equation as post-processing, without introducing another display encoding. Uniform inputs remain uniform; textured or animated inputs follow the ordinary shader expression path.

Dither offers Bayer 2×2, Bayer 4×4, Bayer 8×8 and Interleaved Gradient Noise, matching the post-process patterns. Size counts source pixels per threshold cell. Strength scales centered noise by `1 / (Levels - 1)` in perceptual RGB, then returns linear values; Dither does not quantize by itself. The default 17 levels gives the standalone post-process interval of 1/16. For `Dither → Posterization`, set both Levels to the same value. Zero Strength preserves every input value, including HDR and alpha. The pattern remains stable between frames. With positive Strength, a uniform or 1×1 source uses a 1024×1024 dither lattice so a solid-color material still has a visible pattern; larger source images retain their own dimensions.

Pixelization and Dither use the existing cached static UV0 image pipeline and its source-resolution evaluation, float precision, cancellation and last-good publication rules. Runtime Time, moving source mappings, Particle Color, UV1/triplanar sources and decoded surface normals cannot be placed upstream of these image boundaries. Use Image Transform after processing to apply the completed image with a runtime mapping. Posterization has no static-image restriction.

## Cached image operations

Blur, Grow / Shrink, Directional Warp and the two Occlusion nodes materialize supported static UV0 image branches before rendering. They include upstream Levels, Blend and other adjustments in the generated result. Image Transform materializes its complete input before applying its runtime Mapping. Linking a UV/Panner node afterward can select UV1 or local/world triplanar projection; changing only that mapping reuses the cached image. Time, moving source mappings, Particle Color and decoded surface normals upstream of a cache boundary are rejected with a diagnostic. Source images and settings remain authored; generated textures are disposable resource data.

Blur support is Amount multiplied by each image axis, with sigma equal to one third of that support. Small kernels use finite Gaussian convolution; broad kernels use five variance-matched box passes to bound execution cost. This approximation has independent impulse/normalization checks and is not claimed to be an exact large Gaussian. Grow / Shrink uses periodic square morphology and blends adjacent integer supports; it is not a disk or distance-field operation. Both preserve repeat edges and process without 8-bit intermediate quantization.

Height Occlusion and Normal Occlusion are image-space local horizon approximations, independent of mesh baking and scene SSAO. Flat surfaces are open; a tilted height plane is open away from genuine periodic seam cliffs. Normal Occlusion integrates local normal-derived slopes rather than reconstructing a globally consistent displacement field. These maps do not replace mesh visibility or recover physical scale.

Image Input uses an assigned override while Enabled, otherwise a connected Base, otherwise its Constant. Disabled uses only Constant and schedules no override/Base work. Its saved references and dormant Base wire remain available. Color images decode sRGB once; data/encoded-normal images use Linear Data. Normal Map belongs after image processing and converts the encoded image into the strict Normal type. It follows a common source mapping through UV0/UV1, rotated/mirrored coordinates and local/world triplanar projections. Branches with incompatible source mappings require a common Resize/Image Transform boundary before decoding; a precise diagnostic retains the last usable material. Height to Normal remains a separate height-gradient operation.

Procedural Texture families are Clouds 1, Clouds 2, Multiscale Spots, Fine Dirt, Moisture, Creased, Messy Fibers, White Noise, Gaussian Noise, Parabolic Gradient, Gaussian Streaks and Directional Scratches. Selecting a family applies its own defaults and retains source resolution. Controls appear only where they affect that family. Multiscale Spots Detail balance changes spectral contribution, Moisture Contribution varies about neutral gray, and Messy Fibers Threads changes the threads inside each shape. Gaussian Noise describes scattered Gaussian shapes. Directional Scratches uses analytic subpixel coverage, including rectangular-image cell correction. The rejected legacy Scuff family is not available. Scale changes structures; Resolution changes actual raster size. Enlarging an already rasterized source does not restore missing detail.

**Variation** is the continuous arrangement control stored as `disorder` in the
material. Cyclic families such as Clouds 1 have the same arrangement at 0 and 1;
intermediate values morph it, while Seed chooses a different distribution.
The original source equations and phase behavior are retained.

Clouds 1 and Clouds 2 expose **Scale** as feature size: 1 retains the default,
2 makes larger structures, and 4 larger again. The range is 1/32–32. Integer
periodic lattices and continuous blends preserve tiling; coarse bands clamp
at one cell. Other procedural sources expose **Density** for their frequency
control (Parabolic Gradient uses **Tiling**): higher values add smaller, more
numerous details. Detail below the
authored raster's resolution is filtered toward its mean.

Graph schema 12 converts earlier Clouds frequency values to reciprocal Scale
in memory, preserving existing authored materials. Saving emits the current
representation; merely loading does not rewrite or dirty assets. Other pattern
settings, native material v6, shader generator v33 and uniform ABI 32 retain
their existing contracts. `material_procedural::algorithm_revision = 2`
invalidates generated sources and their display images.

Resolution **Custom** opens Width (px) and Height (px), each in 1–4096. Merely
opening those controls retains the current resolution and document history.
**Keep square tiles** applies to rectangular Directional Scratches sources and
is inactive on square images. The [property audit](MATERIAL_NODE_PROPERTY_AUDIT.md)
records the control matrix, corrected semantics and intentional dependencies.

CPU generation prepares immutable stamp geometry and compiles each family's
recipe once, then evaluates cell-oriented batches over bounded row tiles.
This sequential path retains the original cell accumulation order and uses no
additional worker threads. The authored 1K/2K/4K lattice remains intact; prepared
storage and batch scratch both count toward the image job's memory allowance.

Generated results use cancellable immutable resource jobs, source/content keys and budgeted streaming. Warm frames do not reevaluate source images. Superseded requests are canceled and the prior complete material remains available until replacement. Temporary image/scratch storage is capped at 512 MiB per bake, excluding immutable caller-owned source bitmaps, small planning/queue structures, retained host generations and GPU allocations. RGBA16F GPU images use the normal residency budget; a full 4K mip chain costs approximately 170.67 MiB. Nested private image operations consume only their outer runtime sampler; the twelve-slot material limit remains.

Release measurements on 2026-10-20 used a Ryzen 9 7900X system with an RTX 4090,
with the editor closed and no overlapping build. These are sequential CPU times
for default-pattern preparation and scalar source generation, in seconds;
they exclude host RGBA conversion, mip generation and GPU upload.

| Pattern | 1K | 2K | 4K |
| --- | ---: | ---: | ---: |
| Clouds 1 | 0.194 | 0.690 | 2.724 |
| Clouds 2 | 0.275 | 1.026 | 4.069 |
| Fine Dirt | 0.030 | 0.097 | 0.373 |
| Directional Scratches | 1.350 | 4.543 | 16.579 |

Heavy sources remain a limitation: a first 4K Directional Scratches generation
takes about 16.6 seconds on this machine, and an exact preview needs the
completed source. Reusing that result avoids duplicate source generation.
Total material publication also includes host conversion, mips and upload;
per-frame GPU cost and FPS were not measured by this CPU benchmark.

The 2026-10-21 [property audit](MATERIAL_NODE_PROPERTY_AUDIT.md#validation-and-measured-cost)
repeated these measurements after the Scale and fractional Blur corrections.
Default Clouds 1/2 at 4K measured 2.630/3.897 seconds. Broad RGBA Blur measured
1.861 seconds at 2K and 7.225 seconds at 4K, approximately 7–9% above the prior
run, with the same 128/512 MiB temporary-image allocation. Directional Scratches
at 4K remained expensive at 17.39 seconds. The audit records comparison scope
and the final 15 passing focused checks.

The separate warm DX12 Normal-decoding fixture measured 48 unique completed
GPU view samples per phase, in forward and reverse variant order on the same
RTX 4090. At 1024², one quad and a 2×2 point-sampled linear RGBA8 normal source,
the opaque Normal diagnostic view measured these median elapsed GPU times:

| Mapping | Texture Normal | Data → Levels → Normal Map | Completed Image Transform → Normal Map |
| --- | ---: | ---: | ---: |
| UV0 | 0.093–0.094 ms | 0.098 ms | 0.097 ms |
| World triplanar | 0.097 ms | 0.113 ms | 0.109–0.110 ms |

This isolates the warm mapping/decoding path: it excludes cold generation,
upload, lighting, GI, AO and Glow. It is neither an FPS-loss estimate nor a
4K RGBA16F bandwidth benchmark. Reproduce with
`CRIMSON_TEST_NORMAL_TIMING_OUTPUT=<absolute report.json>` and the focused
`material_mapping_render_core` check; profiling is enabled only in that
optional measurement block. The source-generation and image-kernel reports
are emitted by `material_procedural_tests --benchmark <report.json>` and
`material_atomic_image_bake_tests --benchmark <report.json>`.

Higher Noise Feature size makes larger structures in the selected coordinate
domain. Clouds Detail amount adds smaller-scale variation and is unavailable
for Spots. Noise is analytic and consumes no external image or material sampler.

Blend supports Mix, Multiply, Add, Subtract, Screen, Overlay, Minimum, Maximum,
Difference, Add/Sub and Soft Light. Its opacity/mask weight operates within the
selected blend equation. The image blend clamps the computed RGB operation to
0–1, while alpha interpolates between the two sources. It does not implicitly
perform premultiplied source-over compositing. Brightness/Contrast and Levels
also deliberately process bounded image values rather than replacing Crimson's
existing unrestricted HDR arithmetic or Contrast nodes.

## Straight RGBA and explicit material coverage

RGBA Merge defaults to black RGB and alpha one. RGBA Split exposes independently
connectable channels. Alpha Split removes alpha from its RGB output and exposes
it separately; Alpha Merge replaces alpha without multiplying RGB by it.
Nonzero RGB beneath alpha zero is retained. Split followed by the corresponding
Merge therefore reconstructs the source channels without a premultiplication
or divide-by-zero step.

Constant, Texture 2D and Particle Color gain an explicit RGBA output alongside
their existing RGB/channel outputs. Color4 is a distinct graph value type.
Receiving RGB inputs drop alpha; promotion of RGB to RGBA supplies alpha one.
Ordinary scalar receiving conversions continue to take R. Mapping and Normal
remain strict types. Finite channel values remain available for data/HDR use;
coverage interpretation belongs at the material's explicit Opacity or Opacity
Mask connection. Connecting RGBA to Base Color alone does not enable blending
or automatically use its alpha as coverage.

## Spatial sampling and native Height

Sharpen and Height to Normal reevaluate their complete source branch at each
sample, including upstream adjustments and mappings. Detail size is a unitless
artist control: higher values sample a broader region. The internal reference
support is a UV-domain value; it is not displayed as a UV fraction or a texel
unit. Height to Normal changes shading normals; it does not enable geometry
subdivision, material Height or tessellation.

The current surface's UV0 parameterization defines the filter metric. Fragment
sampling derives local/world position and UV1 offsets from the surface
Jacobians. Native tessellation-domain expressions derive the corresponding
metrics from the patch and its current UV interpolation. They use explicit
texture LOD and do not introduce screen derivatives into a domain shader.
The existing height-gradient stabilization remains intact. Sampling support
cannot reconstruct geometric frequencies beyond tessellated vertex spacing;
this change does not claim to solve that separate displacement limitation.

## Storage, ownership and validation

Graph schema 9 introduced these kinds, Color4 and RGBA pin without renumbering old
identities. The current schema 12 writer retains Resize and adds the reusable image operations; readers retain schemas 1–12 and supply defaults without rewriting
loaded files. Native material files remain v6, generated material ABI remains 32,
and the existing twelve-sampler and uniform budgets remain in force.

Reusable graph/evaluation/shader code belongs to the canonical renderer;
Engine owns document persistence adapters, editor controls and transactions.
New shader helper text is conditional on the operations used, so unrelated
older graphs retain their established generated-source behavior. Numeric edits
remain parameter updates when topology is unchanged.

Canonical hosts use `material_procedural::PreparedPattern::render()` for a full
scalar raster, reserving `memory_bytes()` and `render_scratch_bytes()` beside
the output. Pass cancellation to preparation and rendering. Include
`material_procedural::algorithm_revision` (currently 2) in generated-image keys.
`MaterialGraphImageBake.h` exposes `validateMaterialGraphImagePreview`,
`materialGraphImagePreviewSources` and `bakeMaterialGraphImagePreview`; the
preview options select display extent and preparation allowance, leaving
authored graph values unchanged. Returned floats remain linear/data with
independent alpha; display encoding and checker composition belong to the host.

Focused checks cover mathematical endpoints, typing, alpha-zero RGB, all four
simultaneous channels, preview selection, serialization/history, shader generation
and the domain branch.

The prior image/channel slice on 2026-10-20 built the usual Release executable through
`Build.cmd Release`. All eight focused checks passed: `material_graph_core`,
`material_graph_actions_core`, `material_graph_editor_core`,
`material_graph_publication_core`, `material_graph_shader_core`,
`material_document_core`, `project_material_cache_core` and
`shader_preflight_core`. Shader validation compiles the new operations and
outputs for SM5, including native tessellation-domain expressions. A final
shader rerun passed after the exact Blend endpoint correction. Existing
project-cache preparation retained 8/8 topologies and 36 cached stages without
invoking a compiler. These checks cover graph math, generated shaders,
persistence and headless editor gestures; they are not a claim of visual
acceptance in a manually operated Engine viewport.

## Reference boundary

Adobe's public [RGBA Merge](https://experienceleague.adobe.com/en/docs/substance-3d-designer/using/substance-graphs/nodes-reference-for-substance-graphs/node-library/filters/channels/rgba-merge),
[RGBA Split](https://experienceleague.adobe.com/en/docs/substance-3d-designer/using/substance-graphs/nodes-reference-for-substance-graphs/node-library/filters/channels/rgba-split),
[Alpha Merge](https://experienceleague.adobe.com/en/docs/substance-3d-designer/using/substance-graphs/nodes-reference-for-substance-graphs/node-library/filters/channels/alpha-merge)
and [Alpha Split](https://experienceleague.adobe.com/en/docs/substance-3d-designer/using/substance-graphs/nodes-reference-for-substance-graphs/node-library/filters/channels/alpha-split)
descriptions establish the packing/unpacking responsibilities. Installed
Designer 13.1.1 package metadata independently confirms four scalar split
outputs. These behavioral references do not supply implementation code.
Algorithms are original Texflow/Crimson work; no Adobe graph, shader, image,
decompiler output or runtime is shipped.
