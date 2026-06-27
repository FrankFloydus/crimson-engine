////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "assets/material_document.hpp"
#include "assets/mesh_asset.hpp"
#include "math/types.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "scene/components.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace quader {

/**
 * Builds stable render cache signatures for render extraction and proxy invalidation.
 */
class RenderSignatureBuilder final {
public:
    RenderSignatureBuilder();
    explicit RenderSignatureBuilder(std::uint64_t seed);

    RenderSignatureBuilder& addValue(std::uint64_t value);
    RenderSignatureBuilder& addSize(std::size_t value);
    RenderSignatureBuilder& addBool(bool value);
    RenderSignatureBuilder& addFloat(float value);
    RenderSignatureBuilder& addString(std::string_view value);
    RenderSignatureBuilder& addVec2(QVec2 value);
    RenderSignatureBuilder& addVec3(QVec3 value);
    RenderSignatureBuilder& addMat4(const QMat4& value);

    [[nodiscard]] std::uint64_t value() const {
        return hash_;
    }

    [[nodiscard]] static std::uint64_t materialSignature(
            const AssetRegistry& assets,
            const MaterialComponent* material);
    [[nodiscard]] static std::uint64_t meshAssetSignature(
            std::uint64_t seed,
            const MeshAsset& mesh);
    [[nodiscard]] static std::uint64_t lightSignature(const LightComponent& light);
    [[nodiscard]] static std::uint64_t lightTransformSignature(const QMat4& worldTransform);
    [[nodiscard]] static std::uint64_t overlayFrameSignature(const ViewportOverlayFrame& frame);
    [[nodiscard]] static std::uint64_t overlayCameraSignature(const ViewportOverlayCamera& camera);
    [[nodiscard]] static std::uint64_t overlayLayerSignature(
            const ViewportOverlayFrame& frame,
            ViewportOverlayLayer layer);

private:
    std::uint64_t hash_ = 0;
};

} // namespace quader
