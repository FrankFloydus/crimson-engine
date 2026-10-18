////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/ids.hpp>
#include <quader/modeling/operations/operation_result.hpp>
#include <quader/modeling/payloads.hpp>
#include <quader/modeling/result.hpp>
#include <quader/modeling/types.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace quader::modeling {

/** Configures bridge sampling and independent unitless endpoint tensions. */
struct BridgeOptions {
  int steps = 1;
  float start_tension = 1.0F;
  float end_tension = 1.0F;
};

struct PolygonDocumentImpl;
class PolygonDocumentNativeAccess;
class PolygonDocumentReadSnapshot;
class PreparedFaceExtrusion;
struct PreparedFaceExtrusionImpl;
struct FaceExtrusionEvaluation;
class PreparedSelectionTransform;
struct PreparedSelectionTransformImpl;
struct SelectionTransformEvaluation;

/**
 * Enumerates SDK polygon component selection modes.
 */
enum class SelectionMode {
  Vertex,
  Edge,
  Face,
};

/**
 * Opaque identity of one immutable authored-topology publication.
 *
 * Copies that share topology report the same identity. Selection-only changes
 * and no-op operations retain it; the first real geometry mutation publishes
 * a different identity without exposing native storage details.
 */
struct PolygonContentIdentity final {
  std::uint64_t value = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
  friend constexpr bool operator==(const PolygonContentIdentity &,
                                   const PolygonContentIdentity &) = default;
};

/**
 * Defines the bounds, surface subdivisions, winding, and material used to
 * create a box document.
 */
struct BoxSpec {
  Vec3 min{-1.0F, -1.0F, -1.0F};
  Vec3 max{1.0F, 1.0F, 1.0F};
  MaterialId material{};
  int x_subdivisions = 1;
  int y_subdivisions = 1;
  int z_subdivisions = 1;
  bool flip_faces = false;
};

/**
 * Defines an origin-centered cylinder document.
 */
struct CylinderSpec {
  float width = 2.0F;
  float height = 2.0F;
  float depth = 2.0F;
  int side_count = 32;
  bool capped = true;
  bool flip_faces = false;
  MaterialId material{};
};

/**
 * Defines an origin-centered cone document.
 */
struct ConeSpec {
  float width = 2.0F;
  float height = 2.0F;
  float depth = 2.0F;
  int side_count = 32;
  bool capped = true;
  bool flip_faces = false;
  MaterialId material{};
};

/**
 * Selects latitude/longitude or subdivided-cube sphere topology.
 */
enum class SphereType {
  Uv = 0,
  Quad = 1,
};

/**
 * Defines an origin-centered sphere or upper hemisphere document.
 */
struct SphereSpec {
  float width = 2.0F;
  float height = 2.0F;
  float depth = 2.0F;
  int radial_segments = 32;
  int rings = 16;
  bool hemisphere = false;
  bool flip_faces = false;
  MaterialId material{};
  SphereType type = SphereType::Uv;
  int subdivision = 3;
};

/**
 * Defines an origin-centered torus document whose dimensions are its outer
 * bounds. The tube radius ratio is measured against the outer X/Z radii.
 */
struct TorusSpec {
  float width = 2.0F;
  float height = 0.4F;
  float depth = 2.0F;
  int major_segments = 32;
  int minor_segments = 12;
  float tube_radius_ratio = 0.2F;
  bool flip_faces = false;
  MaterialId material{};
};

/**
 * Captures polygon component selection state with SDK typed IDs. When
 * has_active is true, the active ID for kind must belong to the corresponding
 * selected component list.
 */
struct PolygonSelectionSnapshot {
  SelectionKind kind = SelectionKind::Vertex;
  std::vector<VertexId> vertices;
  std::vector<EdgeKey> edges;
  std::vector<FaceId> faces;
  bool has_active = false;
  VertexId active_vertex{};
  EdgeKey active_edge{};
  FaceId active_face{};
};

/**
 * Defines the live component domain toggled by selection inversion. Selected
 * components outside this universe are preserved.
 */
struct PolygonSelectionUniverse {
  SelectionMode mode = SelectionMode::Vertex;
  std::vector<VertexId> vertices;
  std::vector<EdgeKey> edges;
  std::vector<FaceId> faces;
};

/**
 * SDK edge bevel options that mirror the current polygon operation defaults.
 */
