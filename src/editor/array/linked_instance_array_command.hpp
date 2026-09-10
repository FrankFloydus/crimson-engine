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
#include "scene/scene_texture_mapping.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quader {

/**
 * Describes the linked occurrence copies generated for one array source.
 */
struct LinkedInstanceArraySource final {
  EntityId source = kInvalidEntityId;
  std::vector<QMat4> generated_world_transforms;
  /// Captured per-placement occurrence sampling, parallel to the transforms.
  std::vector<QMat4> generated_sampling_from_local;
};

/**
 * Creates source-inclusive linked occurrence arrays as one scene-history item.
 *
 * Only lightweight occurrence roots are duplicated. Their definition reference
 * remains unchanged, while every generated root receives the exact world
 * transform produced by the renderer-neutral array layout.
 */
class LinkedInstanceArrayCommand final : public SceneCommand {
public:
  LinkedInstanceArrayCommand(
      SceneDocument &scene, std::string label,
      std::vector<LinkedInstanceArraySource> sources)
      : SceneCommand(scene, std::move(label)), sources_(std::move(sources)) {}

  /// Returns the generated occurrences without consulting mutable selection.
  [[nodiscard]] const std::vector<EntityId> &generated_occurrences() const {
    return duplicate_roots_;
  }

  CommandResult undo() override {
    if (duplicate_roots_.empty()) {
      return command_invalid_result("Instance array has not been created.");
    }
    for (auto it = duplicate_roots_.rbegin(); it != duplicate_roots_.rend();
         ++it) {
      if (scene().exists(*it) && !scene().destroy_entity(*it)) {
        return command_invalid_result(
            "Instance array occurrence could not be removed.");
      }
    }
    duplicate_roots_.clear();
    document_command_apply_selection(scene(), before_selection_);
    return command_changed_result();
  }

  CommandResult redo() override {
    duplicate_roots_.clear();
    if (source_snapshots_.empty()) {
      const CommandResult captured = capture_sources();
      if (!captured.ok || !captured.changed) {
        return captured;
      }
    }

    before_selection_ = document_command_capture_selection(scene());
    const CommandResult created = duplicate_snapshots_.empty()
                                      ? create_from_sources()
                                      : restore_duplicates();
    if (!created.ok) {
      destroy_duplicate_roots();
      document_command_apply_selection(scene(), before_selection_);
      return created;
    }

    std::vector<EntityId> elements;
    elements.reserve(source_snapshots_.size() + duplicate_roots_.size());
    for (const DocumentEntitySnapshot &source : source_snapshots_) {
      if (scene().exists(source.id)) {
        elements.push_back(source.id);
      }
    }
    elements.insert(elements.end(), duplicate_roots_.begin(),
                    duplicate_roots_.end());
    document_command_select_only(scene(), elements);
    return command_changed_result();
  }

private:
  CommandResult capture_sources() {
    if (sources_.empty()) {
      return command_unchanged_result();
    }
    source_snapshots_.clear();
    source_snapshots_.reserve(sources_.size());
    for (const LinkedInstanceArraySource &source : sources_) {
      const InstanceOccurrenceComponent *occurrence =
          scene().instance_occurrence(source.source);
      if (occurrence == nullptr || source.generated_world_transforms.empty() ||
          (!source.generated_sampling_from_local.empty() &&
           source.generated_sampling_from_local.size() !=
               source.generated_world_transforms.size()) ||
          !scene().is_instance_definition_entity(occurrence->definition) ||
          scene().has_children(source.source)) {
        source_snapshots_.clear();
        return command_invalid_result(
            "Instance array source is no longer a valid occurrence.");
      }
      std::optional<DocumentEntitySnapshot> snapshot =
          document_command_capture_entity_snapshot(scene(), source.source);
      if (!snapshot.has_value()) {
        source_snapshots_.clear();
        return command_invalid_result(
            "Instance array source could not be captured.");
      }
      snapshot->children.clear();
      source_snapshots_.push_back(std::move(*snapshot));
    }
    return command_changed_result();
  }

