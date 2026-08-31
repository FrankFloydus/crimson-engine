////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_layers_api.hpp"

#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_status_internal.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/scene_session.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

SceneSession &scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::scene_session(session);
}

const SceneSession &read_scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::read_scene_session(session);
}

QuaderLayerSnapshot layer_snapshot(const SceneDocument &scene,
                                   const SceneLayerRecord &record) {
  return {
      .id = record.id,
      .name = record.name,
      .visible = record.visible,
      .wireframe_color = record.wireframe_color,
      .locked = record.locked,
      .members = scene.layer_members(record.id),
  };
}

QuaderApiVoidResult inactive_scene_status(QuaderApiSession session) {
  return quader_api_failure_status(read_scene_session(session),
                                   "No scene is active.");
}

} // namespace

QuaderLayersApi::QuaderLayersApi(QuaderApiSession session,
                                 QuaderApiMutationRunner &mutations)
    : session_(std::move(session)), mutations_(mutations) {}

std::vector<QuaderLayerSnapshot> QuaderLayersApi::all() const {
  const SceneDocument &scene = read_scene_session(session_).scene();
  SceneLayerState state = scene.layer_state();
  std::vector<QuaderLayerSnapshot> snapshots;
  snapshots.reserve(state.layers.size());
  std::unordered_map<SceneLayerId, std::size_t> snapshot_by_layer;
  snapshot_by_layer.reserve(state.layers.size());
  for (const SceneLayerRecord &record : state.layers) {
    snapshot_by_layer.emplace(record.id, snapshots.size());
    snapshots.push_back({
        .id = record.id,
        .name = record.name,
        .visible = record.visible,
        .wireframe_color = record.wireframe_color,
        .locked = record.locked,
    });
  }
  for (const SceneLayerAssignment &assignment : state.assignments) {
    const auto snapshot = snapshot_by_layer.find(assignment.layer_id);
    if (snapshot != snapshot_by_layer.end()) {
      snapshots[snapshot->second].members.push_back(assignment.entity);
    }
  }
  return snapshots;
}

std::optional<QuaderLayerSnapshot>
QuaderLayersApi::layer(QuaderLayerId id) const {
  const SceneDocument &scene = read_scene_session(session_).scene();
  const SceneLayerRecord *record = scene.layer(id);
  return record == nullptr
             ? std::nullopt
             : std::optional<QuaderLayerSnapshot>{
                   layer_snapshot(scene, *record)};
}

std::uint64_t QuaderLayersApi::revision() const {
  return read_scene_session(session_).scene().layer_revision();
}

bool QuaderLayersApi::can_create_from_selection() const {
  const SceneDocument &scene = read_scene_session(session_).scene();
  return !scene.layer_eligible_entities(scene.selected_entities()).empty();
}

bool QuaderLayersApi::can_add_selection_to(QuaderLayerId id) const {
  const SceneDocument &scene = read_scene_session(session_).scene();
  if (scene.layer(id) == nullptr) {
    return false;
  }
  const std::vector<EntityId> eligible =
      scene.layer_eligible_entities(scene.selected_entities());
  return std::any_of(eligible.begin(), eligible.end(),
                     [&](EntityId entity) {
                       return scene.entity_layer(entity) != id;
                     });
}

bool QuaderLayersApi::can_remove_selection(QuaderLayerId id) const {
  const SceneDocument &scene = read_scene_session(session_).scene();
  if (scene.layer(id) == nullptr) {
    return false;
  }
  const std::vector<EntityId> eligible =
      scene.layer_eligible_entities(scene.selected_entities());
  return std::any_of(eligible.begin(), eligible.end(),
                     [&](EntityId entity) {
                       return scene.entity_layer(entity) == id;
                     });
}

bool QuaderLayersApi::can_remove_selection_from_all_layers() const {
  const SceneDocument &scene = read_scene_session(session_).scene();
  const std::vector<EntityId> eligible =
      scene.layer_eligible_entities(scene.selected_entities());
  return std::any_of(eligible.begin(), eligible.end(),
                     [&](EntityId entity) {
                       return scene.entity_layer(entity) !=
                              kInvalidSceneLayerId;
                     });
}

QuaderApiResult<QuaderLayerSnapshot>
QuaderLayersApi::create(std::string name, QVec3 wireframe_color) {
  if (!mutations_.available()) {
    return quader_api_result_from_status<QuaderLayerSnapshot>(
        inactive_scene_status(session_));
  }
  SceneSession &session = scene_session(session_);
  DocumentCreateLayerResult command = session.document_commands().create_layer(
      std::move(name), wireframe_color, false);
  const SceneLayerId created = command.layer_id;
  const QuaderApiVoidResult status =
      mutations_.run_document_result(std::move(command), "Create Layer");
  QuaderApiResult<QuaderLayerSnapshot> result =
      quader_api_result_from_status<QuaderLayerSnapshot>(status);
  if (result.ok && result.changed) {
    const SceneLayerRecord *record = session.scene().layer(created);
    if (record != nullptr) {
      result.value = layer_snapshot(session.scene(), *record);
    }
  }
  return result;
}

