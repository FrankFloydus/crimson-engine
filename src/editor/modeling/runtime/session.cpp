////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace quader_editor_core {
namespace {

int first_selected_index(const std::vector<PolygonObject> &objects) {
  for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
    if (objects[index].selected) {
      return index;
    }
  }
  return -1;
}

} // namespace

CommandResult ok_result(bool changed) { return {true, changed, {}}; }

CommandResult error_result(std::string message) {
  return {false, false, std::move(message)};
}

std::uint32_t saturated_history_size(std::size_t size) {
  return static_cast<std::uint32_t>(
      std::min<std::size_t>(size, std::numeric_limits<std::uint32_t>::max()));
}

SelectionMode sanitize_selection_mode(int mode) {
  return static_cast<SelectionMode>(
      std::clamp(mode, int(SelectionMode::Vertex), int(SelectionMode::Object)));
}

SelectionEditOperation sanitize_selection_operation(int operation) {
  if (operation == int(SelectionEditOperation::Add) ||
      operation == int(SelectionEditOperation::Remove) ||
      operation == int(SelectionEditOperation::Toggle)) {
    return static_cast<SelectionEditOperation>(operation);
  }
  return SelectionEditOperation::Replace;
}

quader_poly::SelectionMode component_selection_mode(SelectionMode mode) {
  switch (mode) {
  case SelectionMode::Edge:
    return quader_poly::SelectionMode::Edge;
  case SelectionMode::Face:
    return quader_poly::SelectionMode::Face;
  case SelectionMode::Vertex:
  case SelectionMode::Object:
  default:
    return quader_poly::SelectionMode::Vertex;
  }
}

bool component_selection_has_mode_components(
    const quader_poly::Selection &selection, SelectionMode mode) {
  switch (mode) {
  case SelectionMode::Vertex:
    return !selection.vertices.empty();
  case SelectionMode::Edge:
    return !selection.edges.empty();
  case SelectionMode::Face:
    return !selection.faces.empty();
  case SelectionMode::Object:
    return false;
  }
  return false;
}

bool component_selection_has_any_components(
    const quader_poly::Selection &selection) {
  return !selection.vertices.empty() || !selection.edges.empty() ||
         !selection.faces.empty();
}

bool polygon_selection_matches(const quader_poly::Selection &left,
                               const quader_poly::Selection &right) {
  return left.mode == right.mode && left.vertices == right.vertices &&
         left.edges == right.edges && left.faces == right.faces &&
         left.has_active == right.has_active &&
         left.active_kind == right.active_kind &&
         left.active_vertex == right.active_vertex &&
         left.active_edge == right.active_edge &&
         left.active_face == right.active_face;
}

int selected_object_index(const std::vector<PolygonObject> &objects,
                          int active_object_id) {
  if (active_object_id > 0) {
    for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
      if (objects[index].id == active_object_id && objects[index].selected) {
        return index;
      }
    }
  }
  return first_selected_index(objects);
}

int object_index_by_id(const std::vector<PolygonObject> &objects,
                       int object_id) {
  for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
    if (objects[index].id == object_id) {
      return index;
    }
  }
  return -1;
}

int selected_object_id(const std::vector<PolygonObject> &objects,
                       int active_object_id) {
  const int index = selected_object_index(objects, active_object_id);
  return index >= 0 ? objects[index].id : 0;
}

std::vector<int>
selected_object_ids(const std::vector<PolygonObject> &objects) {
  std::vector<int> ids;
  for (const PolygonObject &object : objects) {
    if (object.selected && object.id > 0) {
      ids.push_back(object.id);
    }
  }
  return ids;
}

void normalize_active_object_id(const std::vector<PolygonObject> &objects,
                                int &active_object_id) {
  if (selected_object_index(objects, active_object_id) >= 0) {
    return;
  }
  active_object_id = selected_object_id(objects, -1);
  if (active_object_id <= 0) {
    active_object_id = -1;
  }
}

void set_selection_mode(std::vector<PolygonObject> &objects, SelectionMode mode,
                        int &active_object_id, SelectionMode previous_mode) {
  for (PolygonObject &object : objects) {
    if (mode == SelectionMode::Object) {
      object.selection = quader_poly::Selection();
      object.component_edit_target = false;
      continue;
    }
    const bool keep_component_edit_target =
        object.selected &&
        (component_selection_has_any_components(object.selection) ||
         (previous_mode != SelectionMode::Object &&
          object.component_edit_target));
    quader_poly::convert_selection_mode(object.document, object.selection,
                                        component_selection_mode(mode));
    object.component_edit_target = keep_component_edit_target;
  }
  normalize_active_object_id(objects, active_object_id);
}

void clear_selection(std::vector<PolygonObject> &objects,
                     int &active_object_id) {
  for (PolygonObject &object : objects) {
    object.selected = false;
    object.component_edit_target = false;
    object.selection = quader_poly::Selection();
  }
  active_object_id = -1;
}

bool has_selection(const std::vector<PolygonObject> &objects,
                   SelectionMode mode, int active_object_id) {
  const int index = selected_object_index(objects, active_object_id);
  if (index < 0) {
    return false;
  }
  return mode == SelectionMode::Object || !objects[index].selection.empty();
}

std::optional<quader::QVec3>
selection_center(const std::vector<PolygonObject> &objects, SelectionMode mode,
                 int active_object_id) {
  (void)active_object_id;
  const Bounds3 bounds = selection_bounds(objects, mode);
  if (bounds.empty) {
    return std::nullopt;
  }
  return (bounds.min + bounds.max) * 0.5F;
}

ActiveSelection active_selection(const std::vector<PolygonObject> &objects,
                                 SelectionMode mode, int active_object_id) {
  ActiveSelection active;
  active.selection_mode = mode;
  const int index = selected_object_index(objects, active_object_id);
  if (index < 0) {
    return active;
  }
  const PolygonObject &object = objects[index];
  if (object.id <= 0 || !object.selected) {
    return active;
  }
  active.has_active = true;
  active.object_id = object.id;
  if (mode == SelectionMode::Object) {
    active.kind = ActiveSelectionKind::Object;
    return active;
  }
  if (object.selection.empty() || !object.selection.has_active) {
    active.has_active = false;
    return active;
  }
  if (object.selection.active_kind == quader_poly::ElementKind::Vertex) {
    active.kind = ActiveSelectionKind::Vertex;
    active.vertex_id = object.selection.active_vertex;
  } else if (object.selection.active_kind == quader_poly::ElementKind::Edge) {
    active.kind = ActiveSelectionKind::Edge;
    active.edge = object.selection.active_edge;
  } else if (object.selection.active_kind == quader_poly::ElementKind::Face) {
    active.kind = ActiveSelectionKind::Face;
    active.face_id = object.selection.active_face;
  }
  return active;
}

} // namespace quader_editor_core
