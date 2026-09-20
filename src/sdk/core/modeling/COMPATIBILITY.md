<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Modeling SDK compatibility ledger

The supported portable API starts at `quader/modeling/modeling.hpp`. The following five 0.1.x
surfaces are deliberately retained compatibility contracts; they are not alternate modeling
owners or migration switches.

| Retained surface | Compatibility reason and supported replacement | Owner and coverage | Revisit |
| --- | --- | --- | --- |
| `ModelingSession::document(ObjectId)` | Existing consumers may still read the borrowed pointer. New code uses `document_snapshot()` or `document_copy()` so mutation cannot invalidate a live read. | SDK session owner; `ModelingSdkConsumerTests` exercises the borrowed and owned paths plus invalidation. | Remove in 0.2.0 after a downstream consumer search. |
| `ModelingBatch::with_mesh(string, function<void(MeshHandle)>)` | Existing 0.1.x fluent batches compile unchanged. New code uses `with_mesh_checked()` so the batch aggregates the real receipt and errors. | SDK batch owner; `ModelingSdkConsumerTests` covers success, failure, cancellation, and the checked replacement. | Remove in 0.2.0 after a downstream consumer search. |
| `PivotToolOptions`, `PivotTool`, and `ToolsApi::activate_pivot_tool()` | Preserves source compatibility without claiming portable pivot support. The call deterministically returns `UnsupportedOperation`; native pivot intent belongs to `QuaderEditorApi`. | SDK tools owner; `ModelingSdkConsumerTests.UnsupportedSdkPlaceholdersAreDeprecatedAndNotAdvertised` and catalog assertions. | Remove in 0.2.0; do not add it to the supported tool catalog. |
| `ProfilingApi` and `ModelingApi::profiling()` | Preserves source compatibility without inventing a portable capture backend. Profiling data contracts remain supported; capture uses the benchmark harness or native diagnostics. | SDK API owner; `ModelingSdkConsumerTests.UnsupportedSdkPlaceholdersAreDeprecatedAndNotAdvertised`. | Remove the facade in 0.2.0; retain useful neutral data contracts independently. |
| `PolygonDocument::extrude_selected_faces(float)` | Retains the existing individual-face displacement and open-base behavior. New face tools use `FaceExtrudeOptions`; `Individual` explicitly selects this topology policy. Vector-based element extrusion remains a separate supported operation. | SDK polygon owner; `ModelingSdkFaceExtrudeTests.LegacyFloatExtrusionKeepsItsOpenBaseBehavior` and `QdrDocumentOperationExtrudeFacesTests`. | Revisit in 0.2.0 after a downstream consumer search. |

Native projection and viewport adapters are permanent host boundaries, not compatibility debt.

`PolygonDocument::prepare_face_extrusion()` and `PreparedFaceExtrusion::evaluate()` are supported
polygon-owner contracts for repeated signed-amount evaluation. They share the typed operation's
topology and displacement policies; the existing `FaceExtrudeOptions` operation remains the
single-edit path and parity reference. A preparation captures immutable source attributes and
allocation cursors. Its nonzero candidates retain generated IDs and carry complete source-relative
receipts. After publishing zero or retiring that topology, hosts must preserve observed allocation
high-water and create a new preparation before publishing another nonzero candidate. SDK consumer
coverage and the opt-in same-mesh benchmark live in `ModelingSdkFaceExtrudeTests`.
