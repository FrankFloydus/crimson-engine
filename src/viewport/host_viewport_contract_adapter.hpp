////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_input_types.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

bool viewportHostContractAvailable();
std::string viewportInputActionTargetId(std::string_view suffix);
std::string viewportSelectionModeTargetId(std::string_view mode);
std::string viewportGridPresetTargetId(int preset);
std::span<const ViewportShortcutTargetDescriptor> viewportShortcutTargetCatalog();
std::vector<ViewportInputActionBindingDescriptor> viewportInputActionBindingCatalog();
bool viewportInputActionForTargetId(std::string_view targetId, ViewportInputActionKind& action);
std::vector<ViewportShortcutBindingDescriptor> viewportShortcutBindingCatalog();
std::vector<ViewportGridShortcutBindingDescriptor> viewportGridShortcutBindingCatalog();
std::optional<std::string> viewportCommandTargetIdForContractTarget(std::string_view targetId);
std::optional<ViewportView> viewportViewForCommandTargetId(std::string_view targetId);
std::optional<std::string_view> viewportCommandTargetIdForView(ViewportView view);
ViewportHostDefaults viewportHostDefaults();
std::string_view viewportToolContractId(ViewportToolId tool);
bool viewportToolHasHostContract(ViewportToolId tool);
bool viewportToolHasNativeController(ViewportToolId tool);
bool viewportToolAllowsSelectionFallback(ViewportToolId tool);
int viewportDefaultGridPreset();
int viewportClampGridPreset(int preset);
float viewportGridWorldSizeFromContract(int preset);
bool viewportToolFromContractId(std::string_view id, ViewportToolId& tool);
bool viewportSelectionModeFromContractId(std::string_view id, ViewportSelectionMode& mode);
bool viewportShortcutFromContractTarget(std::string_view targetId, ViewportShortcut& shortcut);
bool viewportShortcutForCommandTargetId(std::string_view targetId, ViewportShortcut& shortcut);
bool viewportTargetRequiresRouterDispatch(std::string_view targetId);
bool viewportShortcutsMatch(const ViewportShortcut& lhs, const ViewportShortcut& rhs);
std::optional<std::string> viewportCommandTargetIdForRouterShortcut(const ViewportShortcut& shortcut);
std::optional<std::string> viewportOperationPreviewCommandTargetIdForRouterShortcut(const ViewportShortcut& shortcut);
bool viewportRouterShortcutForTargetId(std::string_view targetId, ViewportShortcut& shortcut);

} // namespace quader
