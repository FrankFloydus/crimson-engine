#pragma once

#include "api/QuaderApiResult.h"
#include "assets/MaterialDocument.h"
#include "assets/MeshAsset.h"
#include "assets/TextureAsset.h"

#include <string>

namespace quader {

class ProjectSession;
class QuaderApiMutationRunner;

class QuaderAssetApi final {
public:
    QuaderAssetApi(ProjectSession& session, QuaderApiMutationRunner& mutations);

    QuaderApiResult<QuaderAssetRef> addMaterial(MaterialDocument material);
    QuaderApiResult<QuaderAssetRef> addMaterial(const QuaderMaterialArgs& args);
    QuaderApiResult<QuaderAssetRef> addTexture(TextureAsset texture);
    QuaderApiResult<QuaderAssetRef> addStandaloneMesh(MeshAsset mesh);

    QuaderApiVoidResult replaceStandaloneMesh(AssetId mesh, MeshAsset replacement);
    QuaderApiVoidResult replaceMaterial(AssetId material, MaterialDocument replacement);
    QuaderApiVoidResult replaceTexture(AssetId texture, TextureAsset replacement);

    QuaderApiVoidResult setMaterialBaseColor(AssetId material, Vec3 color);
    QuaderApiVoidResult setMaterialRoughness(AssetId material, float roughness);
    QuaderApiVoidResult setMaterialMetallic(AssetId material, float metallic);
    QuaderApiVoidResult setMaterialTexture(AssetId material, std::string slot, AssetId texture);

    QuaderApiVoidResult assignMaterialToEntity(EntityId entity, AssetId material);
    QuaderApiVoidResult assignMaterialToObject(QuaderModelingObjectRef object, AssetId material);

private:
    ProjectSession& session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
