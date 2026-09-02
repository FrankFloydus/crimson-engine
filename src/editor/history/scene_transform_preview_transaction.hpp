////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/commands/document/apply_entity_transforms_command.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "editor/commands/document/scene_texture_mapping_plan.hpp"
#include "editor/history/editor_history_service.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {

/**
 * Previews placement and texture sampling atomically from captured baselines.
 * Cancellation restores both; commit records both in one native history entry.
 */
class SceneTransformPreviewTransaction final {
public:
  SceneTransformPreviewTransaction(
      SceneDocument &scene, std::vector<EntityId> entities,
      std::string label = "Scene Transform Preview",
      TextureLockSettings texture_locks = {})
      : scene_(scene), label_(std::move(label)) {
    std::sort(entities.begin(), entities.end());
    entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
    std::vector<EntityId> captured;
    for (const EntityId entity : entities) {
      if (entity == kInvalidEntityId || !scene_.exists(entity) ||
          scene_.is_effectively_locked(entity)) {
        continue;
      }
      const TransformComponent *transform = scene_.transform(entity);
      if (transform == nullptr) {
        continue;
      }
      edit_index_by_entity_.emplace(entity, edits_.size());
      edits_.push_back({.entity = entity, .before = *transform, .after = *transform});
      captured.push_back(entity);
    }
    mapping_plan_ = SceneTextureMappingPlan::capture(scene_, captured, texture_locks);
    if (!mapping_plan_.has_value()) {
      edits_.clear();
      edit_index_by_entity_.clear();
      return;
    }
    const auto mappings = mapping_plan_->evaluate(edits_);
    if (!mappings.has_value()) {
      edits_.clear();
      edit_index_by_entity_.clear();
      return;
    }
    mapping_edits_ = *mappings;
    active_ = true;
  }

  ~SceneTransformPreviewTransaction() {
    if (active_) {
      (void)cancel();
    }
  }

  SceneTransformPreviewTransaction(const SceneTransformPreviewTransaction &) = delete;
  SceneTransformPreviewTransaction &operator=(const SceneTransformPreviewTransaction &) = delete;
  SceneTransformPreviewTransaction(SceneTransformPreviewTransaction &&other) noexcept
      : scene_(other.scene_), label_(std::move(other.label_)),
        edits_(std::move(other.edits_)),
        edit_index_by_entity_(std::move(other.edit_index_by_entity_)),
        mapping_plan_(std::move(other.mapping_plan_)),
        mapping_edits_(std::move(other.mapping_edits_)), active_(other.active_) {
    other.active_ = false;
  }
  SceneTransformPreviewTransaction &operator=(SceneTransformPreviewTransaction &&) = delete;

  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] bool empty() const { return edits_.empty(); }
  [[nodiscard]] const std::vector<DocumentTransformEdit> &edits() const { return edits_; }

  /// Disarms destruction without touching the scene owner. This is reserved
  /// for host teardown after that non-owning scene reference has expired.
  void abandon() noexcept { active_ = false; }

  DocumentCommandResult preview_transform(
      EntityId entity, TransformComponent transform,
      SceneTextureTransformKind kind = SceneTextureTransformKind::Combined) {
    return preview_transforms({{.entity = entity, .after = transform}}, kind);
  }

  DocumentCommandResult preview_transforms(
      std::vector<DocumentTransformEdit> edits,
      SceneTextureTransformKind kind = SceneTextureTransformKind::Combined) {
    if (!active_) {
      return document_command_invalid_request_result("Transform preview is not active");
    }
    std::vector<DocumentTransformEdit> staged = edits_;
    for (const DocumentTransformEdit &edit : edits) {
      if (!scene_transform_is_finite(edit.after)) {
        return document_command_invalid_request_result("Transform contains non-finite values");
      }
      const auto found = edit_index_by_entity_.find(edit.entity);
      if (found == edit_index_by_entity_.end() || !scene_.exists(edit.entity) ||
          scene_.is_effectively_locked(edit.entity)) {
        return document_command_missing_entity_result();
      }
      DocumentTransformEdit &target = staged[found->second];
      target.after = edit.after;
    }
    bool changed = false;
    for (std::size_t index = 0; index < staged.size(); ++index) {
      changed = changed || !document_command_same_transform(staged[index].after,
                                                            edits_[index].after);
    }
    if (!changed) {
      return document_command_unchanged_result();
    }
    auto mappings = mapping_plan_->evaluate(staged, kind);
    if (!mappings.has_value()) {
      return document_command_invalid_request_result("Transform would produce an invalid texture mapping");
    }
    std::vector<SceneTransformAssignment> assignments;
    assignments.reserve(staged.size());
    for (const DocumentTransformEdit &edit : staged) {
      assignments.push_back({.entity = edit.entity, .transform = edit.after});
    }
    std::vector<SceneTextureMappingAssignment> mapping_assignments;
    mapping_assignments.reserve(mappings->size());
    for (const DocumentTextureMappingEdit &edit : *mappings) {
      mapping_assignments.push_back({.entity = edit.entity, .mapping = edit.after});
    }
    if (!scene_.set_transforms_and_texture_mappings(assignments, mapping_assignments)) {
      return document_command_invalid_request_result("Transform preview targets are no longer valid");
    }
    edits_ = std::move(staged);
    mapping_edits_ = std::move(*mappings);
    return document_command_changed_result();
  }

  DocumentCommandResult cancel() {
    if (!active_) {
      return document_command_invalid_request_result("Transform preview is not active");
    }
    const std::uint64_t before_revision = scene_.revision();
    const bool restored = apply_stored(false);
    active_ = false;
    if (!restored) {
      return document_command_invalid_request_result("Transform preview could not be restored");
    }
    return before_revision == scene_.revision() ? document_command_unchanged_result()
                                               : document_command_changed_result();
  }

  DocumentCommandResult accept() {
    if (!active_) {
      return document_command_invalid_request_result("Transform preview is not active");
    }
    if (!finish_targets_are_valid()) {
      return reject_invalid_finish();
    }
    if (!apply_stored(true)) {
      return reject_invalid_finish();
    }
    active_ = false;
    return has_changes() ? document_command_changed_result() : document_command_unchanged_result();
  }

  DocumentCommandResult commit(EditorHistoryService &history) {
    if (!active_) {
      return document_command_invalid_request_result("Transform preview is not active");
    }
    if (!finish_targets_are_valid()) {
      return reject_invalid_finish();
    }
    if (!apply_stored(true)) {
      return reject_invalid_finish();
    }
    if (!has_changes()) {
      active_ = false;
      return document_command_unchanged_result();
    }
    std::vector<DocumentTransformEdit> transforms;
    std::vector<DocumentTextureMappingEdit> mappings;
    for (const DocumentTransformEdit &edit : edits_) {
      if (!document_command_same_transform(edit.before, edit.after)) {
        transforms.push_back(edit);
      }
    }
    for (const DocumentTextureMappingEdit &edit : mapping_edits_) {
      if (edit.before != edit.after) {
        mappings.push_back(edit);
      }
    }
    const DocumentCommandResult result = history.record_executed(
        std::make_unique<ApplyEntityTransformsCommand>(
            scene_, std::move(transforms), label_, std::move(mappings)));
    if (result.ok) {
      active_ = false;
    }
    return result;
  }

