////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/material_document.hpp"
#include "assets/texture_asset.hpp"
#include "scene/document/project_document.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/qdr/qdr_document.hpp"
#include "io/qdr/qdr_format.hpp"
#include "io/qdr/qdr_project_codecs.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>
#include <mesh/polygon/document.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using namespace quader;

namespace {

using quader::test::guardrail::readFile;
using quader::test::guardrail::readSourceFile;
using quader::test::guardrail::sourceFilesUnder;
using quader::test::guardrail::sourcePath;

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

quader_poly::Document makeAuthoredPolygonDocument() {
    quader_poly::Document document;
    document.next_vertex_id = 13u;
    document.next_face_id = 21u;
    document.vertices = {
            quader_poly::Vertex{.id = 10u, .position = {0.0f, 0.0f, 0.0f}},
            quader_poly::Vertex{.id = 11u, .position = {1.0f, 0.0f, 0.0f}},
            quader_poly::Vertex{.id = 12u, .position = {0.0f, 1.0f, 0.0f}},
    };
    quader_poly::Face face;
    face.id = 20u;
    face.vertices = {10u, 11u, 12u};
    face.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    face.material_slot = 2u;
    face.normal_shading = quader_poly::SurfaceShading::authored;
    document.faces.push_back(std::move(face));
    document.hard_normal_edges.push_back(quader_poly::make_edge(10u, 11u));
    return document;
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
    MaterialDocument accentMaterial = makeDefaultQuaderMaterial("Accent");
    accentMaterial.baseColor = {0.1f, 0.8f, 0.2f};
    const AssetId accentMaterialId = document.assets().addMaterial(23u, std::move(accentMaterial));

    const EntityId group = document.scene().createEntityWithId(100u, "Group", EntityKind::Empty);
    const EntityId wall = document.scene().createEntityWithId(200u, "Wall", EntityKind::Mesh);
    const std::string groupUuid = document.scene().uuid(group);
    const std::string wallUuid = document.scene().uuid(wall);
    const EditorObjectId groupObject = document.scene().editorObjectId(group);
    document.scene().setParent(wall, group, false);
    document.scene().setTransform(wall, TransformComponent{.position = {2.0f, 3.0f, 4.0f}});
    document.scene().setModelingObject(wall, ModelingObjectComponent{.objectId = 42, .materialSlot = 2u});
    document.scene().setMesh(wall, MeshComponent{.mesh = meshId});
    document.scene().setMaterial(wall, MaterialComponent{
            .material = materialId,
            .materialSlots = {{2u, accentMaterialId}},
    });
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
    EXPECT_NE(text.find("\"uuid\": \"" + groupUuid + "\""), std::string::npos);
    EXPECT_NE(text.find("\"uuid\": \"" + wallUuid + "\""), std::string::npos);
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
    EXPECT_EQ(loaded.scene().uuid(group), groupUuid);
    EXPECT_EQ(loaded.scene().uuid(wall), wallUuid);
    EXPECT_EQ(loaded.scene().entityForUuid(groupUuid), group);
    EXPECT_EQ(loaded.scene().entityForUuid(wallUuid), wall);
    EXPECT_EQ(loaded.scene().editorObjectId(group), groupObject);
    EXPECT_EQ(loaded.scene().editorObjectId(wall), wallObject);
    EXPECT_EQ(loaded.scene().parent(wall), group);
    ASSERT_NE(loaded.scene().transform(wall), nullptr);
    EXPECT_FLOAT_EQ(loaded.scene().transform(wall)->position.x, 2.0f);
    ASSERT_NE(loaded.scene().mesh(wall), nullptr);
    EXPECT_EQ(loaded.scene().mesh(wall)->mesh, meshId);
    ASSERT_NE(loaded.scene().material(wall), nullptr);
    EXPECT_EQ(loaded.scene().material(wall)->material, materialId);
    EXPECT_EQ(loaded.scene().material(wall)->materialForSlot(2u), accentMaterialId);
    ASSERT_NE(loaded.scene().modelingObject(wall), nullptr);
    EXPECT_EQ(loaded.scene().modelingObject(wall)->objectId, 42);
    EXPECT_EQ(loaded.scene().primarySelection(), wall);
    ASSERT_NE(loaded.assets().mesh(meshId), nullptr);
    EXPECT_EQ(loaded.assets().mesh(meshId)->indices.size(), 3u);
    ASSERT_NE(loaded.assets().material(materialId), nullptr);
    EXPECT_FLOAT_EQ(loaded.assets().material(materialId)->roughness, 0.45f);
    ASSERT_NE(loaded.assets().material(accentMaterialId), nullptr);
    EXPECT_FLOAT_EQ(loaded.assets().material(accentMaterialId)->baseColor.y, 0.8f);
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
    const std::optional<ProjectObjectRecord> loadedRecord = loaded.objectRecord(900u);
    ASSERT_TRUE(loadedRecord.has_value());
    EXPECT_EQ(loadedRecord->sceneEntity, wall);
    const ProjectDocumentIdentityReport identity = loaded.validateObjectIdentityInvariants();
    EXPECT_TRUE(identity.ok()) << (identity.issues.empty() ? std::string{} : identity.issues.front().message);
}

TEST(QdrDocumentIoTests, WritesAndReadsUnreferencedProjectAssets) {
    ProjectDocument document(ProjectDocumentRecord{.id = 88u, .name = "Unreferenced Assets"});
    const AssetId meshId = document.assets().addMesh(12u, makeTriangleMesh("Standalone Mesh"));
    const AssetId textureId = document.assets().addTexture(31u, makeTextureAsset(
            "Unassigned Albedo",
            TextureUsage::BaseColor,
            TextureColorSpace::Srgb,
            4u,
            4u,
            "textures/unassigned.png"));
    const AssetId materialId = document.assets().addMaterial(
            22u,
            makeDefaultQuaderMaterial("Unassigned Material", textureId, textureId));
    ASSERT_NE(meshId, InvalidAssetId);
    ASSERT_NE(textureId, InvalidAssetId);
    ASSERT_NE(materialId, InvalidAssetId);

    const std::string text = QdrDocumentWriter::serializeProjectDocument(document);
    EXPECT_NE(text.find("\"id\": 12"), std::string::npos);
    EXPECT_NE(text.find("\"id\": 22"), std::string::npos);
    EXPECT_NE(text.find("\"id\": 31"), std::string::npos);

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_NE(result.document.assets().mesh(meshId), nullptr);
    EXPECT_NE(result.document.assets().material(materialId), nullptr);
    EXPECT_NE(result.document.assets().texture(textureId), nullptr);
}

TEST(QdrDocumentIoTests, MissingMeshDoubleSidedFieldDefaultsToSingleSided) {
    ProjectDocument document(ProjectDocumentRecord{.id = 89u, .name = "Mesh Defaults"});
    const AssetId meshId = document.assets().addMesh(12u, makeTriangleMesh("Triangle Mesh"));
    const EntityId wall = document.scene().createEntityWithId(200u, "Wall", EntityKind::Mesh);
    document.scene().setMesh(wall, MeshComponent{
            .mesh = meshId,
            .doubleSided = true,
    });

    std::string text = QdrDocumentWriter::serializeProjectDocument(document);
    const std::size_t doubleSidedKey = text.find("\"doubleSided\": true");
    ASSERT_NE(doubleSidedKey, std::string::npos);
    const std::size_t lineBegin = text.rfind('\n', doubleSidedKey);
    const std::size_t lineEnd = text.find('\n', doubleSidedKey);
    ASSERT_NE(lineBegin, std::string::npos);
    ASSERT_NE(lineEnd, std::string::npos);
    text.erase(lineBegin + 1u, lineEnd - lineBegin);

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(result.document.scene().mesh(wall), nullptr);
    EXPECT_FALSE(result.document.scene().mesh(wall)->doubleSided);
}

TEST(QdrDocumentIoTests, ImportsLegacySessionPayloadButWritesProjectDocumentPayload) {
    const std::string text = readSourceFile({"tests", "fixtures", "qdr", "legacy_session_payload.qdr"});

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

TEST(QdrDocumentIoTests, RootHeaderParsingUsesSharedHelperAcrossReaderSurfaces) {
    QdrProjectReadResult missingRootProject = QdrDocumentReader::deserializeProjectDocument("[]");
    EXPECT_FALSE(missingRootProject.ok);
    EXPECT_NE(missingRootProject.message.find("root must be an object"), std::string::npos);

    const std::string oversizedVersion = R"json({
  "format": "quader.qdr",
  "version": 2147483648,
  "document": {}
})json";
    QdrProjectReadResult oversizedProject = QdrDocumentReader::deserializeProjectDocument(oversizedVersion);
    EXPECT_FALSE(oversizedProject.ok);
    EXPECT_NE(oversizedProject.message.find("outside the supported integer range"), std::string::npos);

