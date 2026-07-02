////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"

#include <mesh/polygon/document.hpp>

namespace quader {

/**
 * Bridges the public opaque polygon document wrapper to the absorbed polygon backend.
 */
class QuaderPolygonDocumentInternalAccess final {
public:
  [[nodiscard]] static QuaderPolygonDocument
  from_backend(quader_poly::Document document);
  [[nodiscard]] static quader_poly::Document
  to_backend(const QuaderPolygonDocument &document);
};

} // namespace quader
