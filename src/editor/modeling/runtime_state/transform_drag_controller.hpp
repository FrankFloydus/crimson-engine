////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

/**
 * Stores routing decisions for transform drag preview ownership.
 */
struct TransformDragPreviewRouting {
  bool preview_driven = false;
  bool needs_runtime_preview_transaction = false;
  bool scene_transform_preview_eligible = false;
};

/**
 * Coordinates Transform Drag Controller state transitions for the editor command and action routing layer.
 */
class TransformDragController {
public:
  [[nodiscard]] static TransformDragPreviewRouting
  preview_routing(bool duplicate_selected, bool object_selection_mode,
                  bool transform_operation);

  void set_preview_driven(bool preview_driven, bool duplicate_selected);
  void clear();

  [[nodiscard]] bool preview_driven() const;
  [[nodiscard]] bool duplicate_selected() const;

private:
  bool preview_driven_ = false;
  bool duplicate_selected_ = false;
};

} // namespace quader
