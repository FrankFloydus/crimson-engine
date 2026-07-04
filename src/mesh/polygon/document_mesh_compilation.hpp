////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document_model.hpp>

#include <cstdint>
#include <vector>

namespace quader_poly {

/**
 * Represents a Compiled Vertex value used by the polygon document and mesh editing core.
 */
struct CompiledVertex {
    quader::QVec3 position;
    quader::QVec3 normal { 0.0F, 1.0F, 0.0F };
    quader::QVec2 uv0;
    std::uint32_t color = 0xff9f9f9f;
};

/**
 * Represents a Compiled Primitive value used by the polygon document and mesh editing core.
 */
struct CompiledPrimitive {
    std::uint32_t index_offset = 0;
    std::uint32_t index_count = 0;
    std::uint32_t material_slot = 0;
};

/**
 * Represents a Compiled Mesh value used by the polygon document and mesh editing core.
 */
struct CompiledMesh {
    std::vector<CompiledVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<CompiledPrimitive> primitives;
};

[[nodiscard]] CompiledMesh
compile_document(const Document &document,
                 SurfaceShading shading = SurfaceShading::Authored);

} // namespace quader_poly
