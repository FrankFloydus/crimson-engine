#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader_poly {

using ElementId = std::uint32_t;

inline constexpr ElementId invalid_element_id = 0;

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Transform3 {
    Vec3 x_axis { 1.0F, 0.0F, 0.0F };
    Vec3 y_axis { 0.0F, 1.0F, 0.0F };
    Vec3 z_axis { 0.0F, 0.0F, 1.0F };
    Vec3 origin { 0.0F, 0.0F, 0.0F };
};

struct Ray {
    Vec3 origin;
    Vec3 direction { 0.0F, 0.0F, 1.0F };
};

struct Vertex {
    ElementId id = invalid_element_id;
    Vec3 position;
};

struct Edge {
    ElementId a = invalid_element_id;
    ElementId b = invalid_element_id;

    friend bool operator==(const Edge& left, const Edge& right) = default;
};

enum class SurfaceShading {
    flat,
    smooth,
    authored,
};

struct Face {
    ElementId id = invalid_element_id;
    std::vector<ElementId> vertices;
    std::vector<Vec2> uvs;
    std::uint32_t material_slot = 0;
    SurfaceShading normal_shading = SurfaceShading::flat;
};

struct FacePerimeterInfo {
    std::vector<Edge> edges;
    std::vector<Edge> open_edges;
    std::vector<Edge> closed_edges;
    std::vector<Edge> nonmanifold_edges;

    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool has_open_edges() const;
    [[nodiscard]] bool has_closed_edges() const;
    [[nodiscard]] bool has_nonmanifold_edges() const;
    [[nodiscard]] bool has_only_open_edges() const;
    [[nodiscard]] bool has_only_closed_edges() const;
};

struct Document {
    std::vector<Vertex> vertices;
    std::vector<Face> faces;
    std::vector<Edge> hard_normal_edges;
    ElementId next_vertex_id = 1;
    ElementId next_face_id = 1;
};

enum class PrimitiveKind {
    cube,
    plane,
    cylinder,
    cone,
    quad_sphere,
    uv_sphere,
    ico_sphere,
    torus,
};

struct CubePrimitiveSpec {
    float width = 2.0F;
    float height = 2.0F;
    float depth = 2.0F;
    int width_segments = 1;
    int height_segments = 1;
    int depth_segments = 1;
};

struct PlanePrimitiveSpec {
    float width = 2.0F;
    float depth = 2.0F;
    int width_segments = 1;
    int depth_segments = 1;
};

struct CylinderPrimitiveSpec {
    float radius = 1.0F;
    float height = 2.0F;
    int sides = 16;
    bool cap_top = true;
    bool cap_bottom = true;
};

struct ConePrimitiveSpec {
    float radius = 1.0F;
    float height = 2.0F;
    int sides = 16;
    bool cap_bottom = true;
};

struct QuadSpherePrimitiveSpec {
    float radius = 1.0F;
    int segments_per_face = 4;
};

struct UvSpherePrimitiveSpec {
    float radius = 1.0F;
    int segments = 16;
    int rings = 8;
};

struct IcoSpherePrimitiveSpec {
    float radius = 1.0F;
    int subdivisions = 2;
};

struct TorusPrimitiveSpec {
    float major_radius = 0.7F;
    float minor_radius = 0.3F;
    int major_segments = 16;
    int minor_segments = 8;
};

enum class ElementKind {
    vertex,
    edge,
    face,
};

enum class SelectionMode {
    vertex,
    edge,
    face,
};

struct Selection {
    SelectionMode mode = SelectionMode::vertex;
    std::vector<ElementId> vertices;
    std::vector<Edge> edges;
    std::vector<ElementId> faces;
    bool has_active = false;
    ElementKind active_kind = ElementKind::vertex;
    ElementId active_vertex = invalid_element_id;
    Edge active_edge;
    ElementId active_face = invalid_element_id;

    [[nodiscard]] bool empty() const;
    void clear();
};

struct CompiledVertex {
    Vec3 position;
    Vec3 normal { 0.0F, 1.0F, 0.0F };
    Vec2 uv0;
    std::uint32_t color = 0xff9f9f9f;
};

struct CompiledPrimitive {
    std::uint32_t index_offset = 0;
    std::uint32_t index_count = 0;
    std::uint32_t material_slot = 0;
};

