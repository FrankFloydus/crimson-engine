////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "viewport/viewport_input_types.hpp"

#include <editor/modeling/runtime/session.hpp>
#include <editor/metadata/host_contract.hpp>

namespace quader {

inline ViewportSelectionMode appSelectionModeToViewport(ModelingSelectionMode mode) {
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

inline ModelingSelectionMode viewportSelectionModeToApp(ViewportSelectionMode mode) {
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

inline quader_editor_core::SelectionMode appSelectionModeToRuntime(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return quader_editor_core::SelectionMode::vertex;
    case ModelingSelectionMode::Edge:
        return quader_editor_core::SelectionMode::edge;
    case ModelingSelectionMode::Face:
        return quader_editor_core::SelectionMode::face;
    case ModelingSelectionMode::Object:
    case ModelingSelectionMode::Group:
        return quader_editor_core::SelectionMode::object;
    }
    return quader_editor_core::SelectionMode::object;
}

inline ModelingSelectionMode runtimeSelectionModeToApp(quader_editor_core::SelectionMode mode) {
    switch (mode) {
    case quader_editor_core::SelectionMode::vertex:
        return ModelingSelectionMode::Vertex;
    case quader_editor_core::SelectionMode::edge:
        return ModelingSelectionMode::Edge;
    case quader_editor_core::SelectionMode::face:
        return ModelingSelectionMode::Face;
    case quader_editor_core::SelectionMode::object:
        return ModelingSelectionMode::Object;
    }
    return ModelingSelectionMode::Object;
}

inline quader_host_contract::HostSelectionMode appSelectionModeToHostContract(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return quader_host_contract::HostSelectionMode::vertex;
    case ModelingSelectionMode::Edge:
        return quader_host_contract::HostSelectionMode::edge;
    case ModelingSelectionMode::Face:
        return quader_host_contract::HostSelectionMode::face;
    case ModelingSelectionMode::Object:
        return quader_host_contract::HostSelectionMode::object;
    case ModelingSelectionMode::Group:
        return quader_host_contract::HostSelectionMode::group;
    }
    return quader_host_contract::HostSelectionMode::object;
}

} // namespace quader
