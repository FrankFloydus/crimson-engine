#pragma once

#include <quader_poly/document.h>

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace quader_poly::document_internal {

struct EdgeBevelSide {
    ElementId face_id = invalid_element_id;
    std::uint32_t material_slot = 0;
    Vec3 normal;
    std::map<ElementId, ElementId> endpoint_vertices;
    std::map<ElementId, Vec3> endpoint_positions;
};

struct EdgeBevelBuild {
    Edge edge;
    std::array<EdgeBevelSide, 2> sides;
    std::vector<std::array<ElementId, 2>> rows;
    bool concave = false;
};

struct EdgeBevelCornerArc {
    Edge edge;
    std::vector<ElementId> vertices;
    Vec3 profile_middle;
    bool has_profile_middle = false;
    bool use_global_profile = true;
    float profile = 0.5F;
    BevelProfileType profile_type = BevelProfileType::offset;
};

struct EdgeBevelFaceVertexOffset {
    ElementId source_vertex_id = invalid_element_id;
    ElementId offset_vertex_id = invalid_element_id;
    ElementId source_face_id = invalid_element_id;
    Vec3 position;
};

struct EdgeBevelOffsetLine {
    Vec3 point;
    Vec3 direction;
};

struct EdgeBevelProfilePolylinePoint {
    double x = 0.0;
    double y = 0.0;
    double length = 0.0;
};

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

struct EdgeBevelVMeshKey {
    int side = 0;
    int ring = 0;
    int segment = 0;
};

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

struct EdgeBevelVMeshSlot {
    Vec3 position;
    ElementId vertex_id = invalid_element_id;
};

struct EdgeBevelVMesh {
    int side_count = 0;
    int segments = 0;
    std::vector<EdgeBevelVMeshProfile> profiles;
    std::vector<EdgeBevelVMeshSlot> slots;
};

} // namespace quader_poly::document_internal