struct EdgeBevelSpec {
  float width = 1.0F;
  float profile = 0.5F;
  int segments = 1;
};

struct ExtractedPolygonDocument;
struct SplitPolygonDocuments;

/** Selection geometry facts for revision-cached operation availability. */
struct PolygonSelectionTopologyEligibility {
  bool whole_mesh_closed = false;
  bool whole_mesh_has_open_perimeter = false;
  bool open_face_perimeter = false;
  bool combinable_selected_faces = false;
  bool bridge_face_pair = false;
  std::uint32_t single_selected_face_edge_count = 0;
  bool bridge_edge_pair = false;
  bool selected_edges_belong_to_faces = false;
  bool selected_edges_are_interior = false;
  bool selected_edges_are_open_boundary = false;
  bool three_selected_open_boundary_edges = false;
  bool selected_vertices_belong_to_faces = false;
  bool selected_vertices_share_face = false;
};

/// Defines a portable polygon split plane and optional generated caps.
struct PlaneSplitSpec {
  Vec3 a{};
  Vec3 b{};
  Vec3 c{};
  bool cap = true;
};

/** Selects which side of a cut operation remains. */
enum class CutKeepMode {
  DiscardLeft,
  DiscardRight,
  KeepBoth,
};

/**
 * Owns an SDK polygon document facade over portable polygon operations.
 */
class PolygonDocument {
public:
  PolygonDocument();
  PolygonDocument(const PolygonDocument &other);
  PolygonDocument(PolygonDocument &&other) noexcept;
  PolygonDocument &operator=(const PolygonDocument &other);
  PolygonDocument &operator=(PolygonDocument &&other) noexcept;
  ~PolygonDocument();

  [[nodiscard]] static Result<PolygonDocument> make_box(const BoxSpec &spec);
  [[nodiscard]] static Result<PolygonDocument>
  make_box_from_corners(std::span<const Vec3> corners,
                        MaterialId material = {});
  [[nodiscard]] static Result<PolygonDocument>
  make_cylinder(const CylinderSpec &spec);
  [[nodiscard]] static Result<PolygonDocument>
  make_cone(const ConeSpec &spec);
  [[nodiscard]] static Result<PolygonDocument>
  make_sphere(const SphereSpec &spec);
  [[nodiscard]] static Result<PolygonDocument>
  make_torus(const TorusSpec &spec);
  [[nodiscard]] static Result<PolygonDocument>
  make_face(std::span<const Vec3> points, MaterialId material = {});
  /// Imports authored topology without removing finite collapsed faces.
  /// Structural errors are rejected; validate() additionally diagnoses geometry.
  [[nodiscard]] static Result<PolygonDocument>
  from_authored_topology(const AuthoredTopology &topology);
  /// Restores durable authored records without modeling validation or cleanup.
  /// QGEO stores component indexes with generation one; other generations are
  /// unrepresentable. Validate/edit eligibility remains a separate owner concern.
  [[nodiscard]] static Result<PolygonDocument>
  from_persisted_topology(const AuthoredTopology &topology);

