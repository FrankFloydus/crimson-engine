////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_document_writer.hpp"

#include "assets/material_document.hpp"
#include "diagnostics/profile.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_format.hpp"
#include "io/qdr/qdr_parallel_tasks.hpp"
#include "io/qdr/qdr_polygon_document_codec.hpp"
#include "io/qdr/qdr_project_codecs.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

using json = nlohmann::ordered_json;

std::string stringFromView(std::string_view value) {
    return std::string(value.data(), value.size());
}

json meshToJson(AssetId id, const MeshAsset& mesh) {
    QDR_PROFILE_FUNCTION();
    json vertices = json::array();
    json indices = json::array();
    json primitives = json::array();
    if (!mesh.hasAuthoredPolygonDocument || !mesh.authoredPolygonDocument) {
        vertices.get_ref<json::array_t&>().reserve(mesh.vertices.size());
        for (const MeshVertex& vertex : mesh.vertices) {
            vertices.push_back(json{
                    {"position", qdrVec3ToJson(vertex.position)},
                    {"normal", qdrVec3ToJson(vertex.normal)},
                    {"uv", qdrVec2ToJson(vertex.uv)},
                    {"rgba", vertex.rgba},
            });
        }

        indices = mesh.indices;
        const std::vector<MeshPrimitiveSpan> normalizedPrimitives = normalizedMeshPrimitiveSpans(mesh);
        primitives.get_ref<json::array_t&>().reserve(normalizedPrimitives.size());
        for (const MeshPrimitiveSpan& primitive : normalizedPrimitives) {
            primitives.push_back(json{
                    {"indexOffset", primitive.indexOffset},
                    {"indexCount", primitive.indexCount},
                    {"materialSlot", primitive.materialSlot},
            });
        }
    }

    json value{
            {"id", id},
            {"name", mesh.name},
            {"hasStableCpuTopology", mesh.hasStableCpuTopology},
            {"vertices", vertices},
            {"indices", indices},
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
            {"usage", stringFromView(qdrTextureUsageName(texture.usage))},
            {"colorSpace", stringFromView(qdrTextureColorSpaceName(texture.colorSpace))},
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
            {"baseColor", qdrVec3ToJson(material.baseColor)},
            {"roughness", material.roughness},
            {"metallic", material.metallic},
            {"textureSlots", textureSlots},
            {"nodeGraphReserved", material.nodeGraphReserved},
    };
}

json entityToJson(const SceneDocument& scene, EntityId entity) {
    QDR_PROFILE_FUNCTION();
    json value{
            {"id", entity},
            {"uuid", scene.uuid(entity)},
            {"editorObjectId", scene.editorObjectId(entity)},
            {"name", scene.name(entity) ? scene.name(entity)->name : std::string{}},
            {"kind", stringFromView(qdrEntityKindName(scene.kind(entity)))},
            {"parent", scene.parent(entity)},
    };
    if (const TransformComponent* transform = scene.transform(entity)) {
        value["transform"] = qdrTransformComponentToJson(*transform);
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
        value["mesh"] = qdrMeshComponentToJson(*mesh);
    }
    if (const MaterialComponent* material = scene.material(entity)) {
        value["material"] = qdrMaterialComponentToJson(*material);
    }
    if (const LightComponent* light = scene.light(entity)) {
        value["light"] = qdrLightComponentToJson(*light);
    }
    if (const SceneSettingsComponent* settings = scene.sceneSettings(entity)) {
        value["sceneRoot"] = scene.isSceneRoot(entity);
        value["sceneSettings"] = qdrSceneSettingsComponentToJson(*settings);
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
    QDR_PROFILE_FUNCTION();
    const SceneDocument& scene = document.scene();
    const AssetRegistry& assets = document.assets();

    json meshes = json::array();
    const std::vector<AssetId> meshIds = assets.meshIds();
    struct MeshToWrite {
        AssetId id = InvalidAssetId;
        const MeshAsset* mesh = nullptr;
        json value;
    };
    std::vector<MeshToWrite> meshEntries;
    meshEntries.reserve(meshIds.size());
    for (const AssetId id : meshIds) {
        if (const MeshAsset* mesh = assets.mesh(id)) {
            meshEntries.push_back(MeshToWrite{.id = id, .mesh = mesh});
        }
    }
    qdr_detail::runParallelIndexTasks(meshEntries.size(), "QDR mesh encode", [&](std::size_t index) {
        meshEntries[index].value = meshToJson(meshEntries[index].id, *meshEntries[index].mesh);
    });
    meshes.get_ref<json::array_t&>().reserve(meshEntries.size());
    for (MeshToWrite& entry : meshEntries) {
        meshes.push_back(std::move(entry.value));
    }

    json materials = json::array();
    const std::vector<AssetId> materialIds = assets.materialIds();
    materials.get_ref<json::array_t&>().reserve(materialIds.size());
    for (const AssetId id : materialIds) {
        if (const MaterialDocument* material = assets.material(id)) {
            materials.push_back(materialToJson(id, *material));
        }
    }

    json textures = json::array();
    const std::vector<AssetId> textureIds = assets.textureIds();
    textures.get_ref<json::array_t&>().reserve(textureIds.size());
    for (const AssetId id : textureIds) {
        if (const TextureAsset* texture = assets.texture(id)) {
            textures.push_back(textureToJson(id, *texture));
        }
    }

    json entities = json::array();
    const std::vector<EntityId> entityIds = scene.entityTreeOrder();
    entities.get_ref<json::array_t&>().reserve(entityIds.size());
    for (const EntityId entity : entityIds) {
        entities.push_back(entityToJson(scene, entity));
    }

    json objectRecords = json::array();
    const std::vector<ProjectObjectRecord> records = document.objectRecords();
    objectRecords.get_ref<json::array_t&>().reserve(records.size());
    for (const ProjectObjectRecord& record : records) {
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

json projectRootToJson(const ProjectDocument& document) {
    return json{
            {"format", stringFromView(kQdrFormatName)},
            {"version", kQdrProjectFormatVersion},
            {stringFromView(kQdrDocumentPayloadKey), projectPayloadToJson(document)},
    };
}

std::string serializeProjectDocumentForDisk(const ProjectDocument& document) {
    QDR_PROFILE_FUNCTION();
    return projectRootToJson(document).dump();
}

QdrProjectWriteResult projectWriteResultFromAtomic(quader_io::QdrAtomicWriteResult result) {
    return {result.ok, std::move(result.message)};
}

} // namespace

std::string QdrDocumentWriter::serializeProjectDocument(const ProjectDocument& document) {
    QDR_PROFILE_FUNCTION();
    return projectRootToJson(document).dump(2);
}

QdrProjectWriteResult QdrDocumentWriter::writeProjectDocumentAtomic(
        const std::filesystem::path& path,
        const ProjectDocument& document) {
    QDR_PROFILE_FUNCTION();
    const std::string serialized = [&]() {
        QDR_PROFILE_SCOPE("QDR serialize project document");
        return serializeProjectDocumentForDisk(document);
    }();
    return projectWriteResultFromAtomic(quader_io::write_qdr_text_atomic(path, serialized));
}

} // namespace quader
