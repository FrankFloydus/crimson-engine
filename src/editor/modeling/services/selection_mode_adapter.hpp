////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "viewport/viewport_input_types.hpp"

#include <editor/modeling/runtime/selection_types.hpp>
#include <editor/metadata/host_contract.hpp>

#include <optional>
#include <string_view>

namespace quader {

inline ViewportSelectionMode
app_selection_mode_to_viewport(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return ViewportSelectionMode::Vertex;
  case ModelingSelectionMode::Edge:
    return ViewportSelectionMode::Edge;
  case ModelingSelectionMode::Face:
    return ViewportSelectionMode::Face;
  case ModelingSelectionMode::Object:
    return ViewportSelectionMode::Object;
  case ModelingSelectionMode::Group:
    return ViewportSelectionMode::Group;
  }
  return ViewportSelectionMode::Object;
}

inline bool is_component_selection_mode(ModelingSelectionMode mode) {
  return mode == ModelingSelectionMode::Vertex ||
         mode == ModelingSelectionMode::Edge ||
         mode == ModelingSelectionMode::Face;
}

inline bool selection_mode_uses_scene_document(ModelingSelectionMode mode) {
  return mode == ModelingSelectionMode::Object ||
         mode == ModelingSelectionMode::Group;
}

inline ModelingSelectionMode
viewport_selection_mode_to_app(ViewportSelectionMode mode) {
  switch (mode) {
  case ViewportSelectionMode::Vertex:
    return ModelingSelectionMode::Vertex;
  case ViewportSelectionMode::Edge:
    return ModelingSelectionMode::Edge;
  case ViewportSelectionMode::Face:
    return ModelingSelectionMode::Face;
  case ViewportSelectionMode::Object:
    return ModelingSelectionMode::Object;
  case ViewportSelectionMode::Group:
    return ModelingSelectionMode::Group;
  }
  return ModelingSelectionMode::Object;
}

inline quader_editor_core::SelectionMode
app_selection_mode_to_runtime(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return quader_editor_core::SelectionMode::Vertex;
  case ModelingSelectionMode::Edge:
    return quader_editor_core::SelectionMode::Edge;
  case ModelingSelectionMode::Face:
    return quader_editor_core::SelectionMode::Face;
  case ModelingSelectionMode::Object:
  case ModelingSelectionMode::Group:
    return quader_editor_core::SelectionMode::Object;
  }
  return quader_editor_core::SelectionMode::Object;
}

inline ModelingSelectionMode
runtime_selection_mode_to_app(quader_editor_core::SelectionMode mode) {
  switch (mode) {
  case quader_editor_core::SelectionMode::Vertex:
    return ModelingSelectionMode::Vertex;
  case quader_editor_core::SelectionMode::Edge:
    return ModelingSelectionMode::Edge;
  case quader_editor_core::SelectionMode::Face:
    return ModelingSelectionMode::Face;
  case quader_editor_core::SelectionMode::Object:
    return ModelingSelectionMode::Object;
  }
  return ModelingSelectionMode::Object;
}

inline quader_host_contract::HostSelectionMode
app_selection_mode_to_host_contract(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return quader_host_contract::HostSelectionMode::Vertex;
  case ModelingSelectionMode::Edge:
    return quader_host_contract::HostSelectionMode::Edge;
  case ModelingSelectionMode::Face:
    return quader_host_contract::HostSelectionMode::Face;
  case ModelingSelectionMode::Object:
    return quader_host_contract::HostSelectionMode::Object;
  case ModelingSelectionMode::Group:
    return quader_host_contract::HostSelectionMode::Group;
  }
  return quader_host_contract::HostSelectionMode::Object;
}

inline ModelingSelectionMode
host_selection_mode_to_app(quader_host_contract::HostSelectionMode mode) {
  switch (mode) {
  case quader_host_contract::HostSelectionMode::Vertex:
    return ModelingSelectionMode::Vertex;
  case quader_host_contract::HostSelectionMode::Edge:
    return ModelingSelectionMode::Edge;
  case quader_host_contract::HostSelectionMode::Face:
    return ModelingSelectionMode::Face;
  case quader_host_contract::HostSelectionMode::Object:
    return ModelingSelectionMode::Object;
  case quader_host_contract::HostSelectionMode::Group:
    return ModelingSelectionMode::Group;
  }
  return ModelingSelectionMode::Object;
}

inline ViewportSelectionMode
host_selection_mode_to_viewport(quader_host_contract::HostSelectionMode mode) {
  return app_selection_mode_to_viewport(host_selection_mode_to_app(mode));
}

inline std::string_view
host_selection_mode_id(quader_host_contract::HostSelectionMode mode) {
  for (const quader_host_contract::HostSelectionModeDescriptor &descriptor :
       quader_host_contract::selection_mode_catalog()) {
    if (descriptor.mode == mode) {
      return descriptor.id;
    }
  }
  return {};
}

inline std::optional<quader_host_contract::HostSelectionMode>
host_selection_mode_from_id(std::string_view id) {
  if (const quader_host_contract::HostSelectionModeDescriptor *descriptor =
          quader_host_contract::find_selection_mode_descriptor(id)) {
    return descriptor->mode;
  }
  return std::nullopt;
}

} // namespace quader