  [[nodiscard]] std::size_t vertex_count() const;
  [[nodiscard]] std::size_t face_count() const;
  [[nodiscard]] PolygonContentIdentity content_identity() const noexcept;
  [[nodiscard]] std::uint64_t content_revision() const;
  [[nodiscard]] std::uint64_t selection_revision() const;
  /// Retains allocation cursors observed by history without replacing geometry
  /// or selection and without revalidating an existing authored document.
  void preserve_identity_high_water(const PolygonDocument &other);
  /** Returns whether two facades currently reference the same immutable topology. */
  [[nodiscard]] bool
  shares_topology_with(const PolygonDocument &other) const noexcept;
  /** Returns whether two facades reference the same immutable selection state. */
  [[nodiscard]] bool
  shares_selection_with(const PolygonDocument &other) const noexcept;
  /** Returns whether local vertex positions and polygon topology match exactly. */
  [[nodiscard]] bool
  has_same_geometry(const PolygonDocument &other) const;
  [[nodiscard]] std::vector<VertexId> vertex_ids() const;
  [[nodiscard]] std::vector<EdgeKey> edge_ids() const;
  [[nodiscard]] std::vector<FaceId> face_ids() const;
  [[nodiscard]] std::vector<EdgeKey> hard_edge_ids() const;
  [[nodiscard]] std::vector<EdgeKey> soft_edge_ids() const;
  [[nodiscard]] std::vector<EdgeKey> default_edge_ids() const;
  [[nodiscard]] std::vector<FaceId>
  face_ids_by_material_slot(std::uint32_t material_slot) const;
  [[nodiscard]] std::vector<FaceId>
  face_ids_by_normal(Vec3 direction, float tolerance = 0.01F) const;
  [[nodiscard]] PolygonSelectionSnapshot selection() const;
  [[nodiscard]] bool selected_faces_have_open_perimeter() const;
  [[nodiscard]] PolygonSelectionTopologyEligibility
  selection_topology_eligibility() const;
  [[nodiscard]] PolygonSelectionTopologyEligibility
  whole_mesh_topology_eligibility() const;
  [[nodiscard]] Result<AuthoredPolygonPayload> authored_payload() const;
  [[nodiscard]] Result<AuthoredTopology> authored_topology() const;
  [[nodiscard]] Result<std::vector<FaceTextureSettings>>
  face_texture_settings(std::span<const FaceId> faces) const;
  [[nodiscard]] Result<MeshPayload> compile_mesh() const;
  [[nodiscard]] Result<OperationResult> validate() const;

