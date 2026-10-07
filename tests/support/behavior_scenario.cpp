////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "behavior_scenario.hpp"

#include "behavior_expect.hpp"
#include "viewport_overlay_test_helpers.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "assets/asset_registry.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "ui/viewport_shortcut_controller.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_router.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace quader::test {
namespace {

QuaderApiStatus failed_status(std::string message) {
  return {
      .ok = false,
      .changed = false,
      .message = std::move(message),
      .mutation_kind = QuaderMutationKind::None,
  };
}

BehaviorBounds to_behavior_bounds(const SceneBounds &bounds) {
  return {
      .min = bounds.min,
      .max = bounds.max,
  };
}

void add_overlay_stats(BehaviorSnapshot &snapshot,
                       const ViewportOverlayFrame &frame) {
  snapshot.overlay.line_count = frame.lines.size();
  snapshot.overlay.triangle_count = frame.triangles.size();
  snapshot.overlay.point_count = frame.points.size();
  for (const ViewportOverlayLine &line : frame.lines) {
    ++snapshot.overlay.lines_by_layer[line.layer];
  }
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    ++snapshot.overlay.triangles_by_layer[triangle.layer];
  }
  for (const ViewportOverlayPoint &point : frame.points) {
    ++snapshot.overlay.points_by_layer[point.layer];
  }
}

QuaderPickSelectRequest api_pick_at(QVec3 origin, PickOptions options) {
  const ModelingPickSelectRequest request = pick_at(origin, options);
  return {
      .ray = to_api_ray(request.ray),
      .click_count = request.click_count,
      .control_pressed = request.control_pressed,
      .shift_pressed = request.shift_pressed,
      .command_pressed = request.command_pressed,
      .alt_pressed = request.alt_pressed,
      .preserve_selection_on_miss = request.preserve_selection_on_miss,
      .preserve_selection_on_selected_hit =
          request.preserve_selection_on_selected_hit,
      .vertex_radius = request.vertex_radius,
      .edge_radius = request.edge_radius,
  };
}

QuaderHoverRequest api_hover_at(QVec3 origin, HoverOptions options) {
  const ModelingHoverRequest request = hover_at(origin, options);
  return {
      .ray = to_api_ray(request.ray),
      .selected_modifier_pressed = request.selected_modifier_pressed,
      .vertex_radius = request.vertex_radius,
      .edge_radius = request.edge_radius,
  };
}

QuaderTransformDragBeginRequest
api_transform_drag_begin(DragBeginOptions options) {
  const ModelingTransformDragBeginRequest request =
      transform_drag_begin(options);
  return {
      .tool = to_api_transform_tool(request.tool),
      .operation = to_api_transform_operation(request.operation),
      .axis = to_api_transform_axis(request.axis),
      .pivot = request.pivot,
      .screen_position = request.screen_position,
      .screen_pivot = request.screen_pivot,
      .screen_tip = request.screen_tip,
      .has_world_direction = request.has_world_direction,
      .world_direction = request.world_direction,
      .ray = to_api_ray(request.ray),
      .camera = to_api_viewport_camera(request.camera),
      .grid_size = request.grid_size,
      .vertex_snap_radius = request.vertex_snap_radius,
      .grid_snap_enabled = request.grid_snap_enabled,
      .precision = request.precision,
      .duplicate_selected = request.duplicate_selected,
  };
}

QuaderTransformDragUpdateRequest
api_transform_drag_update(QVec2 screen_position) {
  const ModelingTransformDragUpdateRequest request =
      transform_drag_update(screen_position);
  return {
      .screen_position = request.screen_position,
      .ray = to_api_ray(request.ray),
      .camera = to_api_viewport_camera(request.camera),
      .grid_size = request.grid_size,
      .vertex_snap_radius = request.vertex_snap_radius,
      .grid_snap_enabled = request.grid_snap_enabled,
      .precision = request.precision,
  };
}

} // namespace

BehaviorScenario::BehaviorScenario()
    : session_(),
      api_session_(QuaderApiSessionInternalAccess::from_scene_session(session_)),
      editor_(api_session_) {
  last_status_.ok = true;
}

BehaviorScenario BehaviorScenario::new_project() { return BehaviorScenario(); }

BehaviorGiven BehaviorScenario::given() {
    return BehaviorGiven(*this);
}

BehaviorWhen BehaviorScenario::when() {
    return BehaviorWhen(*this);
}

