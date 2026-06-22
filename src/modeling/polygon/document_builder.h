////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <modeling/polygon/document.h>

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

    VertexRef vertex(Vec3 position);
    DocumentBuilder& vertices(std::span<const Vec3> positions);
    DocumentBuilder& face(std::span<const VertexRef> vertices, std::uint32_t materialSlot = 0);
    DocumentBuilder& faceByIndex(std::span<const std::size_t> vertices, std::uint32_t materialSlot = 0);
    DocumentBuilder& quad(VertexRef a, VertexRef b, VertexRef c, VertexRef d, std::uint32_t materialSlot = 0);
    DocumentBuilder& triangle(VertexRef a, VertexRef b, VertexRef c, std::uint32_t materialSlot = 0);

    DocumentBuilder& cube(Vec3 minBounds, Vec3 maxBounds, std::uint32_t materialSlot = 0);
    DocumentBuilder& boxFromCorners(std::span<const Vec3> corners, std::uint32_t materialSlot = 0);
    DocumentBuilder& translate(Vec3 delta);
    DocumentBuilder& scale(Vec3 scale, Vec3 pivot = {});
    DocumentBuilder& transform(const Transform3& transform, Vec3 pivot = {});
    DocumentBuilder& assignMaterialSlot(std::uint32_t materialSlot);
    DocumentBuilder& clear();

    [[nodiscard]] const Document& preview() const;
    [[nodiscard]] Document build() const;
    [[nodiscard]] Document take();

private:
    Document document_;
    std::vector<ElementId> vertexIds_;
};

} // namespace quader_poly
