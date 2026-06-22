////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/MaterialDocument.h"
#include "assets/TextureAsset.h"
#include "scene/document/ProjectDocument.h"
#include "io/qdr/QdrDocumentReader.h"
#include "io/qdr/QdrDocumentWriter.h"

#include <gtest/gtest.h>
#include <mesh/polygon/document.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

using namespace quader;

namespace {

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.is_open()) << path.string();
    return {
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}

MeshAsset makeTriangleMesh(std::string name) {
    MeshAsset mesh;
    mesh.name = std::move(name);
    mesh.vertices = {
            MeshVertex{.position = {0.0f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.0f, 0.0f}},
            MeshVertex{.position = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {1.0f, 0.0f}},
            MeshVertex{.position = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.0f, 1.0f}},
    };
    mesh.indices = {0u, 1u, 2u};
    mesh.primitives = {MeshPrimitiveSpan{.indexOffset = 0u, .indexCount = 3u, .materialSlot = 2u}};
    return mesh;
}

TextureAsset makeTextureAsset(
        std::string name,
        TextureUsage usage,
        TextureColorSpace colorSpace,
        std::uint32_t width,
        std::uint32_t height,
        std::string sourcePath,
        bool generated = false) {
    TextureAsset texture;
    texture.name = std::move(name);
    texture.usage = usage;
    texture.colorSpace = colorSpace;
    texture.width = width;
    texture.height = height;
    texture.sourcePath = std::filesystem::path(std::move(sourcePath));
    texture.generated = generated;
    return texture;
}

void writeTextFile(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.is_open()) << path.string();
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    ASSERT_TRUE(file.good()) << path.string();
}

std::filesystem::path uniqueTempDirectory(std::string_view label) {
    const auto tick = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("quader-qdr-" + std::string(label) + "-" + std::to_string(tick));
}

/**
 * Owns a temporary QDR test directory and removes it when the test scope exits.
 */
class ScopedTempDirectory {
public:
    explicit ScopedTempDirectory(std::string_view label)
            : path_(uniqueTempDirectory(label)) {
        std::error_code error;
        std::filesystem::create_directories(path_, error);
        EXPECT_FALSE(error) << error.message();
    }

    ~ScopedTempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool containsTempQdrFiles(const std::filesystem::path& directory) {
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string().find(".tmp") != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    const std::size_t position = text.find(from);
    EXPECT_NE(position, std::string::npos);
    if (position == std::string::npos) {
        return text;
    }
    text.replace(position, from.size(), to);
    return text;
}

} // namespace

