////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/api/modeling_contracts.hpp>
#include <quader/modeling/api/mesh_handle.hpp>
#include <quader/modeling/api/previews.hpp>
#include <quader/modeling/api/selection.hpp>
#include <quader/modeling/api/tools.hpp>

namespace quader::modeling {

/**
 * Provides explicit-result access to root modeling operations.
 */
class CheckedModelingApi {
public:
  CheckedModelingApi() = default;
  [[nodiscard]] Result<MeshHandle> create_box(BoxOptions options = {});
  [[nodiscard]] Result<MeshHandle>
  create_cylinder(CylinderOptions options = {});
  [[nodiscard]] Result<MeshHandle> create_cone(ConeOptions options = {});
  [[nodiscard]] Result<MeshHandle> create_sphere(SphereOptions options = {});
  [[nodiscard]] Result<MeshHandle> create_torus(TorusOptions options = {});
  [[nodiscard]] Result<MeshHandle>
  add_document(PolygonDocument document, std::string name = "Mesh");
  [[nodiscard]] CheckedIoApi io();
  [[nodiscard]] CheckedToolsApi tools();
  [[nodiscard]] CheckedOperationsApi operations();

private:
  friend class ModelingApi;
  friend class CheckedIoApi;
  friend class CheckedToolsApi;
  explicit CheckedModelingApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Root public modeling API facade for portable C++ consumers.
 */
class ModelingApi {
public:
  ModelingApi();
  [[nodiscard]] static ModelingApi create(ModelingApiOptions options = {});

  [[nodiscard]] CheckedModelingApi checked();
  [[nodiscard]] const Error *last_error() const;
  void clear_error();

  [[nodiscard]] MeshHandle create_box(BoxOptions options = {});
  [[nodiscard]] MeshHandle create_cylinder(CylinderOptions options = {});
  [[nodiscard]] MeshHandle create_cone(ConeOptions options = {});
  [[nodiscard]] MeshHandle create_sphere(SphereOptions options = {});
  [[nodiscard]] MeshHandle create_torus(TorusOptions options = {});
  [[nodiscard]] MeshHandle add_document(PolygonDocument document,
                                        std::string name = "Mesh");

  [[nodiscard]] BoxTool activate_box_tool(BoxToolOptions options = {});
  [[nodiscard]] KnifeTool activate_knife_tool(KnifeToolOptions options = {});
  [[nodiscard]] CutTool activate_cut_tool(CutToolOptions options = {});

  [[nodiscard]] MeshHandle mesh(ObjectId object);
  [[nodiscard]] MeshCollection meshes();
  [[nodiscard]] std::vector<MeshSummary> mesh_summaries() const;
  [[nodiscard]] std::vector<MeshHandle> selected_meshes() const;

  [[nodiscard]] SelectionApi selection();
  [[nodiscard]] OperationsApi operations();
  [[nodiscard]] ToolsApi tools();
  [[nodiscard]] PreviewApi preview();
  [[nodiscard]] MeshSyncApi mesh_sync();
  [[nodiscard]] IoApi io();
  [[nodiscard]] CommandApi commands();
  /**
   * @deprecated Portable profiling capture is not implemented. This
   * compatibility facade is scheduled for removal in 0.2.0; use the benchmark
   * harness or native diagnostics owner.
   */
  [[deprecated("Portable profiling is unsupported and will be removed in "
               "0.2.0; use the benchmark harness or native diagnostics.")]]
  [[nodiscard]] ProfilingApi profiling();
  [[nodiscard]] RenderApi render();
  [[nodiscard]] ModelingBatch batch(std::string label = {});