QuaderApiResult<QuaderLayerSnapshot> QuaderLayersApi::create_from_selection(
    std::string name, QVec3 wireframe_color) {
  if (!mutations_.available()) {
    return quader_api_result_from_status<QuaderLayerSnapshot>(
        inactive_scene_status(session_));
  }
  SceneSession &session = scene_session(session_);
  DocumentCreateLayerResult command = session.document_commands().create_layer(
      std::move(name), wireframe_color, true);
  const SceneLayerId created = command.layer_id;
  const QuaderApiVoidResult status = mutations_.run_document_result(
      std::move(command), "Create Layer From Selection");
  QuaderApiResult<QuaderLayerSnapshot> result =
      quader_api_result_from_status<QuaderLayerSnapshot>(status);
  if (result.ok && result.changed) {
    const SceneLayerRecord *record = session.scene().layer(created);
    if (record != nullptr) {
      result.value = layer_snapshot(session.scene(), *record);
    }
  }
  return result;
}

QuaderApiVoidResult QuaderLayersApi::rename(QuaderLayerId id,
                                            std::string name) {
  if (!mutations_.available()) {
    return inactive_scene_status(session_);
  }
  return mutations_.run_document_result(
      scene_session(session_).document_commands().rename_layer(
          id, std::move(name)),
      "Rename Layer");
}

QuaderApiVoidResult QuaderLayersApi::set_visible(QuaderLayerId id,
                                                 bool visible) {
  if (!mutations_.available()) {
    return inactive_scene_status(session_);
  }
  return mutations_.run_document_result(
      scene_session(session_).document_commands().set_layer_visible(id,
                                                                     visible),
      "Set Layer Visibility");
}

QuaderApiVoidResult
QuaderLayersApi::set_wireframe_color(QuaderLayerId id, QVec3 color) {
  if (!mutations_.available()) {
    return inactive_scene_status(session_);
  }
  return mutations_.run_document_result(
      scene_session(session_)
          .document_commands()
          .set_layer_wireframe_color(id, color),
      "Set Layer Color");
}

QuaderApiVoidResult QuaderLayersApi::set_locked(QuaderLayerId id,
                                                bool locked) {
  if (!mutations_.available()) {
    return inactive_scene_status(session_);
  }
  SceneSession &session = scene_session(session_);
  return mutations_.run_document_result(
      session.document_commands().set_layer_locked(id, locked),
      "Set Layer Lock");
}

QuaderApiVoidResult QuaderLayersApi::add_selection(QuaderLayerId id) {
  if (!mutations_.available()) {
    return inactive_scene_status(session_);
  }
  SceneSession &session = scene_session(session_);
  return mutations_.run_document_result(
      session.document_commands().add_selection_to_layer(id),
      "Add Selection to Layer");
}

QuaderApiVoidResult QuaderLayersApi::remove_selection(QuaderLayerId id) {
  if (!mutations_.available()) {
    return inactive_scene_status(session_);
  }
  return mutations_.run_document_result(
      scene_session(session_)
          .document_commands()
          .remove_selection_from_layer(id),
      "Remove Selection From Layer");
}

QuaderApiVoidResult QuaderLayersApi::remove_selection_from_all_layers() {
  if (!mutations_.available()) {
    return inactive_scene_status(session_);
  }
  return mutations_.run_document_result(
      scene_session(session_)
          .document_commands()
          .remove_selection_from_all_layers(),
      "Remove Selection From Layers");
}

QuaderApiVoidResult QuaderLayersApi::empty(QuaderLayerId id) {
  if (!mutations_.available()) {
    return inactive_scene_status(session_);
  }
  return mutations_.run_document_result(
      scene_session(session_).document_commands().empty_layer(id),
      "Empty Layer");
}

QuaderApiVoidResult QuaderLayersApi::destroy(QuaderLayerId id) {
  return destroy(std::span<const QuaderLayerId>(&id, 1u));
}

QuaderApiVoidResult
QuaderLayersApi::destroy(std::span<const QuaderLayerId> ids) {
  if (!mutations_.available()) {
    return inactive_scene_status(session_);
  }
  return mutations_.run_document_result(
      scene_session(session_).document_commands().delete_layers(
          std::vector<SceneLayerId>(ids.begin(), ids.end())),
      ids.size() == 1u ? "Delete Layer" : "Delete Layers");
}

} // namespace quader
