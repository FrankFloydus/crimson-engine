////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_api.hpp"
#include "viewport/viewport_workspace_service.hpp"

namespace quader {

class ViewportLocalVisibilityService;

/**
 * Provides runtime-only access to viewport internals without exposing them on the public API.
 */
class QuaderViewportApiRuntimeAccess final {
public:
    [[nodiscard]] static ViewportWorkspaceService& workspace(QuaderViewportApi& api);
    [[nodiscard]] static const ViewportWorkspaceService& workspace(const QuaderViewportApi& api);
    static void bind_scene_workspace(QuaderViewportApi &api,
                                     ViewportWorkspaceService &workspace);
    static void set_scene_rendering_available(QuaderViewportApi &api, bool available);
    static void bind_scene_local_visibility(
        QuaderViewportApi &api, ViewportLocalVisibilityService &service);
};

} // namespace quader
