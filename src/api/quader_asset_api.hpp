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

    QuaderApiResult<QuaderAssetRef> add_material(QuaderMaterialAsset material);
    QuaderApiResult<QuaderAssetRef>
    add_material(const QuaderMaterialArgs &args);
    QuaderApiResult<QuaderAssetRef> add_texture(QuaderTextureAsset texture);
    QuaderApiResult<QuaderAssetRef> add_standalone_mesh(QuaderMeshAsset mesh);

    QuaderApiVoidResult replace_standalone_mesh(AssetId mesh,
                                                QuaderMeshAsset replacement);
    QuaderApiVoidResult replace_material(AssetId material,
                                         QuaderMaterialAsset replacement);
    QuaderApiVoidResult replace_texture(AssetId texture,
                                        QuaderTextureAsset replacement);

    QuaderApiVoidResult set_material_base_color(AssetId material, QVec3 color);
    QuaderApiVoidResult set_material_roughness(AssetId material,
                                               float roughness);
    QuaderApiVoidResult set_material_metallic(AssetId material, float metallic);
    QuaderApiVoidResult set_material_texture(AssetId material, std::string slot,
                                             AssetId texture);

    QuaderApiVoidResult assign_material_to_entity(EntityId entity,
                                                  AssetId material);
    QuaderApiVoidResult
    assign_material_to_object(QuaderModelingObjectRef object, AssetId material);

  private:
    QuaderApiSession session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
