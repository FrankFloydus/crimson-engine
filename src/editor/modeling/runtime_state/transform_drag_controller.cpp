////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime_state/transform_drag_controller.hpp"

namespace quader {

TransformDragPreviewRouting
TransformDragController::preview_routing(bool duplicate_selected,
                                         bool object_selection_mode,
                                         bool transform_operation,
                                         bool scene_transform_selection) {
  const bool duplicate_scene_transform_preview =
      duplicate_selected && object_selection_mode && transform_operation;
  TransformDragPreviewRouting routing;
  routing.preview_driven = !duplicate_scene_transform_preview &&
                           (duplicate_selected || !object_selection_mode);
  routing.needs_runtime_preview_transaction =
      routing.preview_driven ||
      (duplicate_selected && !scene_transform_selection);
  routing.scene_transform_preview_eligible =
      (duplicate_scene_transform_preview || !routing.preview_driven) &&
      transform_operation;
  return routing;
}

void TransformDragController::set_preview_driven(bool preview_driven,
                                                 bool duplicate_selected) {
  preview_driven_ = preview_driven;
  duplicate_selected_ = duplicate_selected;
}

void TransformDragController::clear() {
  preview_driven_ = false;
  duplicate_selected_ = false;
}

bool TransformDragController::preview_driven() const { return preview_driven_; }

bool TransformDragController::duplicate_selected() const {
  return duplicate_selected_;
}

} // namespace quader
