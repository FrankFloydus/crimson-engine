<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Modeling Domain Tests Guide

This subtree covers geometry, polygon, runtime modeling, tools, metadata, and QDR.

## Modeling Test Rules

- Keep tests deterministic and headless.
- Use focused fixtures/helpers here before broad integration coverage.
- Assert relevant topology, geometry, UVs, selection, metadata, and errors.
- Runtime tests inspect state, history, overlays, picking, tools, repeat, undo, and redo.
- Register new modeling-domain tests through `cmake/QuaderModeling.cmake`.

## Placement Rules

- Use `geometry`, `polygon`, or `runtime`. Portable QDR belongs to SDK consumers; native QDRB
  scene IO belongs to root app tests. Do not restore JSON `qdr_document`/`qdr_runtime` categories.
- Polygon operation tests: `tests/modeling_domain/operation/qdr_document_operation_<feature>_tests.cpp`
  with `QdrDocumentOperation...` suites.
- Native operation tests are descriptor/command projection only. Portable behavior belongs to
  document-operation and SDK suites; host-adapter behavior belongs to root app tests.
- Runtime tool tests: `tests/modeling_domain/tool/qdr_modeling_runtime_tool_<feature>_tests.cpp`
  with `QdrModelingRuntimeTool...` suites.
- Metadata tests use `tests/modeling_domain/editor_metadata/` and `editor_metadata`.

## Selection Rules

- Polygon operation edits: expect `quader_modeling_domain_polygon_loop_tests` plus the matching
  `QdrDocumentOperation...` filter.
- Runtime tool edits: expect `quader_modeling_domain_runtime_loop_tests`. Native descriptor edits
  use `qdr_modeling_runtime_operation_command_tests.cpp` and
  `QdrModelingRuntimeOperationCommandTests.*`; add
  `quader_modeling_domain_metadata_loop_tests` and SDK suites when descriptors can change.
- New operation test: build the relevant loop target and run the new suite filter before
  `quader_modeling_domain_tests`.
