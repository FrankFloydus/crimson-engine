#include "ui/ViewportSettingsPanel.h"

#include "core/LocalizationService.h"
#include "ui/UIContext.h"
#include "ui/UISettings.h"
#include "viewport/ViewportSettings.h"

#include <imgui.h>

#include <string>

namespace quader {
namespace {

std::string_view sectionStableId(ViewportSettingsSectionId section) {
    switch (section) {
    case ViewportSettingsSectionId::Scene:
        return "scene";
    case ViewportSettingsSectionId::Grid:
        return "grid";
    case ViewportSettingsSectionId::Selection:
        return "selection";
    case ViewportSettingsSectionId::Overlay:
        return "overlay";
    case ViewportSettingsSectionId::Vertices:
        return "vertices";
    case ViewportSettingsSectionId::Measurements:
        return "measurements";
    case ViewportSettingsSectionId::Picking:
        return "picking";
    case ViewportSettingsSectionId::Gizmo:
        return "gizmo";
    case ViewportSettingsSectionId::Count:
        break;
    }
    return "unknown";
}

std::string sectionLabel(const LocalizationService& localization, const ViewportSettingsSectionDescriptor& section) {
    std::string key = "viewport.settings.section.";
    key.append(sectionStableId(section.id));
    key.append(".label");
    std::string stableId = "viewport.settings.section.";
    stableId.append(sectionStableId(section.id));
    return localization.imguiLabel(key, stableId, section.label);
}

std::string settingLabel(const LocalizationService& localization, std::string_view key, std::string_view fallback) {
    std::string localizationKey = "viewport.settings.";
    localizationKey.append(key);
    localizationKey.append(".label");
    std::string stableId = "viewport.settings.";
    stableId.append(key);
    return localization.imguiLabel(localizationKey, stableId, fallback);
}

void drawLocaleSettings(UIContext& context) {
    LocalizationService& localization = context.localization;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
    const std::string header = localization.imguiLabel("ui.settings.interface", "ui.settings.interface", "Interface");
    if (!ImGui::CollapsingHeader(header.c_str(), flags)) {
        return;
    }

    const std::string selectedLocale = context.settings.locale();
    std::string preview = localization.localeDisplayName(selectedLocale);
    if (preview.empty()) {
        preview = selectedLocale;
    }

    const std::string comboLabel = localization.imguiLabel("ui.settings.locale", "ui.settings.locale", "Language");
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo(comboLabel.c_str(), preview.c_str())) {
        for (const LocaleInfo& locale : localization.availableLocales()) {
            const bool selected = locale.id == selectedLocale;
            const std::string itemLabel = localization.imguiLabel(
                    "locale." + locale.id + ".name",
                    "locale." + locale.id,
                    locale.displayName);
            if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                if (context.localization.selectLocale(locale.id)) {
                    context.settings.setLocale(context.localization.currentLocale());
                }
                if (context.settingsChanged != nullptr && context.settings.locale() != selectedLocale) {
                    *context.settingsChanged = true;
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

} // namespace

bool ViewportSettingsPanel::draw(UIContext& context, bool& open) {
    changedThisFrame_ = false;
    drawFloating(context, open);
    return changedThisFrame_;
}

void ViewportSettingsPanel::drawContent(UIContext& context, bool& open) {
    (void)open;

    drawLocaleSettings(context);

    const std::string resetLabel = context.localization.imguiLabel(
            "ui.settings.viewport.reset_to_defaults",
            "ui.settings.viewport.reset_to_defaults",
            "Reset to Defaults");
    if (ImGui::Button(resetLabel.c_str())) {
        context.viewportSettings.resetToDefaults();
        changedThisFrame_ = true;
    }

    for (const ViewportSettingsSectionDescriptor& section : viewportSettingsSections()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
        if (section.defaultExpanded) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        const std::string header = sectionLabel(context.localization, section);
        if (!ImGui::CollapsingHeader(header.c_str(), flags)) {
            continue;
        }

        for (const ViewportColorSettingDescriptor& descriptor : viewportColorSettings()) {
            if (descriptor.section != section.id) {
                continue;
            }
            ViewportSettingColor color = context.viewportSettings.color(descriptor.id);
            float values[4] = {color.r, color.g, color.b, color.a};
            ImGui::SetNextItemWidth(180.0f);
            const std::string label = settingLabel(context.localization, descriptor.key, descriptor.label);
            if (ImGui::ColorEdit4(label.c_str(),
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
            const std::string label = settingLabel(context.localization, descriptor.key, descriptor.label);
            if (ImGui::SliderFloat(label.c_str(), &value, descriptor.minimum, descriptor.maximum, "%.3f")) {
                changedThisFrame_ = context.viewportSettings.setFloatValue(descriptor.id, value) || changedThisFrame_;
            }
        }

        for (const ViewportBoolSettingDescriptor& descriptor : viewportBoolSettings()) {
            if (descriptor.section != section.id) {
                continue;
            }
            bool value = context.viewportSettings.boolValue(descriptor.id);
            const std::string label = settingLabel(context.localization, descriptor.key, descriptor.label);
            if (ImGui::Checkbox(label.c_str(), &value)) {
                changedThisFrame_ = context.viewportSettings.setBoolValue(descriptor.id, value) || changedThisFrame_;
            }
        }
    }
}

} // namespace quader
