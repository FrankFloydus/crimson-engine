////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_revision.hpp"
#include "assets/material_reference.hpp"
#include "core/command_result.hpp"
#include "core/types.hpp"
#include "editor/array/instance_array_source.hpp"
#include "editor/modeling/linear_array_layout.hpp"
#include "math/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class AssetRegistry;
class EditorHistoryService;
class EditorSelectionService;
class ModelingSessionService;
class SceneInstanceService;
class SceneDocument;

inline constexpr std::string_view kLinearArrayToolId = "linear_array";
inline constexpr std::string_view kLinearArrayCountSetting =
    "linear_array.count";
inline constexpr std::uint32_t kLinearArrayDefaultCount = 4u;
inline constexpr std::uint32_t kLinearArrayMinimumCount =
    kLinearArrayLayoutMinimumCount;
inline constexpr std::uint32_t kLinearArrayMaximumCount =
    kLinearArrayLayoutMaximumCount;
inline constexpr std::size_t kLinearArrayMaximumTotalElements =
    kLinearArrayLayoutMaximumElements;

/**
 * Selects the world-space constraint used while dragging endpoint B.
 */
enum class LinearArrayAxisConstraint {
  Free,
  X,
  Y,
  Z,
};

/**
 * Exposes the renderer-neutral A/B handle state for every viewport pane.
 */
struct LinearArrayHandleState {
  bool visible = false;
  bool dragging = false;
  QVec3 source_world = {};
  QVec3 endpoint_world = {};
  LinearArrayAxisConstraint constraint = LinearArrayAxisConstraint::Free;
  std::uint64_t revision = 0;
};

/**
 * Describes one source mesh and its transform-only generated preview copies.
 */
struct LinearArrayPreviewSource {
  EntityId source_entity = kInvalidEntityId;
  /// Canonical scene entity that owns this preview mesh and material.
  EntityId material_entity = kInvalidEntityId;
  AssetId mesh = kInvalidAssetId;
  MaterialReference material;
  std::vector<QMat4> generated_world_transforms;
};

/**
 * Exposes all generated preview instances for one live Linear Array session.
 */
struct LinearArrayPreviewState {
  bool active = false;
  std::vector<LinearArrayPreviewSource> sources;
  std::uint64_t revision = 0;
};

/**
 * Owns one non-destructive multi-source Linear Array tool session.
 */
class LinearArrayToolService final {
public:
  LinearArrayToolService(SceneDocument &scene, AssetRegistry &assets,
                         EditorHistoryService &history,
                         ModelingSessionService &modeling,
                         EditorSelectionService &selection,
                         SceneInstanceService &instances);

  [[nodiscard]] bool active() const;
  [[nodiscard]] bool has_session() const { return session_.has_value(); }
  [[nodiscard]] bool can_activate() const;
  [[nodiscard]] std::uint64_t tool_settings_revision() const {
    return revision_counter_;
  }
  [[nodiscard]] std::uint32_t count() const;
  [[nodiscard]] std::uint32_t maximum_count() const;
  [[nodiscard]] LinearArrayHandleState handle_state() const;
  /// The returned view remains valid until this service is mutated.
  [[nodiscard]] const LinearArrayPreviewState &preview_state() const;

  CommandResult activate(double fallback_grid_size);
  CommandResult refresh();
  CommandResult cancel();
  CommandResult apply();
  CommandResult set_count(std::uint32_t count);
  CommandResult adjust_count(int delta);
  CommandResult set_axis_constraint(LinearArrayAxisConstraint constraint);
  CommandResult set_endpoint(QVec3 world_position, bool snap_to_grid,
                             double grid_size);
  void begin_endpoint_drag();
  void end_endpoint_drag();

private:
  /// Captures every source invariant required to reject a stale session.
  struct SourceSnapshot {
    EntityId entity = kInvalidEntityId;
    int modeling_object_id = -1;
    std::string uuid;
    EntityId parent = kInvalidEntityId;
    AssetId mesh = kInvalidAssetId;
    MaterialReference material;
    MeshAssetRevision mesh_revision;
    MaterialAssetRevision material_revision;
    QMat4 world_transform;
    QVec3 visible_pivot_world = {};
    EntityId instance_definition = kInvalidEntityId;
    std::vector<InstanceArrayPreviewMeshSnapshot> instance_meshes;
  };

  /// Stores one shared-span live-preview transaction.
  struct Session {
    std::vector<SourceSnapshot> sources;
    EntityId primary_source = kInvalidEntityId;
    QVec3 anchor_world = {};
    QVec3 endpoint_world = {};
    std::uint32_t count = kLinearArrayDefaultCount;
    LinearArrayAxisConstraint constraint = LinearArrayAxisConstraint::Free;
    std::uint64_t selection_revision = 0;
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
    bool dragging = false;
    bool instance_occurrences = false;
  };

  [[nodiscard]] bool
  valid_source_selection(std::vector<EntityId> &ordered_sources,
                         EntityId &primary_source,
                         bool &instance_occurrences) const;
  [[nodiscard]] bool can_activate_preflight() const;
  [[nodiscard]] bool session_sources_are_valid(const Session &session) const;
  [[nodiscard]] bool
  compute_session_sources_are_valid(const Session &session) const;
  [[nodiscard]] static std::uint32_t
  maximum_count_for(std::size_t source_count);
  [[nodiscard]] std::optional<QVec3>
  source_visible_pivot(EntityId entity) const;
  [[nodiscard]] std::uint64_t issue_revision();
  void clear_preview_cache();

  SceneDocument &scene_;
  AssetRegistry &assets_;
  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
  EditorSelectionService &selection_;
  SceneInstanceService &instances_;
  std::optional<Session> session_;
  std::uint64_t revision_counter_ = 0;
  mutable std::uint64_t validated_session_generation_ = 0;
  mutable std::uint64_t validated_scene_revision_ = 0;
  mutable std::uint64_t validated_asset_revision_ = 0;
  mutable std::uint64_t validated_object_map_revision_ = 0;
  mutable int validated_selection_mode_ = -1;
  mutable bool cached_session_sources_valid_ = false;
  mutable std::uint64_t can_activate_scene_revision_ = 0;
  mutable int can_activate_selection_mode_ = -1;
  mutable bool can_activate_cached_value_ = false;
  mutable bool can_activate_cache_valid_ = false;
  mutable std::uint64_t cached_preview_revision_ = 0;
  mutable LinearArrayPreviewState cached_preview_;
};

} // namespace quader
