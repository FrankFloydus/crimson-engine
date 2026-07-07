////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"

#include <optional>
#include <string_view>

namespace quader {

inline constexpr std::string_view kPivotToolId = "pivot";
inline constexpr std::string_view kPivotSnapVertexSetting =
    "pivot.snap.vertex";
inline constexpr std::string_view kPivotSnapEdgeMidpointSetting =
    "pivot.snap.edge_midpoint";
inline constexpr std::string_view kPivotSnapFaceCenterSetting =
    "pivot.snap.face_center";
inline constexpr std::string_view kPivotSnapGridSetting = "pivot.snap.grid";

/**
 * Enumerates pivot snap candidate classes used by persistent and quick pivot
 * placement.
 */
enum class PivotSnapKind {
    None,
    Vertex,
    EdgeMidpoint,
    FaceCenter,
    Grid,
};

/**
 * Stores pivot snap toggles shared by the pivot service and viewport snapping.
 */
struct PivotSnapSettings {
    bool vertex = true;
    bool edge_midpoint = true;
    bool face_center = true;
    bool grid = true;
};

/**
 * Stores the current visible pivot handle state for viewport rendering and hit
 * testing.
 */
struct PivotHandleState {
    bool visible = false;
    bool editing = false;
    bool persistent = false;
    EntityId target = kInvalidEntityId;
    QVec3 world_position = {};
};

/**
 * Stores the temporary selection pivot override consumed by transform gizmos.
 */
struct SelectionPivotOverride {
    bool valid = false;
    QVec3 world_position = {};
};

} // namespace quader
