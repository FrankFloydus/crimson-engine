////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/render_signature_builder.hpp"

#include <bit>
#include <functional>
#include <optional>
#include <string>
#include <algorithm>
#include <vector>

namespace quader {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

} // namespace

RenderSignatureBuilder::RenderSignatureBuilder()
    : hash_(kFnvOffset) {}

RenderSignatureBuilder::RenderSignatureBuilder(std::uint64_t seed)
    : hash_(seed) {}

RenderSignatureBuilder& RenderSignatureBuilder::addValue(std::uint64_t value) {
    hash_ ^= value;
    hash_ *= kFnvPrime;
    return *this;
}

RenderSignatureBuilder& RenderSignatureBuilder::addSize(std::size_t value) {
    return addValue(static_cast<std::uint64_t>(value));
}

RenderSignatureBuilder& RenderSignatureBuilder::addBool(bool value) {
    return addValue(value ? 1u : 0u);
}

RenderSignatureBuilder& RenderSignatureBuilder::addFloat(float value) {
    return addValue(std::bit_cast<std::uint32_t>(value));
}

RenderSignatureBuilder& RenderSignatureBuilder::addString(std::string_view value) {
    return addValue(std::hash<std::string>{}(std::string(value)));
}

RenderSignatureBuilder& RenderSignatureBuilder::addVec2(QVec2 value) {
    addFloat(value.x);
    return addFloat(value.y);
}

RenderSignatureBuilder& RenderSignatureBuilder::addVec3(QVec3 value) {
    addFloat(value.x);
    addFloat(value.y);
    return addFloat(value.z);
}

RenderSignatureBuilder& RenderSignatureBuilder::addMat4(const QMat4& value) {
    for (const float entry : value.values) {
        addFloat(entry);
    }
    return *this;
}

namespace {

void addMaterialDocumentSignature(RenderSignatureBuilder& signature, const AssetRegistry& assets, AssetId materialId) {
    const MaterialDocument* material = assets.material(materialId);
    signature.addValue(materialId);
    if (!material) {
        signature.addValue(0);
        return;
    }

    signature.addValue(material->revision.value);
    for (const MaterialTextureSlotDescriptor& descriptor : materialTextureSlotDescriptors()) {
        const std::optional<AssetId> textureId = materialTextureSlotAsset(*material, descriptor.id);
        if (!textureId.has_value()) {
            continue;
        }
        signature.addString(descriptor.id)
                .addValue(*textureId)
                .addValue(assets.textureRevision(*textureId).value);
    }
}

} // namespace

std::uint64_t RenderSignatureBuilder::materialSignature(
        const AssetRegistry& assets,
        const MaterialComponent* material) {
    if (!material) {
        return 0;
    }

    RenderSignatureBuilder signature;
    addMaterialDocumentSignature(signature, assets, material->material);
    signature.addSize(material->materialSlots.size());
    std::vector<std::uint32_t> slots;
    slots.reserve(material->materialSlots.size());
    for (const auto& [slot, materialId] : material->materialSlots) {
        (void)materialId;
        slots.push_back(slot);
    }
    std::sort(slots.begin(), slots.end());
    for (const std::uint32_t slot : slots) {
        signature.addValue(slot);
        addMaterialDocumentSignature(signature, assets, material->materialSlots.at(slot));
    }
    return signature.value();
}

std::uint64_t RenderSignatureBuilder::meshAssetSignature(std::uint64_t seed, const MeshAsset& mesh) {
    RenderSignatureBuilder signature(seed);
    signature.addSize(mesh.vertices.size());
    for (const MeshVertex& vertex : mesh.vertices) {
        signature.addVec3(vertex.position)
                .addVec3(vertex.normal)
                .addVec2(vertex.uv)
                .addValue(vertex.rgba);
    }
    signature.addSize(mesh.indices.size());
    for (const std::uint32_t index : mesh.indices) {
        signature.addValue(index);
    }
    const std::vector<MeshPrimitiveSpan> primitiveSpans = normalizedMeshPrimitiveSpans(mesh);
    signature.addSize(primitiveSpans.size());
    for (const MeshPrimitiveSpan& primitive : primitiveSpans) {
        signature.addValue(primitive.indexOffset)
                .addValue(primitive.indexCount)
                .addValue(primitive.materialSlot);
    }
    return signature.value();
}