TEST(QdrDocumentIoTests, WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings) {
    ProjectDocument document(ProjectDocumentRecord{.id = 77u, .name = "Projection IO"});

    const AssetId meshId = document.assets().addMesh(12u, makeTriangleMesh("Triangle Mesh"));
    const AssetId baseColorTextureId = document.assets().addTexture(31u, makeTextureAsset(
            "Brick Albedo",
            TextureUsage::BaseColor,
            TextureColorSpace::Srgb,
            16u,
            16u,
            "textures/brick_albedo.png"));
    const AssetId roughnessTextureId = document.assets().addTexture(32u, makeTextureAsset(
            "Brick Roughness",
            TextureUsage::Roughness,
            TextureColorSpace::Linear,
            8u,
            8u,
            "textures/generated/brick_roughness.png",
            true));
    MaterialDocument material = makeDefaultQuaderMaterial("Brick", baseColorTextureId, roughnessTextureId);
    material.baseColor = {0.8f, 0.2f, 0.1f};
    material.roughness = 0.45f;
    const AssetId materialId = document.assets().addMaterial(22u, std::move(material));

    const EntityId group = document.scene().createEntityWithId(100u, "Group", EntityKind::Empty);
    const EntityId wall = document.scene().createEntityWithId(200u, "Wall", EntityKind::Mesh);
    const EditorObjectId groupObject = document.scene().editorObjectId(group);
    document.scene().setParent(wall, group, false);
    document.scene().setTransform(wall, TransformComponent{.position = {2.0f, 3.0f, 4.0f}});
    document.scene().setModelingObject(wall, ModelingObjectComponent{.objectId = 42, .materialSlot = 2u});
    document.scene().setMesh(wall, MeshComponent{.mesh = meshId});
    document.scene().setMaterial(wall, MaterialComponent{.material = materialId});
    document.scene().setSelected(wall, true);
    document.scene().setActiveSelection(wall);
    document.upsertObjectRecord(ProjectObjectRecord{
            .id = 900u,
            .sceneEntity = wall,
            .mesh = meshId,
            .material = materialId,
            .modelingObject = 42,
            .authoredName = "Wall",
            .visible = true,
            .materialSlot = 2u,
    });
    const EditorObjectId wallObject = document.scene().editorObjectId(wall);
    ASSERT_EQ(wallObject, 900u);

    const std::string text = QdrDocumentWriter::serializeProjectDocument(document);
    EXPECT_NE(text.find("\"kind\": \"quader.project-document\""), std::string::npos);
    EXPECT_NE(text.find("\"editorObjectId\": " + std::to_string(groupObject)), std::string::npos);
    EXPECT_NE(text.find("\"editorObjectId\": 900"), std::string::npos);
    EXPECT_NE(text.find("\"textures\": ["), std::string::npos);
    EXPECT_NE(text.find("\"baseColorTexture\": 31"), std::string::npos);
    EXPECT_NE(text.find("\"roughnessTexture\": 32"), std::string::npos);
    EXPECT_EQ(text.find("\"session\""), std::string::npos);

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.migratedLegacySession);

    const ProjectDocument& loaded = result.document;
    EXPECT_EQ(loaded.id(), 77u);
    EXPECT_EQ(loaded.name(), "Projection IO");
    EXPECT_TRUE(loaded.scene().exists(group));
    EXPECT_TRUE(loaded.scene().exists(wall));
    EXPECT_EQ(loaded.scene().editorObjectId(group), groupObject);
    EXPECT_EQ(loaded.scene().editorObjectId(wall), wallObject);
    EXPECT_EQ(loaded.scene().parent(wall), group);
    ASSERT_NE(loaded.scene().transform(wall), nullptr);
    EXPECT_FLOAT_EQ(loaded.scene().transform(wall)->position.x, 2.0f);
    ASSERT_NE(loaded.scene().mesh(wall), nullptr);
    EXPECT_EQ(loaded.scene().mesh(wall)->mesh, meshId);
    ASSERT_NE(loaded.scene().material(wall), nullptr);
    EXPECT_EQ(loaded.scene().material(wall)->material, materialId);
    ASSERT_NE(loaded.scene().modelingObject(wall), nullptr);
    EXPECT_EQ(loaded.scene().modelingObject(wall)->objectId, 42);
    EXPECT_EQ(loaded.scene().primarySelection(), wall);
    ASSERT_NE(loaded.assets().mesh(meshId), nullptr);
    EXPECT_EQ(loaded.assets().mesh(meshId)->indices.size(), 3u);
    ASSERT_NE(loaded.assets().material(materialId), nullptr);
    EXPECT_FLOAT_EQ(loaded.assets().material(materialId)->roughness, 0.45f);
    ASSERT_NE(loaded.assets().texture(baseColorTextureId), nullptr);
    EXPECT_EQ(loaded.assets().texture(baseColorTextureId)->name, "Brick Albedo");
    EXPECT_EQ(loaded.assets().texture(baseColorTextureId)->usage, TextureUsage::BaseColor);
    EXPECT_EQ(loaded.assets().texture(baseColorTextureId)->colorSpace, TextureColorSpace::Srgb);
    EXPECT_EQ(loaded.assets().texture(baseColorTextureId)->width, 16u);
    EXPECT_EQ(loaded.assets().texture(baseColorTextureId)->height, 16u);
    EXPECT_EQ(loaded.assets().texture(baseColorTextureId)->sourcePath.generic_string(), "textures/brick_albedo.png");
    ASSERT_NE(loaded.assets().texture(roughnessTextureId), nullptr);
    EXPECT_EQ(loaded.assets().texture(roughnessTextureId)->usage, TextureUsage::Roughness);
    EXPECT_EQ(loaded.assets().texture(roughnessTextureId)->colorSpace, TextureColorSpace::Linear);
    EXPECT_TRUE(loaded.assets().texture(roughnessTextureId)->generated);
    ASSERT_TRUE(loaded.assets().material(materialId)->textureSlots.contains(MaterialTextureSlot::BaseColor));
    EXPECT_EQ(loaded.assets().material(materialId)->textureSlots.at(MaterialTextureSlot::BaseColor), baseColorTextureId);
    ASSERT_TRUE(loaded.assets().material(materialId)->textureSlots.contains(MaterialTextureSlot::Roughness));
    EXPECT_EQ(loaded.assets().material(materialId)->textureSlots.at(MaterialTextureSlot::Roughness), roughnessTextureId);
    ASSERT_NE(loaded.objectRecord(900u), nullptr);
    EXPECT_EQ(loaded.objectRecord(900u)->sceneEntity, wall);
    const ProjectDocumentIdentityReport identity = loaded.validateObjectIdentityInvariants();
    EXPECT_TRUE(identity.ok()) << (identity.issues.empty() ? std::string{} : identity.issues.front().message);
}

