#pragma once

#include "ui/UISettings.h"

namespace quader {

class LocalizationService;
class ModelingSessionService;
class UIPanelManager;
class ViewportPanel;

struct MainMenuBarContext {
    bool& quitRequested;
    ModelingSessionService& modeling;
    ViewportPanel* viewportPanel = nullptr;
    UIPanelManager& panels;
    UISettings& settings;
    LocalizationService& localization;
    bool& viewportSettingsPanelOpen;
    bool& toolSettingsPanelOpen;
};

struct MainMenuBarResult {
    bool settingsChanged = false;
};

class FileMenu {
public:
    void draw(MainMenuBarContext& context) const;
};

class EditMenu {
public:
    void draw(MainMenuBarContext& context) const;
};

class ToolsMenu {
public:
    void draw(MainMenuBarContext& context) const;
};

class ShowMenu {
public:
    void draw(MainMenuBarContext& context, MainMenuBarResult& result) const;
};

class ViewMenu {
public:
    void draw(MainMenuBarContext& context, MainMenuBarResult& result) const;
};

class LayoutMenu {
public:
    void draw(MainMenuBarContext& context) const;
};

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
