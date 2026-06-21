#pragma once

#include "core/LocalizationService.h"
#include "ui/UIContext.h"
#include "ui/UIStyleTokens.h"

#include <imgui.h>

#include <string>

namespace quader {

class UIFloatingPanel {
public:
    virtual ~UIFloatingPanel() = default;

    void drawFloating(UIContext& context, bool& open) {
        if (!open) {
            return;
        }

        beforeBegin(context, open);
        ImGui::SetNextWindowSize(defaultSize(), ImGuiCond_FirstUseEver);
        const std::string windowTitle = context.localization.imguiLabel(titleKey(), titleStableId(), title());
        if (ImGui::Begin(windowTitle.c_str(), &open, windowFlags(context))) {
            drawContent(context, open);
        }
        ImGui::End();
    }

protected:
    virtual const char* title() const = 0;
    virtual const char* titleKey() const { return title(); }
    virtual const char* titleStableId() const { return title(); }
    virtual void drawContent(UIContext& context, bool& open) = 0;

    virtual ImVec2 defaultSize() const { return {360.0f, 240.0f}; }

    virtual void beforeBegin(UIContext& context, bool& open) {
        (void)context;
        (void)open;
    }

    virtual ImGuiWindowFlags windowFlags(const UIContext& context) const {
        return context.style.panelWindowFlags | ImGuiWindowFlags_NoDocking;
    }
};

} // namespace quader
