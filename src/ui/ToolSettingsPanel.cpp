#include "ui/ToolSettingsPanel.h"

#include "core/Logger.h"
#include "core/UndoRedoManager.h"
#include "editor/ModelingPreviewCommandService.h"
#include "modeling/ModelingSessionService.h"

#include <imgui.h>

#include <string>

namespace quader {

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

    const std::string title = target.label.empty() ? std::string("Tool Settings") : target.label;
    ImGui::TextUnformatted(title.c_str());
    ImGui::Separator();

    switch (target.kind) {
    case ModelingToolSettingsTargetKind::Cut: {
        const ModelingToolStatus status = context.modeling.toolStatus();
        int cutMode = static_cast<int>(settings.cutMode);
        ImGui::TextUnformatted("Keep Mode");
        if (ImGui::RadioButton("Discard Left", cutMode == static_cast<int>(ModelingCutMode::DiscardLeft))) {
            cutMode = static_cast<int>(ModelingCutMode::DiscardLeft);
            settings.cutMode = ModelingCutMode::DiscardLeft;
            changed = true;
        }
        if (ImGui::RadioButton("Discard Right", cutMode == static_cast<int>(ModelingCutMode::DiscardRight))) {
            cutMode = static_cast<int>(ModelingCutMode::DiscardRight);
            settings.cutMode = ModelingCutMode::DiscardRight;
            changed = true;
        }
        if (ImGui::RadioButton("Keep Both", cutMode == static_cast<int>(ModelingCutMode::KeepBoth))) {
            cutMode = static_cast<int>(ModelingCutMode::KeepBoth);
            settings.cutMode = ModelingCutMode::KeepBoth;
            changed = true;
        }
        if (settings.cutMode != static_cast<ModelingCutMode>(cutMode)) {
            settings.cutMode = static_cast<ModelingCutMode>(cutMode);
            changed = true;
        }
        if (status.cutActive) {
            ImGui::Text("Preview points: %d", status.cutPointCount);
        }
        break;
    }
    case ModelingToolSettingsTargetKind::EdgeBevel: {
        float width = static_cast<float>(settings.edgeBevelWidth);
        float profile = static_cast<float>(settings.edgeBevelProfile);
        int segments = settings.edgeBevelSegments;
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::DragFloat("Width", &width, 0.05f, 0.0625f, 16384.0f, "%.3f")) {
            settings.edgeBevelWidth = width;
            changed = true;
        }
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderInt("Segments", &segments, 1, 16)) {
            settings.edgeBevelSegments = segments;
            changed = true;
        }
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderFloat("Profile", &profile, 0.0f, 1.0f, "%.2f")) {
            settings.edgeBevelProfile = profile;
            changed = true;
        }
        break;
    }
    case ModelingToolSettingsTargetKind::Bridge: {
        int steps = settings.bridgeSteps;
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderInt("Steps", &steps, 1, 64)) {
            settings.bridgeSteps = steps;
            changed = true;
        }
        break;
    }
    case ModelingToolSettingsTargetKind::Thicken: {
        float thickness = static_cast<float>(settings.thickenThickness);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::DragFloat("Distance", &thickness, 0.01f, -4.0f, 4.0f, "%.3f")) {
            settings.thickenThickness = thickness;
            changed = true;
        }
        break;
    }
    case ModelingToolSettingsTargetKind::SliceQuad: {
        int xSlices = settings.sliceQuadXSlices;
        int ySlices = settings.sliceQuadYSlices;
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderInt("X", &xSlices, 0, 64)) {
            settings.sliceQuadXSlices = xSlices;
            changed = true;
        }
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderInt("Y", &ySlices, 0, 64)) {
            settings.sliceQuadYSlices = ySlices;
            changed = true;
        }
        break;
    }
    case ModelingToolSettingsTargetKind::MergeByDistance: {
        float tolerance = static_cast<float>(settings.mergeByDistanceTolerance);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::DragFloat("Tolerance", &tolerance, 0.001f, 0.0f, 128.0f, "%.4f")) {
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
        if (ImGui::Button("Apply")) {
            const ModelingCommandResult result =
                    ModelingPreviewCommandService::commit(context.undoRedo, context.modeling, std::string("Apply ") + title);
            if (!result.ok) {
                LOG_ERROR("Tool preview apply failed: {}", result.message.empty() ? "Command failed." : result.message);
            }
            open = result.ok ? false : context.modeling.toolSettingsTarget().active;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            const ModelingCommandResult result = ModelingPreviewCommandService::cancel(context.modeling);
            if (!result.ok) {
                LOG_ERROR("Tool preview cancel failed: {}", result.message.empty() ? "Command failed." : result.message);
            }
            open = result.ok ? false : context.modeling.toolSettingsTarget().active;
        }
    }
}

} // namespace quader
