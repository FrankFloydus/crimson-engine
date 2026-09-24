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
#include "support/command_observable_state.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
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
    return editor.standard_material_documents().new_material();
  });
  expect_scene_asset_denied(editor, [&] {
    return editor.standard_material_documents().open(default_material);
  });
  EXPECT_EQ(project.material_catalog().entries().size(), material_count);
  EXPECT_FALSE(std::filesystem::exists(temporary.path() / "Materials" / "Unavailable"));
  EXPECT_FALSE(std::filesystem::exists(sources.front()));
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

} // namespace
} // namespace quader::test
