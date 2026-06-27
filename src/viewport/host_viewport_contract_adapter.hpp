////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_command_ids.hpp"
#include "editor/commands/modeling_command_tool_catalog.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "viewport/viewport_input_types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <editor/metadata/host_contract.hpp>

namespace quader {

/**
 * Represents a Viewport Host Defaults value used by the viewport input, layout, and camera layer.
 */
struct ViewportHostDefaults {
    double mapUnitsPerWorldUnit = 32.0;
    double defaultGridMapUnits = 32.0;
    double defaultOrthographicSize = 24.0;
    double focusOriginDistance = 64.0;
    double defaultFovDegrees = 60.0;
    double orbitRadiansPerPixel = 0.006;
    double flyLookRadiansPerPixel = 0.004;
    double flyBaseSpeed = 6.0;
    double flyFastMultiplier = 4.0;
    double flySlowMultiplier = 0.25;
    double selectionBoxDragThresholdPixels = 4.0;
};

/**
 * Represents one viewport shortcut binding projected from editor metadata into viewport terms.
 */
struct ViewportShortcutBindingDescriptor {
    std::string targetId;
    std::string shortcut;
};

/**
 * Represents one grid preset shortcut binding projected from editor metadata into viewport terms.
 */
struct ViewportGridShortcutBindingDescriptor {
    int preset = 0;
    std::string shortcut;
};

/**
 * Represents one semantic viewport input action binding target.
 */
struct ViewportInputActionBindingDescriptor {
    std::string targetId;
    std::string shortcut;
    std::string_view when;
    int weight = 20;
    ViewportInputActionKind action = ViewportInputActionKind::None;
};

/**
 * Represents one host shortcut target mapped to command and viewport execution terms.
 */
struct ViewportShortcutTargetDescriptor {
    std::string_view contractTargetId;
    std::string_view commandTargetId;
    ViewportShortcut shortcut;
};

inline bool viewportHostContractAvailable() {
    return true;
}

inline std::string viewportShortcutLabel(
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

inline std::string viewportInputActionTargetId(std::string_view suffix) {
    std::string targetId("viewport.");
    targetId.append(suffix);
    return targetId;
}

inline std::string viewportSelectionModeTargetId(std::string_view mode) {
    std::string targetId("viewport.selection_mode.");
    targetId.append(mode);
    return targetId;
}

inline std::string viewportGridPresetTargetId(int preset) {
    return "viewport.grid_preset." + std::to_string(preset);
}

inline ViewportShortcut viewportTargetShortcut(
        ViewportShortcutKind kind,
        ViewportShortcutContext context = ViewportShortcutContext::Viewport) {
    return {
            .kind = kind,
            .context = context,
    };
}

inline ViewportShortcut viewportMirrorTargetShortcut(ViewportMirrorAxis axis) {
    return {
            .kind = ViewportShortcutKind::MirrorAxis,
            .context = ViewportShortcutContext::Viewport,
            .mirrorAxis = axis,
    };
}

inline std::span<const ViewportShortcutTargetDescriptor> viewportShortcutTargetCatalog() {
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
            {"duplicate_meshes", {},
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

inline std::vector<ViewportInputActionBindingDescriptor> viewportInputActionBindingCatalog() {
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

inline bool viewportInputActionForTargetId(std::string_view targetId, ViewportInputActionKind& action) {
    for (const ViewportInputActionBindingDescriptor& descriptor : viewportInputActionBindingCatalog()) {
        if (descriptor.targetId == targetId) {
            action = descriptor.action;
            return true;
        }
    }
    return false;
}

inline std::vector<ViewportShortcutBindingDescriptor> viewportShortcutBindingCatalog() {
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

inline std::vector<ViewportGridShortcutBindingDescriptor> viewportGridShortcutBindingCatalog() {
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

inline std::optional<std::string> viewportModelingCommandTargetIdForHostTarget(std::string_view targetId) {
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

inline std::optional<std::string> viewportCommandTargetIdForShortcutDescriptor(
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

inline ViewportHostDefaults viewportHostDefaults() {
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

inline std::string_view viewportToolContractId(ViewportToolId tool) {
    const ToolDescriptor* descriptor = findViewportToolDescriptor(tool);
    return descriptor != nullptr ? descriptor->id : std::string_view{};
}

inline bool viewportToolHasHostContract(ViewportToolId tool) {
    const std::string_view id = viewportToolContractId(tool);
    return !id.empty() && quader_host_contract::find_tool_descriptor(id) != nullptr;
}

inline bool viewportToolHasNativeController(ViewportToolId tool) {
    return findViewportToolDescriptor(tool) != nullptr && viewportToolHasHostContract(tool);
}

inline bool viewportToolAllowsSelectionFallback(ViewportToolId tool) {
    const ToolDescriptor* descriptor = findViewportToolDescriptor(tool);
    return descriptor != nullptr && descriptor->allowsSelectionFallback;
}

inline int viewportDefaultGridPreset() {
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

inline int viewportClampGridPreset(int preset) {
    const auto presets = quader_host_contract::grid_preset_catalog();
    const int maxPreset = presets.empty() ? 10 : static_cast<int>(presets.size());
    return std::clamp(preset, 1, maxPreset);
}

inline float viewportGridWorldSizeFromContract(int preset) {
    const auto presets = quader_host_contract::grid_preset_catalog();
    if (!presets.empty()) {
        const int clampedPreset = viewportClampGridPreset(preset);
        return static_cast<float>(presets[static_cast<std::size_t>(clampedPreset - 1)].world_units);
    }
    const int clampedPreset = viewportClampGridPreset(preset);
    const float mapUnits = std::pow(2.0f, static_cast<float>(clampedPreset - 1));
    return mapUnits / 32.0f;
}

inline bool viewportToolFromContractId(std::string_view id, ViewportToolId& tool) {
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

inline bool viewportSelectionModeFromContractId(std::string_view id, ViewportSelectionMode& mode) {
    if (id == "vertex") {
        mode = ViewportSelectionMode::Vertex;
        return true;
    }
    if (id == "edge") {
        mode = ViewportSelectionMode::Edge;
        return true;
    }
    if (id == "face") {
        mode = ViewportSelectionMode::Face;
        return true;
    }
    if (id == "object") {
        mode = ViewportSelectionMode::Object;
        return true;
    }
    if (id == "group") {
        mode = ViewportSelectionMode::Group;
        return true;
    }
    return false;
}

inline std::optional<std::string> viewportCommandTargetIdForContractTarget(std::string_view targetId) {
    for (const ViewportShortcutTargetDescriptor& descriptor : viewportShortcutTargetCatalog()) {
        if (!descriptor.contractTargetId.empty() && descriptor.contractTargetId == targetId) {
            return viewportCommandTargetIdForShortcutDescriptor(descriptor);
        }
    }

    ViewportToolId tool = ViewportToolId::Select;
    if (viewportToolFromContractId(targetId, tool)) {
        std::string commandId("tools.activate.");
        commandId.append(viewportToolContractId(tool));
        return commandId;
    }

    ViewportSelectionMode mode = ViewportSelectionMode::Object;
    if (viewportSelectionModeFromContractId(targetId, mode)) {
        return viewportSelectionModeTargetId(viewportSelectionModeId(mode));
    }

    return std::nullopt;
}

inline bool viewportShortcutFromContractTarget(
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

inline bool viewportShortcutForCommandTargetId(
        std::string_view targetId,
        ViewportShortcut& shortcut) {
    for (const ViewportShortcutTargetDescriptor& descriptor : viewportShortcutTargetCatalog()) {
        const std::optional<std::string> commandTarget = viewportCommandTargetIdForShortcutDescriptor(descriptor);
        if (commandTarget && *commandTarget == targetId) {
            shortcut = descriptor.shortcut;
            return true;
        }
    }

    constexpr std::string_view toolPrefix = "tools.activate.";
    if (targetId.starts_with(toolPrefix)) {
        ViewportToolId tool = ViewportToolId::Select;
        if (!viewportToolFromContractId(targetId.substr(toolPrefix.size()), tool)) {
            return false;
        }
        shortcut.kind = ViewportShortcutKind::ActivateTool;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.tool = tool;
        return true;
    }

    constexpr std::string_view selectionModePrefix = "viewport.selection_mode.";
    if (targetId.starts_with(selectionModePrefix)) {
        ViewportSelectionMode mode = ViewportSelectionMode::Object;
        if (!viewportSelectionModeFromContractId(targetId.substr(selectionModePrefix.size()), mode)) {
            return false;
        }
        shortcut.kind = ViewportShortcutKind::SetSelectionMode;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.selectionMode = mode;
        return true;
    }

    constexpr std::string_view gridPresetPrefix = "viewport.grid_preset.";
    if (targetId.starts_with(gridPresetPrefix)) {
        const std::string presetText(targetId.substr(gridPresetPrefix.size()));
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

inline bool viewportTargetRequiresRouterDispatch(std::string_view targetId) {
    return targetId == QuaderCommand::ViewportFrameSelection.string() ||
            targetId == QuaderCommand::ViewportFocusOrigin.string() ||
            targetId == "viewport.cancel_active_tool" ||
            targetId == "viewport.toggle_maximized" ||
            targetId == "viewport.delete_popup" ||
            targetId.starts_with("tools.activate.") ||
            targetId.starts_with("viewport.selection_mode.") ||
            targetId.starts_with("viewport.grid_preset.");
}

inline bool viewportShortcutsMatch(
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

inline std::optional<std::string> viewportCommandTargetIdForRouterShortcut(
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

inline bool viewportRouterShortcutForTargetId(
        std::string_view targetId,
        ViewportShortcut& shortcut) {
    return viewportTargetRequiresRouterDispatch(targetId) &&
            viewportShortcutForCommandTargetId(targetId, shortcut);
}

} // namespace quader
