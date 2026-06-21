#include "ui/ViewportSettingsPanel.h"

#include "viewport/ViewportSettings.h"

#include <imgui.h>

namespace quader {

bool ViewportSettingsPanel::draw(UIContext& context, bool& open) {
    changedThisFrame_ = false;
    drawFloating(context, open);
    return changedThisFrame_;
}

void ViewportSettingsPanel::drawContent(UIContext& context, bool& open) {
    (void)open;

    if (ImGui::Button("Reset to Defaults")) {
        context.viewportSettings.resetToDefaults();
        changedThisFrame_ = true;
    }

    for (const ViewportSettingsSectionDescriptor& section : viewportSettingsSections()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
        if (section.defaultExpanded) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        if (!ImGui::CollapsingHeader(section.label.data(), flags)) {
            continue;
        }

        for (const ViewportColorSettingDescriptor& descriptor : viewportColorSettings()) {
            if (descriptor.section != section.id) {
                continue;
            }
            ViewportSettingColor color = context.viewportSettings.color(descriptor.id);
            float values[4] = {color.r, color.g, color.b, color.a};
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::ColorEdit4(descriptor.label.data(),
                        values,
                        ImGuiColorEditFlags_NoInputs |
                                ImGuiColorEditFlags_AlphaBar |
                                ImGuiColorEditFlags_AlphaPreviewHalf)) {
                changedThisFrame_ = context.viewportSettings.setColor(
                                            descriptor.id,
                                            ViewportSettingColor{values[0], values[1], values[2], values[3]}) ||
                        changedThisFrame_;
            }
        }

        for (const ViewportFloatSettingDescriptor& descriptor : viewportFloatSettings()) {
            if (descriptor.section != section.id) {
                continue;
            }
            float value = context.viewportSettings.floatValue(descriptor.id);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::SliderFloat(descriptor.label.data(), &value, descriptor.minimum, descriptor.maximum, "%.3f")) {
                changedThisFrame_ = context.viewportSettings.setFloatValue(descriptor.id, value) || changedThisFrame_;
            }
        }

        for (const ViewportBoolSettingDescriptor& descriptor : viewportBoolSettings()) {
            if (descriptor.section != section.id) {
                continue;
            }
            bool value = context.viewportSettings.boolValue(descriptor.id);
            if (ImGui::Checkbox(descriptor.label.data(), &value)) {
                changedThisFrame_ = context.viewportSettings.setBoolValue(descriptor.id, value) || changedThisFrame_;
            }
        }
    }
}

} // namespace quader
