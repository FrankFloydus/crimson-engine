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
#include "render/render_performance_stats.hpp"
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
 * Stores the public editor API surface used by UI controls to express user intent.
 */
struct UIIntentContext {
    QuaderEditorApi& editor;
};

/**
 * Stores read-only scene and query data used by UI panels for presentation.
 */
struct UIScenePresentationContext {
    const SceneDocument& document;
    const AssetRegistry& assets;
    const ModelingSessionService& modeling;
    const EditorSelectionService& selection;
    AssetId default_material = kInvalidAssetId;
};

/**
 * Stores the narrow inspector preview owner access kept for transform preview staging.
 */
struct UIInspectorPreviewContext {
    SceneDocument& document;
    const AssetRegistry& assets;
};

/**
 * Stores mutable UI and viewport preference state used by settings surfaces.
 */
struct UIPreferencesContext {
    UISettings& ui;
    ViewportSettings& viewport;
    bool *ui_settings_changed = nullptr;
};

/**
 * Stores viewport-only scene, modeling, and render data used by viewport surfaces.
 */
struct UIViewportSurfaceContext {
    SceneRenderBridge& render_bridge;
    SceneDocument& document;
    AssetRegistry& assets;
    ModelingSessionService& modeling;
    EditorSelectionService& selection;
    AssetId default_material = kInvalidAssetId;
    FrameTiming frame_timing = {};
    RenderFramePerformanceStats render_performance = {};
    QVec2 relative_mouse_delta = {};
    bool render_resize_suspended = false;
    bool *cursor_capture_requested = nullptr;
    QVec2 *cursor_capture_point = nullptr;
};

/**
 * Stores the UI Context data contract used by the ImGui editor presentation layer.
 */
struct UIContext {
    UIIntentContext intent;
    UIScenePresentationContext scene;
    UIInspectorPreviewContext *inspector_preview = nullptr;
    UIViewportSurfaceContext *viewport = nullptr;
    UIPreferencesContext preferences;
    bool &quit_requested;
    const UIStyleTokens& style;
    const UIIconSet& icons;
    LocalizationService& localization;
    UIPanelId focused_panel = UIPanelId::Viewport;
    UIPanelId previous_focused_panel = UIPanelId::Viewport;
    UIPanelId *focused_panel_storage = nullptr;
    bool *panel_focus_changed = nullptr;
    bool *command_shortcut_consumed = nullptr;

    void request_panel_focus(UIPanelId panel) const {
      if (focused_panel_storage == nullptr) {
        return;
      }
      if (*focused_panel_storage != panel) {
        *focused_panel_storage = panel;
        if (panel_focus_changed != nullptr) {
          *panel_focus_changed = true;
        }
      }
    }

    void mark_command_shortcut_consumed() const {
      if (command_shortcut_consumed != nullptr) {
        *command_shortcut_consumed = true;
      }
    }
};

} // namespace quader
