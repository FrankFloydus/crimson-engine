////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"

#include <string>

namespace quader {

class QuaderApiMutationRunner;

/**
 * Exposes Quader Asset API operations through the public Quader API surface.
 */
class QuaderAssetApi final {
public:
    QuaderAssetApi(QuaderApiSession session, QuaderApiMutationRunner& mutations);

    QuaderApiResult<QuaderAssetRef> addMaterial(QuaderMaterialAsset material);
    QuaderApiResult<QuaderAssetRef> addMaterial(const QuaderMaterialArgs& args);
    QuaderApiResult<QuaderAssetRef> addTexture(QuaderTextureAsset texture);
    QuaderApiResult<QuaderAssetRef> addStandaloneMesh(QuaderMeshAsset mesh);

    QuaderApiVoidResult replaceStandaloneMesh(AssetId mesh, QuaderMeshAsset replacement);
    QuaderApiVoidResult replaceMaterial(AssetId material, QuaderMaterialAsset replacement);
    QuaderApiVoidResult replaceTexture(AssetId texture, QuaderTextureAsset replacement);

    QuaderApiVoidResult setMaterialBaseColor(AssetId material, QVec3 color);
    QuaderApiVoidResult setMaterialRoughness(AssetId material, float roughness);
    QuaderApiVoidResult setMaterialMetallic(AssetId material, float metallic);
    QuaderApiVoidResult setMaterialTexture(AssetId material, std::string slot, AssetId texture);

    QuaderApiVoidResult assignMaterialToEntity(EntityId entity, AssetId material);
    QuaderApiVoidResult assignMaterialToObject(QuaderModelingObjectRef object, AssetId material);

private:
    QuaderApiSession session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
