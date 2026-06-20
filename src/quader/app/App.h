#pragma once

#include "quader/assets/AssetRegistry.h"
#include "quader/core/UndoRedoManager.h"
#include "quader/core/Types.h"
#include "quader/document/DocumentCommandService.h"
#include "quader/modeling/ModelingSessionService.h"
#include "quader/platform/PlatformWindow.h"
#include "quader/render/RenderSystem.h"
#include "quader/render/SceneRenderBridge.h"
#include "quader/scene/SceneDocument.h"
#include "quader/ui/UISystem.h"

#include <filesystem>
#include <memory>

namespace quader {

class App {
public:
    bool initialize(const std::filesystem::path& executablePath);
    int run();
    void shutdown();

private:
    void createStartupScene();

    PlatformWindow window_;
    RenderSystem renderSystem_;
    SceneRenderBridge renderBridge_;
    UISystem ui_;
    AssetRegistry assets_;
    SceneDocument document_;
    std::unique_ptr<UndoRedoManager> undoRedo_;
    std::unique_ptr<DocumentCommandService> documentCommands_;
    std::unique_ptr<ModelingSessionService> modeling_;
    AssetId defaultMaterial_ = InvalidAssetId;
    bool running_ = false;
};

} // namespace quader
