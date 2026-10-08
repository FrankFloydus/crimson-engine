<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Mesh origin placement

`PivotToolService` owns the Center and Pivot origin actions. They accept a complete
Object-mode selection of editable, visible, unlocked meshes backed by SDK-owned
authored polygon documents. Legacy CPU-only meshes, mixed selections, instances,
non-invertible transforms, retained previews and persistent Pivot sessions are
rejected before mutation. Availability checks inspect selected-object metadata;
execution performs finite-geometry validation and resolves destinations.

Center integrates triangle area and centroid in each mesh's local coordinates,
then transforms that result to world space. It consumes Quader's compiled triangle
indices, including its concave polygon triangulation. Shifted double-precision
compensated sums avoid dependence on large local coordinate offsets. Zero-area
geometry uses the authored vertex mean. The numerical helper also defines a CPU
vertex-mean fallback, but command eligibility still requires an editable authored
document supported by the existing rebase publication owner. Local weighting is
deliberate: nonuniform object scaling does not redefine the center in local
coordinates.

Pivot snapshots the valid Quick Pivot override, or the resolved visible selection
pivot, once for every selected mesh. All requests are captured before mutation
and ordered parent-first. Both actions reuse `PivotRebaseCommand` for world-space
geometry preservation, child compensation, shared-asset copy-on-write, UV mapping,
host/modeling publication rollback and one undo/redo record. There is no separate
origin or modeling state store.

Both actions author `custom_pivot`. An origin already within the existing pivot
tolerance is promoted without changing geometry or child transforms; repeating the
action once it has a custom pivot is a no-op. Comparisons use the actual world
transform origin, not the bounds-derived visible pivot.

## Behavioral reference

The Center contract was checked against the local Blender **5.2.0 alpha** source
snapshot at `C:/Users/Drako/Downloads/blender-main/blender-main`:

- `source/blender/editors/object/object_transform.cc` maps the surface center-of-mass
  origin action to mesh surface-center evaluation; SHA-256
  `14DDC36355547644C96F14D61AACDBB4728761D86E321B41EF13D3BC854340E4`.
- `source/blender/blenkernel/intern/mesh_evaluate.cc` specifies area-weighted local
  surface center with vertex-mean fallback; SHA-256
  `D0DE8FC65F8A8D4943B6DDAF994E1C3334E23AA1E7733CB1BDA205AF51980C85`.

This is an independent integration through Quader's mesh compilation and existing
origin-rebase owner. No Blender implementation was copied or used as a temporary
prototype. For nonplanar faces, Quader integrates its rendered triangles rather
than reproducing the reference's documented polygon approximation.
