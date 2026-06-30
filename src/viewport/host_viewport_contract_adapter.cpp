////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/host_viewport_contract_adapter.hpp"

#include "api/quader_command_ids.hpp"
#include "core/public_command_targets.hpp"
#include "editor/commands/modeling_command_tool_catalog.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"

#include <editor/metadata/host_contract.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace quader {
namespace {

std::string viewportShortcutLabel(
        bool primary,
        bool shift,
        bool alt,
        std::string_view key) {
    std::string shortcut;
    if (primary) {
        shortcut += "Ctrl+";
    }
    if (shift) {
        shortcut += "Shift+";
    }
    if (alt) {
        shortcut += "Alt+";
    }
    shortcut += key;
    return shortcut;
}

ViewportShortcut viewportTargetShortcut(
        ViewportShortcutKind kind,
        ViewportShortcutContext context = ViewportShortcutContext::Viewport) {
    return {
            .kind = kind,
            .context = context,
    };
}

ViewportShortcut viewportMirrorTargetShortcut(ViewportMirrorAxis axis) {
    return {
            .kind = ViewportShortcutKind::MirrorAxis,
            .context = ViewportShortcutContext::Viewport,
            .mirrorAxis = axis,
    };
}

ViewportShortcut viewportViewTargetShortcut(ViewportView view) {
    return {
            .kind = ViewportShortcutKind::SetView,
            .context = ViewportShortcutContext::Viewport,
            .view = view,
    };
}

std::optional<std::string> viewportModelingCommandTargetIdForHostTarget(std::string_view targetId) {
    for (const CommandTool* tool : modelingCommandTools()) {
        if (tool == nullptr) {
            continue;
        }
        if (tool->id() == targetId || tool->hostShortcutTargetId() == targetId) {
            return tool->publicCommandTargetId();
        }
    }
    return std::nullopt;
}

std::optional<std::string> viewportCommandTargetIdForShortcutDescriptor(
        const ViewportShortcutTargetDescriptor& descriptor) {
    if (!descriptor.contractTargetId.empty()) {
        if (std::optional<std::string> modelingTarget =
                    viewportModelingCommandTargetIdForHostTarget(descriptor.contractTargetId)) {
            return modelingTarget;
        }
    }
    if (!descriptor.commandTargetId.empty()) {
        return std::string(descriptor.commandTargetId);
    }
    return std::nullopt;
}

} // namespace

bool viewportHostContractAvailable() {
    return true;
}

std::string viewportInputActionTargetId(std::string_view suffix) {
    std::string targetId("viewport.");
    targetId.append(suffix);
    return targetId;
}

std::string viewportSelectionModeTargetId(std::string_view mode) {
    return quaderViewportSelectionModeTargetId(mode);
}

std::string viewportGridPresetTargetId(int preset) {
    return quaderViewportGridPresetTargetId(preset);
}

