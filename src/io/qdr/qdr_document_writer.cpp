////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_document_writer.hpp"

#include "assets/material_document.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_format.hpp"
#include "io/qdr/qdr_polygon_document_codec.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace quader {
namespace {

using json = nlohmann::ordered_json;

std::string stringFromView(std::string_view value) {
    return std::string(value.data(), value.size());
}

json vec2ToJson(QVec2 value) {
    return json{{"x", value.x}, {"y", value.y}};
}

json vec3ToJson(QVec3 value) {
    return json{{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

json transformToJson(const TransformComponent& transform) {
    return json{
            {"position", vec3ToJson(transform.position)},
            {"rotationEuler", vec3ToJson(transform.rotationEuler)},
            {"scale", vec3ToJson(transform.scale)},
    };
}

std::string entityKindName(EntityKind kind) {
    switch (kind) {
    case EntityKind::Empty:
        return "empty";
    case EntityKind::Mesh:
        return "mesh";
    case EntityKind::Light:
        return "light";
    case EntityKind::Generic:
        return "generic";
    }
    return "generic";
}

std::string lightTypeName(LightType type) {
    switch (type) {
    case LightType::Directional:
        return "directional";
    case LightType::Point:
        return "point";
    case LightType::Spot:
        return "spot";
    }
    return "directional";
}

std::string collisionTypeName(MeshCollisionType type) {
    switch (type) {
    case MeshCollisionType::Box:
        return "box";
    case MeshCollisionType::Sphere:
        return "sphere";
    case MeshCollisionType::Cylinder:
        return "cylinder";
    case MeshCollisionType::ConvexHull:
        return "convexHull";
    case MeshCollisionType::Mesh:
        return "mesh";
    }
    return "box";
}

std::string physicsBodyTypeName(MeshPhysicsBodyType type) {
    switch (type) {
    case MeshPhysicsBodyType::StaticBody:
        return "staticBody";
    case MeshPhysicsBodyType::RigidBody:
        return "rigidBody";
    }
    return "staticBody";
}

std::string textureUsageName(TextureUsage usage) {
    switch (usage) {
    case TextureUsage::BaseColor:
        return "baseColor";
    case TextureUsage::Normal:
        return "normal";
    case TextureUsage::Roughness:
        return "roughness";
    case TextureUsage::Metallic:
        return "metallic";
    case TextureUsage::Curvature:
        return "curvature";
    case TextureUsage::Thickness:
        return "thickness";
    case TextureUsage::AmbientOcclusion:
        return "ambientOcclusion";
    case TextureUsage::Position:
        return "position";
    case TextureUsage::Id:
        return "id";
    case TextureUsage::Mask:
        return "mask";
    case TextureUsage::Unknown:
        return "unknown";
    }
    return "unknown";
}

std::string textureColorSpaceName(TextureColorSpace colorSpace) {
    switch (colorSpace) {
    case TextureColorSpace::Linear:
        return "linear";
    case TextureColorSpace::Srgb:
        return "srgb";
    }
    return "srgb";
}

json meshToJson(AssetId id, const MeshAsset& mesh) {
    json vertices = json::array();
    for (const MeshVertex& vertex : mesh.vertices) {
        vertices.push_back(json{
                {"position", vec3ToJson(vertex.position)},
                {"normal", vec3ToJson(vertex.normal)},
                {"uv", vec2ToJson(vertex.uv)},
                {"rgba", vertex.rgba},
        });
    }

    json primitives = json::array();
    for (const MeshPrimitiveSpan& primitive : normalizedMeshPrimitiveSpans(mesh)) {
        primitives.push_back(json{
                {"indexOffset", primitive.indexOffset},
                {"indexCount", primitive.indexCount},
                {"materialSlot", primitive.materialSlot},
        });
    }

    json value{
            {"id", id},
            {"name", mesh.name},
            {"hasStableCpuTopology", mesh.hasStableCpuTopology},
            {"vertices", vertices},
            {"indices", mesh.indices},
            {"primitives", primitives},
    };
    if (mesh.hasAuthoredPolygonDocument && mesh.authoredPolygonDocument) {
        value["polygonDocument"] = quader_io::qdr_polygon_document_to_json(*mesh.authoredPolygonDocument);
    }
    return value;
}

json textureToJson(AssetId id, const TextureAsset& texture) {
    return json{
            {"id", id},
            {"name", texture.name},
            {"usage", textureUsageName(texture.usage)},
            {"colorSpace", textureColorSpaceName(texture.colorSpace)},
            {"width", texture.width},
            {"height", texture.height},
            {"sourcePath", texture.sourcePath.generic_string()},
            {"generated", texture.generated},
    };
}

json materialToJson(AssetId id, const MaterialDocument& material) {
    json textureSlots = json::object();
    for (const MaterialTextureSlotDescriptor& descriptor : materialTextureSlotDescriptors()) {
        if (const std::optional<AssetId> textureId = materialTextureSlotAsset(material, descriptor.id)) {
            textureSlots[descriptor.id] = *textureId;
        }
    }
    return json{
            {"id", id},
            {"name", material.name},
            {"baseColor", vec3ToJson(material.baseColor)},
            {"roughness", material.roughness},
            {"metallic", material.metallic},
            {"textureSlots", textureSlots},
            {"nodeGraphReserved", material.nodeGraphReserved},
    };
}

json meshComponentToJson(const MeshComponent& mesh) {
    return json{
            {"mesh", mesh.mesh},
            {"editorOnly", mesh.editorOnly},
            {"castShadows", mesh.castShadows},
            {"doubleSided", mesh.doubleSided},
            {"generateCollisions", mesh.generateCollisions},
            {"collisionType", collisionTypeName(mesh.collisionType)},
            {"displayCollision", mesh.displayCollision},
            {"bodyType", physicsBodyTypeName(mesh.bodyType)},
    };
}

json lightToJson(const LightComponent& light) {
    return json{
            {"type", lightTypeName(light.type)},
            {"color", vec3ToJson(light.color)},
            {"intensity", light.intensity},
            {"castsShadows", light.castsShadows},
    };
}

json entityToJson(const SceneDocument& scene, EntityId entity) {
    json value{
            {"id", entity},
            {"editorObjectId", scene.editorObjectId(entity)},
            {"name", scene.name(entity) ? scene.name(entity)->name : std::string{}},
            {"kind", entityKindName(scene.kind(entity))},
            {"parent", scene.parent(entity)},
    };
    if (const TransformComponent* transform = scene.transform(entity)) {
        value["transform"] = transformToJson(*transform);
    }
    if (const VisibilityComponent* visibility = scene.visibility(entity)) {
        value["visibility"] = json{
                {"visible", visibility->visible},
                {"selectable", visibility->selectable},
        };
    }
    if (const LockComponent* lock = scene.lock(entity)) {
        value["locked"] = lock->locked;
    }
    if (const SelectionComponent* selection = scene.selection(entity)) {
        value["selected"] = selection->selected;
        value["active"] = scene.primarySelection() == entity;
    }
    if (const ModelingObjectComponent* object = scene.modelingObject(entity)) {
        value["modelingObject"] = json{
                {"objectId", object->objectId},
                {"materialSlot", object->materialSlot},
        };
    }
    if (const MeshComponent* mesh = scene.mesh(entity)) {
        value["mesh"] = meshComponentToJson(*mesh);
    }
    if (const MaterialComponent* material = scene.material(entity)) {
        json materialJson{{"material", material->material}};
        if (!material->materialSlots.empty()) {
            json slots = json::object();
            for (const auto& [slot, materialId] : material->materialSlots) {
                slots[std::to_string(slot)] = materialId;
            }
            materialJson["slots"] = std::move(slots);
        }
        value["material"] = std::move(materialJson);
    }
    if (const LightComponent* light = scene.light(entity)) {
        value["light"] = lightToJson(*light);
    }
    return value;
}

json objectRecordToJson(const ProjectObjectRecord& record) {
    return json{
            {"editorObjectId", record.id},
            {"sceneEntity", record.sceneEntity},
            {"mesh", record.mesh},
            {"material", record.material},
            {"modelingObject", record.modelingObject},
            {"authoredName", record.authoredName},
            {"visible", record.visible},
            {"materialSlot", record.materialSlot},
    };
}

json projectPayloadToJson(const ProjectDocument& document) {
    const SceneDocument& scene = document.scene();
    const AssetRegistry& assets = document.assets();

    json meshes = json::array();
    for (const AssetId id : assets.meshIds()) {
        if (const MeshAsset* mesh = assets.mesh(id)) {
            meshes.push_back(meshToJson(id, *mesh));
        }
    }

    json materials = json::array();
    for (const AssetId id : assets.materialIds()) {
        if (const MaterialDocument* material = assets.material(id)) {
            materials.push_back(materialToJson(id, *material));
        }
    }

    json textures = json::array();
    for (const AssetId id : assets.textureIds()) {
        if (const TextureAsset* texture = assets.texture(id)) {
            textures.push_back(textureToJson(id, *texture));
        }
    }

    json entities = json::array();
    for (const EntityId entity : scene.entityTreeOrder()) {
        entities.push_back(entityToJson(scene, entity));
    }

    json objectRecords = json::array();
    for (const ProjectObjectRecord& record : document.objectRecords()) {
        objectRecords.push_back(objectRecordToJson(record));
    }

    return json{
            {"kind", stringFromView(kQdrProjectDocumentKind)},
            {"project", json{
                    {"id", document.id()},
                    {"name", document.name()},
            }},
            {"assets", json{
                    {"meshes", meshes},
                    {"materials", materials},
                    {"textures", textures},
            }},
            {"entities", entities},
            {"objects", objectRecords},
    };
}

quader_io::QdrAtomicWriteResult validateTemporaryProjectFile(const std::filesystem::path& temp) {
    const QdrProjectReadResult validation = QdrDocumentReader::readProjectDocument(temp);
    if (!validation.ok) {
        return {
                false,
                validation.message.empty()
                        ? "Serialized QDR project document failed validation."
                        : "Serialized QDR project document failed validation: " + validation.message,
        };
    }
    return {true, {}};
}

QdrProjectWriteResult projectWriteResultFromAtomic(quader_io::QdrAtomicWriteResult result) {
    return {result.ok, std::move(result.message)};
}

} // namespace

std::string QdrDocumentWriter::serializeProjectDocument(const ProjectDocument& document) {
    const json root{
            {"format", stringFromView(kQdrFormatName)},
            {"version", kQdrProjectFormatVersion},
            {stringFromView(kQdrDocumentPayloadKey), projectPayloadToJson(document)},
    };
    return root.dump(2);
}

QdrProjectWriteResult QdrDocumentWriter::writeProjectDocumentAtomic(
        const std::filesystem::path& path,
        const ProjectDocument& document) {
    return projectWriteResultFromAtomic(quader_io::write_qdr_text_atomic(
            path,
            serializeProjectDocument(document),
            validateTemporaryProjectFile));
}

} // namespace quader
