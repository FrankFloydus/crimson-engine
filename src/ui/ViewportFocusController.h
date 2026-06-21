#pragma once

#include "ui/UIPanel.h"

namespace quader {

struct UIContext;

struct ViewportFocusState {
    bool requestedThisFrame = false;
    bool focused = false;
};

class ViewportFocusController {
public:
    [[nodiscard]] bool inputActive() const;
    [[nodiscard]] ViewportFocusState resolve(bool focusHovered, const UIContext& uiContext, UIPanelId panelId) const;
};

} // namespace quader
