////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderAssetApi.h"

#include "api/QuaderApiMutationRunner.h"
#include "scene/document/ProjectSession.h"

#include <functional>
#include <optional>
#include <utility>

namespace quader {
namespace {

bool sameVec3(QVec3 left, QVec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

QuaderApiResult<QuaderAssetRef> assetResult(
        QuaderApiMutationRunner& mutations,
        AssetId id,
        QuaderAssetKind kind) {
    QuaderApiResult<QuaderAssetRef> result;
    const QuaderApiVoidResult status = mutations.runAssetResult(true, "Asset mutation");
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = status.message;
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    result.value = QuaderAssetRef{.id = id, .kind = kind};
    return result;
}

QuaderApiResult<QuaderAssetRef> assetFailureResult(
        QuaderApiMutationRunner& mutations,
        std::string message) {
    QuaderApiResult<QuaderAssetRef> result;
    const QuaderApiVoidResult status = mutations.runAssetFailure(std::move(message));
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = status.message;
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    return result;
}

std::optional<std::string> materialTextureReferenceError(
        const AssetRegistry& assets,
        const MaterialDocument& material) {
    for (const auto& [slot, texture] : material.textureSlots) {
        if (slot.empty()) {
            return "Material texture slot is empty";
        }
        if (texture == InvalidAssetId || assets.texture(texture) == nullptr) {
            return "Texture asset does not exist";
        }
    }
    return std::nullopt;
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
    if (!session.assets().updateMaterial(material, update)) {
        return mutations.runAssetFailure("Material asset does not exist");
    }
    return mutations.runAssetResult(true, "Material updated");
}

} // namespace

QuaderAssetApi::QuaderAssetApi(ProjectSession& session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiResult<QuaderAssetRef> QuaderAssetApi::addMaterial(MaterialDocument material) {
    if (const std::optional<std::string> error = materialTextureReferenceError(session_.assets(), material)) {
        return assetFailureResult(mutations_, *error);
    }
    const AssetId id = session_.assets().addMaterial(std::move(material));
    return assetResult(mutations_, id, QuaderAssetKind::Material);
}

QuaderApiResult<QuaderAssetRef> QuaderAssetApi::addMaterial(const QuaderMaterialArgs& args) {
    MaterialDocument material;
    material.name = args.name;
    material.baseColor = args.baseColor;
    material.roughness = args.roughness;
    material.metallic = args.metallic;
    return addMaterial(std::move(material));
}

QuaderApiResult<QuaderAssetRef> QuaderAssetApi::addTexture(TextureAsset texture) {
    const AssetId id = session_.assets().addTexture(std::move(texture));
    return assetResult(mutations_, id, QuaderAssetKind::Texture);
}

QuaderApiResult<QuaderAssetRef> QuaderAssetApi::addStandaloneMesh(MeshAsset mesh) {
    const AssetId id = session_.assets().addMesh(std::move(mesh));
    return assetResult(mutations_, id, QuaderAssetKind::Mesh);
}

QuaderApiVoidResult QuaderAssetApi::replaceStandaloneMesh(AssetId mesh, MeshAsset replacement) {
    if (mesh == InvalidAssetId) {
        return mutations_.runAssetFailure("Mesh asset is invalid");
    }
    for (const EntityId entity : session_.scene().entities()) {
        const MeshComponent* meshComponent = session_.scene().mesh(entity);
        if (meshComponent == nullptr || meshComponent->mesh != mesh) {
            continue;
        }
        if (session_.scene().modelingObject(entity) != nullptr) {
            return mutations_.runAssetFailure("Mesh asset is owned by a modeling object");
        }
    }
    if (!session_.assets().replaceMesh(mesh, std::move(replacement))) {
        return mutations_.runAssetFailure("Mesh asset does not exist");
    }
    return mutations_.runAssetResult(true, "Mesh replaced");
}

QuaderApiVoidResult QuaderAssetApi::replaceMaterial(AssetId material, MaterialDocument replacement) {
    if (const std::optional<std::string> error = materialTextureReferenceError(session_.assets(), replacement)) {
        return mutations_.runAssetFailure(*error);
    }
    if (!session_.assets().replaceMaterial(material, std::move(replacement))) {
        return mutations_.runAssetFailure("Material asset does not exist");
    }
    return mutations_.runAssetResult(true, "Material replaced");
}

QuaderApiVoidResult QuaderAssetApi::replaceTexture(AssetId texture, TextureAsset replacement) {
    if (!session_.assets().replaceTexture(texture, std::move(replacement))) {
        return mutations_.runAssetFailure("Texture asset does not exist");
    }
    return mutations_.runAssetResult(true, "Texture replaced");
}

QuaderApiVoidResult QuaderAssetApi::setMaterialBaseColor(AssetId material, QVec3 color) {
    return updateMaterialOrFail(
            session_,
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
            session_,
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
            session_,
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
    if (slot.empty()) {
        return mutations_.runAssetFailure("Material texture slot is empty");
    }
    if (texture == InvalidAssetId || session_.assets().texture(texture) == nullptr) {
        return mutations_.runAssetFailure("Texture asset does not exist");
    }

    const std::string textureSlot = std::move(slot);
    return updateMaterialOrFail(
            session_,
            mutations_,
            material,
            [textureSlot, texture](const MaterialDocument& document) {
                const auto existing = document.textureSlots.find(textureSlot);
                return existing == document.textureSlots.end() || existing->second != texture;
            },
            [textureSlot, texture](MaterialDocument& document) {
                document.textureSlots[textureSlot] = texture;
            });
}

QuaderApiVoidResult QuaderAssetApi::assignMaterialToEntity(EntityId entity, AssetId material) {
    if (session_.assets().material(material) == nullptr) {
        return mutations_.runAssetFailure("Material asset does not exist");
    }
    return mutations_.runDocumentResult(
            session_.documentCommands().setMaterial(entity, material),
            "Assign Material");
}

QuaderApiVoidResult QuaderAssetApi::assignMaterialToObject(QuaderModelingObjectRef object, AssetId material) {
    if (object.entity == InvalidEntityId) {
        return mutations_.runAssetFailure("Modeling object entity is invalid");
    }
    return assignMaterialToEntity(object.entity, material);
}

} // namespace quader
