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
#include "editor/history/editor_history_service.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {

/**
 * Owns Scene Transform Preview Transaction behavior within the editor document ownership layer.
 */
class SceneTransformPreviewTransaction final {
public:
    SceneTransformPreviewTransaction(
            SceneDocument& scene,
            std::vector<EntityId> entities,
            std::string label = "Scene Transform Preview")
        : scene_(scene)
        , label_(std::move(label)) {
        std::sort(entities.begin(), entities.end());
        entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
        edits_.reserve(entities.size());
        for (const EntityId entity : entities) {
          if (entity == kInvalidEntityId || !scene_.exists(entity) ||
              scene_.is_effectively_locked(entity)) {
            continue;
          }
            const TransformComponent* transform = scene_.transform(entity);
            if (transform == nullptr) {
                continue;
            }
            const std::size_t edit_index = edits_.size();
            edits_.push_back(DocumentTransformEdit{
                    .entity = entity,
                    .before = *transform,
                    .after = *transform,
            });
            edit_index_by_entity_.emplace(entity, edit_index);
        }
        active_ = true;
    }

    ~SceneTransformPreviewTransaction() {
        if (active_) {
            (void)cancel();
        }
    }

    SceneTransformPreviewTransaction(const SceneTransformPreviewTransaction&) = delete;
    SceneTransformPreviewTransaction& operator=(const SceneTransformPreviewTransaction&) = delete;

    SceneTransformPreviewTransaction(
        SceneTransformPreviewTransaction &&other) noexcept
        : scene_(other.scene_), label_(std::move(other.label_)),
          edits_(std::move(other.edits_)),
          edit_index_by_entity_(std::move(other.edit_index_by_entity_)),
          active_(other.active_) {
      other.active_ = false;
    }

    SceneTransformPreviewTransaction& operator=(SceneTransformPreviewTransaction&&) = delete;

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool empty() const { return edits_.empty(); }
    [[nodiscard]] const std::vector<DocumentTransformEdit>& edits() const { return edits_; }

    DocumentCommandResult preview_transform(EntityId entity,
                                            TransformComponent transform) {
      if (!active_) {
        return document_command_invalid_request_result(
            "Transform preview is not active");
      }
      DocumentTransformEdit *edit = edit_for(entity);
      if (edit == nullptr || !scene_.exists(entity) ||
          scene_.is_effectively_locked(entity)) {
        return document_command_missing_entity_result();
      }

      const bool changed =
          !document_command_same_transform(edit->after, transform);
      edit->after = transform;
      const SceneTransformAssignment assignment{
          .entity = entity,
          .transform = transform,
      };
      scene_.set_transforms(
          std::span<const SceneTransformAssignment>(&assignment, 1u));
      return changed ? document_command_changed_result()
                     : document_command_unchanged_result();
    }

    DocumentCommandResult
    preview_transforms(std::vector<DocumentTransformEdit> edits) {
      if (!active_) {
        return document_command_invalid_request_result(
            "Transform preview is not active");
      }

      bool changed = false;
      std::vector<SceneTransformAssignment> assignments;
      assignments.reserve(edits.size());
      for (const DocumentTransformEdit &edit : edits) {
        DocumentTransformEdit *stored_edit = edit_for(edit.entity);
        if (stored_edit == nullptr || !scene_.exists(edit.entity) ||
            scene_.is_effectively_locked(edit.entity)) {
          return document_command_missing_entity_result();
        }
        changed = changed || !document_command_same_transform(
                                 stored_edit->after, edit.after);
        stored_edit->after = edit.after;
        assignments.push_back(SceneTransformAssignment{
            .entity = edit.entity,
            .transform = edit.after,
        });
      }
      scene_.set_transforms(assignments);
      return changed ? document_command_changed_result()
                     : document_command_unchanged_result();
    }

    DocumentCommandResult cancel() {
        if (!active_) {
          return document_command_invalid_request_result(
              "Transform preview is not active");
        }

        bool changed = false;
        std::vector<SceneTransformAssignment> assignments;
        assignments.reserve(edits_.size());
        for (const DocumentTransformEdit& edit : edits_) {
          if (!scene_.exists(edit.entity)) {
            continue;
          }
          const TransformComponent* current = scene_.transform(edit.entity);
          changed = changed || current == nullptr ||
                    !document_command_same_transform(*current, edit.before);
          assignments.push_back({.entity = edit.entity,
                                 .transform = edit.before});
        }
        scene_.set_transforms(assignments);
        active_ = false;
        return changed ? document_command_changed_result()
                       : document_command_unchanged_result();
    }

    DocumentCommandResult accept() {
        if (!active_) {
          return document_command_invalid_request_result(
              "Transform preview is not active");
        }

        bool changed = false;
        std::vector<SceneTransformAssignment> assignments;
        assignments.reserve(edits_.size());
        for (const DocumentTransformEdit& edit : edits_) {
          if (!scene_.exists(edit.entity) ||
              scene_.is_effectively_locked(edit.entity)) {
            continue;
          }
          changed = changed ||
                    !document_command_same_transform(edit.before, edit.after);
          assignments.push_back({.entity = edit.entity,
                                 .transform = edit.after});
        }
        scene_.set_transforms(assignments);
        active_ = false;
        return changed ? document_command_changed_result()
                       : document_command_unchanged_result();
    }

    DocumentCommandResult commit(EditorHistoryService& history) {
        if (!active_) {
          return document_command_invalid_request_result(
              "Transform preview is not active");
        }

        std::vector<DocumentTransformEdit> changed_edits;
        changed_edits.reserve(edits_.size());
        std::vector<SceneTransformAssignment> assignments;
        assignments.reserve(edits_.size());
        for (const DocumentTransformEdit& edit : edits_) {
          if (!scene_.exists(edit.entity) ||
              scene_.is_effectively_locked(edit.entity)) {
            continue;
          }
          assignments.push_back({.entity = edit.entity,
                                 .transform = edit.after});
          if (!document_command_same_transform(edit.before, edit.after)) {
            changed_edits.push_back(edit);
          }
        }
        scene_.set_transforms(assignments);
        active_ = false;

        if (changed_edits.empty()) {
          return document_command_unchanged_result();
        }

        return history.record_executed(
            std::make_unique<ApplyEntityTransformsCommand>(
                scene_, std::move(changed_edits), label_));
    }

private:
  DocumentTransformEdit *edit_for(EntityId entity) {
    const auto it = edit_index_by_entity_.find(entity);
    if (it == edit_index_by_entity_.end() || it->second >= edits_.size()) {
      return nullptr;
    }
    return &edits_[it->second];
  }

    SceneDocument& scene_;
    std::string label_;
    std::vector<DocumentTransformEdit> edits_;
    std::unordered_map<EntityId, std::size_t> edit_index_by_entity_;
    bool active_ = false;
};

} // namespace quader
