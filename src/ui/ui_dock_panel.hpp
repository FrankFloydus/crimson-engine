////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/localization_service.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_layout.hpp"
#include "ui/ui_panel.hpp"
#include "ui/ui_style_tokens.hpp"

#include <imgui.h>

#include <string>

namespace quader {

/**
 * Owns UI Dock Panel behavior within the ImGui editor presentation layer.
 */
class UIDockPanel : public UIPanel {
public:
    const char* title() const override { return uiPanelKey(id()); }

    void draw(UIContext& context) final {
        const float minWidth = minimumWidth(context);
        if (minWidth > 0.0f) {
            setNextWindowMinWidth(minWidth);
        }

        const std::string windowTitle = context.localization.imguiLabel(uiPanelLocalizationKey(id()), uiPanelKey(id()), title());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, panelWindowPadding(context));
        if (!ImGui::Begin(windowTitle.c_str(), nullptr, windowFlags(context))) {
            ImGui::PopStyleVar();
            ImGui::End();
            return;
        }

        if (routesPanelFocus() &&
                ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
                panelFocusInputActive()) {
            context.requestPanelFocus(id());
        }

        drawContent(context);
        ImGui::PopStyleVar();
        ImGui::End();
    }

protected:
    virtual void drawContent(UIContext& context) = 0;
    virtual float minimumWidth(const UIContext& context) const {
        (void)context;
        return 0.0f;
    }
    virtual ImGuiWindowFlags windowFlags(const UIContext& context) const { return context.style.panelWindowFlags; }
    virtual ImVec2 panelWindowPadding(const UIContext& context) const { return context.style.windowPadding; }
    virtual bool routesPanelFocus() const { return true; }
};

} // namespace quader
