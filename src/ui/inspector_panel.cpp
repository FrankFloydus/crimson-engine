////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/inspector_panel.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "core/scene_environment_catalog.hpp"
#include "scene/scene_document.hpp"
#include "scene/transform_pivot.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_style_tokens.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float RadToDeg = 57.29577951308232f;
constexpr float DegToRad = 0.017453292519943295f;
constexpr float kInspectorMinimumValueColumnWidth = 72.0f;
constexpr float kTransformLabelColumnWidth = 70.0f;
constexpr float kAxisTextColumnWidth = 12.0f;
constexpr float kMinAxisInputWidth = 58.0f;

void copyNameToBuffer(const NameComponent* name, std::array<char, 128>& buffer) {
    buffer.fill('\0');
    if (!name) {
        return;
    }
    std::snprintf(buffer.data(), buffer.size(), "%s", name->name.c_str());
}

const char* selectionModeKey(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return "selection.mode.vertex";
    case ModelingSelectionMode::Edge:
        return "selection.mode.edge";
    case ModelingSelectionMode::Face:
        return "selection.mode.face";
    case ModelingSelectionMode::Object:
        return "selection.mode.object";
    case ModelingSelectionMode::Group:
        return "selection.mode.group";
    }
    return "selection.mode.object";
}

const char* selectionModeFallback(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return "Vertex";
    case ModelingSelectionMode::Edge:
        return "Edge";
    case ModelingSelectionMode::Face:
        return "Face";
    case ModelingSelectionMode::Object:
        return "Object";
    case ModelingSelectionMode::Group:
        return "Group";
    }
    return "Object";
}

const char* activeSelectionKindKey(ModelingActiveSelectionKind kind) {
    switch (kind) {
    case ModelingActiveSelectionKind::None:
        return "selection.active.none";
    case ModelingActiveSelectionKind::Object:
        return "selection.active.object";
    case ModelingActiveSelectionKind::Vertex:
        return "selection.active.vertex";
    case ModelingActiveSelectionKind::Edge:
        return "selection.active.edge";
    case ModelingActiveSelectionKind::Face:
        return "selection.active.face";
    case ModelingActiveSelectionKind::Light:
        return "selection.active.light";
    }
    return "selection.active.none";
}

const char* activeSelectionKindFallback(ModelingActiveSelectionKind kind) {
    switch (kind) {
    case ModelingActiveSelectionKind::None:
        return "None";
    case ModelingActiveSelectionKind::Object:
        return "Object";
    case ModelingActiveSelectionKind::Vertex:
        return "Vertex";
    case ModelingActiveSelectionKind::Edge:
        return "Edge";
    case ModelingActiveSelectionKind::Face:
        return "Face";
    case ModelingActiveSelectionKind::Light:
        return "Light";
    }
    return "None";
}

/**
 * Stores edit lifecycle flags emitted by one Inspector axis input.
 */
struct AxisDragFloatResult {
    bool changed = false;
    bool activated = false;
    bool deactivatedAfterEdit = false;
};

AxisDragFloatResult axisDragFloat(const char* axis,
        ImVec4 color,
        float* value,
        bool mixed,
        const char* mixedLabel,
        float speed,
        float minValue,
        float maxValue,
        float inputWidth) {
    ImGui::PushID(axis);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(axis);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(inputWidth < 0.0f ? inputWidth : std::max(inputWidth, kMinAxisInputWidth));
    AxisDragFloatResult result;
    if (mixed) {
        std::array<char, 64> mixedBuffer{};
        std::snprintf(mixedBuffer.data(), mixedBuffer.size(), "%s", mixedLabel);
        ImGui::BeginDisabled();
        ImGui::InputText("##value", mixedBuffer.data(), mixedBuffer.size(), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
    } else {
        result.changed = ImGui::DragFloat("##value", value, speed, minValue, maxValue, "%.3f");
        result.activated = ImGui::IsItemActivated();
        result.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
    }
    ImGui::PopID();
    return result;
}

AxisDragFloatResult axisDragFloat3(const char* stableId,
        const char* label,
        float values[3],
        const std::array<bool, 3>& mixed,
        const char* mixedLabel,
        const UIStyleTokens& style,
        float speed,
        float minValue = 0.0f,
        float maxValue = 0.0f) {
    AxisDragFloatResult result;
    ImGui::PushID(stableId);
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const ImGuiStyle& imguiStyle = ImGui::GetStyle();
    const float minAxisColumnWidth =
            kAxisTextColumnWidth + imguiStyle.ItemInnerSpacing.x + kMinAxisInputWidth + imguiStyle.CellPadding.x * 2.0f;
    const float labelColumnWidth =
            std::clamp(kTransformLabelColumnWidth, 0.0f, std::max(0.0f, availableWidth - 3.0f * minAxisColumnWidth));
    if (ImGui::BeginTable("##AxisDragFloat3", 4,
                ImGuiTableFlags_SizingStretchProp |
                        ImGuiTableFlags_NoSavedSettings |
                        ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelColumnWidth);
        ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);

        ImGui::TableSetColumnIndex(1);
        const AxisDragFloatResult x = axisDragFloat("X", style.axisX, &values[0], mixed[0], mixedLabel, speed, minValue, maxValue, -FLT_MIN);
        result.changed = result.changed || x.changed;
        result.activated = result.activated || x.activated;
        result.deactivatedAfterEdit = result.deactivatedAfterEdit || x.deactivatedAfterEdit;
        ImGui::TableSetColumnIndex(2);
        const AxisDragFloatResult y = axisDragFloat("Y", style.axisY, &values[1], mixed[1], mixedLabel, speed, minValue, maxValue, -FLT_MIN);
        result.changed = result.changed || y.changed;
        result.activated = result.activated || y.activated;
        result.deactivatedAfterEdit = result.deactivatedAfterEdit || y.deactivatedAfterEdit;
        ImGui::TableSetColumnIndex(3);
        const AxisDragFloatResult z = axisDragFloat("Z", style.axisZ, &values[2], mixed[2], mixedLabel, speed, minValue, maxValue, -FLT_MIN);
        result.changed = result.changed || z.changed;
        result.activated = result.activated || z.activated;
        result.deactivatedAfterEdit = result.deactivatedAfterEdit || z.deactivatedAfterEdit;

        ImGui::EndTable();
    }
    ImGui::PopID();
    return result;
}