std::span<const ViewportShortcutTargetDescriptor> viewportShortcutTargetCatalog() {
    static const std::array<ViewportShortcutTargetDescriptor, 26> descriptors{{
            {"new_document", QuaderCommand::ProjectNew.string(),
                    viewportTargetShortcut(ViewportShortcutKind::None, ViewportShortcutContext::Global)},
            {"open_document", QuaderCommand::ProjectOpen.string(),
                    viewportTargetShortcut(ViewportShortcutKind::None, ViewportShortcutContext::Global)},
            {"save_document", QuaderCommand::ProjectSave.string(),
                    viewportTargetShortcut(ViewportShortcutKind::None, ViewportShortcutContext::Global)},
            {"select_all", {},
                    viewportTargetShortcut(ViewportShortcutKind::SelectAll, ViewportShortcutContext::Global)},
            {"copy_meshes", {},
                    viewportTargetShortcut(ViewportShortcutKind::Copy, ViewportShortcutContext::Global)},
            {"duplicate_selection", QuaderCommand::DuplicateSelection.string(),
                    viewportTargetShortcut(ViewportShortcutKind::Duplicate, ViewportShortcutContext::Global)},
            {"invert_selection", {},
                    viewportTargetShortcut(ViewportShortcutKind::InvertSelection, ViewportShortcutContext::Global)},
            {"repeat_last_action", {},
                    viewportTargetShortcut(ViewportShortcutKind::RepeatLastAction, ViewportShortcutContext::Global)},
            {"paste_meshes", {},
                    viewportTargetShortcut(ViewportShortcutKind::Paste, ViewportShortcutContext::Global)},
            {"undo", QuaderCommand::EditUndo.string(),
                    viewportTargetShortcut(ViewportShortcutKind::Undo, ViewportShortcutContext::Global)},
            {"redo", QuaderCommand::EditRedo.string(),
                    viewportTargetShortcut(ViewportShortcutKind::Redo, ViewportShortcutContext::Global)},
            {"delete", QuaderCommand::DeleteSelection.string(),
                    viewportTargetShortcut(ViewportShortcutKind::DeleteSelection, ViewportShortcutContext::Global)},
            {"merge_vertices", {},
                    viewportTargetShortcut(ViewportShortcutKind::MergeActive)},
            {"merge_vertices_at_center", {},
                    viewportTargetShortcut(ViewportShortcutKind::MergeCenter)},
            {"apply_active_material", {},
                    viewportTargetShortcut(ViewportShortcutKind::ApplyMaterial)},
            {"invert_mesh", {},
                    viewportTargetShortcut(ViewportShortcutKind::FlipMeshNormals)},
            {"focus_all", QuaderCommand::ViewportFrameSelection.string(),
                    viewportTargetShortcut(ViewportShortcutKind::FocusDocument)},
            {"focus_origin", QuaderCommand::ViewportFocusOrigin.string(),
                    viewportTargetShortcut(ViewportShortcutKind::FocusOrigin)},
            {"mirror_axis_x", {},
                    viewportMirrorTargetShortcut(ViewportMirrorAxis::X)},
            {"mirror_axis_y", {},
                    viewportMirrorTargetShortcut(ViewportMirrorAxis::Y)},
            {"mirror_axis_z", {},
                    viewportMirrorTargetShortcut(ViewportMirrorAxis::Z)},
            {"flip_horizontal", {},
                    viewportTargetShortcut(ViewportShortcutKind::FlipHorizontal)},
            {"flip_vertical", {},
                    viewportTargetShortcut(ViewportShortcutKind::FlipVertical)},
            {"", "viewport.cancel_active_tool", viewportTargetShortcut(ViewportShortcutKind::CancelActiveTool)},
            {"", "viewport.toggle_maximized", viewportTargetShortcut(ViewportShortcutKind::ToggleMaximized)},
            {"", "viewport.delete_popup", viewportTargetShortcut(ViewportShortcutKind::DeletePopup)},
    }};
    return descriptors;
}

