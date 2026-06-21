#pragma once

#include <quader_poly/document.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

struct ConnectEdgeFacePath {
    std::vector<std::size_t> face_indices;
    std::vector<Edge> shared_edges;
};

struct ConnectEdgeFaceRegion {
    std::vector<std::size_t> face_indices;
    std::vector<Edge> boundary_selected_edges;
};

struct ConnectEdgePairPath {
    Edge first;
    Edge second;
    ConnectEdgeFacePath path;
};

struct OpenBridgeEdgeInfo {
    Edge edge;
    std::pair<ElementId, ElementId> oriented_face_edge;
    Vec3 face_normal;
    std::uint32_t material_slot = 0;
};

struct BridgeRailStepKey {
    ElementId first = invalid_element_id;
    ElementId second = invalid_element_id;
    int step = 0;

    friend bool operator<(const BridgeRailStepKey& left, const BridgeRailStepKey& right)
    {
        if (left.first != right.first) {
            return left.first < right.first;
        }
        if (left.second != right.second) {
            return left.second < right.second;
        }
        return left.step < right.step;
    }
};

struct FaceIslandBoundary {
    std::vector<ElementId> face_ids;
    std::vector<Edge> edges;
    std::vector<ElementId> vertices;
    Vec3 normal;
    std::uint32_t material_slot = 0;
    bool has_material_slot = false;
    bool all_open = false;
};

} // namespace quader_poly::document_internal
