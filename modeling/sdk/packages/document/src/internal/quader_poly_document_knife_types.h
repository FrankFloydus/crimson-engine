#pragma once

#include <quader_poly/document.h>

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

struct EdgeLoopFaceSplit {
    ElementId face_id = invalid_element_id;
    Edge entry_edge;
};

struct ResolvedKnifeTarget {
    ElementId vertex_id = invalid_element_id;
    Vec3 position;
};

struct KnifeSegmentCandidate {
    bool changed = false;
    Document document;
    ElementId previous_vertex = invalid_element_id;
    ElementId current_vertex = invalid_element_id;
    Vec3 previous_position;
    Vec3 current_position;
    std::string message;
};

struct KnifeResolvedStrokePoint {
    ElementId vertex_id = invalid_element_id;
    ElementId face_id = invalid_element_id;
    Vec3 position;
};

struct KnifeBoundaryTarget {
    ElementId vertex_id = invalid_element_id;
    Edge edge;
    float edge_factor = 0.5F;
};

struct KnifeResolvedStrokeSegment {
    std::size_t first_point = 0;
    std::size_t second_point = 0;
    ElementId face_id = invalid_element_id;
};

struct KnifeFaceGraph {
    ElementId face_id = invalid_element_id;
    std::vector<Edge> cut_edges;
};

struct KnifeStrokeCandidate {
    bool changed = false;
    Document document;
    std::vector<KnifeResolvedStrokePoint> points;
    std::vector<KnifeResolvedStrokeSegment> segments;
    std::vector<ElementId> selected_vertices;
    std::string message;
};

struct KnifeEdgeSplit {
    float factor = 0.5F;
    ElementId vertex_id = invalid_element_id;
};

using KnifeEdgeSplitMap = std::map<std::pair<ElementId, ElementId>, std::vector<KnifeEdgeSplit>>;
using KnifePoint2 = std::array<double, 2>;

} // namespace quader_poly::document_internal