std::vector<ViewportInputActionBindingDescriptor> viewportInputActionBindingCatalog() {
    return {
            {
                    .targetId = viewportInputActionTargetId("navigation.orbit"),
                    .shortcut = "MiddleMouseDrag",
                    .action = ViewportInputActionKind::Orbit,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.orbit_snap"),
                    .shortcut = "Alt+MiddleMouseDrag",
                    .action = ViewportInputActionKind::OrbitSnap,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.pan"),
                    .shortcut = "Shift+MiddleMouseDrag",
                    .action = ViewportInputActionKind::Pan,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.fly"),
                    .shortcut = "RightMouseDrag",
                    .action = ViewportInputActionKind::Fly,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.reset_fly_speed"),
                    .shortcut = "MiddleMouse",
                    .action = ViewportInputActionKind::ResetFlySpeed,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.cancel"),
                    .shortcut = "Escape",
                    .when = "viewportFocus && !textInputActive && navigation.active",
                    .weight = 30,
                    .action = ViewportInputActionKind::CancelNavigation,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.zoom"),
                    .shortcut = "Wheel",
                    .action = ViewportInputActionKind::Zoom,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.zoom_to_cursor"),
                    .shortcut = "Ctrl+Wheel",
                    .action = ViewportInputActionKind::ZoomToCursor,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.fly_forward"),
                    .shortcut = "W",
                    .when = "viewportFocus && !textInputActive && navigation.flyActive",
                    .action = ViewportInputActionKind::FlyForward,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.fly_backward"),
                    .shortcut = "S",
                    .when = "viewportFocus && !textInputActive && navigation.flyActive",
                    .action = ViewportInputActionKind::FlyBackward,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.fly_right"),
                    .shortcut = "D",
                    .when = "viewportFocus && !textInputActive && navigation.flyActive",
                    .action = ViewportInputActionKind::FlyRight,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.fly_left"),
                    .shortcut = "A",
                    .when = "viewportFocus && !textInputActive && navigation.flyActive",
                    .action = ViewportInputActionKind::FlyLeft,
            },
            {
                    .targetId = viewportInputActionTargetId("modal_tool.finalize"),
                    .shortcut = "Space",
                    .when = "viewportFocus && !textInputActive && modalTool.active && !navigation.active",
                    .weight = 30,
                    .action = ViewportInputActionKind::ModalToolFinalize,
            },
            {
                    .targetId = viewportInputActionTargetId("modal_tool.finalize"),
                    .shortcut = "Enter",
                    .when = "viewportFocus && !textInputActive && modalTool.active && !navigation.active",
                    .weight = 30,
                    .action = ViewportInputActionKind::ModalToolFinalize,
            },
            {
                    .targetId = viewportInputActionTargetId("modal_tool.mode_cycle"),
                    .shortcut = "F",
                    .when = "viewportFocus && !textInputActive && modalTool.active && !navigation.active",
                    .weight = 30,
                    .action = ViewportInputActionKind::ModalToolModeCycle,
            },
    };
}

bool viewportInputActionForTargetId(std::string_view targetId, ViewportInputActionKind& action) {
    for (const ViewportInputActionBindingDescriptor& descriptor : viewportInputActionBindingCatalog()) {
        if (descriptor.targetId == targetId) {
            action = descriptor.action;
            return true;
        }
    }
    return false;
}

std::vector<ViewportShortcutBindingDescriptor> viewportShortcutBindingCatalog() {
    std::vector<ViewportShortcutBindingDescriptor> bindings;
    for (const quader_host_contract::HostShortcutDescriptor& descriptor :
            quader_host_contract::shortcut_catalog()) {
        bindings.push_back({
                .targetId = std::string(descriptor.target_id),
                .shortcut = viewportShortcutLabel(
                        descriptor.primary_modifier,
                        descriptor.shift,
                        descriptor.alt,
                        descriptor.key),
        });
    }
    return bindings;
}

std::vector<ViewportGridShortcutBindingDescriptor> viewportGridShortcutBindingCatalog() {
    std::vector<ViewportGridShortcutBindingDescriptor> bindings;
    const auto presets = quader_host_contract::grid_preset_catalog();
    bindings.reserve(presets.size());
    for (std::size_t index = 0u; index < presets.size(); ++index) {
        if (presets[index].shortcut.empty()) {
            continue;
        }
        bindings.push_back({
                .preset = static_cast<int>(index + 1u),
                .shortcut = std::string(presets[index].shortcut),
        });
    }
    return bindings;
}

std::optional<ViewportView> viewportViewForCommandTargetId(std::string_view targetId) {
    if (targetId == QuaderCommand::ViewportViewPerspective.string()) {
        return ViewportView::Perspective;
    }
    if (targetId == QuaderCommand::ViewportViewTop.string()) {
        return ViewportView::Top;
    }
    if (targetId == QuaderCommand::ViewportViewBottom.string()) {
        return ViewportView::Bottom;
    }
    if (targetId == QuaderCommand::ViewportViewLeft.string()) {
        return ViewportView::Left;
    }
    if (targetId == QuaderCommand::ViewportViewRight.string()) {
        return ViewportView::Side;
    }
    if (targetId == QuaderCommand::ViewportViewFront.string()) {
        return ViewportView::Front;
    }
    if (targetId == QuaderCommand::ViewportViewBack.string()) {
        return ViewportView::Back;
    }
    return std::nullopt;
}

