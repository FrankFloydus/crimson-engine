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
#include "editor/selection/editor_sub_selection_provider.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "scene/components.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
    SubElement,
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
    EditorSubSelectionKind sub_selection_kind =
        EditorSubSelectionKind::None;
    std::optional<EditorSubSelectionTarget> sub_element;
};

/** Identifies the owner domain responsible for a resolved selection target. */
enum class ResolvedSelectionDomain {
  SceneEntity,
  ModelingComponent,
  RegisteredSubElement,
};

/** One provider-neutral target already resolved by viewport hit arbitration. */
struct ResolvedSelectionTarget {
  ResolvedSelectionDomain domain = ResolvedSelectionDomain::SceneEntity;
  EntityId entity = kInvalidEntityId;
  int modeling_object_id = kInvalidModelingObjectId;
  ModelingActiveSelectionKind component_kind =
      ModelingActiveSelectionKind::None;
  int vertex = -1;
  int edge_a = -1;
  int edge_b = -1;
  int face = -1;
  EditorSubSelectionKind sub_selection_kind =
      EditorSubSelectionKind::None;
  std::optional<EditorSubSelectionTarget> sub_element;
  EntityId source_occurrence = kInvalidEntityId;
  std::uint64_t source_primitive = 0;

  [[nodiscard]] bool operator==(const ResolvedSelectionTarget &) const =
      default;
};

/** Query-only ordering facts; application consumes only target identity. */
struct ResolvedSelectionHit {
  ResolvedSelectionTarget target;
  double ray_distance = 0.0;
  double pick_distance_pixels = 0.0;
  std::optional<QVec3> world_position;
  std::vector<ModelingSelectionVisibilityWitness> visibility_witnesses;
  std::array<std::uint64_t, 4> stable_key{};
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
    std::optional<EditorSubSelectionBaseline> sub_selection;
};

/** Carries one resolved click and the canonical policy used to apply it. */
struct ResolvedSelectionClickRequest {
  std::optional<ResolvedSelectionTarget> winner;
  ModelingPickSelectRequest policy;
  std::optional<EditorSelectionBoxBaseline> baseline;
  /**
   * Routes an empty hit through the current registered sub-selection owner.
   * This preserves the canonical miss/modifier policy for provider-owned
   * elements without turning a provider-only gesture into a scene miss.
   */
  EditorSubSelectionKind miss_sub_selection_kind =
      EditorSubSelectionKind::None;
};

/** Carries resolved marquee targets, baseline, and canonical edit policy. */
struct ResolvedSelectionMarqueeRequest {
  std::vector<ResolvedSelectionTarget> targets;
  ModelingSelectionEdit edit = ModelingSelectionEdit::Replace;
  ModelingSelectionMode mode = ModelingSelectionMode::Object;
  ModelingBoxSelectionRule rule = ModelingBoxSelectionRule::DragDirection;
  EditorSelectionBoxBaseline baseline;
};

/** Checked outcome of applying one pre-resolved canonical selection edit. */
struct ResolvedSelectionApplyResult {
  ModelingCommandResult command;
  ModelingSelectionSummary selection;
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
  void set_sub_selection_provider(EditorSubSelectionProvider *provider);
  [[nodiscard]] bool has_sub_selection() const;
  [[nodiscard]] std::optional<EditorSubSelectionBaseline>
  capture_registered_sub_selection_baseline() const;
  [[nodiscard]] ModelingCommandResult restore_registered_sub_selection_baseline(
      const std::optional<EditorSubSelectionBaseline> &baseline);
  [[nodiscard]] std::optional<ModelingCommandResult>
  try_delete_sub_selection();

