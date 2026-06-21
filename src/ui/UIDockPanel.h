#pragma once

#include "ui/UIContext.h"
#include "ui/UILayout.h"
#include "ui/UIPanel.h"
#include "ui/UIStyleTokens.h"

#include <imgui.h>

namespace quader {

class UIDockPanel : public UIPanel {
public:
    const char* title() const override { return uiPanelKey(id()); }

    void draw(UIContext& context) final {
        const float minWidth = minimumWidth(context);
        if (minWidth > 0.0f) {
            setNextWindowMinWidth(minWidth);
        }

        if (!ImGui::Begin(title(), nullptr, windowFlags(context))) {
            ImGui::End();
            return;
        }

        if (routesPanelFocus() &&
                ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
                panelFocusInputActive()) {
            context.requestPanelFocus(id());
        }

        drawContent(context);
        ImGui::End();
    }

protected:
    virtual void drawContent(UIContext& context) = 0;
    virtual float minimumWidth(const UIContext& context) const {
        (void)context;
        return 0.0f;
    }
    virtual ImGuiWindowFlags windowFlags(const UIContext& context) const { return context.style.panelWindowFlags; }
    virtual bool routesPanelFocus() const { return true; }
};

} // namespace quader
