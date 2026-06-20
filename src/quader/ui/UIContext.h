#pragma once

#include "quader/core/Types.h"

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class ModelingSessionService;
class SceneDocument;
class SceneRenderBridge;
class UndoRedoManager;
class ViewportCameraController;
class ViewportRenderer;
struct UIStyleTokens;

struct UIContext {
    ViewportRenderer& viewport;
    ViewportCameraController& camera;
    SceneRenderBridge& renderBridge;
    SceneDocument& document;
    AssetRegistry& assets;
    UndoRedoManager& undoRedo;
    DocumentCommandService& documentCommands;
    ModelingSessionService& modeling;
    AssetId defaultMaterial = InvalidAssetId;
    bool& quitRequested;
    const UIStyleTokens& style;
};

} // namespace quader
