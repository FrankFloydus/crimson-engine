<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Guided UV Peel ownership

`uv_peel.cpp` owns guide ordering, target validation, storage reconstruction and publication.
Structured quad coordinates come from `uv_peel_structured.cpp`; non-quad-only guides retain
the conformal extension in `uv_peel_solver.cpp`. A guide with both quad and non-quad incident
faces still uses structured evaluation, including the lengths of segments without quad stores.

Structured walks follow the authored quad connectivity. On a connected stair body,
wall quads can close a transverse walk while nearby wall n-gons stop it. These
different spans can assign incompatible UVs to neighboring walking faces. The
generator's optional [walking surface separation](stairs.md#optional-walking-surface-separation)
keeps the connected tread-and-riser strip, ramp decks and landing tops outside
those body walks by detaching their shared boundary within one mesh object.
It does not change Peel's
existing closed-tube seam behavior or add an implicit face-angle boundary.

`UvPeelOptions::smooth_circular_flow` defaults to false. When enabled,
`uv_peel_circular.cpp` groups connected coplanar convex quads and fits the selected
guide projected into their plane to a circle. A conditioned fit, bounded radial
residual, branch-safe domain and compatible shared radius/angle UV field are
required. Unsupported domains retain ordinary Peel. Current acceptance keeps a
whole domain within one open half-plane around the circle center; wide or closed
coplanar domains conservatively retain ordinary Peel. Separate flat stair treads
can share the projected guide circle while retaining their own longitudinal phase.

Accepted domains receive corners from one analytic field before inverse sampling
and float storage. A face's optional `CircularUvInterpolation` retains its center,
two local covectors and base-space radius/angle coefficients. Sampling adds the
exact polar value minus its interpolated corner value, preserving corner samples
without baking a texture or adding geometry. Compilation applies the face texture
rotation and scale to one sparse descriptor per face; vertices carry one-based
indices into that table. Ordinary Peel removes the descriptor on affected faces.
Geometry or base-UV edits clear stale descriptors only on touched faces, while
face texture rotation/scale and material edits retain them. Prepared transforms
keep the usual sparse coordinate path for unannotated documents.

QGEO minor 4 appends sparse face-indexed descriptors. Earlier minors have no
circular field. Persistence retains raw records independently of modeling
validation; safe compilation excludes invalid descriptors. This representation
is renderer-neutral and does not turn ordinary exported corner UVs into a
higher-order map without a consumer that evaluates the retained field.

## Smooth Circular Flow: decision and implementation

The motivating case was `Stairs_2` in the local `diablo_room` scene. Adjacent
angular panels had almost identical shared UV values but a visible change in
texture direction. Moving corner UVs or removing an internal triangle diagonal
does not by itself supply one smooth sampling field across neighboring quads.
The earlier stairs topology and automatic wall mapping remain owned by
[the stairs generator](stairs.md); this change adds no geometry.

The Full editor exposes **Smooth Circular Flow** immediately after **World
Density** in UV Peel. It defaults to off. Select the existing flight guide,
activate UV Peel, enable the option, inspect the preview and Apply. Turning it
off and applying the same guide restores ordinary Peel for the affected faces.
Cancel restores the source mesh; Apply, repeat, undo, redo and scene save/open
retain the accepted mapping through the existing owners.

The production map is Quader's independently authored circular specialization,
using the standard circle identity `|p - c|^2 = R^2`, `hypot` and `atan2`.
It is not an implementation of MVC, ARAP, higher-order finite elements or the
stripe-pattern solver listed below. For a projected point `p = (x, y)`, let
`G(p) = (hypot(x,y), atan2(y,x))`, let `L` be the renderer's perspective-correct
triangle interpolation, and let `C` be the stored two-by-two UV coefficient
matrix. CPU sampling and the mesh shader evaluate the same correction:

```text
UV = L(UV_corner) + C * (G(L(p_corner)) - L(G(p_corner)))
```

