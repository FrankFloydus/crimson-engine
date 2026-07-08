<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Geometry Package

Portable geometry and mesh-processing helpers.

Owns:

- Quader-owned vector, ray, line, plane, segment, triangle, rect, and bounds types;
- plane basis, projection, coordinate, and classified plane-intersection helpers;
- triangulation wrappers and polygon predicates;
- robust primitive distance/intersection helpers for picking, snapping, overlays, and tool math;
- normals, barycentric predicates, bounds, and spatial broad-phase helpers;
- future BVH or spatial index wrappers.

Geometry algorithms are Quader-owned. Public headers expose only Quader-owned types.

## Source Direction

- Keep geometry predicates and distance/intersection helpers implemented in this package instead
  of adding a native geometry backend dependency.
- Boundary guards should stay consistent across float and double paths. Degenerate rays/segments,
  invalid AABBs, degenerate triangles, and mismatched broad-phase spans are permanent boundary
  cases for package tests.
- Adapter-local conversion helpers in `src/mesh/polygon` and `src/editor/modeling` are acceptable
  as thin type boundaries, but reusable predicates, spatial broad-phase, or projection math should
  be added here first and consumed upward.
