////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/ViewportInputRouter.h"
#include "viewport/modes/Tool.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>

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

inline bool viewportHostContractAvailable() {
    return true;
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
    case ViewportKey::H:
        return text == "H";
    case ViewportKey::I:
        return text == "I";
    case ViewportKey::M:
        return text == "M";
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
    case ViewportKey::V:
        return text == "V";
    case ViewportKey::X:
        return text == "X";
    case ViewportKey::Y:
        return text == "Y";
    case ViewportKey::Z:
        return text == "Z";
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
    case ViewportKey::Home:
        return text == "Home";
    case ViewportKey::F:
        return text == "F";
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
    case ViewportKey::Escape:
        return text == "Escape";
    case ViewportKey::Delete:
        return text == "Delete";
    case ViewportKey::Backspace:
        return text == "Backspace";
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

inline bool viewportShortcutFromContractTarget(
        std::string_view targetId,
        ViewportShortcut& shortcut) {
    if (targetId == "select_all") {
        shortcut.kind = ViewportShortcutKind::SelectAll;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == "copy_meshes") {
        shortcut.kind = ViewportShortcutKind::Copy;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == "duplicate_meshes") {
        shortcut.kind = ViewportShortcutKind::Duplicate;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == "invert_selection") {
        shortcut.kind = ViewportShortcutKind::InvertSelection;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == "repeat_last_action") {
        shortcut.kind = ViewportShortcutKind::RepeatLastAction;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == "paste_meshes") {
        shortcut.kind = ViewportShortcutKind::Paste;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == "undo") {
        shortcut.kind = ViewportShortcutKind::Undo;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == "redo") {
        shortcut.kind = ViewportShortcutKind::Redo;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == "delete") {
        shortcut.kind = ViewportShortcutKind::DeleteSelection;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == "merge_vertices") {
        shortcut.kind = ViewportShortcutKind::MergeActive;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == "merge_vertices_at_center") {
        shortcut.kind = ViewportShortcutKind::MergeCenter;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == "apply_active_material") {
        shortcut.kind = ViewportShortcutKind::ApplyMaterial;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == "invert_mesh") {
        shortcut.kind = ViewportShortcutKind::FlipMeshNormals;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == "focus_all") {
        shortcut.kind = ViewportShortcutKind::FocusDocument;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == "focus_origin") {
        shortcut.kind = ViewportShortcutKind::FocusOrigin;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == "mirror_axis_x") {
        shortcut.kind = ViewportShortcutKind::MirrorAxis;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.mirrorAxis = ViewportMirrorAxis::X;
        return true;
    }
    if (targetId == "mirror_axis_y") {
        shortcut.kind = ViewportShortcutKind::MirrorAxis;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.mirrorAxis = ViewportMirrorAxis::Y;
        return true;
    }
    if (targetId == "mirror_axis_z") {
        shortcut.kind = ViewportShortcutKind::MirrorAxis;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.mirrorAxis = ViewportMirrorAxis::Z;
        return true;
    }
    if (targetId == "flip_horizontal") {
        shortcut.kind = ViewportShortcutKind::FlipHorizontal;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == "flip_vertical") {
        shortcut.kind = ViewportShortcutKind::FlipVertical;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
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
