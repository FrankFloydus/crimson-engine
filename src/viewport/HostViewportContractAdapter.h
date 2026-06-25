////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderCommandIds.h"
#include "viewport/ViewportInputRouter.h"
#include "editor/tools/ViewportTool.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <editor/metadata/host_contract.h>

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
    static const std::array<ViewportShortcutTargetDescriptor, 23> descriptors{{
            {"select_all", QuaderCommand::EditSelectAll.string(),
                    viewportTargetShortcut(ViewportShortcutKind::SelectAll, ViewportShortcutContext::Global)},
            {"copy_meshes", QuaderCommand::CopySelection.string(),
                    viewportTargetShortcut(ViewportShortcutKind::Copy, ViewportShortcutContext::Global)},
            {"duplicate_meshes", QuaderCommand::DuplicateSelection.string(),
                    viewportTargetShortcut(ViewportShortcutKind::Duplicate, ViewportShortcutContext::Global)},
            {"invert_selection", QuaderCommand::EditInvertSelection.string(),
                    viewportTargetShortcut(ViewportShortcutKind::InvertSelection, ViewportShortcutContext::Global)},
            {"repeat_last_action", QuaderCommand::EditRepeatLastAction.string(),
                    viewportTargetShortcut(ViewportShortcutKind::RepeatLastAction, ViewportShortcutContext::Global)},
            {"paste_meshes", QuaderCommand::PasteSelection.string(),
                    viewportTargetShortcut(ViewportShortcutKind::Paste, ViewportShortcutContext::Global)},
            {"undo", QuaderCommand::EditUndo.string(),
                    viewportTargetShortcut(ViewportShortcutKind::Undo, ViewportShortcutContext::Global)},
            {"redo", QuaderCommand::EditRedo.string(),
                    viewportTargetShortcut(ViewportShortcutKind::Redo, ViewportShortcutContext::Global)},
            {"delete", QuaderCommand::DeleteSelection.string(),
                    viewportTargetShortcut(ViewportShortcutKind::DeleteSelection, ViewportShortcutContext::Global)},
            {"merge_vertices", QuaderCommand::ModelingMergeVerticesToActive.string(),
                    viewportTargetShortcut(ViewportShortcutKind::MergeActive)},
            {"merge_vertices_at_center", QuaderCommand::ModelingMergeVerticesToCenter.string(),
                    viewportTargetShortcut(ViewportShortcutKind::MergeCenter)},
            {"apply_active_material", QuaderCommand::ApplyMaterial.string(),
                    viewportTargetShortcut(ViewportShortcutKind::ApplyMaterial)},
            {"invert_mesh", QuaderCommand::ModelingInvertMesh.string(),
                    viewportTargetShortcut(ViewportShortcutKind::FlipMeshNormals)},
            {"focus_all", QuaderCommand::ViewportFrameSelection.string(),
                    viewportTargetShortcut(ViewportShortcutKind::FocusDocument)},
            {"focus_origin", QuaderCommand::ViewportFocusOrigin.string(),
                    viewportTargetShortcut(ViewportShortcutKind::FocusOrigin)},
            {"mirror_axis_x", QuaderCommand::ModelingMirrorSelectionX.string(),
                    viewportMirrorTargetShortcut(ViewportMirrorAxis::X)},
            {"mirror_axis_y", QuaderCommand::ModelingMirrorSelectionY.string(),
                    viewportMirrorTargetShortcut(ViewportMirrorAxis::Y)},
            {"mirror_axis_z", QuaderCommand::ModelingMirrorSelectionZ.string(),
                    viewportMirrorTargetShortcut(ViewportMirrorAxis::Z)},
            {"flip_horizontal", QuaderCommand::ModelingFlipHorizontal.string(),
                    viewportTargetShortcut(ViewportShortcutKind::FlipHorizontal)},
            {"flip_vertical", QuaderCommand::ModelingFlipVertical.string(),
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
                    .targetId = viewportInputActionTargetId("navigation.zoom"),
                    .shortcut = "Wheel",
                    .action = ViewportInputActionKind::Zoom,
            },
            {
                    .targetId = viewportInputActionTargetId("navigation.zoom_to_cursor"),
                    .shortcut = "Ctrl+Wheel",
                    .action = ViewportInputActionKind::ZoomToCursor,
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
            .selectionBoxDragThresholdPixels = defaults.selection_box_drag_threshold_pixels,
    };
}

inline std::string_view viewportToolContractId(ViewportToolId tool) {
    switch (tool) {
    case ViewportToolId::Select:
        return "select";
    case ViewportToolId::Move:
        return "move";
    case ViewportToolId::Extend:
        return "extend";
    case ViewportToolId::Extrude:
        return "extrude";
    case ViewportToolId::Rotate:
        return "rotate";
    case ViewportToolId::Scale:
        return "scale";
    case ViewportToolId::Box:
        return "box";
    case ViewportToolId::InsertEdgeLoop:
        return "insert_edge_loop";
    case ViewportToolId::Knife:
        return "knife";
    case ViewportToolId::Cut:
        return "cut";
    case ViewportToolId::Poly:
        return "poly";
    case ViewportToolId::Mirror:
        return "mirror";
    }
    return {};
}

inline bool viewportToolHasHostContract(ViewportToolId tool) {
    const std::string_view id = viewportToolContractId(tool);
    return !id.empty() && quader_host_contract::find_tool_descriptor(id) != nullptr;
}

inline bool viewportToolHasNativeController(ViewportToolId tool) {
    switch (tool) {
    case ViewportToolId::Select:
    case ViewportToolId::Move:
    case ViewportToolId::Extend:
    case ViewportToolId::Extrude:
    case ViewportToolId::Rotate:
    case ViewportToolId::Scale:
        return true;
    case ViewportToolId::Box:
    case ViewportToolId::InsertEdgeLoop:
    case ViewportToolId::Knife:
    case ViewportToolId::Cut:
    case ViewportToolId::Poly:
        return viewportToolHasHostContract(tool);
    case ViewportToolId::Mirror:
        return true;
    }
    return false;
}

inline bool viewportToolAllowsSelectionFallback(ViewportToolId tool) {
    return tool == ViewportToolId::Select ||
            tool == ViewportToolId::Move ||
            tool == ViewportToolId::Extend ||
            tool == ViewportToolId::Extrude ||
            tool == ViewportToolId::Rotate ||
            tool == ViewportToolId::Scale;
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

inline float viewportGridWorldSizeFromContract(int preset) {
    const auto presets = quader_host_contract::grid_preset_catalog();
    if (!presets.empty()) {
        const int clampedPreset = std::clamp(preset, 1, static_cast<int>(presets.size()));
        return static_cast<float>(presets[static_cast<std::size_t>(clampedPreset - 1)].world_units);
    }
    const int clampedPreset = std::clamp(preset, 1, 10);
    const float mapUnits = std::pow(2.0f, static_cast<float>(clampedPreset - 1));
    return mapUnits / 32.0f;
}

inline bool viewportKeyMatchesText(ViewportKey key, std::string_view text) {
    switch (key) {
    case ViewportKey::A:
        return text == "A";
    case ViewportKey::Q:
        return text == "Q";
    case ViewportKey::W:
        return text == "W";
    case ViewportKey::D:
        return text == "D";
    case ViewportKey::E:
        return text == "E";
    case ViewportKey::F:
        return text == "F";
    case ViewportKey::G:
        return text == "G";
    case ViewportKey::H:
        return text == "H";
    case ViewportKey::I:
        return text == "I";
    case ViewportKey::J:
        return text == "J";
    case ViewportKey::K:
        return text == "K";
    case ViewportKey::L:
        return text == "L";
    case ViewportKey::M:
        return text == "M";
    case ViewportKey::N:
        return text == "N";
    case ViewportKey::O:
        return text == "O";
    case ViewportKey::R:
        return text == "R";
    case ViewportKey::S:
        return text == "S";
    case ViewportKey::B:
        return text == "B";
    case ViewportKey::C:
        return text == "C";
    case ViewportKey::P:
        return text == "P";
    case ViewportKey::T:
        return text == "T";
    case ViewportKey::U:
        return text == "U";
    case ViewportKey::V:
        return text == "V";
    case ViewportKey::X:
        return text == "X";
    case ViewportKey::Y:
        return text == "Y";
    case ViewportKey::Z:
        return text == "Z";
    case ViewportKey::Number0:
        return text == "0" || text == "Keypad0";
    case ViewportKey::Number1:
        return text == "1" || text == "Keypad1";
    case ViewportKey::Number2:
        return text == "2" || text == "Keypad2";
    case ViewportKey::Number3:
        return text == "3" || text == "Keypad3";
    case ViewportKey::Number4:
        return text == "4" || text == "Keypad4";
    case ViewportKey::Number5:
        return text == "5" || text == "Keypad5";
    case ViewportKey::Number6:
        return text == "6" || text == "Keypad6";
    case ViewportKey::Number7:
        return text == "7" || text == "Keypad7";
    case ViewportKey::Number8:
        return text == "8" || text == "Keypad8";
    case ViewportKey::Number9:
        return text == "9" || text == "Keypad9";
    case ViewportKey::Home:
        return text == "Home";
    case ViewportKey::End:
        return text == "End";
    case ViewportKey::PageUp:
        return text == "PageUp";
    case ViewportKey::PageDown:
        return text == "PageDown";
    case ViewportKey::Left:
        return text == "Left";
    case ViewportKey::Right:
        return text == "Right";
    case ViewportKey::Up:
        return text == "Up";
    case ViewportKey::Down:
        return text == "Down";
    case ViewportKey::Space:
        return text == "Space";
    case ViewportKey::F1:
        return text == "F1";
    case ViewportKey::F2:
        return text == "F2";
    case ViewportKey::F3:
        return text == "F3";
    case ViewportKey::F4:
        return text == "F4";
    case ViewportKey::F5:
        return text == "F5";
    case ViewportKey::F6:
        return text == "F6";
    case ViewportKey::F7:
        return text == "F7";
    case ViewportKey::F8:
        return text == "F8";
    case ViewportKey::F9:
        return text == "F9";
    case ViewportKey::F10:
        return text == "F10";
    case ViewportKey::F11:
        return text == "F11";
    case ViewportKey::F12:
        return text == "F12";
    case ViewportKey::Escape:
        return text == "Escape";
    case ViewportKey::Delete:
        return text == "Delete";
    case ViewportKey::Backspace:
        return text == "Backspace";
    case ViewportKey::Enter:
        return text == "Enter";
    case ViewportKey::Tab:
        return text == "Tab";
    }
    return false;
}

inline bool viewportShortcutModifiersMatch(
        bool primary,
        bool alt,
        bool shift,
        ViewportModifiers modifiers) {
    return primary == modifiers.primary && alt == modifiers.alt && shift == modifiers.shift;
}

inline bool viewportToolFromContractId(std::string_view id, ViewportToolId& tool) {
    if (id == "select") {
        tool = ViewportToolId::Select;
        return true;
    }
    if (id == "move") {
        tool = ViewportToolId::Move;
        return true;
    }
    if (id == "extend") {
        tool = ViewportToolId::Extend;
        return true;
    }
    if (id == "extrude") {
        tool = ViewportToolId::Extrude;
        return true;
    }
    if (id == "rotate") {
        tool = ViewportToolId::Rotate;
        return true;
    }
    if (id == "scale") {
        tool = ViewportToolId::Scale;
        return true;
    }
    if (id == "box") {
        tool = ViewportToolId::Box;
        return true;
    }
    if (id == "insert_edge_loop" || id == "insert_edge_loop_tool") {
        tool = ViewportToolId::InsertEdgeLoop;
        return true;
    }
    if (id == "knife" || id == "knife_tool") {
        tool = ViewportToolId::Knife;
        return true;
    }
    if (id == "cut" || id == "cut_tool") {
        tool = ViewportToolId::Cut;
        return true;
    }
    if (id == "poly" || id == "poly_tool") {
        tool = ViewportToolId::Poly;
        return true;
    }
    if (id == "mirror" || id == "mirror_tool") {
        tool = ViewportToolId::Mirror;
        return true;
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
            return std::string(descriptor.commandTargetId);
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
        if (descriptor.commandTargetId == targetId) {
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

inline bool resolveViewportShortcutFromHostContract(
        ViewportKey key,
        ViewportModifiers modifiers,
        ViewportShortcut& shortcut) {
    if (!modifiers.primary && !modifiers.alt && !modifiers.shift) {
        const auto presets = quader_host_contract::grid_preset_catalog();
        for (std::size_t index = 0; index < presets.size(); ++index) {
            if (viewportKeyMatchesText(key, presets[index].shortcut)) {
                shortcut.kind = ViewportShortcutKind::SetGridPreset;
                shortcut.context = ViewportShortcutContext::Viewport;
                shortcut.gridPreset = static_cast<int>(index + 1U);
                return true;
            }
        }
    }

    for (const quader_host_contract::HostShortcutDescriptor& descriptor :
            quader_host_contract::shortcut_catalog()) {
        if (!viewportKeyMatchesText(key, descriptor.key) ||
                !viewportShortcutModifiersMatch(
                        descriptor.primary_modifier,
                        descriptor.alt,
                        descriptor.shift,
                        modifiers)) {
            continue;
        }

        ViewportShortcut resolved;
        if (viewportShortcutFromContractTarget(descriptor.target_id, resolved)) {
            shortcut = resolved;
            return true;
        }
    }
    return false;
}

} // namespace quader
