////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only neutral modeling core contract shared by SDK and native owners.

#include <quader/modeling/neutral/polygon/document.hpp>

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
  FaceTextureTransform texture_transform{};
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

} // namespace quader_poly::document_internal