  [[nodiscard]] OperationReceipt undo();
  [[nodiscard]] OperationReceipt redo();
  [[nodiscard]] bool can_undo() const;
  [[nodiscard]] bool can_redo() const;
  [[nodiscard]] RevisionStamp revisions() const;

private:
  friend class MeshHandle;
  friend class CheckedModelingApi;
  friend class BoxTool;
  friend class IoApi;
  friend class ModelingBatch;
  friend class RenderApi;
  explicit ModelingApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Builder step returned after scheduling a mesh creation operation.
 */
class BatchCreatedMeshStep {
public:
  BatchCreatedMeshStep() = default;
  [[nodiscard]] ModelingBatch as(std::string alias);

private:
  friend class ModelingBatch;
  BatchCreatedMeshStep(std::shared_ptr<ModelingBatchState> state,
                       std::size_t step_index);
  std::shared_ptr<ModelingBatchState> state_;
  std::size_t step_index_ = 0;
};

/**
 * Collects public modeling intent and commits it as one batch operation.
 */
class ModelingBatch {
public:
  ModelingBatch() = default;
  [[nodiscard]] BatchCreatedMeshStep create_box(BoxOptions options = {});
  [[nodiscard]] ModelingBatch &with_mesh_checked(
      std::string alias,
      std::function<OperationReceipt(MeshHandle)> operation);
  [[deprecated("Use with_mesh_checked() so the batch can aggregate and "
               "validate the real operation receipt.")]]
  [[nodiscard]] ModelingBatch &
  with_mesh(std::string alias, std::function<void(MeshHandle)> operation);
  [[nodiscard]] BatchResult commit();
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ModelingApi;
  friend class BatchCreatedMeshStep;
  ModelingBatch(std::shared_ptr<ModelingApiContext> context, std::string label);
  explicit ModelingBatch(std::shared_ptr<ModelingBatchState> state);
  std::shared_ptr<ModelingBatchState> state_;
};

/**
 * String-dispatch and current-selection operation facade.
 */
class OperationsApi {
public:
  OperationsApi() = default;
  [[nodiscard]] std::vector<CommandDescriptor> catalog() const;
  [[nodiscard]] CommandDescriptor describe(std::string_view id) const;
  [[nodiscard]] OperationReceipt execute(std::string_view id,
                                         OperationSettings settings = {});
  [[nodiscard]] OperationReceipt delete_selection();
  [[nodiscard]] OperationReceipt copy_selection();
  [[nodiscard]] OperationReceipt paste_selection(PasteOptions options = {});
  [[nodiscard]] OperationReceipt repeat_last_action();
  [[nodiscard]] MeshHandle duplicate_meshes(DuplicateOptions options = {});
  [[nodiscard]] OperationReceipt combine_meshes();
  [[nodiscard]] OperationReceipt translate_selection(Vec3 delta);
  [[nodiscard]] OperationReceipt transform_selection(TransformOptions options);
  [[nodiscard]] OperationReceipt rotate_selection(RotateOptions options);
  [[nodiscard]] OperationReceipt scale_selection(ScaleOptions options);
  [[nodiscard]] OperationReceipt extrude(ExtrudeOptions options);
  [[nodiscard]] OperationReceipt extrude(FaceExtrudeOptions options);
  [[nodiscard]] OperationReceipt inset(InsetOptions options);
  [[nodiscard]] OperationReceipt mirror_selection(Axis axis);
  [[nodiscard]] OperationReceipt flip_horizontal();
  [[nodiscard]] OperationReceipt flip_vertical();
  [[nodiscard]] OperationReceipt invert_mesh_normals();
  [[nodiscard]] OperationReceipt shade_smooth_mesh();
  [[nodiscard]] OperationReceipt shade_flat_mesh();
  [[nodiscard]] PreviewHandle create_outer_hull(ThickenOptions options = {});
  [[nodiscard]] OperationReceipt snap_vertices_to_active();
  [[nodiscard]] OperationReceipt merge_vertices_to_active();
  [[nodiscard]] OperationReceipt merge_vertices_to_center();
  [[nodiscard]] PreviewHandle
  merge_by_distance(MergeByDistanceOptions options = {});
  [[nodiscard]] OperationReceipt
  remove_doubles(MergeByDistanceOptions options = {});
  [[nodiscard]] OperationReceipt bevel_vertices(BevelVerticesOptions options = {});
  [[nodiscard]] OperationReceipt connect_vertices();
  [[nodiscard]] OperationReceipt dissolve_vertices();
  [[nodiscard]] OperationReceipt radial_align_vertices();
  [[nodiscard]] OperationReceipt snap_edges_to_active();
  [[nodiscard]] OperationReceipt connect_edges();
  [[nodiscard]] OperationReceipt split_edges();
  [[nodiscard]] OperationReceipt default_edge_normals();
  [[nodiscard]] OperationReceipt harden_edge_normals();
  [[nodiscard]] OperationReceipt soften_edge_normals();
  [[nodiscard]] PreviewHandle bevel_edges(EdgeBevelOptions options = {});
  [[nodiscard]] OperationReceipt bridge_edges(BridgeOptions options = {});
  [[nodiscard]] PreviewHandle
  interpolated_bridge_edges(BridgeOptions options = {});
  [[nodiscard]] OperationReceipt dissolve_edges();
  [[nodiscard]] OperationReceipt merge_edges();
  [[nodiscard]] OperationReceipt collapse_edges();
  [[nodiscard]] OperationReceipt fill_hole();
  [[nodiscard]] OperationReceipt radial_align_edges();
  [[nodiscard]] OperationReceipt bridge_faces(BridgeOptions options = {});
  [[nodiscard]] PreviewHandle
  interpolated_bridge_faces(BridgeOptions options = {});
  [[nodiscard]] OperationReceipt invert_faces();
  [[nodiscard]] OperationReceipt combine_faces();
  [[nodiscard]] OperationReceipt collapse_faces();
  [[nodiscard]] OperationReceipt radial_align_faces();
  [[nodiscard]] PreviewHandle slice_quad(SliceQuadOptions options = {});
  [[nodiscard]] PreviewHandle thicken_faces(ThickenOptions options = {});
  [[nodiscard]] OperationReceipt extract_faces();
  [[nodiscard]] OperationReceipt detach_faces();
  [[nodiscard]] OperationReceipt
  assign_material_slot_to_selection(std::uint32_t slot);
  [[nodiscard]] OperationReceipt plane_cut(PlaneCutOptions options);
  [[nodiscard]] OperationReceipt knife_segment(KnifeSegmentOptions options);
  [[nodiscard]] OperationReceipt knife_stroke(KnifeStrokeOptions options);
  [[nodiscard]] OperationReceipt insert_edge_loop(InsertEdgeLoopOptions options);

private:
  friend class ModelingApi;
  friend class CheckedOperationsApi;
  friend class CommandApi;
  friend class CutTool;
  friend class InsertEdgeLoopTool;
  friend class KnifeTool;
  friend class MirrorTool;
  friend class TransformTool;
  explicit OperationsApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Provides revision-gated mesh synchronization payloads.
 */
class MeshSyncApi {
public:
  MeshSyncApi() = default;
  [[nodiscard]] MeshSyncSnapshot
  changes_since(std::uint64_t previous_content_revision) const;
  [[nodiscard]] MeshPayload mesh(ObjectId object,
                                 MeshCompileOptions options = {}) const;
  [[nodiscard]] AuthoredPolygonPayload authored_polygon(ObjectId object) const;

private:
  friend class ModelingApi;
  explicit MeshSyncApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Provides portable QDR and OBJ serialization.
 */
class IoApi {
public:
  IoApi() = default;
  [[nodiscard]] std::vector<std::byte> serialize_qdr() const;
  [[nodiscard]] std::vector<std::byte> serialize_qdr(ObjectId object) const;
  [[nodiscard]] std::vector<std::byte>
  serialize_qdr(std::span<const ObjectId> objects) const;
  [[nodiscard]] MeshHandle deserialize_qdr_object(
      std::span<const std::byte> bytes,
      std::string name = "Mesh");
  [[nodiscard]] std::vector<MeshHandle>
  deserialize_qdr_document(std::span<const std::byte> bytes);
  [[nodiscard]] std::string serialize_obj(ObjectId object) const;
  [[nodiscard]] std::string serialize_obj(std::span<const ObjectId> objects) const;

private:
  friend class ModelingApi;
  friend class CheckedIoApi;
  friend class CheckedModelingApi;
  explicit IoApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Provides explicit-result portable IO methods.
 */
class CheckedIoApi {
public:
  CheckedIoApi() = default;
  [[nodiscard]] Result<std::vector<std::byte>> serialize_qdr() const;
  [[nodiscard]] Result<std::vector<std::byte>>
  serialize_qdr(ObjectId object) const;
  [[nodiscard]] Result<std::vector<std::byte>>
  serialize_qdr(std::span<const ObjectId> objects) const;
  [[nodiscard]] Result<MeshHandle>
  deserialize_qdr_object(std::span<const std::byte> bytes,
                         std::string name = "Mesh");
  [[nodiscard]] Result<std::vector<MeshHandle>>
  deserialize_qdr_document(std::span<const std::byte> bytes);
  [[nodiscard]] Result<std::string> serialize_obj(ObjectId object) const;
  [[nodiscard]] Result<std::string>
  serialize_obj(std::span<const ObjectId> objects) const;

private:
  friend class CheckedModelingApi;
  friend class IoApi;
  explicit CheckedIoApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Provides explicit-result current-selection operation methods.
 */
class CheckedOperationsApi {
public:
  CheckedOperationsApi() = default;
  [[nodiscard]] Result<OperationReceipt> delete_selection();
  [[nodiscard]] Result<OperationReceipt> copy_selection();
  [[nodiscard]] Result<OperationReceipt>
  paste_selection(PasteOptions options = {});
  [[nodiscard]] Result<OperationReceipt> repeat_last_action();
  [[nodiscard]] Result<MeshHandle>
  duplicate_meshes(DuplicateOptions options = {});
  [[nodiscard]] Result<OperationReceipt> combine_meshes();
  [[nodiscard]] Result<OperationReceipt> translate_selection(Vec3 delta);
  [[nodiscard]] Result<OperationReceipt>
  transform_selection(TransformOptions options);
  [[nodiscard]] Result<OperationReceipt> rotate_selection(RotateOptions options);
  [[nodiscard]] Result<OperationReceipt> scale_selection(ScaleOptions options);
  [[nodiscard]] Result<OperationReceipt> extrude(ExtrudeOptions options);
  [[nodiscard]] Result<OperationReceipt> extrude(FaceExtrudeOptions options);
  [[nodiscard]] Result<OperationReceipt> inset(InsetOptions options);
  [[nodiscard]] Result<OperationReceipt> mirror_selection(Axis axis);
  [[nodiscard]] Result<OperationReceipt> flip_horizontal();
  [[nodiscard]] Result<OperationReceipt> flip_vertical();
  [[nodiscard]] Result<OperationReceipt> invert_mesh_normals();
  [[nodiscard]] Result<OperationReceipt> shade_smooth_mesh();
  [[nodiscard]] Result<OperationReceipt> shade_flat_mesh();
  [[nodiscard]] Result<PreviewHandle>
  create_outer_hull(ThickenOptions options = {});
  [[nodiscard]] Result<OperationReceipt> snap_vertices_to_active();
  [[nodiscard]] Result<OperationReceipt> merge_vertices_to_active();
  [[nodiscard]] Result<OperationReceipt> merge_vertices_to_center();
  [[nodiscard]] Result<PreviewHandle>
  merge_by_distance(MergeByDistanceOptions options = {});
  [[nodiscard]] Result<OperationReceipt>
  remove_doubles(MergeByDistanceOptions options = {});
  [[nodiscard]] Result<OperationReceipt>
  bevel_vertices(BevelVerticesOptions options = {});
  [[nodiscard]] Result<OperationReceipt> connect_vertices();
  [[nodiscard]] Result<OperationReceipt> dissolve_vertices();
  [[nodiscard]] Result<OperationReceipt> radial_align_vertices();
  [[nodiscard]] Result<OperationReceipt> snap_edges_to_active();
  [[nodiscard]] Result<OperationReceipt> connect_edges();
  [[nodiscard]] Result<OperationReceipt> split_edges();
  [[nodiscard]] Result<OperationReceipt> default_edge_normals();
  [[nodiscard]] Result<OperationReceipt> harden_edge_normals();
  [[nodiscard]] Result<OperationReceipt> soften_edge_normals();
  [[nodiscard]] Result<PreviewHandle>
  bevel_edges(EdgeBevelOptions options = {});
  [[nodiscard]] Result<OperationReceipt> bridge_edges(BridgeOptions options = {});
  [[nodiscard]] Result<PreviewHandle>
  interpolated_bridge_edges(BridgeOptions options = {});
  [[nodiscard]] Result<OperationReceipt> dissolve_edges();
  [[nodiscard]] Result<OperationReceipt> merge_edges();
  [[nodiscard]] Result<OperationReceipt> collapse_edges();
  [[nodiscard]] Result<OperationReceipt> fill_hole();
  [[nodiscard]] Result<OperationReceipt> radial_align_edges();
  [[nodiscard]] Result<OperationReceipt> bridge_faces(BridgeOptions options = {});
  [[nodiscard]] Result<PreviewHandle>
  interpolated_bridge_faces(BridgeOptions options = {});
  [[nodiscard]] Result<OperationReceipt> invert_faces();
  [[nodiscard]] Result<OperationReceipt> combine_faces();
  [[nodiscard]] Result<OperationReceipt> collapse_faces();
  [[nodiscard]] Result<OperationReceipt> radial_align_faces();
  [[nodiscard]] Result<PreviewHandle> slice_quad(SliceQuadOptions options = {});
  [[nodiscard]] Result<PreviewHandle> thicken_faces(ThickenOptions options = {});
  [[nodiscard]] Result<OperationReceipt> extract_faces();
  [[nodiscard]] Result<OperationReceipt> detach_faces();
  [[nodiscard]] Result<OperationReceipt>
  assign_material_slot_to_selection(std::uint32_t slot);
  [[nodiscard]] Result<OperationReceipt> plane_cut(PlaneCutOptions options);
  [[nodiscard]] Result<OperationReceipt>
  knife_segment(KnifeSegmentOptions options);
  [[nodiscard]] Result<OperationReceipt> knife_stroke(KnifeStrokeOptions options);
  [[nodiscard]] Result<OperationReceipt>
  insert_edge_loop(InsertEdgeLoopOptions options);

private:
  friend class CheckedModelingApi;
  explicit CheckedOperationsApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Provides stable string command catalog and dispatch.
 */
class CommandApi {
public:
  CommandApi() = default;
  [[nodiscard]] std::vector<CommandDescriptor> catalog() const;
  [[nodiscard]] CommandDescriptor describe(std::string_view command_id) const;
  [[nodiscard]] OperationReceipt execute(std::string_view command_id,
                                         OperationSettings settings = {});

private:
  friend class ModelingApi;
  explicit CommandApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Deprecated compatibility facade for unsupported portable profiling capture.
 * Scheduled for removal in 0.2.0. Profiling data types remain supported by the
 * benchmark harness independently of this facade.
 */
class ProfilingApi {
public:
  ProfilingApi() = default;
  [[nodiscard]] Result<ProfilingReport> report() const;

private:
  friend class ModelingApi;
  explicit ProfilingApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Provides explicit renderer-neutral scene snapshot materialization.
 */
class RenderApi {
public:
  RenderApi() = default;
  [[nodiscard]] RenderSnapshotPayload
  scene_snapshot(PayloadRequest request = {}) const;

private:
  friend class ModelingApi;
  explicit RenderApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

} // namespace quader::modeling
