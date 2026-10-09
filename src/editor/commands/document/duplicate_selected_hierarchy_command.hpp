////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "diagnostics/profile.hpp"
#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/document/scene_command.hpp"

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {

/**
 * Applies selected hierarchy duplicate behavior as one command in the editor document command layer.
 */
class DuplicateSelectedHierarchyCommand final : public SceneCommand {
public:
    explicit DuplicateSelectedHierarchyCommand(SceneDocument& scene)
        : SceneCommand(scene, "Duplicate Hierarchy") {}

    DuplicateSelectedHierarchyCommand(
        SceneDocument &scene,
        std::vector<DocumentEntitySnapshot> source_snapshots,
        std::unordered_map<EntityId, EntityId> existing_clones_by_source,
        SceneEntityNamePolicy name_policy =
            SceneEntityNamePolicy::DuplicateFamily)
        : SceneCommand(scene, "Duplicate Hierarchy"), captured_sources_(true),
          source_snapshots_(std::move(source_snapshots)),
          existing_clones_by_source_(std::move(existing_clones_by_source)),
          name_policy_(name_policy) {}

    [[nodiscard]] const std::vector<EntityId> &duplicate_roots() const {
      return duplicate_roots_;
    }

    DocumentCommandResult undo() override {
        QDR_PROFILE_FUNCTION();
        if (duplicate_roots_.empty()) {
          return document_command_missing_entity_result();
        }

        for (auto it = duplicate_roots_.rbegin(); it != duplicate_roots_.rend();
             ++it) {
          if (scene().exists(*it) && !scene().destroy_entity(*it)) {
            return document_command_missing_entity_result();
          }
        }
        duplicate_roots_.clear();
        document_command_apply_selection(scene(), before_selection_);
        return document_command_changed_result();
    }

    DocumentCommandResult redo() override {
        QDR_PROFILE_FUNCTION();
        duplicate_roots_.clear();
        if (!captured_sources_) {
          DocumentCommandResult captured = capture_sources();
          if (!captured.ok || !captured.changed) {
            return captured;
          }
        }
        if (source_snapshots_.empty()) {
          return document_command_unchanged_result();
        }

        before_selection_ = document_command_capture_selection(scene());
        DocumentCommandResult restored = duplicate_snapshots_.empty()
                                             ? duplicate_from_sources()
                                             : restore_duplicates();
        if (!restored.ok) {
          destroy_duplicate_roots();
          document_command_apply_selection(scene(), before_selection_);
          return restored;
        }

        document_command_select_only(scene(), duplicate_roots_);
        return document_command_changed_result();
    }

private:
  DocumentCommandResult capture_sources() {
    QDR_PROFILE_FUNCTION();
    captured_sources_ = true;
    const std::vector<EntityId> roots =
        document_command_selected_root_entities(scene());
    if (roots.empty()) {
      return document_command_unchanged_result();
    }

    source_snapshots_.clear();
    source_snapshots_.reserve(roots.size());
    for (const EntityId root : roots) {
      std::optional<DocumentEntitySnapshot> snapshot =
          document_command_capture_entity_snapshot(scene(), root);
      if (!snapshot.has_value()) {
        source_snapshots_.clear();
        return document_command_missing_entity_result();
      }
      source_snapshots_.push_back(std::move(*snapshot));
    }
    return document_command_changed_result();
  }

  DocumentCommandResult duplicate_from_sources() {
    QDR_PROFILE_FUNCTION();
    duplicate_snapshots_.clear();
    duplicate_snapshots_.reserve(source_snapshots_.size());
    SceneEntityNameAllocator name_allocator(scene(), name_policy_);
    for (const DocumentEntitySnapshot &snapshot : source_snapshots_) {
      QDR_PROFILE_SCOPE("Duplicate snapshot tree");
      const EntityId parent = snapshot.parent != kInvalidEntityId &&
                                      scene().exists(snapshot.parent)
                                  ? snapshot.parent
                                  : kInvalidEntityId;
      const EntityId duplicate =
          document_command_duplicate_entity_snapshot_tree(
              scene(), snapshot, parent, false,
              existing_clones_by_source_.empty() ? nullptr
                                                 : &existing_clones_by_source_,
              &name_allocator);
      if (duplicate == kInvalidEntityId) {
        return document_command_invalid_request_result(
            "Selected hierarchy could not be duplicated");
      }
      duplicate_roots_.push_back(duplicate);
    }

    for (const EntityId duplicate : duplicate_roots_) {
      QDR_PROFILE_SCOPE("Capture duplicate snapshot");
      std::optional<DocumentEntitySnapshot> snapshot =
          document_command_capture_entity_snapshot(scene(), duplicate);
      if (!snapshot.has_value()) {
        return document_command_invalid_request_result(
            "Duplicated hierarchy snapshot could not be captured");
      }
      duplicate_snapshots_.push_back(std::move(*snapshot));
    }
    return document_command_changed_result();
  }

  DocumentCommandResult restore_duplicates() {
    QDR_PROFILE_FUNCTION();
    std::unordered_map<EntityId, EntityId> remapped_entities;
    for (const DocumentEntitySnapshot &snapshot : duplicate_snapshots_) {
      QDR_PROFILE_SCOPE("Restore duplicate snapshot tree");
      const EntityId parent = snapshot.parent != kInvalidEntityId &&
                                      scene().exists(snapshot.parent)
                                  ? snapshot.parent
                                  : kInvalidEntityId;
      const EntityId restored = restore_or_reuse_duplicate_snapshot_tree(
          snapshot, parent, remapped_entities);
      if (restored == kInvalidEntityId) {
        return document_command_invalid_request_result(
            "Duplicated hierarchy could not be restored");
      }
      duplicate_roots_.push_back(restored);
    }
    return document_command_changed_result();
  }

  EntityId restore_or_reuse_duplicate_snapshot_tree(
      const DocumentEntitySnapshot &snapshot, EntityId parent,
      std::unordered_map<EntityId, EntityId> &remapped_entities) {
    EntityId restored = kInvalidEntityId;
    if (scene().exists(snapshot.id)) {
      restored = snapshot.id;
      // A modeling memento can recreate modeled descendants before this
      // document command rebuilds their generic ancestors. Restore both the
      // captured parent and sibling ordinal when reusing those exact scene
      // identities so mixed hierarchy redo preserves authored child order.
      if (!apply_scene_entity_state_placement(scene(), restored, snapshot,
                                              parent)) {
        return kInvalidEntityId;
      }
      document_command_apply_entity_snapshot_common_state(
          scene(), restored, snapshot, snapshot.name);
      document_command_apply_entity_snapshot_components(scene(), restored,
                                                        snapshot, true);
    } else {
      restored = document_command_create_entity_from_snapshot(
          scene(), snapshot, parent, snapshot.name, true, true);
      if (restored == kInvalidEntityId) {
        return kInvalidEntityId;
      }
    }

    remapped_entities[snapshot.id] = restored;
    for (const DocumentEntitySnapshot &child : snapshot.children) {
      if (restore_or_reuse_duplicate_snapshot_tree(
              child, restored, remapped_entities) == kInvalidEntityId) {
        if (scene().exists(restored)) {
          (void)scene().destroy_entity(restored);
        }
        document_command_erase_snapshot_remaps(snapshot, remapped_entities);
        return kInvalidEntityId;
      }
    }
    return restored;
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

  bool captured_sources_ = false;
  std::vector<DocumentEntitySnapshot> source_snapshots_;
  std::unordered_map<EntityId, EntityId> existing_clones_by_source_;
  SceneEntityNamePolicy name_policy_ = SceneEntityNamePolicy::DuplicateFamily;
  std::vector<DocumentEntitySnapshot> duplicate_snapshots_;
  std::vector<EntityId> duplicate_roots_;
  DocumentSelectionState before_selection_;
};

} // namespace quader