private:
  [[nodiscard]] bool has_changes() const {
    return std::any_of(edits_.begin(), edits_.end(), [](const auto &edit) {
             return !document_command_same_transform(edit.before, edit.after);
           }) ||
           std::any_of(mapping_edits_.begin(), mapping_edits_.end(), [](const auto &edit) {
             return edit.before != edit.after;
           });
  }

  [[nodiscard]] bool finish_targets_are_valid() const {
    return std::all_of(edits_.begin(), edits_.end(), [this](const auto &edit) {
             return scene_.exists(edit.entity) && !scene_.is_effectively_locked(edit.entity);
           }) && std::all_of(mapping_edits_.begin(), mapping_edits_.end(), [this](const auto &edit) {
             return scene_.exists(edit.entity);
           });
  }

  bool apply_stored(bool use_after) {
    std::vector<SceneTransformAssignment> assignments;
    std::vector<SceneTextureMappingAssignment> mappings;
    for (const DocumentTransformEdit &edit : edits_) {
      if (scene_.exists(edit.entity)) {
        assignments.push_back({.entity = edit.entity,
                               .transform = use_after ? edit.after : edit.before});
      }
    }
    for (const DocumentTextureMappingEdit &edit : mapping_edits_) {
      if (scene_.exists(edit.entity)) {
        mappings.push_back({.entity = edit.entity,
                            .mapping = use_after ? edit.after : edit.before});
      }
    }
    return scene_.set_transforms_and_texture_mappings(assignments, mappings);
  }

  DocumentCommandResult reject_invalid_finish() {
    (void)apply_stored(false);
    active_ = false;
    return document_command_missing_entity_result();
  }

  SceneDocument &scene_;
  std::string label_;
  std::vector<DocumentTransformEdit> edits_;
  std::unordered_map<EntityId, std::size_t> edit_index_by_entity_;
  std::optional<SceneTextureMappingPlan> mapping_plan_;
  std::vector<DocumentTextureMappingEdit> mapping_edits_;
  bool active_ = false;
};

} // namespace quader
