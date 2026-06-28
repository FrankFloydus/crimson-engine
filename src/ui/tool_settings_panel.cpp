////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/tool_settings_panel.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

#include <imgui.h>

#include <string>

namespace quader {
namespace {

std::string localizedToolSettingsTitle(const LocalizationService& localization, const ModelingToolSettingsTarget& target) {
    if (!target.actionId.empty()) {
        std::string key = "modeling.action.";
        key.append(target.actionId);
        key.append(".label");
        return localization.text(key, target.label);
    }
    if (!target.operationId.empty()) {
        std::string key = "modeling.operation.";
        key.append(target.operationId);
        key.append(".label");
        return localization.text(key, target.label);
    }
    if (!target.label.empty()) {
        switch (target.kind) {
        case ModelingToolSettingsTargetKind::Cut:
            return localization.text("tool_settings.target.cut", target.label);
        case ModelingToolSettingsTargetKind::Mirror:
            return localization.text("tool_settings.target.mirror", target.label);
        case ModelingToolSettingsTargetKind::Bridge:
            return localization.text("tool_settings.target.bridge", target.label);
        case ModelingToolSettingsTargetKind::EdgeBevel:
            return localization.text("tool_settings.target.edge_bevel", target.label);
        case ModelingToolSettingsTargetKind::Thicken:
            return localization.text("tool_settings.target.thicken", target.label);
        case ModelingToolSettingsTargetKind::SliceQuad:
            return localization.text("tool_settings.target.slice_quad", target.label);
        case ModelingToolSettingsTargetKind::MergeByDistance:
            return localization.text("tool_settings.target.merge_by_distance", target.label);
        case ModelingToolSettingsTargetKind::None:
            break;
        }
        return target.label;
    }
    return localization.text("ui.panel.tool_settings", "Tool Settings");
}

} // namespace

void ToolSettingsPanel::draw(UIContext& context, bool& open) {
    const ModelingToolSettingsTarget target = context.modeling.toolSettingsTarget();
    if (!target.active) {
        open = false;
        shouldOpenForActiveTarget_ = false;
        return;
    }
    shouldOpenForActiveTarget_ = !open;
    if (!open) {
        open = true;
    }
    drawFloating(context, open);
    shouldOpenForActiveTarget_ = false;
}

void ToolSettingsPanel::beforeBegin(UIContext& context, bool& open) {
    (void)open;
    if (!shouldOpenForActiveTarget_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(
            {viewport->WorkPos.x + 24.0f, viewport->WorkPos.y + 72.0f},
            ImGuiCond_Always);
}

void ToolSettingsPanel::drawContent(UIContext& context, bool& open) {
    ModelingToolSettingsTarget target = context.modeling.toolSettingsTarget();
    ModelingInteractionSettings settings = context.modeling.interactionSettings();
    bool changed = false;

    auto applySettingsUpdate = [&]() {
        if (!changed) {
            return;
        }
        const ModelingCommandResult result = context.modeling.setInteractionSettings(settings);
        if (!result.ok) {
            LOG_ERROR("Tool settings update failed: {}", result.message.empty() ? "Command failed." : result.message);
        }
        changed = false;
        target = context.modeling.toolSettingsTarget();
    };

    const std::string title = localizedToolSettingsTitle(context.localization, target);
    ImGui::TextUnformatted(title.c_str());
    ImGui::Separator();

    switch (target.kind) {
    case ModelingToolSettingsTargetKind::Cut: {
        const ModelingToolStatus status = context.modeling.toolStatus();
        int cutMode = static_cast<int>(settings.cutMode);
        const std::string keepMode = context.localization.text("tool_settings.cut.keep_mode", "Keep Mode");
        ImGui::TextUnformatted(keepMode.c_str());
        const std::string discardLeft = context.localization.imguiLabel(
                "tool_settings.cut.discard_left",
                "tool_settings.cut.discard_left",
                "Discard Left");
        if (ImGui::RadioButton(discardLeft.c_str(), cutMode == static_cast<int>(ModelingCutMode::DiscardLeft))) {
            cutMode = static_cast<int>(ModelingCutMode::DiscardLeft);
            settings.cutMode = ModelingCutMode::DiscardLeft;
            changed = true;
        }
        const std::string discardRight = context.localization.imguiLabel(
                "tool_settings.cut.discard_right",
                "tool_settings.cut.discard_right",
                "Discard Right");
        if (ImGui::RadioButton(discardRight.c_str(), cutMode == static_cast<int>(ModelingCutMode::DiscardRight))) {
            cutMode = static_cast<int>(ModelingCutMode::DiscardRight);
            settings.cutMode = ModelingCutMode::DiscardRight;
            changed = true;
        }
        const std::string keepBoth = context.localization.imguiLabel(
                "tool_settings.cut.keep_both",
                "tool_settings.cut.keep_both",
                "Keep Both");
        if (ImGui::RadioButton(keepBoth.c_str(), cutMode == static_cast<int>(ModelingCutMode::KeepBoth))) {
            cutMode = static_cast<int>(ModelingCutMode::KeepBoth);
            settings.cutMode = ModelingCutMode::KeepBoth;
            changed = true;
        }
        if (settings.cutMode != static_cast<ModelingCutMode>(cutMode)) {
            settings.cutMode = static_cast<ModelingCutMode>(cutMode);
            changed = true;
        }
        if (status.cutActive) {
            const std::string previewPoints = context.localization.text("tool_settings.cut.preview_points", "Preview points");
            ImGui::Text("%s: %d", previewPoints.c_str(), status.cutPointCount);
        }
        break;
    }
    case ModelingToolSettingsTargetKind::EdgeBevel: {
        float width = static_cast<float>(settings.edgeBevelWidth);
        float profile = static_cast<float>(settings.edgeBevelProfile);
        int segments = settings.edgeBevelSegments;
        ImGui::SetNextItemWidth(180.0f);
        const std::string widthLabel = context.localization.imguiLabel("operation.edge_bevel.width", "operation.edge_bevel.width", "Width");
        if (ImGui::DragFloat(widthLabel.c_str(), &width, 0.05f, 0.0625f, 16384.0f, "%.3f")) {
            settings.edgeBevelWidth = width;
            changed = true;
        }
        ImGui::SetNextItemWidth(180.0f);
        const std::string segmentsLabel =
                context.localization.imguiLabel("operation.edge_bevel.segments", "operation.edge_bevel.segments", "Segments");
        if (ImGui::SliderInt(segmentsLabel.c_str(), &segments, 1, 16)) {
            settings.edgeBevelSegments = segments;
            changed = true;
        }
        ImGui::SetNextItemWidth(180.0f);
        const std::string profileLabel =
                context.localization.imguiLabel("operation.edge_bevel.profile", "operation.edge_bevel.profile", "Profile");
        if (ImGui::SliderFloat(profileLabel.c_str(), &profile, 0.0f, 1.0f, "%.2f")) {
            settings.edgeBevelProfile = profile;
            changed = true;
        }
        break;
    }
    case ModelingToolSettingsTargetKind::Bridge: {
        int steps = settings.bridgeSteps;
        ImGui::SetNextItemWidth(180.0f);
        const std::string stepsLabel = context.localization.imguiLabel("operation.bridge.steps", "operation.bridge.steps", "Steps");
        if (ImGui::SliderInt(stepsLabel.c_str(), &steps, 1, 64)) {
            settings.bridgeSteps = steps;
            changed = true;
        }
        break;
    }
    case ModelingToolSettingsTargetKind::Thicken: {
        float thickness = static_cast<float>(settings.thickenThickness);
        ImGui::SetNextItemWidth(180.0f);
        const std::string distanceLabel =
                context.localization.imguiLabel("operation.thicken.distance", "operation.thicken.distance", "Distance");
        if (ImGui::DragFloat(distanceLabel.c_str(), &thickness, 0.01f, -4.0f, 4.0f, "%.3f")) {
            settings.thickenThickness = thickness;
            changed = true;
        }
        break;
    }
    case ModelingToolSettingsTargetKind::SliceQuad: {
        int xSlices = settings.sliceQuadXSlices;
        int ySlices = settings.sliceQuadYSlices;
        ImGui::SetNextItemWidth(180.0f);
        const std::string xLabel = context.localization.imguiLabel("operation.slice_quad.x_slices", "operation.slice_quad.x_slices", "X");
        if (ImGui::SliderInt(xLabel.c_str(), &xSlices, 0, 64)) {
            settings.sliceQuadXSlices = xSlices;
            changed = true;
        }
        ImGui::SetNextItemWidth(180.0f);
        const std::string yLabel = context.localization.imguiLabel("operation.slice_quad.y_slices", "operation.slice_quad.y_slices", "Y");
        if (ImGui::SliderInt(yLabel.c_str(), &ySlices, 0, 64)) {
            settings.sliceQuadYSlices = ySlices;
            changed = true;
        }
        break;
    }
    case ModelingToolSettingsTargetKind::MergeByDistance: {
        float tolerance = static_cast<float>(settings.mergeByDistanceTolerance);
        ImGui::SetNextItemWidth(180.0f);
        const std::string toleranceLabel =
                context.localization.imguiLabel("operation.merge_by_distance.tolerance", "operation.merge_by_distance.tolerance", "Tolerance");
        if (ImGui::DragFloat(toleranceLabel.c_str(), &tolerance, 0.001f, 0.0f, 128.0f, "%.4f")) {
            settings.mergeByDistanceTolerance = tolerance;
            changed = true;
        }
        break;
    }
    case ModelingToolSettingsTargetKind::None:
        break;
    }

    applySettingsUpdate();
    if (!target.active) {
        open = false;
    }

    if (target.operationPreviewActive) {
        ImGui::Separator();
        const std::string applyLabel = context.localization.imguiLabel("ui.action.apply", "ui.action.apply", "Apply");
        if (ImGui::Button(applyLabel.c_str())) {
            const std::string historyLabel = context.localization.format(
                    "history.apply_operation",
                    "Apply {operation}",
                    {{"operation", title}});
            const QuaderApiVoidResult result = context.editor.modeling().commitPreview(historyLabel);
            if (!result.ok) {
                LOG_ERROR("Tool preview apply failed: {}", result.message.empty() ? "Command failed." : result.message);
            }
            open = result.ok ? false : context.modeling.toolSettingsTarget().active;
        }
        ImGui::SameLine();
        const std::string cancelLabel = context.localization.imguiLabel("ui.action.cancel", "ui.action.cancel", "Cancel");
        if (ImGui::Button(cancelLabel.c_str())) {
            const QuaderApiVoidResult result = context.editor.modeling().cancelPreview();
            if (!result.ok) {
                LOG_ERROR("Tool preview cancel failed: {}", result.message.empty() ? "Command failed." : result.message);
            }
            open = result.ok ? false : context.modeling.toolSettingsTarget().active;
        }
    }
}

} // namespace quader
