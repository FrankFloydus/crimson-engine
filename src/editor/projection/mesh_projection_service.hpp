////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"

#include <editor/modeling/runtime/session.hpp>

namespace quader {

/**
 * Builds derived mesh snapshots from runtime polygon mesh data.
 */
class MeshProjectionService final {
public:
    [[nodiscard]] static MeshAsset meshAssetFromCompiledMesh(const quader_poly::CompiledMesh& compiled);
    [[nodiscard]] static MeshAsset compilePolygonDocumentMesh(const quader_poly::Document& document);
    [[nodiscard]] static MeshAsset compileObjectMesh(const quader_editor_core::PolygonObject& object);
};

} // namespace quader
