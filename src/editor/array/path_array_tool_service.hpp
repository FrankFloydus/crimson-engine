////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_revision.hpp"
#include "assets/material_reference.hpp"
#include "assets/mesh_asset.hpp"
#include "core/command_result.hpp"
#include "core/types.hpp"
#include "editor/array/path_array_layout.hpp"
#include "editor/modeling/modeling_preview_types.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "math/types.hpp"
#include "scene/components.hpp"

#include <quader/modeling/curves/curve_arc_length_path.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class EditorHistoryService;
class EditorSelectionService;
class ModelingSessionService;
class SceneDocument;
class SceneStructuralLifecycleService;
class SceneTextureMappingPlan;

inline constexpr std::string_view kPathArrayToolId = "path_array";
inline constexpr std::string_view kPathArrayCountSetting = "path_array.count";
inline constexpr std::string_view kPathArrayDistanceSetting =
    "path_array.distance";
inline constexpr std::string_view kPathArrayFollowPathSetting =
    "path_array.follow_path";
inline constexpr std::string_view kPathArrayForwardAxisSetting =
    "path_array.forward_axis";

inline constexpr std::uint32_t kPathArrayDefaultCount = 4U;
inline constexpr std::uint32_t kPathArrayMinimumCount =
    kPathArrayLayoutMinimumCount;
inline constexpr std::uint32_t kPathArrayMaximumCount =
    kPathArrayLayoutMaximumCount;
inline constexpr std::size_t kPathArrayMaximumHierarchyElements = 1024U;
inline constexpr std::size_t kPathArrayMaximumPreviewParticipants = 1024U;
inline constexpr std::size_t kPathArrayMaximumLights = 128U;
inline constexpr std::size_t kPathArrayMaximumShadowLights = 16U;
inline constexpr bool kPathArrayDefaultFollowPath = true;
inline constexpr PathArraySignedAxis kPathArrayDefaultForwardAxis =
    PathArraySignedAxis::PositiveX;

/**
 * Persists the preferred path spacing across SceneSession replacement.
 *
 * An empty value deliberately means "fit the default count to the path". The
 * first activation initializes that intent; only an explicit Distance edit or
 * Reset replaces it afterward. The application lifetime owner supplies one
 * shared instance to each SceneSession.
 */
struct PathArraySessionDefaults final {
  std::optional<double> remembered_distance;
};

/** Identifies the modal Path Array lifecycle phase. */
enum class PathArrayToolStage : std::uint8_t {
  Inactive,
  AwaitingSource,
  Previewing,
};

/**
 * Publishes one complete, source-inclusive Path Array preview generation.
 */
struct PathArrayPreviewState final {
  bool active = false;
  EntityId path_entity = kInvalidEntityId;
  EntityId source_root = kInvalidEntityId;
  double effective_distance = 0.0;
  double total_path_length = 0.0;
  std::vector<QMat4> root_world_transforms;
  ModelingToolPreviewScene scene;
  std::uint64_t revision = 0U;
};

/** Pins one embedded material dependency used by a source fingerprint. */
struct PathArrayMaterialRevisionKey final {
  AssetId asset = kInvalidAssetId;
  MaterialAssetRevision revision;

  friend bool operator==(const PathArrayMaterialRevisionKey &,
                         const PathArrayMaterialRevisionKey &) = default;
};

/**
 * Owns the modal Curve-driven hierarchy-array session and its atomic commit.
 */
class PathArrayToolService final {
public:
  PathArrayToolService(SceneDocument &scene, AssetRegistry &assets,
                       EditorHistoryService &history,
                       ModelingSessionService &modeling,
                       EditorSelectionService &selection,
                       DocumentCommandService &document_commands,
                       SceneStructuralLifecycleService &structural_lifecycle,
                       PathArraySessionDefaults &defaults);

  [[nodiscard]] bool active() const;
  [[nodiscard]] bool has_session() const { return session_.has_value(); }
  [[nodiscard]] bool has_source() const;
  [[nodiscard]] bool preview_valid() const;
  [[nodiscard]] bool can_activate() const;
  [[nodiscard]] PathArrayToolStage stage() const;
  [[nodiscard]] PathArrayToolStage state() const { return stage(); }
  [[nodiscard]] EntityId path_entity() const;
  [[nodiscard]] EntityId source_root() const;
  [[nodiscard]] bool blocks_document_mutation(EntityId entity) const;
  [[nodiscard]] bool blocks_modeling_content_mutation() const {
    return active() && !committing_;
  }
  [[nodiscard]] std::optional<EntityId>
  resolve_source_candidate(EntityId candidate) const;

