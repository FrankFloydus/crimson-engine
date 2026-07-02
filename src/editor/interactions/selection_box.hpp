////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.hpp>
#include <editor/modeling/runtime/tool_constants.hpp>

#include <vector>

namespace quader_editor_core {

/**
 * Represents a Screen Rect value used by the modal modeling interaction layer.
 */
struct ScreenRect {
	double min_x = 0.0;
	double min_y = 0.0;
	double max_x = 0.0;
	double max_y = 0.0;
};

/**
 * Enumerates rectangle selection hit matching rules used by projected candidates.
 */
enum class RectangleSelectionMatch {
  Contain,
  Crossing,
};

/**
 * Represents a projected screen-space segment used by rectangle selection candidates.
 */
struct RectangleProjectedSegment {
	quader::QVec2 start;
	quader::QVec2 end;
};

/**
 * Represents a Rectangle Projected Object value used by the modal modeling interaction layer.
 */
struct RectangleProjectedObject {
	int object_id = -1;
	bool has_screen_center = false;
	quader::QVec2 screen_center;
	bool has_screen_bounds = false;
	quader::QVec2 screen_min;
	quader::QVec2 screen_max;
	std::vector<quader::QVec2> screen_points;
	std::vector<RectangleProjectedSegment> screen_segments;
	std::vector<std::vector<quader::QVec2>> screen_polygons;
};

/**
 * Represents a Rectangle Projected Face value used by the modal modeling interaction layer.
 */
struct RectangleProjectedFace {
	int object_id = -1;
	int face_index = -1;
	std::vector<int> vertex_indices;
	bool has_screen_position = false;
	quader::QVec2 screen_position;
	std::vector<quader::QVec2> screen_vertices;
};

/**
 * Represents a Rectangle Projected Edge value used by the modal modeling interaction layer.
 */
struct RectangleProjectedEdge {
	int object_id = -1;
	int a_index = -1;
	int b_index = -1;
	bool has_screen_position = false;
	quader::QVec2 screen_position;
	bool has_screen_start = false;
	quader::QVec2 screen_start;
	bool has_screen_end = false;
	quader::QVec2 screen_end;
};

/**
 * Represents a Rectangle Projected Vertex value used by the modal modeling interaction layer.
 */
struct RectangleProjectedVertex {
	int object_id = -1;
	int vertex_index = -1;
	bool has_screen_position = false;
	quader::QVec2 screen_position;
};

/**
 * Stores the Rectangle Projected Hits Request data contract used by the modal modeling interaction layer.
 */
struct RectangleProjectedHitsRequest {
  SelectionMode mode = SelectionMode::Object;
  RectangleSelectionMatch match = RectangleSelectionMatch::Crossing;
  ScreenRect rect;
  std::vector<RectangleProjectedObject> objects;
  std::vector<RectangleProjectedFace> faces;
  std::vector<RectangleProjectedEdge> edges;
  std::vector<RectangleProjectedVertex> vertices;
};

/**
 * Stores the Selection Box State data contract used by the modal modeling interaction layer.
 */
struct SelectionBoxState {
	bool active = false;
	bool dragging = false;
        SelectionEditOperation operation = SelectionEditOperation::Replace;
        quader::QVec2 start;
	quader::QVec2 end;
};

/**
 * Represents a Selection Box Commit value used by the modal modeling interaction layer.
 */
struct SelectionBoxCommit {
	bool active = false;
        SelectionEditOperation operation = SelectionEditOperation::Replace;
        bool dragged = false;
	quader::QVec2 start;
	quader::QVec2 end;
};

/**
 * Represents a Selection Box Primitives value used by the modal modeling interaction layer.
 */
struct SelectionBoxPrimitives {
	bool ok = false;
	bool dragging = false;
	std::vector<quader::QVec2> polygon;
};

void begin_selection_box(SelectionBoxState& state, quader::QVec2 position, SelectionEditOperation operation);
void update_selection_box(
    SelectionBoxState &state, quader::QVec2 position,
    double drag_threshold = kSelectionBoxDefaultDragThreshold);
SelectionBoxCommit
finish_selection_box(SelectionBoxState &state, quader::QVec2 position,
                     SelectionEditOperation operation,
                     double drag_threshold = kSelectionBoxDefaultDragThreshold);
void clear_selection_box(SelectionBoxState& state);
ScreenRect normalized_screen_rect(quader::QVec2 a, quader::QVec2 b);
bool screen_rect_contains(const ScreenRect& rect, quader::QVec2 point);
bool screen_rect_intersects_bounds(const ScreenRect& rect, quader::QVec2 min_bounds, quader::QVec2 max_bounds);
RectangleSelectionHits rectangle_hits_from_projected_candidates(const RectangleProjectedHitsRequest& request);
SelectionBoxPrimitives build_selection_box_primitives(const SelectionBoxState& state, bool camera_matches);

} // namespace quader_editor_core
