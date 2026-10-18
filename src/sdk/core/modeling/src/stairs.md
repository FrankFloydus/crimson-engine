<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Stairs surface topology

`stairs.cpp` owns preset geometry and body-clearance validation. The native creation
tool supplies dimensions and publishes the resulting ordinary polygon document.
Generation retains no recipe in accepted meshes; topology improvements apply when
creating a new mesh.

## Flight guides

Flights have two quad lanes across their walking surfaces and undersides, with a
center rail for longitudinal edge-loop selection. At an internal riser, the
underside advances through a midpoint on the following chord while the walking
surface rises, then advances to the chord end
with the tread. Chord interpolation preserves the outer boundary.

Curved steps use the same number of equal chord panels within every tread, rounded
up to meet the requested angular density. Riser boundaries are always panel
boundaries. Ramps retain uniform angular panels. Curved ramp and slab underside
quads can be nonplanar; the polygon owner's canonical triangulation defines their
rendered surfaces.

Planar route end caps deliberately terminate longitudinal guides. A Solid ramp's
zero-thickness starting tip is retained by its ordinary wall outline. Its first
planar underside panel is one six-corner polygon, so the coincident top/bottom
starting vertex terminates the walking guide instead of continuing onto the
underside. This terminal underside panel uses planar mapping separately from the
underside quad strip.

## Minimal wall panels and automatic UVs

Each original run or angular chord owns one wall polygon on each side, using only
vertices already needed by the walking surface and underside. Ordinary panels
remain genuine quads. A step transition can require six boundary corners to retain
the preceding tread-height vertex and the underside midpoint. The Solid ramp tip
can be a triangle. No wall-only vertices, inset frames, guide rows or interior
panel edges are added.

Wall UVs are authored when generating the mesh: U follows accumulated side-chord
distance and V follows height, at the existing half-unit world-density convention.
This affine panel mapping preserves texture density and phase between neighboring
panels, independently of their rendering diagonals. There is no need to subdivide
a wall to supply these texture coordinates.

Whole wall polygons retain strict polygon validation and canonical triangulation.
A float-limited polygon that cannot retain its canonical boundary can use a
validated split through existing corners; this adds no interior vertices.

The current UV Peel walk continues through ordinary quads. Consequently a later
Peel from a walking or underside guide can also rewrite wall quads. Generated wall
UVs do not create an implicit Peel barrier. An explicit Peel boundary mechanism is
deferred; do not simulate one by adding mesh geometry or repurposing shading data.

## Independent landing patches

Each flight is peeled independently of its neighbors. Landing ports share the
flight's exact boundary vertices, with no interior landing edge extending from a
flight center rail. The degree-three center endpoint stops ordinary loop selection.
Landing non-quads also stop structured transverse Peel walks.

The landing top consists of two planar polygons with at least five corners and one
shared internal guide. L landings use an opposite-corner diagonal; U and switchback
landings use a divider across the middle of their depth. Necessary perimeter
midpoints are shared with the exposed walls. The underside has its own matching
divider. Exposed planar wall spans remain non-quads and use ordinary face mapping.
There are no internal flight caps or duplicate shells at these connections.

Selecting the internal divider invokes the existing non-quad UV Peel path: its
incident patch contains exactly the two landing faces. Loop expansion leaves this
divider as a single edge. Peeling a top divider maps the landing top; the underside
is a separate operation, and vertical walls and end caps use planar face mapping.
See [UV Peel ownership](uv_peel.md) for the solver contract.

## Clearance and regression contract

Collision checks use the final float-authored geometry and the polygon owner's
canonical triangles for both upper and lower surfaces. Apply the mesh compiler's
face-normal orientation correction before classifying their projected winding;
canonical local triangle indices alone do not guarantee outward n-gon winding.
Nominal side and cap faces can contribute small envelope slivers after float
rounding, so every exterior face participates. Upper and lower subdivision
patterns may differ. Each route cell's projected hull and bounds contain every
canonical triangle vertex, including independently rounded center rails; four
outer corners alone are insufficient. These bounds limit candidate pairs, and
projected triangle intersections establish a consistent vertical ordering across
each overlap. Adjacent boundary contact is resolved on those actual triangles.
Nonadjacent body contact remains a rejection, including clearance lost to float
rounding.

[Portable fixtures](../../../../../tests/sdk/modeling_stairs_tests.cpp) protect
closed connected geometry, dimensions, subdivision, canonical interpolation and
clearance. [Native workflows](../../../../../tests/stairs_creation_tool_tests.cpp)
exercise generated mesh selection and UV Peel through the editor owners. The
stairs cases in [Vulkan overlay fixtures](../../../../../tests/selection_overlay_runtime_tests.cpp)
provide actual surface, wire and checker captures.
