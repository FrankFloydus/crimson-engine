<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Modeling Domain Tests Guide

`tests/modeling_domain` covers geometry, polygon, runtime modeling, tools, metadata, and QDR domain
behavior.

## Modeling Test Rules

- Keep tests deterministic and headless.
- Use focused fixtures and helpers under this subtree before adding broad integration coverage.
- Polygon operation tests should assert topology, geometry, UVs, selection, metadata, and error
  behavior relevant to the operation.
- Runtime modeling tests should inspect session state, history, overlays, picking, tool lifecycle,
  and repeat/undo/redo behavior directly.
- Register new modeling-domain tests through `cmake/QuaderModeling.cmake`.