std::optional<std::string_view> viewportCommandTargetIdForView(ViewportView view) {
    switch (view) {
    case ViewportView::Perspective:
        return QuaderCommand::ViewportViewPerspective.string();
    case ViewportView::Top:
        return QuaderCommand::ViewportViewTop.string();
    case ViewportView::Bottom:
        return QuaderCommand::ViewportViewBottom.string();
    case ViewportView::Left:
        return QuaderCommand::ViewportViewLeft.string();
    case ViewportView::Side:
        return QuaderCommand::ViewportViewRight.string();
    case ViewportView::Front:
        return QuaderCommand::ViewportViewFront.string();
    case ViewportView::Back:
        return QuaderCommand::ViewportViewBack.string();
    }
    return std::nullopt;
}

ViewportHostDefaults viewportHostDefaults() {
    const quader_host_contract::HostViewportDefaultsDescriptor defaults =
            quader_host_contract::viewport_defaults();
    return {
            .mapUnitsPerWorldUnit = defaults.map_units_per_world_unit,
            .defaultGridMapUnits = defaults.default_grid_map_units,
            .defaultOrthographicSize = defaults.default_orthographic_size,
            .focusOriginDistance = defaults.focus_origin_distance,
            .defaultFovDegrees = defaults.default_fov_degrees,
            .orbitRadiansPerPixel = defaults.orbit_radians_per_pixel,
            .flyLookRadiansPerPixel = defaults.fly_look_radians_per_pixel,
            .flyBaseSpeed = defaults.fly_base_speed,
            .flyFastMultiplier = defaults.fly_fast_multiplier,
            .flySlowMultiplier = defaults.fly_slow_multiplier,
            .selectionBoxDragThresholdPixels = defaults.selection_box_drag_threshold_pixels,
    };
}

std::string_view viewportToolContractId(ViewportToolId tool) {
    const ToolDescriptor* descriptor = findViewportToolDescriptor(tool);
    return descriptor != nullptr ? descriptor->id : std::string_view{};
}

bool viewportToolHasHostContract(ViewportToolId tool) {
    const std::string_view id = viewportToolContractId(tool);
    return !id.empty() && quader_host_contract::find_tool_descriptor(id) != nullptr;
}

bool viewportToolHasNativeController(ViewportToolId tool) {
    return findViewportToolDescriptor(tool) != nullptr && viewportToolHasHostContract(tool);
}

bool viewportToolAllowsSelectionFallback(ViewportToolId tool) {
    const ToolDescriptor* descriptor = findViewportToolDescriptor(tool);
    return descriptor != nullptr && descriptor->allowsSelectionFallback;
}

int viewportDefaultGridPreset() {
    const auto presets = quader_host_contract::grid_preset_catalog();
    for (std::size_t index = 0; index < presets.size(); ++index) {
        if (presets[index].default_preset) {
            return static_cast<int>(index + 1U);
        }
    }

    const double defaultMapUnits = quader_host_contract::viewport_defaults().default_grid_map_units;
    for (std::size_t index = 0; index < presets.size(); ++index) {
        if (std::abs(presets[index].map_units - defaultMapUnits) <= 0.0001) {
            return static_cast<int>(index + 1U);
        }
    }
    return 6;
}

int viewportClampGridPreset(int preset) {
    const auto presets = quader_host_contract::grid_preset_catalog();
    const int maxPreset = presets.empty() ? 10 : static_cast<int>(presets.size());
    return std::clamp(preset, 1, maxPreset);
}

float viewportGridWorldSizeFromContract(int preset) {
    const auto presets = quader_host_contract::grid_preset_catalog();
    if (!presets.empty()) {
        const int clampedPreset = viewportClampGridPreset(preset);
        return static_cast<float>(presets[static_cast<std::size_t>(clampedPreset - 1)].world_units);
    }
    const int clampedPreset = viewportClampGridPreset(preset);
    const float mapUnits = std::pow(2.0f, static_cast<float>(clampedPreset - 1));
    return mapUnits / 32.0f;
}

