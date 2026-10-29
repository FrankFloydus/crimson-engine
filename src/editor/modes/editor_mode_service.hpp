////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "editor/modes/editor_mode_types.hpp"
#include "editor/modes/editor_mode_policy.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace quader {

class SceneSession;

/** Owns transient mode membership; component selections remain in SDK documents. */
class EditorModeService final {
public:
  explicit EditorModeService(SceneSession &session);

  [[nodiscard]] const EditorModeSnapshot &snapshot() const noexcept {
    return snapshot_;
  }
  [[nodiscard]] EditorMode mode() const noexcept { return snapshot_.mode; }
  [[nodiscard]] EditorWorkspaceMode workspace_mode()const noexcept{return workspace_provider_?workspace_provider_():workspace_mode_;}
  [[nodiscard]] bool permits_tool(std::string_view id)const noexcept{return workspace_tool_allowed(workspace_mode(),id);}
  [[nodiscard]] bool permits_action(std::string_view id)const noexcept{return workspace_action_allowed(workspace_mode(),id);}
  CommandResult set_workspace_mode(EditorWorkspaceMode);
  CommandResult prepare_workspace_mode(EditorWorkspaceMode);
  void workspace_mode_changed();
  void bind_workspace_mode(std::function<EditorWorkspaceMode()> provider){workspace_provider_=std::move(provider);}

  [[nodiscard]] std::uint64_t revision() const noexcept {
    return snapshot_.revision;
  }
  [[nodiscard]] std::uint64_t generation() const noexcept {
    return snapshot_.generation;
  }
  [[nodiscard]] bool contains(EntityId entity) const noexcept {
    return snapshot_.mode == EditorMode::Edit && snapshot_.contains(entity);
  }
  [[nodiscard]] bool
  permits_selection_mode(ModelingSelectionMode kind) const noexcept;
  [[nodiscard]] bool can_enter_edit() const;
  [[nodiscard]] bool can_set_mode(EditorMode mode) const;
  [[nodiscard]] CommandResult set_mode(EditorMode mode);
  /// Direct component choices also work before any mesh is selected.
  [[nodiscard]] CommandResult enter_component_selection(ModelingSelectionMode kind);
  [[nodiscard]] CommandResult toggle();
  [[nodiscard]] CommandResult
  set_component_kind(ModelingSelectionMode kind);
  /// Optional retained UI drafts settle after request validation, before native tools.
  void set_transition_settlement(std::function<CommandResult()> settlement);

  [[nodiscard]] EditorModeSnapshot capture_history_state() const {
    return snapshot_;
  }
  /** State-only restore: the publication owner projects the resulting context. */
  void restore_history_state(const EditorModeSnapshot &state,
                             bool exact_failure_compensation = false);
  /** Failure compensation moves an already allocated checkpoint without callbacks. */
  void restore_exact_state(EditorModeSnapshot &&state) noexcept;
  /** Structural publication remaps only existing participants to its survivor. */
  void remap_participants(std::span<const EntityId> consumed, EntityId survivor);
  /** Structural outputs remain outside Edit and join object selection on exit. */
  void register_object_result(EntityId entity);
  void set_active_participant(EntityId entity);
  /// Selection publication supplies its committed SDK/scene targets, never hover.
  void synchronize_component_participants(std::span<const EntityId> entities,
                                          EntityId active);
  /// Successful Object selection publication updates active identity; clear retains it.
  void synchronize_object_active_entity();
  /// Successful component publication updates this metadata without re-entering modeling.
  void record_component_kind(ModelingSelectionMode kind);
  /** Removes dead/replaced identities; never enrolls newly discovered entities. */
  void reconcile_scene_lifecycle();
  /// Retires invalid tool contexts after committed publication, never during previews.
  void synchronize_tools_after_publication();
  /** Document replacement starts a fresh Object context and first-admission set. */
  void reset_for_document_replacement();

private:
  [[nodiscard]] CommandResult transition_mode(EditorMode mode, bool allow_empty);
  [[nodiscard]] std::vector<EditorModeParticipant>
  entry_participants(bool use_active_fallback = true) const;
  [[nodiscard]] EditorModeParticipant participant_for(EntityId entity) const;
  void assign_active_entity(EntityId entity);
  void normalize_tools();
  void advance_revision(bool new_generation = false) noexcept;

  EditorWorkspaceMode workspace_mode_=EditorWorkspaceMode::Modeling;
  std::function<EditorWorkspaceMode()> workspace_provider_;
  SceneSession &session_;
  EditorModeSnapshot snapshot_;
  std::unordered_set<std::string> admitted_uuids_;
  std::function<CommandResult()> transition_settlement_;
};

} // namespace quader
