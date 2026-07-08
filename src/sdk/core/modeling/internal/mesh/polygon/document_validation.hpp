////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document_model.hpp>

#include <string>
#include <vector>

namespace quader_poly {

/**
 * Enumerates polygon document validation diagnostic severities.
 */
enum class PolygonDocumentDiagnosticSeverity {
  Warning,
  Error,
};

/**
 * Enumerates polygon document validation diagnostic codes.
 */
enum class PolygonDocumentDiagnosticCode {
  DuplicateVertexId,
  DuplicateFaceId,
  MissingVertexReference,
  RepeatedFaceVertex,
  DegenerateFaceLoop,
  NonmanifoldEdge,
  FaceUvCountMismatch,
  InvalidHardNormalEdge,
  UnreferencedVertex,
  TriangulationFailure,
};

/**
 * Stores one programmatically inspectable polygon document validation issue.
 */
struct PolygonDocumentDiagnostic {
    PolygonDocumentDiagnosticCode code = PolygonDocumentDiagnosticCode::DuplicateVertexId;
    PolygonDocumentDiagnosticSeverity severity = PolygonDocumentDiagnosticSeverity::Error;
    ElementId vertex_id = kInvalidElementId;
    ElementId face_id = kInvalidElementId;
    Edge edge;
    std::vector<ElementId> vertex_ids;
    std::string message;
};

/**
 * Stores all validation diagnostics for a polygon document.
 */
struct PolygonDocumentValidationReport {
    std::vector<PolygonDocumentDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const;
    [[nodiscard]] bool ok() const;
};

[[nodiscard]] PolygonDocumentValidationReport validate_polygon_document(const Document& document);

} // namespace quader_poly
