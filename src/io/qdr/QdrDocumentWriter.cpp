////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/QdrDocumentWriter.h"

#include "io/qdr/QdrFormat.h"

#include <nlohmann/json.hpp>
#include <mesh/polygon/document.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace quader {
namespace {

using json = nlohmann::ordered_json;

std::string stringFromView(std::string_view value) {
    return std::string(value.data(), value.size());
}

json vec2ToJson(Vec2 value) {
    return json{{"x", value.x}, {"y", value.y}};
}

json vec3ToJson(Vec3 value) {
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

json polygonVec2ToJson(quader_poly::Vec2 value) {
    return json{{"x", value.x}, {"y", value.y}};
}

json polygonVec3ToJson(quader_poly::Vec3 value) {
    return json{{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

json polygonEdgeToJson(quader_poly::Edge edge) {
    return json{{"a", edge.a}, {"b", edge.b}};
}

std::string surfaceShadingName(quader_poly::SurfaceShading shading) {
    switch (shading) {
    case quader_poly::SurfaceShading::flat:
        return "flat";
    case quader_poly::SurfaceShading::smooth:
        return "smooth";
    case quader_poly::SurfaceShading::authored:
        return "authored";
    }
    return "flat";
}

json polygonDocumentToJson(const quader_poly::Document& document) {
    json vertices = json::array();
    for (const quader_poly::Vertex& vertex : document.vertices) {
        vertices.push_back(json{
                {"id", vertex.id},
                {"position", polygonVec3ToJson(vertex.position)},
        });
    }

    json faces = json::array();
    for (const quader_poly::Face& face : document.faces) {
        json uvs = json::array();
        for (quader_poly::Vec2 uv : face.uvs) {
            uvs.push_back(polygonVec2ToJson(uv));
        }
        faces.push_back(json{
                {"id", face.id},
                {"vertices", face.vertices},
                {"uvs", uvs},
                {"materialSlot", face.material_slot},
                {"normalShading", surfaceShadingName(face.normal_shading)},
        });
    }

    json hardNormalEdges = json::array();
    for (quader_poly::Edge edge : document.hard_normal_edges) {
        hardNormalEdges.push_back(polygonEdgeToJson(edge));
    }

    return json{
            {"nextVertexId", document.next_vertex_id},
            {"nextFaceId", document.next_face_id},
            {"vertices", vertices},
            {"faces", faces},
            {"hardNormalEdges", hardNormalEdges},
    };
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
    for (const MeshPrimitiveSpan& primitive : mesh.primitives) {
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
        value["polygonDocument"] = polygonDocumentToJson(*mesh.authoredPolygonDocument);
    }
    return value;
}

json materialToJson(AssetId id, const MaterialDocument& material) {
    json textureSlots = json::object();
    for (const auto& [slot, textureId] : material.textureSlots) {
        textureSlots[slot] = textureId;
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
        value["material"] = json{{"material", material->material}};
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

    std::set<AssetId> meshIds;
    std::set<AssetId> materialIds;
    for (const EntityId entity : scene.entities()) {
        if (const MeshComponent* mesh = scene.mesh(entity); mesh != nullptr && mesh->mesh != InvalidAssetId) {
            meshIds.insert(mesh->mesh);
        }
        if (const MaterialComponent* material = scene.material(entity);
                material != nullptr && material->material != InvalidAssetId) {
            materialIds.insert(material->material);
        }
    }
    for (const ProjectObjectRecord& record : document.objectRecords()) {
        if (record.mesh != InvalidAssetId) {
            meshIds.insert(record.mesh);
        }
        if (record.material != InvalidAssetId) {
            materialIds.insert(record.material);
        }
    }

    json meshes = json::array();
    for (const AssetId id : meshIds) {
        if (const MeshAsset* mesh = assets.mesh(id)) {
            meshes.push_back(meshToJson(id, *mesh));
        }
    }

    json materials = json::array();
    for (const AssetId id : materialIds) {
        if (const MaterialDocument* material = assets.material(id)) {
            materials.push_back(materialToJson(id, *material));
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
            }},
            {"entities", entities},
            {"objects", objectRecords},
    };
}

std::filesystem::path temporaryPathFor(const std::filesystem::path& path) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp = path;
    temp += ".";
    temp += std::to_string(ticks);
    temp += ".tmp";
    return temp;
}

QdrProjectWriteResult writeTextAtomic(const std::filesystem::path& path, const std::string& text) {
    const std::filesystem::path temp = temporaryPathFor(path);
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            return {false, "Could not open temporary QDR file for writing."};
        }
        stream << text;
        if (!stream.good()) {
            return {false, "Could not write QDR file contents."};
        }
    }

    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (!error) {
        return {true, {}};
    }

    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::filesystem::remove(temp);
        return {false, "Could not replace QDR file: " + error.message()};
    }
    return {true, {}};
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
    return writeTextAtomic(path, serializeProjectDocument(document));
}

} // namespace quader
