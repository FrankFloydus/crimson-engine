////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document.hpp>

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

/**
 * Represents an Edge Loop Face Split value used by the polygon document and mesh editing core.
 */
struct EdgeLoopFaceSplit {
  ElementId face_id = kInvalidElementId;
  Edge entry_edge;
};

/**
 * Represents a Resolved Knife Target value used by the polygon document and mesh editing core.
 */
struct ResolvedKnifeTarget {
  ElementId vertex_id = kInvalidElementId;
  quader::QVec3 position;
};

/**
 * Represents a Knife Segment Candidate value used by the polygon document and mesh editing core.
 */
struct KnifeSegmentCandidate {
    bool changed = false;
    Document document;
    ElementId previous_vertex = kInvalidElementId;
    ElementId current_vertex = kInvalidElementId;
    quader::QVec3 previous_position;
    quader::QVec3 current_position;
    std::string message;
};

/**
 * Represents a Knife Resolved Stroke Point value used by the polygon document and mesh editing core.
 */
struct KnifeResolvedStrokePoint {
  ElementId vertex_id = kInvalidElementId;
  ElementId face_id = kInvalidElementId;
  quader::QVec3 position;
};

/**
 * Represents a Knife Boundary Target value used by the polygon document and mesh editing core.
 */
struct KnifeBoundaryTarget {
  ElementId vertex_id = kInvalidElementId;
  Edge edge;
  float edge_factor = 0.5F;
};

/**
 * Represents a Knife Resolved Stroke Segment value used by the polygon document and mesh editing core.
 */
struct KnifeResolvedStrokeSegment {
    std::size_t first_point = 0;
    std::size_t second_point = 0;
    ElementId face_id = kInvalidElementId;
};

/**
 * Represents a Knife Face Graph value used by the polygon document and mesh editing core.
 */
struct KnifeFaceGraph {
  ElementId face_id = kInvalidElementId;
  std::vector<Edge> cut_edges;
};

/**
 * Represents a Knife Stroke Candidate value used by the polygon document and mesh editing core.
 */
struct KnifeStrokeCandidate {
    bool changed = false;
    Document document;
    std::vector<KnifeResolvedStrokePoint> points;
    std::vector<KnifeResolvedStrokeSegment> segments;
    std::vector<ElementId> selected_vertices;
    std::string message;
};

/**
 * Represents a Knife Edge Split value used by the polygon document and mesh editing core.
 */
struct KnifeEdgeSplit {
    float factor = 0.5F;
    ElementId vertex_id = kInvalidElementId;
};

using KnifeEdgeSplitMap = std::map<std::pair<ElementId, ElementId>, std::vector<KnifeEdgeSplit>>;
using KnifePoint2 = std::array<double, 2>;

} // namespace quader_poly::document_internal
