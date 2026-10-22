////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/bounds_resize_interaction.hpp"
#include "editor/viewport/viewport_interaction_frame.hpp"
#include "viewport/viewport_pointer_gesture.hpp"

#include <cstdint>
#include <optional>

namespace quader {

class QuaderEditorApi;

/// Retains a pane-local projection of the authoritative common selection bounds.
class MeshBoundsResizePresentation final {
public:
  [[nodiscard]] const BoundsResizeFrame *prepare(
      QuaderEditorApi &editor, const ViewportCameraController &camera,
      const ViewportInteractionSelectionState &selection,
      QuaderTransformSpace space, std::uint64_t preview_revision,
      float raster_scale, const BoundsResizeFrame *shared = nullptr);
  [[nodiscard]] const std::optional<ViewportBoundsResizeState> &bounds() const {
    return bounds_;
  }
  [[nodiscard]] std::uint64_t bounds_query_count() const { return bounds_query_count_; }
  void reset();

private:
  std::optional<std::uint64_t> signature_;
  std::optional<ViewportBoundsResizeState> bounds_;
  BoundsResizeInteraction layout_;
  std::uint64_t bounds_query_count_ = 0U;
};

/// Resizes one captured mesh selection through the existing transform owner.
class MeshBoundsResizeInteraction final {
public:
  [[nodiscard]] ViewportBeforeToolInteractionResult handle_frame(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame);
  [[nodiscard]] bool cancel(QuaderEditorApi &editor);
  void reset(QuaderEditorApi *editor = nullptr);
  [[nodiscard]] bool pointer_active() const {
    return gesture_.has_value() || release_suppression_.active();
  }
  [[nodiscard]] bool dragging() const { return dragging_; }
  void clear_hover() { interaction_.hover(-1); }
  [[nodiscard]] const BoundsResizeFrame *frame() const;
  [[nodiscard]] std::uint64_t bounds_query_count() const {
    return presentation_.bounds_query_count();
  }

private:
  /// Pins the pressed selection and context before a deferred resize can settle a tool.
  struct Gesture final {
    ViewportPointerGesture pointer;
    SceneSessionId scene;
    std::uint64_t mode_generation = 0U;
    std::uint64_t selection_revision = 0U;
    std::uint64_t document_generation = 0U;
    bool settle_tool = false;
    QuaderTransformDragBeginRequest request;
  };
  [[nodiscard]] bool finish(QuaderEditorApi &editor);
  void retire();

  MeshBoundsResizePresentation presentation_;
  BoundsResizeInteraction interaction_;
  std::optional<Gesture> gesture_;
  std::optional<double> last_displacement_;
  std::optional<bool> last_grid_snap_;
  int last_grid_preset_ = 0;
  bool dragging_ = false;
  bool left_down_ = false;
  ViewportPointerReleaseSuppression release_suppression_;
};

} // namespace quader