TEST(QdrDocumentIoTests, ImportsLegacySessionPayloadButWritesProjectDocumentPayload) {
    const std::filesystem::path fixture =
            std::filesystem::path(QUADER_SOURCE_DIR) / "tests" / "fixtures" / "qdr" / "legacy_session_payload.qdr";
    const std::string text = readTextFile(fixture);

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.migratedLegacySession);

    const std::vector<EntityId> entities = result.document.scene().entities();
    ASSERT_EQ(entities.size(), 1u);
    const EntityId entity = entities.front();
    EXPECT_NE(entity, static_cast<EntityId>(42u));
    ASSERT_NE(result.document.scene().modelingObject(entity), nullptr);
    EXPECT_EQ(result.document.scene().modelingObject(entity)->objectId, 42);
    EXPECT_EQ(result.document.scene().modelingObject(entity)->materialSlot, 7u);
    ASSERT_NE(result.document.scene().mesh(entity), nullptr);
    const MeshAsset* mesh = result.document.assets().mesh(result.document.scene().mesh(entity)->mesh);
    ASSERT_NE(mesh, nullptr);
    EXPECT_GE(mesh->vertices.size(), 3u);
    EXPECT_TRUE(mesh->hasAuthoredPolygonDocument);
    ASSERT_TRUE(mesh->authoredPolygonDocument);
    EXPECT_EQ(mesh->authoredPolygonDocument.get()->faces.size(), 1u);

    const std::string rewritten = QdrDocumentWriter::serializeProjectDocument(result.document);
    EXPECT_NE(rewritten.find("\"kind\": \"quader.project-document\""), std::string::npos);
    EXPECT_EQ(rewritten.find("\"session\""), std::string::npos);

    QdrProjectReadResult roundTrip = QdrDocumentReader::deserializeProjectDocument(rewritten);
    ASSERT_TRUE(roundTrip.ok) << roundTrip.message;
    EXPECT_FALSE(roundTrip.migratedLegacySession);
}

