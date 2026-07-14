<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Modeling Domain Tests Guide

`tests/modeling_domain` covers geometry, polygon, runtime modeling, tools, metadata, and QDR.

## Modeling Test Rules

- Keep tests deterministic and headless.
- Use focused fixtures/helpers here before broad integration coverage.
- Polygon operation tests assert relevant topology, geometry, UVs, selection, metadata, and errors.
- Runtime tests inspect session state, history, overlays, picking, tool lifecycle, repeat, undo, and
  redo directly.
- Register new modeling-domain tests through `cmake/QuaderModeling.cmake`.

## Placement Rules

- Geometry and polygon-document tests use `geometry` or `polygon`; runtime tests use `runtime`.
  Portable binary QDR belongs to the SDK consumer suite, while native QDRB scene IO belongs to
  root app tests. Do not restore the retired JSON `qdr_document`/`qdr_runtime` categories.
- Polygon operation tests: `tests/modeling_domain/operation/qdr_document_operation_<feature>_tests.cpp`
  with `QdrDocumentOperation...` suites.
- Runtime operation tests: `tests/modeling_domain/operation/qdr_modeling_runtime_operation_<feature>_tests.cpp`
  with `QdrModelingRuntimeOperation...` suites.
- Runtime tool tests: `tests/modeling_domain/tool/qdr_modeling_runtime_tool_<feature>_tests.cpp`
  with `QdrModelingRuntimeTool...` suites.
- Metadata catalog/schema/surface tests: `tests/modeling_domain/editor_metadata/`, category
  `editor_metadata`.

## Selection Rules

- Polygon operation edits: expect `quader_modeling_domain_polygon_loop_tests` plus the matching
  `QdrDocumentOperation...` filter.
- Runtime operation/tool edits: expect `quader_modeling_domain_runtime_loop_tests`; add
  `quader_modeling_domain_metadata_loop_tests` for descriptor/catalog/metadata changes.
- New operation test: build the relevant loop target and run the new suite filter before
  `quader_modeling_domain_tests`.
