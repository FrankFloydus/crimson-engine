////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "app/app_frame_clock.hpp"
#include "core/types.hpp"
#include "scene/document/project_session.hpp"
#include "platform/platform_window.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "ui/ui_system.hpp"

#include <filesystem>
#include <memory>

namespace quader {

class QuaderEditorApi;

/**
 * Stores the App Options data contract used by the native application shell.
 */
struct AppOptions {
    bool borderlessWindow = true;
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
    void handleWindowChromeAction(WindowChromeAction action);

    AppFrameClock frameClock_;
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