bool beginLabeledControlTable(const char* stableId) {
    ImGui::PushID(stableId);
    return ImGui::BeginTable("##InspectorLabeledControl", 2,
            ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_NoSavedSettings |
                    ImGuiTableFlags_NoPadOuterX);
}

void setupLabeledControlColumns() {
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float valueWidth = std::clamp(
            availableWidth * 0.36f,
            kInspectorMinimumValueColumnWidth,
            std::max(kInspectorMinimumValueColumnWidth, availableWidth - 120.0f));
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(
            "Value",
            ImGuiTableColumnFlags_WidthFixed,
            valueWidth);
    ImGui::TableNextRow();
}

void endLabeledControlTable() {
    ImGui::EndTable();
    ImGui::PopID();
}

bool inputTextWithLeftLabel(const char* stableId, const char* label, char* buffer, std::size_t bufferSize, ImGuiInputTextFlags flags) {
    bool changed = false;
    if (beginLabeledControlTable(stableId)) {
        setupLabeledControlColumns();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed = ImGui::InputText("##value", buffer, bufferSize, flags);
        endLabeledControlTable();
    } else {
        ImGui::PopID();
    }
    return changed;
}

bool checkboxWithLeftLabel(const char* stableId, const char* label, bool* value) {
    bool changed = false;
    if (beginLabeledControlTable(stableId)) {
        setupLabeledControlColumns();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        changed = ImGui::Checkbox("##value", value);
        endLabeledControlTable();
    } else {
        ImGui::PopID();
    }
    return changed;
}

void updateContinuousEditTracker(const char* stableId,
        UIContinuousEditTracker& activeEdit,
        EntityId primary,
        std::span<const EntityId> selection,
        std::span<float> values,
        UIContinuousEditResult& result) {
    if (result.shouldApply() || result.activated) {
        result.mergeKey = activeEdit.ensureMergeKey(stableId, primary, selection);
    }
    if (result.activated || result.changed) {
        activeEdit.stage(stableId, primary, selection, std::span<const float>(values.data(), values.size()));
    }
    if (result.deactivatedAfterEdit) {
        const std::vector<float> finalValues =
                activeEdit.finalValues(stableId, primary, selection, std::span<const float>(values.data(), values.size()));
        std::copy(finalValues.begin(), finalValues.end(), values.begin());
        activeEdit.reset();
    } else if (result.deactivated) {
        activeEdit.reset();
    }
}

UIContinuousEditResult dragFloatWithLeftLabel(const char* stableId,
        const char* label,
        float* value,
        UIContinuousEditTracker& activeEdit,
        EntityId primary,
        std::span<const EntityId> selection,
        float speed,
        float minValue,
        float maxValue,
        const char* format = "%.3f") {
    std::array<float, 1> staged{*value};
    activeEdit.applyActiveValues(stableId, primary, selection, std::span<float>(staged.data(), staged.size()));
    *value = staged[0];

    UIContinuousEditResult result;
    if (beginLabeledControlTable(stableId)) {
        setupLabeledControlColumns();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        result.changed = ImGui::DragFloat("##value", value, speed, minValue, maxValue, format);
        result.activated = ImGui::IsItemActivated();
        result.deactivated = ImGui::IsItemDeactivated();
        result.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
        endLabeledControlTable();
    } else {
        ImGui::PopID();
    }

    staged[0] = *value;
    updateContinuousEditTracker(stableId, activeEdit, primary, selection, std::span<float>(staged.data(), staged.size()), result);
    *value = staged[0];
    return result;
}

