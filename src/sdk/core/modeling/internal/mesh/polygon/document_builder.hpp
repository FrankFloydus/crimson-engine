////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace quader_poly {

/**
 * Builds Document Builder data for the polygon document and mesh editing core.
 */
class DocumentBuilder final {
public:
    /**
     * Represents a Vertex Ref value used by the polygon document and mesh editing core.
     */
    struct VertexRef {
        std::size_t index = 0;
    };

    DocumentBuilder() = default;

    VertexRef vertex(quader::QVec3 position);
    DocumentBuilder& vertices(std::span<const quader::QVec3> positions);
    DocumentBuilder &face(std::span<const VertexRef> vertices,
                          std::uint32_t material_slot = 0);
    DocumentBuilder &face_by_index(std::span<const std::size_t> vertices,
                                   std::uint32_t material_slot = 0);
    DocumentBuilder &quad(VertexRef a, VertexRef b, VertexRef c, VertexRef d,
                          std::uint32_t material_slot = 0);
    DocumentBuilder &triangle(VertexRef a, VertexRef b, VertexRef c,
                              std::uint32_t material_slot = 0);

    DocumentBuilder &cube(quader::QVec3 min_bounds, quader::QVec3 max_bounds,
                          std::uint32_t material_slot = 0);
    DocumentBuilder &box_from_corners(std::span<const quader::QVec3> corners,
                                      std::uint32_t material_slot = 0);
    DocumentBuilder& translate(quader::QVec3 delta);
    DocumentBuilder& scale(quader::QVec3 scale, quader::QVec3 pivot = {});
    DocumentBuilder& transform(const Transform3& transform, quader::QVec3 pivot = {});
    DocumentBuilder &assign_material_slot(std::uint32_t material_slot);
    DocumentBuilder& clear();

    [[nodiscard]] const Document& preview() const;
    [[nodiscard]] Document build() const;
    [[nodiscard]] Document take();

private:
    Document document_;
    std::vector<ElementId> vertex_ids_;
};

} // namespace quader_poly
