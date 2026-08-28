////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/scene_document.hpp"

namespace quader {

/**
 * Tests local selectability along a hierarchy segment without excluding hidden
 * members. An invalid exclusive ancestor tests the entire ancestor chain; an
 * explicit ancestor can exclude hidden, unselectable instance-definition storage.
 * Lock policy is checked separately by callers.
 */
[[nodiscard]] inline bool
scene_hierarchy_segment_is_selectable_ignoring_visibility(
    const SceneDocument &scene, EntityId entity,
    EntityId exclusive_ancestor = kInvalidEntityId) {
  if (!scene.exists(entity) ||
      (exclusive_ancestor != kInvalidEntityId &&
       !scene.exists(exclusive_ancestor))) {
    return false;
  }
  EntityId current = entity;
  while (current != kInvalidEntityId && current != exclusive_ancestor) {
    const VisibilityComponent *visibility = scene.visibility(current);
    if (visibility != nullptr && !visibility->selectable) {
      return false;
    }
    current = scene.parent(current);
  }
  return current == exclusive_ancestor;
}

/**
 * Tests the hierarchy segment beginning at `entity` and ending immediately
 * before `exclusive_ancestor`. Instance-definition roots are intentionally
 * excluded because they are hidden canonical storage rather than rendered
 * participants.
 */
[[nodiscard]] inline bool scene_hierarchy_segment_is_visible(
    const SceneDocument &scene, EntityId entity,
    EntityId exclusive_ancestor) {
  if (!scene.exists(entity) || !scene.exists(exclusive_ancestor)) {
    return false;
  }
  EntityId current = entity;
  while (current != kInvalidEntityId && current != exclusive_ancestor) {
    const VisibilityComponent *visibility = scene.visibility(current);
    if (visibility != nullptr && !visibility->visible) {
      return false;
    }
    current = scene.parent(current);
  }
  return current == exclusive_ancestor;
}

/**
 * Applies the same canonical-member segment policy as visibility while also
 * rejecting locally unselectable or locked members and ancestors.
 */
[[nodiscard]] inline bool scene_hierarchy_segment_is_selectable(
    const SceneDocument &scene, EntityId entity,
    EntityId exclusive_ancestor) {
  if (!scene.exists(entity) || !scene.exists(exclusive_ancestor)) {
    return false;
  }
  EntityId current = entity;
  while (current != kInvalidEntityId && current != exclusive_ancestor) {
    const VisibilityComponent *visibility = scene.visibility(current);
    if (visibility != nullptr &&
        (!visibility->visible || !visibility->selectable)) {
      return false;
    }
    const LockComponent *lock = scene.lock(current);
    if (lock != nullptr && lock->locked) {
      return false;
    }
    current = scene.parent(current);
  }
  return current == exclusive_ancestor;
}

} // namespace quader
