////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/viewport/viewport_interaction_frame.hpp"
#include "viewport/viewport_pointer_gesture.hpp"

#include <optional>
#include <array>

namespace quader {

class QuaderEditorApi;

/// Owns the selected point-light radius handle's immutable pointer gesture.
class PointLightRangeInteraction final {
public:
  [[nodiscard]] bool handle_frame(QuaderEditorApi &editor,
                                  const ViewportInteractionFrame &frame);
  [[nodiscard]] bool cancel(QuaderEditorApi &editor);
  void reset(QuaderEditorApi *editor);
  [[nodiscard]] bool pointer_active() const {
    return gesture_.has_value() || release_suppression_.active();
  }
  [[nodiscard]] bool dragging() const { return edit_.has_value(); }
  [[nodiscard]] ViewportPointerCursor pointer_cursor() const {
    if (edit_) return ViewportPointerCursor::ClosedHand;
    return gesture_ || hovered_ ? ViewportPointerCursor::OpenHand
                                : ViewportPointerCursor::Default;
  }
  [[nodiscard]] std::uint64_t hover_pick_count() const { return hover_pick_count_; }

private:
  /// Freezes the camera-facing drag plane and its press coordinate.
  struct Gesture {
    ViewportPointerGesture pointer;
    SceneSessionId scene;
    EntityId entity = kInvalidEntityId;
    std::uint64_t selection_revision = 0;
    QVec3 center = {};
    QVec3 normal = {};
    float start_coordinate = 0.0f;
    float range = 0.0f;
  };
  std::optional<Gesture> gesture_;
  std::optional<QuaderEntityRef> edit_;
  [[nodiscard]] std::optional<ViewportSceneBillboardPickCandidate>
  pick_handle(const ViewportInteractionFrame &frame, QVec2 local);
  bool hovered_ = false;
  bool pick_cache_valid_ = false;
  std::uint64_t pick_revision_ = 0;
  std::array<float, 17> pick_camera_ = {};
  QVec2 pick_local_ = {};
  std::size_t pick_candidate_count_ = 0;
  std::optional<ViewportSceneBillboardPickCandidate> picked_handle_;
  std::uint64_t hover_pick_count_ = 0;
  ViewportPointerReleaseSuppression release_suppression_;
  bool left_down_ = false;
};

} // namespace quader
