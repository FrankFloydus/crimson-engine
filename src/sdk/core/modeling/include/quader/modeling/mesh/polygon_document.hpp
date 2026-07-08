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
#include <vector>

namespace quader::modeling {

struct PolygonDocumentImpl;
class PolygonDocumentNativeAccess;

/**
 * Enumerates SDK polygon component selection modes.
 */
enum class SelectionMode {
  Vertex,
  Edge,
  Face,
};

/**
 * Defines the bounds and material used to create a box document.
 */
struct BoxSpec {
  Vec3 min{-1.0F, -1.0F, -1.0F};
  Vec3 max{1.0F, 1.0F, 1.0F};
  MaterialId material{};
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

  [[nodiscard]] std::size_t vertex_count() const;
  [[nodiscard]] std::size_t face_count() const;
  [[nodiscard]] std::uint64_t content_revision() const;
  [[nodiscard]] std::uint64_t selection_revision() const;
  [[nodiscard]] std::vector<VertexId> vertex_ids() const;
  [[nodiscard]] std::vector<FaceId> face_ids() const;
  [[nodiscard]] Result<AuthoredPolygonPayload> authored_payload() const;
  [[nodiscard]] Result<MeshPayload> compile_mesh() const;
  [[nodiscard]] Result<OperationResult> validate() const;

  [[nodiscard]] Result<OperationResult> set_selection_mode(SelectionMode mode);
  [[nodiscard]] Result<OperationResult> clear_selection();
  [[nodiscard]] Result<OperationResult> select_vertex(VertexId vertex_id);
  [[nodiscard]] Result<OperationResult> select_edge(EdgeKey edge);
  [[nodiscard]] Result<OperationResult> select_face(FaceId face_id);
  [[nodiscard]] Result<OperationResult> select_all_faces();
  [[nodiscard]] Result<OperationResult> translate_selection(Vec3 delta);

private:
  friend class PolygonDocumentNativeAccess;

  explicit PolygonDocument(std::unique_ptr<PolygonDocumentImpl> impl);

  std::unique_ptr<PolygonDocumentImpl> impl_;
};

} // namespace quader::modeling
