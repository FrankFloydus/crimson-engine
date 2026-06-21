#pragma once

#include "assets/AssetRegistry.h"
#include "core/UndoRedoManager.h"
#include "core/Types.h"
#include "document/DocumentCommandService.h"
#include "modeling/ModelingSessionService.h"
#include "platform/PlatformWindow.h"
#include "render/RenderSystem.h"
#include "render/SceneRenderBridge.h"
#include "scene/SceneDocument.h"
#include "ui/UISystem.h"

#include <filesystem>
#include <memory>

namespace quader {

struct AppOptions {
    bool borderlessWindow = false;
};

class App {
public:
    bool initialize(const std::filesystem::path& executablePath, AppOptions options = {});
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
