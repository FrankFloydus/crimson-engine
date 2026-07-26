////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/document/scene_command.hpp"
#include "scene/scene_transform.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quader {

/**
 * Applies Group Selection Command behavior as a command in the editor document command layer.
 */
class GroupSelectionCommand final : public SceneCommand {
public:
  GroupSelectionCommand(
      SceneDocument &scene, std::string group_name = {},
      TransformComponent group_transform = TransformComponent{})
      : SceneCommand(scene, "Group Selection"),
        group_name_(std::move(group_name)), group_transform_(group_transform) {}

  [[nodiscard]] EntityId group_entity() const { return group_; }

  DocumentCommandResult undo() override {
    if (group_ == kInvalidEntityId || !scene().exists(group_)) {
      return document_command_missing_entity_result();
    }

    for (const DocumentEntityPlacement &placement : root_placements_) {
      if (!scene().exists(placement.entity) ||
          (placement.parent != kInvalidEntityId &&
           !scene().exists(placement.parent))) {
        return document_command_invalid_request_result(
            "Grouped entity placement cannot be restored");
      }
    }
    const std::vector<DocumentEntityPlacement> grouped_placements =
        document_command_capture_placements(scene(), roots_);
    if (grouped_placements.size() != root_placements_.size()) {
      return document_command_missing_entity_result();
    }

    std::vector<SceneParentAssignment> parent_assignments;
    std::vector<SceneTransformAssignment> transform_assignments;
    parent_assignments.reserve(root_placements_.size());
    transform_assignments.reserve(root_placements_.size());
    for (const DocumentEntityPlacement &placement : root_placements_) {
      parent_assignments.push_back({
          .child = placement.entity,
          .parent = placement.parent,
      });
      transform_assignments.push_back({
          .entity = placement.entity,
          .transform = placement.transform,
      });
    }

    std::size_t restored_count = 0u;
    if (scene().set_parents(parent_assignments)) {
      scene().set_transforms(transform_assignments);
      restored_count = root_placements_.size();
    } else {
      for (const DocumentEntityPlacement &placement : root_placements_) {
        DocumentCommandResult result =
            document_command_apply_placement(scene(), placement);
        if (!result.ok) {
          if (!rollback_placements(grouped_placements, restored_count)) {
            return document_command_invalid_request_result(
                "Group undo failed and rollback failed");
          }
          return result;
        }
        ++restored_count;
      }
    }
    if (!scene().destroy_entity(group_)) {
      if (!rollback_placements(grouped_placements, restored_count)) {
        return document_command_invalid_request_result(
            "Group undo failed and rollback failed");
      }
      return document_command_missing_entity_result();
    }
    document_command_apply_selection(scene(), before_selection_);
    return document_command_changed_result();
  }