bool viewportToolFromContractId(std::string_view id, ViewportToolId& tool) {
    if (const ToolDescriptor* descriptor = findViewportToolDescriptor(id)) {
        tool = descriptor->tool;
        return true;
    }
    if (const quader_host_contract::HostActionDescriptor* descriptor =
                quader_host_contract::find_action_descriptor(id);
            descriptor != nullptr &&
                    descriptor->kind == quader_host_contract::HostActionKind::tool &&
                    !descriptor->tool_id.empty()) {
        return viewportToolFromContractId(descriptor->tool_id, tool);
    }
    return false;
}

bool viewportSelectionModeFromContractId(std::string_view id, ViewportSelectionMode& mode) {
    if (const std::optional<quader_host_contract::HostSelectionMode> hostMode = hostSelectionModeFromId(id)) {
        mode = hostSelectionModeToViewport(*hostMode);
        return true;
    }
    return false;
}

std::optional<std::string> viewportCommandTargetIdForContractTarget(std::string_view targetId) {
    for (const ViewportShortcutTargetDescriptor& descriptor : viewportShortcutTargetCatalog()) {
        if (!descriptor.contractTargetId.empty() && descriptor.contractTargetId == targetId) {
            return viewportCommandTargetIdForShortcutDescriptor(descriptor);
        }
    }

    ViewportToolId tool = ViewportToolId::Select;
    if (viewportToolFromContractId(targetId, tool)) {
        return quaderToolActivationCommandTargetId(viewportToolContractId(tool));
    }

    ViewportSelectionMode mode = ViewportSelectionMode::Object;
    if (viewportSelectionModeFromContractId(targetId, mode)) {
        return viewportSelectionModeTargetId(viewportSelectionModeId(mode));
    }

    return std::nullopt;
}

bool viewportShortcutFromContractTarget(
        std::string_view targetId,
        ViewportShortcut& shortcut) {
    for (const ViewportShortcutTargetDescriptor& descriptor : viewportShortcutTargetCatalog()) {
        if (!descriptor.contractTargetId.empty() && descriptor.contractTargetId == targetId) {
            shortcut = descriptor.shortcut;
            return true;
        }
    }

    ViewportToolId tool = ViewportToolId::Select;
    if (viewportToolFromContractId(targetId, tool)) {
        shortcut.kind = ViewportShortcutKind::ActivateTool;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.tool = tool;
        return true;
    }

    ViewportSelectionMode mode = ViewportSelectionMode::Object;
    if (viewportSelectionModeFromContractId(targetId, mode)) {
        shortcut.kind = ViewportShortcutKind::SetSelectionMode;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.selectionMode = mode;
        return true;
    }

    return false;
}

bool viewportShortcutForCommandTargetId(
        std::string_view targetId,
        ViewportShortcut& shortcut) {
    if (const std::optional<ViewportView> view = viewportViewForCommandTargetId(targetId)) {
        shortcut = viewportViewTargetShortcut(*view);
        return true;
    }

    for (const ViewportShortcutTargetDescriptor& descriptor : viewportShortcutTargetCatalog()) {
        const std::optional<std::string> commandTarget = viewportCommandTargetIdForShortcutDescriptor(descriptor);
        if (commandTarget && *commandTarget == targetId) {
            shortcut = descriptor.shortcut;
            return true;
        }
    }

    if (const std::optional<std::string_view> toolId = quaderToolIdFromActivationCommandTarget(targetId)) {
        ViewportToolId tool = ViewportToolId::Select;
        if (!viewportToolFromContractId(*toolId, tool)) {
            return false;
        }
        shortcut.kind = ViewportShortcutKind::ActivateTool;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.tool = tool;
        return true;
    }

    if (const std::optional<std::string_view> selectionModeId = quaderSelectionModeIdFromCommandTarget(targetId)) {
        ViewportSelectionMode mode = ViewportSelectionMode::Object;
        if (!viewportSelectionModeFromContractId(*selectionModeId, mode)) {
            return false;
        }
        shortcut.kind = ViewportShortcutKind::SetSelectionMode;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.selectionMode = mode;
        return true;
    }

    if (const std::optional<std::string_view> gridPresetId = quaderGridPresetIdFromCommandTarget(targetId)) {
        const std::string presetText(*gridPresetId);
        try {
            shortcut.kind = ViewportShortcutKind::SetGridPreset;
            shortcut.context = ViewportShortcutContext::Viewport;
            shortcut.gridPreset = std::stoi(presetText);
            return true;
        } catch (...) {
            return false;
        }
    }

    return false;
}

