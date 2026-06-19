#include "session_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace quader_editor_core {
namespace {

LightType sanitize_light_type(LightType type)
{
	switch (type) {
	case LightType::directional:
	case LightType::point:
	case LightType::spot:
		return type;
	}
	return LightType::point;
}

LightShadowMode sanitize_light_shadow_mode(LightShadowMode mode)
{
	switch (mode) {
	case LightShadowMode::none:
	case LightShadowMode::hard:
	case LightShadowMode::soft:
		return mode;
	}
	return LightShadowMode::soft;
}

float finite_or(float value, float fallback)
{
	return std::isfinite(value) ? value : fallback;
}

float clamp_min(float value, float minimum)
{
	return std::max(finite_or(value, minimum), minimum);
}

float clamp_range(float value, float minimum, float maximum)
{
	return std::clamp(finite_or(value, minimum), minimum, maximum);
}

bool vec3_equal(quader_poly::Vec3 left, quader_poly::Vec3 right)
{
	return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool vec3_tiny(quader_poly::Vec3 value)
{
	return quader_poly::length_squared(value) <= transform_point_epsilon;
}

quader_poly::Vec3 normalized_or(quader_poly::Vec3 value, quader_poly::Vec3 fallback)
{
	const float length = quader_poly::length(value);
	if (length <= 0.000001F) {
		return fallback;
	}
	return value / length;
}

bool basis_is_orthonormal(const quader_poly::Transform3& transform)
{
	const float x_length = quader_poly::length(transform.x_axis);
	const float y_length = quader_poly::length(transform.y_axis);
	const float z_length = quader_poly::length(transform.z_axis);
	return std::abs(x_length - 1.0F) <= transform_basis_epsilon &&
		std::abs(y_length - 1.0F) <= transform_basis_epsilon &&
		std::abs(z_length - 1.0F) <= transform_basis_epsilon &&
		std::abs(quader_poly::dot(transform.x_axis, transform.y_axis)) <= transform_basis_epsilon &&
		std::abs(quader_poly::dot(transform.x_axis, transform.z_axis)) <= transform_basis_epsilon &&
		std::abs(quader_poly::dot(transform.y_axis, transform.z_axis)) <= transform_basis_epsilon;
}

quader_poly::Vec3 basis_xform(const quader_poly::Transform3& transform, quader_poly::Vec3 point)
{
	return {
		transform.x_axis.x * point.x + transform.y_axis.x * point.y + transform.z_axis.x * point.z,
		transform.x_axis.y * point.x + transform.y_axis.y * point.y + transform.z_axis.y * point.z,
		transform.x_axis.z * point.x + transform.y_axis.z * point.y + transform.z_axis.z * point.z,
	};
}

quader_poly::Vec3 transformed_point(const quader_poly::Transform3& transform, quader_poly::Vec3 pivot, quader_poly::Vec3 point)
{
	const quader_poly::Vec3 pivot_after_basis = basis_xform(transform, pivot);
	const quader_poly::Vec3 translation = pivot + transform.origin - pivot_after_basis;
	return basis_xform(transform, point) + translation;
}

void include_point(Bounds3& bounds, quader_poly::Vec3 point)
{
	if (bounds.empty) {
		bounds.empty = false;
		bounds.min = point;
		bounds.max = point;
		return;
	}
	bounds.min.x = std::min(bounds.min.x, point.x);
	bounds.min.y = std::min(bounds.min.y, point.y);
	bounds.min.z = std::min(bounds.min.z, point.z);
	bounds.max.x = std::max(bounds.max.x, point.x);
	bounds.max.y = std::max(bounds.max.y, point.y);
	bounds.max.z = std::max(bounds.max.z, point.z);
}

std::string default_light_name(LightType type, int id)
{
	switch (type) {
	case LightType::directional:
		return "Directional Light " + std::to_string(id);
	case LightType::point:
		return "Point Light " + std::to_string(id);
	case LightType::spot:
		return "Spot Light " + std::to_string(id);
	}
	return "Light " + std::to_string(id);
}

LightEntity* light_by_id(std::vector<LightEntity>& lights, int light_id)
{
	const auto iterator = std::find_if(lights.begin(), lights.end(), [light_id](const LightEntity& light) {
		return light.id == light_id;
	});
	return iterator == lights.end() ? nullptr : &*iterator;
}

const LightEntity* light_by_id(const std::vector<LightEntity>& lights, int light_id)
{
	const auto iterator = std::find_if(lights.begin(), lights.end(), [light_id](const LightEntity& light) {
		return light.id == light_id;
	});
	return iterator == lights.end() ? nullptr : &*iterator;
}

bool clear_mesh_selection(std::vector<PolygonObject>& objects, int& active_object_id)
{
	const bool changed = has_selection(objects, SelectionMode::object, active_object_id) || active_object_id != -1;
	clear_selection(objects, active_object_id);
	return changed;
}

float light_float_value(const LightEntity& light, LightFloatProperty property)
{
	switch (property) {
	case LightFloatProperty::intensity:
		return light.intensity;
	case LightFloatProperty::range:
		return light.range;
	case LightFloatProperty::spot_angle:
		return light.spot_angle;
	case LightFloatProperty::inner_spot_angle:
		return light.inner_spot_angle;
	case LightFloatProperty::shadow_strength:
		return light.shadow_strength;
	case LightFloatProperty::shadow_bias:
		return light.shadow_bias;
	case LightFloatProperty::shadow_normal_bias:
		return light.shadow_normal_bias;
	case LightFloatProperty::shadow_near_plane:
		return light.shadow_near_plane;
	}
	return 0.0F;
}

float sanitize_light_float_value(const LightEntity& light, LightFloatProperty property, float value)
{
	switch (property) {
	case LightFloatProperty::intensity:
		return clamp_min(value, 0.0F);
	case LightFloatProperty::range:
		return clamp_min(value, 0.0F);
	case LightFloatProperty::spot_angle:
		return clamp_range(value, 1.0F, 179.0F);
	case LightFloatProperty::inner_spot_angle:
		return clamp_range(value, 0.0F, light.spot_angle);
	case LightFloatProperty::shadow_strength:
		return clamp_range(value, 0.0F, 1.0F);
	case LightFloatProperty::shadow_bias:
		return clamp_min(value, 0.0F);
	case LightFloatProperty::shadow_normal_bias:
		return clamp_min(value, 0.0F);
	case LightFloatProperty::shadow_near_plane:
		return clamp_min(value, 0.0F);
	}
	return value;
}

void set_light_float_value(LightEntity& light, LightFloatProperty property, float value)
{
	switch (property) {
	case LightFloatProperty::intensity:
		light.intensity = value;
		break;
	case LightFloatProperty::range:
		light.range = value;
		break;
	case LightFloatProperty::spot_angle:
		light.spot_angle = value;
		light.inner_spot_angle = std::min(light.inner_spot_angle, light.spot_angle);
		break;
	case LightFloatProperty::inner_spot_angle:
		light.inner_spot_angle = value;
		break;
	case LightFloatProperty::shadow_strength:
		light.shadow_strength = value;
		break;
	case LightFloatProperty::shadow_bias:
		light.shadow_bias = value;
		break;
	case LightFloatProperty::shadow_normal_bias:
		light.shadow_normal_bias = value;
		break;
	case LightFloatProperty::shadow_near_plane:
		light.shadow_near_plane = value;
		break;
	}
}

} // namespace

int next_light_entity_id(const std::vector<LightEntity>& lights, int next_light_id)
{
	int highest = 0;
	for (const LightEntity& light : lights) {
		highest = std::max(highest, light.id);
	}
	return std::max(next_light_id, highest + 1);
}

void normalize_active_light_id(const std::vector<LightEntity>& lights, int& active_light_id)
{
	if (active_light_id > 0) {
		const LightEntity* light = light_by_id(lights, active_light_id);
		if (light != nullptr && light->selected) {
			return;
		}
	}
	const auto iterator = std::find_if(lights.begin(), lights.end(), [](const LightEntity& light) {
		return light.selected;
	});
	active_light_id = iterator == lights.end() ? -1 : iterator->id;
}

std::vector<int> selected_light_ids(const std::vector<LightEntity>& lights)
{
	std::vector<int> ids;
	for (const LightEntity& light : lights) {
		if (light.selected) {
			ids.push_back(light.id);
		}
	}
	return ids;
}

bool clear_light_selection_state(std::vector<LightEntity>& lights, int& active_light_id)
{
	bool changed = active_light_id != -1;
	for (LightEntity& light : lights) {
		changed = changed || light.selected;
		light.selected = false;
	}
	active_light_id = -1;
	return changed;
}

std::optional<quader_poly::Vec3> selected_light_center(const std::vector<LightEntity>& lights)
{
	quader_poly::Vec3 sum {};
	int count = 0;
	for (const LightEntity& light : lights) {
		if (!light.selected) {
			continue;
		}
		sum += light.position;
		count++;
	}
	if (count <= 0) {
		return std::nullopt;
	}
	return sum / static_cast<float>(count);
}

Bounds3 selected_light_bounds(const std::vector<LightEntity>& lights)
{
	Bounds3 bounds;
	for (const LightEntity& light : lights) {
		if (light.selected) {
			include_point(bounds, light.position);
		}
	}
	return bounds;
}

Bounds3 light_bounds(const std::vector<LightEntity>& lights)
{
	Bounds3 bounds;
	for (const LightEntity& light : lights) {
		include_point(bounds, light.position);
	}
	return bounds;
}

CommandResult translate_selected_lights(std::vector<LightEntity>& lights, quader_poly::Vec3 delta)
{
	if (vec3_tiny(delta)) {
		return ok_result(false);
	}
	bool changed = false;
	for (LightEntity& light : lights) {
		if (!light.selected) {
			continue;
		}
		light.position += delta;
		changed = true;
	}
	return ok_result(changed);
}

CommandResult transform_selected_lights(
		std::vector<LightEntity>& lights,
		const quader_poly::Transform3& transform,
		quader_poly::Vec3 pivot)
{
	bool changed = false;
	const bool transform_direction = basis_is_orthonormal(transform);
	for (LightEntity& light : lights) {
		if (!light.selected) {
			continue;
		}
		const quader_poly::Vec3 next_position = transformed_point(transform, pivot, light.position);
		const quader_poly::Vec3 next_direction =
			transform_direction ? normalized_or(basis_xform(transform, light.direction), light.direction) : light.direction;
		if (!vec3_equal(light.position, next_position)) {
			light.position = next_position;
			changed = true;
		}
		if (!vec3_equal(light.direction, next_direction)) {
			light.direction = next_direction;
			changed = true;
		}
	}
	return ok_result(changed);
}

CommandResult EditorSession::create_light(LightType type, quader_poly::Vec3 position, quader_poly::Vec3 direction)
{
	type = sanitize_light_type(type);
	state_.next_light_id = next_light_entity_id(state_.lights, state_.next_light_id);
	const int light_id = state_.next_light_id++;

	LightEntity light;
	light.id = light_id;
	light.type = type;
	light.name = default_light_name(type, light_id);
	light.position = position;
	light.direction = normalized_or(direction, { 0.0F, -1.0F, 0.0F });
	light.selected = true;

	const bool selection_changed = clear_mesh_selection(state_.objects, state_.active_object_id) ||
		clear_light_selection_state(state_.lights, state_.active_light_id);
	state_.selection_mode = SelectionMode::object;
	state_.lights.push_back(std::move(light));
	state_.active_light_id = light_id;
	note_content_result(ok_result(true));
	if (selection_changed || state_.active_light_id == light_id) {
		note_selection_changed();
	}
	return ok_result(true);
}

CommandResult EditorSession::select_light(int light_id)
{
	if (light_by_id(state_.lights, light_id) == nullptr) {
		return error_result("Light not found.");
	}

	const bool mesh_changed = clear_mesh_selection(state_.objects, state_.active_object_id);
	bool light_changed = state_.active_light_id != light_id;
	state_.selection_mode = SelectionMode::object;
	for (LightEntity& light : state_.lights) {
		const bool selected = light.id == light_id;
		light_changed = light_changed || light.selected != selected;
		light.selected = selected;
	}
	state_.active_light_id = light_id;
	if (mesh_changed || light_changed) {
		note_selection_changed();
	}
	return ok_result(mesh_changed || light_changed);
}

CommandResult EditorSession::delete_selected_lights()
{
	const std::size_t old_size = state_.lights.size();
	state_.lights.erase(
		std::remove_if(state_.lights.begin(), state_.lights.end(), [](const LightEntity& light) {
			return light.selected;
		}),
		state_.lights.end());
	if (old_size == state_.lights.size()) {
		return error_result("Select one or more lights first.");
	}
	normalize_active_light_id(state_.lights, state_.active_light_id);
	note_content_result(ok_result(true));
	note_selection_changed();
	return ok_result(true);
}

CommandResult EditorSession::set_light_name(int light_id, std::string name)
{
	LightEntity* light = light_by_id(state_.lights, light_id);
	if (light == nullptr) {
		return error_result("Light not found.");
	}
	if (name.empty()) {
		name = default_light_name(light->type, light->id);
	}
	if (light->name == name) {
		return ok_result(false);
	}
	light->name = std::move(name);
	note_content_result(ok_result(true));
	return ok_result(true);
}

CommandResult EditorSession::set_light_enabled(int light_id, bool enabled)
{
	LightEntity* light = light_by_id(state_.lights, light_id);
	if (light == nullptr) {
		return error_result("Light not found.");
	}
	if (light->enabled == enabled) {
		return ok_result(false);
	}
	light->enabled = enabled;
	note_content_result(ok_result(true));
	return ok_result(true);
}

CommandResult EditorSession::set_light_type(int light_id, LightType type)
{
	LightEntity* light = light_by_id(state_.lights, light_id);
	if (light == nullptr) {
		return error_result("Light not found.");
	}
	type = sanitize_light_type(type);
	if (light->type == type) {
		return ok_result(false);
	}
	light->type = type;
	note_content_result(ok_result(true));
	return ok_result(true);
}

CommandResult EditorSession::set_light_color(int light_id, std::uint32_t color_abgr)
{
	LightEntity* light = light_by_id(state_.lights, light_id);
	if (light == nullptr) {
		return error_result("Light not found.");
	}
	if (light->color_abgr == color_abgr) {
		return ok_result(false);
	}
	light->color_abgr = color_abgr;
	note_content_result(ok_result(true));
	return ok_result(true);
}

CommandResult EditorSession::set_light_shadow_mode(int light_id, LightShadowMode shadow_mode)
{
	LightEntity* light = light_by_id(state_.lights, light_id);
	if (light == nullptr) {
		return error_result("Light not found.");
	}
	shadow_mode = sanitize_light_shadow_mode(shadow_mode);
	if (light->shadow_mode == shadow_mode) {
		return ok_result(false);
	}
	light->shadow_mode = shadow_mode;
	note_content_result(ok_result(true));
	return ok_result(true);
}

CommandResult EditorSession::set_light_float(int light_id, LightFloatProperty property, float value)
{
	LightEntity* light = light_by_id(state_.lights, light_id);
	if (light == nullptr) {
		return error_result("Light not found.");
	}
	value = sanitize_light_float_value(*light, property, value);
	if (light_float_value(*light, property) == value) {
		return ok_result(false);
	}
	set_light_float_value(*light, property, value);
	note_content_result(ok_result(true));
	return ok_result(true);
}

CommandResult EditorSession::set_light_position(int light_id, quader_poly::Vec3 position)
{
	LightEntity* light = light_by_id(state_.lights, light_id);
	if (light == nullptr) {
		return error_result("Light not found.");
	}
	if (vec3_equal(light->position, position)) {
		return ok_result(false);
	}
	light->position = position;
	note_content_result(ok_result(true));
	return ok_result(true);
}

CommandResult EditorSession::set_light_direction(int light_id, quader_poly::Vec3 direction)
{
	LightEntity* light = light_by_id(state_.lights, light_id);
	if (light == nullptr) {
		return error_result("Light not found.");
	}
	if (vec3_equal(light->direction, direction)) {
		return ok_result(false);
	}
	light->direction = normalized_or(direction, { 0.0F, -1.0F, 0.0F });
	note_content_result(ok_result(true));
	return ok_result(true);
}

std::vector<int> EditorSession::selected_light_ids() const
{
	return quader_editor_core::selected_light_ids(state_.lights);
}

} // namespace quader_editor_core