The residual is zero at the corners and replaces the nonlinear part of the
interpolation while retaining affine sampling corrections. All accepted quads
in one flat domain use a shared circular field, rather than independently
smoothing each quad. Circular mode replaces the ordinary inverse-bilinear
correction for those faces. Normal mapping differentiates the final corrected
UVs. Static, dynamic and retained preview geometry use the same packed data.

Acceptance is deliberately limited: the projected guide must support a stable
circle fit, the quad domain must be planar and convex per face, its stored UVs
must agree with the shared radius/angle field, and the complete domain must fit
within one open half-plane about the center. Wide or closed flat domains,
unsupported nonplanar ramps and incompatible transformed metrics fall back to
ordinary Peel. This is not a general nonlinear unwrap or an injectivity proof.
Geometry/base-UV edits clear affected fields; run Peel again after reshaping.
Ordinary corner-UV exports need a consumer of the retained field to reproduce
the smooth sampling, or a separately implemented approximation/bake.

The native bridge's separately negotiated
[UV interpolation extension](../../../../io/bridge/README.md#optional-uv-interpolation-extension)
exports prepared per-vertex coordinates, coefficients and an explicit mode for
ordinary inverse-bilinear quads or circular fields, without changing its core ABI
or adding geometry. An importing engine must preserve these attributes through
its model storage and evaluate the selected correction before the shared material
transform. Exporting only the existing position/normal/corner-UV records loses
both kinds of interior correction and can restore the zigzag pattern even when
all corner UVs are correct. Updating the scene decoder alone does not implement
this consumer behavior.

## Research references and their actual use

Research and comparison were performed on 2026-10-18. The distinction between
methods studied and code delivered is intentional:

| Reference | Consultation and Quader decision |
| --- | --- |
| Kai Hormann and Michael S. Floater, **Mean Value Coordinates for Arbitrary Planar Polygons**, ACM Transactions on Graphics 25(4), 1424-1441 (2006). [Author-hosted paper](https://www.inf.usi.ch/hormann/papers/Hormann.2006.MVC.pdf). | Mathematical source for the independently written, test-only signed MVC half-angle formula. A whole-tread comparison removed internal interpolation joins but displaced the sampled guide by about 0.04455 texture tiles and flattened the desired circular rows. These are observations on this saved fixture, not a general failure claim about MVC. MVC is not used in production. |
| Ligang Liu, Lei Zhang, Yin Xu, Craig Gotsman and Steven J. Gortler, **A Local/Global Approach to Mesh Parameterization** (2008). [Author-hosted paper](https://cs.harvard.edu/~sjg/papers/arap.pdf). | Primary PDF read for local/global ARAP and its triangle-based parameterization. Compared as an alternative corner-coordinate optimizer. Quader's assessment was that replacing the corner optimizer alone would leave the runtime interpolation limitation; its solver was not implemented. |
| Manish Mandad and Marcel Campen, **Efficient Piecewise Higher-Order Parametrization of Discrete Surfaces with Local and Global Injectivity** (2020). [Publication](https://doi.org/10.1016/j.cad.2020.102862), [author preprint](https://homepages.uni-paderborn.de/campen/papers/Efficient_Higher_Order_Parametrization_Preprint.pdf). | Publisher abstract/introduction/result excerpts and indexed first page consulted; the complete PDF was size-blocked during the investigation. Helped distinguish richer within-element UV representation from a better corner optimizer. Its higher-order representation and injectivity machinery were not implemented. |
| Felix Knöppel, Keenan Crane, Ulrich Pinkall and Peter Schröder, **Stripe Patterns on Surfaces** (2015). [Authors' project and paper](https://www.cs.cmu.edu/~kmcrane/Projects/StripePatterns/). | Author page and indexed interpolation/rendering excerpts consulted when considering direction fields and phase-based patterns. This informed the comparison with a general surface-field approach; no stripe solver or singularity handling was implemented. |
| Kai Hormann and Marco Tarini, **A Quadrilateral Rendering Primitive** (2004). [Publication record](https://diglib.eg.org/items/e539603f-2fdf-4013-a65e-61ab9b94bff2), [author-hosted paper](https://www.inf.usi.ch/hormann/papers/Hormann.2004.AQR.pdf). | Primary abstract/excerpts consulted to distinguish an internal quad-diagonal artifact from a join between neighboring quads. Background for the diagnosis, not the source of the production polar algorithm. |

No external DCC source, shader or copied reference implementation was used for
this circular feature. The MVC and polar experiments were independently written
from mathematical formulas and Quader's observed inputs/outputs. A temporary
texture-baked GPU comparison helped inspect candidate appearances; production
uses the live field with arbitrary materials, without a baked texture. The
test-only MVC comparison is retained for regression and explanation.

## Verification owners

[SDK circular tests](../../../../../tests/sdk/uv_peel_circular_tests.cpp) cover
default-off behavior, fitting/fallback, shared fields, transforms, persistence,
invalid raw records and annotation invalidation. The
[native service tests](../../../../../tests/uv_peel_tool_service_tests.cpp) cover
preview toggles, Cancel/Apply, repeat, history and prepared component publication.
[QDR tests](../../../../../tests/qdr_binary_scene_codec_tests.cpp) protect schema
20 compiled fields, historical readers and byte-stable reserialization; QGEO
minor 4 protects the authored extension. Both formats retain older unannotated
inputs, and unused native optional vectors remain absent.

[CPU interpolation tests](../../../../../tests/mesh_quad_uv_interpolation_tests.cpp)
retain the MVC/polar comparison and nonlinear sampler checks.
[Vulkan normal tests](../../../../../tests/render_normal_mapping_runtime_tests.cpp)
check analytic normal directions through static and dynamic uploads. The
opt-in `VulkanSavedStairsOptInCircularUvPeel` in
[selection-overlay runtime tests](../../../../../tests/selection_overlay_runtime_tests.cpp)
copies the user's project before opening it, checks exact geometry preservation,
captures off/on/checker textures, and exercises preview, Cancel, Apply, undo,
redo and save/reopen. Its environment controls are documented at the test entry.
The [Qt settings test](../../../../../tests/qt_tool_settings_number_slider_tests.cpp)
captures the checkbox in English/Italian at 100%, 200%, narrow width and after
scaling down. Real texture and widget captures were inspected before handoff.

The Full and Public Release executables were built and verified without changing
the application version. These are internal technical notes: UV Peel remains
Full-only, so this work does not add a public manual page.

## Structured guide ownership

The structured helper owns an immutable face/edge index built once per target and reused by
its guides. It transports both endpoints across opposite quad edges and integrates their
world-space distances separately. Translation is excluded from the metric. Boundaries and
non-quads end a profile; returning to its directed starting occurrence closes it. Ambiguous
incidence, inconsistent winding, nonclosing cycles and exhausted budgets fail atomically.

UVs belong to authored face corners, including distinct occurrences at a seam. Endpoint spans
are normalized against the maximum transverse span for World Density, or a unit span for Fit.
Repeated corner assignments use explicit guide-segment, endpoint, side and occurrence priority.
This preserves the established cyclic seam values without relying on incidental map iteration.
The adapter retains inverse-sampling certification, stored-float reconstruction and exact
repeated-output no-op behavior. Preparation creates no persistent modeling truth.

The replacement helper was authored from a value interface, observable requirements and Quader
tests without consulting the preceding implementation, foreign editor source or binary research.
Integration retained the existing guide ordering, solver and publication machinery. This describes
the helper's engineering inputs, not legal clearance or independence of the entire UV subsystem.

`UvPeelInvarianceTests` establishes storage-order, corner-rotation, guide-order, seam and world-metric
invariants through the public SDK. Its fixtures passed before replacement. Existing
`ModelingSdkConsumerTests.UvPeel*`, `UvPeelSavedSceneTests`, `UvActionReliabilityTests` and native
`UvPeelToolServiceTests` retain numerical, failure, preview and history coverage.
