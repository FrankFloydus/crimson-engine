////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/api/modeling_contracts.hpp>

namespace quader::modeling {

/**
 * Provides object-scope queries and mutations for all session meshes.
 */
class MeshCollection {
public:
  MeshCollection() = default;
  [[nodiscard]] std::vector<MeshHandle> all() const;
  [[nodiscard]] ObjectSelection selected() const;
  [[nodiscard]] ObjectSelection only(std::span<const ObjectId> objects) const;
  [[nodiscard]] MeshHandle first_selected() const;
  [[nodiscard]] OperationReceipt delete_selected();
  [[nodiscard]] MeshHandle duplicate_selected(DuplicateOptions options = {});
  [[nodiscard]] OperationReceipt combine_selected();

private:
  friend class ModelingApi;
  friend class MeshHandle;
  friend class OperationsApi;
  friend class SelectionApi;
  friend class TransformTool;
  explicit MeshCollection(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

/**
 * Lightweight stale-checkable handle to one SDK mesh object.
 */
class MeshHandle {
public:
  MeshHandle() = default;
  [[nodiscard]] ObjectId id() const;
  [[nodiscard]] bool valid() const;
  [[nodiscard]] MeshSummary summary() const;
  [[nodiscard]] RevisionStamp revisions() const;

  [[nodiscard]] OperationReceipt rename(std::string name);
  [[nodiscard]] OperationReceipt destroy();
  [[nodiscard]] MeshHandle duplicate(DuplicateOptions options = {});
  [[nodiscard]] OperationReceipt combine_with(std::span<const MeshHandle> meshes);

  [[nodiscard]] ObjectSelection select();
  [[nodiscard]] VertexSelection select_all_vertices();
  [[nodiscard]] EdgeSelection select_all_edges();
  [[nodiscard]] FaceSelection select_all_faces();

  [[nodiscard]] MeshVertices vertices();
  [[nodiscard]] MeshEdges edges();
  [[nodiscard]] MeshFaces faces();
  [[nodiscard]] MeshTransform transform();
  [[nodiscard]] MeshMaterials materials();
  [[nodiscard]] MeshPayloads payloads() const;
  [[nodiscard]] MeshValidation validation() const;

private:
  friend class ModelingApi;
  friend class CheckedModelingApi;
  friend class MeshCollection;
  friend class MeshVertices;
  friend class MeshEdges;
  friend class MeshFaces;
  friend class MeshTransform;
  friend class MeshMaterials;
  friend class MeshPayloads;
  friend class MeshValidation;
  friend class MeshSyncApi;
  friend class IoApi;
  friend class FaceSelection;
  friend class ObjectSelection;
  friend class OperationsApi;
  friend struct BatchResult;
  friend class ModelingBatch;
  friend class RenderApi;
  explicit MeshHandle(std::shared_ptr<ModelingApiContext> context,
                      ObjectId object);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
  ObjectId object_{};
};

/**
 * Provides vertex selection scopes for one mesh.
 */
class MeshVertices {
public:
  MeshVertices() = default;
  [[nodiscard]] VertexSelection all() const;
  [[nodiscard]] VertexSelection selected() const;
  [[nodiscard]] VertexSelection only(std::span<const VertexId> vertices) const;
  [[nodiscard]] VertexSelection active() const;

private:
  friend class MeshHandle;
  explicit MeshVertices(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides edge selection scopes for one mesh.
 */
class MeshEdges {
public:
  MeshEdges() = default;
  [[nodiscard]] EdgeSelection all() const;
  [[nodiscard]] EdgeSelection selected() const;
  [[nodiscard]] EdgeSelection only(std::span<const EdgeKey> edges) const;
  [[nodiscard]] EdgeSelection boundary() const;
  [[nodiscard]] EdgeSelection hard() const;
  [[nodiscard]] EdgeSelection soft() const;

private:
  friend class MeshHandle;
  explicit MeshEdges(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides face selection scopes for one mesh.
 */
class MeshFaces {
public:
  MeshFaces() = default;
  [[nodiscard]] FaceSelection all() const;
  [[nodiscard]] FaceSelection selected() const;
  [[nodiscard]] FaceSelection only(std::span<const FaceId> faces) const;
  [[nodiscard]] FaceSelection by_material_slot(std::uint32_t slot) const;
  [[nodiscard]] FaceSelection by_normal(Vec3 direction,
                                        float tolerance = 0.01F) const;

private:
  friend class MeshHandle;
  explicit MeshFaces(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides object-level transform operations for one mesh.
 */
class MeshTransform {
public:
  MeshTransform() = default;
  [[nodiscard]] OperationReceipt translate(Vec3 delta);
  [[nodiscard]] OperationReceipt rotate(RotateOptions options);
  [[nodiscard]] OperationReceipt scale(ScaleOptions options);
  [[nodiscard]] OperationReceipt apply(TransformOptions options);
  [[nodiscard]] PreviewHandle preview(TransformOptions options = {});

private:
  friend class MeshHandle;
  explicit MeshTransform(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides material assignment operations for one mesh.
 */
class MeshMaterials {
public:
  MeshMaterials() = default;
  [[nodiscard]] OperationReceipt assign(MaterialId material);
  [[nodiscard]] OperationReceipt assign_slot(std::uint32_t slot);
  [[nodiscard]] OperationReceipt assign_slot(FaceSelection faces,
                                             std::uint32_t slot);

private:
  friend class MeshHandle;
  explicit MeshMaterials(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides explicit authored and compiled mesh payload materialization.
 */
class MeshPayloads {
public:
  MeshPayloads() = default;
  [[nodiscard]] AuthoredPolygonPayload authored_polygon() const;
  [[nodiscard]] MeshPayload compile_mesh(MeshCompileOptions options = {}) const;
  [[nodiscard]] MeshCompileResult
  compile_mesh_if_changed(std::uint64_t previous_revision,
                          MeshCompileOptions options = {}) const;

private:
  friend class MeshHandle;
  explicit MeshPayloads(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides validation for one mesh document.
 */
class MeshValidation {
public:
  MeshValidation() = default;
  [[nodiscard]] OperationReceipt validate() const;

private:
  friend class MeshHandle;
  explicit MeshValidation(MeshHandle mesh);
  MeshHandle mesh_;
};

} // namespace quader::modeling
