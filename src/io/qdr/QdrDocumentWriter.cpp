////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/QdrDocumentWriter.h"

#include "io/qdr/QdrDocumentReader.h"
#include "io/qdr/QdrFormat.h"

#include <nlohmann/json.hpp>
#include <mesh/polygon/document.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
    std::set<AssetId> textureIds;
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

    for (const AssetId materialId : materialIds) {
        if (const MaterialDocument* material = assets.material(materialId)) {
            for (const auto& [slot, textureId] : material->textureSlots) {
                (void)slot;
                if (textureId != InvalidAssetId) {
                    textureIds.insert(textureId);
                }
            }
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

    json textures = json::array();
    for (const AssetId id : textureIds) {
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

std::string systemErrorMessage(const std::string& action, const std::error_code& error) {
    if (!error) {
        return action;
    }
    return action + ": " + error.message();
}

std::string windowsErrorMessage(const std::string& action, unsigned long errorCode) {
    std::error_code error(static_cast<int>(errorCode), std::system_category());
    return systemErrorMessage(action, error);
}

std::string uniqueTempSuffix(std::uint64_t attempt) {
    static std::atomic_uint64_t counter{0};
    const std::uint64_t tick =
            static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::uint64_t value = counter.fetch_add(1, std::memory_order_relaxed) ^ tick;
    std::ostringstream stream;
    stream << std::hex << value << "-" << attempt;
    return stream.str();
}

std::filesystem::path targetDirectoryForAtomicWrite(const std::filesystem::path& target) {
    const std::filesystem::path directory = target.parent_path();
    return directory.empty() ? std::filesystem::path(".") : directory;
}

std::filesystem::path makeTempPathForAtomicWrite(
        const std::filesystem::path& target,
        const std::filesystem::path& directory,
        std::uint64_t attempt) {
    return directory / ("." + target.filename().string() + "." + uniqueTempSuffix(attempt) + ".tmp");
}

void deleteTempFileBestEffort(const std::filesystem::path& temp) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
}

QdrProjectWriteResult writeTextFile(const std::filesystem::path& temp, std::string_view text) {
    std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return {false, "Could not open temporary QDR file for writing."};
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream.good()) {
        return {false, "Could not write QDR file contents."};
    }
    stream.flush();
    if (!stream.good()) {
        return {false, "Could not flush QDR file contents."};
    }
    stream.close();
    if (!stream.good()) {
        return {false, "Could not close temporary QDR file."};
    }
    return {true, {}};
}

QdrProjectWriteResult validateTemporaryProjectFile(const std::filesystem::path& temp) {
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

QdrProjectWriteResult replaceWithTempFile(const std::filesystem::path& temp, const std::filesystem::path& target) {
#ifdef _WIN32
    if (::MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        return {false, windowsErrorMessage("Could not replace QDR file", ::GetLastError())};
    }
    return {true, {}};
#else
    std::error_code error;
    std::filesystem::rename(temp, target, error);
    if (error) {
        return {false, systemErrorMessage("Could not replace QDR file", error)};
    }
    return {true, {}};
#endif
}

QdrProjectWriteResult writeTextAtomic(const std::filesystem::path& target, std::string_view text) {
    if (target.empty() || target.filename().empty()) {
        return {false, "QDR file path is empty."};
    }

    const std::filesystem::path directory = targetDirectoryForAtomicWrite(target);
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return {false, systemErrorMessage("QDR output directory does not exist", error)};
    }

    constexpr std::uint64_t maxAttempts = 32;
    for (std::uint64_t attempt = 0; attempt < maxAttempts; ++attempt) {
        const std::filesystem::path temp = makeTempPathForAtomicWrite(target, directory, attempt);
        if (std::filesystem::exists(temp, error)) {
            continue;
        }
        if (error) {
            return {false, systemErrorMessage("Could not inspect temporary QDR file path", error)};
        }

        QdrProjectWriteResult writeResult = writeTextFile(temp, text);
        if (!writeResult.ok) {
            deleteTempFileBestEffort(temp);
            return writeResult;
        }

        QdrProjectWriteResult validationResult = validateTemporaryProjectFile(temp);
        if (!validationResult.ok) {
            deleteTempFileBestEffort(temp);
            return validationResult;
        }

        QdrProjectWriteResult replaceResult = replaceWithTempFile(temp, target);
        if (!replaceResult.ok) {
            deleteTempFileBestEffort(temp);
            return replaceResult;
        }

        return {true, {}};
    }

    return {false, "Could not allocate a unique temporary QDR file path."};
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
