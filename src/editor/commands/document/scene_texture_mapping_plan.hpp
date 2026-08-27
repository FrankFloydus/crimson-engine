////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_edits.hpp"
#include "scene/scene_document.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace quader {

/**
 * Captures independent movement/rotation and scale locks at gesture start.
 * Later preference changes do not alter preview compensation or history replay.
 */
struct TextureLockSettings {
  bool texture_lock = true;
  bool texture_lock_scale = false;
};

/**
 * Identifies the semantic operation whose lock controls texture compensation.
 * Rotation and scale include their pivot translation; combined edits apply
 * scale, rotation, then translation in that order.
 */
enum class SceneTextureTransformKind {
  Combined,
  Translation,
  Rotation,
  Scale,
};

/**
 * Captures affected hierarchy once and evaluates scene texture compensation
 * without querying live hierarchy, authored topology, or material catalogs.
 */
class SceneTextureMappingPlan final {
public:
  [[nodiscard]] static std::optional<SceneTextureMappingPlan>
  capture(const SceneDocument &scene, std::span<const EntityId> targets,
          TextureLockSettings settings);

  [[nodiscard]] std::optional<std::vector<DocumentTextureMappingEdit>> evaluate(
      std::span<const DocumentTransformEdit> transforms,
      SceneTextureTransformKind kind = SceneTextureTransformKind::Combined) const;

private:
  /**
   * Caches one affected hierarchy node and its placement/mapping baseline.
   * Parent indices let preview evaluation propagate transforms without querying
   * the live scene hierarchy again.
   */
  struct Node {
    EntityId entity = kInvalidEntityId;
    std::optional<std::size_t> parent_index;
    TransformComponent before_transform;
    QMat4 before_world;
    QMat4 before_world_inverse;
    QMat4 before_parent_world;
    QMat4 before_parent_inverse;
    std::optional<TextureMappingComponent> before_mapping;
    bool mapping_target = false;
  };

  TextureLockSettings settings_;
  std::vector<Node> nodes_;
  std::unordered_map<EntityId, std::size_t> node_by_entity_;
};

} // namespace quader
