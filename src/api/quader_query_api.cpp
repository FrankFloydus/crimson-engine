////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_query_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_history_api.hpp"
#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "scene/document/project_session.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "scene/scene_bounds.hpp"

#include <optional>
#include <string>
#include <utility>

namespace quader {
namespace {

ProjectSession &project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::project_session(session);
}

QuaderBounds api_bounds(const SceneBounds &bounds) {
  return {
      .min = bounds.min,
      .max = bounds.max,
  };
}

QuaderModelingObjectRef object_ref_for_entity(const ProjectSession &session,
                                              EntityId entity) {
  QuaderModelingObjectRef ref;
  ref.entity = entity;
  ref.entity_uuid = session.scene().uuid(entity);
  ref.editor_object_id = session.scene().editor_object_id(entity);
  if (const ModelingObjectComponent *object =
          session.scene().modeling_object(entity)) {
    ref.modeling_object_id = object->object_id;
  }
  if (const MeshComponent *mesh = session.scene().mesh(entity)) {
    ref.mesh = mesh->mesh;
  }
  if (const MaterialComponent *material = session.scene().material(entity)) {
    ref.material = material->material;
  }
  return ref;
}

QuaderEntitySnapshot entity_snapshot_for(const ProjectSession &session,
                                         EntityId entity) {
  const SceneDocument &scene = session.scene();
  QuaderEntitySnapshot snapshot;
  snapshot.id = entity;
  snapshot.uuid = scene.uuid(entity);
  snapshot.editor_object_id = scene.editor_object_id(entity);
  snapshot.kind = to_api_entity_kind(scene.kind(entity));
  snapshot.parent = scene.parent(entity);
  snapshot.active = scene.primary_selection() == entity;

  if (const NameComponent *name = scene.name(entity)) {
    snapshot.name = name->name;
  }
  if (const TransformComponent *transform = scene.transform(entity)) {
    snapshot.transform = to_api_transform(*transform);
  }
  if (const VisibilityComponent *visibility = scene.visibility(entity)) {
    snapshot.visible = visibility->visible;
    snapshot.selectable = visibility->selectable;
  }
  if (const LockComponent *lock = scene.lock(entity)) {
    snapshot.locked = lock->locked;
  }
  if (const SelectionComponent *selection = scene.selection(entity)) {
    snapshot.selected = selection->selected;
  }
  if (const MeshComponent *mesh = scene.mesh(entity)) {
    snapshot.has_mesh = true;
    snapshot.mesh = mesh->mesh;
  }
  if (const MaterialComponent *material = scene.material(entity)) {
    snapshot.has_material = true;
    snapshot.material = material->material;
  }
  if (const LightComponent *light = scene.light(entity)) {
    snapshot.has_light = true;
    snapshot.light = to_api_light_component(*light);
  }
  if (const SceneSettingsComponent *settings = scene.scene_settings(entity)) {
    snapshot.is_scene_root = scene.is_scene_root(entity);
    snapshot.has_scene_settings = true;
    snapshot.scene_settings = to_api_scene_settings(*settings);
  }
  if (const ModelingObjectComponent *object = scene.modeling_object(entity)) {
    snapshot.has_modeling_object = true;
    snapshot.modeling_object_id = object->object_id;
  }
  return snapshot;
}

QuaderMeshSnapshot mesh_snapshot_for(AssetId id, const MeshAsset &mesh) {
  QuaderMeshSnapshot snapshot;
  snapshot.mesh = id;
  snapshot.revision = mesh.revision.value;
  snapshot.vertex_count = mesh.vertices.size();
  snapshot.index_count = mesh.indices.size();
  snapshot.primitive_count = mesh.primitives.size();
  snapshot.stable_cpu_topology = mesh.has_stable_cpu_topology;
  if (const std::optional<SceneBounds> bounds = local_mesh_bounds(mesh)) {
    snapshot.has_bounds = true;
    snapshot.bounds = api_bounds(*bounds);
  }
  return snapshot;
}

} // namespace