BehaviorThen BehaviorScenario::then() {
    return BehaviorThen(*this);
}

EntityId BehaviorScenario::entity(std::string_view alias) const {
    return object(alias).entity;
}

QuaderModelingObjectRef BehaviorScenario::object(std::string_view alias) const {
    const auto found = aliases_.find(std::string(alias));
    if (found == aliases_.end()) {
        ADD_FAILURE() << "Unknown behavior alias: " << alias;
        return {};
    }
    return found->second;
}

ModelingObjectId
BehaviorScenario::modeling_object(std::string_view alias) const {
  return object(alias).modeling_object_id;
}

BehaviorSnapshot BehaviorScenario::snapshot(std::string label) const {
    BehaviorSnapshot result;
    result.label = std::move(label);
    result.selection = session_.modeling().selection_summary();
    result.tool_status = session_.modeling().tool_status();
    add_overlay_stats(result, selection_overlay_frame());

    for (const auto& [alias, object] : aliases_) {
        EntitySnapshot entity;
        entity.id = object.entity;
        entity.alias = alias;
        entity.mesh = object.mesh;
        entity.modeling_object_id = object.modeling_object_id;
        if (const NameComponent* name = session_.scene().name(object.entity)) {
            entity.name = name->name;
        }
        if (const TransformComponent* transform = session_.scene().transform(object.entity)) {
            entity.transform = *transform;
        }
        if (const SelectionComponent* selection = session_.scene().selection(object.entity)) {
            entity.selected = selection->selected;
        }
        if (const MeshComponent* mesh = session_.scene().mesh(object.entity)) {
            entity.mesh = mesh->mesh;
        }
        result.entities.push_back(entity);

        if (const MeshAsset* mesh = session_.assets().mesh(entity.mesh)) {
          MeshSnapshot mesh_snapshot;
          mesh_snapshot.mesh = entity.mesh;
          mesh_snapshot.vertex_count = mesh->geometry().vertices.size();
          mesh_snapshot.index_count = mesh->geometry().indices.size();
          mesh_snapshot.positions.reserve(mesh->geometry().vertices.size());
          for (const MeshVertex &vertex : mesh->geometry().vertices) {
            mesh_snapshot.positions.push_back(vertex.position);
          }
          if (const std::optional<SceneBounds> bounds =
                  local_mesh_bounds(*mesh)) {
            mesh_snapshot.bounds = to_behavior_bounds(*bounds);
          }
          result.meshes.push_back(mesh_snapshot);
        }
    }

    if (const std::optional<SceneBounds> bounds =
            selected_world_bounds(session_.scene(), session_.assets())) {
      result.selected_bounds = to_behavior_bounds(*bounds);
    }

    return result;
}

std::size_t BehaviorScenario::modeling_object_count() const {
  return session_.modeling().objects().size();
}

void BehaviorScenario::remember(std::string_view alias, QuaderModelingObjectRef object) {
    aliases_.insert_or_assign(std::string(alias), object);
}

void BehaviorScenario::remember(std::string_view alias, QuaderEntityRef entity) {
    remember(alias, entity.id);
}

void BehaviorScenario::remember(std::string_view alias, EntityId entity) {
  aliases_.insert_or_assign(std::string(alias), object_for_entity(entity));
}

void BehaviorScenario::remember_entity_named(std::string_view alias,
                                             std::string_view name) {
  for (const QuaderEntitySnapshot &snapshot : api().query().entities()) {
    if (snapshot.name == name) {
      remember(alias, snapshot.id);
      return;
    }
  }
  ADD_FAILURE() << "Missing entity named " << name;
}

void BehaviorScenario::record_status(const QuaderApiStatus &status) {
  last_status_ = status;
}

void BehaviorScenario::record_selection(
    const QuaderApiResult<QuaderSelectionSummary> &selection) {
  record_status(selection);
  if (selection.ok) {
    last_selection_ = selection.value;
  }
}

void BehaviorScenario::record_drag(const QuaderTransformDragPreview &drag) {
  last_drag_ = drag;
  last_status_ = {
      .ok = drag.ok,
      .changed = drag.changed,
      .message = drag.message,
      .mutation_kind = QuaderMutationKind::Modeling,
      .revisions = api().revisions(),
  };
}