struct CompiledMesh {
    std::vector<CompiledVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<CompiledPrimitive> primitives;
};

struct OperationResult {
    bool changed = false;
    std::string message;
};

enum class PlaneCutMode {
    keep_back = 0,
    keep_front = 1,
    keep_both = 2,
};

struct PlaneCutRequest {
    Vec3 first_point;
    Vec3 second_point;
    Vec3 third_point;
    PlaneCutMode mode = PlaneCutMode::keep_both;
};

struct PlaneCutResult {
    bool changed = false;
    Document front_document;
    Selection front_selection;
    Document back_document;
    Selection back_selection;
    int split_count = 0;
    int removed_count = 0;
    std::string message;
};

enum class BevelProfileType {
    offset = 0,
};

struct EdgeBevelSettings {
    float width = 1.0F;
    // Profile in the 0..1 range; 0.5 is circular and 1 is the exact square endpoint.
    float profile = 0.5F;
    BevelProfileType profile_type = BevelProfileType::offset;
    int segments = 1;
};

enum class KnifePointTargetKind {
    existing_vertex,
    existing_edge,
    inserted_vertex,
    face_point,
};

struct KnifePointTarget {
    KnifePointTargetKind kind = KnifePointTargetKind::existing_vertex;
    ElementId vertex_id = invalid_element_id;
    Edge edge;
    float edge_factor = 0.5F;
    ElementId face_id = invalid_element_id;
    Vec3 position;
};

struct KnifeStrokeSegment {
    std::uint32_t first_point = 0;
    std::uint32_t second_point = 0;
};

struct ExtractFacesResult {
    bool changed = false;
    Document extracted_document;
    Selection extracted_selection;
    std::string message;
};

} // namespace quader_poly

#include <quader_poly/poly_operation.h>