  DocumentCommandResult set_scene_selection(std::span<const EntityId> entities,
                                            EntityId active = kInvalidEntityId);
  DocumentCommandResult
  set_inspection_scene_selection(std::span<const EntityId> entities,
                                 EntityId active = kInvalidEntityId);
  /**
   * Applies an exact non-history scene selection and publishes it atomically
   * to modeling and registered sub-selection owners.
   */
  DocumentCommandResult
  synchronize_scene_selection(std::span<const EntityId> entities,
                              EntityId active = kInvalidEntityId);
  /** Publishes the scene's current selection through every selection owner. */
  [[nodiscard]] DocumentCommandResult publish_scene_selection();
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
  [[nodiscard]] ResolvedSelectionApplyResult
  apply_resolved_click(const ResolvedSelectionClickRequest &request);
  [[nodiscard]] ResolvedSelectionApplyResult
  apply_resolved_marquee(const ResolvedSelectionMarqueeRequest &request);
  [[nodiscard]] std::optional<ResolvedSelectionHit>
  query_modeling_hit(const ModelingPickSelectRequest &request) const;
  [[nodiscard]] std::vector<ResolvedSelectionHit>
  query_modeling_depth_hits(const ModelingPickSelectRequest &request) const;
  [[nodiscard]] std::vector<ResolvedSelectionHit>
  query_modeling_marquee_hits(
      const ModelingBoxSelectRequest &request) const;
  /**
   * Advances one provider-neutral depth stack. Ordinary and depth-cycle picks
   * share this state so meshes, Curves, billboards, and formal occurrences
   * cannot maintain independent cursors.
   */
  [[nodiscard]] std::optional<ResolvedSelectionTarget>
  resolve_depth_arbitrated_target(
      std::span<const ResolvedSelectionTarget> ordered_depth_targets,
      std::optional<ResolvedSelectionTarget> ordinary_target,
      const ModelingPickSelectRequest &request);
  ModelingCommandResult preview_resolved_hover(
      std::optional<ResolvedSelectionTarget> target);
  [[nodiscard]] std::optional<ResolvedSelectionTarget>
  resolved_hover_target() const { return resolved_hover_target_; }
  /**
   * Resolves one scene hit through the installed occurrence/scope policy and
   * promotes it to its nearest selectable group when one exists. Tools that
   * pick hierarchy roots use this owner instead of duplicating group walks.
   */
  [[nodiscard]] EntityId
  group_promoted_scene_target(EntityId entity) const;
  /** Applies the ordinary pick policy to an already resolved native entity. */
  [[nodiscard]] DocumentCommandResult
  pick_scene_entity(EntityId entity, const ModelingPickSelectRequest &request);
  /**
   * Modeling-only compatibility seam. Public and viewport marquee selection
   * must commit typed candidates through apply_resolved_marquee().
   */
  [[nodiscard]] ModelingSelectionSummary
  box_select(const ModelingBoxSelectRequest &request);
  [[nodiscard]] ModelingSelectionSummary
  box_select(const ModelingBoxSelectRequest &request,
             const EditorSelectionBoxBaseline &baseline);
  /**
   * Resolves a polygon modeling-element ray query, then publishes its typed
   * target through the same cross-domain hover transaction as the viewport.
   * This compatibility request has no camera or screen position, so it does
   * not synthesize Curve, billboard, or other screen-space provider hits.
   */
  ModelingCommandResult update_hover(const ModelingHoverRequest &request);
  /** Publishes one explicit polygon element through the resolved authority. */
  ModelingCommandResult set_hover(const ModelingDirectHoverRequest &request);
  /** Queries the polygon hover owner without changing any retained hover. */
  [[nodiscard]] ModelingHoverSummary
  preview_hover(const ModelingHoverRequest &request) const;
  ModelingCommandResult clear_hover();

private:
  [[nodiscard]] bool scene_target_selectable(EntityId entity) const;
  [[nodiscard]] bool component_owner_selectable(EntityId entity) const;
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
  [[nodiscard]] ResolvedSelectionApplyResult scene_pick_selection_result(
      EntityId target_entity, ModelingSelectionMode mode,
      const ModelingPickSelectRequest &request,
      const EditorSelectionBoxBaseline *baseline = nullptr);
  [[nodiscard]] ModelingSelectionSummary
  scene_box_selection(std::span<const EntityId> target_entities,
                      ModelingSelectionMode mode, ModelingSelectionEdit edit,
                      const EditorSelectionBoxBaseline *baseline = nullptr);
  [[nodiscard]] ResolvedSelectionApplyResult scene_box_selection_result(
      std::span<const EntityId> target_entities, ModelingSelectionMode mode,
      ModelingSelectionEdit edit,
      const EditorSelectionBoxBaseline *baseline = nullptr);
  [[nodiscard]] ModelingCommandResult restore_sub_selection_after_failure(
      const EditorSubSelectionBaseline &baseline,
      std::string_view failure_message);
  ModelingCommandResult publish_resolved_hover(
      std::optional<ResolvedSelectionTarget> target,
      bool explicit_modeling_element_request,
      const ModelingHoverSummary *modeling_preview = nullptr);
  [[nodiscard]] EntityId nearest_group_ancestor(EntityId entity) const;
  [[nodiscard]] ModelingSelectionSummary
  scene_summary_for(ModelingSelectionMode mode) const;
  DocumentCommandResult
  apply_scene_selection_state(SceneSelectionState selection);
  DocumentCommandResult
  apply_runtime_scene_selection_state(SceneSelectionState selection);
  ModelingCommandResult apply_component_selection_command(
      ModelingComponentSelectionCommand command,
      std::span<const ModelingSelectionMode> allowed_modes);
  [[nodiscard]] EditorSubSelectionSnapshot
  current_sub_selection_snapshot() const;

  SceneDocument &scene_;
  DocumentCommandService &document_commands_;
  ModelingSessionService &modeling_;
  SceneSelectionTargetResolver scene_target_resolver_;
  SceneSelectionScopeFilter scene_scope_filter_;
  SceneSelectionHistorySuppressionProvider
      scene_selection_history_suppression_provider_;
  EditorSubSelectionProvider *sub_selection_provider_ = nullptr;
  mutable bool summary_cache_valid_ = false;
  mutable bool summary_cache_preview_active_ = false;
  mutable std::uint64_t summary_cache_scene_revision_ = 0;
  mutable std::uint64_t summary_cache_modeling_content_revision_ = 0;
  mutable std::uint64_t summary_cache_modeling_selection_revision_ = 0;
  mutable int summary_cache_modeling_selection_mode_ = -1;
  mutable ModelingSelectionSummary summary_cache_modeling_;
  mutable ModelingSelectionSummary summary_cache_resolved_;
  mutable EditorSubSelectionSnapshot summary_cache_sub_selection_;
  std::optional<ResolvedSelectionTarget> resolved_hover_target_;
  bool resolved_depth_cycle_active_ = false;
  std::string resolved_depth_cycle_signature_;
  QVec3 resolved_depth_cycle_origin_{};
  std::size_t resolved_depth_cycle_index_ = 0u;
};

} // namespace quader
