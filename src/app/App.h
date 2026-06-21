#pragma once

#include "core/Types.h"
#include "document/ProjectSession.h"
#include "platform/PlatformWindow.h"
#include "render/RenderSystem.h"
#include "render/SceneRenderBridge.h"
#include "ui/UISystem.h"

#include <filesystem>

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
    ProjectSession projectSession_;
    AssetId defaultMaterial_ = InvalidAssetId;
    bool running_ = false;
};

} // namespace quader
