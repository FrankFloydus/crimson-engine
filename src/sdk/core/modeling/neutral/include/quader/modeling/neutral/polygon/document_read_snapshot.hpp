////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only SDK-to-native read bridge; not part of the installed SDK surface.

#include <quader/modeling/mesh/polygon_document.hpp>
#include <quader/modeling/neutral/polygon/document_model.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace quader::modeling {

/** Pins authoritative coordinates and authored topology across later publications. */
class PolygonDocumentReadSnapshot final {
public:
  [[nodiscard]] quader_poly::DocumentReadView view() const noexcept {
    auto result = quader_poly::document_read_view(*source_);
    if (vertices_) {
      result.vertices = *vertices_;
    }
    return result;
  }
  [[nodiscard]] PolygonContentIdentity content_identity() const noexcept {
    return content_identity_;
  }
  [[nodiscard]] PolygonContentIdentity topology_identity() const noexcept {
    return topology_identity_;
  }

private:
  friend class PolygonDocumentStorage;
  PolygonDocumentReadSnapshot(
      std::shared_ptr<const quader_poly::Document> source,
      std::shared_ptr<const std::vector<quader_poly::Vertex>> vertices,
      PolygonContentIdentity content_identity,
      PolygonContentIdentity topology_identity)
      : source_(std::move(source)), vertices_(std::move(vertices)),
        content_identity_(content_identity), topology_identity_(topology_identity) {}

  std::shared_ptr<const quader_poly::Document> source_;
  std::shared_ptr<const std::vector<quader_poly::Vertex>> vertices_;
  PolygonContentIdentity content_identity_;
  PolygonContentIdentity topology_identity_;
};

/** Reads a pinned candidate without materializing or copying its authored faces. */
[[nodiscard]] PolygonDocumentReadSnapshot
polygon_document_read_snapshot(const PolygonDocument &document);

} // namespace quader::modeling
