////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_query_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_history_api.hpp"
#include "api/quader_layers_api.hpp"
#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_transform_pivot.hpp"

#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

const SceneSession &read_scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::read_scene_session(session);
}

/// Caches shared hierarchy facts while projecting a query result set.
struct EntityQueryContext final {
  EntityId active_selection = kInvalidEntityId;
  std::unordered_map<EntityId, EntityId> containing_definition;
};

EntityQueryContext build_entity_query_context(
    const SceneDocument &scene, std::span<const EntityId> tree_order) {
  EntityQueryContext context;
  context.active_selection = scene.primary_selection();
  context.containing_definition.reserve(tree_order.size());
  for (const EntityId entity : tree_order) {
    const EntityId parent = scene.parent(entity);
    EntityId definition =
        parent == kInvalidEntityId
            ? kInvalidEntityId
            : context.containing_definition[parent];
    if (scene.is_instance_definition_entity(entity)) {
      definition = entity;
    }
    context.containing_definition.emplace(entity, definition);
  }
  return context;
}

bool entity_visible_in_scene_query(
    const SceneDocument &scene, EntityId entity,
    EntityId active_definition, const EntityQueryContext &context) {
  if (scene.is_instance_definition_entity(entity)) {
    return false;
  }
  const auto containing = context.containing_definition.find(entity);
  const EntityId definition =
      containing == context.containing_definition.end()
          ? kInvalidEntityId
          : containing->second;
  if (definition == kInvalidEntityId) {
    return true;
  }
  return active_definition != kInvalidEntityId &&
         definition == active_definition;
}

QuaderBounds api_bounds(const SceneBounds &bounds) {
  return {
      .min = bounds.min,
      .max = bounds.max,
  };
}