  [[nodiscard]] std::uint32_t count() const;
  [[nodiscard]] double distance() const;
  [[nodiscard]] double maximum_distance() const;
  [[nodiscard]] bool follow_path() const;
  [[nodiscard]] PathArraySignedAxis forward_axis() const;
  [[nodiscard]] std::uint32_t maximum_count() const;
  [[nodiscard]] std::uint64_t tool_settings_revision() const {
    return tool_settings_revision_;
  }
  /// The returned view remains valid until this service is mutated.
  [[nodiscard]] const PathArrayPreviewState &preview_state() const;

  CommandResult activate();
  CommandResult refresh();
  CommandResult cancel();
  CommandResult apply();
  CommandResult select_source(EntityId source);
  CommandResult clear_source();

  CommandResult set_count(std::uint32_t count);
  CommandResult set_distance(double distance);
  CommandResult set_follow_path(bool follow);
  CommandResult set_forward_axis(PathArraySignedAxis axis);
  CommandResult reset_settings();

private:
  /** Pins every authored property and dependency used to validate one node. */
  struct HierarchyNodeFingerprint final {
    EntityId entity = kInvalidEntityId;
    std::string uuid;
    EntityId parent = kInvalidEntityId;
    EntityKind kind = EntityKind::Generic;
    TransformComponent transform;
    VisibilityComponent visibility;
    LockComponent lock;
    SceneLayerId layer_id = kInvalidSceneLayerId;
    std::optional<ModelingObjectComponent> modeling_object;
    std::optional<MeshComponent> mesh;
    std::optional<CurveComponent> curve;
    std::optional<MaterialComponent> material;
    std::optional<TextureMappingComponent> texture_mapping;
    std::optional<LightComponent> light;
    std::optional<InstanceDefinitionComponent> instance_definition;
    std::optional<InstanceOccurrenceComponent> instance_occurrence;
    MeshAssetRevision mesh_revision;
    CurveAssetRevision curve_revision;
    std::vector<PathArrayMaterialRevisionKey> material_revisions;
  };

  /** Caches one visible mesh member independently of station transforms. */
  struct MeshParticipant final {
    EntityId source_entity = kInvalidEntityId;
    EntityId visibility_entity = kInvalidEntityId;
    MaterialComponent material;
    MeshAsset mesh;
    QMat4 member_from_root;
    bool double_sided = false;
    bool cast_shadows = false;
  };

  /** Caches one visible Curve member independently of station transforms. */
  struct CurveParticipant final {
    EntityId source_entity = kInvalidEntityId;
    EntityId visibility_entity = kInvalidEntityId;
    AssetId curve = kInvalidAssetId;
    QMat4 member_from_root;
  };

  /** Caches one visible light member and all of its authored light policy. */
  struct LightParticipant final {
    EntityId source_entity = kInvalidEntityId;
    EntityId visibility_entity = kInvalidEntityId;
    LightComponent light;
    QMat4 member_from_root;
  };

  /** Describes the local cage shown for a visible group or generic node. */
  struct MarkerParticipant final {
    EntityId source_entity = kInvalidEntityId;
    QVec3 local_min = {};
    QVec3 local_max = {};
    QMat4 member_from_root;
  };

  /** Pins a complete source hierarchy and its renderer-neutral participants. */
  struct SourceSnapshot final {
    EntityId root = kInvalidEntityId;
    std::string uuid;
    EntityId parent = kInvalidEntityId;
    TransformComponent local_transform;
    QMat4 world_transform;
    QVec3 visible_pivot_world = {};
    std::size_t hierarchy_node_count = 1U;
    std::size_t visible_preview_participant_count = 1U;
    std::size_t light_count = 0U;
    std::size_t shadow_light_count = 0U;
    std::vector<HierarchyNodeFingerprint> hierarchy;
    std::vector<MeshParticipant> meshes;
    std::vector<CurveParticipant> curves;
    std::vector<LightParticipant> lights;
    std::vector<MarkerParticipant> markers;
    ModelingToolPreviewSuppression suppression;
  };

  /**
   * Pins the driving Curve and its converted native layout stations so
   * lightweight settings edits never rebuild the world-space path backbone.
   */
  struct PathSnapshot final {
    EntityId entity = kInvalidEntityId;
    std::string uuid;
    EntityId parent = kInvalidEntityId;
    int modeling_object_id = kInvalidModelingObjectId;
    AssetId curve = kInvalidAssetId;
    CurveAssetRevision curve_revision;
    std::uint64_t curve_content_revision = 0U;
    QMat4 world_transform;
    modeling::CurveArcLengthPath path;
    std::vector<PathArrayPathStation> layout_stations;
  };

  /** Owns all state retained from activation until apply or cancellation. */
  struct Session final {
    PathSnapshot path;
    EditorSelectionBoxBaseline path_selection;
    std::optional<SourceSnapshot> source;
    std::uint32_t count = kPathArrayDefaultCount;
    // Distance is direct user state. Count and source-capacity changes never
    // derive, clamp, or otherwise replace it.
    double distance = 0.0;
    bool follow_path = kPathArrayDefaultFollowPath;
    PathArraySignedAxis forward_axis = kPathArrayDefaultForwardAxis;
    // Path and source generations pin expensive captures independently from
    // lightweight layout edits. Settings setters update layout_revision only.
    std::uint64_t path_generation = 0U;
    std::uint64_t source_generation = 0U;
    std::uint64_t layout_revision = 0U;
  };

