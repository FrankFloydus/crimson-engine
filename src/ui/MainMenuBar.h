////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/UISettings.h"

namespace quader {

class LocalizationService;
class ModelingSessionService;
class QuaderEditorApi;
class SceneDocument;
class UIPanelManager;
class ViewportPanel;

/**
 * Stores the Main Menu Bar Context data contract used by the ImGui editor presentation layer.
 */
struct MainMenuBarContext {
    bool& quitRequested;
    QuaderEditorApi& editor;
    SceneDocument& document;
    ModelingSessionService& modeling;
    ViewportPanel* viewportPanel = nullptr;
    UIPanelManager& panels;
    UISettings& settings;
    LocalizationService& localization;
    bool& viewportSettingsPanelOpen;
    bool& toolSettingsPanelOpen;
};

/**
 * Stores the Main Menu Bar Result data contract used by the ImGui editor presentation layer.
 */
struct MainMenuBarResult {
    bool settingsChanged = false;
};

/**
 * Owns File Menu behavior within the ImGui editor presentation layer.
 */
class FileMenu {
public:
    void draw(MainMenuBarContext& context) const;
};

/**
 * Owns Edit Menu behavior within the ImGui editor presentation layer.
 */
class EditMenu {
public:
    void draw(MainMenuBarContext& context) const;
};

/**
 * Owns Tools Menu behavior within the ImGui editor presentation layer.
 */
class ToolsMenu {
public:
    void draw(MainMenuBarContext& context) const;
};

/**
 * Owns Show Menu behavior within the ImGui editor presentation layer.
 */
class ShowMenu {
public:
    void draw(MainMenuBarContext& context, MainMenuBarResult& result) const;
};

/**
 * Owns View Menu behavior within the ImGui editor presentation layer.
 */
class ViewMenu {
public:
    void draw(MainMenuBarContext& context, MainMenuBarResult& result) const;
};

/**
 * Owns Layout Menu behavior within the ImGui editor presentation layer.
 */
class LayoutMenu {
public:
    void draw(MainMenuBarContext& context) const;
};

/**
 * Owns Main Menu Bar behavior within the ImGui editor presentation layer.
 */
class MainMenuBar {
public:
    MainMenuBarResult draw(MainMenuBarContext context) const;

private:
    FileMenu fileMenu_;
    EditMenu editMenu_;
    ToolsMenu toolsMenu_;
    ShowMenu showMenu_;
    ViewMenu viewMenu_;
    LayoutMenu layoutMenu_;
};

} // namespace quader
