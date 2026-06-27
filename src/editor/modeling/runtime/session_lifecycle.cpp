////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <utility>

namespace quader_editor_core {

EditorSession::EditorSession(SessionState state) :
		state_(std::move(state))
{
	state_.next_object_id = next_polygon_object_id(state_.objects, state_.next_object_id);
	state_.next_light_id = next_light_entity_id(state_.lights, state_.next_light_id);
	normalize_active_object();
}

EditorSession::EditorSession(std::vector<PolygonObject> objects)
{
	state_.objects = std::move(objects);
	state_.next_object_id = next_polygon_object_id(state_.objects, state_.next_object_id);
	state_.next_light_id = next_light_entity_id(state_.lights, state_.next_light_id);
	normalize_active_object();
}

const SessionState& EditorSession::state() const
{
	return state_;
}

SessionState& EditorSession::state()
{
	return state_;
}

void EditorSession::set_state(SessionState state)
{
	state_ = std::move(state);
	state_.next_object_id = next_polygon_object_id(state_.objects, state_.next_object_id);
	state_.next_light_id = next_light_entity_id(state_.lights, state_.next_light_id);
	normalize_active_object();
}

void EditorSession::reset_to_default_scene()
{
	set_state(default_scene_state());
	clear_history();
}

const std::vector<PolygonObject>& EditorSession::objects() const
{
	return state_.objects;
}

std::vector<PolygonObject>& EditorSession::objects()
{
	return state_.objects;
}

void EditorSession::set_objects(std::vector<PolygonObject> objects)
{
	state_.objects = std::move(objects);
	state_.next_object_id = next_polygon_object_id(state_.objects, state_.next_object_id);
	normalize_active_object();
	state_.dirty = true;
	++state_.content_revision;
	++state_.selection_revision;
}

const std::vector<LightEntity>& EditorSession::lights() const
{
	return state_.lights;
}

std::vector<LightEntity>& EditorSession::lights()
{
	return state_.lights;
}

int EditorSession::next_object_id() const
{
	return state_.next_object_id;
}

int EditorSession::next_light_id() const
{
	return state_.next_light_id;
}

int EditorSession::active_object_id() const
{
	return state_.active_object_id;
}

int EditorSession::active_light_id() const
{
	return state_.active_light_id;
}

SelectionMode EditorSession::selection_mode() const
{
	return state_.selection_mode;
}

bool EditorSession::dirty() const
{
	return state_.dirty;
}

void EditorSession::mark_clean()
{
	state_.dirty = false;
}

} // namespace quader_editor_core
