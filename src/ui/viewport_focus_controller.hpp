////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/ui_panel.hpp"

namespace quader {

struct UIContext;

/**
 * Stores the Viewport Focus State data contract used by the ImGui editor presentation layer.
 */
struct ViewportFocusState {
  bool requested_this_frame = false;
  bool focused = false;
};

/**
 * Coordinates Viewport Focus Controller state transitions for the ImGui editor presentation layer.
 */
class ViewportFocusController {
public:
  [[nodiscard]] bool input_active() const;
  [[nodiscard]] ViewportFocusState resolve(bool focus_hovered,
                                           const UIContext &ui_context,
                                           UIPanelId panel_id) const;
};

} // namespace quader