void BehaviorScenario::remember_selected_copy_alias() {
  const std::vector<EntityId> selected = session_.scene().selected_entities();
  if (selected.size() != 1u) {
    return;
  }

  const EntityId selected_entity = selected.front();
  const auto selected_already_aliased =
      std::any_of(aliases_.begin(), aliases_.end(), [&](const auto &entry) {
        return entry.second.entity == selected_entity;
      });
  if (selected_already_aliased) {
    return;
  }

  for (const auto &[alias, object] : aliases_) {
    if (object.entity == selected_entity) {
      continue;
    }
    const std::string copy_alias = alias + ".copy";
    if (!aliases_.contains(copy_alias)) {
      remember(copy_alias, object_for_entity(selected_entity));
    }
    return;
  }
}

QuaderModelingObjectRef
BehaviorScenario::object_for_entity(EntityId entity) const {
  QuaderModelingObjectRef result;
  result.scene_session_id =
      QuaderApiSessionInternalAccess::scene_session_id(api_session_);
  result.entity = entity;
  result.entity_uuid = session_.scene().uuid(entity);
  result.editor_object_id = session_.scene().editor_object_id(entity);
  if (const ModelingObjectComponent *modeling =
          session_.scene().modeling_object(entity)) {
    result.modeling_object_id = modeling->object_id;
  }
  if (const MeshComponent *mesh = session_.scene().mesh(entity)) {
    result.mesh = mesh->mesh;
  }
  if (const MaterialComponent *material = session_.scene().material(entity)) {
    result.material = material->material;
  }
  return result;
}

QVec3 BehaviorScenario::world_position(EntityId entity) const {
  return transform_point(session_.scene().world_transform_matrix(entity), {});
}

ViewportOverlayFrame BehaviorScenario::selection_overlay_frame() const {
  return test_support::visible_modeling_overlay_frame(session_.modeling());
}

