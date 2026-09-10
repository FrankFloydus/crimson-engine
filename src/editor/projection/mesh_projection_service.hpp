////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"

#include <editor/modeling/runtime/session.hpp>
#include <quader/modeling/mesh/polygon_document.hpp>
#include <quader/modeling/payloads.hpp>

#include <optional>

namespace quader {

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
  /** Preserves displayed UV interiors while rebasing authored mesh geometry. */
  [[nodiscard]] static quader::modeling::Result<quader::modeling::PolygonDocument>
  rebase_sdk_document_texture_mapping(
      const quader_poly::Document &before,
      const quader::modeling::PolygonDocument &transformed,
      const QMat4 &source_sampling, const QMat4 &destination_sampling,
      bool reflected_geometry);
  /** Repairs complete n-gon charts after a nonsingular affine edit at acceptance. */
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
   * Canonically recompiles a topology-retained committed transform into the
   * current asset's detached buffers. Every derived vertex attribute and index
   * is refreshed; only vector allocations and unchanged authored layout are
   * retained.
   */
  [[nodiscard]] static std::optional<MeshAsset>
  update_sdk_document_mesh_positions(
      const quader::modeling::PolygonDocument &document,
      const MeshAsset &current);
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