TEST(QdrDocumentIoTests, RejectsUnknownCanonicalProjectPayloadKindWithoutLegacyMigration) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.legacy-session"
  },
  "session": {
    "nextObjectId": 2,
    "activeObjectId": -1,
    "selectionMode": "object",
    "objects": []
  }
})json";

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migratedLegacySession);
    EXPECT_NE(result.message.find("Unsupported QDR project document payload kind"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithDanglingObjectRecordEntity) {
    ProjectDocument document(ProjectDocumentRecord{.id = 78u, .name = "Dangling Record"});
    const EntityId entity = document.scene().createEntityWithId(200u, "Object", EntityKind::Mesh);
    document.upsertObjectRecord(ProjectObjectRecord{
            .id = 900u,
            .sceneEntity = entity,
            .authoredName = "Object",
    });

    std::string text = QdrDocumentWriter::serializeProjectDocument(document);
    text = replaceOnce(text, "\"sceneEntity\": 200", "\"sceneEntity\": 999");

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migratedLegacySession);
    EXPECT_NE(result.message.find("object record points at a missing scene entity"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithMismatchedEditorObjectRecordId) {
    ProjectDocument document(ProjectDocumentRecord{.id = 80u, .name = "Conflicting Record"});
    const EntityId first = document.scene().createEntityWithId(100u, "First", EntityKind::Mesh);
    const EntityId second = document.scene().createEntityWithId(200u, "Second", EntityKind::Mesh);
    const EditorObjectId firstObject = document.scene().editorObjectId(first);
    const EditorObjectId secondObject = document.scene().editorObjectId(second);
    ASSERT_NE(firstObject, InvalidEditorObjectId);
    ASSERT_NE(secondObject, InvalidEditorObjectId);
    ASSERT_NE(firstObject, secondObject);
    document.upsertObjectRecord(ProjectObjectRecord{
            .id = firstObject,
            .sceneEntity = first,
            .authoredName = "First",
    });
    document.upsertObjectRecord(ProjectObjectRecord{
            .id = secondObject,
            .sceneEntity = second,
            .authoredName = "Second",
    });

    std::string text = QdrDocumentWriter::serializeProjectDocument(document);
    text = replaceOnce(
            text,
            "\"editorObjectId\": " + std::to_string(secondObject) + ",\n        \"sceneEntity\": 200",
            "\"editorObjectId\": 999,\n        \"sceneEntity\": 200");

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migratedLegacySession);
    EXPECT_NE(result.message.find("does not match the scene editor object id"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithMissingMeshAssetReference) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 79,
      "name": "Missing Mesh"
    },
    "assets": {
      "meshes": [],
      "materials": []
    },
    "entities": [
      {
        "id": 200,
        "name": "Object",
        "kind": "mesh",
        "parent": 0,
        "mesh": {
          "mesh": 404
        }
      }
    ],
    "objects": []
  }
})json";

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migratedLegacySession);
    EXPECT_NE(result.message.find("entity references a missing mesh asset"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithDanglingMaterialTextureReference) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 81,
      "name": "Missing Texture"
    },
    "assets": {
      "meshes": [],
      "materials": [
        {
          "id": 22,
          "name": "Textured Material",
          "baseColor": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "roughness": 1,
          "metallic": 0,
          "textureSlots": {
            "baseColorTexture": 404
          },
          "nodeGraphReserved": true
        }
      ],
      "textures": []
    },
    "entities": [],
    "objects": []
  }
})json";

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migratedLegacySession);
    EXPECT_NE(result.message.find("material references a missing texture asset"), std::string::npos);
}

TEST(QdrDocumentIoTests, ProjectAtomicWriterRejectsDanglingMaterialTextureBeforeReplace) {
    ScopedTempDirectory directory("qdr-project-atomic-texture-failure");
    const std::filesystem::path target = directory.path() / "project.qdr";
    constexpr std::string_view existingText = "old project bytes";
    writeTextFile(target, existingText);

    ProjectDocument document(ProjectDocumentRecord{.id = 82u, .name = "Bad Texture Ref"});
    MaterialDocument material = makeDefaultQuaderMaterial("Broken Material");
    material.textureSlots[MaterialTextureSlot::BaseColor] = 404u;
    const AssetId materialId = document.assets().addMaterial(22u, std::move(material));
    const EntityId entity = document.scene().createEntityWithId(200u, "Object", EntityKind::Mesh);
    document.scene().setMaterial(entity, MaterialComponent{.material = materialId});

    const QdrProjectWriteResult result = QdrDocumentWriter::writeProjectDocumentAtomic(target, document);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("material references a missing texture asset"), std::string::npos);
    EXPECT_EQ(readTextFile(target), std::string(existingText));
    EXPECT_FALSE(containsTempQdrFiles(directory.path()));
}