bool viewportTargetRequiresRouterDispatch(std::string_view targetId) {
    return viewportViewForCommandTargetId(targetId).has_value() ||
            targetId == QuaderCommand::ViewportFrameSelection.string() ||
            targetId == QuaderCommand::ViewportFocusOrigin.string() ||
            targetId == "viewport.cancel_active_tool" ||
            targetId == "viewport.toggle_maximized" ||
            targetId == "viewport.delete_popup" ||
            quaderToolIdFromActivationCommandTarget(targetId).has_value() ||
            quaderSelectionModeIdFromCommandTarget(targetId).has_value() ||
            quaderGridPresetIdFromCommandTarget(targetId).has_value();
}

bool viewportShortcutsMatch(
        const ViewportShortcut& lhs,
        const ViewportShortcut& rhs) {
    if (lhs.kind != rhs.kind || lhs.context != rhs.context) {
        return false;
    }

    switch (lhs.kind) {
    case ViewportShortcutKind::ActivateTool:
        return lhs.tool == rhs.tool;
    case ViewportShortcutKind::SetSelectionMode:
        return lhs.selectionMode == rhs.selectionMode;
    case ViewportShortcutKind::SetGridPreset:
        return lhs.gridPreset == rhs.gridPreset;
    case ViewportShortcutKind::SetView:
        return lhs.view == rhs.view;
    case ViewportShortcutKind::MirrorAxis:
        return lhs.mirrorAxis == rhs.mirrorAxis;
    case ViewportShortcutKind::None:
    case ViewportShortcutKind::FocusDocument:
    case ViewportShortcutKind::FocusOrigin:
    case ViewportShortcutKind::ToggleMaximized:
    case ViewportShortcutKind::CancelActiveTool:
    case ViewportShortcutKind::DeletePopup:
    case ViewportShortcutKind::DeleteSelection:
    case ViewportShortcutKind::SelectAll:
    case ViewportShortcutKind::InvertSelection:
    case ViewportShortcutKind::Copy:
    case ViewportShortcutKind::Paste:
    case ViewportShortcutKind::Duplicate:
    case ViewportShortcutKind::Undo:
    case ViewportShortcutKind::Redo:
    case ViewportShortcutKind::RepeatLastAction:
    case ViewportShortcutKind::MergeActive:
    case ViewportShortcutKind::MergeCenter:
    case ViewportShortcutKind::FlipMeshNormals:
    case ViewportShortcutKind::FlipHorizontal:
    case ViewportShortcutKind::FlipVertical:
    case ViewportShortcutKind::ApplyMaterial:
        return true;
    }
    return false;
}

std::optional<std::string> viewportCommandTargetIdForRouterShortcut(
        const ViewportShortcut& shortcut) {
    for (const ViewportShortcutTargetDescriptor& descriptor : viewportShortcutTargetCatalog()) {
        const std::optional<std::string> commandTarget = viewportCommandTargetIdForShortcutDescriptor(descriptor);
        if (!commandTarget ||
                viewportTargetRequiresRouterDispatch(*commandTarget) ||
                !viewportShortcutsMatch(shortcut, descriptor.shortcut)) {
            continue;
        }
        return *commandTarget;
    }
    return std::nullopt;
}

std::optional<std::string> viewportOperationPreviewCommandTargetIdForRouterShortcut(
        const ViewportShortcut& shortcut) {
    switch (shortcut.kind) {
    case ViewportShortcutKind::ToggleMaximized:
        return std::string(QuaderCommand::ModelingApplyActivePreview.string());
    case ViewportShortcutKind::CancelActiveTool:
        return std::string(QuaderCommand::ModelingCancelActivePreview.string());
    default:
        return std::nullopt;
    }
}

bool viewportRouterShortcutForTargetId(
        std::string_view targetId,
        ViewportShortcut& shortcut) {
    return viewportTargetRequiresRouterDispatch(targetId) &&
            viewportShortcutForCommandTargetId(targetId, shortcut);
}

} // namespace quader
