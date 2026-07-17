////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/scene_command.hpp"
#include "editor/selection/scene_selection_application.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace quader {

/**
 * Replaces one complete, validated scene-layer state as a single history item.
 */
class SetSceneLayerStateCommand final : public SceneCommand {
public:
  SetSceneLayerStateCommand(SceneDocument &scene, std::string label,
                            SceneLayerState before, SceneLayerState after)
      : SceneCommand(scene, std::move(label)), before_(std::move(before)),
        after_(std::move(after)),
        before_selection_(capture_scene_selection(scene)) {
    normalize(before_);
    normalize(after_);
    before_effectively_locked_.reserve(before_selection_.selected.size());
    for (const EntityId entity : before_selection_.selected) {
      before_effectively_locked_.push_back(scene.is_effectively_locked(entity));
    }
  }

  DocumentCommandResult undo() override {
    return apply(before_, before_selection_);
  }

  DocumentCommandResult redo() override {
    bool changed = false;
    if (!apply_layer_state(after_, changed)) {
      return document_command_invalid_request_result(
          "Scene layer state is invalid");
    }
    if (!after_selection_initialized_) {
      after_selection_ = before_selection_;
      std::vector<EntityId> retained;
      retained.reserve(after_selection_.selected.size());
      for (std::size_t index = 0; index < after_selection_.selected.size();
           ++index) {
        const EntityId entity = after_selection_.selected[index];
        const bool was_locked = index < before_effectively_locked_.size() &&
                                before_effectively_locked_[index];
        if (!was_locked && scene().is_effectively_locked(entity)) {
          continue;
        }
        retained.push_back(entity);
      }
      after_selection_.selected = std::move(retained);
      if (std::find(after_selection_.selected.begin(),
                    after_selection_.selected.end(),
                    after_selection_.active) ==
          after_selection_.selected.end()) {
        after_selection_.active = after_selection_.selected.empty()
                                      ? kInvalidEntityId
                                      : after_selection_.selected.back();
      }
      after_selection_initialized_ = true;
    }
    return apply_selection(after_selection_, changed);
  }

private:
  static void normalize(SceneLayerState &state) {
    std::sort(state.layers.begin(), state.layers.end(),
              [](const SceneLayerRecord &left,
                 const SceneLayerRecord &right) {
                return left.id < right.id;
              });
    std::sort(state.assignments.begin(), state.assignments.end(),
              [](const SceneLayerAssignment &left,
                 const SceneLayerAssignment &right) {
                return left.entity < right.entity;
              });
  }

  bool apply_layer_state(const SceneLayerState &state, bool &changed) {
    if (scene().layer_state() == state) {
      return true;
    }
    if (!scene().replace_layer_state(state)) {
      return false;
    }
    changed = true;
    return true;
  }

  DocumentCommandResult apply(const SceneLayerState &state,
                              const SceneSelectionState &selection) {
    bool changed = false;
    if (!apply_layer_state(state, changed)) {
      return document_command_invalid_request_result(
          "Scene layer state is invalid");
    }
    return apply_selection(selection, changed);
  }

  DocumentCommandResult apply_selection(const SceneSelectionState &selection,
                                        bool changed) {
    if (!same_scene_selection(capture_scene_selection(scene()), selection)) {
      (void)apply_scene_selection(scene(), selection,
                                  SceneSelectionApplicationMode::UndoableCommand);
      changed = true;
    }
    return changed ? document_command_changed_result()
                   : document_command_unchanged_result();
  }

  SceneLayerState before_;
  SceneLayerState after_;
  SceneSelectionState before_selection_;
  SceneSelectionState after_selection_;
  std::vector<bool> before_effectively_locked_;
  bool after_selection_initialized_ = false;
};

} // namespace quader