std::uint64_t RenderSignatureBuilder::lightSignature(const LightComponent& light) {
    RenderSignatureBuilder signature;
    signature.addValue(static_cast<std::uint32_t>(light.type))
            .addVec3(light.color)
            .addFloat(light.intensity)
            .addBool(light.castsShadows);
    return signature.value();
}

std::uint64_t RenderSignatureBuilder::lightTransformSignature(const QMat4& worldTransform) {
    RenderSignatureBuilder signature;
    signature.addMat4(worldTransform);
    return signature.value();
}

std::uint64_t RenderSignatureBuilder::overlayFrameSignature(const ViewportOverlayFrame& frame) {
    RenderSignatureBuilder signature;
    signature.addSize(frame.triangles.size());
    for (const ViewportOverlayTriangle& triangle : frame.triangles) {
        signature.addValue(static_cast<std::uint32_t>(triangle.layer))
                .addValue(triangle.color)
                .addVec3(triangle.a)
                .addVec3(triangle.b)
                .addVec3(triangle.c);
    }
    signature.addSize(frame.lines.size());
    for (const ViewportOverlayLine& line : frame.lines) {
        signature.addValue(static_cast<std::uint32_t>(line.layer))
                .addValue(line.color)
                .addFloat(line.widthPixels)
                .addVec3(line.start)
                .addVec3(line.end);
    }
    signature.addSize(frame.points.size());
    for (const ViewportOverlayPoint& point : frame.points) {
        signature.addValue(static_cast<std::uint32_t>(point.layer))
                .addValue(point.color)
                .addFloat(point.sizePixels)
                .addBool(point.outlineGrowthPixels.has_value());
        if (point.outlineGrowthPixels.has_value()) {
            signature.addFloat(*point.outlineGrowthPixels);
        }
        signature.addVec3(point.position);
    }
    return signature.value();
}

std::uint64_t RenderSignatureBuilder::overlayCameraSignature(const ViewportOverlayCamera& camera) {
    RenderSignatureBuilder signature;
    signature.addValue(static_cast<std::uint32_t>(camera.projection))
            .addVec3(camera.eye)
            .addVec3(camera.forward)
            .addVec3(camera.right)
            .addVec3(camera.up)
            .addVec2(camera.viewportSize)
            .addFloat(camera.orthographicSize)
            .addFloat(camera.fovDegrees)
            .addFloat(camera.nearPlane)
            .addFloat(camera.farPlane);
    return signature.value();
}

std::uint64_t RenderSignatureBuilder::overlayLayerSignature(
        const ViewportOverlayFrame& frame,
        ViewportOverlayLayer layer) {
    RenderSignatureBuilder signature;
    signature.addValue(static_cast<std::uint32_t>(layer));
    for (const ViewportOverlayTriangle& triangle : frame.triangles) {
        if (triangle.layer != layer) {
            continue;
        }
        signature.addValue(static_cast<std::uint32_t>(triangle.layer))
                .addValue(triangle.color)
                .addVec3(triangle.a)
                .addVec3(triangle.b)
                .addVec3(triangle.c);
    }
    for (const ViewportOverlayLine& line : frame.lines) {
        if (line.layer != layer) {
            continue;
        }
        signature.addValue(static_cast<std::uint32_t>(line.layer))
                .addValue(line.color)
                .addFloat(line.widthPixels)
                .addVec3(line.start)
                .addVec3(line.end);
    }
    for (const ViewportOverlayPoint& point : frame.points) {
        if (point.layer != layer) {
            continue;
        }
        signature.addValue(static_cast<std::uint32_t>(point.layer))
                .addValue(point.color)
                .addFloat(point.sizePixels)
                .addBool(point.outlineGrowthPixels.has_value());
        if (point.outlineGrowthPixels.has_value()) {
            signature.addFloat(*point.outlineGrowthPixels);
        }
        signature.addVec3(point.position);
    }
    return signature.value();
}

} // namespace quader
