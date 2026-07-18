////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <diagnostics/profile.hpp>
#include <editor/interactions/interaction_api.hpp>

#include <algorithm>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace quader_editor_core {
namespace {

/**
 * Stores the Object Selection Snapshot data contract used by the modeling
 * runtime domain.
 */
struct ObjectSelectionSnapshot {
  int id = -1;
  bool selected = false;
  bool component_edit_target = false;
  quader_poly::Selection selection;
};

/**
 * Stores the Selection Snapshot data contract used by the modeling runtime
 * domain.
 */
struct SelectionSnapshot {
  SelectionMode mode = SelectionMode::Object;
  int active_object_id = -1;
  std::vector<ObjectSelectionSnapshot> objects;
};

SelectionSnapshot
capture_selection_snapshot(const std::vector<PolygonObject> &objects,
                           SelectionMode mode, int active_object_id) {
  SelectionSnapshot snapshot;
  snapshot.mode = mode;
  snapshot.active_object_id = active_object_id;
  snapshot.objects.reserve(objects.size());
  for (const PolygonObject &object : objects) {
    snapshot.objects.push_back({object.id, object.selected,
                                object.component_edit_target,
                                object.selection});
  }
  return snapshot;
}

bool selection_snapshot_matches(const SelectionSnapshot &left,
                                const SelectionSnapshot &right) {
  if (left.mode != right.mode ||
      left.active_object_id != right.active_object_id ||
      left.objects.size() != right.objects.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.objects.size(); ++index) {
    const ObjectSelectionSnapshot &left_object = left.objects[index];
    const ObjectSelectionSnapshot &right_object = right.objects[index];
    if (left_object.id != right_object.id ||
        left_object.selected != right_object.selected ||
        left_object.component_edit_target !=
            right_object.component_edit_target ||
        !polygon_selection_matches(left_object.selection,
                                   right_object.selection)) {
      return false;
    }
  }
  return true;
}

bool selection_changed_since(const SelectionSnapshot &before,
                             const std::vector<PolygonObject> &objects,
                             SelectionMode mode, int active_object_id) {
  return !selection_snapshot_matches(
      before, capture_selection_snapshot(objects, mode, active_object_id));
}

} // namespace

void EditorSession::set_selection_mode(SelectionMode mode) {
  if (state_.selection_mode == mode) {
    return;
  }
  const SelectionMode previous_mode = state_.selection_mode;
  state_.selection_mode = mode;
  quader_editor_core::set_selection_mode(state_.objects, state_.selection_mode,
                                         state_.active_object_id,
                                         previous_mode);
  note_selection_changed();
}

void EditorSession::clear_selection() {
  if (!quader_editor_core::has_selection(
          state_.objects, state_.selection_mode, state_.active_object_id) &&
      state_.active_object_id <= 0) {
    return;
  }
  quader_editor_core::clear_selection(state_.objects, state_.active_object_id);
  note_selection_changed();
}

void EditorSession::set_selected_object_ids(const std::vector<int> &object_ids,
                                            int active_id) {
  state_.selection_mode = SelectionMode::Object;
  for (PolygonObject &object : state_.objects) {
    object.selection = quader_poly::Selection();
    object.component_edit_target = false;
    object.selected = std::find(object_ids.begin(), object_ids.end(),
                                object.id) != object_ids.end();
  }
  state_.active_object_id =
      active_id > 0 ? active_id : (object_ids.empty() ? -1 : object_ids.back());
  normalize_active_object();
  note_selection_changed();
}

void EditorSession::set_component_selection(int object_id,
                                            quader_poly::Selection selection,
                                            SelectionMode mode) {
  state_.selection_mode =
      mode == SelectionMode::Object ? SelectionMode::Vertex : mode;
  for (PolygonObject &object : state_.objects) {
    if (object.id != object_id) {
      object.selected = false;
      object.component_edit_target = false;
      object.selection = quader_poly::Selection();
      continue;
    }
    object.selection = std::move(selection);
    object.selection.mode = component_selection_mode(state_.selection_mode);
    object.selected = !object.selection.empty();
    object.component_edit_target = object.selected;
    state_.active_object_id = object.selected ? object.id : -1;
  }
  normalize_active_object();
  note_selection_changed();
}

bool EditorSession::has_selection() const {
  return quader_editor_core::has_selection(
      state_.objects, state_.selection_mode, state_.active_object_id);
}

