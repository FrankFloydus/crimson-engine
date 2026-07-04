////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <nlohmann/json.hpp>
#include <mesh/polygon/document.hpp>

namespace quader_io {

using QdrJson = nlohmann::ordered_json;

/**
 * Selects compatibility defaults for migrated polygon payloads or strict
 * current-schema validation for project payloads.
 */
enum class QdrPolygonDocumentParseMode {
  LegacyDefault,
  StrictCurrentSchema,
};

[[nodiscard]] QdrJson qdr_polygon_document_to_json(const quader_poly::Document& document);
[[nodiscard]] quader_poly::Document qdr_polygon_document_from_json(const QdrJson& value);
[[nodiscard]] quader_poly::Document
qdr_polygon_document_from_json(const QdrJson &value,
                               QdrPolygonDocumentParseMode mode);

} // namespace quader_io
