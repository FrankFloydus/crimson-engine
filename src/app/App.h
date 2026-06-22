////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "document/ProjectSession.h"
#include "platform/PlatformWindow.h"
#include "render/RenderSystem.h"
#include "render/SceneRenderBridge.h"
#include "ui/UISystem.h"

#include <filesystem>
#include <memory>

namespace quader {

class QuaderEditorApi;

/**
 * Stores the App Options data contract used by the native application shell.
 */
struct AppOptions {
    bool borderlessWindow = false;
};

/**
 * Owns App behavior within the native application shell.
 */
class App {
public:
    App();
    ~App();

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
    std::unique_ptr<QuaderEditorApi> editor_;
    AssetId defaultMaterial_ = InvalidAssetId;
    bool running_ = false;
    bool shutdownComplete_ = false;
};

} // namespace quader