namespace quader_poly {

struct PickOptions {
    SelectionMode mode = SelectionMode::vertex;
    float vertex_radius = 0.12F;
    float edge_radius = 0.10F;
};

struct PickResult {
    bool hit = false;
    ElementKind kind = ElementKind::vertex;
    ElementId vertex_id = invalid_element_id;
    Edge edge;
    ElementId face_id = invalid_element_id;
    float distance = 0.0F;
    Vec3 position;
};

struct PickFaceTriangle {
    ElementId face_id = invalid_element_id;
    Vec3 a;
    Vec3 b;
    Vec3 c;
    Vec3 normal;
    Vec3 bounds_min;
    Vec3 bounds_max;
};

struct OverlayLine {
    Vec3 start;
    Vec3 end;
    std::uint32_t color = 0xffffffff;
    float width_pixels = 1.0F;
    Edge edge;
    ElementId face_id = invalid_element_id;
};

struct OverlayTriangle {
    Vec3 a;
    Vec3 b;
    Vec3 c;
    std::uint32_t color = 0xffffffff;
    ElementId face_id = invalid_element_id;
};

struct OverlayPoint {
    Vec3 position;
    std::uint32_t color = 0xffffffff;
    float size_pixels = 5.0F;
    ElementId vertex_id = invalid_element_id;
};

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

struct OverlayData {
    std::vector<OverlayLine> lines;
    std::vector<OverlayTriangle> triangles;
    std::vector<OverlayPoint> points;
};

struct KnifeSegmentPreview {
    bool valid = false;
    Vec3 start;
    Vec3 end;
    OverlayData overlay;
    std::string message;
};

[[nodiscard]] Vec3 operator+(const Vec3& left, const Vec3& right);
[[nodiscard]] Vec3 operator-(const Vec3& left, const Vec3& right);
[[nodiscard]] Vec3 operator*(const Vec3& value, float scalar);
[[nodiscard]] Vec3 operator/(const Vec3& value, float scalar);
Vec3& operator+=(Vec3& left, const Vec3& right);

[[nodiscard]] float dot(const Vec3& left, const Vec3& right);
[[nodiscard]] Vec3 cross(const Vec3& left, const Vec3& right);
[[nodiscard]] float length_squared(const Vec3& value);
[[nodiscard]] float length(const Vec3& value);
[[nodiscard]] Vec3 normalize_or_zero(const Vec3& value);
[[nodiscard]] Edge make_edge(ElementId a, ElementId b);

[[nodiscard]] Document make_cube(const CubePrimitiveSpec& spec);
[[nodiscard]] Document make_plane(const PlanePrimitiveSpec& spec);
[[nodiscard]] Document make_cylinder(const CylinderPrimitiveSpec& spec);
[[nodiscard]] Document make_cone(const ConePrimitiveSpec& spec);
[[nodiscard]] Document make_quad_sphere(const QuadSpherePrimitiveSpec& spec);
[[nodiscard]] Document make_uv_sphere(const UvSpherePrimitiveSpec& spec);
[[nodiscard]] Document make_ico_sphere(const IcoSpherePrimitiveSpec& spec);
[[nodiscard]] Document make_torus(const TorusPrimitiveSpec& spec);
[[nodiscard]] ElementId add_vertex(Document& document, Vec3 position);
[[nodiscard]] ElementId add_face(Document& document, std::span<const ElementId> vertices, std::uint32_t material_slot = 0);
[[nodiscard]] const Vertex* find_vertex(const Document& document, ElementId id);
[[nodiscard]] Vertex* find_vertex(Document& document, ElementId id);
[[nodiscard]] const Face* find_face(const Document& document, ElementId id);
[[nodiscard]] Face* find_face(Document& document, ElementId id);
[[nodiscard]] std::vector<Edge> document_edges(const Document& document);
[[nodiscard]] FacePerimeterInfo face_perimeter_info(const Document& document, ElementId face_id);
[[nodiscard]] FacePerimeterInfo selected_faces_perimeter_info(const Document& document, const Selection& selection);
[[nodiscard]] Vec3 face_normal(const Document& document, const Face& face);
[[nodiscard]] std::optional<float> edge_factor_from_position(const Document& document, Edge edge, Vec3 position);
[[nodiscard]] std::optional<float> edge_factor_from_ray(const Document& document, Edge edge, const Ray& ray);
void ensure_face_uvs(Document& document);
void regenerate_face_uvs(Document& document);
void clear_face_uvs(Document& document);
void shift_face_uvs_for_translation(Document& document, Vec3 delta);
[[nodiscard]] OperationResult apply_uv_operation(Document& document, const Selection& selection, std::string_view operation, float shift_world_amount = 1.0F);
[[nodiscard]] OperationResult assign_selected_face_material_slot(Document& document, const Selection& selection, std::uint32_t material_slot);
[[nodiscard]] OperationResult assign_face_material_slot(Document& document, ElementId face_id, std::uint32_t material_slot);
[[nodiscard]] CompiledMesh compile_document(const Document& document, SurfaceShading shading = SurfaceShading::authored);

void select_only(Selection& selection, const PickResult& pick);
void add_selection(Selection& selection, const PickResult& pick);
void remove_selection(Selection& selection, const PickResult& pick);
void set_selection_mode(Selection& selection, SelectionMode mode);
void convert_selection_mode(const Document& document, Selection& selection, SelectionMode mode);
[[nodiscard]] bool selection_contains(const Selection& selection, ElementId vertex_id);
[[nodiscard]] bool selection_contains(const Selection& selection, Edge edge);
[[nodiscard]] bool selection_contains_face(const Selection& selection, ElementId face_id);
[[nodiscard]] std::vector<ElementId> selected_vertex_ids(const Document& document, const Selection& selection);
[[nodiscard]] std::vector<Edge> edge_loop_edges(const Document& document, Edge seed_edge);
void select_edge_loop(const Document& document, Selection& selection, Edge seed_edge, bool toggle = false);
[[nodiscard]] std::optional<Vec3> selection_center(const Document& document, const Selection& selection);
[[nodiscard]] OperationResult translate_selection(Document& document, const Selection& selection, Vec3 delta);
[[nodiscard]] OperationResult transform_selection(Document& document, const Selection& selection, const Transform3& transform);
[[nodiscard]] OperationResult snap_selected_vertices_to_active(Document& document, Selection& selection);
[[nodiscard]] OperationResult merge_selected_vertices_to_active(Document& document, Selection& selection);
[[nodiscard]] OperationResult merge_selected_vertices_to_center(Document& document, Selection& selection);
[[nodiscard]] OperationResult merge_selected_vertices_by_distance(Document& document, Selection& selection, float tolerance);
[[nodiscard]] OperationResult bevel_selected_vertices(Document& document, Selection& selection, float distance);
[[nodiscard]] OperationResult connect_selected_vertices(Document& document, Selection& selection);
[[nodiscard]] OperationResult dissolve_selected_vertices(Document& document, Selection& selection);
[[nodiscard]] OperationResult connect_selected_edges(Document& document, Selection& selection);
[[nodiscard]] OperationResult split_selected_edges(Document& document, Selection& selection);
[[nodiscard]] OperationResult dissolve_selected_edges(Document& document, Selection& selection);
[[nodiscard]] OperationResult merge_selected_edges(Document& document, Selection& selection);
[[nodiscard]] OperationResult collapse_selected_edges(Document& document, Selection& selection);
[[nodiscard]] OperationResult fill_hole_from_selected_edges(Document& document, Selection& selection);
[[nodiscard]] OperationResult bevel_selected_edges(Document& document, Selection& selection, const EdgeBevelSettings& settings = {});
[[nodiscard]] OperationResult bridge_edge_pairs(Document& document, Selection& selection, const std::vector<std::pair<Edge, Edge>>& edge_pairs, int steps = 1);
[[nodiscard]] OperationResult bridge_edge_boundaries(Document& document, Selection& selection, std::span<const ElementId> first_vertices, std::span<const ElementId> second_vertices, bool closed, int steps = 1);
[[nodiscard]] OperationResult bridge_selected_edges(Document& document, Selection& selection, int steps = 1);
[[nodiscard]] OperationResult combine_selected_faces(Document& document, Selection& selection);
[[nodiscard]] OperationResult collapse_selected_faces(Document& document, Selection& selection);
[[nodiscard]] OperationResult radial_align_selection(Document& document, Selection& selection);
[[nodiscard]] OperationResult flip_selected_face_normals(Document& document, const Selection& selection);
[[nodiscard]] OperationResult recalculate_selected_face_normals(Document& document, const Selection& selection, bool outside);
[[nodiscard]] OperationResult shade_selected_faces_smooth(Document& document, const Selection& selection);
[[nodiscard]] OperationResult shade_selected_faces_flat(Document& document, const Selection& selection);
[[nodiscard]] OperationResult harden_selected_edge_normals(Document& document, const Selection& selection);
[[nodiscard]] OperationResult soften_selected_edge_normals(Document& document, const Selection& selection);
[[nodiscard]] OperationResult delete_selection(Document& document, const Selection& selection);
[[nodiscard]] PlaneCutResult plane_cut(const Document& document, const PlaneCutRequest& request);
[[nodiscard]] OperationResult insert_edge_loop(Document& document, Selection& selection, float factor = 0.5F);
[[nodiscard]] OperationResult slice_selected_quads(Document& document, Selection& selection, int x_slices = 1, int y_slices = 1);
[[nodiscard]] OperationResult knife_segment(Document& document, Selection& selection, const KnifePointTarget& previous, const KnifePointTarget& current);
[[nodiscard]] OperationResult knife_stroke(Document& document, Selection& selection, std::span<const KnifePointTarget> points, std::span<const KnifeStrokeSegment> segments);
[[nodiscard]] OperationResult extrude_selected_elements(Document& document, Selection& selection, Vec3 offset, float closed_edge_ledge_size = 0.0F);
[[nodiscard]] OperationResult inset_selected_elements(Document& document, Selection& selection, const Transform3& transform, Vec3 pivot);
[[nodiscard]] OperationResult inset_selected_faces(Document& document, Selection& selection, float amount = 0.18F);
[[nodiscard]] OperationResult extrude_selected_faces(Document& document, Selection& selection, float distance = 0.45F);
[[nodiscard]] OperationResult detach_selected_faces(Document& document, Selection& selection);
[[nodiscard]] ExtractFacesResult extract_selected_faces(Document& document, Selection& selection);
[[nodiscard]] OperationResult bridge_selected_faces(Document& document, Selection& selection, int steps = 1);
[[nodiscard]] OperationResult thicken_selected_faces(Document& document, Selection& selection, float thickness = 0.25F, bool from_center = false);

[[nodiscard]] PickResult pick_element(const Document& document, const Ray& local_ray, const PickOptions& options);
[[nodiscard]] std::vector<PickFaceTriangle> build_face_pick_geometry(const Document& document);
[[nodiscard]] PickResult pick_face_geometry(std::span<const PickFaceTriangle> triangles, const Ray& local_ray);
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
