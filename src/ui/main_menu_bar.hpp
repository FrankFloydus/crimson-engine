////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

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
    LocalizationService& localization;
    bool& viewportSettingsPanelOpen;
};

/**
 * Stores the Main Menu Bar Result data contract used by the ImGui editor presentation layer.
 */
struct MainMenuBarResult {
    bool settingsChanged = false;
    bool popupOpen = false;
};

/**
 * Owns File Menu behavior within the ImGui editor presentation layer.
 */
class FileMenu {
public:
    bool draw(MainMenuBarContext& context) const;
};

/**
 * Owns Edit Menu behavior within the ImGui editor presentation layer.
 */
class EditMenu {
public:
    bool draw(MainMenuBarContext& context) const;
};

/**
 * Owns Tools Menu behavior within the ImGui editor presentation layer.
 */
class ToolsMenu {
public:
    bool draw(MainMenuBarContext& context) const;
};

/**
 * Owns Create Menu behavior within the ImGui editor presentation layer.
 */
class CreateMenu {
public:
    bool draw(MainMenuBarContext& context) const;
};

/**
 * Owns View Menu behavior within the ImGui editor presentation layer.
 */
class ViewMenu {
public:
    bool draw(MainMenuBarContext& context, MainMenuBarResult& result) const;
};

/**
 * Owns Layout Menu behavior within the ImGui editor presentation layer.
 */
class LayoutMenu {
public:
    bool draw(MainMenuBarContext& context) const;
};

/**
 * Owns Main Menu Bar behavior within the ImGui editor presentation layer.
 */
class MainMenuBar {
public:
    MainMenuBarResult draw(MainMenuBarContext context) const;
    MainMenuBarResult drawMenus(MainMenuBarContext context) const;

private:
    FileMenu fileMenu_;
    EditMenu editMenu_;
    ToolsMenu toolsMenu_;
    CreateMenu createMenu_;
    ViewMenu viewMenu_;
    LayoutMenu layoutMenu_;
};

} // namespace quader
