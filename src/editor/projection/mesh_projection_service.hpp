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

#include <optional>

namespace quader {

/**
 * Builds derived mesh snapshots from runtime polygon mesh data.
 */
class MeshProjectionService final {
public:
  [[nodiscard]] static MeshAsset
  mesh_asset_from_compiled_mesh(const quader_poly::CompiledMesh &compiled);
  [[nodiscard]] static MeshAsset
  compile_polygon_document_mesh(const quader_poly::Document &document);
  [[nodiscard]] static MeshAsset
  compile_object_mesh(const quader_editor_core::PolygonObject &object);
  /** Converts one native authored document at the host/SDK boundary. */
  [[nodiscard]] static quader::modeling::Result<quader::modeling::PolygonDocument>
  sdk_document_from_polygon_document(const quader_poly::Document &document);
  /** Projects one SDK document into native render and persistence payloads. */
  [[nodiscard]] static std::optional<MeshAsset>
  compile_sdk_document_mesh(const quader::modeling::PolygonDocument &document);
};

} // namespace quader
