////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <io/qdr/qdr_runtime_io_compat.hpp>

namespace quader_io {
namespace {

QdrSelectionMode qdr_selection_mode_from_editor(quader_editor_core::SelectionMode mode)
{
	switch (mode) {
        case quader_editor_core::SelectionMode::Vertex:
          return QdrSelectionMode::Vertex;
        case quader_editor_core::SelectionMode::Edge:
          return QdrSelectionMode::Edge;
        case quader_editor_core::SelectionMode::Face:
          return QdrSelectionMode::Face;
        case quader_editor_core::SelectionMode::Object:
          return QdrSelectionMode::Object;
        }
        return QdrSelectionMode::Object;
}

quader_editor_core::SelectionMode editor_selection_mode_from_qdr(QdrSelectionMode mode)
{
	switch (mode) {
        case QdrSelectionMode::Vertex:
          return quader_editor_core::SelectionMode::Vertex;
        case QdrSelectionMode::Edge:
          return quader_editor_core::SelectionMode::Edge;
        case QdrSelectionMode::Face:
          return quader_editor_core::SelectionMode::Face;
        case QdrSelectionMode::Object:
          return quader_editor_core::SelectionMode::Object;
        }
        return quader_editor_core::SelectionMode::Object;
}

QdrLightType qdr_light_type_from_editor(quader_editor_core::LightType type)
{
	switch (type) {
        case quader_editor_core::LightType::Directional:
          return QdrLightType::Directional;
        case quader_editor_core::LightType::Point:
          return QdrLightType::Point;
        case quader_editor_core::LightType::Spot:
          return QdrLightType::Spot;
        }
        return QdrLightType::Point;
}

quader_editor_core::LightType editor_light_type_from_qdr(QdrLightType type)
{
	switch (type) {
        case QdrLightType::Directional:
          return quader_editor_core::LightType::Directional;
        case QdrLightType::Point:
          return quader_editor_core::LightType::Point;
        case QdrLightType::Spot:
          return quader_editor_core::LightType::Spot;
        }
        return quader_editor_core::LightType::Point;
}

QdrLightShadowMode qdr_light_shadow_mode_from_editor(quader_editor_core::LightShadowMode mode)
{
	switch (mode) {
        case quader_editor_core::LightShadowMode::None:
          return QdrLightShadowMode::None;
        case quader_editor_core::LightShadowMode::Hard:
          return QdrLightShadowMode::Hard;
        case quader_editor_core::LightShadowMode::Soft:
          return QdrLightShadowMode::Soft;
        }
        return QdrLightShadowMode::Soft;
}

quader_editor_core::LightShadowMode editor_light_shadow_mode_from_qdr(QdrLightShadowMode mode)
{
	switch (mode) {
        case QdrLightShadowMode::None:
          return quader_editor_core::LightShadowMode::None;
        case QdrLightShadowMode::Hard:
          return quader_editor_core::LightShadowMode::Hard;
        case QdrLightShadowMode::Soft:
          return quader_editor_core::LightShadowMode::Soft;
        }
        return quader_editor_core::LightShadowMode::Soft;
}

} // namespace

QdrDocument document_from_session_state(const quader_editor_core::SessionState& state)
{
	QdrDocument document;
	document.next_object_id = state.next_object_id;
	document.next_light_id = state.next_light_id;
	document.active_object_id = state.active_object_id;
	document.active_light_id = state.active_light_id;
	document.selection_mode = qdr_selection_mode_from_editor(state.selection_mode);
	document.content_revision = state.content_revision;
	document.selection_revision = state.selection_revision;
	document.objects.reserve(state.objects.size());
	for (const quader_editor_core::PolygonObject& object : state.objects) {
		document.objects.push_back({
			object.id,
			object.material_id,
			object.selected,
			object.document,
			object.selection,
		});
	}
	document.lights.reserve(state.lights.size());
	for (const quader_editor_core::LightEntity& light : state.lights) {
		document.lights.push_back({
			light.id,
			light.name,
			light.enabled,
			qdr_light_type_from_editor(light.type),
			light.selected,
			light.position,
			light.direction,
			light.color_abgr,
			light.intensity,
			qdr_light_shadow_mode_from_editor(light.shadow_mode),
			light.shadow_strength,
			light.shadow_bias,
			light.shadow_normal_bias,
			light.shadow_near_plane,
			light.range,
			light.spot_angle,
			light.inner_spot_angle,
		});
	}
	return document;
}

quader_editor_core::SessionState session_state_from_document(const QdrDocument& document)
{
	quader_editor_core::SessionState state;
	state.next_object_id = document.next_object_id;
	state.next_light_id = document.next_light_id;
	state.active_object_id = document.active_object_id;
	state.active_light_id = document.active_light_id;
	state.selection_mode = editor_selection_mode_from_qdr(document.selection_mode);
	state.content_revision = document.content_revision;
	state.selection_revision = document.selection_revision;
	state.dirty = false;
	state.objects.reserve(document.objects.size());
	for (const QdrObject& object : document.objects) {
		state.objects.push_back({
			object.id,
			object.material_id,
			object.selected,
			object.document,
			object.selection,
		});
	}
	state.lights.reserve(document.lights.size());
	for (const QdrLight& light : document.lights) {
		state.lights.push_back({
			light.id,
			light.name,
			light.enabled,
			editor_light_type_from_qdr(light.type),
			light.selected,
			light.position,
			light.direction,
			light.color_abgr,
			light.intensity,
			editor_light_shadow_mode_from_qdr(light.shadow_mode),
			light.shadow_strength,
			light.shadow_bias,
			light.shadow_normal_bias,
			light.shadow_near_plane,
			light.range,
			light.spot_angle,
			light.inner_spot_angle,
		});
	}
	return state;
}

std::string serialize_session_state(const quader_editor_core::SessionState& state)
{
	return serialize_document(document_from_session_state(state));
}

ReadResult deserialize_session_state(std::string_view text)
{
	QdrReadResult result = deserialize_document(text);
	if (!result.ok) {
		return { false, {}, result.message };
	}
	return { true, session_state_from_document(result.document), {} };
}

QdrWriteResult write_session_state_atomic(
	const std::filesystem::path& path,
	const quader_editor_core::SessionState& state)
{
	return write_document_atomic(path, document_from_session_state(state));
}

} // namespace quader_io
