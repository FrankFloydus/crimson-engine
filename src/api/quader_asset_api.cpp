////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_asset_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_api_mutation_runner.hpp"
#include "scene/document/project_session.hpp"

#include <functional>
#include <utility>

namespace quader {
namespace {

ProjectSession& projectSession(QuaderApiSession session) {
    return QuaderApiSessionInternalAccess::projectSession(session);
}


bool sameVec3(QVec3 left, QVec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

QuaderApiResult<QuaderAssetRef> assetResult(
        QuaderApiMutationRunner& mutations,
        AssetId id,
        QuaderAssetKind kind) {
    const QuaderApiVoidResult status = mutations.runAssetResult(true, "Asset mutation");
    return quaderApiResultFromStatus(status, QuaderAssetRef{.id = id, .kind = kind});
}

QuaderApiResult<QuaderAssetRef> assetFailureResult(
        QuaderApiMutationRunner& mutations,
        std::string message) {
    const QuaderApiVoidResult status = mutations.runAssetFailure(std::move(message));
    return quaderApiResultFromStatus<QuaderAssetRef>(status);
}

QuaderApiVoidResult updateMaterialOrFail(
        ProjectSession& session,
        QuaderApiMutationRunner& mutations,
        AssetId material,
        const std::function<bool(const MaterialDocument&)>& shouldChange,
        const std::function<void(MaterialDocument&)>& update) {
    const MaterialDocument* document = session.assets().material(material);
    if (document == nullptr) {
        return mutations.runAssetFailure("Material asset does not exist");
    }
    if (!shouldChange(*document)) {
        return mutations.runAssetResult(false, "Material unchanged");
    }
    MaterialDocument updated = *document;
    update(updated);
    const AssetRegistryValidationResult validation = session.assets().validateMaterial(updated);
    if (!validation.ok) {
        return mutations.runAssetFailure(validation.message);
    }
    if (!session.assets().replaceMaterial(material, std::move(updated))) {
        return mutations.runAssetFailure("Material asset does not exist");
    }
    return mutations.runAssetResult(true, "Material updated");
}

} // namespace

QuaderAssetApi::QuaderAssetApi(QuaderApiSession session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiResult<QuaderAssetRef> QuaderAssetApi::addMaterial(QuaderMaterialAsset material) {
    MaterialDocument document = toMaterialDocument(material);
    const AssetRegistryValidationResult validation = projectSession(session_).assets().validateMaterial(document);
    if (!validation.ok) {
        return assetFailureResult(mutations_, validation.message);
    }
    const AssetId id = projectSession(session_).assets().addMaterial(std::move(document));
    if (id == InvalidAssetId) {
        return assetFailureResult(mutations_, "Material asset could not be added");
    }
    return assetResult(mutations_, id, QuaderAssetKind::Material);
}

QuaderApiResult<QuaderAssetRef> QuaderAssetApi::addMaterial(const QuaderMaterialArgs& args) {
    QuaderMaterialAsset material;
    material.name = args.name;
    material.baseColor = args.baseColor;
    material.roughness = args.roughness;
    material.metallic = args.metallic;
    return addMaterial(std::move(material));
}

QuaderApiResult<QuaderAssetRef> QuaderAssetApi::addTexture(QuaderTextureAsset texture) {
    const AssetId id = projectSession(session_).assets().addTexture(toTextureAsset(texture));
    return assetResult(mutations_, id, QuaderAssetKind::Texture);
}

QuaderApiResult<QuaderAssetRef> QuaderAssetApi::addStandaloneMesh(QuaderMeshAsset mesh) {
    const AssetId id = projectSession(session_).assets().addMesh(toMeshAsset(mesh));
    return assetResult(mutations_, id, QuaderAssetKind::Mesh);
}

QuaderApiVoidResult QuaderAssetApi::replaceStandaloneMesh(AssetId mesh, QuaderMeshAsset replacement) {
    const ProjectAssetMutationResult result =
            projectSession(session_).replaceStandaloneMesh(mesh, toMeshAsset(replacement));
    if (!result.ok) {
        return mutations_.runAssetFailure(result.message);
    }
    return mutations_.runAssetResult(result.changed, result.message);
}

QuaderApiVoidResult QuaderAssetApi::replaceMaterial(AssetId material, QuaderMaterialAsset replacement) {
    MaterialDocument document = toMaterialDocument(replacement);
    const AssetRegistryValidationResult validation = projectSession(session_).assets().validateMaterial(document);
    if (!validation.ok) {
        return mutations_.runAssetFailure(validation.message);
    }
    if (!projectSession(session_).assets().replaceMaterial(material, std::move(document))) {
        return mutations_.runAssetFailure("Material asset does not exist");
    }
    return mutations_.runAssetResult(true, "Material replaced");
}

QuaderApiVoidResult QuaderAssetApi::replaceTexture(AssetId texture, QuaderTextureAsset replacement) {
    if (!projectSession(session_).assets().replaceTexture(texture, toTextureAsset(replacement))) {
        return mutations_.runAssetFailure("Texture asset does not exist");
    }
    return mutations_.runAssetResult(true, "Texture replaced");
}

QuaderApiVoidResult QuaderAssetApi::setMaterialBaseColor(AssetId material, QVec3 color) {
    return updateMaterialOrFail(
            projectSession(session_),
            mutations_,
            material,
            [color](const MaterialDocument& document) {
                return !sameVec3(document.baseColor, color);
            },
            [color](MaterialDocument& document) {
        document.baseColor = color;
            });
}

QuaderApiVoidResult QuaderAssetApi::setMaterialRoughness(AssetId material, float roughness) {
    return updateMaterialOrFail(
            projectSession(session_),
            mutations_,
            material,
            [roughness](const MaterialDocument& document) {
                return document.roughness != roughness;
            },
            [roughness](MaterialDocument& document) {
        document.roughness = roughness;
            });
}

QuaderApiVoidResult QuaderAssetApi::setMaterialMetallic(AssetId material, float metallic) {
    return updateMaterialOrFail(
            projectSession(session_),
            mutations_,
            material,
            [metallic](const MaterialDocument& document) {
                return document.metallic != metallic;
            },
            [metallic](MaterialDocument& document) {
        document.metallic = metallic;
            });
}

QuaderApiVoidResult QuaderAssetApi::setMaterialTexture(AssetId material, std::string slot, AssetId texture) {
    const AssetRegistryMutationResult result =
            projectSession(session_).assets().setMaterialTexture(material, slot, texture);
    if (!result.ok) {
        return mutations_.runAssetFailure(result.message);
    }
    return mutations_.runAssetResult(result.changed, result.message);
}

QuaderApiVoidResult QuaderAssetApi::assignMaterialToEntity(EntityId entity, AssetId material) {
    return mutations_.runDocumentResult(
            projectSession(session_).documentCommands().setMaterial(entity, material),
            "Assign Material");
}

QuaderApiVoidResult QuaderAssetApi::assignMaterialToObject(QuaderModelingObjectRef object, AssetId material) {
    if (object.entity == InvalidEntityId) {
        return mutations_.runAssetFailure("Modeling object entity is invalid");
    }
    return assignMaterialToEntity(object.entity, material);
}

} // namespace quader
