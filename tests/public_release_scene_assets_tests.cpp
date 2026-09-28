////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "assets/asset_registry.hpp"
#include "assets/default_material_catalog.hpp"
#include "core/release_features.hpp"
#include "editor/session/project_session.hpp"
#include "editor/instances/project_clipboard_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "support/command_observable_state.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace quader::test {
namespace {

/// Isolates project clipboard and material-import fixtures from user projects.
class PublicSceneAssetsDirectory final {
public:
  PublicSceneAssetsDirectory()
      : path_(std::filesystem::temp_directory_path() /
              ("quader-public-scene-assets-" + std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directories(path_);
  }
  ~PublicSceneAssetsDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

template <typename Action>
void expect_scene_asset_denied(QuaderEditorApi &editor, Action action) {
  const auto before = capture_command_observable_state(editor);
  const auto result = action();
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_EQ(result.error_message(), release_feature_unavailable_message());
  const auto difference = diff_command_observable_state(
      before, capture_command_observable_state(editor));
  EXPECT_TRUE(difference.empty()) << difference.summary;
}

TEST(PublicReleaseSceneAssetsTests, AuthoringDenialsPreserveModelingPreview) {
  QuaderStandaloneEditor standalone;
  QuaderEditorApi &editor = standalone.editor();
  const auto mesh = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Model");
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor.modeling().update_transform_translate({0.5f, 0.0f, 0.0f}).ok());
  expect_scene_asset_denied(editor, [&] {
    return editor.assets().add_material(QuaderMaterialAsset{.name = "Custom"});
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.assets().add_texture(QuaderTextureAsset{.name = "Custom"});
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.assets().assign_material_to_entity(mesh.value().entity, 99u);
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.assets().set_material_base_color(99u, {0.2f, 0.3f, 0.4f});
  });
  expect_scene_asset_denied(editor, [&] { return editor.scene().create_point_light(); });
  expect_scene_asset_denied(editor, [&] { return editor.scene().create_directional_light(); });
  for (const auto kind : {QuaderEntityKind::Light, QuaderEntityKind::Curve,
                          QuaderEntityKind::Instance}) {
    expect_scene_asset_denied(editor, [&] {
      return editor.scene().create_entity({.name = "Excluded", .kind = kind});
    });
  }
  expect_scene_asset_denied(editor, [&] {
    return editor.selection().select_faces_with_same_material();
  });
}

TEST(PublicReleaseSceneAssetsTests, MaterialImportAndEditorRejectBeforeWritingFiles) {
  PublicSceneAssetsDirectory temporary;
  ProjectSession project;
  const auto created = project.create(temporary.path());
  ASSERT_TRUE(created.ok) << created.message;
  QuaderEditorApi editor(project);
  const auto material_count = project.material_catalog().entries().size();
  const auto default_material = *ProjectAssetId::parse(kDefaultProjectMaterialId);
  const std::array sources{temporary.path() / "unread-source.png"};
  expect_scene_asset_denied(editor, [&] {
    return editor.project_assets().import_textures_as_materials(sources);
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.project_assets().create_material_folder("Materials", "Unavailable");
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.project_assets().rename_material(default_material, "Unavailable");
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.project_assets().rename_material_folder("Missing", "Unavailable");
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.project_assets().move_material_folder("Missing", "");
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.project_assets().move_materials(std::array{default_material}, "");
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.project_assets().duplicate_materials(std::array{default_material});
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.project_assets().copy_materials(std::array{default_material}, "");
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.project_assets().delete_material_folder("Missing", 0u);
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.project_assets().delete_materials(std::array{default_material}, 0u);
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.standard_material_documents().new_material();
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.standard_material_documents().open(default_material);
  });
  EXPECT_EQ(project.material_catalog().entries().size(), material_count);
  EXPECT_FALSE(std::filesystem::exists(temporary.path() / "Materials" / "Unavailable"));
  EXPECT_FALSE(std::filesystem::exists(sources.front()));
}

TEST(PublicReleaseSceneAssetsTests, GenericProjectFoldersRemainAvailableForScenes) {
  PublicSceneAssetsDirectory temporary;
  ProjectSession project;
  ASSERT_TRUE(project.create(temporary.path()).ok);
  QuaderEditorApi editor(project);
  auto &assets = editor.project_assets();
  const auto created = assets.create_folder("", "Levels");
  ASSERT_TRUE(created.ok()) << created.error_message();
  EXPECT_TRUE(created.changed());
  const auto renamed = assets.rename_asset("Levels", "Maps");
  ASSERT_TRUE(renamed.ok()) << renamed.error_message();
  EXPECT_TRUE(renamed.changed());
  const auto scene = assets.create_scene_asset("Maps", "Main");
  ASSERT_TRUE(scene.ok()) << scene.error_message();
  EXPECT_TRUE(std::filesystem::exists(temporary.path() / "Maps/Main.qdr"));
}

TEST(PublicReleaseSceneAssetsTests, ExistingInstancesCannotBeEditedCopiedOrDuplicated) {
  PublicSceneAssetsDirectory temporary;
  ProjectSession project;
  const auto created = project.create(temporary.path());
  ASSERT_TRUE(created.ok) << created.message;
  QuaderEditorApi editor(project);
  const auto mesh = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Instance Member");
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  SceneSession &session = *project.active_scene()->session;
  SceneDocument &scene = session.scene();
  // Seed retained authored data directly: public authoring must not manufacture it.
  const EntityId definition = scene.create_entity("Existing Definition");
  ASSERT_TRUE(scene.set_parent(mesh.value().entity, definition, false));
  ASSERT_TRUE(scene.set_instance_definition(definition));
  const EntityId occurrence = scene.create_entity("Existing Instance", EntityKind::Instance);
  ASSERT_TRUE(scene.set_instance_occurrence(occurrence, {.definition = definition}));
  ASSERT_TRUE(editor.selection().select_object(occurrence).ok());

  EXPECT_FALSE(editor.modeling().can_create_instance());
  EXPECT_FALSE(editor.modeling().can_flatten_instance());
  EXPECT_FALSE(editor.modeling().can_enter_instance_edit(occurrence));
  EXPECT_FALSE(editor.modeling().can_execute_tool("copy_selection"));
  EXPECT_FALSE(editor.modeling().can_duplicate_selected_entity_hierarchy());
  expect_scene_asset_denied(editor, [&] { return editor.modeling().create_instance(); });
  expect_scene_asset_denied(editor, [&] { return editor.modeling().flatten_instance(); });
  expect_scene_asset_denied(editor, [&] { return editor.modeling().enter_instance_edit(occurrence); });
  expect_scene_asset_denied(editor, [&] { return editor.modeling().copy_selection(); });
  expect_scene_asset_denied(editor, [&] { return editor.modeling().duplicate_selected_entity_hierarchy(); });
  expect_scene_asset_denied(editor, [&] { return editor.scene().duplicate(occurrence); });
  expect_scene_asset_denied(editor, [&] { return editor.scene().duplicate(definition); });

  const auto before = capture_command_observable_state(editor);
  const std::array roots{occurrence};
  const auto duplicated = session.modeling().duplicate_entity_hierarchy(
      roots, session.document_commands(), ModelingSelectionMode::Object);
  EXPECT_FALSE(duplicated.ok);
  EXPECT_FALSE(duplicated.changed);
  EXPECT_EQ(duplicated.message, release_feature_unavailable_message());
  EXPECT_FALSE(project.clipboard().can_paste_instance_selection(project));
  const auto pasted = project.clipboard().paste_instance_selection(project);
  EXPECT_FALSE(pasted.ok);
  EXPECT_FALSE(pasted.changed);
  EXPECT_EQ(pasted.message, release_feature_unavailable_message());
  EXPECT_FALSE(project.clipboard().has_instance_payload());
  const auto difference = diff_command_observable_state(
      before, capture_command_observable_state(editor));
  EXPECT_TRUE(difference.empty()) << difference.summary;
  ASSERT_NE(scene.instance_occurrence(occurrence), nullptr);
  EXPECT_EQ(scene.instance_occurrence(occurrence)->definition, definition);
  EXPECT_EQ(scene.parent(mesh.value().entity), definition);
}

TEST(PublicReleaseSceneAssetsTests, CaseRenamedFullSceneRejectsBeforeRecoveryWrites) {
#if !defined(_WIN32)
  GTEST_SKIP() << "This compatibility probe uses Windows case-insensitive file access.";
#else
  PublicSceneAssetsDirectory temporary;
  const auto root = temporary.path() / "Project";
  std::filesystem::create_directory(root);
  ProjectSession project;
  ASSERT_TRUE(project.create(root).ok);
  const auto source = ProjectRelativePath::parse_portable("Original.qdr").value();
  const auto destination = ProjectRelativePath::parse_portable("original.qdr").value();
  ASSERT_TRUE(project.save_as(project.active_scene_id(), source).ok);
  const auto identity = project.active_scene()->scene_id;
  const auto manifest_path = project.manifest_path();
  auto candidate = *project.manifest();
  ASSERT_EQ(candidate.minimum_product_release(), kPublicRelease01);
  ASSERT_TRUE(candidate.unregister_scene(identity).ok());
  ASSERT_TRUE(candidate.register_scene({.id = identity, .path = destination}).ok());
  ASSERT_TRUE(project.prepare_asset_manifest(candidate).ok);
  ASSERT_TRUE(project.close().ok);
  const auto read_text = [](const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
  };
  const auto baseline = read_text(manifest_path);
  const auto read = QdrDocumentReader::read_project_document(root / source.native_path());
  ASSERT_TRUE(read.ok) << read.message;
  auto snapshot = qdr_project_write_snapshot_from_document(read.document);
  ASSERT_TRUE(snapshot.ok());
  snapshot.value().minimum_product_release = kInternalFullRelease;
  ASSERT_TRUE(QdrDocumentWriter::write_project_document_atomic(
      root / source.native_path(), snapshot.value(), read.scene_uuid).ok);
  const auto staging = root / "case-rename-stage";
  std::filesystem::rename(root / source.native_path(), staging);
  std::filesystem::rename(staging, root / destination.native_path());
  std::filesystem::create_directories(root / ".quader");
  const auto journal_path = root / ".quader" /
      ("asset-organization-" + std::string(ProjectAssetId::generate().str()) + ".json");
  const nlohmann::json journal{
      {"version", 1}, {"state", "committed"},
      {"project", std::string(candidate.id().str())},
      {"manifest", "Project.qproj"}, {"baseline", baseline},
      {"candidate", ProjectManifestStore::serialize(candidate)},
      {"actions", nlohmann::json::array()},
  };
  std::ofstream(journal_path, std::ios::binary) << journal.dump(2);
  const auto journal_before = read_text(journal_path);
  const auto scene_before = read_text(root / destination.native_path());

  const auto opened = project.open(manifest_path);
  EXPECT_FALSE(opened.ok);
  EXPECT_FALSE(project.has_project());
  EXPECT_EQ(read_text(manifest_path), baseline);
  EXPECT_EQ(read_text(journal_path), journal_before);
  EXPECT_EQ(read_text(root / destination.native_path()), scene_before);
#endif
}

} // namespace
} // namespace quader::test
