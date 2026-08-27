////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/scene_command.hpp"
#include "editor/commands/document/scene_texture_mapping_plan.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace quader {

/**
 * Applies Apply Entity Transform Command behavior as a command in the editor document command layer.
 */
class ApplyEntityTransformCommand final : public SceneCommand {
public:
    ApplyEntityTransformCommand(
            SceneDocument& scene,
            EntityId entity,
            TransformComponent before,
            TransformComponent after,
            std::string label = "Apply Entity Transform",
            std::optional<TextureLockSettings> texture_locks = std::nullopt)
        : SceneCommand(scene, std::move(label))
        , entity_(entity)
        , before_(before)
        , after_(after)
        , texture_locks_(texture_locks) {}

    DocumentCommandResult undo() override {
        return apply(before_, false);
    }

    DocumentCommandResult redo() override {
      if (texture_locks_.has_value() && !mapping_captured_) {
        const TransformComponent *current = scene().transform(entity_);
        if (current == nullptr) {
          return document_command_missing_entity_result();
        }
        // Earlier operations in a scene batch may have reparented/transformed
        // this node. Capture its actual frame at the first execution only.
        before_ = *current;
        const std::optional<SceneTextureMappingPlan> plan =
            SceneTextureMappingPlan::capture(scene(), {&entity_, 1u}, *texture_locks_);
        const DocumentTransformEdit edit{.entity = entity_, .before = before_, .after = after_};
        if (!plan.has_value()) {
          return document_command_invalid_request_result("Invalid texture transform hierarchy");
        }
        const auto mappings = plan->evaluate({&edit, 1u});
        if (!mappings.has_value()) {
          return document_command_invalid_request_result("Invalid texture transform mapping");
        }
        mapping_edits_ = *mappings;
        mapping_captured_ = true;
      }
      if (document_command_same_transform(before_, after_) &&
          std::none_of(mapping_edits_.begin(), mapping_edits_.end(),
                       [](const auto &edit) { return edit.before != edit.after; })) {
        return document_command_unchanged_result();
      }
        return apply(after_, true);
    }

    bool merge_with(const IUndoableCommand &newer_command,
                    std::string_view merge_key) override {
      if (merge_key.empty()) {
        return false;
      }
      const auto *newer =
          dynamic_cast<const ApplyEntityTransformCommand *>(&newer_command);
      if (newer == nullptr || &newer->scene() != &scene() ||
          newer->entity_ != entity_) {
        return false;
      }
      after_ = newer->after_;
      for (const DocumentTextureMappingEdit &new_edit : newer->mapping_edits_) {
        const auto found = std::find_if(mapping_edits_.begin(), mapping_edits_.end(),
            [&](const auto &edit) { return edit.entity == new_edit.entity; });
        if (found == mapping_edits_.end()) {
          mapping_edits_.push_back(new_edit);
        } else {
          found->after = new_edit.after;
        }
      }
      return true;
    }

private:
    DocumentCommandResult apply(const TransformComponent& transform, bool use_after) {
        if (!scene().exists(entity_)) {
          return document_command_missing_entity_result();
        }
        std::vector<SceneTextureMappingAssignment> mappings;
        mappings.reserve(mapping_edits_.size());
        for (const DocumentTextureMappingEdit &edit : mapping_edits_) {
          mappings.push_back({.entity = edit.entity,
                              .mapping = use_after ? edit.after : edit.before});
        }
        const SceneTransformAssignment assignment{.entity = entity_, .transform = transform};
        if (!scene().set_transforms_and_texture_mappings({&assignment, 1u}, mappings)) {
          return document_command_invalid_request_result(
              "Entity transform could not be applied");
        }
        return document_command_changed_result();
    }

    EntityId entity_ = kInvalidEntityId;
    TransformComponent before_;
    TransformComponent after_;
    std::optional<TextureLockSettings> texture_locks_;
    std::vector<DocumentTextureMappingEdit> mapping_edits_;
    bool mapping_captured_ = false;
};

} // namespace quader
