////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"

#include <editor/modeling/runtime/runtime_command.hpp>

#include <optional>

namespace quader {

/**
 * Builds typed runtime commands for viewport modeling tools.
 */
class RuntimeToolCommandService {
public:
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  no_settings(quader_editor_core::EditorRuntimeCommandId command_id);

  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  set_active_tool(ModelingViewportTool tool);

  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  update_insert_edge_loop_hover(const ModelingToolRayEvent &event,
                                int target_object_id = -1,
                                std::optional<quader_editor_core::PickHitRecord>
                                    resolved_hit = std::nullopt,
                                std::optional<float> resolved_factor =
                                    std::nullopt);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  begin_insert_edge_loop_drag(const ModelingToolRayEvent &event,
                              int target_object_id = -1,
                              std::optional<quader_editor_core::PickHitRecord>
                                  resolved_hit = std::nullopt,
                              std::optional<float> resolved_factor =
                                  std::nullopt);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  update_insert_edge_loop_drag(const ModelingToolRayEvent &event,
                               int target_object_id = -1,
                               std::optional<float> resolved_factor =
                                   std::nullopt,
                               bool resolved_factor_from_world = false);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  commit_insert_edge_loop_drag();
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  cancel_insert_edge_loop();

  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand activate_cut();
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  update_cut_hover(const ModelingToolRayEvent &event);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  click_cut(const ModelingToolRayEvent &event);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  begin_cut_point_drag(int point_index);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  begin_cut_line_drag(const ModelingToolRayEvent &event);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand end_cut_drag();
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  set_cut_mode(ModelingCutMode mode);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand finalize_cut();
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand cancel_cut();

  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  activate_mirror();
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  update_mirror_hover(const ModelingToolRayEvent &event);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  click_mirror(const ModelingToolRayEvent &event);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  begin_mirror_point_drag(int point_index);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  begin_mirror_line_drag(const ModelingToolRayEvent &event);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  end_mirror_drag();
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  finalize_mirror();
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  cancel_mirror();

  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  update_poly_hover(const ModelingToolRayEvent &event);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  accept_poly_point();
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  commit_poly();
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand cancel_poly();

  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  update_box_hover(
      const ModelingBoxToolEvent &event,
      std::optional<quader_editor_core::PickHitRecord> resolved_surface_hit);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  begin_box_footprint(
      const ModelingBoxToolEvent &event,
      std::optional<quader_editor_core::PickHitRecord> resolved_surface_hit);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  update_box_footprint(const ModelingBoxToolEvent &event);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  begin_box_height(const ModelingBoxToolEvent &event);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  update_box_height(const ModelingBoxToolEvent &event);
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand commit_box();
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand cancel_box();
  [[nodiscard]] static quader_editor_core::EditorRuntimeCommand
  create_box_from_bounds(const ModelingCreateBoxRequest &request);
};

} // namespace quader