std::optional<quader::QVec3> EditorSession::selection_center() const {
  return quader_editor_core::selection_center(
      state_.objects, state_.selection_mode, state_.active_object_id);
}

Bounds3 EditorSession::document_bounds() const {
  return quader_editor_core::document_bounds(state_.objects);
}

Bounds3 EditorSession::selection_bounds() const {
  return quader_editor_core::selection_bounds(state_.objects,
                                               state_.selection_mode);
}

ActiveSelection EditorSession::active_selection() const {
  return quader_editor_core::active_selection(
      state_.objects, state_.selection_mode, state_.active_object_id);
}

std::vector<int> EditorSession::selected_object_ids() const {
  return quader_editor_core::selected_object_ids(state_.objects);
}

std::optional<PickHitRecord>
EditorSession::pick_nearest(const PickRequest &request) const {
  return quader_editor_core::pick_nearest(state_.objects, request);
}

std::vector<PickHitRecord>
EditorSession::pick_all(const PickRequest &request) const {
  return quader_editor_core::pick_all(state_.objects, request);
}

std::string
EditorSession::pick_hits_signature(const PickRequest &request) const {
  return quader_editor_core::pick_hits_signature(pick_all(request));
}

bool EditorSession::pick_select(SelectionEditOperation operation,
                                const quader_poly::Ray &ray,
                                quader_poly::PickOptions pick_options,
                                bool allow_component_fallback) {
  const SelectionSnapshot before = capture_selection_snapshot(
      state_.objects, state_.selection_mode, state_.active_object_id);
  quader_editor_core::pick_select(state_.objects, state_.selection_mode,
                                  operation, ray, state_.active_object_id,
                                  pick_options, allow_component_fallback);
  const bool changed = selection_changed_since(
      before, state_.objects, state_.selection_mode, state_.active_object_id);
  if (changed) {
    note_selection_changed();
  }
  return changed;
}

bool EditorSession::pick_select_face(SelectionEditOperation operation,
                                     const quader_poly::Ray &ray,
                                     quader_poly::PickOptions pick_options) {
  const SelectionSnapshot before = capture_selection_snapshot(
      state_.objects, state_.selection_mode, state_.active_object_id);
  quader_editor_core::pick_select_face(state_.objects, state_.selection_mode,
                                       operation, ray, state_.active_object_id,
                                       pick_options);
  const bool changed = selection_changed_since(
      before, state_.objects, state_.selection_mode, state_.active_object_id);
  if (changed) {
    note_selection_changed();
  }
  return changed;
}

bool EditorSession::pick_select_all_faces(
    SelectionEditOperation operation, const quader_poly::Ray &ray,
    quader_poly::PickOptions pick_options) {
  const SelectionSnapshot before = capture_selection_snapshot(
      state_.objects, state_.selection_mode, state_.active_object_id);
  quader_editor_core::pick_select_all_faces(
      state_.objects, state_.selection_mode, operation, ray,
      state_.active_object_id, pick_options);
  const bool changed = selection_changed_since(
      before, state_.objects, state_.selection_mode, state_.active_object_id);
  if (changed) {
    note_selection_changed();
  }
  return changed;
}

bool EditorSession::pick_select_edge_loop(
    SelectionEditOperation operation, const quader_poly::Ray &ray,
    quader_poly::PickOptions pick_options) {
  const SelectionSnapshot before = capture_selection_snapshot(
      state_.objects, state_.selection_mode, state_.active_object_id);
  quader_editor_core::pick_select_edge_loop(
      state_.objects, state_.selection_mode, operation, ray,
      state_.active_object_id, pick_options);
  const bool changed = selection_changed_since(
      before, state_.objects, state_.selection_mode, state_.active_object_id);
  if (changed) {
    note_selection_changed();
  }
  return changed;
}

bool EditorSession::pick_select_all_vertices(
    SelectionEditOperation operation, const quader_poly::Ray &ray,
    quader_poly::PickOptions pick_options) {
  const SelectionSnapshot before = capture_selection_snapshot(
      state_.objects, state_.selection_mode, state_.active_object_id);
  quader_editor_core::pick_select_all_vertices(
      state_.objects, state_.selection_mode, operation, ray,
      state_.active_object_id, pick_options);
  const bool changed = selection_changed_since(
      before, state_.objects, state_.selection_mode, state_.active_object_id);
  if (changed) {
    note_selection_changed();
  }
  return changed;
}

} // namespace quader_editor_core
