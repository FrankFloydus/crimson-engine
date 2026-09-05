<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Native Modeling Runtime Projections

This package works on ephemeral `quader_poly::Document` views projected from the portable core.
The local `quader_editor_core` namespace does not make it the SDK boundary.

- Owns native picking/hover, rectangle selection and pick cycling, selection summaries and semantic
  overlays, transform-drag/tool input state, and native command-to-operation mapping.
- `quader::modeling` owns durable modeling documents, component selection, operations, previews,
  and modeling history. Scene identity/hierarchy and ordered editor history stay with native owners;
  render owns overlay styling/GPU resources, and viewport owns workspace/navigation state.
- Production picking, overlay, bounds, and input helpers consume immutable snapshots or narrow
  `EditorRuntimeSelectionView` values. Do not retain/materialize full `EditorSession` instances as
  production selection projections; that compatibility surface serves local mutating algorithms/tests.

Use the matching `tests/modeling_domain/tool` and runtime suites for host interaction, and SDK
consumer/parity tests for portable behavior. Real window/GPU output follows the root smoke workflow.
