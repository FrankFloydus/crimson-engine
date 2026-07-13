////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"

#include <optional>

namespace quader {

class AssetRegistry;
class SceneDocument;

/**
 * Selects the geometry extent used by the committed scene-pivot resolver.
 */
enum class VisiblePivotBoundsScope {
  Entity,
  Hierarchy,
};

/**
 * Resolves the committed pivot shown for one scene entity.
 *
 * Authored custom pivots use the parent-aware world transform origin. Entities
 * without a custom pivot use the center of their visible geometry bounds.
 */
[[nodiscard]] std::optional<QVec3> resolve_entity_visible_pivot(
    const SceneDocument &scene, const AssetRegistry &assets, EntityId entity,
    VisiblePivotBoundsScope bounds_scope = VisiblePivotBoundsScope::Entity);

/**
 * Resolves the pivot shown by transform tools for one selected entity.
 *
 * A selected hierarchy uses its complete visible bounds; a leaf uses only its
 * own visible bounds. Custom pivots always resolve to the world origin.
 */
[[nodiscard]] std::optional<QVec3> resolve_entity_transform_visible_pivot(
    const SceneDocument &scene, const AssetRegistry &assets, EntityId entity);

/**
 * Resolves the committed transform pivot for the current scene selection.
 */
[[nodiscard]] std::optional<QVec3>
resolve_scene_selection_visible_pivot(const SceneDocument &scene,
                                      const AssetRegistry &assets);

} // namespace quader