    const quader_io::QdrReadResult missingRootLegacy = quader_io::deserialize_document("[]");
    EXPECT_FALSE(missingRootLegacy.ok);
    EXPECT_NE(missingRootLegacy.message.find("root must be an object"), std::string::npos);

    const quader_io::QdrReadResult oversizedLegacy = quader_io::deserialize_document(oversizedVersion);
    EXPECT_FALSE(oversizedLegacy.ok);
    EXPECT_NE(oversizedLegacy.message.find("outside the supported integer range"), std::string::npos);

    const std::string qdrRootHeader = readSourceFile("src/io/qdr/qdr_root.hpp");
    const std::string legacyReader = readSourceFile("src/io/qdr/qdr_document.cpp");
    const std::string projectReader = readSourceFile("src/io/qdr/qdr_document_reader.cpp");
    EXPECT_NE(qdrRootHeader.find("QdrRootHeader"), std::string::npos);
    EXPECT_NE(legacyReader.find("read_qdr_root_header(root)"), std::string::npos);
    EXPECT_NE(projectReader.find("read_qdr_root_header(root)"), std::string::npos);
    EXPECT_EQ(legacyReader.find("read_version_header"), std::string::npos);
    EXPECT_EQ(projectReader.find("readVersionHeader"), std::string::npos);
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
    EXPECT_NE(result.message.find("Texture asset does not exist"), std::string::npos);
}

