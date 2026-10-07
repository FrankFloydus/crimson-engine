////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_mode_api.hpp"

#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "editor/modes/editor_mode_service.hpp"

namespace quader {
namespace {

EditorMode native_mode(QuaderEditorMode mode) {
  return mode == QuaderEditorMode::Edit ? EditorMode::Edit : EditorMode::Object;
}

} // namespace

QuaderEditorModeApi::QuaderEditorModeApi(
    QuaderApiSession session, QuaderApiMutationRunner &mutations)
    : session_(session), mutations_(mutations) {}

void QuaderEditorModeApi::rebind_session(QuaderApiSession session) {
  session_ = session;
}

QuaderEditorMode QuaderEditorModeApi::current() const {
  return QuaderApiSessionInternalAccess::read_scene_session(session_)
                     .editor_mode().mode() == EditorMode::Edit
             ? QuaderEditorMode::Edit : QuaderEditorMode::Object;
}

std::uint64_t QuaderEditorModeApi::revision() const {
  return QuaderApiSessionInternalAccess::read_scene_session(session_)
      .editor_mode().revision();
}

std::uint64_t QuaderEditorModeApi::generation() const {
  return QuaderApiSessionInternalAccess::read_scene_session(session_)
      .editor_mode().generation();
}

QuaderEditorModeState QuaderEditorModeApi::state() const {
  const SceneSession &scene =
      QuaderApiSessionInternalAccess::read_scene_session(session_);
  const EditorModeSnapshot &snapshot = scene.editor_mode().snapshot();
  const auto entity_ref = [&](EntityId entity) {
    return QuaderEntityRef{
        .scene_session_id = mutations_.scene_session_id(),
        .id = entity,
        .uuid = scene.scene().uuid(entity),
        .editor_object_id = scene.scene().editor_object_id(entity),
    };
  };
  QuaderEditorModeState result{
      .mode = snapshot.mode == EditorMode::Edit ? QuaderEditorMode::Edit
                                               : QuaderEditorMode::Object,
      .revision = snapshot.revision,
      .generation = snapshot.generation,
      .component_kind = to_api_selection_mode(snapshot.component_kind),
      .active_entity = entity_ref(snapshot.active_entity),
  };
  result.participants.reserve(snapshot.participants.size());
  for (const EditorModeParticipant &participant : snapshot.participants) {
    result.participants.push_back(entity_ref(participant.entity));
  }
  result.object_results.reserve(snapshot.object_results.size());
  for (const EntityId entity : snapshot.object_results) {
    result.object_results.push_back(entity_ref(entity));
  }
  return result;
}

bool QuaderEditorModeApi::can_set(QuaderEditorMode mode) const {
  return (mode == QuaderEditorMode::Object || mode == QuaderEditorMode::Edit) &&
         QuaderApiSessionInternalAccess::active_scene_session(session_) != nullptr &&
         QuaderApiSessionInternalAccess::read_scene_session(session_)
             .editor_mode().can_set_mode(native_mode(mode));
}

QuaderApiVoidResult QuaderEditorModeApi::set(QuaderEditorMode mode) {
  SceneSession &scene = QuaderApiSessionInternalAccess::scene_session(session_);
  if (!mutations_.available()) {
    return quader_api_failure_status(scene, "No scene is active.");
  }
  if (mode != QuaderEditorMode::Object && mode != QuaderEditorMode::Edit) {
    return quader_api_failure_status(scene, "Unknown editor mode.");
  }
  const CommandResult result = scene.editor_mode().set_mode(native_mode(mode));
  return mutations_.run_runtime_result(result,
                                      result.changed ? "Set Editor Mode" : "");
}

QuaderApiVoidResult QuaderEditorModeApi::toggle_edit() {
  return set(current() == QuaderEditorMode::Edit ? QuaderEditorMode::Object
                                                : QuaderEditorMode::Edit);
}

} // namespace quader
