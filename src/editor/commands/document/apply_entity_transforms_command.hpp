////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/scene_command.hpp"

#include <string>
#include <utility>
#include <vector>

namespace quader {

/**
 * Applies one logical transform operation to multiple scene entities while
 * preserving SceneDocument's batched revision contract.
 */
class ApplyEntityTransformsCommand final : public SceneCommand {
public:
  ApplyEntityTransformsCommand(
      SceneDocument &scene, std::vector<DocumentTransformEdit> edits,
      std::string label = "Apply Entity Transforms",
      std::vector<DocumentTextureMappingEdit> mappings = {})
      : SceneCommand(scene, std::move(label)) {
    before_.reserve(edits.size());
    after_.reserve(edits.size());
    for (const DocumentTransformEdit &edit : edits) {
      before_.push_back({.entity = edit.entity, .transform = edit.before});
      after_.push_back({.entity = edit.entity, .transform = edit.after});
    }
    for (const DocumentTextureMappingEdit &edit : mappings) {
      before_mappings_.push_back({.entity = edit.entity, .mapping = edit.before});
      after_mappings_.push_back({.entity = edit.entity, .mapping = edit.after});
    }
  }

  DocumentCommandResult undo() override { return apply(before_, before_mappings_); }

  DocumentCommandResult redo() override { return apply(after_, after_mappings_); }

private:
  DocumentCommandResult
  apply(const std::vector<SceneTransformAssignment> &assignments,
        const std::vector<SceneTextureMappingAssignment> &mappings) {
    if (assignments.empty() && mappings.empty()) {
      return document_command_unchanged_result();
    }
    for (const SceneTransformAssignment &assignment : assignments) {
      if (!scene().exists(assignment.entity)) {
        return document_command_missing_entity_result();
      }
    }
    if (!scene().set_transforms_and_texture_mappings(assignments, mappings)) {
      return document_command_invalid_request_result(
          "Transform and texture mapping could not be applied");
    }
    return document_command_changed_result();
  }

  std::vector<SceneTransformAssignment> before_;
  std::vector<SceneTransformAssignment> after_;
  std::vector<SceneTextureMappingAssignment> before_mappings_;
  std::vector<SceneTextureMappingAssignment> after_mappings_;
};

} // namespace quader
