#pragma once

#include <quader_editor_core/session.h>
#include <quader_editor_core/tool_constants.h>

#include <vector>

namespace quader_editor_core {

struct ScreenRect {
	double min_x = 0.0;
	double min_y = 0.0;
	double max_x = 0.0;
	double max_y = 0.0;
};

struct RectangleProjectedObject {
	int object_id = -1;
	bool has_screen_center = false;
	quader_poly::Vec2 screen_center;
	bool has_screen_bounds = false;
	quader_poly::Vec2 screen_min;
	quader_poly::Vec2 screen_max;
};

struct RectangleProjectedFace {
	int object_id = -1;
	int face_index = -1;
	std::vector<int> vertex_indices;
	bool has_screen_position = false;
	quader_poly::Vec2 screen_position;
};

struct RectangleProjectedEdge {
	int object_id = -1;
	int a_index = -1;
	int b_index = -1;
	bool has_screen_position = false;
	quader_poly::Vec2 screen_position;
};

struct RectangleProjectedVertex {
	int object_id = -1;
	int vertex_index = -1;
	bool has_screen_position = false;
	quader_poly::Vec2 screen_position;
};

struct RectangleProjectedHitsRequest {
	SelectionMode mode = SelectionMode::object;
	ScreenRect rect;
	std::vector<RectangleProjectedObject> objects;
	std::vector<RectangleProjectedFace> faces;
	std::vector<RectangleProjectedEdge> edges;
	std::vector<RectangleProjectedVertex> vertices;
};

struct SelectionBoxState {
	bool active = false;
	bool dragging = false;
	SelectionEditOperation operation = SelectionEditOperation::replace;
	quader_poly::Vec2 start;
	quader_poly::Vec2 end;
};

struct SelectionBoxCommit {
	bool active = false;
	SelectionEditOperation operation = SelectionEditOperation::replace;
	bool dragged = false;
	quader_poly::Vec2 start;
	quader_poly::Vec2 end;
};

struct SelectionBoxPrimitives {
	bool ok = false;
	bool dragging = false;
	std::vector<quader_poly::Vec2> polygon;
};

void begin_selection_box(SelectionBoxState& state, quader_poly::Vec2 position, SelectionEditOperation operation);
void update_selection_box(SelectionBoxState& state, quader_poly::Vec2 position, double drag_threshold = selection_box_default_drag_threshold);
SelectionBoxCommit finish_selection_box(SelectionBoxState& state, quader_poly::Vec2 position, SelectionEditOperation operation, double drag_threshold = selection_box_default_drag_threshold);
void clear_selection_box(SelectionBoxState& state);
ScreenRect normalized_screen_rect(quader_poly::Vec2 a, quader_poly::Vec2 b);
bool screen_rect_contains(const ScreenRect& rect, quader_poly::Vec2 point);
bool screen_rect_intersects_bounds(const ScreenRect& rect, quader_poly::Vec2 min_bounds, quader_poly::Vec2 max_bounds);
RectangleSelectionHits rectangle_hits_from_projected_candidates(const RectangleProjectedHitsRequest& request);
SelectionBoxPrimitives build_selection_box_primitives(const SelectionBoxState& state, bool camera_matches);

} // namespace quader_editor_core
