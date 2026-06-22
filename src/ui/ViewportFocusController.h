////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/UIPanel.h"

namespace quader {

struct UIContext;

/**
 * Stores the Viewport Focus State data contract used by the ImGui editor presentation layer.
 */
struct ViewportFocusState {
    bool requestedThisFrame = false;
    bool focused = false;
};

/**
 * Coordinates Viewport Focus Controller state transitions for the ImGui editor presentation layer.
 */
class ViewportFocusController {
public:
    [[nodiscard]] bool inputActive() const;
    [[nodiscard]] ViewportFocusState resolve(bool focusHovered, const UIContext& uiContext, UIPanelId panelId) const;
};

} // namespace quader
