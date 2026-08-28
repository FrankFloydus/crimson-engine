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

#include <cstdint>
#include <functional>
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
 * Stores the press-time selection baseline used by a deferred box or depth pick.
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
  using SceneSelectionTargetResolver = std::function<EntityId(EntityId)>;
  using SceneSelectionScopeFilter = std::function<bool(EntityId)>;
  using SceneSelectionHistorySuppressionProvider = std::function<bool()>;

  EditorSelectionService(SceneDocument &scene,
                         DocumentCommandService &document_commands,
                         ModelingSessionService &modeling);

  [[nodiscard]] EditorSelectionState state() const;
  [[nodiscard]] EditorSelectionTarget target() const;
  [[nodiscard]] ModelingSelectionSummary summary() const;
  [[nodiscard]] ModelingHoverSummary hover_summary() const;
  [[nodiscard]] ModelingSelectionMode mode() const;
  void set_scene_selection_scope_policy(
      SceneSelectionTargetResolver resolver,
      SceneSelectionScopeFilter filter);
  void set_scene_selection_history_suppression_provider(
      SceneSelectionHistorySuppressionProvider provider);

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
  ModelingCommandResult select_edge_direction();
  ModelingCommandResult select_edge_loops();
  ModelingCommandResult select_edge_ring();
  ModelingCommandResult select_faces_with_same_material();
  ModelingCommandResult select_coplanar_faces();
  ModelingCommandResult select_face_normal();
  ModelingCommandResult select_mesh_ngons();
  ModelingCommandResult select_similar_meshes();
  ModelingCommandResult grow();
  ModelingCommandResult shrink();

  [[nodiscard]] EditorSelectionBoxBaseline
  capture_box_selection_baseline() const;
  [[nodiscard]] ModelingSelectionSummary
  pick_select(const ModelingPickSelectRequest &request);
  [[nodiscard]] ModelingSelectionSummary
  pick_select(const ModelingPickSelectRequest &request,
              const EditorSelectionBoxBaseline &baseline);
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
  [[nodiscard]] ModelingSelectionSummary
  pick_select(const ModelingPickSelectRequest &request,
              const EditorSelectionBoxBaseline *baseline);
  [[nodiscard]] bool scene_target_selectable(EntityId entity) const;
  [[nodiscard]] std::vector<EntityId>
  scene_bulk_candidates(ModelingSelectionMode mode) const;
  [[nodiscard]] EditorSelectionTarget
  target_from_summary(const ModelingSelectionSummary &summary) const;
  [[nodiscard]] ModelingSelectionSummary
  box_select(const ModelingBoxSelectRequest &request,
             const EditorSelectionBoxBaseline *baseline);
  [[nodiscard]] ModelingSelectionSummary
  scene_pick_selection(EntityId target_entity, ModelingSelectionMode mode,
                       const ModelingPickSelectRequest &request,
                       const EditorSelectionBoxBaseline *baseline = nullptr);
  [[nodiscard]] ModelingSelectionSummary
  scene_box_selection(std::span<const EntityId> target_entities,
                      ModelingSelectionMode mode, ModelingSelectionEdit edit,
                      const EditorSelectionBoxBaseline *baseline = nullptr);
  [[nodiscard]] EntityId nearest_group_ancestor(EntityId entity) const;
  [[nodiscard]] ModelingSelectionSummary
  scene_summary_for(ModelingSelectionMode mode) const;
  DocumentCommandResult
  apply_scene_selection_state(SceneSelectionState selection);
  ModelingCommandResult apply_component_selection_command(
      ModelingComponentSelectionCommand command,
      std::span<const ModelingSelectionMode> allowed_modes);

  SceneDocument &scene_;
  DocumentCommandService &document_commands_;
  ModelingSessionService &modeling_;
  SceneSelectionTargetResolver scene_target_resolver_;
  SceneSelectionScopeFilter scene_scope_filter_;
  SceneSelectionHistorySuppressionProvider
      scene_selection_history_suppression_provider_;
  mutable bool summary_cache_valid_ = false;
  mutable bool summary_cache_preview_active_ = false;
  mutable std::uint64_t summary_cache_scene_revision_ = 0;
  mutable std::uint64_t summary_cache_modeling_content_revision_ = 0;
  mutable std::uint64_t summary_cache_modeling_selection_revision_ = 0;
  mutable int summary_cache_modeling_selection_mode_ = -1;
  mutable ModelingSelectionSummary summary_cache_modeling_;
  mutable ModelingSelectionSummary summary_cache_resolved_;
};

} // namespace quader
