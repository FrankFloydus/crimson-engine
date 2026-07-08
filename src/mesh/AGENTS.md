<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Mesh Package Guide

Follow root `AGENTS.md` domain ownership, SDK-boundary, modeling, and testing rules before changing
polygon-backed behavior.

## Mesh Rules

- `src/mesh` owns pure geometry and polygon algorithms.
- Keep mesh code engine-clean: no UI, render, viewport, app, platform, or public API ownership.
- Polygon operations should be deterministic and preserve topology, selection, UV, and metadata
  contracts.
- Editor-facing command semantics, history, selection sync, and scene identity belong above this
  package.
- Add or update modeling-domain tests for geometry, polygon operation, topology, picking, or mesh
  contract changes.
