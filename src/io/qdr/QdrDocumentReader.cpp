////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/QdrDocumentReader.h"

#include "assets/MaterialDocument.h"
#include "io/qdr/QdrFormat.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <io/qdr/qdr_document.h>
#include <mesh/polygon/document.h>

namespace quader {
namespace {

using json = nlohmann::ordered_json;

std::string stringFromView(std::string_view value) {
    return std::string(value.data(), value.size());
}

void requireObject(const json& value, std::string_view label) {
    if (!value.is_object()) {
        throw std::runtime_error(std::string(label) + " must be an object.");
    }
}

std::string readFormatHeader(const json& root) {
    const auto it = root.find("format");
    if (it == root.end() || !it->is_string()) {
        throw std::runtime_error("QDR document is missing a string format header.");
    }
    return it->get<std::string>();
}

int readVersionHeader(const json& root) {
    const auto it = root.find("version");
    if (it == root.end() || !it->is_number_integer()) {
        throw std::runtime_error("QDR document is missing an integer version header.");
    }
    const long long version = it->get<long long>();
    if (version < std::numeric_limits<int>::min() || version > std::numeric_limits<int>::max()) {
        throw std::runtime_error("QDR document version header is outside the supported integer range.");
    }
    return static_cast<int>(version);
}

Vec2 vec2FromJson(const json& value) {
    return {
            value.value("x", 0.0f),
            value.value("y", 0.0f),
    };
}

Vec3 vec3FromJson(const json& value, Vec3 fallback = {}) {
    if (!value.is_object()) {
        return fallback;
    }
    return {
            value.value("x", fallback.x),
            value.value("y", fallback.y),
            value.value("z", fallback.z),
    };
}

TransformComponent transformFromJson(const json& value) {
    TransformComponent transform;
    if (!value.is_object()) {
        return transform;
    }
    transform.position = vec3FromJson(value.value("position", json::object()));
    transform.rotationEuler = vec3FromJson(value.value("rotationEuler", json::object()));
    transform.scale = vec3FromJson(value.value("scale", json::object()), {1.0f, 1.0f, 1.0f});
    return transform;
}

EntityKind parseEntityKind(const std::string& value) {
    if (value == "empty") {
        return EntityKind::Empty;
    }
    if (value == "mesh") {
        return EntityKind::Mesh;
    }
    if (value == "light") {
        return EntityKind::Light;
    }
    return EntityKind::Generic;
}

LightType parseLightType(const std::string& value) {
    if (value == "point") {
        return LightType::Point;
    }
    if (value == "spot") {
        return LightType::Spot;
    }
    return LightType::Directional;
}

MeshCollisionType parseCollisionType(const std::string& value) {
    if (value == "sphere") {
        return MeshCollisionType::Sphere;
    }
    if (value == "cylinder") {
        return MeshCollisionType::Cylinder;
    }
    if (value == "convexHull") {
        return MeshCollisionType::ConvexHull;
    }
    if (value == "mesh") {
        return MeshCollisionType::Mesh;
    }
    return MeshCollisionType::Box;
}

MeshPhysicsBodyType parsePhysicsBodyType(const std::string& value) {
    if (value == "rigidBody") {
        return MeshPhysicsBodyType::RigidBody;
    }
    return MeshPhysicsBodyType::StaticBody;
}

TextureUsage parseTextureUsage(const std::string& value) {
    if (value == "baseColor") {
        return TextureUsage::BaseColor;
    }
    if (value == "normal") {
        return TextureUsage::Normal;
    }
    if (value == "roughness") {
        return TextureUsage::Roughness;
    }
    if (value == "metallic") {
        return TextureUsage::Metallic;
    }
    if (value == "curvature") {
        return TextureUsage::Curvature;
    }
    if (value == "thickness") {
        return TextureUsage::Thickness;
    }
    if (value == "ambientOcclusion") {
        return TextureUsage::AmbientOcclusion;
    }
    if (value == "position") {
        return TextureUsage::Position;
    }
    if (value == "id") {
        return TextureUsage::Id;
    }
    if (value == "mask") {
        return TextureUsage::Mask;
    }
    return TextureUsage::Unknown;
}

TextureColorSpace parseTextureColorSpace(const std::string& value) {
    if (value == "linear") {
        return TextureColorSpace::Linear;
    }
    return TextureColorSpace::Srgb;
}

Vec3 fromRuntime(quader_poly::Vec3 value) {
    return {value.x, value.y, value.z};
}

quader_poly::Vec2 polygonVec2FromJson(const json& value) {
    return {
            value.value("x", 0.0f),
            value.value("y", 0.0f),
    };
}

quader_poly::Vec3 polygonVec3FromJson(const json& value) {
    return {
            value.value("x", 0.0f),
            value.value("y", 0.0f),
            value.value("z", 0.0f),
    };
}

quader_poly::Edge polygonEdgeFromJson(const json& value) {
    return quader_poly::make_edge(
            value.value("a", quader_poly::invalid_element_id),
            value.value("b", quader_poly::invalid_element_id));
}

quader_poly::SurfaceShading parseSurfaceShading(const std::string& value) {
    if (value == "smooth") {
        return quader_poly::SurfaceShading::smooth;
    }
    if (value == "authored") {
        return quader_poly::SurfaceShading::authored;
    }
    return quader_poly::SurfaceShading::flat;
}

quader_poly::Document polygonDocumentFromJson(const json& value) {
    quader_poly::Document document;
    document.next_vertex_id = value.value("nextVertexId", 1u);
    document.next_face_id = value.value("nextFaceId", 1u);
    for (const json& vertexJson : value.value("vertices", json::array())) {
        document.vertices.push_back(quader_poly::Vertex{
                .id = vertexJson.value("id", quader_poly::invalid_element_id),
                .position = polygonVec3FromJson(vertexJson.value("position", json::object())),
        });
    }
    for (const json& faceJson : value.value("faces", json::array())) {
        quader_poly::Face face;
        face.id = faceJson.value("id", quader_poly::invalid_element_id);
        face.vertices = faceJson.value("vertices", std::vector<quader_poly::ElementId>{});
        face.material_slot = faceJson.value("materialSlot", 0u);
        face.normal_shading = parseSurfaceShading(faceJson.value("normalShading", std::string("flat")));
        for (const json& uvJson : faceJson.value("uvs", json::array())) {
            face.uvs.push_back(polygonVec2FromJson(uvJson));
        }
        document.faces.push_back(std::move(face));
    }
    for (const json& edgeJson : value.value("hardNormalEdges", json::array())) {
        document.hard_normal_edges.push_back(polygonEdgeFromJson(edgeJson));
    }
    return document;
}

MeshAsset meshFromPolygonDocument(const quader_poly::Document& document, std::string name) {
    const quader_poly::CompiledMesh compiled = quader_poly::compile_document(document);
    MeshAsset mesh;
    mesh.name = std::move(name);
    mesh.vertices.reserve(compiled.vertices.size());
    mesh.indices = compiled.indices;
    mesh.primitives.reserve(compiled.primitives.size());
    mesh.hasStableCpuTopology = true;
    mesh.hasAuthoredPolygonDocument = true;
    mesh.authoredPolygonDocument = std::make_shared<quader_poly::Document>(document);
    for (const quader_poly::CompiledVertex& vertex : compiled.vertices) {
        mesh.vertices.push_back(MeshVertex{
                .position = fromRuntime(vertex.position),
                .normal = fromRuntime(vertex.normal),
                .uv = {vertex.uv0.x, vertex.uv0.y},
                .rgba = vertex.color,
        });
    }
    for (const quader_poly::CompiledPrimitive& primitive : compiled.primitives) {
        mesh.primitives.push_back(MeshPrimitiveSpan{
                .indexOffset = primitive.index_offset,
                .indexCount = primitive.index_count,
                .materialSlot = primitive.material_slot,
        });
    }
    return mesh;
}

MeshAsset meshFromJson(const json& value) {
    MeshAsset mesh;
    mesh.name = value.value("name", std::string("Mesh"));
    mesh.hasStableCpuTopology = value.value("hasStableCpuTopology", true);
    for (const json& vertexJson : value.value("vertices", json::array())) {
        mesh.vertices.push_back(MeshVertex{
                .position = vec3FromJson(vertexJson.value("position", json::object())),
                .normal = vec3FromJson(vertexJson.value("normal", json::object()), {0.0f, 1.0f, 0.0f}),
                .uv = vec2FromJson(vertexJson.value("uv", json::object())),
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
        mesh.authoredPolygonDocument = std::make_shared<quader_poly::Document>(polygonDocumentFromJson(*polygon));
        if (mesh.vertices.empty() && mesh.indices.empty()) {
            mesh = meshFromPolygonDocument(*mesh.authoredPolygonDocument, mesh.name);
        }
    }
    return mesh;
}

TextureAsset textureFromJson(const json& value) {
    TextureAsset texture;
    texture.name = value.value("name", std::string("Texture"));
    texture.usage = parseTextureUsage(value.value("usage", std::string("unknown")));
    texture.colorSpace = parseTextureColorSpace(value.value("colorSpace", std::string("srgb")));
    texture.width = value.value("width", 0u);
    texture.height = value.value("height", 0u);
    texture.sourcePath = std::filesystem::path(value.value("sourcePath", std::string{}));
    texture.generated = value.value("generated", false);
    return texture;
}

MaterialDocument materialFromJson(const json& value) {
    MaterialDocument material;
    material.name = value.value("name", std::string("Material"));
    material.baseColor = vec3FromJson(value.value("baseColor", json::object()), {1.0f, 1.0f, 1.0f});
    material.roughness = value.value("roughness", 1.0f);
    material.metallic = value.value("metallic", 0.0f);
    material.nodeGraphReserved = value.value("nodeGraphReserved", true);
    if (const auto slots = value.find("textureSlots"); slots != value.end() && slots->is_object()) {
        for (const auto& [slot, textureId] : slots->items()) {
            material.textureSlots[slot] = textureId.get<AssetId>();
        }
    }
    return material;
}

MeshComponent meshComponentFromJson(const json& value) {
    MeshComponent mesh;
    mesh.mesh = value.value("mesh", InvalidAssetId);
    mesh.editorOnly = value.value("editorOnly", false);
    mesh.castShadows = value.value("castShadows", true);
    mesh.doubleSided = value.value("doubleSided", true);
    mesh.generateCollisions = value.value("generateCollisions", true);
    mesh.collisionType = parseCollisionType(value.value("collisionType", std::string("box")));
    mesh.displayCollision = value.value("displayCollision", false);
    mesh.bodyType = parsePhysicsBodyType(value.value("bodyType", std::string("staticBody")));
    return mesh;
}

LightComponent lightFromJson(const json& value) {
    LightComponent light;
    light.type = parseLightType(value.value("type", std::string("directional")));
    light.color = vec3FromJson(value.value("color", json::object()), {1.0f, 1.0f, 1.0f});
    light.intensity = value.value("intensity", 80000.0f);
    light.castsShadows = value.value("castsShadows", false);
    return light;
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

void validateMaterialTextureReferences(const std::set<AssetId>& textureAssetIds, const MaterialDocument& material) {
    for (const auto& [slot, textureId] : material.textureSlots) {
        (void)slot;
        if (textureId == InvalidAssetId || !textureAssetIds.contains(textureId)) {
            throw std::runtime_error("QDR project document material references a missing texture asset.");
        }
    }
}

void validateEntityAssetReferences(const ProjectDocument& document) {
    for (const EntityId entity : document.scene().entities()) {
        if (const MeshComponent* mesh = document.scene().mesh(entity);
                mesh != nullptr && (mesh->mesh == InvalidAssetId || document.assets().mesh(mesh->mesh) == nullptr)) {
            throw std::runtime_error("QDR project document entity references a missing mesh asset.");
        }
        if (const MaterialComponent* material = document.scene().material(entity);
                material != nullptr && document.assets().material(material->material) == nullptr) {
            throw std::runtime_error("QDR project document entity references a missing material asset.");
        }
    }
}

void validateObjectRecordReferences(const ProjectDocument& document, const ProjectObjectRecord& record) {
    if (record.id == InvalidEditorObjectId) {
        throw std::runtime_error("QDR project document contains an object record without a stable object id.");
    }
    if (record.sceneEntity == InvalidEntityId || !document.scene().exists(record.sceneEntity)) {
        throw std::runtime_error("QDR project object record points at a missing scene entity.");
    }
    if (record.mesh != InvalidAssetId && document.assets().mesh(record.mesh) == nullptr) {
        throw std::runtime_error("QDR project object record references a missing mesh asset.");
    }
    if (record.material != InvalidAssetId && document.assets().material(record.material) == nullptr) {
        throw std::runtime_error("QDR project object record references a missing material asset.");
    }
}

ProjectDocument projectDocumentFromAppPayload(const json& payload) {
    requireObject(payload, "QDR project document payload");
    const json project = payload.value("project", json::object());
    ProjectDocument document(ProjectDocumentRecord{
            .id = project.value("id", 0ull),
            .name = project.value("name", std::string("Untitled Project")),
    });

    const json assetsJson = payload.value("assets", json::object());
    for (const json& meshJson : assetsJson.value("meshes", json::array())) {
        const AssetId id = meshJson.value("id", InvalidAssetId);
        document.assets().addMesh(id, meshFromJson(meshJson));
    }
    std::set<AssetId> textureAssetIds;
    for (const json& textureJson : assetsJson.value("textures", json::array())) {
        const AssetId id = textureJson.value("id", InvalidAssetId);
        if (id == InvalidAssetId) {
            throw std::runtime_error("QDR project document contains an invalid texture asset id.");
        }
        if (!textureAssetIds.insert(id).second) {
            throw std::runtime_error("QDR project document contains duplicate texture asset ids.");
        }
        document.assets().addTexture(id, textureFromJson(textureJson));
    }
    for (const json& materialJson : assetsJson.value("materials", json::array())) {
        const AssetId id = materialJson.value("id", InvalidAssetId);
        MaterialDocument material = materialFromJson(materialJson);
        validateMaterialTextureReferences(textureAssetIds, material);
        document.assets().addMaterial(id, std::move(material));
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
        const EntityKind kind = parseEntityKind(entityJson.value("kind", std::string("generic")));
        const EntityId entity = document.scene().createEntityWithId(requestedId, name, kind, editorObjectId);
        if (entity == InvalidEntityId) {
            throw std::runtime_error("QDR document contains a duplicate or invalid entity id.");
        }
        if (hasExplicitEditorObjectId) {
            entitiesWithExplicitEditorObjectIds.insert(entity);
        }

        document.scene().setTransform(entity, transformFromJson(entityJson.value("transform", json::object())));
        if (const auto visibilityJson = entityJson.find("visibility");
                visibilityJson != entityJson.end() && visibilityJson->is_object()) {
            document.scene().setVisible(entity, visibilityJson->value("visible", true));
            if (VisibilityComponent* visibility = document.scene().visibility(entity)) {
                visibility->selectable = visibilityJson->value("selectable", true);
            }
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
            document.scene().setMesh(entity, meshComponentFromJson(*meshJson));
        }
        if (const auto materialJson = entityJson.find("material");
                materialJson != entityJson.end() && materialJson->is_object()) {
            const AssetId material = materialJson->value("material", InvalidAssetId);
            if (material != InvalidAssetId) {
                document.scene().setMaterial(entity, MaterialComponent{.material = material});
            }
        }
        if (const auto lightJson = entityJson.find("light"); lightJson != entityJson.end() && lightJson->is_object()) {
            document.scene().setLight(entity, lightFromJson(*lightJson));
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

    validateEntityAssetReferences(document);

    std::set<EditorObjectId> objectRecordIds;
    std::set<EntityId> objectRecordEntities;
    for (const json& recordJson : payload.value("objects", json::array())) {
        const ProjectObjectRecord record = objectRecordFromJson(recordJson);
        validateObjectRecordReferences(document, record);
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

Vec3 colorFromAbgr(std::uint32_t color) {
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
    ProjectDocument document(ProjectDocumentRecord{.name = "Imported QDR"});
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
                .castsShadows = legacyLight.shadow_mode != quader_io::QdrLightShadowMode::none,
        });
        if (legacyLight.selected) {
            document.scene().setSelected(entity, true);
        }
    }

    return document;
}

QdrProjectReadResult legacyReadResult(std::string_view text) {
    quader_io::QdrReadResult legacy = quader_io::deserialize_document(text);
    if (!legacy.ok) {
        return {false, {}, legacy.message, false};
    }
    return {true, projectDocumentFromLegacyQdr(legacy.document), {}, true};
}

QdrProjectReadResult readFromRoot(const json& root, std::string_view text) {
    requireObject(root, "QDR document root");
    const std::string format = readFormatHeader(root);
    if (format != stringFromView(kQdrFormatName)) {
        return {false, {}, "Unsupported Quader file format: " + format, false};
    }

    const int version = readVersionHeader(root);
    if (version < kQdrMinSupportedFormatVersion || version > kQdrProjectFormatVersion) {
        return {false, {}, "Unsupported Quader file version: " + std::to_string(version), false};
    }

    const auto documentPayload = root.find(stringFromView(kQdrDocumentPayloadKey));
    if (documentPayload != root.end()) {
        if (!documentPayload->is_object()) {
            return {false, {}, "QDR document payload must be an object.", false};
        }

        const auto kind = documentPayload->find("kind");
        if (kind != documentPayload->end()) {
            if (!kind->is_string()) {
                return {false, {}, "QDR project document payload kind must be a string.", false};
            }
            const std::string kindText = kind->get<std::string>();
            if (kindText != stringFromView(kQdrProjectDocumentKind)) {
                return {false, {}, "Unsupported QDR project document payload kind: " + kindText, false};
            }
            if (version != kQdrProjectFormatVersion) {
                return {false, {}, "QDR project document payload requires version " +
                                std::to_string(kQdrProjectFormatVersion) + ".", false};
            }
            return {true, projectDocumentFromAppPayload(*documentPayload), {}, false};
        }

        if (version == kQdrProjectFormatVersion) {
            return {false, {}, "QDR project document payload is missing its kind.", false};
        }
        return legacyReadResult(text);
    }

    if (root.find(stringFromView(kQdrLegacySessionPayloadKey)) != root.end()) {
        return legacyReadResult(text);
    }

    return {false, {}, "QDR document is missing its document payload.", false};
}

} // namespace

QdrProjectReadResult QdrDocumentReader::deserializeProjectDocument(std::string_view text) {
    try {
        const json root = json::parse(text.begin(), text.end());
        return readFromRoot(root, text);
    } catch (const std::exception& exception) {
        return {false, {}, exception.what(), false};
    } catch (...) {
        return {false, {}, "Unknown error reading QDR document.", false};
    }
}

QdrProjectReadResult QdrDocumentReader::readProjectDocument(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return {false, {}, "Could not open QDR document: " + path.string(), false};
    }
    const std::string text{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
    return deserializeProjectDocument(text);
}

} // namespace quader