  [[nodiscard]] std::optional<PathSnapshot>
  capture_selected_path() const;
  [[nodiscard]] bool path_is_valid(const PathSnapshot &path) const;
  [[nodiscard]] std::optional<SourceSnapshot>
  capture_source(EntityId source) const;
  [[nodiscard]] bool source_is_current(const SourceSnapshot &source) const;
  [[nodiscard]] std::optional<PathArrayLayoutResult>
  build_layout(const Session &session) const;
  [[nodiscard]] std::optional<std::vector<std::vector<QMat4>>>
  build_preview_mesh_mappings(
      const Session &session,
      const PathArrayLayoutResult &layout) const;
  [[nodiscard]] static bool
  same_source_snapshot(const SourceSnapshot &left,
                       const SourceSnapshot &right);
  [[nodiscard]] static std::uint32_t
  maximum_count_for(const SourceSnapshot &source);
  [[nodiscard]] static double fit_distance(double path_length, bool closed,
                                           std::uint32_t count);
  [[nodiscard]] std::uint64_t issue_revision();
  void issue_tool_settings_revision();
  void invalidate_preview();
  void clear_preview();
  CommandResult apply_selection_baseline(
      const EditorSelectionBoxBaseline &baseline);
  CommandResult restore_selection_after_failed_apply(
      const EditorSelectionBoxBaseline &selection, CommandResult failure);

  SceneDocument &scene_;
  AssetRegistry &assets_;
  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
  EditorSelectionService &selection_;
  DocumentCommandService &document_commands_;
  SceneStructuralLifecycleService &structural_lifecycle_;
  PathArraySessionDefaults &defaults_;
  std::optional<Session> session_;
  bool committing_ = false;
  std::uint64_t revision_counter_ = 0U;
  std::uint64_t tool_settings_revision_ = 0U;
  mutable std::uint64_t cached_path_generation_ = 0U;
  mutable std::uint64_t cached_path_scene_revision_ = 0U;
  mutable std::uint64_t cached_path_asset_revision_ = 0U;
  mutable std::uint64_t cached_path_object_map_revision_ = 0U;
  mutable std::uint64_t cached_path_modeling_content_revision_ = 0U;
  mutable bool cached_path_valid_ = false;
  mutable bool cached_can_activate_initialized_ = false;
  mutable std::uint64_t cached_can_activate_scene_revision_ = 0U;
  mutable std::uint64_t cached_can_activate_asset_revision_ = 0U;
  mutable std::uint64_t cached_can_activate_object_map_revision_ = 0U;
  mutable std::uint64_t cached_can_activate_modeling_content_revision_ = 0U;
  mutable int cached_can_activate_selection_mode_ = -1;
  mutable bool cached_can_activate_ = false;
  mutable std::uint64_t cached_source_generation_ = 0U;
  mutable std::uint64_t cached_source_scene_revision_ = 0U;
  mutable std::uint64_t cached_source_asset_revision_ = 0U;
  mutable std::uint64_t cached_source_object_map_revision_ = 0U;
  mutable std::uint64_t cached_source_modeling_content_revision_ = 0U;
  mutable bool cached_source_valid_ = false;
  // A changed capture is retained for refresh() to consume, avoiding a second
  // hierarchy traversal after source_is_current() detects a revision change.
  mutable std::optional<SourceSnapshot> cached_changed_source_;
  mutable std::uint64_t cached_texture_plan_source_generation_ = 0U;
  mutable bool cached_texture_plan_texture_lock_ = false;
  mutable bool cached_texture_plan_texture_lock_scale_ = false;
  mutable bool cached_texture_plan_attempted_ = false;
  mutable std::shared_ptr<const SceneTextureMappingPlan>
      cached_texture_mapping_plan_;
  mutable bool cached_preview_attempted_ = false;
  mutable std::uint64_t cached_preview_attempt_layout_revision_ = 0U;
  mutable std::uint64_t cached_preview_attempt_scene_revision_ = 0U;
  mutable std::uint64_t cached_preview_attempt_asset_revision_ = 0U;
  mutable std::uint64_t cached_preview_attempt_object_map_revision_ = 0U;
  mutable std::uint64_t cached_preview_attempt_modeling_content_revision_ = 0U;
  mutable bool cached_preview_attempt_texture_lock_ = false;
  mutable bool cached_preview_attempt_texture_lock_scale_ = false;
  mutable bool cached_preview_current_valid_ = false;
  mutable std::uint64_t preview_publication_revision_ = 0U;
  mutable PathArrayPreviewState cached_preview_;
};

} // namespace quader
