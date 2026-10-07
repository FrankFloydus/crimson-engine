////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"
#include "assets/mesh_geometry_projection.hpp"

#include <editor/modeling/runtime/session.hpp>
#include <quader/modeling/mesh/polygon_document.hpp>
#include <quader/modeling/payloads.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace quader {

/** Immutable affected-face connectivity and compiled ranges for one gesture. */
struct MeshProjectionPositionUpdatePlan;
/** Retains canonical topology and direct authored-to-compiled vertex mappings. */
struct MeshRetainedPositionProjectionPlan;

/** Cumulative projection work and immutable gesture-plan preparation. */
struct MeshProjectionPositionUpdateStats {
  std::uint64_t retained_translation_count = 0;
  std::uint64_t retained_flat_face_count = 0;
  std::uint64_t retained_partial_count = 0;
  /** Includes neighborhood work followed by a full-layout fallback. */
  std::uint64_t partial_compiled_face_count = 0;
  std::uint64_t full_compile_count = 0;
  std::uint64_t partial_plan_build_count = 0;
  /** Face visits while constructing neighborhoods, excluding layout checks. */
  std::uint64_t partial_plan_face_visit_count = 0;
  std::uint64_t partial_plan_reuse_count = 0;
  std::uint64_t partial_plan_fallback_count = 0;
  std::uint64_t retained_stream_projection_count = 0;
  std::uint64_t retained_stream_fallback_count = 0;
};

/**
 * Builds derived mesh snapshots from runtime polygon mesh data.
 */
