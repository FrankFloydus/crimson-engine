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

- Quader-owned vector, ray, line, plane, segment, triangle, rect, and bounds types
- plane basis, projection, coordinate, and classified plane-intersection helpers
- triangulation wrappers and polygon predicates
- robust primitive distance/intersection helpers for picking, snapping, overlays, and tool math
- normals, barycentric predicates, bounds, and spatial broad-phase helpers
- future BVH or spatial index wrappers

Third-party geometry backends must stay private to this package. Public headers must expose only Quader-owned types.

## Phase 1 Source-Direction Notes

- Keep GTE Mathematics includes and `gte::` types confined to private `src`
  implementation files. Consumers should only see `quader_geometry` types from
  public headers, and `qdr_geometry` should keep backend targets linked
  privately.
- Boundary guards should stay consistent between template fallback paths and
  GTE-backed `double` paths. Degenerate rays/segments, invalid AABBs,
  degenerate triangles, and mismatched broad-phase spans are permanent boundary
  cases for package tests.
- Migration candidates found during the Slice 1 audit:
  - `package/com.qvertexworks.quader/Editor/Viewport/CutPreviewResultLineBuilder.cs`
    owns mesh-plane cut-preview line extraction in Unity presentation code.
    It should move behind an SDK/native geometry contract if the behavior
    becomes portable tool or overlay policy rather than render-only preview
    mesh shaping.
  - `package/com.qvertexworks.quader/Editor/Viewport/CutPreviewPlaneFrame.cs`
    owns cut-preview plane frame construction in Unity presentation code.
    Keep it render-only, or promote the reusable plane-frame math into
    `qdr_geometry` before another engine needs to match it.
  - Adapter-local conversion helpers in `src/modeling/polygon` and
    `src/modeling` are acceptable as thin type boundaries, but
    reusable predicates, spatial broad-phase, or projection math should be
    added here first and consumed upward.