BehaviorGiven &BehaviorGiven::unit_box(std::string_view alias,
                                       std::string name) {
  return box(alias, {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
             std::move(name));
}

BehaviorGiven& BehaviorGiven::box(std::string_view alias, QuaderBounds bounds, std::string name) {
    QuaderCreateBoxArgs args;
    args.bounds = {
            .min = bounds.min,
            .max = bounds.max,
    };
    args.name = std::move(name);
    const QuaderApiResult<QuaderModelingObjectRef> result =
        scenario_.api().modeling().create_box(args);
    scenario_.record_status(result);
    if (result.ok) {
        scenario_.remember(alias, result.value);
    } else {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::mesh(std::string_view alias, QuaderPolygonDocument document, std::string name) {
    QuaderCreateMeshArgs args;
    args.name = std::move(name);
    const QuaderApiResult<QuaderModelingObjectRef> result =
        scenario_.api().modeling().create_mesh(std::move(document), args);
    scenario_.record_status(result);
    if (result.ok) {
        scenario_.remember(alias, result.value);
    } else {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorGiven &BehaviorGiven::entity(std::string_view alias, std::string name,
                                     QuaderEntityKind kind,
                                     QuaderTransform transform,
                                     std::string_view parent_alias) {
  QuaderCreateEntityArgs args;
  args.name = std::move(name);
  args.kind = kind;
  args.transform = transform;
  if (!parent_alias.empty()) {
    args.parent = scenario_.entity(parent_alias);
  }
  const QuaderApiResult<QuaderEntityRef> result =
      scenario_.api().scene().create_entity(args);
  scenario_.record_status(result);
  if (result.ok) {
    scenario_.remember(alias, result.value);
  } else {
    ADD_FAILURE() << result.message;
  }
  return *this;
}

BehaviorGiven &
BehaviorGiven::selected_aliases(std::initializer_list<std::string_view> aliases,
                                std::string_view active_alias) {
  std::vector<EntityId> entities;
  entities.reserve(aliases.size());
  for (const std::string_view alias : aliases) {
    entities.push_back(scenario_.entity(alias));
  }
  const QuaderApiStatus status = scenario_.api().selection().select_objects(
      std::span<const EntityId>(entities.data(), entities.size()),
      scenario_.entity(active_alias));
  scenario_.record_status(status);
  if (!status.ok) {
    ADD_FAILURE() << status.message;
  }
  return *this;
}

BehaviorGiven& BehaviorGiven::selected(std::string_view alias) {
  const QuaderApiStatus status =
      scenario_.api().selection().select_object(scenario_.object(alias));
  scenario_.record_status(status);
  if (!status.ok) {
    ADD_FAILURE() << status.message;
  }
    return *this;
}

BehaviorGiven &BehaviorGiven::edit_mode() {
  const auto status = scenario_.editor().mode().set(QuaderEditorMode::Edit);
  if (!status.ok()) {
    ADD_FAILURE() << status.error_message();
  }
  return *this;
}

BehaviorGiven &BehaviorGiven::object_mode() {
  const auto status = scenario_.editor().mode().set(QuaderEditorMode::Object);
  if (!status.ok()) {
    ADD_FAILURE() << status.error_message();
  }
  return *this;
}

BehaviorGiven &BehaviorGiven::selection_mode(QuaderSelectionMode mode) {
  const QuaderApiStatus status = scenario_.api().selection().set_mode(mode);
  scenario_.record_status(status);
  if (!status.ok) {
    ADD_FAILURE() << status.message;
  }
  return *this;
}

BehaviorGiven &BehaviorGiven::vertex_selected(std::string_view alias,
                                              int vertex) {
  const QuaderApiStatus status = scenario_.api().selection().select_vertex(
      scenario_.object(alias), vertex);
  scenario_.record_status(status);
  if (!status.ok) {
    ADD_FAILURE() << status.message;
  }
  return *this;
}

BehaviorGiven &BehaviorGiven::edge_selected(std::string_view alias, int a,
                                            int b) {
  const QuaderApiStatus status =
      scenario_.api().selection().select_edge(scenario_.object(alias), a, b);
  scenario_.record_status(status);
  if (!status.ok) {
    ADD_FAILURE() << status.message;
  }
  return *this;
}

BehaviorGiven &BehaviorGiven::face_selected(std::string_view alias, int face) {
  const QuaderApiStatus status =
      scenario_.api().selection().select_face(scenario_.object(alias), face);
  scenario_.record_status(status);
  if (!status.ok) {
    ADD_FAILURE() << status.message;
  }
  return *this;
}

BehaviorGiven &
BehaviorGiven::all_components_selected(QuaderSelectionMode mode) {
  selection_mode(mode);
  const QuaderApiStatus status = scenario_.api().selection().select_all();
  scenario_.record_status(status);
  if (!status.ok) {
    ADD_FAILURE() << status.message;
  }
  return *this;
}

BehaviorGiven& BehaviorGiven::transform(std::string_view alias, QuaderTransform transform) {
  const QuaderApiStatus status =
      scenario_.api().scene().set_transform(scenario_.entity(alias), transform);
  scenario_.record_status(status);
  if (!status.ok) {
    ADD_FAILURE() << status.message;
  }
    return *this;
}

BehaviorGiven& BehaviorGiven::material(std::string_view alias, AssetId material) {
  const QuaderApiStatus status =
      scenario_.api().assets().assign_material_to_object(
          scenario_.object(alias), material);
  scenario_.record_status(status);
  if (!status.ok) {
    ADD_FAILURE() << status.message;
  }
    return *this;
}

BehaviorWhen& BehaviorWhen::activate(ModelingViewportTool) {
  scenario_.record_status(failed_status("Viewport tool activation is not part "
                                        "of the current QuaderApi surface."));
  return *this;
}

BehaviorWhen &BehaviorWhen::pick(QVec3 ray_origin, PickOptions options) {
  const QuaderApiResult<QuaderSelectionSummary> result =
      scenario_.api().selection().pick_select(api_pick_at(ray_origin, options));
  scenario_.record_selection(result);
  return *this;
}

BehaviorWhen &BehaviorWhen::hover(QVec3 ray_origin, HoverOptions options) {
  const QuaderApiResult<QuaderHoverSummary> result =
      scenario_.api().selection().hover(api_hover_at(ray_origin, options));
  scenario_.record_status(result);
  return *this;
}

BehaviorWhen &BehaviorWhen::hover_vertex(std::string_view alias, int vertex) {
  const QuaderApiResult<QuaderHoverSummary> result =
      scenario_.api().selection().hover_vertex(scenario_.object(alias), vertex);
  scenario_.record_status(result);
  return *this;
}

BehaviorWhen &BehaviorWhen::clear_hover() {
  const QuaderApiStatus status = scenario_.api().selection().clear_hover();
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::begin_transform_preview(bool duplicate_selected) {
  const QuaderApiStatus status =
      scenario_.api().modeling().begin_transform_preview(duplicate_selected);
  scenario_.record_status(status);
  if (status.ok && duplicate_selected) {
    scenario_.remember_selected_copy_alias();
  }
  return *this;
}

BehaviorWhen &BehaviorWhen::translate_preview(QVec3 delta) {
  const QuaderApiStatus status =
      scenario_.api().modeling().update_transform_translate(delta);
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::matrix_preview(const QuaderTransform3 &transform,
                                           QVec3 pivot) {
  const QuaderApiStatus status =
      scenario_.api().modeling().update_transform_matrix(transform, pivot);
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::commit_transform_preview() {
  const QuaderApiStatus status =
      scenario_.api().modeling().commit_transform_preview();
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::cancel_transform_preview() {
  const QuaderApiStatus status =
      scenario_.api().modeling().cancel_transform_preview();
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::begin_transform_drag(DragBeginOptions options) {
  if (options.use_selection_center) {
    if (const std::optional<QVec3> center =
            scenario_.api().query().selection_center()) {
      options.pivot = *center;
    }
  }
  const QuaderApiResult<QuaderTransformDragPreview> result =
      scenario_.api().modeling().begin_transform_drag(
          api_transform_drag_begin(options));
  scenario_.record_drag(result.value);
  if (!result.ok) {
    ADD_FAILURE() << result.message;
  }
  return *this;
}

BehaviorWhen &BehaviorWhen::update_transform_drag(QVec2 screen_position) {
  const QuaderApiResult<QuaderTransformDragPreview> result =
      scenario_.api().modeling().update_transform_drag(
          api_transform_drag_update(screen_position));
  scenario_.record_drag(result.value);
  if (!result.ok) {
    ADD_FAILURE() << result.message;
  }
  return *this;
}

BehaviorWhen &BehaviorWhen::update_transform_drag(float screen_x) {
  return update_transform_drag(QVec2{screen_x, 0.0f});
}

BehaviorWhen &BehaviorWhen::begin_x_move_drag(DragBeginOptions options) {
  return begin_transform_drag(options);
}

BehaviorWhen &BehaviorWhen::update_x_move_drag(float screen_x) {
  return update_transform_drag(screen_x);
}

BehaviorWhen &BehaviorWhen::commit_drag() {
  const QuaderApiResult<QuaderTransformDragPreview> result =
      scenario_.api().modeling().commit_transform_drag();
  scenario_.record_drag(result.value);
  if (!result.ok) {
    ADD_FAILURE() << result.message;
  }
  return *this;
}

BehaviorWhen &BehaviorWhen::cancel_drag() {
  const QuaderApiStatus status =
      scenario_.api().modeling().cancel_transform_drag();
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::execute_tool(std::string_view tool_id) {
  const QuaderApiStatus status =
      scenario_.api().modeling().execute_tool(tool_id);
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &
BehaviorWhen::dispatch_viewport_shortcut(const ViewportShortcut &shortcut) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input_frame;
  input_frame.viewport_focused = true;
  input_frame.viewport_size = {800.0f, 600.0f};
  input_frame.resolved_shortcuts = {shortcut};

  const ViewportInputResult input = router.handle_frame(input_frame, camera);
  const ViewportShortcutControllerResult result =
      ViewportShortcutController{}.handle_modeling_shortcut(input,
                                                            scenario_.editor());
  scenario_.record_status({
      .ok = input.shortcut_handled && result.command_executed,
      .changed = result.command_executed,
      .message = result.command_executed
                     ? std::string{}
                     : "Viewport shortcut did not execute a command.",
      .mutation_kind = result.command_executed ? QuaderMutationKind::Document
                                               : QuaderMutationKind::None,
      .revisions = scenario_.api().revisions(),
  });
  return *this;
}

BehaviorWhen& BehaviorWhen::execute(QuaderCommandId command) {
    const QuaderApiStatus status = scenario_.editor().commands().execute(command);
    scenario_.record_status(status);
    return *this;
}

BehaviorWhen &BehaviorWhen::execute(std::string_view command_id) {
  const QuaderApiStatus status =
      scenario_.editor().commands().execute(command_id);
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::assign_material_slot(std::uint32_t material_slot) {
  const QuaderApiStatus status =
      scenario_.api().modeling().assign_material_slot_to_selection(
          material_slot);
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::clear_scene_selection() {
  const QuaderApiStatus status =
      scenario_.api().selection().select_objects(std::span<const EntityId>());
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::group_selection(std::string_view alias,
                                            std::string name,
                                            QuaderTransform transform) {
  const QuaderApiResult<QuaderEntityRef> result =
      scenario_.api().scene().group_selection(std::move(name), transform);
  scenario_.record_status(result);
  if (result.ok) {
    scenario_.remember(alias, result.value);
  } else {
    ADD_FAILURE() << result.message;
  }
  return *this;
}

BehaviorWhen &BehaviorWhen::ungroup_selection() {
  const QuaderApiStatus status = scenario_.api().scene().ungroup_selection();
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &
BehaviorWhen::duplicate_selected_hierarchy(std::string_view root_alias) {
  const QuaderApiResult<QuaderDuplicateHierarchyResult> result =
      scenario_.api().scene().duplicate_selected_hierarchy();
  scenario_.record_status(result);
  if (result.ok && result.value.root_entities.size() == 1u) {
    scenario_.remember(root_alias, result.value.root_entities.front());
  } else if (result.ok) {
    ADD_FAILURE() << "Expected exactly one duplicated root, got "
                  << result.value.root_entities.size();
  } else {
    ADD_FAILURE() << result.message;
  }
  return *this;
}

BehaviorWhen& BehaviorWhen::destroy(std::string_view alias) {
    const QuaderApiStatus status = scenario_.api().scene().destroy(scenario_.entity(alias));
    scenario_.record_status(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::rename(std::string_view alias, std::string name) {
    const QuaderApiStatus status = scenario_.api().scene().rename(scenario_.entity(alias), std::move(name));
    scenario_.record_status(status);
    return *this;
}

BehaviorWhen &BehaviorWhen::reparent(std::string_view alias,
                                     std::string_view parent_alias,
                                     bool preserve_world_transform) {
  const QuaderApiStatus status = scenario_.api().scene().reparent(
      scenario_.entity(alias), scenario_.entity(parent_alias),
      preserve_world_transform);
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::set_transform(std::string_view alias,
                                          QuaderTransform transform) {
  const QuaderApiStatus status =
      scenario_.api().scene().set_transform(scenario_.entity(alias), transform);
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::set_visible(std::string_view alias, bool visible) {
  const QuaderApiStatus status =
      scenario_.api().scene().set_visible(scenario_.entity(alias), visible);
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::set_locked(std::string_view alias, bool locked) {
  const QuaderApiStatus status =
      scenario_.api().scene().set_locked(scenario_.entity(alias), locked);
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen &BehaviorWhen::set_group_flags(std::string_view alias,
                                            QuaderTransform transform,
                                            bool visible, bool locked) {
  const EntityId entity = scenario_.entity(alias);
  const QuaderApiStatus status = scenario_.api()
                                     .scene()
                                     .batch("Group Flags")
                                     .set_transform(entity, transform)
                                     .set_visible(entity, visible)
                                     .set_locked(entity, locked)
                                     .commit();
  scenario_.record_status(status);
  return *this;
}

BehaviorWhen& BehaviorWhen::undo() {
  scenario_.record_status(scenario_.api().history().undo());
  return *this;
}

BehaviorWhen& BehaviorWhen::redo() {
  scenario_.record_status(scenario_.api().history().redo());
  return *this;
}

CommandExpect BehaviorThen::command() const {
  return CommandExpect(scenario_.last_status_);
}

SceneExpect BehaviorThen::scene() const {
    return SceneExpect(scenario_);
}

SelectionExpect BehaviorThen::selection() const {
    return SelectionExpect(scenario_);
}

MeshExpect BehaviorThen::mesh(std::string_view alias) const {
    return MeshExpect(scenario_, alias);
}

OverlayExpect BehaviorThen::overlay() const {
    return OverlayExpect(scenario_);
}

AssetExpect BehaviorThen::assets() const {
    return AssetExpect(scenario_);
}

ToolStatusExpect BehaviorThen::tool() const {
    return ToolStatusExpect(scenario_);
}

UndoRedoExpect BehaviorThen::history() const {
    return UndoRedoExpect(scenario_);
}

SnapshotExpect BehaviorThen::snapshot(const BehaviorSnapshot& snapshot) const {
    return SnapshotExpect(snapshot);
}

} // namespace quader::test
