////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_document_reader.hpp"

#include "assets/material_document.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "io/qdr/qdr_format.hpp"
#include "io/qdr/qdr_polygon_document_codec.hpp"
#include "io/qdr/qdr_project_codecs.hpp"
#include "io/qdr/qdr_root.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <io/qdr/qdr_document.hpp>
#include <mesh/polygon/document.hpp>

namespace quader {
namespace {

using json = nlohmann::ordered_json;

void requireObject(const json& value, std::string_view label) {
    if (!value.is_object()) {
        throw std::runtime_error(std::string(label) + " must be an object.");
    }
}

QVec3 fromRuntime(quader::QVec3 value) {
    return {value.x, value.y, value.z};
}

MeshAsset meshFromPolygonDocument(const quader_poly::Document& document, std::string name) {
    MeshAsset mesh = MeshProjectionService::compilePolygonDocumentMesh(document);
    mesh.name = std::move(name);
    mesh.hasAuthoredPolygonDocument = true;
    mesh.authoredPolygonDocument = std::make_shared<quader_poly::Document>(document);
    return mesh;
}

MeshAsset meshFromJson(const json& value) {
    MeshAsset mesh;
    mesh.name = value.value("name", std::string("Mesh"));
    mesh.hasStableCpuTopology = value.value("hasStableCpuTopology", true);
    for (const json& vertexJson : value.value("vertices", json::array())) {
        mesh.vertices.push_back(MeshVertex{
                .position = qdrVec3FromJson(vertexJson.value("position", json::object())),
                .normal = qdrVec3FromJson(vertexJson.value("normal", json::object()), {0.0f, 1.0f, 0.0f}),
                .uv = qdrVec2FromJson(vertexJson.value("uv", json::object())),
                .rgba = vertexJson.value("rgba", 0xffffffffu),
        });
    }
    mesh.indices = value.value("indices", std::vector<std::uint32_t>{});
    for (const json& primitiveJson : value.value("primitives", json::array())) {
        mesh.primitives.push_back(MeshPrimitiveSpan{
                .indexOffset = primitiveJson.value("indexOffset", 0u),
                .indexCount = primitiveJson.value("indexCount", 0u),
                .materialSlot = primitiveJson.value("materialSlot", 0u),
        });
    }
    if (const auto polygon = value.find("polygonDocument"); polygon != value.end() && polygon->is_object()) {
        mesh.hasAuthoredPolygonDocument = true;
        mesh.authoredPolygonDocument =
                std::make_shared<quader_poly::Document>(quader_io::qdr_polygon_document_from_json(*polygon));
        if (mesh.vertices.empty() && mesh.indices.empty()) {
            mesh = meshFromPolygonDocument(*mesh.authoredPolygonDocument, mesh.name);
        }
    }
    mesh.primitives = normalizedMeshPrimitiveSpans(mesh);
    return mesh;
}

TextureAsset textureFromJson(const json& value) {
    TextureAsset texture;
    texture.name = value.value("name", std::string("Texture"));
    texture.usage = qdrTextureUsageFromName(value.value("usage", std::string("unknown")));
    texture.colorSpace = qdrTextureColorSpaceFromName(value.value("colorSpace", std::string("srgb")));
    texture.width = value.value("width", 0u);
    texture.height = value.value("height", 0u);
    texture.sourcePath = std::filesystem::path(value.value("sourcePath", std::string{}));
    texture.generated = value.value("generated", false);
    return texture;
}

MaterialDocument materialFromJson(const json& value) {
    MaterialDocument material;
    material.name = value.value("name", std::string("Material"));
    material.baseColor = qdrVec3FromJson(value.value("baseColor", json::object()), {1.0f, 1.0f, 1.0f});
    material.roughness = value.value("roughness", 1.0f);
    material.metallic = value.value("metallic", 0.0f);
    material.nodeGraphReserved = value.value("nodeGraphReserved", true);
    if (const auto slots = value.find("textureSlots"); slots != value.end() && slots->is_object()) {
        for (const auto& [slot, textureId] : slots->items()) {
            if (!setMaterialTextureSlot(material, slot, textureId.get<AssetId>())) {
                throw std::runtime_error("QDR project document material is invalid: Unsupported material texture slot.");
            }
        }
    }
    return material;
}

EditorObjectId editorObjectIdFromJson(const json& value) {
    if (const auto editorObjectId = value.find("editorObjectId");
            editorObjectId != value.end() && editorObjectId->is_number_unsigned()) {
        return editorObjectId->get<EditorObjectId>();
    }
    return value.value("id", InvalidEditorObjectId);
}

ProjectObjectRecord objectRecordFromJson(const json& value) {
    return ProjectObjectRecord{
            .id = editorObjectIdFromJson(value),
            .sceneEntity = value.value("sceneEntity", InvalidEntityId),
            .mesh = value.value("mesh", InvalidAssetId),
            .material = value.value("material", InvalidAssetId),
            .modelingObject = value.value("modelingObject", InvalidModelingObjectId),
            .authoredName = value.value("authoredName", std::string{}),
            .visible = value.value("visible", true),
            .materialSlot = value.value("materialSlot", 0u),
    };
}

std::string assetKindLabel(std::string_view kind) {
    return std::string(kind.data(), kind.size());
}

AssetId qdrAssetIdFromJson(const json& value, std::string_view kind) {
    const AssetId id = value.value("id", InvalidAssetId);
    if (id == InvalidAssetId) {
        throw std::runtime_error("QDR project document contains an invalid " + assetKindLabel(kind) + " asset id.");
    }
    return id;
}

void requireUniqueAssetId(std::set<AssetId>& ids, AssetId id, std::string_view kind) {
    if (!ids.insert(id).second) {
        throw std::runtime_error("QDR project document contains duplicate " + assetKindLabel(kind) + " asset ids.");
    }
}

void validateMaterialAssetForQdr(const AssetRegistry& assets, const MaterialDocument& material) {
    const AssetRegistryValidationResult validation = assets.validateMaterial(material);
    if (!validation.ok) {
        throw std::runtime_error("QDR project document material is invalid: " + validation.message + ".");
    }
}

void requireValidAssetReferencesForQdr(const ProjectDocumentAssetReferenceReport& report) {
    if (!report.ok()) {
        throw std::runtime_error("QDR project document " + report.issues.front().message);
    }
}

ProjectDocument projectDocumentFromAppPayload(const json& payload) {
    requireObject(payload, "QDR project document payload");
    const json project = payload.value("project", json::object());
    ProjectDocument document(
            ProjectDocumentRecord{
                    .id = project.value("id", 0ull),
                    .name = project.value("name", std::string("Untitled Project")),
            },
            ProjectDocumentRootPolicy::DeferSceneRoot);

    const json assetsJson = payload.value("assets", json::object());
    std::set<AssetId> meshAssetIds;
    for (const json& meshJson : assetsJson.value("meshes", json::array())) {
        const AssetId id = qdrAssetIdFromJson(meshJson, "mesh");
        requireUniqueAssetId(meshAssetIds, id, "mesh");
        if (document.assets().addMesh(id, meshFromJson(meshJson)) == InvalidAssetId) {
            throw std::runtime_error("QDR project document contains duplicate mesh asset ids.");
        }
    }
    std::set<AssetId> textureAssetIds;
    for (const json& textureJson : assetsJson.value("textures", json::array())) {
        const AssetId id = qdrAssetIdFromJson(textureJson, "texture");
        requireUniqueAssetId(textureAssetIds, id, "texture");
        if (document.assets().addTexture(id, textureFromJson(textureJson)) == InvalidAssetId) {
            throw std::runtime_error("QDR project document contains duplicate texture asset ids.");
        }
    }
    std::set<AssetId> materialAssetIds;
    for (const json& materialJson : assetsJson.value("materials", json::array())) {
        const AssetId id = qdrAssetIdFromJson(materialJson, "material");
        requireUniqueAssetId(materialAssetIds, id, "material");
        MaterialDocument material = materialFromJson(materialJson);
        validateMaterialAssetForQdr(document.assets(), material);
        if (document.assets().addMaterial(id, std::move(material)) == InvalidAssetId) {
            throw std::runtime_error("QDR project document contains duplicate or invalid material asset ids.");
        }
    }

    std::vector<std::pair<EntityId, EntityId>> parentLinks;
    std::set<EntityId> entitiesWithExplicitEditorObjectIds;
    EntityId activeSelection = InvalidEntityId;
    for (const json& entityJson : payload.value("entities", json::array())) {
        const EntityId requestedId = entityJson.value("id", InvalidEntityId);
        const bool hasExplicitEditorObjectId = entityJson.contains("editorObjectId");
        const EditorObjectId editorObjectId = hasExplicitEditorObjectId
                ? entityJson.value("editorObjectId", InvalidEditorObjectId)
                : InvalidEditorObjectId;
        const std::string name = entityJson.value("name", std::string("Entity"));
        const EntityKind kind = qdrEntityKindFromName(entityJson.value("kind", std::string("generic")));
        const EntityId entity = document.scene().createEntityWithId(requestedId, name, kind, editorObjectId);
        if (entity == InvalidEntityId) {
            throw std::runtime_error("QDR document contains a duplicate or invalid entity id.");
        }
        if (const auto uuidJson = entityJson.find("uuid");
                uuidJson != entityJson.end() && uuidJson->is_string() &&
                !document.scene().setEntityUuid(entity, uuidJson->get<std::string>())) {
            throw std::runtime_error("QDR document contains a duplicate or invalid entity uuid.");
        }
        if (hasExplicitEditorObjectId) {
            entitiesWithExplicitEditorObjectIds.insert(entity);
        }

        document.scene().setTransform(entity, qdrTransformComponentFromJson(entityJson.value("transform", json::object())));
        if (const auto visibilityJson = entityJson.find("visibility");
                visibilityJson != entityJson.end() && visibilityJson->is_object()) {
            document.scene().setVisible(entity, visibilityJson->value("visible", true));
            document.scene().setSelectable(entity, visibilityJson->value("selectable", true));
        }
        document.scene().setLocked(entity, entityJson.value("locked", false));
        if (const auto objectJson = entityJson.find("modelingObject");
                objectJson != entityJson.end() && objectJson->is_object()) {
            document.scene().setModelingObject(entity, ModelingObjectComponent{
                    .objectId = objectJson->value("objectId", InvalidModelingObjectId),
                    .materialSlot = objectJson->value("materialSlot", 0u),
            });
        }
        if (const auto meshJson = entityJson.find("mesh"); meshJson != entityJson.end() && meshJson->is_object()) {
            document.scene().setMesh(entity, qdrMeshComponentFromJson(*meshJson));
        }
        if (const auto materialJson = entityJson.find("material");
                materialJson != entityJson.end() && materialJson->is_object()) {
            MaterialComponent material = qdrMaterialComponentFromJson(*materialJson);
            if (material.material != InvalidAssetId) {
                document.scene().setMaterial(entity, std::move(material));
            }
        }
        if (const auto lightJson = entityJson.find("light"); lightJson != entityJson.end() && lightJson->is_object()) {
            document.scene().setLight(entity, qdrLightComponentFromJson(*lightJson));
        }
        if (entityJson.value("sceneRoot", false) ||
                entityJson.find("sceneSettings") != entityJson.end()) {
            const json settingsJson = entityJson.value("sceneSettings", json::object());
            if (!document.scene().setSceneSettings(entity, qdrSceneSettingsComponentFromJson(settingsJson))) {
                throw std::runtime_error("QDR project document contains multiple scene root entities.");
            }
        }

        if (entityJson.value("selected", false)) {
            document.scene().setSelected(entity, true);
        }
        if (entityJson.value("active", false)) {
            activeSelection = entity;
        }
        parentLinks.emplace_back(entity, entityJson.value("parent", InvalidEntityId));
    }

    for (const auto& [child, parent] : parentLinks) {
        if (parent != InvalidEntityId) {
            if (!document.scene().exists(parent)) {
                throw std::runtime_error("QDR project document entity references a missing parent.");
            }
            if (!document.scene().setParent(child, parent, false)) {
                throw std::runtime_error("QDR project document contains an invalid entity hierarchy.");
            }
        }
    }
    if (activeSelection != InvalidEntityId) {
        if (!document.scene().setActiveSelection(activeSelection)) {
            throw std::runtime_error("QDR project document active selection must reference a selected entity.");
        }
    }

    if (document.ensureSceneRoot() == InvalidEntityId) {
        throw std::runtime_error("QDR project document could not create a scene root.");
    }

    requireValidAssetReferencesForQdr(document.validateAssetReferences());

    std::set<EditorObjectId> objectRecordIds;
    std::set<EntityId> objectRecordEntities;
    for (const json& recordJson : payload.value("objects", json::array())) {
        const ProjectObjectRecord record = objectRecordFromJson(recordJson);
        requireValidAssetReferencesForQdr(document.validateAssetReferences(record));
        if (!objectRecordIds.insert(record.id).second) {
            throw std::runtime_error("QDR project document contains duplicate object record ids.");
        }
        if (!objectRecordEntities.insert(record.sceneEntity).second) {
            throw std::runtime_error("QDR project document contains duplicate object record scene entities.");
        }
        if (entitiesWithExplicitEditorObjectIds.contains(record.sceneEntity) &&
                document.scene().editorObjectId(record.sceneEntity) != record.id) {
            throw std::runtime_error("QDR project object record id does not match the scene editor object id.");
        }
        if (document.upsertObjectRecord(record) == InvalidEditorObjectId) {
            throw std::runtime_error("QDR project object record conflicts with an existing editor object id.");
        }
    }

    const ProjectDocumentIdentityReport identity = document.validateObjectIdentityInvariants();
    if (!identity.ok()) {
        throw std::runtime_error("QDR project document identity invariant failed: " + identity.issues.front().message);
    }

    return document;
}

QVec3 colorFromAbgr(std::uint32_t color) {
    const float r = static_cast<float>(color & 0xffu) / 255.0f;
    const float g = static_cast<float>((color >> 8u) & 0xffu) / 255.0f;
    const float b = static_cast<float>((color >> 16u) & 0xffu) / 255.0f;
    return {r, g, b};
}

LightType lightTypeFromLegacy(quader_io::QdrLightType type) {
    switch (type) {
    case quader_io::QdrLightType::point:
        return LightType::Point;
    case quader_io::QdrLightType::spot:
        return LightType::Spot;
    case quader_io::QdrLightType::directional:
        return LightType::Directional;
    }
    return LightType::Point;
}

ProjectDocument projectDocumentFromLegacyQdr(const quader_io::QdrDocument& legacy) {
    ProjectDocument document(
            ProjectDocumentRecord{.name = "Imported QDR"},
            ProjectDocumentRootPolicy::DeferSceneRoot);
    std::unordered_map<std::uint32_t, AssetId> materialBySlot;
    std::unordered_map<int, EntityId> entityByLegacyObject;

    const auto materialForSlot = [&](std::uint32_t slot) {
        if (const auto existing = materialBySlot.find(slot); existing != materialBySlot.end()) {
            return existing->second;
        }
        MaterialDocument material = makeDefaultQuaderMaterial("QDR Material " + std::to_string(slot));
        const AssetId id = document.assets().addMaterial(std::move(material));
        materialBySlot[slot] = id;
        return id;
    };

    for (const quader_io::QdrObject& object : legacy.objects) {
        const std::string name = "Imported Object " + std::to_string(object.id);
        MeshAsset mesh = meshFromPolygonDocument(object.document, name);
        const AssetId meshId = document.assets().addMesh(std::move(mesh));
        const AssetId materialId = materialForSlot(static_cast<std::uint32_t>(object.material_id));
        const EntityId entity = document.scene().createEntity(name, EntityKind::Mesh);
        document.scene().setModelingObject(entity, ModelingObjectComponent{
                .objectId = object.id,
                .materialSlot = static_cast<std::uint32_t>(object.material_id),
        });
        document.scene().setMesh(entity, MeshComponent{.mesh = meshId});
        document.scene().setMaterial(entity, MaterialComponent{.material = materialId});
        if (object.selected) {
            document.scene().setSelected(entity, true);
        }
        document.createObjectRecord(ProjectObjectRecord{
                .sceneEntity = entity,
                .mesh = meshId,
                .material = materialId,
                .modelingObject = object.id,
                .authoredName = name,
                .visible = true,
                .materialSlot = static_cast<std::uint32_t>(object.material_id),
        });
        entityByLegacyObject[object.id] = entity;
    }

    if (const auto active = entityByLegacyObject.find(legacy.active_object_id);
            active != entityByLegacyObject.end()) {
        document.scene().setSelected(active->second, true);
        document.scene().setActiveSelection(active->second);
    }

    for (const quader_io::QdrLight& legacyLight : legacy.lights) {
        const std::string name = legacyLight.name.empty()
                ? "Imported Light " + std::to_string(legacyLight.id)
                : legacyLight.name;
        const EntityId entity = document.scene().createEntity(name, EntityKind::Light);
        document.scene().setTransform(entity, TransformComponent{.position = fromRuntime(legacyLight.position)});
        document.scene().setVisible(entity, legacyLight.enabled);
        document.scene().setLight(entity, LightComponent{
                .type = lightTypeFromLegacy(legacyLight.type),
                .color = colorFromAbgr(legacyLight.color_abgr),
                .intensity = legacyLight.intensity,
                .range = legacyLight.range,
                .castsShadows = legacyLight.shadow_mode != quader_io::QdrLightShadowMode::none,
        });
        if (legacyLight.selected) {
            document.scene().setSelected(entity, true);
        }
    }

    (void)document.ensureSceneRoot();
    return document;
}

QdrProjectReadResult legacyReadResult(std::string_view text) {
    quader_io::QdrReadResult legacy = quader_io::deserialize_document(text);
    if (!legacy.ok) {
        return {false, ProjectDocument{}, legacy.message, false};
    }
    return {true, projectDocumentFromLegacyQdr(legacy.document), {}, true};
}

QdrProjectReadResult readFromRoot(const json& root, std::string_view text) {
    quader_io::require_qdr_root_object(root);
    const quader_io::QdrRootHeader header = quader_io::read_qdr_root_header(root);
    if (header.format != quader_io::qdr_string_from_view(kQdrFormatName)) {
        return {false, ProjectDocument{}, "Unsupported Quader file format: " + header.format, false};
    }

    const int version = header.version;
    if (version < kQdrMinSupportedFormatVersion || version > kQdrProjectFormatVersion) {
        return {false, ProjectDocument{}, "Unsupported Quader file version: " + std::to_string(version), false};
    }

    const auto documentPayload = root.find(quader_io::qdr_string_from_view(kQdrDocumentPayloadKey));
    if (documentPayload != root.end()) {
        if (!documentPayload->is_object()) {
            return {false, ProjectDocument{}, "QDR document payload must be an object.", false};
        }

        const auto kind = documentPayload->find("kind");
        if (kind != documentPayload->end()) {
            if (!kind->is_string()) {
                return {false, ProjectDocument{}, "QDR project document payload kind must be a string.", false};
            }
            const std::string kindText = kind->get<std::string>();
            if (kindText != quader_io::qdr_string_from_view(kQdrProjectDocumentKind)) {
                return {false, ProjectDocument{}, "Unsupported QDR project document payload kind: " + kindText, false};
            }
            if (version != kQdrProjectFormatVersion) {
                return {false, ProjectDocument{}, "QDR project document payload requires version " +
                                std::to_string(kQdrProjectFormatVersion) + ".", false};
            }
            return {true, projectDocumentFromAppPayload(*documentPayload), {}, false};
        }

        if (version == kQdrProjectFormatVersion) {
            return {false, ProjectDocument{}, "QDR project document payload is missing its kind.", false};
        }
        return legacyReadResult(text);
    }

    if (root.find(quader_io::qdr_string_from_view(kQdrLegacySessionPayloadKey)) != root.end()) {
        return legacyReadResult(text);
    }

    return {false, ProjectDocument{}, "QDR document is missing its document payload.", false};
}

} // namespace

QdrProjectReadResult QdrDocumentReader::deserializeProjectDocument(std::string_view text) {
    try {
        const json root = json::parse(text.begin(), text.end());
        return readFromRoot(root, text);
    } catch (const std::exception& exception) {
        return {false, ProjectDocument{}, exception.what(), false};
    } catch (...) {
        return {false, ProjectDocument{}, "Unknown error reading QDR document.", false};
    }
}

QdrProjectReadResult QdrDocumentReader::readProjectDocument(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return {false, ProjectDocument{}, "Could not open QDR document: " + path.string(), false};
    }
    const std::string text{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
    return deserializeProjectDocument(text);
}

} // namespace quader