  [[nodiscard]] Result<OperationResult> set_selection_mode(SelectionMode mode);
  [[nodiscard]] Result<OperationResult> clear_selection();
  [[nodiscard]] Result<OperationResult>
  select_vertices(std::span<const VertexId> vertex_ids,
                  SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] Result<OperationResult>
  select_edges(std::span<const EdgeKey> edges,
               SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] Result<OperationResult>
  select_faces(std::span<const FaceId> face_ids,
               SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] Result<OperationResult> select_vertex(VertexId vertex_id);
  [[nodiscard]] Result<OperationResult> select_edge(EdgeKey edge);
  [[nodiscard]] Result<OperationResult> select_face(FaceId face_id);
  [[nodiscard]] Result<OperationResult> select_all_vertices();
  [[nodiscard]] Result<OperationResult> select_all_edges();
  [[nodiscard]] Result<OperationResult> select_all_faces();
  [[nodiscard]] Result<OperationResult>
  invert_selection(const PolygonSelectionUniverse &eligible);
  [[nodiscard]] Result<OperationResult>
  apply_selection(const PolygonSelectionSnapshot &selection,
                  SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] Result<OperationResult> translate_selection(Vec3 delta);
  [[nodiscard]] Result<OperationResult>
  transform_selection(Transform3 transform);
  /// Captures the source and selected vertex storage indices for repeated
  /// topology-preserving transform evaluations relative to this document.
  [[nodiscard]] Result<PreparedSelectionTransform>
  prepare_selection_transform() const;
  [[nodiscard]] Result<OperationResult>
  set_vertex_positions(std::span<const VertexPositionUpdate> updates);
  [[nodiscard]] Result<OperationResult>
  reflect_across_plane(Vec3 origin, Vec3 normal);
  [[nodiscard]] Result<OperationResult>
  assign_selected_face_material_slot(std::uint32_t material_slot);
  [[nodiscard]] Result<OperationResult>
  assign_all_face_material_slot(std::uint32_t material_slot);
  [[nodiscard]] Result<OperationResult>
  assign_face_material_slot(FaceId face_id, std::uint32_t material_slot);
  [[nodiscard]] Result<OperationResult>
  set_face_texture_transforms(std::span<const FaceTextureTransformUpdate> updates);
  [[nodiscard]] Result<OperationResult> snap_selected_vertices_to_active();
  [[nodiscard]] Result<OperationResult> merge_selected_vertices_to_active();
  [[nodiscard]] Result<OperationResult> merge_selected_vertices_to_center();
  [[nodiscard]] Result<OperationResult>
  merge_selected_vertices_by_distance(float tolerance);
  /// Welds vertices in the current component selection into nearby unselected
  /// targets, then welds remaining selected clusters without moving survivors.
  [[nodiscard]] Result<OperationResult>
  auto_merge_selected_vertices(float tolerance = 0.001F);
  [[nodiscard]] Result<OperationResult> remove_double_vertices();
  [[nodiscard]] Result<OperationResult> bevel_selected_vertices(float distance);
  [[nodiscard]] Result<OperationResult> connect_selected_vertices();
  [[nodiscard]] Result<OperationResult> dissolve_selected_vertices();
  [[nodiscard]] Result<OperationResult> connect_selected_edges();
  [[nodiscard]] Result<OperationResult> snap_selected_edges_to_active();
  [[nodiscard]] Result<OperationResult> split_selected_edges();
  [[nodiscard]] Result<OperationResult> dissolve_selected_edges();
  [[nodiscard]] Result<OperationResult> merge_selected_edges();
  [[nodiscard]] Result<OperationResult> collapse_selected_edges();
  [[nodiscard]] Result<OperationResult> fill_hole_from_selected_edges();
  [[nodiscard]] Result<OperationResult>
  bevel_selected_edges(EdgeBevelSpec settings = {});
  [[nodiscard]] Result<OperationResult>
  bridge_selected_edges(int steps = 1);
  [[nodiscard]] Result<OperationResult> bridge_selected_edges(BridgeOptions options);
  [[nodiscard]] Result<OperationResult> combine_selected_faces();
  [[nodiscard]] Result<OperationResult> collapse_selected_faces();
  [[nodiscard]] Result<OperationResult> radial_align_selection();
  [[nodiscard]] Result<OperationResult> flip_selected_face_normals();
  [[nodiscard]] Result<OperationResult>
  recalculate_selected_face_normals(bool outside = true);
  [[nodiscard]] Result<OperationResult> shade_selected_faces_smooth();
  [[nodiscard]] Result<OperationResult> shade_selected_faces_flat();
  [[nodiscard]] Result<OperationResult> harden_selected_edge_normals();
  [[nodiscard]] Result<OperationResult> soften_selected_edge_normals();
  [[nodiscard]] Result<OperationResult>
  reset_selected_edge_normals_to_default();
  [[nodiscard]] Result<OperationResult> delete_selection();
  [[nodiscard]] Result<OperationResult>
  insert_edge_loop(float factor = 0.5F);
  [[nodiscard]] Result<OperationResult>
  knife_segment(KnifeTarget previous, KnifeTarget current);
  [[nodiscard]] Result<OperationResult>
  knife_stroke(std::span<const KnifeTarget> points,
               std::span<const KnifeStrokeSegment> segments);
  [[nodiscard]] Result<OperationResult>
  slice_selected_quads(int x_slices = 1, int y_slices = 1);
  [[nodiscard]] Result<OperationResult>
  extrude_selected_elements(Vec3 offset,
                            float closed_edge_ledge_size = 0.0F);
  /// Support width uses raw grid size: full size for vertices, quarter size for closed edges.
  [[nodiscard]] Result<OperationResult>
  transform_extrude_selected_elements(Transform3 transform, Vec3 pivot = {},
                                      float extrusion_grid_size = 1.0F);
  [[nodiscard]] Result<OperationResult>
  inset_selected_elements(Transform3 transform, Vec3 pivot);
  [[nodiscard]] Result<OperationResult>
  inset_selected_faces(float amount = 0.18F);
  [[nodiscard]] Result<OperationResult>
  extrude_selected_faces(float distance = 0.45F);
  [[nodiscard]] Result<OperationResult>
  extrude_selected_faces(const FaceExtrudeOptions &options);
  /// Captures this source, its allocation cursors, and fixed extrusion topology.
  /// Reconcile retired IDs before preparing a new extrusion lifecycle.
  [[nodiscard]] Result<PreparedFaceExtrusion> prepare_face_extrusion(
      FaceExtrudeMode mode, const Transform3 &extrusion_from_local = {}) const;
  [[nodiscard]] Result<PolygonFaceExtrudeFrame> selected_face_extrude_frame(
      const Transform3 &extrusion_from_local = {}) const;
  [[nodiscard]] Result<OperationResult> detach_selected_faces();
  [[nodiscard]] Result<ExtractedPolygonDocument> extract_selected_faces();
  [[nodiscard]] Result<SplitPolygonDocuments>
  split_by_plane(const PlaneSplitSpec &spec) const;
  [[nodiscard]] Result<OperationResult>
  cut_surface_by_plane(const PlaneSplitSpec &spec);
  /// Cuts selected faces in place; optional caps fill newly exposed plane loops.
  [[nodiscard]] Result<OperationResult> cut_selected_faces_by_plane(
      const PlaneSplitSpec &spec, CutKeepMode keep = CutKeepMode::KeepBoth);
  /// Inserts plane intersections into selected edges and their incident loops.
  [[nodiscard]] Result<OperationResult>
  cut_selected_edges_by_plane(const PlaneSplitSpec &spec);
  [[nodiscard]] Result<OperationResult>
  bridge_selected_faces(int steps = 1);
  [[nodiscard]] Result<OperationResult> bridge_selected_faces(BridgeOptions options);
  [[nodiscard]] Result<OperationResult>
  thicken_selected_faces(float thickness = 0.25F,
                         bool from_center = false);

private:
  friend class PolygonDocumentNativeAccess;
  friend PolygonDocumentReadSnapshot
  polygon_document_read_snapshot(const PolygonDocument &document);
  friend class PreparedFaceExtrusion;
  friend class PreparedSelectionTransform;