QuaderModelingObjectRef object_ref_for_entity(
    const SceneSession &session, SceneSessionId scene_session_id,
    EntityId entity) {
  QuaderModelingObjectRef ref;
  ref.scene_session_id = scene_session_id;
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

QuaderEntitySnapshot entity_snapshot_for(const SceneSession &session,
                                         EntityId entity,
                                         EntityId active_selection) {
  const SceneDocument &scene = session.scene();
  QuaderEntitySnapshot snapshot;
  snapshot.id = entity;
  snapshot.uuid = scene.uuid(entity);
  snapshot.editor_object_id = scene.editor_object_id(entity);
  snapshot.kind = to_api_entity_kind(scene.kind(entity));
  snapshot.parent = scene.parent(entity);
  snapshot.active = active_selection == entity;
  snapshot.layer_id = scene.entity_layer(entity);

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
  if (const CurveComponent *curve = scene.curve(entity)) {
    snapshot.has_curve = true;
    snapshot.curve = curve->curve;
  }
  if (const TextureMappingComponent *mapping = scene.texture_mapping(entity)) {
    snapshot.texture_sampling_from_local = mapping->sampling_from_local;
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
  snapshot.is_instance_definition =
      scene.is_instance_definition_entity(entity);
  if (const InstanceOccurrenceComponent *occurrence =
          scene.instance_occurrence(entity)) {
    snapshot.has_instance_occurrence = true;
    snapshot.instance_definition = occurrence->definition;
  }
  return snapshot;
}

QuaderMeshSnapshot mesh_snapshot_for(AssetId id, const MeshAsset &mesh) {
  QuaderMeshSnapshot snapshot;
  const MeshGeometryPayload &geometry = mesh.geometry();
  snapshot.mesh = id;
  snapshot.revision = mesh.revision.value;
  snapshot.vertex_count = geometry.vertices.size();
  snapshot.index_count = geometry.indices.size();
  snapshot.primitive_count = geometry.primitives.size();
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
  const SceneSession &session = read_scene_session(session_);
  std::vector<QuaderEntitySnapshot> result;
  const SceneInstanceEditState edit_state =
      session.instances().instance_edit_state();
  const EntityId active_definition =
      edit_state.active ? edit_state.definition : kInvalidEntityId;
  const std::vector<EntityId> tree_order =
      session.scene().entity_tree_order();
  const EntityQueryContext context =
      build_entity_query_context(session.scene(), tree_order);
  const std::vector<EntityId> ids = session.scene().entities();
  result.reserve(ids.size());
  for (const EntityId id : ids) {
    if (!entity_visible_in_scene_query(session.scene(), id,
                                       active_definition, context)) {
      continue;
    }
    result.push_back(
        entity_snapshot_for(session, id, context.active_selection));
  }
  return result;
}

std::vector<QuaderEntitySnapshot> QuaderQueryApi::entity_tree() const {
  const SceneSession &session = read_scene_session(session_);
  std::vector<QuaderEntitySnapshot> result;
  const SceneInstanceEditState edit_state =
      session.instances().instance_edit_state();
  const EntityId active_definition =
      edit_state.active ? edit_state.definition : kInvalidEntityId;
  const std::vector<EntityId> ids = session.scene().entity_tree_order();
  const EntityQueryContext context =
      build_entity_query_context(session.scene(), ids);
  result.reserve(ids.size());
  for (const EntityId id : ids) {
    if (!entity_visible_in_scene_query(session.scene(), id,
                                       active_definition, context)) {
      continue;
    }
    result.push_back(
        entity_snapshot_for(session, id, context.active_selection));
  }
  return result;
}

std::optional<QuaderEntitySnapshot> QuaderQueryApi::entity(EntityId id) const {
  const SceneSession &session = read_scene_session(session_);
  if (!session.scene().exists(id)) {
    return std::nullopt;
  }
  return entity_snapshot_for(session, id,
                             session.scene().primary_selection());
}

std::optional<QuaderMeshSnapshot> QuaderQueryApi::mesh(AssetId id) const {
  const MeshAsset *mesh = read_scene_session(session_).assets().mesh(id);
  if (mesh == nullptr) {
    return std::nullopt;
  }
  return mesh_snapshot_for(id, *mesh);
}

std::optional<QuaderMaterialAsset> QuaderQueryApi::material(AssetId id) const {
  const MaterialDocument *material =
      read_scene_session(session_).assets().material(id);
  if (material == nullptr) {
    return std::nullopt;
  }
  return to_api_material_asset(*material);
}

std::optional<QuaderTextureAsset> QuaderQueryApi::texture(AssetId id) const {
  const TextureAsset *texture =
      read_scene_session(session_).assets().texture(id);
  if (texture == nullptr) {
    return std::nullopt;
  }
  return to_api_texture_asset(*texture);
}

std::vector<QuaderModelingObjectRecord>
QuaderQueryApi::modeling_objects() const {
  const std::vector<ModelingObjectRecord> records =
      read_scene_session(session_).modeling().objects();
  std::vector<QuaderModelingObjectRecord> result;
  result.reserve(records.size());
  for (const ModelingObjectRecord &record : records) {
    result.push_back(to_api_modeling_object_record(record));
  }
  return result;
}

std::optional<QuaderModelingObjectRef>
QuaderQueryApi::modeling_object_for_entity(EntityId entity) const {
  const SceneSession &session = read_scene_session(session_);
  if (!session.scene().exists(entity)) {
    return std::nullopt;
  }
  QuaderModelingObjectRef ref = object_ref_for_entity(
      session, mutations_.scene_session_id(), entity);
  if (ref.modeling_object_id == kInvalidModelingObjectId) {
    return std::nullopt;
  }
  return ref;
}

std::optional<QuaderModelingObjectRef> QuaderQueryApi::modeling_object_for_id(
    QuaderModelingObjectId modeling_object_id) const {
  const std::optional<EntityId> entity =
      read_scene_session(session_).modeling().entity_for_modeling_object(
          modeling_object_id);
  if (!entity.has_value()) {
    return std::nullopt;
  }
  return object_ref_for_entity(read_scene_session(session_),
                               mutations_.scene_session_id(), *entity);
}

std::optional<QuaderMeshAsset>
QuaderQueryApi::compiled_object_mesh(QuaderModelingObjectRef object) const {
  if (!mutations_.belongs_to_active_scene(object.scene_session_id) ||
      object.modeling_object_id == kInvalidModelingObjectId) {
    return std::nullopt;
  }
  const std::optional<MeshAsset> mesh =
      read_scene_session(session_).modeling().compile_object_mesh(
          object.modeling_object_id);
  return mesh ? std::optional<QuaderMeshAsset>{to_api_mesh_asset(*mesh)}
              : std::nullopt;
}

std::optional<double>
QuaderQueryApi::mesh_surface_distance(const QuaderRay &ray) const {
  return read_scene_session(session_).modeling().pick_mesh_surface_distance(
      to_modeling_ray(ray));
}

QuaderMeshSnapQueryResult QuaderQueryApi::mesh_snap_query(
    const QuaderRay &ray, double vertex_radius,
    bool exclude_current_selection) const {
  const ModelingMeshSnapQueryResult query =
      read_scene_session(session_).modeling().pick_mesh_snap_query(
          to_modeling_ray(ray), vertex_radius, exclude_current_selection);
  QuaderMeshSnapQueryResult result{
      .nearest_surface_ray_distance = query.nearest_surface_ray_distance,
  };
  if (query.vertex_target.has_value()) {
    result.vertex_target = QuaderMeshVertexSnapTarget{
        .modeling_object_id = query.vertex_target->modeling_object_id,
        .vertex_id =
            static_cast<QuaderElementId>(query.vertex_target->vertex_id),
        .world_position = query.vertex_target->world_position,
        .ray_distance = query.vertex_target->ray_distance,
        .pick_distance = query.vertex_target->pick_distance,
    };
  }
  return result;
}

std::optional<QVec3> QuaderQueryApi::selection_center() const {
  const SceneSession &session = read_scene_session(session_);
  if (session.instances().instance_edit_state().active) {
    return session.modeling().selection_center();
  }
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

std::optional<QVec3> QuaderQueryApi::selection_transform_pivot() const {
  const SceneSession &session = read_scene_session(session_);
  const SelectionPivotOverride pivot =
      session.pivot().selection_pivot_override();
  if (pivot.valid) {
    return pivot.world_position;
  }
  if (session.instances().instance_edit_state().active) {
    return session.modeling().selection_center();
  }
  const ModelingSelectionMode mode = session.selection().mode();
  if (mode == ModelingSelectionMode::Object ||
      mode == ModelingSelectionMode::Group) {
    if (const std::optional<QVec3> committed_pivot =
            resolve_scene_selection_visible_pivot(session.scene(),
                                                  session.assets())) {
      return committed_pivot;
    }
  }
  return session.modeling().selection_center();
}

std::optional<QuaderTransform3>
QuaderQueryApi::selection_transform_orientation() const {
  const std::optional<ModelingTransform3> orientation =
      read_scene_session(session_).modeling().selection_transform_orientation();
  return orientation.has_value()
             ? std::optional<QuaderTransform3>{
                   to_api_transform3(*orientation)}
             : std::nullopt;
}

QuaderSelectionSummary QuaderQueryApi::selection() const {
  return to_api_selection_summary(
      read_scene_session(session_).selection().summary());
}

QuaderToolStatus QuaderQueryApi::tool_status() const {
  const SceneSession &session = read_scene_session(session_);
  QuaderToolStatus status = to_api_tool_status(session.modeling().tool_status());
  if (session.primitive_creation().active()) {
    status.active_tool = QuaderViewportTool::CreatePrimitive;
    status.modal_tool_owns_viewport = true;
  }
  if (session.curve().active()) {
    status.active_tool = QuaderViewportTool::Curve;
    status.modal_tool_owns_viewport = true;
  }
  return status;
}

QuaderAssetSnapshot QuaderQueryApi::assets() const {
  const AssetRegistry &assets = read_scene_session(session_).assets();
  return {
      .mesh_count = assets.mesh_count(),
      .material_count = assets.material_count(),
      .texture_count = assets.texture_count(),
      .revision = assets.revision(),
      .curve_count = assets.curve_count(),
  };
}

QuaderHistorySnapshot QuaderQueryApi::history() const {
    return QuaderHistoryApi(session_, mutations_).snapshot();
}

QuaderOverlayFrame QuaderQueryApi::selection_overlay() const {
  return to_api_overlay_frame(
      read_scene_session(session_).modeling().selection_overlay_frame());
}

QuaderRevisionStamp QuaderQueryApi::revisions() const {
  return quader_api_current_revisions(read_scene_session(session_));
}

QuaderApiSnapshot QuaderQueryApi::snapshot() const {
  return {
      .revisions = revisions(),
      .entities = entity_tree(),
      .layers = QuaderLayersApi(session_, mutations_).all(),
      .modeling_objects = modeling_objects(),
      .selection = selection(),
      .tool_status = tool_status(),
      .assets = assets(),
      .history = history(),
      .selection_overlay = selection_overlay(),
  };
}

} // namespace quader