  CommandResult create_from_sources() {
    duplicate_snapshots_.clear();
    SceneEntityNameAllocator name_allocator(scene());
    for (std::size_t source_index = 0; source_index < sources_.size();
         ++source_index) {
      const LinkedInstanceArraySource &source = sources_[source_index];
      const DocumentEntitySnapshot &snapshot = source_snapshots_[source_index];
      const EntityId parent = snapshot.parent;
      if (parent != kInvalidEntityId && !scene().exists(parent)) {
        return command_invalid_result(
            "Instance array source parent no longer exists.");
      }
      const QMat4 parent_world =
          parent == kInvalidEntityId
              ? scene_transform_identity_matrix()
              : scene().world_transform_matrix(parent);
      for (std::size_t index = 0; index < source.generated_world_transforms.size(); ++index) {
        const QMat4 &generated_world = source.generated_world_transforms[index];
        const std::optional<TransformComponent> local =
            try_scene_local_transform_preserving_world(
                parent_world, generated_world, snapshot.transform);
        if (!local.has_value()) {
          return command_invalid_result(
              "Instance array transform could not be represented.");
        }
        const QMat4 recomposed = scene_transform_multiply(
            parent_world, compose_transform(local->spatial_transform()));
        if (!scene_matrix_near_equal(recomposed, generated_world)) {
          return command_invalid_result(
              "Instance array transform cannot be represented under its "
              "source parent without shear.");
        }

        DocumentEntitySnapshot generated = snapshot;
        generated.transform = *local;
        if (!source.generated_sampling_from_local.empty()) {
          const QMat4 &mapping = source.generated_sampling_from_local[index];
          if (!texture_mapping_is_valid({mapping})) {
            return command_invalid_result("Instance array texture mapping is invalid.");
          }
          generated.texture_mapping = mapping.values == QMat4{}.values
              ? std::nullopt : std::optional{TextureMappingComponent{mapping}};
        }
        generated.children.clear();
        const EntityId duplicate = document_command_duplicate_entity_snapshot_tree(
            scene(), generated, parent, false, nullptr, &name_allocator);
        if (duplicate == kInvalidEntityId) {
          return command_invalid_result(
              "Instance array occurrence could not be created.");
        }
        duplicate_roots_.push_back(duplicate);

        std::optional<DocumentEntitySnapshot> duplicate_snapshot =
            document_command_capture_entity_snapshot(scene(), duplicate);
        if (!duplicate_snapshot.has_value()) {
          return command_invalid_result(
              "Instance array occurrence could not be captured.");
        }
        duplicate_snapshot->children.clear();
        duplicate_snapshots_.push_back(std::move(*duplicate_snapshot));
      }
    }
    return duplicate_roots_.empty()
               ? command_unchanged_result()
               : command_changed_result();
  }

  CommandResult restore_duplicates() {
    for (const DocumentEntitySnapshot &snapshot : duplicate_snapshots_) {
      const EntityId parent = snapshot.parent;
      if (parent != kInvalidEntityId && !scene().exists(parent)) {
        return command_invalid_result(
            "Instance array occurrence parent no longer exists.");
      }
      const EntityId restored = document_command_create_entity_from_snapshot(
          scene(), snapshot, parent, snapshot.name, false, true);
      if (restored == kInvalidEntityId) {
        return command_invalid_result(
            "Instance array occurrence could not be restored.");
      }
      duplicate_roots_.push_back(restored);
    }
    return duplicate_roots_.empty()
               ? command_unchanged_result()
               : command_changed_result();
  }

  void destroy_duplicate_roots() {
    for (auto it = duplicate_roots_.rbegin(); it != duplicate_roots_.rend();
         ++it) {
      if (scene().exists(*it)) {
        (void)scene().destroy_entity(*it);
      }
    }
    duplicate_roots_.clear();
  }

  std::vector<LinkedInstanceArraySource> sources_;
  std::vector<DocumentEntitySnapshot> source_snapshots_;
  std::vector<DocumentEntitySnapshot> duplicate_snapshots_;
  std::vector<EntityId> duplicate_roots_;
  DocumentSelectionState before_selection_;
};

} // namespace quader
