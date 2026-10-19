////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/crimson/crimson_scene_projection.hpp"
#include "render/crimson/crimson_editor_projection.hpp"
#include "render/render_host.hpp"

#include <memory>
#include <string>
#include <vector>

namespace quader {

/** Captures the editor input accepted with one pane's published scene. */
struct CrimsonEditorViewDiagnostics final {
  std::uint8_t layer_mask = 0u;
  std::uintptr_t frame_identity = 0u;
  std::uint64_t frame_revision = 0u;
  std::uint64_t scene_generation = 0u;
  std::size_t ordinary_masks = 0u;
  std::size_t active_masks = 0u;
  crimson::shared::Float4 ordinary_color{};
  crimson::shared::Float4 active_color{};
};

/** Internal migration evidence; this does not change public feature policy. */
struct CrimsonRenderDiagnostics final {
  std::uint64_t frame_submissions = 0u;
  std::uint64_t accepted_scene_generation = 0u;
  std::uint64_t pending_scene_generation = 0u;
  std::uint64_t accepted_editor_scene_generation = 0u;
  std::size_t active_views = 0u;
  bool editor_overlays_pending = true;
  bool selection_depth_accelerator_pending = true;
  CrimsonSceneProjectionStats translation;
  CrimsonEditorProjectionCounters editor_projection;
  crimson::shared::EditorRetentionStats editor_gpu;
  std::vector<CrimsonEditorViewDiagnostics> editor_views;
  std::string error;
};

[[nodiscard]] std::unique_ptr<RenderHost> create_crimson_render_host();
[[nodiscard]] bool crimson_render_diagnostics(const RenderHost &host,
                                              CrimsonRenderDiagnostics &diagnostics);

} // namespace quader