TEST(QdrDocumentIoTests, MigratesLegacyMaterialTextureSlotAliasesToCanonicalSlots) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 83,
      "name": "Aliased Texture Slot"
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
            "baseColor": 31
          },
          "nodeGraphReserved": true
        }
      ],
      "textures": [
        {
          "id": 31,
          "name": "Base Texture",
          "usage": "baseColor",
          "colorSpace": "srgb",
          "width": 1,
          "height": 1,
          "sourcePath": "",
          "generated": true
        }
      ]
    },
    "entities": [
      {
        "id": 200,
        "name": "Material Entity",
        "kind": "generic",
        "parent": 0,
        "material": {
          "material": 22
        }
      }
    ],
    "objects": []
  }
})json";

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    ASSERT_TRUE(result.ok) << result.message;
    const MaterialDocument* material = result.document.assets().material(22u);
    ASSERT_NE(material, nullptr);
    ASSERT_TRUE(material->textureSlots.contains(MaterialTextureSlot::BaseColor));
    EXPECT_EQ(material->textureSlots.at(MaterialTextureSlot::BaseColor), 31u);
    EXPECT_FALSE(material->textureSlots.contains("baseColor"));

    const std::string rewritten = QdrDocumentWriter::serializeProjectDocument(result.document);
    EXPECT_NE(rewritten.find("\"baseColorTexture\": 31"), std::string::npos);
    EXPECT_EQ(rewritten.find("\"baseColor\": 31"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithUnsupportedMaterialTextureSlot) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 84,
      "name": "Unsupported Texture Slot"
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
            "normalTexture": 31
          },
          "nodeGraphReserved": true
        }
      ],
      "textures": [
        {
          "id": 31,
          "name": "Normal Texture",
          "usage": "normal",
          "colorSpace": "linear",
          "width": 1,
          "height": 1,
          "sourcePath": "",
          "generated": true
        }
      ]
    },
    "entities": [],
    "objects": []
  }
})json";

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migratedLegacySession);
    EXPECT_NE(result.message.find("Unsupported material texture slot"), std::string::npos);
}

