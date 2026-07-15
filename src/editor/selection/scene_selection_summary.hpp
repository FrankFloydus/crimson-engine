////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "scene/scene_document.hpp"

#include <cstdint>
#include <vector>

namespace quader {

/**
 * Builds the canonical selection summary for scene-owned selection modes.
 *
 * Object-component selection remains modeling-runtime owned. Group and other
 * scene-only projections must use SceneDocument's dedicated selection revision
 * so unrelated content edits do not invalidate selection consumers.
 */
[[nodiscard]] inline ModelingSelectionSummary make_scene_selection_summary(
    const SceneDocument &scene, ModelingSelectionMode mode,
    std::uint64_t content_revision) {
  const std::vector<EntityId> selected = scene.selected_entities();
  const EntityId active = scene.primary_selection();
  const bool active_is_light =
      active != kInvalidEntityId && scene.light(active) != nullptr;
  return {
      .mode = mode,
      .has_selection = !selected.empty(),
      .active_kind =
          selected.empty()
              ? ModelingActiveSelectionKind::None
              : (active_is_light ? ModelingActiveSelectionKind::Light
                                 : ModelingActiveSelectionKind::Object),
      .selected_object_count = static_cast<std::uint32_t>(selected.size()),
      .content_revision = content_revision,
      .selection_revision = scene.selection_revision(),
  };
}

} // namespace quader
