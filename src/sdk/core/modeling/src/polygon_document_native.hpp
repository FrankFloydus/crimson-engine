////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/mesh/polygon_document.hpp>

#include <mesh/polygon/document.hpp>

#include <memory>
#include <utility>

namespace quader::modeling {

/**
 * Stores the native polygon document state hidden behind the SDK facade.
 */
struct PolygonDocumentImpl {
  quader_poly::Document document;
  quader_poly::Selection selection;
  std::uint64_t content_revision = 1;
  std::uint64_t selection_revision = 0;
};

/**
 * Adapts SDK polygon documents to current native polygon owners during migration.
 */
class PolygonDocumentNativeAccess final {
public:
  [[nodiscard]] static PolygonDocument
  from_native(quader_poly::Document document,
              quader_poly::Selection selection = {}) {
    auto impl = std::make_unique<PolygonDocumentImpl>();
    impl->document = std::move(document);
    impl->selection = std::move(selection);
    return PolygonDocument(std::move(impl));
  }

  [[nodiscard]] static quader_poly::Document &
  document(PolygonDocument &document) {
    return document.impl_->document;
  }

  [[nodiscard]] static const quader_poly::Document &
  document(const PolygonDocument &document) {
    return document.impl_->document;
  }

  [[nodiscard]] static quader_poly::Selection &
  selection(PolygonDocument &document) {
    return document.impl_->selection;
  }

  [[nodiscard]] static const quader_poly::Selection &
  selection(const PolygonDocument &document) {
    return document.impl_->selection;
  }

  [[nodiscard]] static PolygonDocumentImpl &impl(PolygonDocument &document) {
    return *document.impl_;
  }

  [[nodiscard]] static const PolygonDocumentImpl &
  impl(const PolygonDocument &document) {
    return *document.impl_;
  }
};

} // namespace quader::modeling
