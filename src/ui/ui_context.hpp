////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"

namespace quader {

class AssetRegistry;
class EditorSelectionService;
class LocalizationService;
class ModelingSessionService;
class QuaderEditorApi;
class SceneDocument;
class UISettings;
class ViewportSettings;

/// Public editor intent exposed to retained Qt widgets.
struct UIIntentContext {
  QuaderEditorApi &editor;
};

/// Read-only document data projected into retained Qt widgets.
struct UIScenePresentationContext {
  const SceneDocument &document;
  const AssetRegistry &assets;
  const ModelingSessionService &modeling;
  const EditorSelectionService &selection;
  AssetId default_material = kInvalidAssetId;
};

/// Narrow mutable owner access used only for inspector transform previews.
struct UIInspectorPreviewContext {
  SceneDocument &document;
  const AssetRegistry &assets;
};

/// Mutable user preference state exposed to settings surfaces.
struct UIPreferencesContext {
  UISettings &ui;
  ViewportSettings &viewport;
  bool *ui_settings_changed = nullptr;
};

/**
 * Per-refresh context. Widgets must not retain this object or its transient
 * pointers; long-lived owners referenced by it remain app-owned.
 */
struct UIContext {
  UIIntentContext intent;
  UIScenePresentationContext scene;
  UIInspectorPreviewContext *inspector_preview = nullptr;
  UIPreferencesContext preferences;
  LocalizationService &localization;
};

} // namespace quader