QuaderQueryApi::QuaderQueryApi(QuaderApiSession session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

std::vector<QuaderEntitySnapshot> QuaderQueryApi::entities() const {
    std::vector<QuaderEntitySnapshot> result;
    const std::vector<EntityId> ids =
        project_session(session_).scene().entities();
    result.reserve(ids.size());
    for (const EntityId id : ids) {
      result.push_back(entity_snapshot_for(project_session(session_), id));
    }
    return result;
}

std::vector<QuaderEntitySnapshot> QuaderQueryApi::entity_tree() const {
  std::vector<QuaderEntitySnapshot> result;
  const std::vector<EntityId> ids =
      project_session(session_).scene().entity_tree_order();
  result.reserve(ids.size());
  for (const EntityId id : ids) {
    result.push_back(entity_snapshot_for(project_session(session_), id));
  }
  return result;
}

std::optional<QuaderEntitySnapshot> QuaderQueryApi::entity(EntityId id) const {
  if (!project_session(session_).scene().exists(id)) {
    return std::nullopt;
  }
  return entity_snapshot_for(project_session(session_), id);
}

std::optional<QuaderMeshSnapshot> QuaderQueryApi::mesh(AssetId id) const {
  const MeshAsset *mesh = project_session(session_).assets().mesh(id);
  if (mesh == nullptr) {
    return std::nullopt;
  }
  return mesh_snapshot_for(id, *mesh);
}

std::optional<QuaderMaterialAsset> QuaderQueryApi::material(AssetId id) const {
  const MaterialDocument *material =
      project_session(session_).assets().material(id);
  if (material == nullptr) {
    return std::nullopt;
  }
  return to_api_material_asset(*material);
}

std::optional<QuaderTextureAsset> QuaderQueryApi::texture(AssetId id) const {
  const TextureAsset *texture = project_session(session_).assets().texture(id);
  if (texture == nullptr) {
    return std::nullopt;
  }
  return to_api_texture_asset(*texture);
}

std::vector<QuaderModelingObjectRecord>
QuaderQueryApi::modeling_objects() const {
  const std::vector<ModelingObjectRecord> records =
      project_session(session_).modeling().objects();
  std::vector<QuaderModelingObjectRecord> result;
  result.reserve(records.size());
  for (const ModelingObjectRecord &record : records) {
    result.push_back(to_api_modeling_object_record(record));
  }
  return result;
}

std::optional<QuaderModelingObjectRef>
QuaderQueryApi::modeling_object_for_entity(EntityId entity) const {
  if (!project_session(session_).scene().exists(entity)) {
    return std::nullopt;
  }
  QuaderModelingObjectRef ref =
      object_ref_for_entity(project_session(session_), entity);
  if (ref.modeling_object_id == kInvalidModelingObjectId) {
    return std::nullopt;
  }
  return ref;
}

std::optional<QuaderModelingObjectRef> QuaderQueryApi::modeling_object_for_id(
    QuaderModelingObjectId modeling_object_id) const {
  const std::optional<EntityId> entity =
      project_session(session_).modeling().entity_for_modeling_object(
          modeling_object_id);
  if (!entity.has_value()) {
    return std::nullopt;
  }
  return object_ref_for_entity(project_session(session_), *entity);
}

std::optional<QuaderMeshAsset>
QuaderQueryApi::compiled_object_mesh(QuaderModelingObjectRef object) const {
  if (object.modeling_object_id == kInvalidModelingObjectId) {
    return std::nullopt;
  }
  const std::optional<MeshAsset> mesh =
      project_session(session_).modeling().compile_object_mesh(
          object.modeling_object_id);
  return mesh ? std::optional<QuaderMeshAsset>{to_api_mesh_asset(*mesh)}
              : std::nullopt;
}

std::optional<QVec3> QuaderQueryApi::selection_center() const {
  ProjectSession &session = project_session(session_);
  const QuaderSelectionSummary summary = selection();
  if (summary.active_kind == QuaderActiveSelectionKind::Object ||
      summary.active_kind == QuaderActiveSelectionKind::Light) {
    if (const std::optional<SceneBounds> bounds =
            selected_world_bounds(session.scene(), session.assets())) {
      return bounds_center(*bounds);
    }
  }
  return session.modeling().selection_center();
}

QuaderSelectionSummary QuaderQueryApi::selection() const {
  return to_api_selection_summary(
      project_session(session_).selection().summary());
}

QuaderToolStatus QuaderQueryApi::tool_status() const {
  return to_api_tool_status(project_session(session_).modeling().tool_status());
}

QuaderAssetSnapshot QuaderQueryApi::assets() const {
  return {
      .mesh_count = project_session(session_).assets().mesh_count(),
      .material_count = project_session(session_).assets().material_count(),
      .texture_count = project_session(session_).assets().texture_count(),
      .revision = project_session(session_).assets().revision(),
  };
}

QuaderHistorySnapshot QuaderQueryApi::history() const {
    return QuaderHistoryApi(session_, mutations_).snapshot();
}

QuaderOverlayFrame QuaderQueryApi::selection_overlay() const {
  return to_api_overlay_frame(
      project_session(session_).modeling().selection_overlay_frame());
}

QuaderRevisionStamp QuaderQueryApi::revisions() const {
  return quader_api_current_revisions(project_session(session_));
}

QuaderApiSnapshot QuaderQueryApi::snapshot() const {
  return {
      .revisions = revisions(),
      .entities = entity_tree(),
      .modeling_objects = modeling_objects(),
      .selection = selection(),
      .tool_status = tool_status(),
      .assets = assets(),
      .history = history(),
      .selection_overlay = selection_overlay(),
  };
}

} // namespace quader
