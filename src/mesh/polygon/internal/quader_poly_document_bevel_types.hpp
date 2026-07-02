////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace quader_poly::document_internal {

/**
 * Represents an Edge Bevel Side value used by the polygon document and mesh editing core.
 */
struct EdgeBevelSide {
  ElementId face_id = kInvalidElementId;
  std::uint32_t material_slot = 0;
  quader::QVec3 normal;
  std::map<ElementId, ElementId> endpoint_vertices;
  std::map<ElementId, quader::QVec3> endpoint_positions;
};

/**
 * Represents an Edge Bevel Build value used by the polygon document and mesh editing core.
 */
struct EdgeBevelBuild {
    Edge edge;
    std::array<EdgeBevelSide, 2> sides;
    std::vector<std::array<ElementId, 2>> rows;
    bool concave = false;
};

/**
 * Represents an Edge Bevel Corner Arc value used by the polygon document and mesh editing core.
 */
struct EdgeBevelCornerArc {
    Edge edge;
    std::vector<ElementId> vertices;
    quader::QVec3 profile_middle;
    bool has_profile_middle = false;
    bool use_global_profile = true;
    float profile = 0.5F;
    BevelProfileType profile_type = BevelProfileType::Offset;
};

/**
 * Represents an Edge Bevel Face Vertex Offset value used by the polygon document and mesh editing core.
 */
struct EdgeBevelFaceVertexOffset {
  ElementId source_vertex_id = kInvalidElementId;
  ElementId offset_vertex_id = kInvalidElementId;
  ElementId source_face_id = kInvalidElementId;
  quader::QVec3 position;
};

/**
 * Represents an Edge Bevel Offset Line value used by the polygon document and mesh editing core.
 */
struct EdgeBevelOffsetLine {
    quader::QVec3 point;
    quader::QVec3 direction;
};

/**
 * Represents an Edge Bevel Profile Polyline Point value used by the polygon document and mesh editing core.
 */
struct EdgeBevelProfilePolylinePoint {
    double x = 0.0;
    double y = 0.0;
    double length = 0.0;
};

/**
 * Stores the Edge Bevel Tri Corner Key data contract used by the polygon document and mesh editing core.
 */
struct EdgeBevelTriCornerKey {
    int side = 0;
    int ring = 0;
    int segment = 0;

    friend bool operator<(const EdgeBevelTriCornerKey& left, const EdgeBevelTriCornerKey& right)
    {
        if (left.side != right.side) {
            return left.side < right.side;
        }
        if (left.ring != right.ring) {
            return left.ring < right.ring;
        }
        return left.segment < right.segment;
    }
};

/**
 * Stores the Edge Bevel V Mesh Key data contract used by the polygon document and mesh editing core.
 */
struct EdgeBevelVMeshKey {
    int side = 0;
    int ring = 0;
    int segment = 0;
};

/**
 * Represents an Edge Bevel V Mesh Profile value used by the polygon document and mesh editing core.
 */
struct EdgeBevelVMeshProfile {
    quader::QVec3 source;
    quader::QVec3 start;
    quader::QVec3 end;
    quader::QVec3 plane_co;
    quader::QVec3 plane_normal;
    quader::QVec3 projection_direction;
    bool use_global_profile = true;
    float profile = 0.5F;
    BevelProfileType profile_type = BevelProfileType::Offset;
    bool has_projection = false;
};

/**
 * Represents an Edge Bevel V Mesh Slot value used by the polygon document and mesh editing core.
 */
struct EdgeBevelVMeshSlot {
    quader::QVec3 position;
    ElementId vertex_id = kInvalidElementId;
};

/**
 * Represents an Edge Bevel V Mesh value used by the polygon document and mesh editing core.
 */
struct EdgeBevelVMesh {
    int side_count = 0;
    int segments = 0;
    std::vector<EdgeBevelVMeshProfile> profiles;
    std::vector<EdgeBevelVMeshSlot> slots;
};

} // namespace quader_poly::document_internal
