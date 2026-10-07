////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/runtime_tool_command_service.hpp"

#include "editor/modeling/modeling_viewport_camera_adapter.hpp"

#include <algorithm>
#include <utility>

#include <editor/interactions/cut_tool.hpp>

namespace quader {
namespace {

quader::QVec2 to_runtime(QVec2 value) { return {value.x, value.y}; }

quader::QVec3 to_runtime(QVec3 value) { return {value.x, value.y, value.z}; }

quader_poly::Ray to_runtime(const ModelingRay &ray) {
  return {
      .origin = to_runtime(ray.origin),
      .direction = to_runtime(normalize(ray.direction)),
  };
}

quader_editor_core::ViewportInteractionTool
to_runtime(ModelingViewportTool tool) {
  switch (tool) {
  case ModelingViewportTool::Move:
    return quader_editor_core::ViewportInteractionTool::Move;
  case ModelingViewportTool::Extrude:
    return quader_editor_core::ViewportInteractionTool::Extrude;
  case ModelingViewportTool::Rotate:
    return quader_editor_core::ViewportInteractionTool::Rotate;
  case ModelingViewportTool::Scale:
    return quader_editor_core::ViewportInteractionTool::Scale;
  case ModelingViewportTool::Box:
  case ModelingViewportTool::Cylinder:
  case ModelingViewportTool::Sphere:
    return quader_editor_core::ViewportInteractionTool::Box;
  case ModelingViewportTool::InsertEdgeLoop:
    return quader_editor_core::ViewportInteractionTool::InsertEdgeLoop;
  case ModelingViewportTool::Knife:
    return quader_editor_core::ViewportInteractionTool::Knife;
  case ModelingViewportTool::Cut:
    return quader_editor_core::ViewportInteractionTool::Cut;
  case ModelingViewportTool::Poly:
    return quader_editor_core::ViewportInteractionTool::Poly;
  case ModelingViewportTool::Mirror:
    return quader_editor_core::ViewportInteractionTool::Mirror;
  case ModelingViewportTool::Select:
    return quader_editor_core::ViewportInteractionTool::Select;
  case ModelingViewportTool::LassoSelect:
    return quader_editor_core::ViewportInteractionTool::LassoSelect;
  case ModelingViewportTool::PaintSelect:
    return quader_editor_core::ViewportInteractionTool::PaintSelect;
  }
  return quader_editor_core::ViewportInteractionTool::Select;
}

quader_editor_core::CutMode to_runtime(ModelingCutMode mode) {
  switch (mode) {
  case ModelingCutMode::DiscardLeft:
    return quader_editor_core::CutMode::KeepBack;
  case ModelingCutMode::DiscardRight:
    return quader_editor_core::CutMode::KeepFront;
  case ModelingCutMode::KeepBoth:
    return quader_editor_core::CutMode::KeepBoth;
  }
  return quader_editor_core::CutMode::KeepBoth;
}

quader_editor_core::EditorRuntimeCommand make_command(
    quader_editor_core::EditorRuntimeCommandId command_id,
    quader_editor_core::EditorRuntimeCommandSettings settings =
        std::monostate{}) {
  quader_editor_core::EditorRuntimeCommand command;
  command.command_id = command_id;
  command.settings = std::move(settings);
  return command;
}

quader_editor_core::InsertEdgeLoopPickRuntimeCommandSettings
insert_edge_loop_pick_settings(const ModelingToolRayEvent &event,
                               int target_object_id,
                               std::optional<quader_editor_core::PickHitRecord>
                                   resolved_hit,
                               std::optional<float> resolved_factor,
                               bool resolved_factor_from_world) {
  quader_editor_core::InsertEdgeLoopPickRuntimeCommandSettings settings;
  settings.ray = to_runtime(event.ray);
  settings.target_object_id = target_object_id;
  settings.resolved_hit = std::move(resolved_hit);
  settings.resolved_factor = resolved_factor;
  settings.resolved_factor_from_world = resolved_factor_from_world;
  settings.grid_size = event.grid_size;
  settings.vertex_radius = event.vertex_radius;
  settings.edge_radius = event.edge_radius;
  settings.snap_enabled = event.snap_enabled;
  return settings;
}

quader_editor_core::CutRayRuntimeCommandSettings
cut_ray_settings(const ModelingToolRayEvent &event, bool dragging) {
  quader_editor_core::CutRayRuntimeCommandSettings settings;
  settings.camera = modeling_runtime_camera(event.camera);
  settings.ray = to_runtime(event.ray);
  settings.grid_size = event.grid_size;
  settings.dragging = dragging;
  return settings;
}

quader_editor_core::MirrorRayRuntimeCommandSettings
mirror_ray_settings(const ModelingToolRayEvent &event, bool dragging) {
  quader_editor_core::MirrorRayRuntimeCommandSettings settings;
  settings.camera = modeling_runtime_camera(event.camera);
  settings.ray = to_runtime(event.ray);
  settings.grid_size = event.grid_size;
  settings.dragging = dragging;
  return settings;
}

quader_editor_core::BoxRayRuntimeCommandSettings
box_ray_settings(
    const ModelingBoxToolEvent &event,
    std::optional<quader_editor_core::PickHitRecord> resolved_surface_hit =
        std::nullopt,
    bool surface_hit_resolved = false) {
  quader_editor_core::BoxRayRuntimeCommandSettings settings;
  settings.ray = to_runtime(event.ray);
  settings.resolved_surface_hit = std::move(resolved_surface_hit);
  settings.surface_hit_resolved = surface_hit_resolved;
  settings.grid_size = event.grid_size;
  settings.snap_enabled = event.snap_enabled;
  return settings;
}

quader_editor_core::BoxHeightRuntimeCommandSettings
box_height_settings(const ModelingBoxToolEvent &event) {
  quader_editor_core::BoxHeightRuntimeCommandSettings settings;
  settings.screen_position = to_runtime(event.screen_position);
  settings.ray = to_runtime(event.ray);
  settings.grid_size = event.grid_size;
  settings.snap_enabled = event.snap_enabled;
  settings.has_camera = event.has_camera;
  if (event.has_camera) {
    settings.camera = modeling_runtime_camera(event.camera);
  }
  return settings;
}

} // namespace

quader_editor_core::EditorRuntimeCommand RuntimeToolCommandService::no_settings(
    quader_editor_core::EditorRuntimeCommandId command_id) {
  return make_command(command_id);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::set_active_tool(ModelingViewportTool tool) {
  quader_editor_core::SetActiveToolRuntimeCommandSettings settings;
  settings.tool = to_runtime(tool);
  return make_command(quader_editor_core::EditorRuntimeCommandId::SetActiveTool,
                      settings);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::update_insert_edge_loop_hover(
    const ModelingToolRayEvent &event, int target_object_id,
    std::optional<quader_editor_core::PickHitRecord> resolved_hit,
    std::optional<float> resolved_factor) {
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::UpdateInsertEdgeLoopHover,
      insert_edge_loop_pick_settings(event, target_object_id,
                                     std::move(resolved_hit),
                                     resolved_factor, true));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::begin_insert_edge_loop_drag(
    const ModelingToolRayEvent &event, int target_object_id,
    std::optional<quader_editor_core::PickHitRecord> resolved_hit,
    std::optional<float> resolved_factor) {
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::BeginInsertEdgeLoopDrag,
      insert_edge_loop_pick_settings(event, target_object_id,
                                     std::move(resolved_hit),
                                     resolved_factor, true));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::update_insert_edge_loop_drag(
    const ModelingToolRayEvent &event, int target_object_id,
    std::optional<float> resolved_factor,
    bool resolved_factor_from_world) {
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::UpdateInsertEdgeLoopDrag,
      insert_edge_loop_pick_settings(event, target_object_id, std::nullopt,
                                     resolved_factor,
                                     resolved_factor_from_world));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::cancel_insert_edge_loop() {
  return no_settings(
      quader_editor_core::EditorRuntimeCommandId::CancelInsertEdgeLoop);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::activate_cut() {
  return no_settings(quader_editor_core::EditorRuntimeCommandId::ActivateCut);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::update_cut_hover(const ModelingToolRayEvent &event) {
  return make_command(quader_editor_core::EditorRuntimeCommandId::UpdateCutHover,
                      cut_ray_settings(event, event.dragging));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::click_cut(const ModelingToolRayEvent &event) {
  return make_command(quader_editor_core::EditorRuntimeCommandId::ClickCut,
                      cut_ray_settings(event, event.dragging));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::begin_cut_point_drag(int point_index) {
  quader_editor_core::CutPointDragRuntimeCommandSettings settings;
  settings.point_index = point_index;
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::BeginCutPointDrag, settings);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::begin_cut_line_drag(
    const ModelingToolRayEvent &event) {
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::BeginCutLineDrag,
      cut_ray_settings(event, true));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::end_cut_drag() {
  return no_settings(quader_editor_core::EditorRuntimeCommandId::EndCutDrag);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::set_cut_mode(ModelingCutMode mode) {
  quader_editor_core::SetCutModeRuntimeCommandSettings settings;
  settings.mode = quader_editor_core::cut_mode_id(to_runtime(mode));
  return make_command(quader_editor_core::EditorRuntimeCommandId::SetCutMode,
                      settings);
}

quader_editor_core::EditorRuntimeCommand RuntimeToolCommandService::cancel_cut() {
  return no_settings(quader_editor_core::EditorRuntimeCommandId::CancelCut);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::activate_mirror() {
  return no_settings(
      quader_editor_core::EditorRuntimeCommandId::ActivateMirror);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::update_mirror_hover(
    const ModelingToolRayEvent &event) {
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::UpdateMirrorHover,
      mirror_ray_settings(event, event.dragging));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::click_mirror(const ModelingToolRayEvent &event) {
  return make_command(quader_editor_core::EditorRuntimeCommandId::ClickMirror,
                      mirror_ray_settings(event, event.dragging));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::begin_mirror_point_drag(int point_index) {
  quader_editor_core::MirrorPointDragRuntimeCommandSettings settings;
  settings.point_index = point_index;
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::BeginMirrorPointDrag,
      settings);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::begin_mirror_line_drag(
    const ModelingToolRayEvent &event) {
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::BeginMirrorLineDrag,
      mirror_ray_settings(event, true));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::end_mirror_drag() {
  return no_settings(quader_editor_core::EditorRuntimeCommandId::EndMirrorDrag);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::cancel_mirror() {
  return no_settings(quader_editor_core::EditorRuntimeCommandId::CancelMirror);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::update_poly_hover(
    const ModelingToolRayEvent &event,
    std::optional<quader_editor_core::PickHitRecord> resolved_surface_hit) {
  quader_editor_core::PolyHoverRuntimeCommandSettings settings;
  settings.camera = modeling_runtime_camera(event.camera);
  settings.ray = to_runtime(event.ray);
  settings.resolved_surface_hit = std::move(resolved_surface_hit);
  settings.surface_hit_resolved = true;
  settings.grid_size = event.grid_size;
  settings.snap_enabled = event.snap_enabled;
  settings.constrain_axis = event.constrain_axis;
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::UpdatePolyHover, settings);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::accept_poly_point() {
  return no_settings(quader_editor_core::EditorRuntimeCommandId::AcceptPolyPoint);
}

quader_editor_core::EditorRuntimeCommand RuntimeToolCommandService::cancel_poly() {
  return no_settings(quader_editor_core::EditorRuntimeCommandId::CancelPoly);
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::update_box_hover(
    const ModelingBoxToolEvent &event,
    std::optional<quader_editor_core::PickHitRecord> resolved_surface_hit) {
  return make_command(quader_editor_core::EditorRuntimeCommandId::UpdateBoxHover,
                      box_ray_settings(event, std::move(resolved_surface_hit),
                                       true));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::begin_box_footprint(
    const ModelingBoxToolEvent &event,
    std::optional<quader_editor_core::PickHitRecord> resolved_surface_hit) {
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::BeginBoxFootprint,
      box_ray_settings(event, std::move(resolved_surface_hit), true));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::update_box_footprint(
    const ModelingBoxToolEvent &event) {
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::UpdateBoxFootprint,
      box_ray_settings(event));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::begin_box_height(const ModelingBoxToolEvent &event) {
  return make_command(quader_editor_core::EditorRuntimeCommandId::BeginBoxHeight,
                      box_height_settings(event));
}

quader_editor_core::EditorRuntimeCommand
RuntimeToolCommandService::update_box_height(const ModelingBoxToolEvent &event) {
  return make_command(
      quader_editor_core::EditorRuntimeCommandId::UpdateBoxHeight,
      box_height_settings(event));
}

quader_editor_core::EditorRuntimeCommand RuntimeToolCommandService::cancel_box() {
  return no_settings(quader_editor_core::EditorRuntimeCommandId::CancelBox);
}

} // namespace quader
