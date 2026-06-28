////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "viewport/viewport_input_router.hpp"

namespace quader {

class QuaderEditorApi;
class ModelingSessionService;

/**
 * Stores the Viewport Shortcut Controller Result data contract used by the ImGui editor presentation layer.
 */
struct ViewportShortcutControllerResult {
    bool consumed = false;
    bool commandExecuted = false;
    bool invalidateHover = false;
    bool markOverlayDirty = false;
};

/**
 * Coordinates Viewport Shortcut Controller state transitions for the ImGui editor presentation layer.
 */
class ViewportShortcutController {
public:
    [[nodiscard]] ViewportShortcutControllerResult handleOperationPreviewShortcut(
            const ViewportInputResult& inputResult,
            const ModelingToolSettingsTarget& toolSettingsTarget,
            QuaderEditorApi& editor,
            ModelingSessionService& modeling) const;

    [[nodiscard]] ViewportShortcutControllerResult handleModelingShortcut(
            const ViewportInputResult& inputResult,
            QuaderEditorApi& editor,
            ModelingSessionService& modeling) const;
};

} // namespace quader