TEST(QdrDocumentIoTests, RegistryRejectsDanglingMaterialTextureBeforeProjectWrite) {
    ProjectDocument document(ProjectDocumentRecord{.id = 82u, .name = "Bad Texture Ref"});
    MaterialDocument material = makeDefaultQuaderMaterial("Broken Material");
    material.textureSlots[MaterialTextureSlot::BaseColor] = 404u;
    const AssetId materialId = document.assets().addMaterial(22u, std::move(material));
    EXPECT_EQ(materialId, InvalidAssetId);
    EXPECT_EQ(document.assets().materialCount(), 0u);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithDuplicateMeshAssetIds) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 85,
      "name": "Duplicate Mesh"
    },
    "assets": {
      "meshes": [
        {
          "id": 12,
          "name": "First",
          "hasStableCpuTopology": true,
          "vertices": [],
          "indices": [],
          "primitives": []
        },
        {
          "id": 12,
          "name": "Second",
          "hasStableCpuTopology": true,
          "vertices": [],
          "indices": [],
          "primitives": []
        }
      ],
      "materials": [],
      "textures": []
    },
    "entities": [],
    "objects": []
  }
})json";

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("duplicate mesh asset ids"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithDuplicateMaterialAssetIds) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 86,
      "name": "Duplicate Material"
    },
    "assets": {
      "meshes": [],
      "materials": [
        {
          "id": 22,
          "name": "First",
          "baseColor": {"x": 1, "y": 1, "z": 1},
          "roughness": 1,
          "metallic": 0,
          "textureSlots": {},
          "nodeGraphReserved": true
        },
        {
          "id": 22,
          "name": "Second",
          "baseColor": {"x": 1, "y": 1, "z": 1},
          "roughness": 1,
          "metallic": 0,
          "textureSlots": {},
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
    EXPECT_NE(result.message.find("duplicate material asset ids"), std::string::npos);
}

TEST(QdrDocumentIoTests, NormalizesMeshPrimitiveSpansAtQdrBoundary) {
    ProjectDocument document(ProjectDocumentRecord{.id = 87u, .name = "Primitive Normalize"});
    MeshAsset mesh = makeTriangleMesh("Invalid Primitive Mesh");
    mesh.primitives = {MeshPrimitiveSpan{.indexOffset = 12u, .indexCount = 3u, .materialSlot = 7u}};
    const AssetId meshId = document.assets().addMesh(12u, std::move(mesh));
    ASSERT_NE(meshId, InvalidAssetId);

    const std::string text = QdrDocumentWriter::serializeProjectDocument(document);
    EXPECT_NE(text.find("\"indexOffset\": 0"), std::string::npos);
    EXPECT_NE(text.find("\"indexCount\": 3"), std::string::npos);
    EXPECT_NE(text.find("\"materialSlot\": 0"), std::string::npos);
    EXPECT_EQ(text.find("\"indexOffset\": 12"), std::string::npos);

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    ASSERT_TRUE(result.ok) << result.message;
    const MeshAsset* loaded = result.document.assets().mesh(meshId);
    ASSERT_NE(loaded, nullptr);
    ASSERT_EQ(loaded->primitives.size(), 1u);
    EXPECT_EQ(loaded->primitives.front().indexOffset, 0u);
    EXPECT_EQ(loaded->primitives.front().indexCount, 3u);
    EXPECT_EQ(loaded->primitives.front().materialSlot, 0u);
}

TEST(QdrDocumentIoTests, SceneSelectablePersistsThroughQdrUsingSceneOwnedMutation) {
    ProjectDocument document(ProjectDocumentRecord{.id = 90u, .name = "Selectable Persistence"});
    const EntityId entity = document.scene().createEntityWithId(200u, "Pick Masked", EntityKind::Mesh);
    ASSERT_NE(entity, InvalidEntityId);
    const std::uint64_t before = document.scene().revision();
    EXPECT_TRUE(document.scene().setSelectable(entity, false));
    EXPECT_GT(document.scene().revision(), before);

    const std::string text = QdrDocumentWriter::serializeProjectDocument(document);
    EXPECT_NE(text.find("\"selectable\": false"), std::string::npos);

    QdrProjectReadResult result = QdrDocumentReader::deserializeProjectDocument(text);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(result.document.scene().exists(entity));
    const VisibilityComponent* visibility = result.document.scene().visibility(entity);
    ASSERT_NE(visibility, nullptr);
    EXPECT_TRUE(visibility->visible);
    EXPECT_FALSE(visibility->selectable);
}

TEST(QdrDocumentIoTests, SharedPolygonDocumentCodecPreservesAuthoredNormalShadingAcrossProjectAndLegacyQdr) {
    const quader_poly::Document polygon = makeAuthoredPolygonDocument();

    ProjectDocument project(ProjectDocumentRecord{.id = 91u, .name = "Authored Normals"});
    MeshAsset authoredMesh;
    authoredMesh.name = "Authored Polygon Mesh";
    authoredMesh.hasAuthoredPolygonDocument = true;
    authoredMesh.authoredPolygonDocument = std::make_shared<quader_poly::Document>(polygon);
    const AssetId meshId = project.assets().addMesh(12u, std::move(authoredMesh));
    ASSERT_NE(meshId, InvalidAssetId);

    const std::string projectText = QdrDocumentWriter::serializeProjectDocument(project);
    EXPECT_NE(projectText.find("\"normalShading\": \"authored\""), std::string::npos);
    QdrProjectReadResult projectRoundTrip = QdrDocumentReader::deserializeProjectDocument(projectText);
    ASSERT_TRUE(projectRoundTrip.ok) << projectRoundTrip.message;
    const MeshAsset* loadedMesh = projectRoundTrip.document.assets().mesh(meshId);
    ASSERT_NE(loadedMesh, nullptr);
    ASSERT_TRUE(loadedMesh->authoredPolygonDocument);
    ASSERT_EQ(loadedMesh->authoredPolygonDocument->faces.size(), 1u);
    EXPECT_EQ(loadedMesh->authoredPolygonDocument->faces.front().normal_shading, quader_poly::SurfaceShading::authored);

    quader_io::QdrObject object;
    object.id = 1;
    object.material_id = 2;
    object.selected = true;
    object.document = polygon;
    object.selection.mode = quader_poly::SelectionMode::face;
    object.selection.faces = {20u};
    object.selection.has_active = true;
    object.selection.active_kind = quader_poly::ElementKind::face;
    object.selection.active_face = 20u;

    quader_io::QdrDocument legacy;
    legacy.objects.push_back(std::move(object));
    legacy.next_object_id = 2;
    legacy.active_object_id = 1;
    legacy.selection_mode = quader_io::QdrSelectionMode::face;

    const std::string legacyText = quader_io::serialize_document(legacy);
    EXPECT_NE(legacyText.find("\"normalShading\": \"authored\""), std::string::npos);
    const quader_io::QdrReadResult legacyRoundTrip = quader_io::deserialize_document(legacyText);
    ASSERT_TRUE(legacyRoundTrip.ok) << legacyRoundTrip.message;
    ASSERT_EQ(legacyRoundTrip.document.objects.front().document.faces.size(), 1u);
    EXPECT_EQ(
            legacyRoundTrip.document.objects.front().document.faces.front().normal_shading,
            quader_poly::SurfaceShading::authored);
}

TEST(QdrDocumentIoTests, QdrPolygonDocumentCodecAndAtomicWriteHaveSingleOwners) {
    const std::string reader = readSourceFile({"src", "io", "qdr", "qdr_document_reader.cpp"});
    const std::string writer = readSourceFile({"src", "io", "qdr", "qdr_document_writer.cpp"});
    const std::string legacy = readSourceFile({"src", "io", "qdr", "qdr_document.cpp"});
    const std::string codec = readSourceFile({"src", "io", "qdr", "qdr_polygon_document_codec.cpp"});
    const std::string atomic = readSourceFile({"src", "io", "qdr", "qdr_atomic_write.cpp"});

    EXPECT_NE(reader.find("#include \"io/qdr/qdr_polygon_document_codec.hpp\""), std::string::npos);
    EXPECT_NE(writer.find("#include \"io/qdr/qdr_polygon_document_codec.hpp\""), std::string::npos);
    EXPECT_NE(legacy.find("#include \"io/qdr/qdr_polygon_document_codec.hpp\""), std::string::npos);
    EXPECT_NE(writer.find("#include \"io/qdr/qdr_atomic_write.hpp\""), std::string::npos);
    EXPECT_NE(legacy.find("#include \"io/qdr/qdr_atomic_write.hpp\""), std::string::npos);

    EXPECT_NE(codec.find("qdr_polygon_document_to_json"), std::string::npos);
    EXPECT_NE(codec.find("qdr_polygon_document_from_json"), std::string::npos);
    EXPECT_NE(atomic.find("write_qdr_text_atomic"), std::string::npos);
    EXPECT_NE(atomic.find("make_temp_path_for_atomic_write"), std::string::npos);

    EXPECT_EQ(reader.find("polygonDocumentFromJson("), std::string::npos);
    EXPECT_EQ(reader.find("polygonVec2FromJson("), std::string::npos);
    EXPECT_EQ(reader.find("parseSurfaceShading("), std::string::npos);
    EXPECT_EQ(writer.find("polygonDocumentToJson("), std::string::npos);
    EXPECT_EQ(writer.find("polygonVec2ToJson("), std::string::npos);
    EXPECT_EQ(writer.find("surfaceShadingName("), std::string::npos);
    EXPECT_EQ(legacy.find("\njson document_to_json(const quader_poly::Document&"), std::string::npos);
    EXPECT_EQ(legacy.find("\nquader_poly::Document document_from_json(const json& value)"), std::string::npos);

    const char* atomicOwnerTokens[] = {
            "unique_temp_suffix(",
            "make_temp_path_for_atomic_write(",
            "replace_with_temp_file(",
            "MoveFileExW(",
    };
    for (const char* token : atomicOwnerTokens) {
        EXPECT_EQ(writer.find(token), std::string::npos) << token;
        EXPECT_EQ(legacy.find(token), std::string::npos) << token;
        EXPECT_NE(atomic.find(token), std::string::npos) << token;
    }
}

TEST(QdrDocumentIoTests, QdrFormatConstantsAndProjectCodecsHaveSingleOwners) {
    EXPECT_EQ(quader_io::qdr_format_name, kQdrFormatName);
    EXPECT_EQ(quader_io::qdr_document_payload_key, kQdrDocumentPayloadKey);
    EXPECT_EQ(quader_io::qdr_legacy_session_payload_key, kQdrLegacySessionPayloadKey);
    EXPECT_EQ(quader_io::qdr_min_supported_format_version, kQdrMinSupportedFormatVersion);
    EXPECT_EQ(quader_io::qdr_format_version, kQdrLegacySessionFormatVersion);

    EXPECT_EQ(qdrEntityKindName(EntityKind::Mesh), std::string_view("mesh"));
    EXPECT_EQ(qdrEntityKindFromName("light"), EntityKind::Light);
    EXPECT_EQ(qdrLightTypeName(LightType::Spot), std::string_view("spot"));
    EXPECT_EQ(qdrLightTypeFromName("point"), LightType::Point);
    EXPECT_EQ(qdrMeshCollisionTypeName(MeshCollisionType::ConvexHull), std::string_view("convexHull"));
    EXPECT_EQ(qdrMeshCollisionTypeFromName("mesh"), MeshCollisionType::Mesh);
    EXPECT_EQ(qdrMeshPhysicsBodyTypeName(MeshPhysicsBodyType::RigidBody), std::string_view("rigidBody"));
    EXPECT_EQ(qdrMeshPhysicsBodyTypeFromName("missing"), MeshPhysicsBodyType::StaticBody);
    EXPECT_EQ(qdrTextureUsageName(TextureUsage::BaseColor), std::string_view("baseColor"));
    EXPECT_EQ(qdrTextureUsageFromName("roughness"), TextureUsage::Roughness);
    EXPECT_EQ(qdrTextureColorSpaceName(TextureColorSpace::Linear), std::string_view("linear"));
    EXPECT_EQ(qdrTextureColorSpaceFromName("srgb"), TextureColorSpace::Srgb);

    const std::string cmake = readSourceFile("CMakeLists.txt");
    const std::string legacyHeader = readSourceFile({"src", "io", "qdr", "qdr_document.hpp"});
    const std::string reader = readSourceFile({"src", "io", "qdr", "qdr_document_reader.cpp"});
    const std::string writer = readSourceFile({"src", "io", "qdr", "qdr_document_writer.cpp"});
    const std::string codecHeader = readSourceFile({"src", "io", "qdr", "qdr_project_codecs.hpp"});
    const std::string codec = readSourceFile({"src", "io", "qdr", "qdr_project_codecs.cpp"});

    EXPECT_NE(cmake.find("src/io/qdr/qdr_project_codecs.cpp"), std::string::npos);
    EXPECT_NE(legacyHeader.find("qdr_format_name = quader::kQdrFormatName"), std::string::npos);
    EXPECT_NE(legacyHeader.find("qdr_document_payload_key = quader::kQdrDocumentPayloadKey"), std::string::npos);
    EXPECT_NE(legacyHeader.find("qdr_format_version = quader::kQdrLegacySessionFormatVersion"), std::string::npos);

    EXPECT_NE(reader.find("#include \"io/qdr/qdr_project_codecs.hpp\""), std::string::npos);
    EXPECT_NE(writer.find("#include \"io/qdr/qdr_project_codecs.hpp\""), std::string::npos);
    EXPECT_NE(reader.find("qdrEntityKindFromName("), std::string::npos);
    EXPECT_NE(reader.find("qdrTextureUsageFromName("), std::string::npos);
    EXPECT_NE(writer.find("qdrEntityKindName("), std::string::npos);
    EXPECT_NE(writer.find("qdrTextureUsageName("), std::string::npos);

    EXPECT_NE(codecHeader.find("qdrEntityKindName("), std::string::npos);
    EXPECT_NE(codec.find("qdrEntityKindName("), std::string::npos);
    EXPECT_EQ(reader.find("parseEntityKind("), std::string::npos);
    EXPECT_EQ(reader.find("parseTextureUsage("), std::string::npos);
    EXPECT_EQ(reader.find("parseTextureColorSpace("), std::string::npos);
    EXPECT_EQ(writer.find("entityKindName("), std::string::npos);
    EXPECT_EQ(writer.find("textureUsageName("), std::string::npos);
    EXPECT_EQ(writer.find("textureColorSpaceName("), std::string::npos);
}

TEST(QdrDocumentIoTests, SceneSelectableMutationStaysSceneOwned) {
    const std::string sceneHeader = readSourceFile({"src", "scene", "scene_document.hpp"});
    const std::string sceneSource = readSourceFile({"src", "scene", "scene_document.cpp"});
    const std::string qdrReader = readSourceFile({"src", "io", "qdr", "qdr_document_reader.cpp"});

    EXPECT_NE(sceneHeader.find("bool setSelectable(EntityId id, bool selectable);"), std::string::npos);
    EXPECT_NE(sceneSource.find("bool SceneDocument::setSelectable"), std::string::npos);
    EXPECT_NE(qdrReader.find("document.scene().setSelectable("), std::string::npos);
    EXPECT_EQ(qdrReader.find("->selectable ="), std::string::npos);

    const std::filesystem::path sourceRoot = sourcePath("src");
    for (const std::filesystem::path& file : sourceFilesUnder(sourceRoot)) {
        if (file.filename() == "scene_document.cpp") {
            continue;
        }
        const std::string source = readFile(file);
        EXPECT_EQ(source.find("->selectable ="), std::string::npos) << file.string();
    }
}

TEST(ProjectionOwnershipGuardrailTests, ProjectionIsReadOnlyAndModelingOwnsSceneSync) {
    const std::string cmake = readSourceFile("CMakeLists.txt");
    EXPECT_NE(cmake.find("src/editor/projection/mesh_projection_service.cpp"), std::string::npos);
    EXPECT_EQ(cmake.find("src/editor/projection/modeling_scene_reconcile_service.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("src/editor/projection/picking_cache_projection.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("src/editor/projection/selection_overlay_projection.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("src/editor/modeling/services/modeling_mesh_sync_service.cpp"), std::string::npos);

    const std::string services =
            readSourceFile({"src", "editor", "modeling", "services", "modeling_session_services.cpp"});
    EXPECT_EQ(services.find("ModelingSceneReconcileService::reconcileModelingObjectSceneNodes"), std::string::npos);
    EXPECT_EQ(services.find("MeshProjectionService::projectModelingMeshesToScene"), std::string::npos);
    EXPECT_NE(services.find("PickingCacheProjection::sceneTransformedObjectsForPicking"), std::string::npos);
    EXPECT_NE(services.find("PickingCacheProjection::objectFaceGeometrySignature"), std::string::npos);

    const std::string meshSync =
            readSourceFile({"src", "editor", "modeling", "services", "modeling_mesh_sync_service.cpp"});
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
    const std::filesystem::path projectionRoot = sourcePath({"src", "editor", "projection"});
    for (const std::filesystem::path& file : sourceFilesUnder(projectionRoot)) {
        const std::string source = readFile(file);
        for (const char* forbidden : forbiddenProjectionMutations) {
            EXPECT_EQ(source.find(forbidden), std::string::npos)
                    << file.string() << " contains projection-owned mutation token " << forbidden;
        }
    }

    const std::string facade =
            readSourceFile({"src", "editor", "modeling", "services", "modeling_session_service.cpp"});
    const std::string overlayService =
            readSourceFile({"src", "editor", "modeling", "services", "modeling_session_services.cpp"});
    EXPECT_EQ(facade.find("SelectionOverlayProjection::projectSelectionOverlayFrame"), std::string::npos);
    EXPECT_NE(overlayService.find("SelectionOverlayProjection::projectSelectionOverlayFrame"), std::string::npos);
}

TEST(ProjectionOwnershipGuardrailTests, RenderDoesNotDependOnProjectionOrModelingOwners) {
    const std::filesystem::path renderRoot = sourcePath({"src", "render"});
    for (const std::filesystem::path& file : sourceFilesUnder(renderRoot)) {
        const std::string source = readFile(file);
        EXPECT_EQ(source.find("editor/projection/"), std::string::npos) << file.string();
        EXPECT_EQ(source.find("#include \"modeling/"), std::string::npos) << file.string();
    }
}
