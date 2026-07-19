////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/mesh/polygon_document.hpp>

#include <quader/modeling/neutral/polygon/document.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
  mutable std::shared_ptr<const quader_poly::Document> edge_cache_document;
  mutable std::shared_ptr<const std::vector<quader_poly::Edge>> edge_cache;
};

/**
 * Captures only the immutable selection payload and its document-local
 * revision. Topology is deliberately excluded so component-selection history
 * can swap a sparse payload without replacing the polygon document.
 */
struct PolygonDocumentSelectionState {
  std::shared_ptr<const quader_poly::Selection> selection;
  std::uint64_t selection_revision = 0;
};

/**
 * Owns a fully validated immutable replacement ready for a no-fail commit.
 */
struct PolygonDocumentSelectionReplacement {
  std::shared_ptr<const quader_poly::Selection> selection;
  std::uint64_t selection_revision = 0;
  bool changed = false;
  std::string message;
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

  /**
   * Validates and canonicalizes an exact component-selection replacement
   * without mutating the document or copying its immutable topology.
   */
  [[nodiscard]] static Result<PolygonDocumentSelectionReplacement>
  prepare_selection_replacement(
      const PolygonDocument &document,
      const PolygonSelectionSnapshot &replacement);

  /**
   * Validates an exact replacement against another document while reusing an
   * already canonical immutable payload for the same snapshot. This removes
   * one selection allocation per identical object in large batch selections
   * without weakening per-document stale-ID validation.
   */
  [[nodiscard]] static Result<PolygonDocumentSelectionReplacement>
  prepare_selection_replacement_reusing(
      const PolygonDocument &document,
      const PolygonDocument &validated_document,
      const PolygonSelectionSnapshot &replacement,
      std::shared_ptr<const quader_poly::Selection> canonical_selection);

  [[nodiscard]] static PolygonDocumentSelectionState
  capture_selection_state(const PolygonDocument &document) noexcept {
    return {
        .selection = document.impl_->selection,
        .selection_revision = document.impl_->selection_revision,
    };
  }

  /** Commits a previously prepared replacement using only no-fail moves. */
  static void apply_selection_replacement(
      PolygonDocument &document,
      PolygonDocumentSelectionReplacement replacement) noexcept {
    if (!replacement.changed) {
      return;
    }
    document.impl_->selection = std::move(replacement.selection);
    document.impl_->selection_revision = replacement.selection_revision;
  }

  /** Swaps a sparse selection-history payload for undo/redo. */
  static void swap_selection_state(
      PolygonDocument &document,
      PolygonDocumentSelectionState &alternate) noexcept {
    std::swap(document.impl_->selection, alternate.selection);
    std::swap(document.impl_->selection_revision,
              alternate.selection_revision);
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
