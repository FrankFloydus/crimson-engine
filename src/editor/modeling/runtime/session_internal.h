////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace quader_editor_core {

inline constexpr float transform_basis_epsilon = 0.00001F;
inline constexpr float transform_point_epsilon = 0.00000001F;
inline constexpr float create_box_epsilon = 0.000001F;

CommandResult ok_result(bool changed);
CommandResult error_result(std::string message);
bool state_revisions_differ(const SessionState& left, const SessionState& right);
bool basis_is_identity(const quader_poly::Transform3& transform);
std::uint32_t saturated_history_size(std::size_t size);

int object_index_by_id(const std::vector<PolygonObject>& objects, int object_id);
Bounds3 document_bounds(const PolygonObject& object);
void align_component_selection_to_mode(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id);
bool clear_light_selection_state(std::vector<LightEntity>& lights, int& active_light_id);
std::optional<quader_poly::Vec3> selected_light_center(const std::vector<LightEntity>& lights);
Bounds3 light_bounds(const std::vector<LightEntity>& lights);
Bounds3 selected_light_bounds(const std::vector<LightEntity>& lights);
CommandResult translate_selected_lights(std::vector<LightEntity>& lights, quader_poly::Vec3 delta);
CommandResult transform_selected_lights(
	std::vector<LightEntity>& lights,
	const quader_poly::Transform3& transform,
	quader_poly::Vec3 pivot);

} // namespace quader_editor_core
