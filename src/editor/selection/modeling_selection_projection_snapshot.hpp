////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace quader {

/**
 * Immutable native read model of SDK-owned polygon selection.
 *
 * The SDK modeling session remains the source of truth. This value carries only
 * the polygon documents and selection facts required by native picking,
 * overlays, and interaction policy; it deliberately exposes no mutable
 * EditorSession state or modeling mutation API.
 */
class ModelingSelectionProjectionSnapshot final {
public:
  ModelingSelectionProjectionSnapshot() = default;

  ModelingSelectionProjectionSnapshot(
      std::vector<quader_editor_core::PolygonObject> objects,
      quader_editor_core::SelectionMode selection_mode, int active_object_id,
      std::uint64_t content_revision, std::uint64_t selection_revision)
      : objects_(std::move(objects)), selection_mode_(selection_mode),
        active_object_id_(active_object_id),
        content_revision_(content_revision),
        selection_revision_(selection_revision) {}

  [[nodiscard]] std::span<const quader_editor_core::PolygonObject>
  objects() const noexcept {
    return objects_;
  }

  /// Retains vector-backed storage for legacy runtime algorithms that require
  /// stable indexed records. The returned container is immutable.
  [[nodiscard]] const std::vector<quader_editor_core::PolygonObject> &
  object_records() const noexcept {
    return objects_;
  }

  [[nodiscard]] quader_editor_core::SelectionMode
  selection_mode() const noexcept {
    return selection_mode_;
  }

  [[nodiscard]] int active_object_id() const noexcept {
    return active_object_id_;
  }

  [[nodiscard]] std::uint64_t content_revision() const noexcept {
    return content_revision_;
  }

  [[nodiscard]] std::uint64_t selection_revision() const noexcept {
    return selection_revision_;
  }

  [[nodiscard]] bool has_selection() const {
    return quader_editor_core::has_selection(objects_, selection_mode_,
                                             active_object_id_);
  }

  [[nodiscard]] std::vector<int> selected_object_ids() const {
    return quader_editor_core::selected_object_ids(objects_);
  }

  [[nodiscard]] quader_editor_core::ActiveSelection active_selection() const {
    return quader_editor_core::active_selection(objects_, selection_mode_,
                                                active_object_id_);
  }

  [[nodiscard]] std::optional<QVec3> selection_center() const {
    return quader_editor_core::selection_center(objects_, selection_mode_,
                                                active_object_id_);
  }

  [[nodiscard]] quader_editor_core::Bounds3 selection_bounds() const {
    return quader_editor_core::selection_bounds(objects_, selection_mode_);
  }

  /**
   * Creates a short-lived edited value for native selection policy. The
   * adapter must explicitly publish the result back to the SDK owner.
   */
  [[nodiscard]] ModelingSelectionProjectionSnapshot with_selection(
      std::vector<quader_editor_core::PolygonObject> objects,
      quader_editor_core::SelectionMode selection_mode, int active_object_id,
      std::uint64_t selection_revision) const {
    return ModelingSelectionProjectionSnapshot(
        std::move(objects), selection_mode, active_object_id,
        content_revision_, selection_revision);
  }

private:
  std::vector<quader_editor_core::PolygonObject> objects_;
  quader_editor_core::SelectionMode selection_mode_ =
      quader_editor_core::SelectionMode::Object;
  int active_object_id_ = -1;
  std::uint64_t content_revision_ = 0;
  std::uint64_t selection_revision_ = 0;
};

} // namespace quader
