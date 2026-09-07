////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/render_instance_edit_scope.hpp"
#include "render/render_scene_binding.hpp"

#include <cstdint>

namespace quader {

class AssetRegistry;
class SceneDocument;
class ViewportLocalVisibilitySnapshot;
struct RenderSceneSnapshot;

/**
 * Refreshes existing light properties after scalar revisions prove that every
 * other render input is unchanged. Retains mesh/material payloads and placement
 * records; mixed edits and changed scene bindings require normal extraction.
 */
[[nodiscard]] bool try_refresh_render_scene_snapshot_light_properties(
    RenderSceneSnapshot &snapshot, const SceneDocument &document,
    const AssetRegistry &assets, std::uint64_t frame_index = 0,
    RenderSceneBindingKey scene_binding = {},
    std::uint64_t project_material_revision = 0u,
    RenderInstanceEditScope instance_edit_scope = {},
    const ViewportLocalVisibilitySnapshot *local_visibility = nullptr);

} // namespace quader