class MeshProjectionService final {
public:
  /** Converts a native affine sampling matrix to the neutral modeling form. */
  [[nodiscard]] static quader_poly::Transform3
  texture_mapping_transform(const QMat4 &matrix);
  [[nodiscard]] static MeshAsset
  mesh_asset_from_compiled_mesh(const quader_poly::CompiledMesh &compiled);
  [[nodiscard]] static MeshAsset
  compile_polygon_document_mesh(const quader_poly::Document &document);
  [[nodiscard]] static MeshAsset
  compile_object_mesh(const quader_editor_core::PolygonObject &object);
  /** Converts one native authored document at the host/SDK boundary. */
  [[nodiscard]] static quader::modeling::Result<quader::modeling::PolygonDocument>
  sdk_document_from_polygon_document(const quader_poly::Document &document);
  /** Copies SDK-authored topology without compiling render geometry. */
  [[nodiscard]] static std::optional<quader_poly::Document>
  polygon_document_from_sdk_document(
      const quader::modeling::PolygonDocument &document);
  /** Rebases UVs where representable, otherwise retains the source corner chart. */
  [[nodiscard]] static quader::modeling::Result<quader::modeling::PolygonDocument>
  rebase_sdk_document_texture_mapping(
      const quader_poly::Document &before,
      const quader::modeling::PolygonDocument &transformed,
      const QMat4 &source_sampling, const QMat4 &destination_sampling,
      bool reflected_geometry);
  /** Retains full-face n-gon interiors and planar charts for partial face edits. */
  [[nodiscard]] static quader::modeling::Result<quader::modeling::OperationResult>
  preserve_transformed_sdk_document_uv_layout(
      const quader::modeling::PolygonDocument &before,
      quader::modeling::PolygonDocument &transformed,
      bool whole_object = false);
  /** Projects one SDK document into native render and persistence payloads. */
  [[nodiscard]] static std::optional<MeshAsset>
  compile_sdk_document_mesh(const quader::modeling::PolygonDocument &document);
  /** Converts an SDK-owned compiled payload without recompiling topology. */
  [[nodiscard]] static std::optional<MeshAsset>
  mesh_asset_from_sdk_payload(
      const quader::modeling::MeshPayload &payload,
      const quader::modeling::PolygonDocument &authored_document);
  /**
   * Projects topology-retained positions into detached buffers. Flat faces and
   * exact whole-mesh translations with unchanged canonical face normals retain
   * their unaffected attributes. Partial edits compile the complete affected
   * normal neighborhood; unsupported layouts use the full compiler.
   */
  [[nodiscard]] static std::optional<MeshAsset>
  update_sdk_document_mesh_positions(
      const quader::modeling::PolygonDocument &document,
      const MeshAsset &current,
      const MeshProjectionPositionUpdatePlan *plan = nullptr);
  /**
   * Caches the complete normal neighborhood of a fixed movable vertex set.
   * Unsupported layouts are retained as ineligible plans; all updates still
   * validate their source/layout and use canonical compilation on mismatch.
   */
  [[nodiscard]] static std::shared_ptr<const MeshProjectionPositionUpdatePlan>
  prepare_position_update_plan(
      const MeshAsset &current,
      std::span<const std::uint32_t> movable_vertex_ids);
  /** Prepares immutable compilation input once for an SDK coordinate gesture. */
  [[nodiscard]] static std::shared_ptr<const MeshRetainedPositionProjectionPlan>
  prepare_retained_position_projection(
      const quader::modeling::PolygonDocument &document, const MeshAsset &current,
      const MeshProjectionPositionUpdatePlan *partial_plan = nullptr);
  /** Derives canonical streams without materializing an authored native document. */
  [[nodiscard]] static std::shared_ptr<const MeshGeometryProjection>
  project_retained_mesh_positions(
      const quader::modeling::PolygonDocument &document,
      const quader::modeling::PolygonDocument &previous_document,
      const MeshRetainedPositionProjectionPlan &plan,
      const std::shared_ptr<const MeshGeometryProjection> &previous = {},
      std::shared_ptr<std::vector<QVec3>> positions = {});
  /**
   * Applies the complete fixed vertex-update set of a prepared SDK transform
   * directly to gesture scratch and retained compiled ranges. Publications never
   * borrow the mutable scratch. A different update set/layout returns no result.
   * Full-face UV-preserving n-gons retain this path while triangle identities
   * match the source. Partial planar faces retain their source affine chart;
   * loop rebasing remains with the canonical fallback.
   */
  [[nodiscard]] static std::shared_ptr<const MeshGeometryProjection>
  project_retained_mesh_position_updates(
      std::span<const modeling::VertexPositionUpdate> updates,
      const MeshRetainedPositionProjectionPlan &plan,
      const std::shared_ptr<const MeshGeometryProjection> &previous = {},
      std::shared_ptr<std::vector<QVec3>> positions = {},
      bool preserve_preview_uv_layout = false);
  /**
   * Adopts accepted streams, reusing a family's authored pin only when its full
   * content exactly matches this object's accepted SDK document.
   */
  [[nodiscard]] static MeshAsset materialize_retained_mesh_projection(
      const quader::modeling::PolygonDocument &document, const MeshAsset &source,
      const MeshGeometryProjection &projection,
      const std::shared_ptr<const quader_poly::Document> &shared_authored = {});
  /** Pins accepted coordinates and authored topology without assembling copies. */
  [[nodiscard]] static MeshAsset materialize_retained_mesh_projection(
      const quader::modeling::PolygonDocument &document, const MeshAsset &source,
      std::shared_ptr<const MeshGeometryProjection> projection);
  /** Projects an exported authored document once, with canonical fallback. */
  [[nodiscard]] static MeshAsset update_polygon_document_mesh_positions(
      quader_poly::Document document, const MeshAsset &current,
      const MeshProjectionPositionUpdatePlan *plan = nullptr);
  /** Excludes separate compile calls and updates rejected before derivation. */
  [[nodiscard]] static MeshProjectionPositionUpdateStats
  position_update_stats() noexcept;
  /** Bakes occurrence sampling into a detached export-only compiled payload. */
  static void apply_mesh_texture_mapping(MeshAsset &mesh,
                                        const QMat4 &sampling_from_local);
  /** Reconstructs missing charts for legacy compiled-only geometry. */
  [[nodiscard]] static bool initialize_compiled_texture_mapping(MeshAsset &mesh);
  /** Bakes placement sampling into base UVs while preserving face settings. */
  [[nodiscard]] static bool bake_polygon_texture_mapping(
      quader_poly::Document &document, const QMat4 &sampling_from_local);
};

} // namespace quader
