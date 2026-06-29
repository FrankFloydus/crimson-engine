////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/frame_timing.hpp"
#include "core/types.hpp"
#include "math/types.hpp"
#include "ui/ui_icon_set.hpp"
#include "ui/ui_panel.hpp"
#include "viewport/viewport_settings.hpp"

namespace quader {

class AssetRegistry;
class EditorSelectionService;
class LocalizationService;
class ModelingSessionService;
class QuaderEditorApi;
class SceneDocument;
class SceneRenderBridge;
class UISettings;
struct UIStyleTokens;

/**
 * Stores the UI Context data contract used by the ImGui editor presentation layer.
 */
struct UIContext {
    QuaderEditorApi& editor;
    SceneRenderBridge& renderBridge;
    SceneDocument& document;
    AssetRegistry& assets;
    ModelingSessionService& modeling;
    EditorSelectionService& selection;
    AssetId defaultMaterial = InvalidAssetId;
    bool& quitRequested;
    const UIStyleTokens& style;
    const UIIconSet& icons;
    LocalizationService& localization;
    UISettings& settings;
    ViewportSettings& viewportSettings;
    FrameTiming frameTiming = {};
    QVec2 viewportRelativeMouseDelta = {};
    bool viewportRenderResizeSuspended = false;
    bool* viewportCursorCaptureRequested = nullptr;
    QVec2* viewportCursorCapturePoint = nullptr;
    UIPanelId focusedPanel = UIPanelId::Viewport;
    UIPanelId previousFocusedPanel = UIPanelId::Viewport;
    UIPanelId* focusedPanelStorage = nullptr;
    bool* panelFocusChanged = nullptr;
    bool* commandShortcutConsumed = nullptr;
    bool* settingsChanged = nullptr;

    void requestPanelFocus(UIPanelId panel) const {
        if (focusedPanelStorage == nullptr) {
            return;
        }
        if (*focusedPanelStorage != panel) {
            *focusedPanelStorage = panel;
            if (panelFocusChanged != nullptr) {
                *panelFocusChanged = true;
            }
        }
    }

    void markCommandShortcutConsumed() const {
        if (commandShortcutConsumed != nullptr) {
            *commandShortcutConsumed = true;
        }
    }
};

} // namespace quader
