#pragma once

namespace quader {

class UIContext;

enum class UIPanelId {
    Viewport,
    Outliner,
    Inspector,
    Console,
};

inline const char* uiPanelKey(UIPanelId id) {
    switch (id) {
    case UIPanelId::Viewport:
        return "Viewport";
    case UIPanelId::Outliner:
        return "Outliner";
    case UIPanelId::Inspector:
        return "Inspector";
    case UIPanelId::Console:
        return "Console";
    }
    return "Unknown";
}

inline const char* uiPanelLocalizationKey(UIPanelId id) {
    switch (id) {
    case UIPanelId::Viewport:
        return "ui.panel.viewport";
    case UIPanelId::Outliner:
        return "ui.panel.outliner";
    case UIPanelId::Inspector:
        return "ui.panel.inspector";
    case UIPanelId::Console:
        return "ui.panel.console";
    }
    return "ui.panel.unknown";
}

class UIPanel {
public:
    virtual ~UIPanel() = default;

    virtual UIPanelId id() const = 0;
    virtual const char* title() const = 0;
    virtual bool defaultVisible() const { return true; }
    virtual bool canToggle() const { return true; }
    virtual void draw(UIContext& context) = 0;
};

} // namespace quader