  explicit PolygonDocument(std::unique_ptr<PolygonDocumentImpl> impl);
  [[nodiscard]] static Result<PolygonDocument>
  import_authored_topology(const AuthoredTopology &topology,
                          bool preserve_storage);

  std::unique_ptr<PolygonDocumentImpl> impl_;
};

/** Carries an immutable transform candidate and its receipt against the source. */
struct SelectionTransformEvaluation {
  PolygonDocument document;
  OperationResult receipt;
  /// All prepared targets in source storage order, including unchanged targets.
  /// Shares the candidate's authoritative coordinates; null for an empty target.
  std::shared_ptr<const std::vector<VertexPositionUpdate>> positions;
};

/** Reports coordinate publication and legacy materialization work for one plan. */
struct PreparedSelectionTransformWorkStats {
  std::uint64_t coordinate_publication_count = 0;
  std::uint64_t evaluated_vertex_count = 0;
  std::uint64_t topology_materialization_count = 0;
  std::uint64_t coordinate_snapshot_count = 0;
};

/**
 * Resolves component selection once for a topology-preserving gesture. Each
 * evaluation starts from the captured source, preserves selection and authored
 * UVs, and rejects nonfinite input or output without changing earlier results.
 * Identity/no-op evaluations retain the source's immutable topology identity.
 */
class PreparedSelectionTransform final {
public:
  [[nodiscard]] bool valid() const noexcept { return impl_ != nullptr; }
  [[nodiscard]] Result<SelectionTransformEvaluation>
  evaluate_translation(Vec3 delta) const;
  [[nodiscard]] Result<SelectionTransformEvaluation>
  evaluate_transform(Transform3 transform) const;
  [[nodiscard]] PreparedSelectionTransformWorkStats work_stats() const noexcept;

private:
  friend class PolygonDocument;
  std::shared_ptr<const PreparedSelectionTransformImpl> impl_;
};

/** An evaluated candidate and its complete receipt relative to the captured source. */
struct FaceExtrusionEvaluation {
  PolygonDocument document;
  OperationResult receipt;
};

/**
 * Immutable prepared extrusion; all nonzero evaluations retain its generated IDs.
 * Zero returns the original source. Once a host publishes zero or retires this
 * topology, it must reconcile allocation high-water and prepare a new lifecycle.
 * Failures specific to nonzero extrusion are reported by evaluate(), allowing
 * zero to preserve otherwise valid source geometry with cancelling normals.
 * Evaluation never changes an earlier candidate or the preparation on failure.
 */
class PreparedFaceExtrusion final {
public:
  [[nodiscard]] bool valid() const noexcept { return impl_ != nullptr; }
  [[nodiscard]] Result<FaceExtrusionEvaluation> evaluate(
      float amount, std::optional<Vec3> region_direction = {}) const;

private:
  friend class PolygonDocument;
  std::shared_ptr<const PreparedFaceExtrusionImpl> impl_;
};

/**
 * Result for extracting faces into a new SDK polygon document.
 */
struct ExtractedPolygonDocument {
  OperationResult receipt;
  PolygonDocument document;
};

/// Carries both portable documents produced by a plane split.
struct SplitPolygonDocuments {
  OperationResult receipt;
  PolygonDocument front;
  PolygonDocument back;
  int split_count = 0;
  int removed_count = 0;
};

} // namespace quader::modeling
