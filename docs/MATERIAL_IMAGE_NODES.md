# Image and channel nodes

The material graph adds Levels, Invert, Blend, Grayscale, To Color, Noise,
Brightness / Contrast, Sharpen, Height to Normal, RGBA Merge, RGBA Split,
Alpha Split and Alpha Merge. They are ordinary editable nodes, available from
the graph add/search menu and compatible typed-pin creation. Existing links,
node/pin IDs, document history and the default ImGui style are retained.

Current graph schema10 additionally provides [Resize](MATERIAL_RESIZE.md) for a cached intermediate image with explicit pixel dimensions. Pin colors represent resolved types. New RGB/RGBA-to-fixed-Scalar wires insert an explicit Red conversion; older implicit conversions display an R badge and remain unchanged.

## Artist workflow

Connect a Texture 2D or Noise source through an adjustment, then to a material
channel. Edit disconnected input defaults inline or in Properties; connecting
an input retains its default for later disconnection. A gesture is one undo
command and Escape cancels it. Right-click an output to preview that specific
output; selecting another node does not retarget the preview.

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
identities. The current schema10 writer adds Resize; readers retain schemas 1–10 and supply defaults without rewriting
loaded files. Native material files remain v6, generated material ABI remains 32,
and the existing twelve-sampler and uniform budgets remain in force.

Reusable graph/evaluation/shader code belongs to the canonical renderer;
Engine owns document persistence adapters, editor controls and transactions.
New shader helper text is conditional on the operations used, so unrelated
older graphs retain their established generated-source behavior. Numeric edits
remain parameter updates when topology is unchanged.

Focused checks cover mathematical endpoints, typing, alpha-zero RGB, all four
simultaneous channels, preview selection, serialization/history, shader generation
and the domain branch.

The 2026-10-20 integration built the usual Release executable through
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
