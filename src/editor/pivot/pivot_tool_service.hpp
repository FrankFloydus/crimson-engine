////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "core/types.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/pivot/pivot_types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace quader {

class AssetRegistry;
class EditorHistoryService;
class EditorSelectionService;
class ModelingSessionService;
class SceneDocument;
struct SceneBounds;

/**
 * Owns persistent and temporary pivot placement state for the editor session.
 */
class PivotToolService final {
public:
  PivotToolService(SceneDocument &scene, AssetRegistry &assets,
                   EditorHistoryService &history,
                   ModelingSessionService &modeling);

  [[nodiscard]] bool persistent_active() const;
  [[nodiscard]] PivotSnapSettings snap_settings() const { return snap_settings_; }
  [[nodiscard]] PivotHandleState handle_state() const;
  [[nodiscard]] SelectionPivotOverride selection_pivot_override() const;
  [[nodiscard]] std::uint64_t selection_pivot_revision() const {
    return selection_pivot_revision_;
  }
  [[nodiscard]] std::optional<QVec3> persistent_preview_position() const;
  [[nodiscard]] bool
  can_activate_persistent(const EditorSelectionService &selection) const;
  [[nodiscard]] bool can_copy_pivot_from_active(
      const EditorSelectionService &selection) const;

  CommandResult activate_persistent(EditorSelectionService &selection);
  CommandResult cancel_persistent();
  CommandResult apply_persistent();
  CommandResult
  copy_pivot_from_active(const EditorSelectionService &selection);
  CommandResult set_snap_enabled(std::string_view setting_id, bool enabled);
  CommandResult execute_align_action(std::string_view action_id);
  void set_persistent_preview_position(QVec3 world_position);

  void begin_quick(QVec3 initial_world_position);
  void set_quick_preview_position(QVec3 world_position);
  void end_quick_edit();
  void clear_quick_override();
  [[nodiscard]] bool quick_edit_active() const;

private:
  /**
   * Distinguishes direct mesh rebasing from hierarchy-root rebasing.
   */
  enum class PivotTargetKind {
      Mesh,
      Group,
  };

  /**
   * Captures one modal Pivot tool session until it is applied or canceled.
   */
  struct PersistentSession {
      EntityId target = kInvalidEntityId;
      PivotTargetKind kind = PivotTargetKind::Mesh;
      QVec3 original_world_position = {};
      QVec3 preview_world_position = {};
      std::string target_uuid;
      EntityId target_parent = kInvalidEntityId;
      ModelingSelectionMode selection_mode = ModelingSelectionMode::Object;
      std::uint64_t selection_revision = 0;
      QVec3 target_bounds_min = {};
      QVec3 target_bounds_max = {};
      std::uint64_t validated_scene_revision = 0;
      std::uint64_t validated_asset_revision = 0;
  };

  /**
   * Tracks the temporary selection pivot override while hold-D Quick Pivot runs.
   */
  struct QuickState {
      bool override_valid = false;
      bool editing = false;
      std::uint64_t selection_revision = 0;
      QVec3 world_position = {};
  };

  [[nodiscard]] std::optional<EntityId>
  active_selection_target(const EditorSelectionService &selection) const;
  [[nodiscard]] PivotTargetKind target_kind(EntityId target,
                                            const EditorSelectionService &selection) const;
  [[nodiscard]] bool target_is_eligible(EntityId target,
                                        PivotTargetKind kind) const;
  [[nodiscard]] bool persistent_session_is_valid() const;
  [[nodiscard]] std::optional<SceneBounds> target_bounds() const;
  [[nodiscard]] CommandResult align_to_world_position(QVec3 world_position);

  SceneDocument &scene_;
  AssetRegistry &assets_;
  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
  PivotSnapSettings snap_settings_;
  mutable std::optional<PersistentSession> persistent_;
  QuickState quick_;
  std::uint64_t selection_pivot_revision_ = 0;
};

} // namespace quader
