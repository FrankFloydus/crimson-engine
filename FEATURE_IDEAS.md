<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Product ideas

Optional directions for textured environment/prop modeling. This is a brainstorming list, not
implementation authorization or a defect backlog. Recheck the current toolset before promoting
an idea; verified serious defects belong in [tasklist.md](tasklist.md).

## Modeling

- Revisit committed operation parameters through a small editable operation stack.
- Editable Boolean cutters for openings and unions.
- Smart extrusion that joins or cuts intersecting surfaces.
- Ongoing symmetry with welding, clipping and an adjustable plane.
- Sweeps driven by editable Curves and cross-sections.
- Persistent dimensions and geometric constraints.
- Surface retopology with snapping and relaxation.

## Materials and UVs

- A 2D UV workspace with synchronized selection, seams, stitching, straightening and packing.
- Project texel-density targets, inspection and normalization.
- Named trim-sheet strips with repetition and endpoint alignment.
- Multi-face/brush continuation extending the existing face-texture transfer.
- Texture anchors that preserve alignment and density while geometry changes.
- Surface decal placement, layering and export baking.
- Layered materials with painted or procedural masks.
- Artist-facing normal, ambient-occlusion, curvature and material-ID baking.

## Modular assembly

- Reusable prefab/kit libraries with pivots, materials and thumbnails.
- Named connectors for compatible module placement.
- Resizable modules that preserve frame/trim dimensions and texture scale.
- Parametric stairs, arches, doors, railings and wall panels.
- Distribution improvements beyond the existing Path Array: surface scattering, exclusion zones,
  spacing/orientation controls and repeatable random seeds.
- Replace tagged blockouts with detailed modules while preserving dimensions and connections.

## Workflow and interchange

- Saved selections and reevaluable selection rules.
- Parameterized modeling recipes recorded from ordinary operations.
- Named visual checkpoints and alternatives, including selective object/material restoration.
- Complete scene interchange carrying hierarchy, transforms, materials and textures.
- Incremental synchronization with one game engine and explicit override/conflict handling.
- Collision/LOD generation, inspection and export.
- A production inspector for geometry, materials, UVs and export compatibility.
- Walkthrough clearance checks using player size, eye height and step height.

Current context: command search, editable Curves, Path Array, UV Peel and single-face texture
continuation already exist. Future work should extend their owners, not recreate those features.
Priorities require a new product decision; the previous speculative ranking is not a roadmap.