TEST(ProjectionOwnershipGuardrailTests, ProjectionIsReadOnlyAndModelingOwnsSceneSync) {
    const std::string cmake = readTextFile(std::filesystem::path(QUADER_SOURCE_DIR) / "CMakeLists.txt");
    EXPECT_NE(cmake.find("src/editor/projection/MeshProjectionService.cpp"), std::string::npos);
    EXPECT_EQ(cmake.find("src/editor/projection/ModelingSceneReconcileService.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("src/editor/projection/PickingCacheProjection.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("src/editor/projection/SelectionOverlayProjection.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("src/editor/modeling/services/ModelingMeshSyncService.cpp"), std::string::npos);

    const std::string services =
            readTextFile(std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "modeling" / "services" / "ModelingSessionServices.cpp");
    EXPECT_EQ(services.find("ModelingSceneReconcileService::reconcileModelingObjectSceneNodes"), std::string::npos);
    EXPECT_EQ(services.find("MeshProjectionService::projectModelingMeshesToScene"), std::string::npos);
    EXPECT_NE(services.find("PickingCacheProjection::sceneTransformedObjectsForPicking"), std::string::npos);
    EXPECT_NE(services.find("PickingCacheProjection::objectFaceGeometrySignature"), std::string::npos);

    const std::string meshSync =
            readTextFile(std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "modeling" / "services" / "ModelingMeshSyncService.cpp");
    EXPECT_NE(meshSync.find("void reconcileModelingObjectSceneNodes"), std::string::npos);
    EXPECT_NE(meshSync.find("void projectModelingMeshesToScene"), std::string::npos);
    EXPECT_NE(meshSync.find("scene.createEntity"), std::string::npos);
    EXPECT_NE(meshSync.find("scene.destroyEntity"), std::string::npos);
    EXPECT_NE(meshSync.find("assets.addMesh"), std::string::npos);
    EXPECT_NE(meshSync.find("assets.replaceMesh"), std::string::npos);

    const char* forbiddenProjectionMutations[] = {
            "AssetRegistry",
            "SceneModelingSyncService",
            "createEntity(",
            "createEntityWithId(",
            "destroyEntity(",
            "setModelingObject(",
            "setMesh(",
            "setMaterial(",
            "addMesh(",
            "replaceMesh(",
            ".bind(",
            ".unbindObject(",
            "markSceneObjectsSynced(",
            "clearSelection(",
            "setSelected(",
    };
    const std::filesystem::path projectionRoot = std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "projection";
    for (const std::filesystem::directory_entry& entry :
            std::filesystem::recursive_directory_iterator(projectionRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string extension = entry.path().extension().string();
        if (extension != ".h" && extension != ".cpp") {
            continue;
        }
        const std::string source = readTextFile(entry.path());
        for (const char* forbidden : forbiddenProjectionMutations) {
            EXPECT_EQ(source.find(forbidden), std::string::npos)
                    << entry.path().string() << " contains projection-owned mutation token " << forbidden;
        }
    }

    const std::string facade =
            readTextFile(std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "modeling" / "services" / "ModelingSessionService.cpp");
    EXPECT_NE(facade.find("SelectionOverlayProjection::projectSelectionOverlayFrame"), std::string::npos);
}

TEST(ProjectionOwnershipGuardrailTests, RenderDoesNotDependOnProjectionOrModelingOwners) {
    const std::filesystem::path renderRoot = std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render";
    for (const std::filesystem::directory_entry& entry :
            std::filesystem::recursive_directory_iterator(renderRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string extension = entry.path().extension().string();
        if (extension != ".h" && extension != ".cpp") {
            continue;
        }
        const std::string source = readTextFile(entry.path());
        EXPECT_EQ(source.find("editor/projection/"), std::string::npos) << entry.path().string();
        EXPECT_EQ(source.find("#include \"modeling/"), std::string::npos) << entry.path().string();
    }
}
