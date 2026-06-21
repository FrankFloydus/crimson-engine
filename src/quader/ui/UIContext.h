#pragma once

#include "quader/core/Types.h"
#include "quader/math/Types.h"
#include "quader/ui/UIIconSet.h"
#include "quader/ui/UIPanel.h"
#include "quader/viewport/ViewportSettings.h"

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class ModelingSessionService;
class SceneDocument;
class SceneRenderBridge;
class UndoRedoManager;
struct UIStyleTokens;

struct UIContext {
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
    ViewportSettings& viewportSettings;
    Vec2 viewportRelativeMouseDelta = {};
    bool* viewportCursorCaptureRequested = nullptr;
    Vec2* viewportCursorCapturePoint = nullptr;
    UIPanelId focusedPanel = UIPanelId::Viewport;
    UIPanelId previousFocusedPanel = UIPanelId::Viewport;
    UIPanelId* focusedPanelStorage = nullptr;
    bool* panelFocusChanged = nullptr;

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
