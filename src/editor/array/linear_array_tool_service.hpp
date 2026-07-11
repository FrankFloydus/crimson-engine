////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_revision.hpp"
#include "core/command_result.hpp"
#include "core/types.hpp"
#include "math/types.hpp"

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
class SceneDocument;

inline constexpr std::string_view kLinearArrayToolId = "linear_array";
inline constexpr std::string_view kLinearArrayCountSetting =
    "linear_array.count";
inline constexpr std::uint32_t kLinearArrayDefaultCount = 4u;
inline constexpr std::uint32_t kLinearArrayMinimumCount = 2u;
inline constexpr std::uint32_t kLinearArrayMaximumCount = 128u;

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
 * Describes one shared source mesh and the world transforms of preview copies.
 */
struct LinearArrayPreviewState {
  bool active = false;
  EntityId source_entity = kInvalidEntityId;
  AssetId mesh = kInvalidAssetId;
  AssetId material = kInvalidAssetId;
  std::vector<QMat4> copy_world_transforms;
  std::uint64_t revision = 0;
};

/**
 * Owns one non-destructive Linear Array tool session until Apply or Cancel.
 */
class LinearArrayToolService final {
public:
  LinearArrayToolService(SceneDocument &scene, AssetRegistry &assets,
                         EditorHistoryService &history,
                         ModelingSessionService &modeling,
                         EditorSelectionService &selection);

  [[nodiscard]] bool active() const;
  [[nodiscard]] bool has_session() const { return session_.has_value(); }
  [[nodiscard]] bool can_activate() const;
  [[nodiscard]] std::uint32_t count() const;
  [[nodiscard]] LinearArrayHandleState handle_state() const;
  [[nodiscard]] LinearArrayPreviewState preview_state() const;

  CommandResult activate(double fallback_grid_size);
  CommandResult refresh();
  CommandResult cancel();
  CommandResult apply();
  CommandResult set_count(std::uint32_t count);
  CommandResult set_axis_constraint(LinearArrayAxisConstraint constraint);
  CommandResult set_endpoint(QVec3 world_position, bool snap_to_grid,
                             double grid_size);
  void begin_endpoint_drag();
  void end_endpoint_drag();

private:
  /**
   * Captures every source invariant needed to reject stale preview commits.
   */
  struct Session {
    EntityId source_entity = kInvalidEntityId;
    int source_modeling_object_id = -1;
    std::string source_uuid;
    EntityId source_parent = kInvalidEntityId;
    AssetId source_mesh = kInvalidAssetId;
    AssetId source_material = kInvalidAssetId;
    MeshAssetRevision source_mesh_revision;
    QMat4 source_world_transform;
    QVec3 source_world = {};
    QVec3 endpoint_world = {};
    std::uint32_t count = kLinearArrayDefaultCount;
    LinearArrayAxisConstraint constraint = LinearArrayAxisConstraint::Free;
    std::uint64_t selection_revision = 0;
    std::uint64_t revision = 1;
    bool dragging = false;
  };

  [[nodiscard]] bool valid_source_selection(EntityId &source) const;
  [[nodiscard]] bool session_source_is_valid(const Session &session) const;
  [[nodiscard]] static QMat4
  with_world_origin(QMat4 transform, QVec3 world_origin);

  SceneDocument &scene_;
  AssetRegistry &assets_;
  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
  EditorSelectionService &selection_;
  std::optional<Session> session_;
};

} // namespace quader