    DocumentCommandResult redo() override {
      if (!captured_roots_) {
        roots_ = document_command_selected_root_entities(scene());
        captured_roots_ = true;
      }
      roots_ = document_command_normalize_existing_entities(scene(), roots_);
      if (roots_.empty()) {
        return document_command_unchanged_result();
      }

      before_selection_ = document_command_capture_selection(scene());
      root_placements_ = document_command_capture_placements(scene(), roots_);
      const EntityId common_parent =
          document_command_common_parent(scene(), roots_);
      const std::optional<QMat4> effective_group_world =
          preflight_group_world(common_parent);
      const std::optional<std::vector<SceneTransformAssignment>>
          root_transforms =
              effective_group_world.has_value()
                  ? planned_root_reparents(*effective_group_world)
                  : std::nullopt;
      if (!effective_group_world.has_value() ||
          !root_transforms.has_value()) {
        return document_command_invalid_request_result(
            "Selected entities cannot preserve their world transforms under "
            "the group");
      }
      const std::string name =
          group_name_.empty() ? std::string("Group") : group_name_;
      group_ = scene().create_entity_with_id(group_, name, EntityKind::Empty,
                                             group_object_id_);
      if (group_ == kInvalidEntityId) {
        return document_command_invalid_request_result(
            "Group entity cannot be created");
      }
      if (!group_uuid_.empty() &&
          !scene().set_entity_uuid(group_, group_uuid_)) {
        scene().destroy_entity(group_);
        group_ = kInvalidEntityId;
        return document_command_invalid_request_result(
            "Group entity identity cannot be restored");
      }
      if (group_uuid_.empty()) {
        group_uuid_ = scene().uuid(group_);
      }
      group_object_id_ = scene().editor_object_id(group_);
      scene().set_transform(group_, group_transform_);
      if (common_parent != kInvalidEntityId &&
          !scene().set_parent(group_, common_parent, true)) {
        scene().destroy_entity(group_);
        return document_command_invalid_request_result(
            "Group entity cannot be reparented");
      }
        std::vector<SceneParentAssignment> parent_assignments;
        parent_assignments.reserve(roots_.size());
        for (const EntityId root : roots_) {
          parent_assignments.push_back({
              .child = root,
              .parent = group_,
          });
        }
        std::size_t grouped_count = 0u;
        if (scene().set_parents(parent_assignments)) {
          scene().set_transforms(*root_transforms);
          grouped_count = roots_.size();
        } else {
          for (const EntityId root : roots_) {
            if (!scene().set_parent(root, group_, true)) {
              if (!rollback_created_group(grouped_count)) {
                return document_command_invalid_request_result(
                    "Grouping failed and rollback failed");
              }
              return document_command_invalid_request_result(
                  "Selected entity cannot be grouped");
            }
            ++grouped_count;
          }
        }
        document_command_select_only(scene(), {group_});
        return document_command_changed_result();
    }

private:
  std::optional<QMat4>
  preflight_group_world(EntityId common_parent) const {
    if (!scene_transform_is_finite(group_transform_)) {
      return std::nullopt;
    }
    const QMat4 requested_group_world =
        compose_transform(group_transform_.spatial_transform());
    if (!scene_matrix_is_finite(requested_group_world)) {
      return std::nullopt;
    }
    if (common_parent == kInvalidEntityId) {
      return requested_group_world;
    }

    const QMat4 parent_world = scene().world_transform_matrix(common_parent);
    const std::optional<TransformComponent> group_local =
        try_scene_local_transform_preserving_world(
            parent_world, requested_group_world, group_transform_);
    if (!group_local.has_value()) {
      return std::nullopt;
    }
    return scene_transform_multiply(
        parent_world, compose_transform(group_local->spatial_transform()));
  }

  std::optional<std::vector<SceneTransformAssignment>>
  planned_root_reparents(const QMat4 &group_world) const {
    std::vector<SceneTransformAssignment> transforms;
    transforms.reserve(roots_.size());
    for (const EntityId root : roots_) {
      const TransformComponent *transform = scene().transform(root);
      const std::optional<TransformComponent> local =
          transform == nullptr
              ? std::nullopt
              : try_scene_local_transform_preserving_world(
                    group_world, scene().world_transform_matrix(root),
                    *transform);
      if (!local.has_value()) {
        return std::nullopt;
      }
      transforms.push_back({
          .entity = root,
          .transform = *local,
      });
    }
    return transforms;
  }

  bool rollback_placements(
      const std::vector<DocumentEntityPlacement> &placements,
      std::size_t count) {
    while (count > 0u) {
      --count;
      if (!document_command_apply_placement(scene(), placements[count]).ok) {
        return false;
      }
    }
    return true;
  }

  bool rollback_created_group(std::size_t grouped_count) {
    if (!rollback_placements(root_placements_, grouped_count)) {
      return false;
    }
    return scene().destroy_entity(group_);
  }

  std::string group_name_;
  TransformComponent group_transform_;
  bool captured_roots_ = false;
  std::vector<EntityId> roots_;
  std::vector<DocumentEntityPlacement> root_placements_;
  DocumentSelectionState before_selection_;
  EntityId group_ = kInvalidEntityId;
  std::string group_uuid_;
  EditorObjectId group_object_id_ = kInvalidEditorObjectId;
};

} // namespace quader
