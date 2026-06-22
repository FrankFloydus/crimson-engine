////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document.h>

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace quader_poly::document_internal {

/**
 * Represents an Edge Bevel Side value used by the polygon document and mesh editing core.
 */
struct EdgeBevelSide {
    ElementId face_id = invalid_element_id;
    std::uint32_t material_slot = 0;
    Vec3 normal;
    std::map<ElementId, ElementId> endpoint_vertices;
    std::map<ElementId, Vec3> endpoint_positions;
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
    Vec3 profile_middle;
    bool has_profile_middle = false;
    bool use_global_profile = true;
    float profile = 0.5F;
    BevelProfileType profile_type = BevelProfileType::offset;
};

/**
 * Represents an Edge Bevel Face Vertex Offset value used by the polygon document and mesh editing core.
 */
struct EdgeBevelFaceVertexOffset {
    ElementId source_vertex_id = invalid_element_id;
    ElementId offset_vertex_id = invalid_element_id;
    ElementId source_face_id = invalid_element_id;
    Vec3 position;
};

/**
 * Represents an Edge Bevel Offset Line value used by the polygon document and mesh editing core.
 */
struct EdgeBevelOffsetLine {
    Vec3 point;
    Vec3 direction;
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
    Vec3 source;
    Vec3 start;
    Vec3 end;
    Vec3 plane_co;
    Vec3 plane_normal;
    Vec3 projection_direction;
    bool use_global_profile = true;
    float profile = 0.5F;
    BevelProfileType profile_type = BevelProfileType::offset;
    bool has_projection = false;
};

/**
 * Represents an Edge Bevel V Mesh Slot value used by the polygon document and mesh editing core.
 */
struct EdgeBevelVMeshSlot {
    Vec3 position;
    ElementId vertex_id = invalid_element_id;
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
