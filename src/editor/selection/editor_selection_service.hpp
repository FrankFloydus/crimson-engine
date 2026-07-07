////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "core/types.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "editor/modeling/modeling_selection_box_baseline.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "scene/components.hpp"

#include <optional>
#include <span>
#include <vector>

namespace quader {

class DocumentCommandService;
class ModelingSessionService;
class SceneDocument;

/**
 * Enumerates EditorSelectionTargetKind values used by the editor layer.
 */
enum class EditorSelectionTargetKind {
    None,
    Object,
    Group,
    Vertex,
    Edge,
    Face,
};

using EditorSelectionEdit = SceneSelectionEdit;

/**
 * Represents an Editor Selection Target value used by the editor selection authority.
 */
struct EditorSelectionTarget {
    EditorSelectionTargetKind kind = EditorSelectionTargetKind::None;
    EntityId entity = kInvalidEntityId;
    int modeling_object_id = kInvalidModelingObjectId;
    int vertex = -1;
    int edge_a = -1;
    int edge_b = -1;
    int face = -1;
};

/**
 * Stores the Editor Selection State data contract used by the editor selection authority.
 */
struct EditorSelectionState {
    ModelingSelectionMode mode = ModelingSelectionMode::Object;
    ModelingSelectionSummary summary;
    EditorSelectionTarget target;
    std::vector<EntityId> scene_selection;
    EntityId active_entity = kInvalidEntityId;
};

/**
 * Stores the selection baseline used to recompute live box-selection edits during one drag.
 */
struct EditorSelectionBoxBaseline {
    ModelingSelectionMode mode = ModelingSelectionMode::Object;
    std::vector<EntityId> scene_selection;
    EntityId active_entity = kInvalidEntityId;
    std::optional<ModelingSelectionBoxBaseline> modeling_selection;
};

/**
 * Coordinates Editor Selection Service responsibilities for the editor selection authority.
 */
class EditorSelectionService final {
public:
  EditorSelectionService(SceneDocument &scene,
                         DocumentCommandService &document_commands,
                         ModelingSessionService &modeling);

  [[nodiscard]] EditorSelectionState state() const;
  [[nodiscard]] EditorSelectionTarget target() const;
  [[nodiscard]] ModelingSelectionSummary summary() const;
  [[nodiscard]] ModelingHoverSummary hover_summary() const;
  [[nodiscard]] ModelingSelectionMode mode() const;

  DocumentCommandResult set_scene_selection(std::span<const EntityId> entities,
                                            EntityId active = kInvalidEntityId);
  DocumentCommandResult
  set_inspection_scene_selection(std::span<const EntityId> entities,
                                 EntityId active = kInvalidEntityId);
  DocumentCommandResult edit_scene_selection(EntityId entity,
                                             EditorSelectionEdit edit);
  DocumentCommandResult edit_scene_selection(std::span<const EntityId> entities,
                                             EditorSelectionEdit edit);
  ModelingCommandResult
  set_object_selection(std::span<const EntityId> entities,
                       EntityId active = kInvalidEntityId);
  ModelingCommandResult
  set_component_selection(const ModelingComponentSelectionRequest &request);
  ModelingCommandResult set_mode(ModelingSelectionMode mode);
  ModelingCommandResult select_all();
  ModelingCommandResult invert();
  ModelingCommandResult clear();

  [[nodiscard]] EditorSelectionBoxBaseline
  capture_box_selection_baseline() const;
  [[nodiscard]] ModelingSelectionSummary
  pick_select(const ModelingPickSelectRequest &request);
  [[nodiscard]] ModelingSelectionSummary
  box_select(const ModelingBoxSelectRequest &request);
  [[nodiscard]] ModelingSelectionSummary
  box_select(const ModelingBoxSelectRequest &request,
             const EditorSelectionBoxBaseline &baseline);
  ModelingCommandResult update_hover(const ModelingHoverRequest &request);
  ModelingCommandResult set_hover(const ModelingDirectHoverRequest &request);
  [[nodiscard]] ModelingHoverSummary
  preview_hover(const ModelingHoverRequest &request) const;
  ModelingCommandResult clear_hover();

private:
  [[nodiscard]] EditorSelectionTarget
  target_from_summary(const ModelingSelectionSummary &summary) const;
  [[nodiscard]] ModelingSelectionSummary
  box_select(const ModelingBoxSelectRequest &request,
             const EditorSelectionBoxBaseline *baseline);
  [[nodiscard]] ModelingSelectionSummary
  scene_pick_selection(EntityId target_entity, ModelingSelectionMode mode,
                       const ModelingPickSelectRequest &request);
  [[nodiscard]] ModelingSelectionSummary
  scene_box_selection(std::span<const EntityId> target_entities,
                      ModelingSelectionMode mode, ModelingSelectionEdit edit,
                      const EditorSelectionBoxBaseline *baseline = nullptr);
  [[nodiscard]] EntityId nearest_group_ancestor(EntityId entity) const;
  [[nodiscard]] ModelingSelectionSummary
  scene_summary_for(ModelingSelectionMode mode) const;

  SceneDocument &scene_;
  DocumentCommandService &document_commands_;
  ModelingSessionService &modeling_;
};

} // namespace quader
