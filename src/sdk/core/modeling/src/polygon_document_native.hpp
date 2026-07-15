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
  std::shared_ptr<const quader_poly::Document> document =
      std::make_shared<const quader_poly::Document>();
  std::shared_ptr<const quader_poly::Selection> selection =
      std::make_shared<const quader_poly::Selection>();
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
    impl->document = std::make_shared<const quader_poly::Document>(
        std::move(document));
    impl->selection = std::make_shared<const quader_poly::Selection>(
        std::move(selection));
    return PolygonDocument(std::move(impl));
  }

  [[nodiscard]] static const quader_poly::Document &
  document(const PolygonDocument &document) {
    return *document.impl_->document;
  }

  [[nodiscard]] static const quader_poly::Selection &
  selection(const PolygonDocument &document) {
    return *document.impl_->selection;
  }

  static void replace_document(PolygonDocument &document,
                               quader_poly::Document replacement) {
    document.impl_->document =
        std::make_shared<const quader_poly::Document>(std::move(replacement));
  }

  static void replace_selection(PolygonDocument &document,
                                quader_poly::Selection replacement) {
    document.impl_->selection =
        std::make_shared<const quader_poly::Selection>(std::move(replacement));
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
