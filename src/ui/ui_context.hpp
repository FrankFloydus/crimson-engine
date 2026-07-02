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
 * Stores the UI Context data contract used by the ImGui editor presentation layer.
 */
struct UIContext {
    QuaderEditorApi& editor;
    SceneRenderBridge &render_bridge;
    SceneDocument& document;
    AssetRegistry& assets;
    ModelingSessionService& modeling;
    EditorSelectionService& selection;
    AssetId default_material = kInvalidAssetId;
    bool &quit_requested;
    const UIStyleTokens& style;
    const UIIconSet& icons;
    LocalizationService& localization;
    UISettings& settings;
    ViewportSettings &viewport_settings;
    FrameTiming frame_timing = {};
    RenderFramePerformanceStats render_performance = {};
    QVec2 viewport_relative_mouse_delta = {};
    bool viewport_render_resize_suspended = false;
    bool *viewport_cursor_capture_requested = nullptr;
    QVec2 *viewport_cursor_capture_point = nullptr;
    UIPanelId focused_panel = UIPanelId::Viewport;
    UIPanelId previous_focused_panel = UIPanelId::Viewport;
    UIPanelId *focused_panel_storage = nullptr;
    bool *panel_focus_changed = nullptr;
    bool *command_shortcut_consumed = nullptr;
    bool *settings_changed = nullptr;

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
