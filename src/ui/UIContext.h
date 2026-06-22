#pragma once

#include "core/Types.h"
#include "math/Types.h"
#include "ui/UIIconSet.h"
#include "ui/UIPanel.h"
#include "viewport/ViewportSettings.h"

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class LocalizationService;
class ModelingSessionService;
class QuaderEditorApi;
class SceneDocument;
class SceneRenderBridge;
class UndoRedoManager;
class UISettings;
struct UIStyleTokens;

struct UIContext {
    QuaderEditorApi& editor;
    SceneRenderBridge& renderBridge;
    SceneDocument& document;
    AssetRegistry& assets;
    UndoRedoManager& undoRedo;
    DocumentCommandService& documentCommands;
    ModelingSessionService& modeling;
    AssetId defaultMaterial = InvalidAssetId;
    bool& quitRequested;
    const UIStyleTokens& style;
    const UIIconSet& icons;
    LocalizationService& localization;
    UISettings& settings;
    ViewportSettings& viewportSettings;
    Vec2 viewportRelativeMouseDelta = {};
    bool* viewportCursorCaptureRequested = nullptr;
    Vec2* viewportCursorCapturePoint = nullptr;
    UIPanelId focusedPanel = UIPanelId::Viewport;
    UIPanelId previousFocusedPanel = UIPanelId::Viewport;
    UIPanelId* focusedPanelStorage = nullptr;
    bool* panelFocusChanged = nullptr;
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
};

} // namespace quader
