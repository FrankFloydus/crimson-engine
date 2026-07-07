////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "core/types.hpp"
#include "editor/pivot/pivot_types.hpp"

#include <cstdint>
#include <optional>
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

  [[nodiscard]] bool persistent_active() const { return persistent_.has_value(); }
  [[nodiscard]] PivotSnapSettings snap_settings() const { return snap_settings_; }
  [[nodiscard]] PivotHandleState handle_state() const;
  [[nodiscard]] SelectionPivotOverride selection_pivot_override() const;
  [[nodiscard]] std::optional<QVec3> persistent_preview_position() const;

  CommandResult activate_persistent(EditorSelectionService &selection);
  CommandResult cancel_persistent();
  CommandResult apply_persistent();
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
      std::uint64_t selection_revision = 0;
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
  [[nodiscard]] std::optional<SceneBounds> target_bounds() const;
  [[nodiscard]] CommandResult align_to_world_position(QVec3 world_position);

  SceneDocument &scene_;
  AssetRegistry &assets_;
  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
  PivotSnapSettings snap_settings_;
  std::optional<PersistentSession> persistent_;
  QuickState quick_;
};

} // namespace quader
