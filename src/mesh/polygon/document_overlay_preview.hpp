////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document_operations.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace quader_poly {

/**
 * Represents an Overlay Line value used by the polygon document and mesh editing core.
 */
struct OverlayLine {
    quader::QVec3 start;
    quader::QVec3 end;
    std::uint32_t color = 0xffffffff;
    float width_pixels = 1.0F;
    Edge edge;
    ElementId face_id = kInvalidElementId;
};

/**
 * Represents an Overlay Triangle value used by the polygon document and mesh editing core.
 */
struct OverlayTriangle {
    quader::QVec3 a;
    quader::QVec3 b;
    quader::QVec3 c;
    std::uint32_t color = 0xffffffff;
    ElementId face_id = kInvalidElementId;
};

/**
 * Represents an Overlay Point value used by the polygon document and mesh editing core.
 */
struct OverlayPoint {
    quader::QVec3 position;
    std::uint32_t color = 0xffffffff;
    float size_pixels = 5.0F;
    ElementId vertex_id = kInvalidElementId;
};

/**
 * Represents an Overlay Style value used by the polygon document and mesh editing core.
 */
struct OverlayStyle {
    std::uint32_t edge_color = 0xff2bd8ff;
    std::uint32_t selected_edge_color = 0xff2d37ff;
    std::uint32_t vertex_color = 0xff50e6ff;
    std::uint32_t selected_vertex_color = 0xff2a30ff;
    std::uint32_t face_border_color = 0xff2d37ff;
    std::uint32_t face_fill_color = 0x663d37ff;
    std::uint32_t loop_preview_color = 0xff4df1f5;
    float edge_width_pixels = 2.0F;
    float selected_edge_width_pixels = 3.0F;
    float face_border_width_pixels = 2.0F;
    float loop_preview_width_pixels = 3.0F;
    float vertex_size_pixels = 7.0F;
    float selected_vertex_size_pixels = 9.0F;
};

/**
 * Represents an Overlay Data value used by the polygon document and mesh editing core.
 */
struct OverlayData {
    std::vector<OverlayLine> lines;
    std::vector<OverlayTriangle> triangles;
    std::vector<OverlayPoint> points;
};

/**
 * Represents a Knife Segment Preview value used by the polygon document and mesh editing core.
 */
struct KnifeSegmentPreview {
    bool valid = false;
    quader::QVec3 start;
    quader::QVec3 end;
    OverlayData overlay;
    std::string message;
};

[[nodiscard]] OverlayData build_edge_loop_preview(
    const Document& document,
    Edge edge,
    float factor = 0.5F,
    const OverlayStyle& style = {});
[[nodiscard]] KnifeSegmentPreview build_knife_segment_preview(
    const Document& document,
    const KnifePointTarget& previous,
    const KnifePointTarget& current,
    const OverlayStyle& style = {});
[[nodiscard]] OverlayData build_overlay(
    const Document& document,
    const Selection& selection,
    const OverlayStyle& style = {});

} // namespace quader_poly