UIContinuousEditResult colorEdit3WithLeftLabel(const char* stableId,
        const char* label,
        float values[3],
        UIContinuousEditTracker& activeEdit,
        EntityId primary,
        std::span<const EntityId> selection) {
    std::array<float, 3> staged{values[0], values[1], values[2]};
    activeEdit.applyActiveValues(stableId, primary, selection, std::span<float>(staged.data(), staged.size()));
    values[0] = staged[0];
    values[1] = staged[1];
    values[2] = staged[2];

    UIContinuousEditResult result;
    if (beginLabeledControlTable(stableId)) {
        setupLabeledControlColumns();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        result.changed = ImGui::ColorEdit3(
                "##value",
                values,
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
        result.activated = ImGui::IsItemActivated();
        result.deactivated = ImGui::IsItemDeactivated();
        result.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
        endLabeledControlTable();
    } else {
        ImGui::PopID();
    }

    staged = {values[0], values[1], values[2]};
    updateContinuousEditTracker(stableId, activeEdit, primary, selection, std::span<float>(staged.data(), staged.size()), result);
    values[0] = staged[0];
    values[1] = staged[1];
    values[2] = staged[2];
    return result;
}

template <typename Enum>
bool comboWithLeftLabel(const char* stableId,
        const char* label,
        Enum* value,
        const std::vector<std::pair<Enum, std::string>>& options) {
    bool changed = false;
    if (beginLabeledControlTable(stableId)) {
        setupLabeledControlColumns();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        const auto selected = std::find_if(options.begin(), options.end(), [&](const auto& option) {
            return option.first == *value;
        });
        const char* preview = selected == options.end() ? "" : selected->second.c_str();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##value", preview)) {
            for (const auto& [optionValue, optionLabel] : options) {
                const bool isSelected = optionValue == *value;
                if (ImGui::Selectable(optionLabel.c_str(), isSelected)) {
                    *value = optionValue;
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        endLabeledControlTable();
    } else {
        ImGui::PopID();
    }
    return changed;
}

void drawSceneSettingsInspector(
        UIContext& context,
        EntityId entity,
        std::span<const EntityId> selection,
        UIContinuousEditTracker& activeEdit,
        SceneSettingsComponent settings,
        LocalizationService& localization) {
    auto applySettings = [&](SceneSettingsComponent next, std::string mergeKey = {}) {
        next.environment.intensity = std::max(next.environment.intensity, 0.0f);
        next.environment.backgroundIntensity = std::max(next.environment.backgroundIntensity, 0.0f);
        next.camera.aperture = std::max(next.camera.aperture, 0.01f);
        next.camera.shutterSpeed = std::max(next.camera.shutterSpeed, 0.000001f);
        next.camera.sensitivity = std::max(next.camera.sensitivity, 1.0f);
        next.camera.nearPlane = std::max(next.camera.nearPlane, 0.001f);
        next.camera.farPlane = std::max(next.camera.farPlane, next.camera.nearPlane + 0.001f);
        next.postProcess.ambientOcclusionIntensity =
                std::max(next.postProcess.ambientOcclusionIntensity, 0.0f);
        next.postProcess.ambientOcclusionRadius =
                std::max(next.postProcess.ambientOcclusionRadius, 0.0f);
        next.postProcess.bloomStrength = std::max(next.postProcess.bloomStrength, 0.0f);
        (void)context.editor.scene().setSceneSettings(
                entity,
                toApiSceneSettings(next),
                QuaderSceneEditOptions{.mergeKey = std::move(mergeKey)});
    };

    const std::string environmentLabel = localization.text("ui.inspector.environment", "Environment");
    if (ImGui::CollapsingHeader(environmentLabel.c_str())) {
        const std::string presetLabel = localization.text("ui.inspector.environment_preset", "Preset");
        std::string environmentId = settings.environment.environmentId;
        std::vector<std::pair<std::string, std::string>> environmentOptions;
        for (const SceneEnvironmentDescriptor& descriptor : sceneEnvironmentCatalog()) {
            environmentOptions.emplace_back(
                    std::string(descriptor.id),
                    localization.text(std::string(descriptor.labelKey), std::string(descriptor.fallbackLabel)));
        }
        if (comboWithLeftLabel("environment_preset", presetLabel.c_str(), &environmentId, environmentOptions)) {
            settings.environment.environmentId = environmentId;
            applySettings(settings);
        }

        bool enabled = settings.environment.enabled;
        const std::string enabledLabel = localization.text("ui.inspector.enabled", "Enabled");
        if (checkboxWithLeftLabel("environment_enabled", enabledLabel.c_str(), &enabled)) {
            settings.environment.enabled = enabled;
            applySettings(settings);
        }

        bool showBackground = settings.environment.showBackground;
        const std::string backgroundLabel = localization.text("ui.inspector.background", "Background");
        if (checkboxWithLeftLabel("environment_background", backgroundLabel.c_str(), &showBackground)) {
            settings.environment.showBackground = showBackground;
            applySettings(settings);
        }

        float clearColor[3] = {
                settings.environment.clearColor.x,
                settings.environment.clearColor.y,
                settings.environment.clearColor.z,
        };
        const std::string clearColorLabel = localization.text("ui.inspector.clear_color", "Clear Color");
        const UIContinuousEditResult clearColorEdit = colorEdit3WithLeftLabel(
                "environment_clear_color",
                clearColorLabel.c_str(),
                clearColor,
                activeEdit,
                entity,
                selection);
        if (clearColorEdit.shouldApply()) {
            settings.environment.clearColor = {
                    std::clamp(clearColor[0], 0.0f, 1.0f),
                    std::clamp(clearColor[1], 0.0f, 1.0f),
                    std::clamp(clearColor[2], 0.0f, 1.0f),
            };
            applySettings(settings, clearColorEdit.mergeKey);
        }

        float intensity = settings.environment.intensity;
        const std::string intensityLabel = localization.text("ui.inspector.intensity", "Intensity");
        const UIContinuousEditResult intensityEdit = dragFloatWithLeftLabel(
                "environment_intensity",
                intensityLabel.c_str(),
                &intensity,
                activeEdit,
                entity,
                selection,
                0.01f,
                0.0f,
                16.0f,
                "%.3f");
        if (intensityEdit.shouldApply()) {
            settings.environment.intensity = intensity;
            applySettings(settings, intensityEdit.mergeKey);
        }

        float backgroundIntensity = settings.environment.backgroundIntensity;
        const std::string backgroundIntensityLabel =
                localization.text("ui.inspector.background_intensity", "Background Intensity");
        const UIContinuousEditResult backgroundIntensityEdit = dragFloatWithLeftLabel(
                "environment_background_intensity",
                backgroundIntensityLabel.c_str(),
                &backgroundIntensity,
                activeEdit,
                entity,
                selection,
                0.01f,
                0.0f,
                16.0f,
                "%.3f");
        if (backgroundIntensityEdit.shouldApply()) {
            settings.environment.backgroundIntensity = backgroundIntensity;
            applySettings(settings, backgroundIntensityEdit.mergeKey);
        }

        float rotationY = settings.environment.rotationY * RadToDeg;
        const std::string rotationLabel = localization.text("ui.inspector.rotation", "Rotation");
        const UIContinuousEditResult rotationEdit = dragFloatWithLeftLabel(
                "environment_rotation",
                rotationLabel.c_str(),
                &rotationY,
                activeEdit,
                entity,
                selection,
                0.5f,
                -360.0f,
                360.0f,
                "%.2f");
        if (rotationEdit.shouldApply()) {
            settings.environment.rotationY = rotationY * DegToRad;
            applySettings(settings, rotationEdit.mergeKey);
        }
    }

    const std::string cameraLabel = localization.text("ui.inspector.camera", "Camera");
    if (ImGui::CollapsingHeader(cameraLabel.c_str())) {
        float aperture = settings.camera.aperture;
        const std::string apertureLabel = localization.text("ui.inspector.aperture", "Aperture");
        const UIContinuousEditResult apertureEdit = dragFloatWithLeftLabel(
                "camera_aperture",
                apertureLabel.c_str(),
                &aperture,
                activeEdit,
                entity,
                selection,
                0.05f,
                0.01f,
                128.0f,
                "%.2f");
        if (apertureEdit.shouldApply()) {
            settings.camera.aperture = aperture;
            applySettings(settings, apertureEdit.mergeKey);
        }

        float shutterSpeed = settings.camera.shutterSpeed;
        const std::string shutterLabel = localization.text("ui.inspector.shutter_speed", "Shutter");
        const UIContinuousEditResult shutterEdit = dragFloatWithLeftLabel(
                "camera_shutter",
                shutterLabel.c_str(),
                &shutterSpeed,
                activeEdit,
                entity,
                selection,
                0.0001f,
                0.000001f,
                10.0f,
                "%.6f");
        if (shutterEdit.shouldApply()) {
            settings.camera.shutterSpeed = shutterSpeed;
            applySettings(settings, shutterEdit.mergeKey);
        }

        float sensitivity = settings.camera.sensitivity;
        const std::string sensitivityLabel = localization.text("ui.inspector.sensitivity", "ISO");
        const UIContinuousEditResult sensitivityEdit = dragFloatWithLeftLabel(
                "camera_sensitivity",
                sensitivityLabel.c_str(),
                &sensitivity,
                activeEdit,
                entity,
                selection,
                1.0f,
                1.0f,
                51200.0f,
                "%.0f");
        if (sensitivityEdit.shouldApply()) {
            settings.camera.sensitivity = sensitivity;
            applySettings(settings, sensitivityEdit.mergeKey);
        }

        float nearPlane = settings.camera.nearPlane;
        const std::string nearLabel = localization.text("ui.inspector.near_plane", "Near");
        const UIContinuousEditResult nearEdit = dragFloatWithLeftLabel(
                "camera_near",
                nearLabel.c_str(),
                &nearPlane,
                activeEdit,
                entity,
                selection,
                0.001f,
                0.001f,
                10000.0f,
                "%.4f");
        if (nearEdit.shouldApply()) {
            settings.camera.nearPlane = nearPlane;
            applySettings(settings, nearEdit.mergeKey);
        }

        float farPlane = settings.camera.farPlane;
        const std::string farLabel = localization.text("ui.inspector.far_plane", "Far");
        const UIContinuousEditResult farEdit = dragFloatWithLeftLabel(
                "camera_far",
                farLabel.c_str(),
                &farPlane,
                activeEdit,
                entity,
                selection,
                1.0f,
                0.001f,
                1000000.0f,
                "%.2f");
        if (farEdit.shouldApply()) {
            settings.camera.farPlane = farPlane;
            applySettings(settings, farEdit.mergeKey);
        }
    }

    const std::string postProcessLabel = localization.text("ui.inspector.post_process", "Post Process");
    if (ImGui::CollapsingHeader(postProcessLabel.c_str())) {
        bool enabled = settings.postProcess.enabled;
        const std::string enabledLabel = localization.text("ui.inspector.enabled", "Enabled");
        if (checkboxWithLeftLabel("post_process_enabled", enabledLabel.c_str(), &enabled)) {
            settings.postProcess.enabled = enabled;
            applySettings(settings);
        }

        bool antiAliasing = settings.postProcess.antiAliasing;
        const std::string aaLabel = localization.text("ui.inspector.anti_aliasing", "Anti-Aliasing");
        if (checkboxWithLeftLabel("post_process_aa", aaLabel.c_str(), &antiAliasing)) {
            settings.postProcess.antiAliasing = antiAliasing;
            applySettings(settings);
        }

        bool dithering = settings.postProcess.dithering;
        const std::string ditheringLabel = localization.text("ui.inspector.dithering", "Dithering");
        if (checkboxWithLeftLabel("post_process_dithering", ditheringLabel.c_str(), &dithering)) {
            settings.postProcess.dithering = dithering;
            applySettings(settings);
        }

        SceneToneMapping toneMapping = settings.postProcess.toneMapping;
        const std::string toneMappingLabel = localization.text("ui.inspector.tone_mapping", "Tone Mapping");
        const std::vector<std::pair<SceneToneMapping, std::string>> toneMappingOptions{
                {SceneToneMapping::Linear, localization.text("ui.inspector.tone_mapping.linear", "Linear")},
                {SceneToneMapping::AcesLegacy, localization.text("ui.inspector.tone_mapping.aces_legacy", "ACES Legacy")},
                {SceneToneMapping::Aces, localization.text("ui.inspector.tone_mapping.aces", "ACES")},
                {SceneToneMapping::Filmic, localization.text("ui.inspector.tone_mapping.filmic", "Filmic")},
                {SceneToneMapping::DisplayRange, localization.text("ui.inspector.tone_mapping.display_range", "Display Range")},
        };
        if (comboWithLeftLabel("post_process_tone_mapping", toneMappingLabel.c_str(), &toneMapping, toneMappingOptions)) {
            settings.postProcess.toneMapping = toneMapping;
            applySettings(settings);
        }

        bool ambientOcclusion = settings.postProcess.ambientOcclusion;
        const std::string aoLabel = localization.text("ui.inspector.ambient_occlusion", "Ambient Occlusion");
        if (checkboxWithLeftLabel("post_process_ao", aoLabel.c_str(), &ambientOcclusion)) {
            settings.postProcess.ambientOcclusion = ambientOcclusion;
            applySettings(settings);
        }
        if (settings.postProcess.ambientOcclusion) {
            float aoIntensity = settings.postProcess.ambientOcclusionIntensity;
            const std::string aoIntensityLabel =
                    localization.text("ui.inspector.ambient_occlusion_intensity", "AO Intensity");
            const UIContinuousEditResult aoIntensityEdit = dragFloatWithLeftLabel(
                    "post_process_ao_intensity",
                    aoIntensityLabel.c_str(),
                    &aoIntensity,
                    activeEdit,
                    entity,
                    selection,
                    0.01f,
                    0.0f,
                    8.0f,
                    "%.3f");
            if (aoIntensityEdit.shouldApply()) {
                settings.postProcess.ambientOcclusionIntensity = aoIntensity;
                applySettings(settings, aoIntensityEdit.mergeKey);
            }

            float aoRadius = settings.postProcess.ambientOcclusionRadius;
            const std::string aoRadiusLabel = localization.text("ui.inspector.ambient_occlusion_radius", "AO Radius");
            const UIContinuousEditResult aoRadiusEdit = dragFloatWithLeftLabel(
                    "post_process_ao_radius",
                    aoRadiusLabel.c_str(),
                    &aoRadius,
                    activeEdit,
                    entity,
                    selection,
                    0.01f,
                    0.0f,
                    10.0f,
                    "%.3f");
            if (aoRadiusEdit.shouldApply()) {
                settings.postProcess.ambientOcclusionRadius = aoRadius;
                applySettings(settings, aoRadiusEdit.mergeKey);
            }
        }

        bool bloom = settings.postProcess.bloom;
        const std::string bloomLabel = localization.text("ui.inspector.bloom", "Bloom");
        if (checkboxWithLeftLabel("post_process_bloom", bloomLabel.c_str(), &bloom)) {
            settings.postProcess.bloom = bloom;
            applySettings(settings);
        }
        float bloomStrength = settings.postProcess.bloomStrength;
        const std::string bloomStrengthLabel = localization.text("ui.inspector.bloom_strength", "Bloom Strength");
        const UIContinuousEditResult bloomStrengthEdit = dragFloatWithLeftLabel(
                "post_process_bloom_strength",
                bloomStrengthLabel.c_str(),
                &bloomStrength,
                activeEdit,
                entity,
                selection,
                0.01f,
                0.0f,
                1.0f,
                "%.3f");
        if (bloomStrengthEdit.shouldApply()) {
            settings.postProcess.bloomStrength = bloomStrength;
            applySettings(settings, bloomStrengthEdit.mergeKey);
        }
    }
}

/**
 * Represents a Bool Aggregate value used by the ImGui editor presentation layer.
 */
struct BoolAggregate {
    bool hasValue = false;
    bool allTrue = true;
    bool anyTrue = false;

    bool mixed() const {
        return anyTrue && !allTrue;
    }
};

BoolAggregate aggregateVisibility(const SceneDocument& document, const std::vector<EntityId>& selected) {
    BoolAggregate result;
    for (const EntityId id : selected) {
        if (const VisibilityComponent* visibility = document.visibility(id)) {
            result.hasValue = true;
            result.allTrue = result.allTrue && visibility->visible;
            result.anyTrue = result.anyTrue || visibility->visible;
        }
    }
    return result;
}

BoolAggregate aggregateLock(const SceneDocument& document, const std::vector<EntityId>& selected) {
    BoolAggregate result;
    for (const EntityId id : selected) {
        if (const LockComponent* lock = document.lock(id)) {
            result.hasValue = true;
            result.allTrue = result.allTrue && lock->locked;
            result.anyTrue = result.anyTrue || lock->locked;
        }
    }
    return result;
}

bool sameFloat(float a, float b) {
    return std::fabs(a - b) <= 0.0001f;
}

std::array<bool, 3> mixedPositionAxes(const SceneDocument& document,
        const std::vector<EntityId>& selected,
        const TransformComponent& primary) {
    std::array<bool, 3> mixed{false, false, false};
    for (const EntityId id : selected) {
        if (const TransformComponent* transform = document.transform(id)) {
            mixed[0] = mixed[0] || !sameFloat(transform->position.x, primary.position.x);
            mixed[1] = mixed[1] || !sameFloat(transform->position.y, primary.position.y);
            mixed[2] = mixed[2] || !sameFloat(transform->position.z, primary.position.z);
        }
    }
    return mixed;
}

std::array<bool, 3> mixedRotationAxes(const SceneDocument& document,
        const std::vector<EntityId>& selected,
        const TransformComponent& primary) {
    std::array<bool, 3> mixed{false, false, false};
    for (const EntityId id : selected) {
        if (const TransformComponent* transform = document.transform(id)) {
            mixed[0] = mixed[0] || !sameFloat(transform->rotationEuler.x, primary.rotationEuler.x);
            mixed[1] = mixed[1] || !sameFloat(transform->rotationEuler.y, primary.rotationEuler.y);
            mixed[2] = mixed[2] || !sameFloat(transform->rotationEuler.z, primary.rotationEuler.z);
        }
    }
    return mixed;
}

std::array<bool, 3> mixedScaleAxes(const SceneDocument& document,
        const std::vector<EntityId>& selected,
        const TransformComponent& primary) {
    std::array<bool, 3> mixed{false, false, false};
    for (const EntityId id : selected) {
        if (const TransformComponent* transform = document.transform(id)) {
            mixed[0] = mixed[0] || !sameFloat(transform->scale.x, primary.scale.x);
            mixed[1] = mixed[1] || !sameFloat(transform->scale.y, primary.scale.y);
            mixed[2] = mixed[2] || !sameFloat(transform->scale.z, primary.scale.z);
        }
    }
    return mixed;
}

} // namespace

float InspectorPanel::minimumWidth(const UIContext& context) const {
    // UIDockPanel applies setNextWindowMinWidth(context.style.sidebarMinWidth) before opening the window.
    return context.style.sidebarMinWidth;
}

ImVec2 InspectorPanel::panelWindowPadding(const UIContext& context) const {
    return {context.style.windowPadding.x, context.style.windowPadding.y};
}

ImGuiWindowFlags InspectorPanel::windowFlags(const UIContext& context) const {
    return context.style.panelWindowFlags;
}

void InspectorPanel::drawContent(UIContext& context) {
    // UIDockPanel routes hover input through context.requestPanelFocus(id()) before content draws.
    SceneDocument& document = context.document;
    ModelingSessionService& modeling = context.modeling;

    const std::vector<EntityId> selected = document.selectedEntities();
    const ModelingSelectionSummary selection = modeling.selectionSummary();
    LocalizationService& localization = context.localization;
    const std::string mixedLabel = localization.text("ui.inspector.mixed", "Mixed");
    const std::string modePrefix = localization.text("ui.inspector.mode", "Mode");
    const std::string activePrefix = localization.text("ui.inspector.active", "Active");
    const std::string verticesPrefix = localization.text("ui.inspector.vertices", "Vertices");
    const std::string edgesPrefix = localization.text("ui.inspector.edges", "Edges");
    const std::string facesPrefix = localization.text("ui.inspector.faces", "Faces");
    if (selected.empty()) {
        activeTransformEdit_.reset();
        activeContinuousEdit_.reset();
        if (selection.hasSelection) {
            const std::string mode = localization.text(selectionModeKey(selection.mode), selectionModeFallback(selection.mode));
            const std::string active = localization.text(activeSelectionKindKey(selection.activeKind), activeSelectionKindFallback(selection.activeKind));
            ImGui::Text("%s: %s", modePrefix.c_str(), mode.c_str());
            ImGui::Text("%s: %s", activePrefix.c_str(), active.c_str());
            ImGui::Text("%s: %u", verticesPrefix.c_str(), selection.selectedVertexCount);
            ImGui::Text("%s: %u", edgesPrefix.c_str(), selection.selectedEdgeCount);
            ImGui::Text("%s: %u", facesPrefix.c_str(), selection.selectedFaceCount);
        } else {
            const std::string noSelection = localization.text("ui.inspector.no_selection", "No selection");
            ImGui::TextUnformatted(noSelection.c_str());
        }
        return;
    }

    EntityId primary = document.primarySelection();
    if (primary == InvalidEntityId) {
        primary = selected.front();
    }
    if (activeTransformEdit_ &&
            (activeTransformEdit_->primary != primary || activeTransformEdit_->selection != selected)) {
        activeTransformEdit_.reset();
    }
    activeContinuousEdit_.resetIfEntityScopeChanged(primary, std::span<const EntityId>(selected.data(), selected.size()));
    const bool primaryIsSceneRoot = document.isSceneRoot(primary);
    if (isComponentSelectionMode(selection.mode)) {
        const std::string active = localization.text(activeSelectionKindKey(selection.activeKind), activeSelectionKindFallback(selection.activeKind));
        ImGui::Text("%s: %s", activePrefix.c_str(), active.c_str());
        if (selection.selectedVertexCount > 0u) {
            ImGui::Text("%s: %u", verticesPrefix.c_str(), selection.selectedVertexCount);
        }
        if (selection.selectedEdgeCount > 0u) {
            ImGui::Text("%s: %u", edgesPrefix.c_str(), selection.selectedEdgeCount);
        }
        if (selection.selectedFaceCount > 0u) {
            ImGui::Text("%s: %u", facesPrefix.c_str(), selection.selectedFaceCount);
        }
    }

    if (selected.size() == 1u) {
        std::array<char, 128> nameBuffer;
        copyNameToBuffer(document.name(primary), nameBuffer);
        const std::string nameLabel = localization.text("ui.inspector.name", "Name");
        if (primaryIsSceneRoot) {
            (void)inputTextWithLeftLabel("name", nameLabel.c_str(), nameBuffer.data(), nameBuffer.size(),
                    ImGuiInputTextFlags_ReadOnly);
        } else {
            if (inputTextWithLeftLabel("name", nameLabel.c_str(), nameBuffer.data(), nameBuffer.size(),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                (void)context.editor.scene().rename(primary, nameBuffer.data());
            }
        }
    }

    std::vector<EntityId> editableSelected;
    editableSelected.reserve(selected.size());
    for (const EntityId id : selected) {
        if (!document.isSceneRoot(id)) {
            editableSelected.push_back(id);
        }
    }

    const BoolAggregate visibilityAggregate = aggregateVisibility(document, editableSelected);
    if (visibilityAggregate.hasValue) {
        bool hidden = !visibilityAggregate.allTrue;
        const std::string hiddenLabel = localization.text("ui.inspector.hidden", "Hidden");
        if (checkboxWithLeftLabel("hidden", hiddenLabel.c_str(), &hidden)) {
            std::vector<QuaderSceneVisibilityEdit> edits;
            edits.reserve(editableSelected.size());
            for (const EntityId id : editableSelected) {
                if (const VisibilityComponent* current = document.visibility(id)) {
                    edits.push_back(QuaderSceneVisibilityEdit{
                            .entity = id,
                            .before = current->visible,
                            .after = !hidden,
                    });
                }
            }
            (void)context.editor.scene().setVisibility(std::span<const QuaderSceneVisibilityEdit>(edits.data(), edits.size()));
        }
        if (visibilityAggregate.mixed()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", mixedLabel.c_str());
        }
    }

    const BoolAggregate lockAggregate = aggregateLock(document, editableSelected);
    if (lockAggregate.hasValue) {
        bool locked = lockAggregate.allTrue;
        const std::string lockedLabel = localization.text("ui.inspector.locked", "Locked");
        if (checkboxWithLeftLabel("locked", lockedLabel.c_str(), &locked)) {
            std::vector<QuaderSceneLockEdit> edits;
            edits.reserve(editableSelected.size());
            for (const EntityId id : editableSelected) {
                if (const LockComponent* current = document.lock(id)) {
                    edits.push_back(QuaderSceneLockEdit{
                            .entity = id,
                            .before = current->locked,
                            .after = locked,
                    });
                }
            }
            (void)context.editor.scene().setLocks(std::span<const QuaderSceneLockEdit>(edits.data(), edits.size()));
        }
        if (lockAggregate.mixed()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", mixedLabel.c_str());
        }
    }

    const TransformComponent* transform = document.transform(primary);
    if (transform) {
        std::size_t transformCount = 0;
        for (const EntityId id : selected) {
            if (document.transform(id)) {
                ++transformCount;
            }
        }

        const std::string transformLabel = localization.text("ui.inspector.transform", "Transform");
        if (ImGui::CollapsingHeader(transformLabel.c_str())) {
            if (selected.size() > 1u) {
                const std::string spatialEntities = localization.format(
                        "ui.inspector.spatial_entities",
                        "{count} spatial entities",
                        {{"count", std::to_string(transformCount)}});
                ImGui::TextDisabled("%s", spatialEntities.c_str());
            }

            auto transformEditMatches = [&](InspectorTransformEditKind kind) {
                return activeTransformEdit_ &&
                        activeTransformEdit_->kind == kind &&
                        activeTransformEdit_->primary == primary &&
                        activeTransformEdit_->selection == selected;
            };

            auto applyActiveTransformEditValues = [&](InspectorTransformEditKind kind, float values[3]) {
                if (!transformEditMatches(kind)) {
                    return;
                }
                values[0] = activeTransformEdit_->values[0];
                values[1] = activeTransformEdit_->values[1];
                values[2] = activeTransformEdit_->values[2];
            };

            auto stageTransformEdit = [&](InspectorTransformEditKind kind, const float values[3]) {
                activeTransformEdit_ = InspectorTransformEditState{
                        .kind = kind,
                        .primary = primary,
                        .selection = selected,
                        .values = {values[0], values[1], values[2]},
                };
            };

            auto finalTransformEditValues = [&](InspectorTransformEditKind kind, const float values[3]) {
                std::array<float, 3> result{values[0], values[1], values[2]};
                if (transformEditMatches(kind)) {
                    result = activeTransformEdit_->values;
                }
                return result;
            };

            auto refreshPrimaryTransform = [&]() {
                transform = document.transform(primary);
            };

            float position[3] = {transform->position.x, transform->position.y, transform->position.z};
            applyActiveTransformEditValues(InspectorTransformEditKind::Position, position);
            const std::array<bool, 3> positionMixed = mixedPositionAxes(document, selected, *transform);
            const std::string positionLabel = localization.text("ui.inspector.position", "Position");
            const AxisDragFloatResult positionResult =
                    axisDragFloat3("position", positionLabel.c_str(), position, positionMixed, mixedLabel.c_str(), context.style, 0.05f);
            if (positionResult.activated || positionResult.changed) {
                stageTransformEdit(InspectorTransformEditKind::Position, position);
            }
            if (positionResult.deactivatedAfterEdit) {
                const std::array<float, 3> finalPosition =
                        finalTransformEditValues(InspectorTransformEditKind::Position, position);
                const QVec3 nextPosition{finalPosition[0], finalPosition[1], finalPosition[2]};
                activeTransformEdit_.reset();
                SceneTransformDragService transformService;
                (void)transformService.moveSelectedToPrimaryPosition(
                        context.editor,
                        document,
                        primary,
                        nextPosition,
                        "Set Transform");
                refreshPrimaryTransform();
            }

            const QVec3 displayedRotation = positiveEulerRadians(transform->rotationEuler);
            float rotation[3] = {
                    displayedRotation.x * RadToDeg,
                    displayedRotation.y * RadToDeg,
                    displayedRotation.z * RadToDeg,
            };
            applyActiveTransformEditValues(InspectorTransformEditKind::Rotation, rotation);
            const std::array<bool, 3> rotationMixed = mixedRotationAxes(document, selected, *transform);
            const std::string rotationLabel = localization.text("ui.inspector.rotation", "Rotation");
            const AxisDragFloatResult rotationResult =
                    axisDragFloat3("rotation", rotationLabel.c_str(), rotation, rotationMixed, mixedLabel.c_str(), context.style, 0.5f);
            if (rotationResult.activated || rotationResult.changed) {
                stageTransformEdit(InspectorTransformEditKind::Rotation, rotation);
            }
            if (rotationResult.deactivatedAfterEdit) {
                const std::array<float, 3> finalRotation =
                        finalTransformEditValues(InspectorTransformEditKind::Rotation, rotation);
                const QVec3 nextRotation{
                        finalRotation[0] * DegToRad,
                        finalRotation[1] * DegToRad,
                        finalRotation[2] * DegToRad,
                };
                activeTransformEdit_.reset();
                SceneTransformDragService transformService;
                (void)transformService.rotateSelectedToPrimaryEulerAroundSelectionPivot(
                        context.editor,
                        document,
                        context.assets,
                        primary,
                        nextRotation,
                        "Set Transform");
                refreshPrimaryTransform();
            }

            float scale[3] = {transform->scale.x, transform->scale.y, transform->scale.z};
            applyActiveTransformEditValues(InspectorTransformEditKind::Scale, scale);
            const std::array<bool, 3> scaleMixed = mixedScaleAxes(document, selected, *transform);
            const std::string scaleLabel = localization.text("ui.inspector.scale", "Scale");
            const AxisDragFloatResult scaleResult =
                    axisDragFloat3("scale", scaleLabel.c_str(), scale, scaleMixed, mixedLabel.c_str(), context.style, 0.02f, 0.001f, 1000.0f);
            if (scaleResult.activated || scaleResult.changed) {
                stageTransformEdit(InspectorTransformEditKind::Scale, scale);
            }
            if (scaleResult.deactivatedAfterEdit) {
                const std::array<float, 3> finalScale =
                        finalTransformEditValues(InspectorTransformEditKind::Scale, scale);
                activeTransformEdit_.reset();
                SceneTransformDragService transformService;
                (void)transformService.scaleSelectedToPrimaryScaleAroundSelectionPivot(
                        context.editor,
                        document,
                        context.assets,
                        primary,
                        {finalScale[0], finalScale[1], finalScale[2]},
                        "Set Transform");
                refreshPrimaryTransform();
            }
        }
    }

    if (selected.size() == 1u && primaryIsSceneRoot) {
        if (const SceneSettingsComponent* settings = document.sceneSettings(primary)) {
            drawSceneSettingsInspector(
                    context,
                    primary,
                    std::span<const EntityId>(selected.data(), selected.size()),
                    activeContinuousEdit_,
                    *settings,
                    localization);
        }
        return;
    }

    if (selected.size() == 1u) {
        if (const MeshComponent* currentMesh = document.mesh(primary)) {
            MeshComponent mesh = *currentMesh;
            auto applyMeshComponent = [&](MeshComponent next) {
                (void)context.editor.scene().setMeshComponent(primary, toApiMeshComponent(next));
            };

            const std::string exportLabel = localization.text("ui.inspector.export", "Export");
            if (ImGui::CollapsingHeader(exportLabel.c_str())) {
                bool editorOnly = mesh.editorOnly;
                const std::string editorOnlyLabel = localization.text("ui.inspector.editor_only", "Editor Only");
                if (checkboxWithLeftLabel("editor_only", editorOnlyLabel.c_str(), &editorOnly)) {
                    mesh.editorOnly = editorOnly;
                    applyMeshComponent(mesh);
                }
            }

            const std::string lightingLabel = localization.text("ui.inspector.lighting", "Lighting");
            if (ImGui::CollapsingHeader(lightingLabel.c_str())) {
                bool castShadows = mesh.castShadows;
                const std::string castShadowsLabel = localization.text("ui.inspector.cast_shadows", "Cast Shadows");
                if (checkboxWithLeftLabel("cast_shadows", castShadowsLabel.c_str(), &castShadows)) {
                    mesh.castShadows = castShadows;
                    applyMeshComponent(mesh);
                }

                bool doubleSided = mesh.doubleSided;
                const std::string doubleSidedLabel = localization.text("ui.inspector.double_sided", "Double sided");
                if (checkboxWithLeftLabel("double_sided", doubleSidedLabel.c_str(), &doubleSided)) {
                    mesh.doubleSided = doubleSided;
                    applyMeshComponent(mesh);
                }
            }

            const std::string physicsLabel = localization.text("ui.inspector.physics", "Physics");
            if (ImGui::CollapsingHeader(physicsLabel.c_str())) {
                bool generateCollisions = mesh.generateCollisions;
                const std::string generateCollisionsLabel = localization.text("ui.inspector.generate_collisions", "Generate Collisions");
                if (checkboxWithLeftLabel("generate_collisions", generateCollisionsLabel.c_str(), &generateCollisions)) {
                    mesh.generateCollisions = generateCollisions;
                    applyMeshComponent(mesh);
                }

                MeshCollisionType collisionType = mesh.collisionType;
                const std::string collisionTypeLabel = localization.text("ui.inspector.collision_type", "Collision Type");
                const std::vector<std::pair<MeshCollisionType, std::string>> collisionOptions{
                        {MeshCollisionType::Box, localization.text("ui.inspector.collision_type.box", "Box")},
                        {MeshCollisionType::Sphere, localization.text("ui.inspector.collision_type.sphere", "Sphere")},
                        {MeshCollisionType::Cylinder, localization.text("ui.inspector.collision_type.cylinder", "Cylinder")},
                        {MeshCollisionType::ConvexHull, localization.text("ui.inspector.collision_type.convex_hull", "Convex Hull")},
                        {MeshCollisionType::Mesh, localization.text("ui.inspector.collision_type.mesh", "Mesh")},
                };
                if (comboWithLeftLabel("collision_type", collisionTypeLabel.c_str(), &collisionType, collisionOptions)) {
                    mesh.collisionType = collisionType;
                    applyMeshComponent(mesh);
                }

                bool displayCollision = mesh.displayCollision;
                const std::string displayCollisionLabel = localization.text("ui.inspector.display_collision", "Display Collision");
                if (checkboxWithLeftLabel("display_collision", displayCollisionLabel.c_str(), &displayCollision)) {
                    mesh.displayCollision = displayCollision;
                    applyMeshComponent(mesh);
                }

                MeshPhysicsBodyType bodyType = mesh.bodyType;
                const std::string bodyTypeLabel = localization.text("ui.inspector.body_type", "Body type");
                const std::vector<std::pair<MeshPhysicsBodyType, std::string>> bodyOptions{
                        {MeshPhysicsBodyType::StaticBody, localization.text("ui.inspector.body_type.static_body", "Static Body")},
                        {MeshPhysicsBodyType::RigidBody, localization.text("ui.inspector.body_type.rigid_body", "Rigid Body")},
                };
                if (comboWithLeftLabel("body_type", bodyTypeLabel.c_str(), &bodyType, bodyOptions)) {
                    mesh.bodyType = bodyType;
                    applyMeshComponent(mesh);
                }
            }
        }

        if (const LightComponent* currentLight = document.light(primary)) {
            LightComponent light = *currentLight;
            auto applyLightComponent = [&](LightComponent next, std::string mergeKey = {}) {
                next.range = std::max(next.range, 0.001f);
                next.intensity = std::max(next.intensity, 0.0f);
                (void)context.editor.scene().setLightComponent(
                        primary,
                        toApiLightComponent(next),
                        QuaderSceneEditOptions{.mergeKey = std::move(mergeKey)});
            };

            const std::string lightLabel = localization.text("ui.inspector.light", "Light");
            if (ImGui::CollapsingHeader(lightLabel.c_str())) {
                LightType type = light.type;
                const std::string typeLabel = localization.text("ui.inspector.light_type", "Type");
                const std::vector<std::pair<LightType, std::string>> typeOptions{
                        {LightType::Directional, localization.text("ui.inspector.light_type.directional", "Directional")},
                        {LightType::Point, localization.text("ui.inspector.light_type.point", "Point")},
                        {LightType::Spot, localization.text("ui.inspector.light_type.spot", "Spot")},
                };
                if (comboWithLeftLabel("light_type", typeLabel.c_str(), &type, typeOptions)) {
                    light.type = type;
                    applyLightComponent(light);
                }

                float color[3] = {light.color.x, light.color.y, light.color.z};
                const std::string colorLabel = localization.text("ui.inspector.color", "Color");
                const UIContinuousEditResult colorEdit = colorEdit3WithLeftLabel(
                        "light_color",
                        colorLabel.c_str(),
                        color,
                        activeContinuousEdit_,
                        primary,
                        std::span<const EntityId>(selected.data(), selected.size()));
                if (colorEdit.shouldApply()) {
                    light.color = {
                            std::clamp(color[0], 0.0f, 1.0f),
                            std::clamp(color[1], 0.0f, 1.0f),
                            std::clamp(color[2], 0.0f, 1.0f),
                    };
                    applyLightComponent(light, colorEdit.mergeKey);
                }

                float intensity = light.intensity;
                const std::string intensityLabel = localization.text("ui.inspector.intensity", "Intensity");
                const UIContinuousEditResult intensityEdit = dragFloatWithLeftLabel(
                        "light_intensity",
                        intensityLabel.c_str(),
                        &intensity,
                        activeContinuousEdit_,
                        primary,
                        std::span<const EntityId>(selected.data(), selected.size()),
                        0.01f,
                        0.0f,
                        16.0f,
                        "%.3f");
                if (intensityEdit.shouldApply()) {
                    light.intensity = intensity;
                    applyLightComponent(light, intensityEdit.mergeKey);
                }

                if (light.type == LightType::Point || light.type == LightType::Spot) {
                    float range = light.range;
                    const std::string rangeLabel = localization.text("ui.inspector.range", "Range");
                    const UIContinuousEditResult rangeEdit = dragFloatWithLeftLabel(
                            "light_range",
                            rangeLabel.c_str(),
                            &range,
                            activeContinuousEdit_,
                            primary,
                            std::span<const EntityId>(selected.data(), selected.size()),
                            0.1f,
                            0.001f,
                            10000.0f,
                            "%.2f");
                    if (rangeEdit.shouldApply()) {
                        light.range = range;
                        applyLightComponent(light, rangeEdit.mergeKey);
                    }
                }

                bool castShadows = light.castsShadows;
                const std::string castShadowsLabel = localization.text("ui.inspector.cast_shadows", "Cast Shadows");
                if (checkboxWithLeftLabel("light_cast_shadows", castShadowsLabel.c_str(), &castShadows)) {
                    light.castsShadows = castShadows;
                    applyLightComponent(light);
                }
            }
        }
    }

    if (selected.size() > 1u) {
        std::size_t meshCount = 0;
        std::size_t lightCount = 0;
        for (const EntityId id : selected) {
            meshCount += document.mesh(id) ? 1u : 0u;
            lightCount += document.light(id) ? 1u : 0u;
        }

        const std::string componentsLabel = localization.text("ui.inspector.components", "Components");
        if (ImGui::CollapsingHeader(componentsLabel.c_str())) {
            const std::string meshesLabel = localization.text("ui.inspector.meshes", "Meshes");
            const std::string lightsLabel = localization.text("ui.inspector.lights", "Lights");
            ImGui::Text("%s: %llu", meshesLabel.c_str(), static_cast<unsigned long long>(meshCount));
            ImGui::Text("%s: %llu", lightsLabel.c_str(), static_cast<unsigned long long>(lightCount));
        }
    }
}

} // namespace quader
