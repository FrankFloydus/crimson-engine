////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "core/public_command_targets.hpp"
#include "editor/input/quader_keybinding_service.hpp"
#include "editor/input/viewport_keybinding_resolver.hpp"
#include "editor/input/viewport_input_binding_adapter.hpp"
#include "math/glm_bridge.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "support/behavior_scenario.hpp"
#include "support/command_observable_state.hpp"
#include "ui/viewport_shortcut_controller.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <gtest/gtest.h>
#ifndef NDEBUG
#include <nlohmann/json.hpp>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::optional<quader::QuaderEntitySnapshot>
entity_named(const quader::QuaderEditorApi &editor, std::string_view name) {
  for (const quader::QuaderEntitySnapshot &snapshot :
       editor.query().entities()) {
    if (snapshot.name == name) {
      return snapshot;
    }
  }
  return std::nullopt;
}

std::optional<quader::QuaderEntitySnapshot>
entity_by_id(const quader::QuaderEditorApi &editor, quader::EntityId id) {
  return editor.query().entity(id);
}

std::optional<quader::QuaderEntitySnapshot>
scene_root(const quader::QuaderEditorApi &editor) {
  for (const quader::QuaderEntitySnapshot &snapshot :
       editor.query().entities()) {
    if (snapshot.is_scene_root) {
      return snapshot;
    }
  }
  return std::nullopt;
}

std::size_t user_entity_count(const quader::QuaderEditorApi &editor) {
  std::size_t count = 0;
  for (const quader::QuaderEntitySnapshot &snapshot :
       editor.query().entities()) {
    if (!snapshot.is_scene_root) {
      ++count;
    }
  }
  return count;
}

quader::QuaderSceneSettings
scene_root_settings(const quader::QuaderEditorApi &editor) {
  const std::optional<quader::QuaderEntitySnapshot> root = scene_root(editor);
  EXPECT_TRUE(root.has_value());
  EXPECT_TRUE(root.has_value() && root->has_scene_settings);
  return root.has_value() ? root->scene_settings
                          : quader::QuaderSceneSettings{};
}

void expect_scene_settings_eq(const quader::QuaderSceneSettings &actual,
                              const quader::QuaderSceneSettings &expected) {
  EXPECT_EQ(actual.environment.environment_id,
            expected.environment.environment_id);
  EXPECT_EQ(actual.environment.enabled, expected.environment.enabled);
  EXPECT_EQ(actual.environment.show_background,
            expected.environment.show_background);
  EXPECT_FLOAT_EQ(actual.environment.clear_color.x,
                  expected.environment.clear_color.x);
  EXPECT_FLOAT_EQ(actual.environment.clear_color.y,
                  expected.environment.clear_color.y);
  EXPECT_FLOAT_EQ(actual.environment.clear_color.z,
                  expected.environment.clear_color.z);
  EXPECT_FLOAT_EQ(actual.environment.intensity, expected.environment.intensity);
  EXPECT_FLOAT_EQ(actual.environment.background_intensity,
                  expected.environment.background_intensity);
  EXPECT_FLOAT_EQ(actual.environment.rotation_y,
                  expected.environment.rotation_y);
  EXPECT_FLOAT_EQ(actual.camera.aperture, expected.camera.aperture);
  EXPECT_FLOAT_EQ(actual.camera.shutter_speed, expected.camera.shutter_speed);
  EXPECT_FLOAT_EQ(actual.camera.sensitivity, expected.camera.sensitivity);
  EXPECT_FLOAT_EQ(actual.camera.near_plane, expected.camera.near_plane);
  EXPECT_FLOAT_EQ(actual.camera.far_plane, expected.camera.far_plane);
  EXPECT_EQ(actual.post_process.enabled, expected.post_process.enabled);
  EXPECT_EQ(actual.post_process.anti_aliasing,
            expected.post_process.anti_aliasing);
  EXPECT_EQ(actual.post_process.ambient_occlusion,
            expected.post_process.ambient_occlusion);
  EXPECT_FLOAT_EQ(actual.post_process.ambient_occlusion_intensity,
                  expected.post_process.ambient_occlusion_intensity);
  EXPECT_FLOAT_EQ(actual.post_process.ambient_occlusion_radius,
                  expected.post_process.ambient_occlusion_radius);
  EXPECT_EQ(actual.post_process.bloom, expected.post_process.bloom);
  EXPECT_FLOAT_EQ(actual.post_process.bloom_strength,
                  expected.post_process.bloom_strength);
  EXPECT_EQ(actual.post_process.dithering, expected.post_process.dithering);
  EXPECT_EQ(actual.post_process.tone_mapping,
            expected.post_process.tone_mapping);
}

void expect_light_eq(const quader::QuaderLightComponent &actual,
                     const quader::QuaderLightComponent &expected) {
  EXPECT_EQ(actual.type, expected.type);
  EXPECT_FLOAT_EQ(actual.color.x, expected.color.x);
  EXPECT_FLOAT_EQ(actual.color.y, expected.color.y);
  EXPECT_FLOAT_EQ(actual.color.z, expected.color.z);
  EXPECT_FLOAT_EQ(actual.intensity, expected.intensity);
  EXPECT_FLOAT_EQ(actual.range, expected.range);
  EXPECT_EQ(actual.casts_shadows, expected.casts_shadows);
}

std::vector<quader::EntityId>
selected_entities(const quader::QuaderEditorApi &editor) {
  std::vector<quader::EntityId> selected;
  for (const quader::QuaderEntitySnapshot &snapshot :
       editor.query().entities()) {
    if (snapshot.selected) {
      selected.push_back(snapshot.id);
    }
  }
  std::sort(selected.begin(), selected.end());
  return selected;
}

std::size_t child_count(const quader::QuaderEditorApi &editor,
                        quader::EntityId parent) {
  const std::vector<quader::QuaderEntitySnapshot> entities =
      editor.query().entities();
  return static_cast<std::size_t>(std::count_if(
      entities.begin(), entities.end(),
      [parent](const quader::QuaderEntitySnapshot &candidate) {
        return candidate.parent == parent;
      }));
}

std::unordered_set<std::string>
available_command_ids(const quader::QuaderEditorApi &editor) {
  std::unordered_set<std::string> result;
  for (const quader::QuaderCommandDescriptor &command :
       editor.commands().available()) {
    result.insert(command.id);
  }
  return result;
}

std::optional<quader::QuaderCommandDescriptor>
command_descriptor(const quader::QuaderEditorApi &editor,
                   quader::QuaderCommandId command_id) {
  return editor.commands().descriptor(command_id);
}

std::unordered_set<std::string> typed_command_ids() {
  std::unordered_set<std::string> ids;
  for (const quader::QuaderCommandId command_id :
       quader::quader_command::typed_command_ids()) {
    ids.emplace(command_id.string());
  }
  return ids;
}

std::string
command_id_for_context_action(const quader::QuaderEditorApi &editor,
                              const quader::QuaderActionItem &action) {
  if (action.kind == quader::QuaderActionKind::Tool) {
    return editor.commands().command_id_for_tool_activation(action.tool_id);
  }
  return editor.commands().command_id_for_modeling_action(action.action_id);
}

const quader::QuaderActionPanel *
find_action_panel(const quader::QuaderActionSet &action_set,
                  std::string_view panel_id) {
  const auto found = std::find_if(
      action_set.panels.begin(), action_set.panels.end(),
      [&](const auto &panel) { return panel.panel_id == panel_id; });
  return found == action_set.panels.end() ? nullptr : &(*found);
}

const quader::QuaderActionItem *
find_action_item(const quader::QuaderActionPanel &panel,
                 std::string_view action_id) {
  const auto found = std::find_if(
      panel.actions.begin(), panel.actions.end(),
      [&](const auto &action) { return action.action_id == action_id; });
  return found == panel.actions.end() ? nullptr : &(*found);
}

/// Owns an isolated editor session for command routing tests that need fresh
/// history, selection, and scene state per assertion path.
struct CommandTestHarness {
    quader::QuaderStandaloneEditor standalone;
    quader::QuaderEditorApi& editor;

    CommandTestHarness()
        : standalone(quader::create_standalone_editor()),
          editor(standalone.editor()) {}
};

static_assert(!std::is_copy_constructible_v<quader::QuaderSceneBatch>);
static_assert(!std::is_copy_assignable_v<quader::QuaderSceneBatch>);
static_assert(std::is_move_constructible_v<quader::QuaderSceneBatch>);
static_assert(std::is_move_assignable_v<quader::QuaderSceneBatch>);

quader::QuaderModelingObjectRef
create_selected_box(quader::QuaderEditorApi &editor,
                    std::string name = "Command Box") {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      editor.modeling().create_box(
          quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                               .max = {1.0f, 1.0f, 1.0f}},
          std::move(name));
  EXPECT_TRUE(created.ok()) << created.error_message();
  EXPECT_TRUE(editor.selection().select_object(created.value()).ok());
  return created.value();
}

quader::QuaderModelingObjectRef
create_polygon_mesh(quader::QuaderEditorApi &editor, std::size_t side_count,
                    std::string name) {
  quader_poly::Document document;
  std::vector<quader_poly::ElementId> vertices;
  vertices.reserve(side_count);
  for (std::size_t index = 0; index < side_count; ++index) {
    const float angle =
        6.2831853071795864769F * static_cast<float>(index) /
        static_cast<float>(side_count);
    vertices.push_back(quader_poly::add_vertex(
        document, {std::cos(angle), std::sin(angle), 0.0F}));
  }
  EXPECT_NE(quader_poly::add_face(document, vertices),
            quader_poly::kInvalidElementId);
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      editor.modeling().create_mesh(
          quader::QuaderPolygonDocumentInternalAccess::from_backend(
              std::move(document)),
          quader::QuaderCreateMeshArgs{.name = std::move(name)});
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

TEST(EditorApiTests, NativeModelingMeshHandleRoutesComponentPreviewWorkflow) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;

  const quader::QuaderApiResult<quader::QuaderMeshHandle> created =
      editor.modeling().create_box_mesh(
          quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                               .max = {1.0f, 1.0f, 1.0f}},
          "Native Handle Box");
  ASSERT_TRUE(created.ok()) << created.error_message();

  quader::QuaderMeshHandle mesh = created.value();
  ASSERT_TRUE(mesh.valid());
  const std::optional<quader::QuaderMeshAsset> before = mesh.compiled_mesh();
  ASSERT_TRUE(before.has_value());

  const quader::QuaderFaceSelection face = mesh.faces().only({1});
  ASSERT_EQ(face.count(), 1u);
  const quader::QuaderApiVoidResult selected = face.replace();
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(editor.selection().summary().selected_face_count, 1u);

  quader::QuaderPreviewHandle preview =
      face.slice({.x_slices = 1, .y_slices = 1});
  ASSERT_TRUE(preview.begin_result().ok())
      << preview.begin_result().error_message();
  const quader::QuaderApiVoidResult committed =
      preview.commit("Slice Native Handle Face");
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.changed());

  const std::optional<quader::QuaderMeshAsset> after = mesh.compiled_mesh();
  ASSERT_TRUE(after.has_value());
  EXPECT_GT(after->vertices.size(), before->vertices.size());
}

TEST(EditorApiTests, NativeModelingMeshHandleCannotAliasAnUndoBranch) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;

  const quader::QuaderApiResult<quader::QuaderMeshHandle> first_created =
      editor.modeling().create_box_mesh(
          quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                               .max = {1.0f, 1.0f, 1.0f}},
          "Retired Handle Box");
  ASSERT_TRUE(first_created.ok()) << first_created.error_message();
  quader::QuaderMeshHandle retired = first_created.value();
  ASSERT_TRUE(retired.valid());
  const quader::QuaderModelingObjectId retired_id = retired.id();

  const quader::QuaderApiVoidResult undone = editor.history().undo();
  ASSERT_TRUE(undone.ok()) << undone.error_message();
  EXPECT_FALSE(retired.valid());

  const quader::QuaderApiResult<quader::QuaderMeshHandle> branch_created =
      editor.modeling().create_box_mesh(
          quader::QuaderBounds{.min = {2.0f, 2.0f, 2.0f},
                               .max = {3.0f, 3.0f, 3.0f}},
          "Branch Handle Box");
  ASSERT_TRUE(branch_created.ok()) << branch_created.error_message();
  const quader::QuaderMeshHandle branch = branch_created.value();
  EXPECT_TRUE(branch.valid());
  EXPECT_NE(branch.id(), retired_id);
  EXPECT_FALSE(retired.valid());
}

constexpr float kTestPositionEpsilon = 0.0001f;

void expect_vec3_near(quader::QVec3 actual, quader::QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, kTestPositionEpsilon);
  EXPECT_NEAR(actual.y, expected.y, kTestPositionEpsilon);
  EXPECT_NEAR(actual.z, expected.z, kTestPositionEpsilon);
}

std::optional<quader::QVec3>
entity_world_origin_translation_only(const quader::QuaderEditorApi &editor,
                                     quader::EntityId entity) {
  quader::QVec3 origin{};
  quader::EntityId current = entity;
  int guard = 0;
  while (current != quader::kInvalidEntityId && guard < 128) {
    const std::optional<quader::QuaderEntitySnapshot> snapshot =
        editor.query().entity(current);
    if (!snapshot.has_value()) {
      return std::nullopt;
    }
    origin += snapshot->transform.position;
    current = snapshot->parent;
    ++guard;
  }
  return current == quader::kInvalidEntityId ? std::optional<quader::QVec3>{origin}
                                             : std::nullopt;
}

/**
 * Stores translation-only world bounds for API-level pivot rebase assertions.
 */
struct TestWorldBounds {
    quader::QVec3 min;
    quader::QVec3 max;
};

std::optional<TestWorldBounds>
compiled_object_world_bounds_translation_only(
    const quader::QuaderEditorApi &editor,
    quader::QuaderModelingObjectRef object) {
  const std::optional<quader::QuaderMeshAsset> mesh =
      editor.query().compiled_object_mesh(object);
  const std::optional<quader::QVec3> origin =
      entity_world_origin_translation_only(editor, object.entity);
  if (!mesh.has_value() || !origin.has_value() || mesh->vertices.empty()) {
    return std::nullopt;
  }

  TestWorldBounds bounds{
      .min = *origin + mesh->vertices.front().position,
      .max = *origin + mesh->vertices.front().position,
  };
  for (const quader::QuaderMeshVertex &vertex : mesh->vertices) {
    const quader::QVec3 world = *origin + vertex.position;
    bounds.min.x = std::min(bounds.min.x, world.x);
    bounds.min.y = std::min(bounds.min.y, world.y);
    bounds.min.z = std::min(bounds.min.z, world.z);
    bounds.max.x = std::max(bounds.max.x, world.x);
    bounds.max.y = std::max(bounds.max.y, world.y);
    bounds.max.z = std::max(bounds.max.z, world.z);
  }
  return bounds;
}

void expect_bounds_near(const TestWorldBounds &actual,
                        const TestWorldBounds &expected) {
  expect_vec3_near(actual.min, expected.min);
  expect_vec3_near(actual.max, expected.max);
}

const quader::QuaderToolSettingControl *
find_tool_setting_control(const quader::QuaderToolSettingsTarget &target,
                          std::string_view id) {
  const auto found =
      std::find_if(target.controls.begin(), target.controls.end(),
                   [&](const quader::QuaderToolSettingControl &control) {
                     return control.id == id;
                   });
  return found == target.controls.end() ? nullptr : &(*found);
}

std::size_t count_tool_setting_controls_of_kind(
    const quader::QuaderToolSettingsTarget &target,
    quader::QuaderToolSettingControlKind kind) {
  return static_cast<std::size_t>(
      std::count_if(target.controls.begin(), target.controls.end(),
                    [kind](const quader::QuaderToolSettingControl &control) {
                      return control.kind == kind;
                    }));
}

/// Captures material and texture ids used by undo/redo preservation regressions.
struct TexturedMaterialRefs {
  quader::AssetId material = quader::kInvalidAssetId;
  quader::AssetId base_color_texture = quader::kInvalidAssetId;
  quader::AssetId roughness_texture = quader::kInvalidAssetId;
};

TexturedMaterialRefs create_textured_material(quader::QuaderEditorApi &editor,
                                              std::string name) {
  quader::QuaderTextureAsset base_color;
  base_color.name = name + " Base";
  base_color.usage = quader::QuaderTextureUsage::BaseColor;
  base_color.width = 8;
  base_color.height = 8;
  base_color.generated = true;
  const quader::QuaderApiResult<quader::QuaderAssetRef> base_color_texture =
      editor.assets().add_texture(std::move(base_color));
  if (!base_color_texture.ok()) {
    ADD_FAILURE() << base_color_texture.error_message();
    return {};
  }

  quader::QuaderTextureAsset roughness;
  roughness.name = name + " Roughness";
  roughness.usage = quader::QuaderTextureUsage::Roughness;
  roughness.color_space = quader::QuaderTextureColorSpace::Linear;
  roughness.width = 4;
  roughness.height = 4;
  roughness.generated = true;
  const quader::QuaderApiResult<quader::QuaderAssetRef> roughness_texture =
      editor.assets().add_texture(std::move(roughness));
  if (!roughness_texture.ok()) {
    ADD_FAILURE() << roughness_texture.error_message();
    return {};
  }

  quader::QuaderMaterialAsset material;
  material.name = std::move(name);
  material.texture_slots.emplace("baseColorTexture",
                                 base_color_texture.value().id);
  material.texture_slots.emplace("roughnessTexture",
                                 roughness_texture.value().id);
  const quader::QuaderApiResult<quader::QuaderAssetRef> material_ref =
      editor.assets().add_material(std::move(material));
  if (!material_ref.ok()) {
    ADD_FAILURE() << material_ref.error_message();
    return {};
  }

  return {
      .material = material_ref.value().id,
      .base_color_texture = base_color_texture.value().id,
      .roughness_texture = roughness_texture.value().id,
  };
}

void expect_entity_textured(const quader::QuaderEditorApi &editor,
                            quader::EntityId entity,
                            const TexturedMaterialRefs &refs) {
  const std::optional<quader::QuaderEntitySnapshot> snapshot =
      editor.query().entity(entity);
  ASSERT_TRUE(snapshot.has_value());
  ASSERT_TRUE(snapshot->has_material);
  EXPECT_EQ(snapshot->material, refs.material);

  const std::optional<quader::QuaderMaterialAsset> material =
      editor.query().material(refs.material);
  ASSERT_TRUE(material.has_value());
  ASSERT_TRUE(material->texture_slots.contains("baseColorTexture"));
  ASSERT_TRUE(material->texture_slots.contains("roughnessTexture"));
  EXPECT_EQ(material->texture_slots.at("baseColorTexture"),
            refs.base_color_texture);
  EXPECT_EQ(material->texture_slots.at("roughnessTexture"),
            refs.roughness_texture);

  const std::optional<quader::QuaderTextureAsset> base_color =
      editor.query().texture(refs.base_color_texture);
  ASSERT_TRUE(base_color.has_value());
  EXPECT_EQ(base_color->usage, quader::QuaderTextureUsage::BaseColor);
  EXPECT_EQ(base_color->color_space, quader::QuaderTextureColorSpace::Srgb);

  const std::optional<quader::QuaderTextureAsset> roughness =
      editor.query().texture(refs.roughness_texture);
  ASSERT_TRUE(roughness.has_value());
  EXPECT_EQ(roughness->usage, quader::QuaderTextureUsage::Roughness);
  EXPECT_EQ(roughness->color_space, quader::QuaderTextureColorSpace::Linear);
}

quader::QuaderViewportCamera api_orthographic_camera() {
  return {
      .orthographic = true,
      .origin = {0.0f, 0.0f, 10.0f},
      .x_axis = {1.0f, 0.0f, 0.0f},
      .y_axis = {0.0f, 1.0f, 0.0f},
      .z_axis = {0.0f, 0.0f, 1.0f},
      .viewport_size = {100.0f, 100.0f},
      .orthographic_size = 100.0,
      .fov_degrees = 60.0,
  };
}

void setup_selection_for_mode(quader::QuaderEditorApi &editor,
                              quader::QuaderSelectionMode mode) {
  const quader::QuaderModelingObjectRef object = create_selected_box(editor);
  ASSERT_TRUE(editor.selection().set_mode(mode).ok());
  if (mode == quader::QuaderSelectionMode::Object) {
    return;
  }
  if (mode == quader::QuaderSelectionMode::Face) {
    ASSERT_TRUE(editor.selection().select_face(object, 1).ok());
  } else if (mode == quader::QuaderSelectionMode::Edge) {
    ASSERT_TRUE(editor.selection().select_edge(object, 1, 2).ok());
  } else if (mode == quader::QuaderSelectionMode::Vertex) {
    ASSERT_TRUE(editor.selection().select_vertex(object, 1).ok());
  }
  ASSERT_TRUE(editor.selection().select_all().ok());
}

bool known_non_success_command_message(std::string_view message) {
  return message != "Unknown command id." &&
         message != "Command tool is not registered.";
}

std::filesystem::path keybinding_test_path(std::string_view name) {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("quader-keybindings-" + std::string(name) + ".json");
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  return path;
}

std::filesystem::path project_test_path(std::string_view name) {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
                               ("quader-project-" + std::string(name) + ".qdr");
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  return path;
}

std::string read_text_file(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

quader::QuaderApiVoidResult
save_scene_as_native(quader::QuaderEditorApi &editor,
                     const std::filesystem::path &path) {
  return editor.scene_documents().save_as_native_path(
      *editor.scene_documents().active_scene_id(), path);
}

quader::QuaderApiVoidResult
open_scene_native(quader::QuaderEditorApi &editor,
                  const std::filesystem::path &path) {
  return quader::quader_api_status_from_result(
      editor.scene_documents().open_native_path(path));
}

quader::QuaderMeshAsset triangle_mesh(std::string name) {
  quader::QuaderMeshAsset mesh;
  mesh.name = std::move(name);
  mesh.vertices = {
      {.position = {0.0f, 0.0f, 0.0f},
       .normal = {0.0f, 1.0f, 0.0f},
       .uv = {0.0f, 0.0f}},
      {.position = {1.0f, 0.0f, 0.0f},
       .normal = {0.0f, 1.0f, 0.0f},
       .uv = {1.0f, 0.0f}},
      {.position = {0.0f, 0.0f, 1.0f},
       .normal = {0.0f, 1.0f, 0.0f},
       .uv = {0.0f, 1.0f}},
  };
  mesh.indices = {0, 1, 2};
  return mesh;
}

bool same_viewport_shortcut(const quader::ViewportShortcut &left,
                            const quader::ViewportShortcut &right) {
  return left.kind == right.kind && left.context == right.context &&
         left.tool == right.tool &&
         left.selection_mode == right.selection_mode &&
         left.mirror_axis == right.mirror_axis &&
         left.grid_preset == right.grid_preset;
}

bool has_keybinding(const std::vector<quader::QuaderKeybindingRule> &rules,
                    std::string_view target_id, std::string_view shortcut) {
  return std::any_of(rules.begin(), rules.end(),
                     [&](const quader::QuaderKeybindingRule &rule) {
                       return rule.target_id == target_id &&
                              rule.shortcut == shortcut;
                     });
}

bool has_keybinding_when(const std::vector<quader::QuaderKeybindingRule> &rules,
                         std::string_view target_id, std::string_view shortcut,
                         std::string_view when) {
  return std::any_of(rules.begin(), rules.end(),
                     [&](const quader::QuaderKeybindingRule &rule) {
                       return rule.target_id == target_id &&
                              rule.shortcut == shortcut && rule.when == when;
                     });
}

const quader::QuaderKeybindingCatalogEntry *
find_keybinding_catalog_entry(
    const std::vector<quader::QuaderKeybindingCatalogEntry> &entries,
    std::string_view target_id) {
  const auto found =
      std::find_if(entries.begin(), entries.end(), [&](const auto &entry) {
        return std::find(entry.target_ids.begin(), entry.target_ids.end(),
                         target_id) != entry.target_ids.end();
      });
  return found == entries.end() ? nullptr : &(*found);
}

bool has_keybinding_catalog_entry(
    const std::vector<quader::QuaderKeybindingCatalogEntry> &entries,
    std::string_view target_id, std::string_view shortcut) {
  return std::any_of(entries.begin(), entries.end(), [&](const auto &entry) {
    return std::find(entry.target_ids.begin(), entry.target_ids.end(),
                     target_id) != entry.target_ids.end() &&
           entry.shortcut == shortcut;
  });
}

quader::QuaderInputChord single_input_chord(std::string_view shortcut) {
  const std::optional<std::vector<quader::QuaderInputChord>> parsed =
      quader::quader_input_parse_shortcut(shortcut);
  EXPECT_TRUE(parsed.has_value()) << shortcut;
  if (!parsed || parsed->empty()) {
    return {};
  }
  EXPECT_EQ(parsed->size(), 1u) << shortcut;
  return parsed->front();
}

void expect_same_api_status(const quader::QuaderApiVoidResult &actual,
                            const quader::QuaderApiVoidResult &expected) {
  EXPECT_EQ(actual.ok(), expected.ok());
  EXPECT_EQ(actual.changed(), expected.changed());
  EXPECT_EQ(actual.error_message(), expected.error_message());
  EXPECT_EQ(actual.mutation_kind, expected.mutation_kind);
  EXPECT_EQ(actual.revisions.project_revision,
            expected.revisions.project_revision);
  EXPECT_EQ(actual.revisions.scene_revision, expected.revisions.scene_revision);
  EXPECT_EQ(actual.revisions.scene_layer_revision,
            expected.revisions.scene_layer_revision);
  EXPECT_EQ(actual.revisions.asset_revision, expected.revisions.asset_revision);
  EXPECT_EQ(actual.revisions.modeling_content_revision,
            expected.revisions.modeling_content_revision);
  EXPECT_EQ(actual.revisions.modeling_selection_revision,
            expected.revisions.modeling_selection_revision);
}

} // namespace

TEST(EditorApiTests, AggregatesDocumentApisAndCommandRouting) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    ASSERT_TRUE(
        editor.commands()
            .set_keybinding_storage_path(keybinding_test_path("aggregate"))
            .ok());

    EXPECT_FALSE(editor.project().dirty());
    EXPECT_EQ(editor.project().metadata().name, "Untitled Project");

    const std::vector<quader::QuaderCommandDescriptor> commands = editor.commands().available();
    EXPECT_NE(std::find_if(commands.begin(), commands.end(),
                           [](const quader::QuaderCommandDescriptor &command) {
                             return command.id ==
                                    quader::quader_command::kEditUndo.string();
                           }),
              commands.end());
    const std::optional<quader::QuaderCommandDescriptor> duplicate =
        command_descriptor(editor, quader::quader_command::kDuplicateSelection);
    ASSERT_TRUE(duplicate.has_value());
    EXPECT_FALSE(duplicate->enabled);
    const std::optional<quader::QuaderCommandDescriptor> apply_material =
        command_descriptor(editor, quader::quader_command::kApplyMaterial);
    ASSERT_TRUE(apply_material.has_value());
    EXPECT_FALSE(apply_material->enabled);
    EXPECT_TRUE(command_descriptor(editor, quader::quader_command::kProjectNew)
                    .has_value());
    EXPECT_TRUE(command_descriptor(editor, quader::quader_command::kProjectOpen)
                    .has_value());
    EXPECT_TRUE(command_descriptor(editor, quader::quader_command::kProjectClose)
                    .has_value());
    EXPECT_FALSE(command_descriptor(editor, quader::QuaderCommandId{"project.save"})
                     .has_value());
    EXPECT_FALSE(
        command_descriptor(editor, quader::QuaderCommandId{"project.save_as"})
            .has_value());
    EXPECT_TRUE(command_descriptor(
                    editor, quader::quader_command::kSceneDocumentNew)
                    .has_value());
    EXPECT_TRUE(command_descriptor(
                    editor, quader::quader_command::kSceneDocumentOpen)
                    .has_value());
    EXPECT_TRUE(command_descriptor(
                    editor, quader::quader_command::kSceneDocumentSave)
                    .has_value());
    EXPECT_TRUE(command_descriptor(
                    editor, quader::quader_command::kSceneDocumentSaveAs)
                    .has_value());
    EXPECT_TRUE(command_descriptor(
                    editor, quader::quader_command::kSceneDocumentSaveAll)
                    .has_value());
    EXPECT_TRUE(command_descriptor(
                    editor, quader::quader_command::kSceneDocumentClose)
                    .has_value());
    EXPECT_TRUE(command_descriptor(
                    editor, quader::quader_command::kSceneDocumentNext)
                    .has_value());
    EXPECT_TRUE(command_descriptor(
                    editor, quader::quader_command::kSceneDocumentPrevious)
                    .has_value());
    const std::optional<quader::QuaderCommandDescriptor> export_obj =
        command_descriptor(editor,
                           quader::quader_command::kProjectExportSelectionObj);
    ASSERT_TRUE(export_obj.has_value());
    EXPECT_FALSE(export_obj->enabled);
    EXPECT_TRUE(command_descriptor(
                    editor, quader::quader_command::kSceneCreatePointLight)
                    .has_value());
    EXPECT_TRUE(
        command_descriptor(editor,
                           quader::quader_command::kSceneCreateDirectionalLight)
            .has_value());
    EXPECT_EQ(editor.commands().shortcut_for(quader::quader_command::kEditUndo),
              "Ctrl+Z");
    EXPECT_EQ(
        editor.commands().shortcut_for(quader::quader_command::kProjectNew),
        "");
    EXPECT_EQ(
        editor.commands().shortcut_for(quader::quader_command::kProjectOpen),
        "");
    EXPECT_EQ(
        editor.commands().shortcut_for(quader::QuaderCommandId{"project.save"}),
        "");
    EXPECT_EQ(
        editor.commands().shortcut_for(quader::QuaderCommandId{"project.save_as"}),
        "");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneDocumentNew),
              "Ctrl+N");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneDocumentOpen),
              "Ctrl+O");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneDocumentSave),
              "Ctrl+S");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneDocumentSaveAs),
              "Ctrl+Alt+S");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneDocumentSaveAll),
              "Ctrl+Shift+S");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneDocumentClose),
              "Ctrl+W");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneDocumentNext),
              "Ctrl+Tab");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneDocumentPrevious),
              "Ctrl+Shift+Tab");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kProjectExportSelectionObj),
              "");
#ifndef NDEBUG
    EXPECT_TRUE(command_descriptor(editor,
                                   quader::quader_command::kDevCopySelectionJson)
                    .has_value());
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kDevCopySelectionJson),
              "Ctrl+Shift+C");
#endif
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneCreatePointLight),
              "");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneCreateDirectionalLight),
              "");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kViewportFocusAll),
              "Home");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kViewportFrameSelection),
              "");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kViewportFocusOrigin),
              "Shift+Home");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kViewportSpawnWindow),
              "");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kViewportToggleLocalVisibility),
              "Backslash");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kViewportToggleTransformSpace),
              "T");
    const std::optional<quader::QuaderCommandDescriptor> spawn_window =
        command_descriptor(editor,
                           quader::quader_command::kViewportSpawnWindow);
    ASSERT_TRUE(spawn_window.has_value());
    EXPECT_FALSE(spawn_window->enabled);
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kViewportViewPerspective),
              "F4");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kToolsActivatePivot),
              "P");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kModelingApplyActivePreview),
              "Space");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kModelingCancelActivePreview),
              "Escape");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kToolsActivatePoly),
              "");
    EXPECT_EQ(editor.commands().shortcut_for(quader::quader_command::kEditUndo),
              "Ctrl+Z");
    EXPECT_EQ(
        editor.commands().command_id_for_modeling_action("delete_selection"),
        std::string(quader::quader_command::kDeleteSelection.string()));
    EXPECT_EQ(editor.commands().shortcut_for(
                  editor.commands().command_id_for_modeling_action(
                      "delete_selection")),
              "Delete");
    EXPECT_EQ(
        editor.commands().command_id_for_modeling_action("invert_mesh"),
        std::string(quader::quader_command::kModelingInvertMesh.string()));
    EXPECT_EQ(editor.commands().command_id_for_modeling_action("bridge_edges"),
              quader::quader_modeling_command_target_id("bridge_edges"));
    EXPECT_EQ(editor.commands().command_id_for_tool_activation("knife"),
              quader::quader_tool_activation_command_target_id("knife"));
    EXPECT_EQ(editor.commands().keybinding_target_id_for_selection_mode(
                  quader::QuaderSelectionMode::Vertex),
              quader::quader_viewport_selection_mode_target_id("vertex"));
    EXPECT_EQ(editor.commands().keybinding_target_id_for_selection_mode(
                  quader::QuaderSelectionMode::Edge),
              quader::quader_viewport_selection_mode_target_id("edge"));
    EXPECT_EQ(editor.commands().keybinding_target_id_for_selection_mode(
                  quader::QuaderSelectionMode::Face),
              quader::quader_viewport_selection_mode_target_id("face"));
    EXPECT_EQ(editor.commands().keybinding_target_id_for_selection_mode(
                  quader::QuaderSelectionMode::Object),
              quader::quader_viewport_selection_mode_target_id("object"));
    EXPECT_EQ(editor.commands().keybinding_target_id_for_selection_mode(
                  quader::QuaderSelectionMode::Group),
              quader::quader_viewport_selection_mode_target_id("group"));
    EXPECT_EQ(editor.commands().shortcut_for(
                  editor.commands().keybinding_target_id_for_selection_mode(
                      quader::QuaderSelectionMode::Vertex)),
              "1");
    EXPECT_EQ(editor.commands().shortcut_for(
                  editor.commands().keybinding_target_id_for_selection_mode(
                      quader::QuaderSelectionMode::Edge)),
              "2");
    EXPECT_EQ(editor.commands().shortcut_for(
                  editor.commands().keybinding_target_id_for_selection_mode(
                      quader::QuaderSelectionMode::Face)),
              "3");
    EXPECT_EQ(editor.commands().shortcut_for(
                  editor.commands().keybinding_target_id_for_selection_mode(
                      quader::QuaderSelectionMode::Object)),
              "4");
    EXPECT_EQ(editor.commands().shortcut_for(
                  editor.commands().keybinding_target_id_for_selection_mode(
                      quader::QuaderSelectionMode::Group)),
              "5");
    for (int preset = 1; preset <= 10; ++preset) {
      const std::string target =
          editor.commands().keybinding_target_id_for_grid_preset(preset);
      EXPECT_EQ(target,
                quader::quader_viewport_grid_preset_target_id(preset));
      EXPECT_EQ(editor.commands().shortcut_for(target), "");
    }
    EXPECT_FALSE(editor.commands().last_status().has_value());
    EXPECT_EQ(editor.commands().last_status_revision(), 0u);
    const quader::QuaderApiVoidResult missing_command =
        editor.commands().execute("__missing_command__");
    EXPECT_FALSE(missing_command.ok());
    EXPECT_FALSE(missing_command);
    EXPECT_EQ(missing_command.error_message(), "Unknown command id.");
    ASSERT_TRUE(editor.commands().last_status().has_value());
    expect_same_api_status(*editor.commands().last_status(), missing_command);
    EXPECT_EQ(editor.commands().last_status_revision(), 1u);
    EXPECT_EQ(editor.commands()
                  .execute("modeling.__missing_action__")
                  .error_message(),
              "Unknown command id.");
    EXPECT_EQ(editor.commands().last_status_revision(), 2u);
    EXPECT_EQ(editor.commands()
                  .execute(quader::quader_tool_activation_command_target_id(
                      "__missing_tool__"))
                  .error_message(),
              "Unknown command id.");
    EXPECT_EQ(editor.commands().last_status_revision(), 3u);

    quader::QuaderCreateBoxArgs args;
    args.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
    args.name = "Editor API Box";

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
        editor.modeling().create_box(args);
    ASSERT_TRUE(created.ok()) << created.error_message();
    EXPECT_TRUE(created.changed());
    EXPECT_NE(created.value().entity, quader::kInvalidEntityId);
    ASSERT_TRUE(scene_root(editor).has_value());
    EXPECT_EQ(user_entity_count(editor), 1u);

    const quader::QuaderApiVoidResult undo =
        editor.commands().execute(quader::quader_command::kEditUndo);
    EXPECT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(undo.changed());
    ASSERT_TRUE(editor.commands().last_status().has_value());
    expect_same_api_status(*editor.commands().last_status(), undo);
    EXPECT_EQ(editor.commands().last_status_revision(), 4u);
}

TEST(EditorApiTests, ProjectLifecycleCommandsRouteThroughProjectApi) {
  auto compare_project_command = [](quader::QuaderCommandId command_id,
                                    auto direct_call) {
    CommandTestHarness command_harness;
    quader::QuaderEditorApi &command_editor = command_harness.editor;
    CommandTestHarness direct_harness;
    quader::QuaderEditorApi &direct_editor = direct_harness.editor;

    const quader::QuaderApiVoidResult command_result =
        command_editor.commands().execute(command_id);
    const quader::QuaderApiVoidResult direct_result =
        direct_call(direct_editor.project());
    expect_same_api_status(command_result, direct_result);
  };

  compare_project_command(
      quader::quader_command::kProjectNew,
      [](quader::QuaderProjectApi &project) { return project.new_project(); });
  compare_project_command(
      quader::quader_command::kProjectOpen,
      [](quader::QuaderProjectApi &project) { return project.open({}); });
}

TEST(EditorApiTests, SceneDocumentCommandsRouteThroughSceneDocumentsApi) {
  auto compare_scene_command = [](quader::QuaderCommandId command_id,
                                  auto direct_call,
                                  bool make_dirty = false) {
    CommandTestHarness command_harness;
    quader::QuaderEditorApi &command_editor = command_harness.editor;
    CommandTestHarness direct_harness;
    quader::QuaderEditorApi &direct_editor = direct_harness.editor;
    if (make_dirty) {
      (void)create_selected_box(command_editor, "Command Dirty Scene");
      (void)create_selected_box(direct_editor, "Direct Dirty Scene");
    }

    const quader::QuaderApiVoidResult command_result =
        command_editor.commands().execute(command_id);
    const quader::QuaderApiStatus direct_result =
        quader::quader_api_status_from_result(direct_call(direct_editor));
    expect_same_api_status(command_result, direct_result);
  };

  compare_scene_command(
      quader::quader_command::kSceneDocumentNew,
      [](quader::QuaderEditorApi &editor) {
        return editor.scene_documents().new_scene();
      });
  compare_scene_command(
      quader::quader_command::kSceneDocumentOpen,
      [](quader::QuaderEditorApi &editor) {
        return editor.scene_documents().open();
      });
  compare_scene_command(
      quader::quader_command::kSceneDocumentSave,
      [](quader::QuaderEditorApi &editor) {
        return editor.scene_documents().save_active();
      });
  compare_scene_command(
      quader::quader_command::kSceneDocumentSaveAs,
      [](quader::QuaderEditorApi &editor) {
        return editor.scene_documents().save_as(
            *editor.scene_documents().active_scene_id());
      });
  compare_scene_command(
      quader::quader_command::kSceneDocumentSaveAll,
      [](quader::QuaderEditorApi &editor) {
        return editor.scene_documents().save_all();
      },
      true);
  compare_scene_command(
      quader::quader_command::kSceneDocumentClose,
      [](quader::QuaderEditorApi &editor) {
        return editor.scene_documents().close_active();
      });
}

TEST(EditorApiTests,
     DirtyProjectReplacementCancelPreservesStateAndDiscardUsesOneBoundary) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  create_selected_box(editor, "Dirty Replacement Box");
  ASSERT_TRUE(editor.project().dirty());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  const quader::QuaderHistorySnapshot before = editor.query().history();

  editor.project().set_runtime_hooks(quader::QuaderProjectRuntimeHooks{
      .confirm_dirty_project_replacement =
          [](std::string_view) {
            return quader::QuaderDirtyProjectDecision::Cancel;
          },
  });
  const quader::QuaderApiVoidResult canceled = editor.project().new_project();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_FALSE(canceled.changed());
  EXPECT_TRUE(entity_named(editor, "Dirty Replacement Box").has_value());
  EXPECT_TRUE(editor.project().dirty());
  EXPECT_TRUE(editor.tools().active_settings().active);
  EXPECT_EQ(editor.query().history().undo_label, before.undo_label);

  editor.project().set_runtime_hooks(quader::QuaderProjectRuntimeHooks{
      .confirm_dirty_project_replacement =
          [](std::string_view) {
            return quader::QuaderDirtyProjectDecision::Discard;
          },
  });
  const quader::QuaderApiVoidResult discarded = editor.project().new_project();
  ASSERT_TRUE(discarded.ok()) << discarded.error_message();
  EXPECT_TRUE(discarded.changed());
  EXPECT_FALSE(entity_named(editor, "Dirty Replacement Box").has_value());
  EXPECT_FALSE(editor.project().dirty());
  EXPECT_FALSE(editor.tools().active_settings().active);
  EXPECT_FALSE(editor.history().can_undo());
}

TEST(EditorApiTests,
     DirtyOpenPreflightsBeforeReadingAndRereadsAfterSavingTheTarget) {
  {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    create_selected_box(editor, "Canceled Dirty Open Box");
    int confirmations = 0;
    editor.project().set_runtime_hooks(quader::QuaderProjectRuntimeHooks{
        .confirm_dirty_project_replacement =
            [&confirmations](std::string_view action_id) {
              ++confirmations;
              EXPECT_EQ(action_id, "project.open");
              return quader::QuaderDirtyProjectDecision::Cancel;
            },
    });
    const std::filesystem::path missing =
        project_test_path("dirty-open-cancel-missing");
    std::filesystem::remove(missing);

    const quader::QuaderApiVoidResult canceled = editor.project().open(missing);
    ASSERT_TRUE(canceled.ok()) << canceled.error_message();
    EXPECT_FALSE(canceled.changed());
    EXPECT_EQ(confirmations, 1);
    EXPECT_TRUE(entity_named(editor, "Canceled Dirty Open Box").has_value());
    EXPECT_TRUE(editor.project().dirty());
  }

  const std::filesystem::path path =
      project_test_path("dirty-open-save-same-target");
  std::filesystem::remove(path);
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  create_selected_box(editor, "Saved Open Baseline");
  ASSERT_TRUE(save_scene_as_native(editor, path).ok());
  const quader::QuaderApiResult<quader::QuaderEntityRef> unsaved =
      editor.scene().create_entity(
          quader::QuaderCreateEntityArgs{.name = "Saved During Open Preflight"});
  ASSERT_TRUE(unsaved.ok()) << unsaved.error_message();
  ASSERT_TRUE(editor.project().dirty());
  editor.project().set_runtime_hooks(quader::QuaderProjectRuntimeHooks{
      .confirm_dirty_project_replacement =
          [](std::string_view action_id) {
            EXPECT_EQ(action_id, "project.open");
            return quader::QuaderDirtyProjectDecision::Save;
          },
  });

  const quader::QuaderApiVoidResult opened = editor.project().open(path);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  EXPECT_TRUE(opened.changed());
  EXPECT_TRUE(entity_named(editor, "Saved During Open Preflight").has_value());
  EXPECT_FALSE(editor.project().dirty());

  CommandTestHarness disk_harness;
  const quader::QuaderApiVoidResult disk_opened =
      disk_harness.editor.project().open(path);
  ASSERT_TRUE(disk_opened.ok()) << disk_opened.error_message();
  EXPECT_TRUE(entity_named(disk_harness.editor,
                           "Saved During Open Preflight")
                  .has_value());
  std::filesystem::remove(path);
}

TEST(EditorApiTests, ProjectSaveAsSaveAndOpenUseQdrDocumentIo) {
  const std::filesystem::path path = project_test_path("roundtrip");
  CommandTestHarness save_harness;
  quader::QuaderEditorApi &editor = save_harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Persisted Box");
  ASSERT_NE(box.entity, quader::kInvalidEntityId);

  const std::optional<quader::QuaderEntitySnapshot> root = scene_root(editor);
  ASSERT_TRUE(root.has_value());
  ASSERT_TRUE(root->has_scene_settings);
  quader::QuaderSceneSettings settings = root->scene_settings;
  settings.environment.environment_id = "pillars_2k";
  settings.environment.enabled = false;
  settings.environment.show_background = false;
  settings.environment.clear_color = {0.2f, 0.3f, 0.4f};
  settings.environment.intensity = 2.25f;
  settings.environment.background_intensity = 0.5f;
  settings.environment.rotation_y = 1.25f;
  settings.camera.aperture = 8.0f;
  settings.camera.shutter_speed = 1.0f / 60.0f;
  settings.camera.sensitivity = 320.0f;
  settings.camera.near_plane = 0.1f;
  settings.camera.far_plane = 500.0f;
  settings.post_process.enabled = true;
  settings.post_process.anti_aliasing = false;
  settings.post_process.ambient_occlusion = true;
  settings.post_process.ambient_occlusion_intensity = 2.5f;
  settings.post_process.ambient_occlusion_radius = 1.1f;
  settings.post_process.bloom = true;
  settings.post_process.bloom_strength = 0.65f;
  settings.post_process.dithering = false;
  settings.post_process.tone_mapping = quader::QuaderToneMapping::DisplayRange;
  ASSERT_TRUE(editor.scene().set_scene_settings(root->id, settings).ok());

  quader::QuaderCreatePointLightArgs light_args;
  light_args.name = "Persisted Key Light";
  light_args.transform.position = {3.0f, 4.0f, 5.0f};
  light_args.light = quader::QuaderLightComponent{
      .type = quader::QuaderLightType::Point,
      .color = {1.0f, 0.75f, 0.5f},
      .intensity = 950.0f,
      .range = 16.0f,
      .casts_shadows = true,
  };
  light_args.select_created = true;
  const quader::QuaderApiResult<quader::QuaderEntityRef> light =
      editor.scene().create_point_light(light_args);
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_NE(light.value().id, quader::kInvalidEntityId);
  EXPECT_TRUE(editor.project().dirty());

  const quader::QuaderApiVoidResult saved =
      save_scene_as_native(editor, path);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  EXPECT_TRUE(saved.changed());
  EXPECT_FALSE(editor.project().dirty());
  EXPECT_TRUE(std::filesystem::exists(path));
  const std::string saved_text = read_text_file(path);
  ASSERT_GE(saved_text.size(), 4u);
  EXPECT_EQ(saved_text.substr(0u, 4u), "QDRB");

  CommandTestHarness open_harness;
  quader::QuaderEditorApi &opened = open_harness.editor;
  const quader::QuaderRevisionStamp before_open_revisions = opened.revisions();
  const quader::QuaderApiVoidResult open_result =
      open_scene_native(opened, path);
  ASSERT_TRUE(open_result.ok()) << open_result.error_message();
  EXPECT_TRUE(open_result.changed());
  EXPECT_FALSE(opened.project().dirty());
  EXPECT_GT(opened.revisions().scene_revision,
            before_open_revisions.scene_revision);
  EXPECT_TRUE(entity_named(opened, "Persisted Box").has_value());
  EXPECT_EQ(user_entity_count(opened), 2u);
  expect_scene_settings_eq(scene_root_settings(opened), settings);
  const std::optional<quader::QuaderEntitySnapshot> opened_light =
      entity_named(opened, "Persisted Key Light");
  ASSERT_TRUE(opened_light.has_value());
  ASSERT_TRUE(opened_light->has_light);
  EXPECT_EQ(opened_light->kind, quader::QuaderEntityKind::Light);
  EXPECT_FLOAT_EQ(opened_light->transform.position.x, 3.0f);
  EXPECT_FLOAT_EQ(opened_light->transform.position.y, 4.0f);
  EXPECT_FLOAT_EQ(opened_light->transform.position.z, 5.0f);
  expect_light_eq(opened_light->light, light_args.light);
  EXPECT_TRUE(selected_entities(opened).empty())
      << "selection is transient and must not be stored in a scene file";

  quader::QuaderTransform moved_transform = opened_light->transform;
  moved_transform.position.x += 0.25f;
  const quader::QuaderApiVoidResult moved = opened.scene().set_transform(
      opened_light->id, moved_transform);
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  EXPECT_TRUE(moved.changed());
  EXPECT_TRUE(opened.project().dirty());
  const quader::QuaderApiVoidResult saved_again =
      opened.scene_documents().save_active();
  ASSERT_TRUE(saved_again.ok()) << saved_again.error_message();
  EXPECT_FALSE(opened.project().dirty());
}

TEST(EditorApiTests,
     CancelingOperationPreviewRestoresCleanProjectContentCheckpoint) {
  const std::filesystem::path path =
      project_test_path("cancel-preview-clean-checkpoint");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Cancel Preview Clean Box");
  ASSERT_TRUE(save_scene_as_native(editor, path).ok());
  ASSERT_FALSE(editor.project().dirty());
  const quader::QuaderRevisionStamp clean_revisions = editor.revisions();
  const std::optional<quader::QuaderMeshSnapshot> before =
      editor.query().mesh(box.mesh);
  ASSERT_TRUE(before.has_value());

  const quader::QuaderApiVoidResult preview = editor.commands().execute(
      editor.commands().command_id_for_modeling_action("half_cut"));
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  ASSERT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());

  const quader::QuaderApiVoidResult canceled =
      editor.tools().cancel_active_tool_preview();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_TRUE(canceled.changed());
  EXPECT_FALSE(editor.project().dirty());
  const quader::QuaderRevisionStamp restored_revisions = editor.revisions();
  EXPECT_TRUE(restored_revisions.scene_revision >
                  clean_revisions.scene_revision ||
              restored_revisions.asset_revision >
                  clean_revisions.asset_revision);
  const std::optional<quader::QuaderMeshSnapshot> after =
      editor.query().mesh(box.mesh);
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->vertex_count, before->vertex_count);
  EXPECT_EQ(after->primitive_count, before->primitive_count);

  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST(EditorApiTests, CommittingOperationPreviewMakesCleanProjectDirty) {
  const std::filesystem::path path =
      project_test_path("commit-preview-dirty-checkpoint");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  (void)create_selected_box(editor, "Commit Preview Dirty Box");
  ASSERT_TRUE(save_scene_as_native(editor, path).ok());
  ASSERT_FALSE(editor.project().dirty());

  const quader::QuaderApiVoidResult preview = editor.commands().execute(
      editor.commands().command_id_for_modeling_action("half_cut"));
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  const quader::QuaderApiVoidResult committed =
      editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.changed());
  EXPECT_TRUE(editor.project().dirty());

  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST(EditorApiTests,
     CancelingOperationPreviewDoesNotHideAnInterveningSceneEdit) {
  const std::filesystem::path path =
      project_test_path("cancel-preview-concurrent-edit");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Cancel Preview Concurrent Edit Box");
  ASSERT_TRUE(save_scene_as_native(editor, path).ok());
  ASSERT_FALSE(editor.project().dirty());

  const quader::QuaderApiVoidResult preview = editor.commands().execute(
      editor.commands().command_id_for_modeling_action("half_cut"));
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  ASSERT_TRUE(editor.scene().set_visible(box.entity, false).ok());
  ASSERT_TRUE(editor.project().dirty());

  const quader::QuaderApiVoidResult canceled =
      editor.tools().cancel_active_tool_preview();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_TRUE(editor.project().dirty());
  const std::optional<quader::QuaderEntitySnapshot> after =
      editor.query().entity(box.entity);
  ASSERT_TRUE(after.has_value());
  EXPECT_FALSE(after->visible);

  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST(EditorApiTests,
     CancelingPrimitiveSessionDoesNotHideAnInterveningSceneEdit) {
  const std::filesystem::path path =
      project_test_path("cancel-primitive-concurrent-edit");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(save_scene_as_native(editor, path).ok());
  ASSERT_FALSE(editor.project().dirty());

  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_FALSE(editor.project().dirty());
  quader::QuaderCreateEntityArgs args;
  args.name = "Primitive Session Concurrent Edit";
  const quader::QuaderApiResult<quader::QuaderEntityRef> created =
      editor.scene().create_entity(args);
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.project().dirty());

  const quader::QuaderApiVoidResult canceled =
      editor.tools().cancel_active_tool_preview();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_TRUE(editor.project().dirty());
  EXPECT_TRUE(editor.query().entity(created.value().id).has_value());

  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST(EditorApiTests, ProjectNewCommandOpensCleanUnsavedEmptyProject) {
  const std::filesystem::path path = project_test_path("new-command");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  (void)create_selected_box(editor, "Project New Saved Box");

  const quader::QuaderApiVoidResult saved =
      save_scene_as_native(editor, path);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  EXPECT_FALSE(editor.project().dirty());
  EXPECT_TRUE(editor.history().can_undo());
  EXPECT_FALSE(selected_entities(editor).empty());

  quader::QuaderCreateEntityArgs entity_args;
  entity_args.name = "Project New Dirty Entity";
  const quader::QuaderApiResult<quader::QuaderEntityRef> dirty_entity =
      editor.scene().create_entity(entity_args);
  ASSERT_TRUE(dirty_entity.ok()) << dirty_entity.error_message();
  EXPECT_TRUE(editor.project().dirty());
  EXPECT_GT(user_entity_count(editor), 1u);

  editor.project().set_runtime_hooks(quader::QuaderProjectRuntimeHooks{
      .confirm_dirty_project_replacement =
          [](std::string_view) {
            return quader::QuaderDirtyProjectDecision::Discard;
          },
  });

  const quader::QuaderApiVoidResult created =
      editor.commands().execute(quader::quader_command::kProjectNew);
  ASSERT_TRUE(created.ok()) << created.error_message();
  EXPECT_TRUE(created.changed());
  EXPECT_TRUE(editor.project().path().empty());
  EXPECT_FALSE(editor.project().dirty());
  EXPECT_EQ(user_entity_count(editor), 0u);
  EXPECT_TRUE(selected_entities(editor).empty());
  EXPECT_TRUE(scene_root(editor).has_value());
  EXPECT_FALSE(editor.history().can_undo());
  EXPECT_FALSE(editor.history().can_redo());
  EXPECT_FALSE(editor.query().history().can_undo);
  EXPECT_FALSE(editor.query().history().can_redo);
}

TEST(EditorApiTests, SceneSaveCommandUsesRuntimePathHookForUnsavedScene) {
  const std::filesystem::path path = project_test_path("scene-runtime-hooks");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  int save_path_requests = 0;
  editor.scene_documents().set_runtime_hooks(
      quader::QuaderSceneDocumentsRuntimeHooks{
      .choose_save_scene_as_path =
          [&](quader::SceneSessionId)
              -> std::optional<std::filesystem::path> {
        ++save_path_requests;
        return path;
      },
  });

  (void)create_selected_box(editor, "Hook Saved Box");
  const quader::QuaderApiVoidResult saved =
      editor.commands().execute(quader::quader_command::kSceneDocumentSave);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  EXPECT_EQ(save_path_requests, 1);
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_FALSE(editor.scene_documents().active_scene()->dirty);
}

TEST(EditorApiTests, ProjectOpenCommandUsesRuntimePathHookForEmptyPath) {
  const std::filesystem::path path = project_test_path("open-runtime-hook");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  (void)create_selected_box(editor, "Hook Saved Box");
  ASSERT_TRUE(save_scene_as_native(editor, path).ok());

  ASSERT_TRUE(
      editor.commands().execute(quader::quader_command::kProjectNew).ok());
  EXPECT_FALSE(entity_named(editor, "Hook Saved Box").has_value());

  int open_path_requests = 0;
  editor.project().set_runtime_hooks(quader::QuaderProjectRuntimeHooks{
      .choose_open_path = [&]() -> std::optional<std::filesystem::path> {
        ++open_path_requests;
        return path;
      },
  });
  const quader::QuaderApiVoidResult opened =
      editor.commands().execute(quader::quader_command::kProjectOpen);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  EXPECT_EQ(open_path_requests, 1);
  EXPECT_TRUE(entity_named(editor, "Hook Saved Box").has_value());
}

TEST(EditorApiTests, PublicAssetApiOwnsStandaloneMeshReplacementPolicy) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiResult<quader::QuaderAssetRef> standalone_mesh =
        editor.assets().add_standalone_mesh(triangle_mesh("Standalone"));
    ASSERT_TRUE(standalone_mesh.ok()) << standalone_mesh.error_message();

    quader::QuaderMeshAsset replacement = triangle_mesh("Replacement");
    replacement.vertices.push_back({.position = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.0f, 0.0f}});
    const quader::QuaderApiVoidResult replaced =
        editor.assets().replace_standalone_mesh(standalone_mesh.value().id,
                                                replacement);
    ASSERT_TRUE(replaced.ok()) << replaced.error_message();
    EXPECT_TRUE(replaced.changed());
    const std::optional<quader::QuaderMeshSnapshot> mesh_snapshot =
        editor.query().mesh(standalone_mesh.value().id);
    ASSERT_TRUE(mesh_snapshot.has_value());
    EXPECT_EQ(mesh_snapshot->vertex_count, replacement.vertices.size());

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef>
        modeling_object = editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Modeling Owned");
    ASSERT_TRUE(modeling_object.ok()) << modeling_object.error_message();

    const quader::QuaderApiVoidResult api_rejected =
        editor.assets().replace_standalone_mesh(modeling_object.value().mesh,
                                                triangle_mesh("Rejected"));
    EXPECT_FALSE(api_rejected.ok());
    EXPECT_NE(api_rejected.error_message().find("modeling object"),
              std::string::npos);
}

TEST(EditorApiTests, PublicAssetApiEditsAreUndoableThroughHistory) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiResult<quader::QuaderAssetRef> material =
        editor.assets().add_material(
            quader::QuaderMaterialArgs{.name = "Undoable API Material"});
    ASSERT_TRUE(material.ok()) << material.error_message();
    EXPECT_TRUE(material.changed());
    EXPECT_EQ(material.mutation_kind, quader::QuaderMutationKind::Asset);
    EXPECT_EQ(editor.query().history().undo_label, "Add Material");
    ASSERT_TRUE(editor.query().material(material.value().id).has_value());

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_FALSE(editor.query().material(material.value().id).has_value());
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_TRUE(editor.query().material(material.value().id).has_value());

    const quader::QuaderApiVoidResult color_changed =
        editor.assets().set_material_base_color(material.value().id,
                                                {0.2f, 0.4f, 0.6f});
    ASSERT_TRUE(color_changed.ok()) << color_changed.error_message();
    EXPECT_TRUE(color_changed.changed());
    EXPECT_EQ(color_changed.mutation_kind, quader::QuaderMutationKind::Asset);
    ASSERT_TRUE(editor.query().material(material.value().id).has_value());
    EXPECT_FLOAT_EQ(editor.query().material(material.value().id)->base_color.z,
                    0.6f);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_FLOAT_EQ(editor.query().material(material.value().id)->base_color.z,
                    1.0f);
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_FLOAT_EQ(editor.query().material(material.value().id)->base_color.x,
                    0.2f);

    quader::QuaderTextureAsset texture;
    texture.name = "Undoable API Texture";
    texture.usage = quader::QuaderTextureUsage::BaseColor;
    const quader::QuaderApiResult<quader::QuaderAssetRef> texture_ref =
        editor.assets().add_texture(std::move(texture));
    ASSERT_TRUE(texture_ref.ok()) << texture_ref.error_message();
    const quader::QuaderApiVoidResult texture_slot =
        editor.assets().set_material_texture(
            material.value().id, "baseColor", texture_ref.value().id);
    ASSERT_TRUE(texture_slot.ok()) << texture_slot.error_message();
    ASSERT_TRUE(editor.query().material(material.value().id).has_value());
    EXPECT_TRUE(editor.query()
                    .material(material.value().id)
                    ->texture_slots.contains("baseColorTexture"));
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(editor.query().material(material.value().id).has_value());
    EXPECT_FALSE(editor.query()
                     .material(material.value().id)
                     ->texture_slots.contains("baseColorTexture"));
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(editor.query()
                  .material(material.value().id)
                  ->texture_slots.at("baseColorTexture"),
              texture_ref.value().id);

    const quader::QuaderApiResult<quader::QuaderAssetRef> mesh =
        editor.assets().add_standalone_mesh(triangle_mesh("Undoable API Mesh"));
    ASSERT_TRUE(mesh.ok()) << mesh.error_message();
    quader::QuaderMeshAsset replacement =
        triangle_mesh("Undoable API Replacement");
    replacement.vertices.push_back(
        {.position = {0.0f, 2.0f, 0.0f},
         .normal = {0.0f, 1.0f, 0.0f},
         .uv = {0.0f, 0.0f}});
    const quader::QuaderApiVoidResult mesh_replaced =
        editor.assets().replace_standalone_mesh(mesh.value().id, replacement);
    ASSERT_TRUE(mesh_replaced.ok()) << mesh_replaced.error_message();
    ASSERT_TRUE(editor.query().mesh(mesh.value().id).has_value());
    EXPECT_EQ(editor.query().mesh(mesh.value().id)->vertex_count, 4u);
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(editor.query().mesh(mesh.value().id).has_value());
    EXPECT_EQ(editor.query().mesh(mesh.value().id)->vertex_count, 3u);
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(editor.query().mesh(mesh.value().id)->vertex_count, 4u);
}

TEST(EditorApiTests, SceneCommandsRejectModelingOwnedEntityStructuralAndMeshMutations) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Modeling Owned Guard");
    const quader::QuaderApiResult<quader::QuaderAssetRef> replacement_mesh =
        editor.assets().add_standalone_mesh(
            triangle_mesh("Standalone Replacement"));
    ASSERT_TRUE(replacement_mesh.ok()) << replacement_mesh.error_message();

    const quader::QuaderApiVoidResult destroyed = editor.scene().destroy(box.entity);
    EXPECT_FALSE(destroyed.ok());
    EXPECT_NE(destroyed.error_message().find("modeling object"),
              std::string::npos);
    EXPECT_TRUE(editor.query().entity(box.entity).has_value());
    EXPECT_TRUE(editor.object(box).valid());

    const quader::QuaderApiResult<quader::QuaderEntityRef> duplicated = editor.object(box).duplicate();
    EXPECT_FALSE(duplicated.ok());
    EXPECT_NE(duplicated.error_message().find("modeling object"),
              std::string::npos);

    const quader::QuaderApiVoidResult replaced =
        editor.scene().set_mesh_component(
            box.entity,
            quader::QuaderMeshComponent{.mesh = replacement_mesh.value().id});
    EXPECT_FALSE(replaced.ok());
    EXPECT_NE(replaced.error_message().find("modeling object"),
              std::string::npos);
    const std::optional<quader::QuaderEntitySnapshot> snapshot = editor.query().entity(box.entity);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->has_mesh);
    EXPECT_EQ(snapshot->mesh, box.mesh);
}

TEST(EditorApiTests, PreferencesPersistAndResolveCustomCommandShortcuts) {
  const std::filesystem::path path = keybinding_test_path("command");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;

  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  const quader::QuaderApiVoidResult updated = editor.preferences().set_shortcut(
      quader::quader_command::kEditUndo, "Ctrl+Alt+Z");
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  EXPECT_TRUE(updated.changed());
  EXPECT_EQ(editor.commands().shortcut_for(quader::quader_command::kEditUndo),
            "Ctrl+Alt+Z");
  EXPECT_EQ(editor.commands().shortcut_for(quader::quader_command::kEditUndo),
            "Ctrl+Alt+Z");
  EXPECT_NE(read_text_file(path).find("\"key\": \"Ctrl+Alt+Z\""),
            std::string::npos);

  CommandTestHarness reloaded_harness;
  quader::QuaderEditorApi &reloaded = reloaded_harness.editor;
  ASSERT_TRUE(reloaded.commands().set_keybinding_storage_path(path).ok());
  EXPECT_EQ(reloaded.commands().shortcut_for(quader::quader_command::kEditUndo),
            "Ctrl+Alt+Z");

  ASSERT_TRUE(reloaded.preferences()
                  .reset_shortcut(quader::quader_command::kEditUndo)
                  .ok());
  EXPECT_EQ(reloaded.commands().shortcut_for(quader::quader_command::kEditUndo),
            "Ctrl+Z");
}

TEST(EditorApiTests, ViewportDisplayAndSnappingPreferencesUseRuntimeHooks) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  bool snapping_enabled = true;
  bool backface_selection_enabled = false;
  bool grid_visible = true;
  bool gizmos_visible = true;
  bool mesh_grid_visible = true;
  bool overlays_visible = true;
  bool grid_snap_enabled = true;
  bool vertex_snap_enabled = true;
  bool rotation_snap_enabled = true;
  double rotation_snap_degrees = 15.0;
  const auto runtime_result = [](bool changed) {
    return quader::QuaderApiVoidResult{
        .ok = true,
        .changed = changed,
        .message = {},
        .mutation_kind = quader::QuaderMutationKind::Runtime,
    };
  };
  editor.preferences().set_runtime_hooks(quader::QuaderPreferencesRuntimeHooks{
      .snapping_enabled = [&]() { return snapping_enabled; },
      .set_snapping_enabled =
          [&](bool enabled) {
            const bool changed = snapping_enabled != enabled;
            snapping_enabled = enabled;
            return runtime_result(changed);
          },
      .backface_selection_enabled =
          [&]() { return backface_selection_enabled; },
      .set_backface_selection_enabled =
          [&](bool enabled) {
            const bool changed = backface_selection_enabled != enabled;
            backface_selection_enabled = enabled;
            return runtime_result(changed);
          },
      .grid_visible = [&]() { return grid_visible; },
      .set_grid_visible =
          [&](bool visible) {
            const bool changed = grid_visible != visible;
            grid_visible = visible;
            return runtime_result(changed);
          },
      .gizmos_visible = [&]() { return gizmos_visible; },
      .set_gizmos_visible =
          [&](bool visible) {
            const bool changed = gizmos_visible != visible;
            gizmos_visible = visible;
            return runtime_result(changed);
          },
      .mesh_grid_visible = [&]() { return mesh_grid_visible; },
      .set_mesh_grid_visible =
          [&](bool visible) {
            const bool changed = mesh_grid_visible != visible;
            mesh_grid_visible = visible;
            return runtime_result(changed);
          },
      .overlays_visible = [&]() { return overlays_visible; },
      .set_overlays_visible =
          [&](bool visible) {
            const bool changed = overlays_visible != visible;
            overlays_visible = visible;
            return runtime_result(changed);
          },
      .grid_snap_enabled = [&]() { return grid_snap_enabled; },
      .set_grid_snap_enabled =
          [&](bool enabled) {
            const bool changed = grid_snap_enabled != enabled;
            grid_snap_enabled = enabled;
            return runtime_result(changed);
          },
      .vertex_snap_enabled = [&]() { return vertex_snap_enabled; },
      .set_vertex_snap_enabled =
          [&](bool enabled) {
            const bool changed = vertex_snap_enabled != enabled;
            vertex_snap_enabled = enabled;
            return runtime_result(changed);
          },
      .rotation_snap_enabled = [&]() { return rotation_snap_enabled; },
      .set_rotation_snap_enabled =
          [&](bool enabled) {
            const bool changed = rotation_snap_enabled != enabled;
            rotation_snap_enabled = enabled;
            return runtime_result(changed);
          },
      .rotation_snap_degrees = [&]() { return rotation_snap_degrees; },
      .set_rotation_snap_degrees =
          [&](double degrees) {
            const bool changed = rotation_snap_degrees != degrees;
            rotation_snap_degrees = degrees;
            return runtime_result(changed);
          },
  });

  EXPECT_TRUE(editor.preferences().snapping_enabled());
  EXPECT_FALSE(editor.preferences().backface_selection_enabled());
  EXPECT_TRUE(editor.preferences().grid_visible());
  EXPECT_TRUE(editor.preferences().gizmos_visible());
  EXPECT_TRUE(editor.preferences().mesh_grid_visible());
  EXPECT_TRUE(editor.preferences().overlays_visible());
  EXPECT_TRUE(editor.preferences().grid_snap_enabled());
  EXPECT_TRUE(editor.preferences().vertex_snap_enabled());
  EXPECT_TRUE(editor.preferences().rotation_snap_enabled());
  EXPECT_DOUBLE_EQ(editor.preferences().rotation_snap_degrees(), 15.0);

  EXPECT_TRUE(editor.preferences().set_snapping_enabled(false).changed());
  EXPECT_TRUE(editor.preferences()
                  .set_backface_selection_enabled(true)
                  .changed());
  EXPECT_TRUE(editor.preferences().set_grid_visible(false).changed());
  EXPECT_TRUE(editor.preferences().set_gizmos_visible(false).changed());
  EXPECT_TRUE(editor.preferences().set_mesh_grid_visible(false).changed());
  EXPECT_TRUE(editor.preferences().set_overlays_visible(false).changed());
  EXPECT_TRUE(editor.preferences().set_grid_snap_enabled(false).changed());
  EXPECT_TRUE(editor.preferences().set_vertex_snap_enabled(false).changed());
  EXPECT_TRUE(editor.preferences().set_rotation_snap_enabled(false).changed());
  EXPECT_TRUE(
      editor.preferences().set_rotation_snap_degrees(22.5).changed());
  EXPECT_FALSE(editor.preferences().snapping_enabled());
  EXPECT_TRUE(editor.preferences().backface_selection_enabled());
  EXPECT_FALSE(editor.preferences().grid_visible());
  EXPECT_FALSE(editor.preferences().gizmos_visible());
  EXPECT_FALSE(editor.preferences().mesh_grid_visible());
  EXPECT_FALSE(editor.preferences().overlays_visible());
  EXPECT_FALSE(editor.preferences().grid_snap_enabled());
  EXPECT_FALSE(editor.preferences().vertex_snap_enabled());
  EXPECT_FALSE(editor.preferences().rotation_snap_enabled());
  EXPECT_DOUBLE_EQ(editor.preferences().rotation_snap_degrees(), 22.5);
  EXPECT_FALSE(editor.preferences().set_rotation_snap_degrees(22.5).changed());

  EXPECT_TRUE(editor.commands()
                  .execute(quader::quader_command::kViewportToggleGrid)
                  .changed());
  EXPECT_TRUE(editor.commands()
                  .execute(quader::quader_command::kViewportToggleGizmos)
                  .changed());
  EXPECT_TRUE(editor.commands()
                  .execute(quader::quader_command::kViewportToggleMeshGrid)
                  .changed());
  EXPECT_TRUE(editor.commands()
                  .execute(quader::quader_command::kViewportToggleOverlays)
                  .changed());
  EXPECT_TRUE(editor.commands()
                  .execute(
                      quader::quader_command::kSelectToggleBackfaceSelection)
                  .changed());
  EXPECT_TRUE(editor.preferences().grid_visible());
  EXPECT_TRUE(editor.preferences().gizmos_visible());
  EXPECT_TRUE(editor.preferences().mesh_grid_visible());
  EXPECT_TRUE(editor.preferences().overlays_visible());
  EXPECT_FALSE(editor.preferences().backface_selection_enabled());
}

TEST(EditorApiTests, TextureLockPreferencesToggleIndependentlyAndRemainBindable) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  bool texture_lock = true;
  bool texture_lock_scale = false;
  const auto set_state = [](bool &state, bool enabled) {
    const bool changed = state != enabled;
    state = enabled;
    return quader::QuaderApiVoidResult{
        .ok = true,
        .changed = changed,
        .mutation_kind = quader::QuaderMutationKind::Runtime,
    };
  };
  editor.preferences().set_runtime_hooks(quader::QuaderPreferencesRuntimeHooks{
      .texture_lock_enabled = [&] { return texture_lock; },
      .set_texture_lock_enabled =
          [&](bool enabled) { return set_state(texture_lock, enabled); },
      .texture_lock_scale_enabled = [&] { return texture_lock_scale; },
      .set_texture_lock_scale_enabled =
          [&](bool enabled) { return set_state(texture_lock_scale, enabled); },
  });

  quader_poly::Document topology;
  const std::array corners{
      quader_poly::add_vertex(topology, {-1.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(topology, {1.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(topology, {1.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(topology, {-1.0f, 1.0f, 0.0f}),
  };
  ASSERT_NE(quader_poly::add_face(topology, corners),
            quader_poly::kInvalidElementId);
  topology.faces.front().uvs = {
      {0.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 2.0f}, {0.0f, 3.0f}};
  topology.faces.front().texture_transform = {
      .scale = {1.5f, -0.75f},
      .shift_uv = {0.25f, -0.125f},
      .rotation_degrees = 15.0f,
  };
  const auto textured_mesh = editor.modeling().create_mesh(
      quader::QuaderPolygonDocumentInternalAccess::from_backend(topology),
      quader::QuaderCreateMeshArgs{.name = "Texture Preference Fixture"});
  ASSERT_TRUE(textured_mesh.ok()) << textured_mesh.error_message();
  ASSERT_TRUE(editor.selection().select_object(textured_mesh.value()).ok());
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const auto selection = selected_entities(editor);
  ASSERT_EQ(selection.size(), 1u);
  const auto occurrence_before_scale = editor.query().entity(selection.front());
  ASSERT_TRUE(occurrence_before_scale.has_value());
  ASSERT_TRUE(occurrence_before_scale->has_instance_occurrence);
  auto transform = occurrence_before_scale->transform;
  transform.scale = {2.0f, 3.0f, 1.0f};
  ASSERT_TRUE(editor.scene().set_transform(selection.front(), transform).ok());
  const auto occurrence = editor.query().entity(selection.front());
  ASSERT_TRUE(occurrence.has_value());
  ASSERT_TRUE(occurrence->texture_sampling_from_local.has_value());
  EXPECT_NE(occurrence->texture_sampling_from_local->values,
            quader::QMat4{}.values);
  const auto before_toggles =
      quader::test::capture_command_observable_state(editor);
  const auto expect_document_unchanged = [&] {
    constexpr auto kUnchangedDomains =
        quader::test::kAllCommandObservableDomains &
        ~quader::test::command_observable_domain_mask(
            quader::test::CommandObservableDomain::Preferences);
    const auto change = quader::test::diff_command_observable_state(
        before_toggles, quader::test::capture_command_observable_state(editor),
        kUnchangedDomains);
    EXPECT_TRUE(change.empty()) << change.summary;
  };

  EXPECT_TRUE(editor.preferences().texture_lock_enabled());
  EXPECT_FALSE(editor.preferences().texture_lock_scale_enabled());
  EXPECT_FALSE(editor.preferences().set_texture_lock_enabled(true).changed());
  EXPECT_FALSE(editor.preferences().set_texture_lock_scale_enabled(false).changed());
  expect_document_unchanged();

  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kViewportToggleTextureLock)
                  .changed());
  EXPECT_FALSE(editor.preferences().texture_lock_enabled());
  EXPECT_FALSE(editor.preferences().texture_lock_scale_enabled());
  expect_document_unchanged();
  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kViewportToggleTextureLockScale)
                  .changed());
  EXPECT_FALSE(editor.preferences().texture_lock_enabled());
  EXPECT_TRUE(editor.preferences().texture_lock_scale_enabled());
  expect_document_unchanged();
  ASSERT_TRUE(editor.preferences().set_texture_lock_enabled(true).changed());
  EXPECT_TRUE(editor.preferences().texture_lock_enabled());
  EXPECT_TRUE(editor.preferences().texture_lock_scale_enabled());
  expect_document_unchanged();

  const std::filesystem::path path = keybinding_test_path("texture-locks");
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  const auto catalog = editor.commands().keybinding_catalog();
  constexpr std::array kCommands{
      quader::quader_command::kViewportToggleTextureLock,
      quader::quader_command::kViewportToggleTextureLockScale,
  };
  for (const auto command : kCommands) {
    const auto *entry = find_keybinding_catalog_entry(catalog, command.string());
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->default_shortcut.empty());
    EXPECT_TRUE(entry->unassigned);
    EXPECT_FALSE(entry->customized);
    EXPECT_TRUE(editor.commands().shortcut_for(command).empty());
  }
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(kCommands[0], "Ctrl+Alt+F18")
                  .ok());
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(kCommands[1], "Ctrl+Alt+F19")
                  .ok());
  CommandTestHarness reloaded;
  ASSERT_TRUE(reloaded.editor.commands().set_keybinding_storage_path(path).ok());
  EXPECT_EQ(reloaded.editor.commands().shortcut_for(kCommands[0]), "Ctrl+Alt+F18");
  EXPECT_EQ(reloaded.editor.commands().shortcut_for(kCommands[1]), "Ctrl+Alt+F19");
  ASSERT_TRUE(reloaded.editor.preferences().reset_shortcut(kCommands[0]).ok());
  EXPECT_TRUE(reloaded.editor.commands().shortcut_for(kCommands[0]).empty());
  EXPECT_EQ(reloaded.editor.commands().shortcut_for(kCommands[1]), "Ctrl+Alt+F19");
  std::filesystem::remove(path);
}

TEST(EditorApiTests,
     EditTextureCommandsHaveTypedFaceScopesAndUnassignedDefaults) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const std::filesystem::path path =
      keybinding_test_path("edit-texture-defaults");
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  constexpr std::array kCommands{
      quader::quader_command::kModelingTextureAlignToGrid,
      quader::quader_command::kModelingTextureAlignToFace,
      quader::quader_command::kModelingTextureAlignToView,
      quader::quader_command::kModelingTextureScaleXUp,
      quader::quader_command::kModelingTextureScaleXDown,
      quader::quader_command::kModelingTextureScaleYUp,
      quader::quader_command::kModelingTextureScaleYDown,
      quader::quader_command::kModelingTextureShiftLeft,
      quader::quader_command::kModelingTextureShiftRight,
      quader::quader_command::kModelingTextureShiftUp,
      quader::quader_command::kModelingTextureShiftDown,
      quader::quader_command::kModelingTextureRotateClockwise,
      quader::quader_command::kModelingTextureRotateCounterClockwise,
      quader::quader_command::kModelingTextureFlipTextureHorizontal,
      quader::quader_command::kModelingTextureFlipTextureVertical,
      quader::quader_command::kModelingTextureFlipFaceHorizontal,
      quader::quader_command::kModelingTextureFlipFaceVertical,
      quader::quader_command::kModelingTextureFitBoth,
      quader::quader_command::kModelingTextureFitX,
      quader::quader_command::kModelingTextureFitY,
      quader::quader_command::kModelingTextureJustifyLeft,
      quader::quader_command::kModelingTextureJustifyRight,
      quader::quader_command::kModelingTextureJustifyTop,
      quader::quader_command::kModelingTextureJustifyBottom,
      quader::quader_command::kModelingTextureJustifyCenter,
  };
  const auto typed = typed_command_ids();
  const auto registered = available_command_ids(editor);
  const auto catalog = editor.commands().keybinding_catalog();
  for (const quader::QuaderCommandId command : kCommands) {
    SCOPED_TRACE(command.string());
    EXPECT_TRUE(typed.contains(std::string(command.string())));
    EXPECT_TRUE(registered.contains(std::string(command.string())));
    const auto descriptor = editor.commands().descriptor(command);
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_FALSE(descriptor->enabled);
    EXPECT_FALSE(descriptor->label.empty());

    const auto *entry =
        find_keybinding_catalog_entry(catalog, command.string());
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->scope_id, "viewport.selection.face");
    EXPECT_EQ(entry->localization_key,
              "command." + std::string(command.string()));
    EXPECT_TRUE(entry->default_shortcut.empty());
    EXPECT_TRUE(entry->unassigned);
    EXPECT_FALSE(entry->customized);
    EXPECT_TRUE(editor.commands().shortcut_for(command).empty());
  }
  EXPECT_EQ(std::count_if(registered.begin(), registered.end(),
                         [](const std::string &id) {
                           return id.starts_with("modeling.texture.");
                         }),
            kCommands.size());

  const auto object = create_selected_box(editor, "Edit Texture Commands");
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(object, 1).ok());
  for (const quader::ViewportToolId tool :
       {quader::ViewportToolId::Select, quader::ViewportToolId::Move,
        quader::ViewportToolId::Rotate, quader::ViewportToolId::Scale}) {
    ASSERT_TRUE(editor.tools().activate(tool).ok());
    for (const quader::QuaderCommandId command : kCommands) {
      const auto descriptor = editor.commands().descriptor(command);
      ASSERT_TRUE(descriptor.has_value()) << command.string();
      EXPECT_TRUE(descriptor->enabled) << command.string();
    }
  }

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  for (const quader::QuaderCommandId command : kCommands) {
    EXPECT_FALSE(editor.commands().descriptor(command)->enabled)
        << command.string();
  }
  std::filesystem::remove(path);
}

TEST(EditorApiTests,
     EditTextureKeybindingsPersistRespectContextAndReuseExclusiveScopes) {
  const std::filesystem::path path =
      keybinding_test_path("edit-texture-rebinding");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  constexpr auto kTextureCommand =
      quader::quader_command::kModelingTextureScaleXUp;
  constexpr auto kEdgeCommand = quader::quader_command::kSelectEdgeLoops;
  constexpr std::string_view kShortcut = "Ctrl+Alt+F20";
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(kTextureCommand, std::string(kShortcut))
                  .ok());

  const auto catalog = editor.commands().keybinding_catalog();
  const auto *edge =
      find_keybinding_catalog_entry(catalog, kEdgeCommand.string());
  ASSERT_NE(edge, nullptr);
  const auto analysis =
      editor.commands().analyze_shortcut_assignment(edge->key, kShortcut);
  ASSERT_TRUE(analysis.ok);
  EXPECT_TRUE(analysis.conflicts.empty());
  EXPECT_FALSE(analysis.compatible_reuses.empty());
  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(edge->key, std::string(kShortcut))
                  .ok());

  CommandTestHarness reloaded;
  quader::QuaderEditorApi &loaded_editor = reloaded.editor;
  ASSERT_TRUE(loaded_editor.commands().set_keybinding_storage_path(path).ok());
  EXPECT_EQ(loaded_editor.commands().shortcut_for(kTextureCommand), kShortcut);
  EXPECT_EQ(loaded_editor.commands().shortcut_for(kEdgeCommand), kShortcut);
  const auto object =
      create_selected_box(loaded_editor, "Rebound Texture Command");
  ASSERT_TRUE(
      loaded_editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(loaded_editor.selection().select_face(object, 1).ok());
  const auto before = loaded_editor.modeling().face_texture_settings();
  ASSERT_TRUE(before.editable);
  ASSERT_TRUE(before.scale_x.has_value());

  quader::ViewportToolState state;
  state.selection_mode = quader::ViewportSelectionMode::Face;
  const auto chord = single_input_chord(kShortcut);
  const auto dispatch = loaded_editor.commands().dispatch_viewport_input_binding(
      chord, state, true, false);
  ASSERT_EQ(dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(dispatch.command_id, kTextureCommand.string());
  const auto result = loaded_editor.commands().execute(dispatch.command_id);
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_TRUE(result.changed());
  const auto after = loaded_editor.modeling().face_texture_settings();
  ASSERT_TRUE(after.scale_x.has_value());
  EXPECT_NEAR(*after.scale_x, *before.scale_x + 0.1, 1.0e-6);

  const auto expect_no_match =
      [&](bool focused, bool text_input,
          quader::QuaderViewportKeybindingContext context = {}) {
    const auto blocked =
        loaded_editor.commands().dispatch_viewport_input_binding(
            chord, state, focused, text_input, context);
    EXPECT_EQ(blocked.kind,
              quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  };
  expect_no_match(false, false);
  expect_no_match(true, true);
  expect_no_match(true, false, {.navigation_active = true});
  for (const auto mode :
       {quader::ViewportSelectionMode::Vertex,
        quader::ViewportSelectionMode::Object,
        quader::ViewportSelectionMode::Group}) {
    state.selection_mode = mode;
    expect_no_match(true, false);
  }

  ASSERT_TRUE(
      loaded_editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(loaded_editor.selection().select_edge(object, 1, 2).ok());
  state.selection_mode = quader::ViewportSelectionMode::Edge;
  const auto edge_dispatch =
      loaded_editor.commands().dispatch_viewport_input_binding(
          chord, state, true, false);
  ASSERT_EQ(edge_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(edge_dispatch.command_id, kEdgeCommand.string());

  ASSERT_TRUE(loaded_editor.preferences().reset_shortcut(kTextureCommand).ok());
  EXPECT_TRUE(loaded_editor.commands().shortcut_for(kTextureCommand).empty());
  EXPECT_EQ(loaded_editor.commands().shortcut_for(kEdgeCommand), kShortcut);
  std::filesystem::remove(path);
}

TEST(EditorApiTests, KeybindingConflictsAndRemovalSupportRebindingUi) {
  const std::filesystem::path path = keybinding_test_path("conflicts");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;

  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kEditUndo, "Ctrl+Alt+Z")
                  .ok());

  const std::vector<quader::QuaderKeybindingConflict> conflicts =
      editor.commands().conflicts_for_shortcut("Ctrl+Alt+Z");
  ASSERT_EQ(conflicts.size(), 1u);
  EXPECT_EQ(conflicts.front().target_id,
            quader::quader_command::kEditUndo.string());
  EXPECT_EQ(conflicts.front().source, "user");

  ASSERT_TRUE(editor.preferences()
                  .remove_shortcut(quader::quader_command::kEditUndo)
                  .ok());
  EXPECT_EQ(editor.commands().shortcut_for(quader::quader_command::kEditUndo),
            "");
  const std::string removed_undo_command =
      "\"command\": \"-" +
      std::string(quader::quader_command::kEditUndo.string()) + "\"";
  EXPECT_NE(read_text_file(path).find(removed_undo_command), std::string::npos);
}

TEST(EditorApiTests, ShortcutMutationsReportNoOpChangedStatus) {
  const std::filesystem::path path = keybinding_test_path("shortcut-noops");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;

  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  const quader::QuaderApiVoidResult same_default =
      editor.preferences().set_shortcut(quader::quader_command::kEditUndo,
                                        "Ctrl+Z");
  ASSERT_TRUE(same_default.ok()) << same_default.error_message();
  EXPECT_FALSE(same_default.changed());

  const quader::QuaderApiVoidResult custom = editor.preferences().set_shortcut(
      quader::quader_command::kEditUndo, "Ctrl+Alt+Z");
  ASSERT_TRUE(custom.ok()) << custom.error_message();
  EXPECT_TRUE(custom.changed());

  const quader::QuaderApiVoidResult same_custom =
      editor.preferences().set_shortcut(quader::quader_command::kEditUndo,
                                        "Ctrl+Alt+Z");
  ASSERT_TRUE(same_custom.ok()) << same_custom.error_message();
  EXPECT_FALSE(same_custom.changed());

  const quader::QuaderApiVoidResult removed =
      editor.preferences().remove_shortcut(quader::quader_command::kEditUndo);
  ASSERT_TRUE(removed.ok()) << removed.error_message();
  EXPECT_TRUE(removed.changed());

  const quader::QuaderApiVoidResult removed_again =
      editor.preferences().remove_shortcut(quader::quader_command::kEditUndo);
  ASSERT_TRUE(removed_again.ok()) << removed_again.error_message();
  EXPECT_FALSE(removed_again.changed());

  const quader::QuaderApiVoidResult reset =
      editor.preferences().reset_shortcut(quader::quader_command::kEditUndo);
  ASSERT_TRUE(reset.ok()) << reset.error_message();
  EXPECT_TRUE(reset.changed());

  const quader::QuaderApiVoidResult reset_again =
      editor.preferences().reset_shortcut(quader::quader_command::kEditUndo);
  ASSERT_TRUE(reset_again.ok()) << reset_again.error_message();
  EXPECT_FALSE(reset_again.changed());
}

TEST(EditorApiTests,
     ActionKeybindingsOwnAliasesSingleMappingPersistenceAndReset) {
  const std::filesystem::path path =
      keybinding_test_path("action-single-mapping");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  const auto initial_catalog = editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *initial =
      find_keybinding_catalog_entry(
          initial_catalog,
          quader::quader_command::kToolsApplyPrimitiveCreation.string());
  ASSERT_NE(initial, nullptr);
  ASSERT_GT(initial->target_ids.size(), 1u);
  EXPECT_EQ(initial->default_shortcut, "Space");
  const quader::QuaderKeybindingActionKey action = initial->key;

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(initial->target_ids.front(), "Ctrl+Alt+F18")
                  .ok());
  const auto grouped_catalog = editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *grouped =
      find_keybinding_catalog_entry(
          grouped_catalog,
          quader::quader_command::kToolsApplyPrimitiveCreation.string());
  ASSERT_NE(grouped, nullptr);
  EXPECT_EQ(grouped->shortcut, "Ctrl+Alt+F18");
  EXPECT_TRUE(grouped->customized);
  EXPECT_EQ(grouped->source, quader::QuaderKeybindingSourceKind::User);
  for (const std::string &target_id : grouped->target_ids) {
    EXPECT_TRUE(has_keybinding(editor.commands().keybindings(), target_id,
                               "Ctrl+Alt+F18"))
        << target_id;
  }

  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(action, "Space")
                  .ok());
  const auto default_catalog = editor.commands().keybinding_catalog();
  const auto *default_entry = find_keybinding_catalog_entry(
      default_catalog,
      quader::quader_command::kToolsApplyPrimitiveCreation.string());
  ASSERT_NE(default_entry, nullptr);
  EXPECT_FALSE(default_entry->customized);
  std::string default_file = read_text_file(path);
  default_file.erase(
      std::remove(default_file.begin(), default_file.end(), '\r'),
      default_file.end());
  EXPECT_EQ(default_file, "[\n]\n");

  const quader::QuaderApiVoidResult changed =
      editor.preferences().set_action_shortcut(
          action, "Ctrl+Enter",
          quader::QuaderKeybindingConflictPolicy::Reject);
  ASSERT_TRUE(changed.ok()) << changed.error_message();
  EXPECT_TRUE(changed.changed());

  CommandTestHarness reloaded_harness;
  quader::QuaderEditorApi &reloaded = reloaded_harness.editor;
  ASSERT_TRUE(reloaded.commands().set_keybinding_storage_path(path).ok());
  auto reloaded_catalog = reloaded.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *reloaded_entry =
      find_keybinding_catalog_entry(
          reloaded_catalog,
          quader::quader_command::kToolsApplyPrimitiveCreation.string());
  ASSERT_NE(reloaded_entry, nullptr);
  EXPECT_EQ(reloaded_entry->shortcut, "Ctrl+Enter");

  ASSERT_TRUE(reloaded.preferences()
                  .remove_shortcut(reloaded_entry->target_ids.front())
                  .ok());
  reloaded_catalog = reloaded.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *unassigned =
      find_keybinding_catalog_entry(
          reloaded_catalog,
          quader::quader_command::kToolsApplyPrimitiveCreation.string());
  ASSERT_NE(unassigned, nullptr);
  EXPECT_TRUE(unassigned->shortcut.empty());
  EXPECT_TRUE(unassigned->customized);
  EXPECT_TRUE(unassigned->unassigned);
  EXPECT_EQ(unassigned->source, quader::QuaderKeybindingSourceKind::User);

  ASSERT_TRUE(reloaded.preferences()
                  .reset_action_shortcut(
                      action, quader::QuaderKeybindingConflictPolicy::Reject)
                  .ok());
  reloaded_catalog = reloaded.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *reset =
      find_keybinding_catalog_entry(
          reloaded_catalog,
          quader::quader_command::kToolsApplyPrimitiveCreation.string());
  ASSERT_NE(reset, nullptr);
  EXPECT_EQ(reset->shortcut, "Space");
  EXPECT_FALSE(reset->customized);
  EXPECT_FALSE(reset->unassigned);
  EXPECT_EQ(reset->source, quader::QuaderKeybindingSourceKind::Default);

  ASSERT_TRUE(reloaded.preferences()
                  .set_action_shortcut(action, "Ctrl+Enter")
                  .ok());
  ASSERT_TRUE(reloaded.preferences().remove_action_shortcut(action).ok());
  const auto action_removed_catalog =
      reloaded.commands().keybinding_catalog();
  const auto *action_removed = find_keybinding_catalog_entry(
      action_removed_catalog,
      quader::quader_command::kToolsApplyPrimitiveCreation.string());
  ASSERT_NE(action_removed, nullptr);
  EXPECT_TRUE(action_removed->unassigned);
  ASSERT_TRUE(reloaded.preferences()
                  .set_action_shortcut(action, "Ctrl+Enter")
                  .ok());
  ASSERT_TRUE(reloaded.preferences().reset_all_shortcuts().ok());
  std::string reset_file = read_text_file(path);
  reset_file.erase(std::remove(reset_file.begin(), reset_file.end(), '\r'),
                   reset_file.end());
  EXPECT_EQ(reset_file, "[\n]\n");
}

TEST(EditorApiTests,
     ActionKeybindingAnalysisClassifiesReuseExactAndPrefixConflicts) {
  const std::filesystem::path path =
      keybinding_test_path("action-conflict-analysis");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  auto catalog = editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *undo =
      find_keybinding_catalog_entry(
          catalog, quader::quader_command::kEditUndo.string());
  const quader::QuaderKeybindingCatalogEntry *redo =
      find_keybinding_catalog_entry(
          catalog, quader::quader_command::kEditRedo.string());
  ASSERT_NE(undo, nullptr);
  ASSERT_NE(redo, nullptr);
  const quader::QuaderKeybindingActionKey undo_action = undo->key;
  const quader::QuaderKeybindingActionKey redo_action = redo->key;

  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(redo_action, "Ctrl+Alt+F23")
                  .ok());
  quader::QuaderKeybindingAssignmentAnalysis exact =
      editor.commands().analyze_shortcut_assignment(undo_action,
                                                    "Ctrl+Alt+F23");
  ASSERT_TRUE(exact.ok);
  ASSERT_EQ(exact.conflicts.size(), 1u);
  EXPECT_EQ(exact.conflicts.front().action, redo_action);
  EXPECT_EQ(exact.conflicts.front().kind,
            quader::QuaderKeybindingCollisionKind::ExactConflict);
  const quader::QuaderApiVoidResult rejected =
      editor.preferences().set_action_shortcut(
          undo_action, "Ctrl+Alt+F23",
          quader::QuaderKeybindingConflictPolicy::Reject);
  EXPECT_FALSE(rejected.ok());

  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(
                      undo_action, "Ctrl+Alt+F23",
                      quader::QuaderKeybindingConflictPolicy::Replace)
                  .ok());
  catalog = editor.commands().keybinding_catalog();
  redo = find_keybinding_catalog_entry(
      catalog, quader::quader_command::kEditRedo.string());
  ASSERT_NE(redo, nullptr);
  EXPECT_TRUE(redo->unassigned);

  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(redo_action, "Ctrl+K Ctrl+C")
                  .ok());
  const quader::QuaderKeybindingAssignmentAnalysis shorter =
      editor.commands().analyze_shortcut_assignment(undo_action, "Ctrl+K");
  ASSERT_TRUE(shorter.ok);
  ASSERT_FALSE(shorter.conflicts.empty());
  EXPECT_EQ(shorter.conflicts.front().kind,
            quader::QuaderKeybindingCollisionKind::PrefixConflict);
  const quader::QuaderKeybindingAssignmentAnalysis longer =
      editor.commands().analyze_shortcut_assignment(undo_action,
                                                    "Ctrl+K Ctrl+C D");
  ASSERT_TRUE(longer.ok);
  ASSERT_FALSE(longer.conflicts.empty());
  EXPECT_EQ(longer.conflicts.front().kind,
            quader::QuaderKeybindingCollisionKind::PrefixConflict);

  catalog = editor.commands().keybinding_catalog();
  const std::string move_target =
      editor.commands().command_id_for_tool_activation("move");
  const quader::QuaderKeybindingCatalogEntry *move =
      find_keybinding_catalog_entry(catalog, move_target);
  ASSERT_NE(move, nullptr);
  const quader::QuaderKeybindingAssignmentAnalysis compatible =
      editor.commands().analyze_shortcut_assignment(move->key, "W");
  ASSERT_TRUE(compatible.ok);
  EXPECT_TRUE(compatible.conflicts.empty());
  EXPECT_NE(std::find_if(compatible.compatible_reuses.begin(),
                         compatible.compatible_reuses.end(),
                         [](const auto &collision) {
                           return collision.target_id ==
                                  "viewport.navigation.fly_forward";
                         }),
            compatible.compatible_reuses.end());
}

TEST(EditorApiTests, ActionKeybindingReplaceUnassignsEveryCollision) {
  const std::filesystem::path path =
      keybinding_test_path("action-replace-many");
  {
    std::ofstream file(path);
    file << "[\n"
         << "  {\"command\":\""
         << quader::quader_command::kEditRedo.string()
         << "\",\"key\":\"Ctrl+Alt+F19\",\"when\":\"true\"},\n"
         << "  {\"command\":\""
         << quader::quader_command::kEditRepeatLastAction.string()
         << "\",\"key\":\"Ctrl+Alt+F19\",\"when\":\"true\"}\n"
         << "]\n";
  }
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  const auto catalog = editor.commands().keybinding_catalog();
  const auto *undo = find_keybinding_catalog_entry(
      catalog, quader::quader_command::kEditUndo.string());
  ASSERT_NE(undo, nullptr);
  const quader::QuaderKeybindingAssignmentAnalysis analysis =
      editor.commands().analyze_shortcut_assignment(undo->key,
                                                    "Ctrl+Alt+F19");
  ASSERT_TRUE(analysis.ok);
  EXPECT_EQ(analysis.conflicts.size(), 2u);

  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(
                      undo->key, "Ctrl+Alt+F19",
                      quader::QuaderKeybindingConflictPolicy::Replace)
                  .ok());
  const auto replaced = editor.commands().keybinding_catalog();
  const auto *redo = find_keybinding_catalog_entry(
      replaced, quader::quader_command::kEditRedo.string());
  const auto *repeat = find_keybinding_catalog_entry(
      replaced, quader::quader_command::kEditRepeatLastAction.string());
  ASSERT_NE(redo, nullptr);
  ASSERT_NE(repeat, nullptr);
  EXPECT_TRUE(redo->unassigned);
  EXPECT_TRUE(repeat->unassigned);
}

TEST(EditorApiTests, ActionKeybindingWriteFailureDoesNotChangeLiveResolver) {
  const std::filesystem::path blocker =
      keybinding_test_path("action-write-blocker");
  {
    std::ofstream file(blocker);
    file << "not a directory";
  }
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(blocker / "keybindings.json")
                  .ok());
  const auto catalog = editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *undo =
      find_keybinding_catalog_entry(
          catalog, quader::quader_command::kEditUndo.string());
  ASSERT_NE(undo, nullptr);
  const std::string before = undo->shortcut;

  const quader::QuaderApiVoidResult failed =
      editor.preferences().set_action_shortcut(undo->key, "Ctrl+Alt+F20");
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  const auto after_catalog = editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *after =
      find_keybinding_catalog_entry(
          after_catalog,
          quader::quader_command::kEditUndo.string());
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->shortcut, before);
  std::error_code ignored;
  std::filesystem::remove(blocker, ignored);
}

TEST(EditorApiTests,
     ActionKeybindingLateCommitFailurePreservesLiveAndDiskState) {
#ifndef _WIN32
  GTEST_SKIP() << "The deterministic replacement lock uses Windows sharing.";
#else
  const std::filesystem::path path =
      keybinding_test_path("action-late-commit-failure");
  {
    std::ofstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    file << "[]\n";
  }

  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  const auto catalog = editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *undo =
      find_keybinding_catalog_entry(
          catalog, quader::quader_command::kEditUndo.string());
  ASSERT_NE(undo, nullptr);
  const std::string before_shortcut = undo->shortcut;
  const std::string before_disk = read_text_file(path);

  const HANDLE replacement_guard =
      ::CreateFileW(path.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(replacement_guard, INVALID_HANDLE_VALUE);

  const quader::QuaderApiVoidResult failed =
      editor.preferences().set_action_shortcut(undo->key, "Ctrl+Alt+F20");
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_NE(failed.error_message().find("replace"), std::string::npos);

  const auto after_catalog = editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *after =
      find_keybinding_catalog_entry(
          after_catalog, quader::quader_command::kEditUndo.string());
  EXPECT_NE(after, nullptr);
  if (after != nullptr) {
    EXPECT_EQ(after->shortcut, before_shortcut);
  }
  EXPECT_EQ(read_text_file(path), before_disk);

  std::vector<std::filesystem::path> temporary_files;
  const std::string temporary_prefix = path.filename().string() + ".tmp.";
  const std::string transaction_prefix =
      "." + path.filename().string() + ".";
  for (const std::filesystem::directory_entry &entry :
       std::filesystem::directory_iterator(path.parent_path())) {
    const std::string filename = entry.path().filename().string();
    if (filename.rfind(temporary_prefix, 0u) == 0u ||
        filename.rfind(transaction_prefix, 0u) == 0u) {
      temporary_files.push_back(entry.path());
    }
  }
  EXPECT_TRUE(temporary_files.empty());

  EXPECT_NE(::CloseHandle(replacement_guard), 0);
  std::error_code ignored;
  for (const std::filesystem::path &temporary : temporary_files) {
    std::filesystem::remove(temporary, ignored);
  }
  std::filesystem::remove(path, ignored);
#endif
}

TEST(EditorApiTests,
     LegacyActionMappingsUseLatestEntryAndCurrentAppOwnedContext) {
  const std::filesystem::path path =
      keybinding_test_path("action-legacy-latest-wins");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  const auto initial_catalog = editor.commands().keybinding_catalog();
  const auto *initial = find_keybinding_catalog_entry(
      initial_catalog,
      quader::quader_command::kToolsApplyPrimitiveCreation.string());
  ASSERT_NE(initial, nullptr);
  ASSERT_GT(initial->target_ids.size(), 1u);
  const quader::QuaderKeybindingActionKey action = initial->key;
  const std::vector<std::string> targets = initial->target_ids;
  const std::string canonical_when = initial->when;

  {
    std::ofstream file(path);
    file << "[\n"
         << "  {\"command\":\"" << targets.front()
         << "\",\"key\":\"Ctrl+Alt+F20\",\"when\":\"true\"},\n"
         << "  {\"command\":\"" << targets.back()
         << "\",\"key\":\"Ctrl+Alt+F21\",\"when\":\"true\"}\n"
         << "]\n";
  }
  const std::string legacy_file = read_text_file(path);

  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  EXPECT_EQ(read_text_file(path), legacy_file);
  const auto loaded_catalog = editor.commands().keybinding_catalog();
  const auto *loaded = find_keybinding_catalog_entry(
      loaded_catalog,
      quader::quader_command::kToolsApplyPrimitiveCreation.string());
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->shortcut, "Ctrl+Alt+F21");
  EXPECT_EQ(loaded->when, canonical_when);
  for (const std::string &target_id : targets) {
    EXPECT_TRUE(has_keybinding(editor.commands().keybindings(), target_id,
                               "Ctrl+Alt+F21"))
        << target_id;
    EXPECT_FALSE(has_keybinding_when(editor.commands().keybindings(), target_id,
                                     "Ctrl+Alt+F21", "true"))
        << target_id;
  }

  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(action, "Ctrl+Alt+F22")
                  .ok());
  const std::string canonical_file = read_text_file(path);
  EXPECT_EQ(canonical_file.find("Ctrl+Alt+F20"), std::string::npos);
  EXPECT_EQ(canonical_file.find("Ctrl+Alt+F21"), std::string::npos);
  EXPECT_NE(canonical_file.find("Ctrl+Alt+F22"), std::string::npos);
}

TEST(EditorApiTests,
     ScopedModelingMappingsReuseInputAcrossExclusiveSelectionModes) {
  const std::filesystem::path path =
      keybinding_test_path("selection-mode-scopes");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  const quader::QuaderModelingObjectRef object =
      create_selected_box(editor, "Scoped Keybinding Box");
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.selection().select_vertex(object, 1).ok());
  ASSERT_TRUE(editor.selection()
                  .select_vertex(object, 2, quader::QuaderSelectionEdit::Add)
                  .ok());

  auto catalog = editor.commands().keybinding_catalog();
  const std::string merge_vertices_target =
      editor.commands().command_id_for_modeling_action("merge_vertices");
  const std::string merge_edges_target =
      editor.commands().command_id_for_modeling_action("merge_edges");
  const auto *merge_vertices =
      find_keybinding_catalog_entry(catalog, merge_vertices_target);
  const auto *merge_edges =
      find_keybinding_catalog_entry(catalog, merge_edges_target);
  ASSERT_NE(merge_vertices, nullptr);
  ASSERT_NE(merge_edges, nullptr);
  EXPECT_EQ(merge_vertices->scope_id, "viewport.selection.vertex");
  EXPECT_EQ(merge_edges->scope_id, "viewport.selection.edge");
  EXPECT_EQ(merge_vertices->shortcut, "M");

  const quader::QuaderKeybindingAssignmentAnalysis compatible =
      editor.commands().analyze_shortcut_assignment(merge_edges->key, "M");
  ASSERT_TRUE(compatible.ok);
  EXPECT_TRUE(compatible.conflicts.empty());
  EXPECT_FALSE(compatible.compatible_reuses.empty());
  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(merge_edges->key, "M")
                  .ok());

  catalog = editor.commands().keybinding_catalog();
  merge_vertices =
      find_keybinding_catalog_entry(catalog, merge_vertices_target);
  merge_edges = find_keybinding_catalog_entry(catalog, merge_edges_target);
  ASSERT_NE(merge_vertices, nullptr);
  ASSERT_NE(merge_edges, nullptr);

  const quader::QuaderSelectionSummary vertex_selection =
      editor.query().selection();
  EXPECT_EQ(vertex_selection.mode, quader::QuaderSelectionMode::Vertex);
  EXPECT_GE(vertex_selection.selected_vertex_count, 2u);
  EXPECT_NE(vertex_selection.active_vertex_id,
            quader::kInvalidQuaderElementId);
  for (const std::string &target_id : merge_vertices->target_ids) {
    const std::optional<quader::QuaderCommandDescriptor> descriptor =
        editor.commands().descriptor(target_id);
    if (descriptor.has_value()) {
      EXPECT_TRUE(descriptor->enabled) << target_id;
    }
  }

  quader::ViewportToolState state;
  state.selection_mode = quader::ViewportSelectionMode::Vertex;
  quader::QuaderViewportKeybindingDispatch dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("M"), state, true, false);
  ASSERT_EQ(dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_NE(std::find(merge_vertices->target_ids.begin(),
                      merge_vertices->target_ids.end(), dispatch.command_id),
            merge_vertices->target_ids.end());

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(object, 1, 2).ok());
  ASSERT_TRUE(editor.selection()
                  .select_edge(object, 2, 3, quader::QuaderSelectionEdit::Add)
                  .ok());
  state.selection_mode = quader::ViewportSelectionMode::Edge;
  dispatch = editor.commands().dispatch_viewport_input_binding(
      single_input_chord("M"), state, true, false);
  ASSERT_EQ(dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_NE(std::find(merge_edges->target_ids.begin(),
                      merge_edges->target_ids.end(), dispatch.command_id),
            merge_edges->target_ids.end());

  const auto *radial_align = find_keybinding_catalog_entry(
      catalog, editor.commands().command_id_for_modeling_action("radial_align"));
  ASSERT_NE(radial_align, nullptr);
  EXPECT_EQ(radial_align->scope_id, "viewport.selection.component");
}

TEST(EditorApiTests, DisabledKeybindingCandidateFallsThroughOrPassesInput) {
  const std::filesystem::path fallback_path =
      keybinding_test_path("availability-fallback");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  create_selected_box(editor, "Availability Fallback Box");
  {
    std::ofstream file(fallback_path);
    file << "[\n"
         << "  {\"command\":\""
         << quader::quader_command::kEditUndo.string()
         << "\",\"key\":\"Ctrl+Alt+F24\"},\n"
         << "  {\"command\":\""
         << quader::quader_command::kEditRedo.string()
         << "\",\"key\":\"Ctrl+Alt+F24\"}\n"
         << "]\n";
  }
  ASSERT_TRUE(
      editor.commands().set_keybinding_storage_path(fallback_path).ok());
  quader::ViewportToolState state;
  const quader::QuaderViewportKeybindingDispatch fallback =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Alt+F24"), state, false, false);
  ASSERT_EQ(fallback.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(fallback.command_id,
            quader::quader_command::kEditUndo.string());

  const std::filesystem::path unavailable_path =
      keybinding_test_path("availability-pass-through");
  {
    std::ofstream file(unavailable_path);
    file << "[{\"command\":\""
         << quader::quader_command::kEditCreateInstance.string()
         << "\",\"key\":\"Ctrl+Alt+F23\"}]\n";
  }
  ASSERT_TRUE(
      editor.commands().set_keybinding_storage_path(unavailable_path).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  const quader::QuaderViewportKeybindingDispatch no_match =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Alt+F23"), state, false, false);
  EXPECT_EQ(no_match.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
}

TEST(EditorApiTests, KeybindingConflictsUseSemanticWhenOverlap) {
    quader::QuaderKeybindingService service;
    service.register_target("navigation.any");
    service.register_target("navigation.fly");
    service.register_target("tool.move");
    service.register_target("tool.box");

    ASSERT_TRUE(service.add_default_keybinding(
        "navigation.any", "F", "viewportFocus && navigation.active"));
    ASSERT_TRUE(service.add_default_keybinding(
        "navigation.fly", "F", "viewportFocus && navigation.flyActive"));
    ASSERT_TRUE(
        service.add_default_keybinding("tool.move", "G", "activeTool.move"));
    ASSERT_TRUE(
        service.add_default_keybinding("tool.box", "G", "activeTool.box"));

    const std::vector<quader::QuaderKeybindingConflict> fly_conflicts =
        service.conflicts_for("F", "viewportFocus && navigation.flyActive");
    EXPECT_NE(std::find_if(fly_conflicts.begin(), fly_conflicts.end(),
                           [](const auto &conflict) {
                             return conflict.target_id == "navigation.any";
                           }),
              fly_conflicts.end());
    EXPECT_NE(std::find_if(fly_conflicts.begin(), fly_conflicts.end(),
                           [](const auto &conflict) {
                             return conflict.target_id == "navigation.fly";
                           }),
              fly_conflicts.end());

    const std::vector<quader::QuaderKeybindingConflict> move_conflicts =
        service.conflicts_for("G", "activeTool.move");
    ASSERT_EQ(move_conflicts.size(), 1u);
    EXPECT_EQ(move_conflicts.front().target_id, "tool.move");
}

TEST(EditorApiTests, PersistedKeybindingsRejectUnknownTargets) {
  const std::filesystem::path unknown_path =
      keybinding_test_path("unknown-target");
  {
    std::ofstream file(unknown_path);
    file << "[{\"command\":\"missing.command\",\"key\":\"Ctrl+M\"}]\n";
  }

    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderApiVoidResult loaded =
        editor.commands().set_keybinding_storage_path(unknown_path);
    EXPECT_FALSE(loaded.ok());
    EXPECT_NE(loaded.error_message().find("unknown command id"),
              std::string::npos);

    const std::filesystem::path unknown_removal_path =
        keybinding_test_path("unknown-removal-target");
    {
      std::ofstream file(unknown_removal_path);
      file << "[{\"command\":\"-missing.command\"}]\n";
    }

    CommandTestHarness removal_harness;
    quader::QuaderEditorApi &removal_editor = removal_harness.editor;
    const quader::QuaderApiVoidResult loaded_removal =
        removal_editor.commands().set_keybinding_storage_path(
            unknown_removal_path);
    EXPECT_FALSE(loaded_removal.ok());
    EXPECT_NE(loaded_removal.error_message().find("unknown command id"),
              std::string::npos);
}

TEST(EditorApiTests, ViewportKeybindingsResolveContextAndUserOverrides) {
  const std::filesystem::path path = keybinding_test_path("viewport");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  create_selected_box(editor, "Undo Shortcut Box");

  quader::ViewportToolState state;
  quader::ViewportShortcut shortcut;
  const std::string face_selection_target =
      editor.commands().keybinding_target_id_for_selection_mode(
          quader::QuaderSelectionMode::Face);
  EXPECT_EQ(face_selection_target,
            quader::quader_viewport_selection_mode_target_id("face"));
  EXPECT_EQ(editor.commands().shortcut_for(face_selection_target), "3");
  ASSERT_TRUE(editor.commands().resolve_viewport_shortcut(
      quader::ViewportKey::Number3, {}, state, true, false, shortcut));
  EXPECT_EQ(shortcut.kind, quader::ViewportShortcutKind::SetSelectionMode);
  EXPECT_EQ(shortcut.selection_mode, quader::ViewportSelectionMode::Face);

  ASSERT_TRUE(
      editor.preferences()
          .set_shortcut(face_selection_target, "Ctrl+Alt+3")
          .ok());
  EXPECT_EQ(editor.commands().shortcut_for(face_selection_target),
            "Ctrl+Alt+3");
  shortcut = {};
  EXPECT_FALSE(editor.commands().resolve_viewport_shortcut(
      quader::ViewportKey::Number3, {}, state, true, false, shortcut));
  ASSERT_TRUE(editor.commands().resolve_viewport_shortcut(
      quader::ViewportKey::Number3, {.primary = true, .alt = true}, state, true,
      false, shortcut));
  EXPECT_EQ(shortcut.kind, quader::ViewportShortcutKind::SetSelectionMode);
  EXPECT_EQ(shortcut.selection_mode, quader::ViewportSelectionMode::Face);
  ASSERT_TRUE(editor.preferences().reset_shortcut(face_selection_target).ok());
  EXPECT_EQ(editor.commands().shortcut_for(face_selection_target), "3");

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kEditUndo, "Ctrl+U")
                  .ok());
  const quader::QuaderViewportKeybindingDispatch dispatch =
      editor.commands().dispatch_viewport_keybinding(
          quader::ViewportKey::U, {.primary = true}, state, true, false);
  EXPECT_EQ(dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(dispatch.command_id, quader::quader_command::kEditUndo.string());

  shortcut = {};
  EXPECT_FALSE(editor.commands().resolve_viewport_shortcut(
      quader::ViewportKey::Number3, {.primary = true}, state, true, true,
      shortcut));
}

TEST(EditorApiTests, GlobalCommandKeybindingsResolveWithAndWithoutViewportFocus) {
  const std::filesystem::path path =
      keybinding_test_path("global-command-shortcuts");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  create_selected_box(editor, "Global Undo Shortcut Box");

  quader::ViewportToolState state;
  const quader::QuaderViewportKeybindingDispatch new_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+N"), state, false, false);
  ASSERT_EQ(new_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(new_dispatch.command_id,
            quader::quader_command::kSceneDocumentNew.string());

  const quader::QuaderViewportKeybindingDispatch open_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+O"), state, false, false);
  ASSERT_EQ(open_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(open_dispatch.command_id,
            quader::quader_command::kSceneDocumentOpen.string());

  const quader::QuaderViewportKeybindingDispatch save_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+S"), state, false, false);
  ASSERT_EQ(save_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(save_dispatch.command_id,
            quader::quader_command::kSceneDocumentSave.string());

  const quader::QuaderViewportKeybindingDispatch save_as_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Alt+S"), state, false, false);
  ASSERT_EQ(save_as_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(save_as_dispatch.command_id,
            quader::quader_command::kSceneDocumentSaveAs.string());

  const quader::QuaderViewportKeybindingDispatch save_all_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Shift+S"), state, false, false);
  ASSERT_EQ(save_all_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(save_all_dispatch.command_id,
            quader::quader_command::kSceneDocumentSaveAll.string());

  const quader::QuaderViewportKeybindingDispatch close_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+W"), state, false, false);
  ASSERT_EQ(close_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(close_dispatch.command_id,
            quader::quader_command::kSceneDocumentClose.string());

  const quader::QuaderViewportKeybindingDispatch next_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Tab"), state, false, false);
  EXPECT_EQ(next_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);

  const quader::QuaderViewportKeybindingDispatch previous_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Shift+Tab"), state, false, false);
  EXPECT_EQ(previous_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);

  const quader::QuaderViewportKeybindingDispatch focused_copy_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+C"), state, true, false);
  ASSERT_EQ(focused_copy_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(focused_copy_dispatch.command_id,
            quader::quader_command::kCopySelection.string());
  EXPECT_NE(focused_copy_dispatch.command_id,
            quader::quader_command::kViewportFrameSelection.string());

  const quader::QuaderViewportKeybindingDispatch focused_cut_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+X"), state, true, false);
  ASSERT_EQ(focused_cut_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(focused_cut_dispatch.command_id,
            quader::quader_command::kCutSelection.string());

#ifndef NDEBUG
  const quader::QuaderViewportKeybindingDispatch dev_copy_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Shift+C"), state, false, false);
  ASSERT_EQ(dev_copy_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(dev_copy_dispatch.command_id,
            quader::quader_command::kDevCopySelectionJson.string());

  const quader::QuaderViewportKeybindingDispatch focused_dev_copy_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Shift+C"), state, true, false);
  ASSERT_EQ(focused_dev_copy_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(focused_dev_copy_dispatch.command_id,
            quader::quader_command::kDevCopySelectionJson.string());
#endif

  const quader::QuaderViewportKeybindingDispatch undo_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Z"), state, false, false);
  ASSERT_EQ(undo_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(undo_dispatch.command_id,
            quader::quader_command::kEditUndo.string());

  ASSERT_TRUE(
      editor.commands().execute(quader::quader_command::kEditUndo).ok());

  const quader::QuaderViewportKeybindingDispatch removed_redo_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Y"), state, false, false);
  EXPECT_EQ(removed_redo_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);

  const quader::QuaderViewportKeybindingDispatch shift_redo_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Shift+Z"), state, false, false);
  ASSERT_EQ(shift_redo_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(shift_redo_dispatch.command_id,
            quader::quader_command::kEditRedo.string());

  ASSERT_TRUE(
      editor.commands().execute(quader::quader_command::kEditRedo).ok());

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kEditUndo, "Ctrl+Alt+Z")
                  .ok());
  const quader::QuaderViewportKeybindingDispatch rebound_undo_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Alt+Z"), state, false, false);
  ASSERT_EQ(rebound_undo_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(rebound_undo_dispatch.command_id,
            quader::quader_command::kEditUndo.string());
}

#ifndef NDEBUG
TEST(EditorApiTests,
     DebugCopySelectionJsonShortcutRejectsConflictingFocusSelectionBind) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(
                      keybinding_test_path(
                          "debug-copy-selection-json-reserved-shortcut"))
                  .ok());

  const quader::QuaderApiVoidResult rejected =
      editor.preferences().set_shortcut(
          quader::quader_command::kViewportFrameSelection, "Ctrl+Shift+C");
  EXPECT_FALSE(rejected.ok());
  EXPECT_TRUE(editor.commands()
                  .shortcut_for(
                      quader::quader_command::kViewportFrameSelection)
                  .empty());

  quader::ViewportToolState state;
  const quader::QuaderViewportKeybindingDispatch dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Shift+C"), state, true, false);
  ASSERT_EQ(dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(dispatch.command_id,
            quader::quader_command::kDevCopySelectionJson.string());
  EXPECT_NE(dispatch.command_id,
            quader::quader_command::kViewportFrameSelection.string());
}
#endif

TEST(EditorApiTests, FocusSelectionUserShortcutUsesViewportContext) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(
                      keybinding_test_path("focus-selection-shortcut-context"))
                  .ok());

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kViewportFrameSelection,
                                "Ctrl+Alt+F")
                  .ok());

  quader::ViewportToolState state;
  const quader::QuaderViewportKeybindingDispatch without_viewport_focus =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Alt+F"), state, false, false);
  EXPECT_EQ(without_viewport_focus.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);

  const quader::QuaderViewportKeybindingDispatch with_viewport_focus =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Alt+F"), state, true, false);
  ASSERT_EQ(with_viewport_focus.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(with_viewport_focus.command_id,
            quader::quader_command::kViewportFrameSelection.string());
}

#ifndef NDEBUG
TEST(EditorApiTests, DebugCopySelectionJsonCopiesEdgeSelectionAndReportsStatus) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(
                      keybinding_test_path("debug-copy-selection-json"))
                  .ok());

  std::string copied_text;
  editor.dev_tools().set_runtime_hooks(quader::QuaderDevToolsRuntimeHooks{
      .set_clipboard_text =
          [&copied_text](std::string_view text, std::string &error_message) {
            copied_text = std::string(text);
            error_message.clear();
            return true;
          },
  });

  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Dev Copy Edge Box");
  ASSERT_TRUE(editor.selection().select_edge(box, 1, 2).ok());
  const quader::QuaderSelectionSummary selected =
      editor.selection().summary();
  ASSERT_EQ(selected.mode, quader::QuaderSelectionMode::Edge);
  ASSERT_EQ(selected.selected_edge_count, 1u);

  const quader::QuaderApiVoidResult copied =
      editor.commands().execute(quader::quader_command::kDevCopySelectionJson);
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  EXPECT_FALSE(copied.changed());
  EXPECT_EQ(copied.error_message(), "Copied selection JSON to clipboard.");
  ASSERT_TRUE(editor.commands().last_status().has_value());
  expect_same_api_status(*editor.commands().last_status(), copied);
  ASSERT_FALSE(copied_text.empty());

  const nlohmann::ordered_json root = nlohmann::ordered_json::parse(copied_text);
  EXPECT_EQ(root.at("kind"), "quader.dev.selection");
  EXPECT_EQ(root.at("version"), 1);

  const nlohmann::ordered_json &selection = root.at("selection");
  EXPECT_EQ(selection.at("mode"), "edge");
  EXPECT_TRUE(selection.at("hasSelection").get<bool>());
  EXPECT_EQ(selection.at("active").at("kind"), "edge");
  EXPECT_EQ(selection.at("active").at("modelingObjectId"),
            box.modeling_object_id);
  EXPECT_EQ(selection.at("counts").at("objects"), 1);
  EXPECT_EQ(selection.at("counts").at("edges"), 1);
  ASSERT_EQ(selection.at("objects").size(), 1u);

  const nlohmann::ordered_json &object = selection.at("objects").at(0);
  EXPECT_EQ(object.at("modelingObjectId"), box.modeling_object_id);
  EXPECT_EQ(object.at("entity"), box.entity);
  EXPECT_EQ(object.at("name"), "Dev Copy Edge Box");
  EXPECT_TRUE(object.at("selected").get<bool>());
  ASSERT_EQ(object.at("selection").at("edges").size(), 1u);
  EXPECT_EQ(object.at("selection").at("edges").at(0).at("a"), 1);
  EXPECT_EQ(object.at("selection").at("edges").at(0).at("b"), 2);
  ASSERT_EQ(object.at("selectedGeometry").at("edges").size(), 1u);
  const nlohmann::ordered_json &edge =
      object.at("selectedGeometry").at("edges").at(0);
  EXPECT_EQ(edge.at("a"), 1);
  EXPECT_EQ(edge.at("b"), 2);
  EXPECT_TRUE(edge.at("aPosition").contains("x"));
  EXPECT_TRUE(edge.at("bPosition").contains("x"));
}
#endif

TEST(EditorApiTests, GlobalCommandKeybindingsDoNotStealTextInputOrViewportOnlyShortcuts) {
  const std::filesystem::path path =
      keybinding_test_path("global-command-context");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  quader::ViewportToolState state;
  const quader::QuaderViewportKeybindingDispatch unavailable_undo =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Z"), state, false, false);
  EXPECT_EQ(unavailable_undo.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);

  const quader::QuaderViewportKeybindingDispatch text_input_undo =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Z"), state, false, true);
  EXPECT_EQ(text_input_undo.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);

  create_selected_box(editor, "Pivot Shortcut Box");

  const quader::QuaderViewportKeybindingDispatch pivot_without_viewport_focus =
          editor.commands().dispatch_viewport_input_binding(
              single_input_chord("P"), state, false, false);
  EXPECT_EQ(pivot_without_viewport_focus.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);

  const quader::QuaderViewportKeybindingDispatch pivot_with_viewport_focus =
          editor.commands().dispatch_viewport_input_binding(
              single_input_chord("P"), state, true, false);
  ASSERT_EQ(pivot_with_viewport_focus.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
  EXPECT_EQ(pivot_with_viewport_focus.shortcut.kind,
            quader::ViewportShortcutKind::ActivateTool);
  EXPECT_EQ(pivot_with_viewport_focus.shortcut.tool,
            quader::ViewportToolId::Pivot);
}

TEST(EditorApiTests,
     PrimitiveToolShortcutsResolveWithoutSameContextConflicts) {
  const std::filesystem::path path =
      keybinding_test_path("primitive-tool-shortcuts");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  EXPECT_EQ(editor.commands().shortcut_for(
                quader::quader_command::kToolsActivateCreatePrimitive),
            "Shift+B");
  const quader::QuaderViewportKeybindingDispatch dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Shift+B"), {}, true, false);
  ASSERT_EQ(
      dispatch.kind,
      quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
  EXPECT_EQ(dispatch.shortcut.kind,
            quader::ViewportShortcutKind::ActivateTool);
  EXPECT_EQ(dispatch.shortcut.tool,
            quader::ViewportToolId::CreatePrimitive);

  EXPECT_EQ(editor.commands()
                .dispatch_viewport_input_binding(
                    single_input_chord("Shift+C"), {}, true, false)
                .kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  EXPECT_EQ(editor.commands()
                .dispatch_viewport_input_binding(
                    single_input_chord("Shift+S"), {}, true, false)
                .kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);

  EXPECT_TRUE(editor.commands()
                  .shortcut_for(quader::quader_command::kToolsActivateCut)
                  .empty());
  const std::vector<quader::QuaderKeybindingConflict> cylinder_conflicts =
      editor.commands().conflicts_for_shortcut(
          "Shift+C",
          "viewportFocus && !textInputActive && !navigation.active");
  EXPECT_TRUE(cylinder_conflicts.empty());
}

TEST(EditorApiTests,
     CreatePrimitiveExposesDynamicSettingsAndRemainsActiveUntilCancel) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const auto control = [](const quader::QuaderToolSettingsTarget &target,
                          std::string_view id) {
    return std::find_if(target.controls.begin(), target.controls.end(),
                        [id](const auto &item) { return item.id == id; });
  };

  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  EXPECT_EQ(editor.tools().active_tool(), "create_primitive");
  const quader::ViewportToolState box_tool_state{
      .active_tool = quader::ViewportToolId::CreatePrimitive,
  };
  const quader::QuaderViewportKeybindingDispatch space_without_focus =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Space"), box_tool_state, false, false);
  EXPECT_EQ(space_without_focus.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  const quader::QuaderViewportKeybindingDispatch removed_enter_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Enter"), box_tool_state, true, false);
  EXPECT_EQ(removed_enter_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  const quader::QuaderViewportKeybindingDispatch space_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Space"), box_tool_state, true, false);
  ASSERT_EQ(space_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(space_dispatch.command_id,
            quader::quader_command::kToolsApplyPrimitiveCreation.string());
  const quader::QuaderViewportKeybindingDispatch escape_without_focus =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Escape"), box_tool_state, false, false);
  EXPECT_EQ(escape_without_focus.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  const quader::QuaderViewportKeybindingDispatch escape_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Escape"), box_tool_state, true, false);
  ASSERT_EQ(escape_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(escape_dispatch.command_id,
            quader::quader_command::kToolsCancelPrimitiveCreation.string());
  quader::QuaderToolSettingsTarget settings = editor.tools().active_settings();
  EXPECT_TRUE(settings.active);
  EXPECT_EQ(settings.description,
            "Draw a footprint, set its height, and refine the primitive before "
            "applying it.");
  EXPECT_TRUE(settings.can_reset);
  EXPECT_TRUE(settings.apply_visible);
  EXPECT_FALSE(settings.can_apply);
  EXPECT_TRUE(settings.can_cancel);
  const auto primitive_type = control(settings, "primitive.type");
  ASSERT_NE(primitive_type, settings.controls.end());
  ASSERT_EQ(primitive_type->choices.size(), 5U);
  EXPECT_EQ(primitive_type->choices[0].id, "box");
  EXPECT_EQ(primitive_type->choices[1].id, "cylinder");
  EXPECT_EQ(primitive_type->choices[2].id, "sphere");
  EXPECT_EQ(primitive_type->choices[3].id, "cone");
  EXPECT_EQ(primitive_type->choices[4].id, "torus");
  EXPECT_DOUBLE_EQ(primitive_type->choices[0].value, 0.0);
  EXPECT_DOUBLE_EQ(primitive_type->choices[1].value, 1.0);
  EXPECT_DOUBLE_EQ(primitive_type->choices[2].value, 2.0);
  EXPECT_DOUBLE_EQ(primitive_type->choices[3].value, 3.0);
  EXPECT_DOUBLE_EQ(primitive_type->choices[4].value, 4.0);
  EXPECT_EQ(editor.tools().active_primitive_default_name(), "Box");
  ASSERT_NE(control(settings, "primitive.box.subdivisions_x"),
            settings.controls.end());
  ASSERT_NE(control(settings, "primitive.box.subdivisions_y"),
            settings.controls.end());
  ASSERT_NE(control(settings, "primitive.box.subdivisions_z"),
            settings.controls.end());
  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive",
                               "primitive.box.subdivisions_x", 4.0)
                  .ok());
  EXPECT_FALSE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_EQ(editor.tools().active_tool(), "create_primitive");

  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive", "primitive.type",
                               "cylinder")
                  .ok());
  EXPECT_EQ(editor.tools().active_tool(), "create_primitive");
  EXPECT_EQ(editor.query().tool_status().active_tool,
            quader::QuaderViewportTool::CreatePrimitive);
  settings = editor.tools().active_settings();
  ASSERT_NE(control(settings, "primitive.cylinder.side_count"),
            settings.controls.end());
  ASSERT_NE(control(settings, "primitive.cylinder.caps"),
            settings.controls.end());
  ASSERT_NE(control(settings, "primitive.flip_faces"),
            settings.controls.end());
  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive",
                               "primitive.cylinder.side_count", 12.0)
                  .ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive",
                               "primitive.cylinder.caps", false)
                  .ok());

  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive", "primitive.type", "sphere")
                  .ok());
  EXPECT_EQ(editor.tools().active_tool(), "create_primitive");
  EXPECT_EQ(editor.query().tool_status().active_tool,
            quader::QuaderViewportTool::CreatePrimitive);
  settings = editor.tools().active_settings();
  ASSERT_NE(control(settings, "primitive.sphere.radial_segments"),
            settings.controls.end());
  ASSERT_NE(control(settings, "primitive.sphere.rings"),
            settings.controls.end());
  ASSERT_NE(control(settings, "primitive.sphere.hemisphere"),
            settings.controls.end());
  ASSERT_NE(control(settings, "primitive.flip_faces"),
            settings.controls.end());

  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive", "primitive.type", "cone")
                  .ok());
  EXPECT_EQ(editor.tools().active_primitive_type(), "cone");
  EXPECT_EQ(editor.tools().active_primitive_default_name(), "Cone");
  settings = editor.tools().active_settings();
  ASSERT_NE(control(settings, "primitive.cone.side_count"),
            settings.controls.end());
  ASSERT_NE(control(settings, "primitive.cone.caps"),
            settings.controls.end());
  ASSERT_NE(control(settings, "primitive.flip_faces"),
            settings.controls.end());
  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive",
                               "primitive.cone.side_count", 7.0)
                  .ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive", "primitive.cone.caps",
                               false)
                  .ok());

  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive", "primitive.type", "torus")
                  .ok());
  EXPECT_EQ(editor.tools().active_primitive_type(), "torus");
  EXPECT_EQ(editor.tools().active_primitive_default_name(), "Torus");
  settings = editor.tools().active_settings();
  ASSERT_NE(control(settings, "primitive.torus.major_segments"),
            settings.controls.end());
  ASSERT_NE(control(settings, "primitive.torus.minor_segments"),
            settings.controls.end());
  const auto tube_ratio =
      control(settings, "primitive.torus.tube_radius_ratio");
  ASSERT_NE(tube_ratio, settings.controls.end());
  EXPECT_DOUBLE_EQ(tube_ratio->number_value, 0.2);
  EXPECT_DOUBLE_EQ(tube_ratio->min_value, 0.01);
  EXPECT_DOUBLE_EQ(tube_ratio->max_value, 0.49);
  EXPECT_DOUBLE_EQ(tube_ratio->step, 0.01);
  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive",
                               "primitive.torus.major_segments", 24.0)
                  .ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive",
                               "primitive.torus.minor_segments", 10.0)
                  .ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive",
                               "primitive.torus.tube_radius_ratio", 0.3)
                  .ok());

  const quader::QuaderApiVoidResult reset =
      editor.tools().reset_active_settings();
  ASSERT_TRUE(reset.ok()) << reset.error_message();
  EXPECT_TRUE(reset.changed());
  EXPECT_EQ(editor.tools().active_primitive_type(), "torus");
  settings = editor.tools().active_settings();
  const quader::QuaderToolSettingControl *reset_major =
      find_tool_setting_control(settings, "primitive.torus.major_segments");
  const quader::QuaderToolSettingControl *reset_minor =
      find_tool_setting_control(settings, "primitive.torus.minor_segments");
  const quader::QuaderToolSettingControl *reset_ratio =
      find_tool_setting_control(settings, "primitive.torus.tube_radius_ratio");
  ASSERT_NE(reset_major, nullptr);
  ASSERT_NE(reset_minor, nullptr);
  ASSERT_NE(reset_ratio, nullptr);
  EXPECT_DOUBLE_EQ(reset_major->number_value, 32.0);
  EXPECT_DOUBLE_EQ(reset_minor->number_value, 12.0);
  EXPECT_DOUBLE_EQ(reset_ratio->number_value, 0.2);

  const quader::QuaderApiVoidResult canceled =
      editor.tools().cancel_active_tool_preview();
  EXPECT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_TRUE(editor.tools().active_primitive_default_name().empty());
  EXPECT_FALSE(editor.tools().active_settings().active);
}

TEST(EditorApiTests,
     ActiveSettingsRevisionSkipsStableReadsAndTracksPrimitiveChanges) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;

  const std::uint64_t initial = editor.tools().active_settings_revision();
  EXPECT_EQ(editor.tools().active_settings_revision(), initial);

  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  const std::uint64_t activated = editor.tools().active_settings_revision();
  EXPECT_GT(activated, initial);
  EXPECT_EQ(editor.tools().active_settings_revision(), activated);

  const quader::QuaderApiVoidResult changed = editor.tools().set_setting(
      "create_primitive", "primitive.box.subdivisions_x", 4.0);
  ASSERT_TRUE(changed.ok()) << changed.error_message();
  EXPECT_TRUE(changed.changed());
  const std::uint64_t setting_changed =
      editor.tools().active_settings_revision();
  EXPECT_GT(setting_changed, activated);

  const quader::QuaderApiVoidResult unchanged = editor.tools().set_setting(
      "create_primitive", "primitive.box.subdivisions_x", 4.0);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(editor.tools().active_settings_revision(), setting_changed);

  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_GT(editor.tools().active_settings_revision(), setting_changed);
}

TEST(EditorApiTests,
     ActiveToolRevisionTracksOrdinaryToolIdentityWithoutStableReadChurn) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  (void)create_selected_box(editor, "Active Tool Revision Source");

  const std::uint64_t select_revision = editor.tools().active_tool_revision();
  EXPECT_EQ(editor.tools().active_tool_revision(), select_revision);
  EXPECT_EQ(editor.tools().active_tool(), "select");

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Move).ok());
  const std::uint64_t move_revision = editor.tools().active_tool_revision();
  EXPECT_GT(move_revision, select_revision);
  EXPECT_EQ(editor.tools().active_tool_revision(), move_revision);
  EXPECT_EQ(editor.tools().active_tool(), "move");

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Rotate).ok());
  EXPECT_GT(editor.tools().active_tool_revision(), move_revision);
  EXPECT_EQ(editor.tools().active_tool(), "rotate");
}

TEST(EditorApiTests,
     ArrayToolSettingsRevisionSkipsStableReadsAndTracksSettingsChanges) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  (void)create_selected_box(editor, "Array Settings Revision Source");

  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::LinearArray).ok());
  const std::uint64_t linear_settings_revision =
      editor.tools().active_settings_revision();
  for (int step = 0; step < 32; ++step) {
    EXPECT_EQ(editor.tools().active_settings_revision(),
              linear_settings_revision);
  }

  const quader::QuaderApiVoidResult count_changed =
      editor.tools().set_setting("linear_array", "linear_array.count", 6.0);
  ASSERT_TRUE(count_changed.ok()) << count_changed.error_message();
  ASSERT_TRUE(count_changed.changed());
  const std::uint64_t count_revision =
      editor.tools().active_settings_revision();
  EXPECT_GT(count_revision, linear_settings_revision);
  const quader::QuaderApiVoidResult count_unchanged =
      editor.tools().set_setting("linear_array", "linear_array.count", 6.0);
  ASSERT_TRUE(count_unchanged.ok()) << count_unchanged.error_message();
  EXPECT_FALSE(count_unchanged.changed());
  EXPECT_EQ(editor.tools().active_settings_revision(), count_revision);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());

  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::RadialArray).ok());
  const std::uint64_t radial_settings_revision =
      editor.tools().active_settings_revision();
  for (int step = 0; step < 32; ++step) {
    EXPECT_EQ(editor.tools().active_settings_revision(),
              radial_settings_revision);
  }

  const quader::QuaderApiVoidResult degrees_changed =
      editor.tools().set_setting("radial_array", "radial_array.degrees",
                                 270.0);
  ASSERT_TRUE(degrees_changed.ok()) << degrees_changed.error_message();
  ASSERT_TRUE(degrees_changed.changed());
  const std::uint64_t degrees_revision =
      editor.tools().active_settings_revision();
  EXPECT_GT(degrees_revision, radial_settings_revision);

  const quader::QuaderApiVoidResult degrees_unchanged =
      editor.tools().set_setting("radial_array", "radial_array.degrees",
                                 270.0);
  ASSERT_TRUE(degrees_unchanged.ok()) << degrees_unchanged.error_message();
  EXPECT_FALSE(degrees_unchanged.changed());
  EXPECT_EQ(editor.tools().active_settings_revision(), degrees_revision);
}

TEST(EditorApiTests, ToolSettingsResetRejectsNoTarget) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;

  const quader::QuaderToolSettingsTarget inactive =
      editor.tools().active_settings();
  EXPECT_FALSE(inactive.active);
  EXPECT_TRUE(inactive.description.empty());
  EXPECT_FALSE(inactive.can_reset);
  const quader::QuaderApiVoidResult no_target =
      editor.tools().reset_active_settings();
  EXPECT_FALSE(no_target.ok());
  EXPECT_FALSE(no_target.changed());
}

TEST(EditorApiTests,
     PrimitiveCommandsDoNotFallThroughToOperationPreviews) {
  constexpr std::array kPrimitiveCommands{
      quader::quader_command::kToolsApplyPrimitiveCreation,
      quader::quader_command::kToolsCancelPrimitiveCreation,
  };

  for (const quader::QuaderCommandId command : kPrimitiveCommands) {
    SCOPED_TRACE(command.string());
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    (void)create_selected_box(editor, "Primitive Command Isolation Box");
    const quader::QuaderApiVoidResult preview = editor.commands().execute(
        editor.commands().command_id_for_modeling_action("half_cut"));
    ASSERT_TRUE(preview.ok()) << preview.error_message();

    const quader::QuaderApiVoidResult isolated =
        editor.commands().execute(command);
    EXPECT_FALSE(isolated.ok());
    const quader::QuaderApiVoidResult canceled =
        editor.tools().cancel_active_tool_preview();
    EXPECT_TRUE(canceled.ok()) << canceled.error_message();
  }
}

TEST(EditorApiTests, GenericModalCancelRoutesThroughPrimitiveOwner) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive", "primitive.type", "sphere")
                  .ok());
  ASSERT_TRUE(editor.tools().has_active_primitive_creation());

  const quader::QuaderApiVoidResult canceled =
      editor.tools().cancel_modal_tool();

  EXPECT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_FALSE(editor.tools().has_active_primitive_creation());
  EXPECT_EQ(editor.tools().active_tool(), "select");
}

TEST(EditorApiTests, ViewportSelectionModeShortcutsResolveAsRouterIntents) {
    /// Maps one viewport shortcut mode to the public modeling selection mode it should apply.
    struct ModeCase {
      quader::ViewportSelectionMode viewport_mode;
    };
    constexpr std::array kCases{
        ModeCase{quader::ViewportSelectionMode::Vertex},
        ModeCase{quader::ViewportSelectionMode::Edge},
        ModeCase{quader::ViewportSelectionMode::Face},
        ModeCase{quader::ViewportSelectionMode::Object},
        ModeCase{quader::ViewportSelectionMode::Group},
    };

    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::ViewportShortcutController controller;

    for (const ModeCase &mode : kCases) {
      SCOPED_TRACE(static_cast<int>(mode.viewport_mode));
      const std::string target = quader::viewport_selection_mode_target_id(
          quader::viewport_selection_mode_id(mode.viewport_mode));
      const std::optional<std::vector<quader::QuaderInputChord>> parsed =
          quader::quader_input_parse_shortcut(
              editor.commands().shortcut_for(target));
      ASSERT_TRUE(parsed.has_value()) << target;
      ASSERT_EQ(parsed->size(), 1u) << target;

      const quader::QuaderViewportKeybindingDispatch dispatch =
          editor.commands().dispatch_viewport_input_binding(parsed->front(), {},
                                                            true, false);
      ASSERT_EQ(
          dispatch.kind,
          quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
      EXPECT_EQ(dispatch.shortcut.kind,
                quader::ViewportShortcutKind::SetSelectionMode);
      EXPECT_EQ(dispatch.shortcut.selection_mode, mode.viewport_mode);

      quader::ViewportInputResult input;
      input.shortcut_handled = true;
      input.selection_mode_changed = true;
      input.shortcut = dispatch.shortcut;

      const quader::ViewportShortcutControllerResult result =
          controller.handle_modeling_shortcut(input, editor);

      EXPECT_FALSE(result.command_executed);
      EXPECT_FALSE(result.invalidate_hover);
      EXPECT_FALSE(result.mark_overlay_dirty);
      EXPECT_EQ(editor.selection().summary().mode,
                quader::QuaderSelectionMode::Object);
    }
}

TEST(EditorApiTests,
     KnifeShortcutResolvesAcrossEveryPersistentSelectionMode) {
  constexpr std::array kModes{
      quader::ViewportSelectionMode::Vertex,
      quader::ViewportSelectionMode::Edge,
      quader::ViewportSelectionMode::Face,
      quader::ViewportSelectionMode::Object,
      quader::ViewportSelectionMode::Group,
  };
  const std::filesystem::path path =
      keybinding_test_path("knife-all-selection-modes");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  (void)create_selected_box(editor, "Knife Shortcut Box");
  EXPECT_EQ(editor.commands().shortcut_for(
                quader::quader_command::kToolsActivateKnife),
            "C");

  for (const quader::ViewportSelectionMode mode : kModes) {
    SCOPED_TRACE(static_cast<int>(mode));
    quader::ViewportToolState state;
    state.selection_mode = mode;
    const quader::QuaderViewportKeybindingDispatch dispatch =
        editor.commands().dispatch_viewport_input_binding(
            single_input_chord("C"), state, true, false);

    ASSERT_EQ(
        dispatch.kind,
        quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(dispatch.shortcut.kind,
              quader::ViewportShortcutKind::ActivateTool);
    EXPECT_EQ(dispatch.shortcut.tool, quader::ViewportToolId::Knife);
  }
}

TEST(EditorApiTests, InputBindingsSupportPunctuationAndMouseChords) {
  const std::filesystem::path path = keybinding_test_path("input-chords");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  quader::ViewportToolState state;
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kEditUndo, "Ctrl+Comma")
                  .ok());
  EXPECT_EQ(editor.commands().shortcut_for(quader::quader_command::kEditUndo),
            "Ctrl+Comma");
  quader::QuaderCreateEntityArgs undo_seed_args;
  undo_seed_args.name = "Undo Binding Seed";
  ASSERT_TRUE(editor.scene().create_entity(undo_seed_args).ok());
  ASSERT_TRUE(editor.history().can_undo());

  const quader::QuaderViewportKeybindingDispatch comma_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          quader::QuaderInputChord{
              .primary = true,
              .device = quader::QuaderInputDevice::Keyboard,
              .key = quader::QuaderKeyCode::Comma,
          },
          state, true, false);
  EXPECT_EQ(comma_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(comma_dispatch.command_id,
            quader::quader_command::kEditUndo.string());

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kEditRedo, "Mouse4")
                  .ok());
  EXPECT_EQ(editor.commands().shortcut_for(quader::quader_command::kEditRedo),
            "Mouse4");
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.history().can_redo());

  const quader::QuaderViewportKeybindingDispatch mouse_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          quader::QuaderInputChord{
              .device = quader::QuaderInputDevice::Mouse,
              .mouse_button = quader::QuaderMouseButton::Back,
          },
          state, true, false);
  EXPECT_EQ(mouse_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(mouse_dispatch.command_id,
            quader::quader_command::kEditRedo.string());

  quader::QuaderCreateEntityArgs wheel_undo_seed;
  wheel_undo_seed.name = "Wheel Undo Binding Seed";
  ASSERT_TRUE(editor.scene().create_entity(wheel_undo_seed).ok());
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kEditUndo, "WheelUp")
                  .ok());
  const quader::QuaderViewportKeybindingDispatch wheel_command_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          quader::QuaderInputChord{
              .device = quader::QuaderInputDevice::Mouse,
              .gesture = quader::QuaderInputGesture::WheelUp,
          },
          state, true, false);
  EXPECT_EQ(wheel_command_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(wheel_command_dispatch.command_id,
            quader::quader_command::kEditUndo.string());

  const std::string rotate_target =
      editor.commands().command_id_for_tool_activation("rotate");
  ASSERT_TRUE(editor.preferences().set_shortcut(rotate_target, "WheelDown").ok());
  const quader::QuaderViewportKeybindingDispatch wheel_tool_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          quader::QuaderInputChord{
              .device = quader::QuaderInputDevice::Mouse,
              .gesture = quader::QuaderInputGesture::WheelDown,
          },
          state, true, false);
  EXPECT_EQ(wheel_tool_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
  EXPECT_EQ(wheel_tool_dispatch.shortcut.kind,
            quader::ViewportShortcutKind::ActivateTool);
  EXPECT_EQ(wheel_tool_dispatch.shortcut.tool, quader::ViewportToolId::Rotate);
}

TEST(EditorApiTests,
     PendingMultiChordDispatchOwnsAStatelessViewportActionContinuation) {
  const std::filesystem::path path =
      keybinding_test_path("pending-viewport-action-continuation");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  quader::QuaderCreateEntityArgs undo_seed;
  undo_seed.name = "Pending Binding Seed";
  ASSERT_TRUE(editor.scene().create_entity(undo_seed).ok());
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kEditUndo, "Ctrl+K D")
                  .ok());

  quader::ViewportToolState state;
  const auto prefix = editor.commands().dispatch_viewport_input_binding(
      quader::QuaderInputChord{
          .primary = true,
          .device = quader::QuaderInputDevice::Keyboard,
          .key = quader::QuaderKeyCode::K,
      },
      state, true, false);
  EXPECT_EQ(prefix.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::PendingChord);
  EXPECT_TRUE(editor.commands().has_pending_keybinding_chord());

  const auto continuation = editor.commands().dispatch_viewport_input_binding(
      quader::QuaderInputChord{
          .device = quader::QuaderInputDevice::Keyboard,
          .key = quader::QuaderKeyCode::D,
      },
      state, true, false);
  EXPECT_EQ(continuation.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(continuation.command_id,
            quader::quader_command::kEditUndo.string());
  EXPECT_FALSE(editor.commands().has_pending_keybinding_chord());
}

TEST(EditorApiTests, ViewportNavigationActionsUseMouseInputBindings) {
  const std::filesystem::path path =
      keybinding_test_path("navigation-bindings");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  quader::ViewportToolState state;
  quader::ViewportInputActionKind action =
      quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("MiddleMouseDrag"), state, true, false,
      action));
  EXPECT_EQ(action, quader::ViewportInputActionKind::Orbit);
  EXPECT_EQ(editor.commands().shortcut_for("viewport.navigation.pan"),
            "Shift+MiddleMouseDrag");
  EXPECT_EQ(
      editor.commands().shortcut_for("viewport.navigation.reset_fly_speed"),
      "MiddleMouse");

  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("MiddleMouse"), state, true, false,
      action, quader::QuaderViewportKeybindingContext{
                  .navigation_active = true,
                  .fly_navigation_active = true,
              }));
  EXPECT_EQ(action, quader::ViewportInputActionKind::ResetFlySpeed);

  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Escape"), state, true, false, action));
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Escape"), state, true, false, action,
      quader::QuaderViewportKeybindingContext{.navigation_active = true}));
  EXPECT_EQ(action, quader::ViewportInputActionKind::CancelNavigation);

  ASSERT_TRUE(
      editor.preferences()
          .set_shortcut("viewport.navigation.orbit", "Alt+LeftMouseDrag")
          .ok());
  EXPECT_EQ(editor.commands().shortcut_for("viewport.navigation.orbit"),
            "Alt+LeftMouseDrag");

  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("MiddleMouseDrag"), state, true, false,
      action));
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Alt+LeftMouseDrag"), state, true,
      false, action));
  EXPECT_EQ(action, quader::ViewportInputActionKind::Orbit);
}

TEST(EditorApiTests, ViewportKeyChordConversionUsesSharedInputAdapter) {
  const std::optional<quader::QuaderInputChord> move_chord =
      quader::quader_input_chord_for_viewport_key(quader::ViewportKey::W, {});
  ASSERT_TRUE(move_chord.has_value());
  EXPECT_EQ(move_chord->device, quader::QuaderInputDevice::Keyboard);
  EXPECT_EQ(move_chord->key, quader::QuaderKeyCode::W);
  EXPECT_EQ(quader::viewport_key_for_quader_key_code(move_chord->key),
            quader::ViewportKey::W);

  const std::optional<quader::QuaderInputChord> primary_delete =
      quader::quader_input_chord_for_viewport_key(
          quader::ViewportKey::Delete,
          quader::ViewportModifiers{.primary = true});
  ASSERT_TRUE(primary_delete.has_value());
  EXPECT_TRUE(primary_delete->primary);
  EXPECT_EQ(primary_delete->key, quader::QuaderKeyCode::Delete);

  EXPECT_EQ(
      quader::viewport_key_for_quader_key_code(quader::QuaderKeyCode::Numpad1),
      quader::ViewportKey::Number1);
  EXPECT_EQ(quader::viewport_key_for_quader_key_code(
                quader::QuaderKeyCode::NumpadEnter),
            quader::ViewportKey::Enter);
  EXPECT_EQ(quader::quader_key_code_for_viewport_key(quader::ViewportKey::F12),
            quader::QuaderKeyCode::F12);
}

TEST(EditorApiTests, ViewportHostShortcutCatalogDrivesCommandKeybindings) {
  const std::filesystem::path path =
      keybinding_test_path("viewport-host-catalog");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  create_selected_box(editor, "Shortcut History A");
  create_selected_box(editor, "Shortcut History B");
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.history().can_undo());
  ASSERT_TRUE(editor.history().can_redo());

  const std::vector<quader::QuaderKeybindingRule> rules =
      editor.commands().keybindings();
  EXPECT_TRUE(has_keybinding_when(rules,
                                  quader::quader_command::kEditUndo.string(),
                                  "Ctrl+Z", "!textInputActive"));
  EXPECT_TRUE(
      has_keybinding_when(rules, quader::quader_command::kEditRedo.string(),
                          "Ctrl+Shift+Z", "!textInputActive"));
  EXPECT_FALSE(has_keybinding(
      rules, quader::quader_command::kEditRedo.string(), "Ctrl+Y"));
  quader::ViewportToolState state;
  int checked = 0;
  for (const quader::ViewportShortcutBindingDescriptor &descriptor :
       quader::viewport_shortcut_binding_catalog()) {
    quader::ViewportShortcut contract_shortcut;
    if (!quader::viewport_shortcut_from_contract_target(descriptor.target_id,
                                                        contract_shortcut)) {
      continue;
    }

    const std::optional<std::string> command_target =
        quader::viewport_command_target_id_for_contract_target(
            descriptor.target_id);
    ASSERT_TRUE(command_target.has_value()) << descriptor.target_id;
    EXPECT_TRUE(has_keybinding(rules, *command_target, descriptor.shortcut))
        << descriptor.target_id << " -> " << *command_target << " / "
        << descriptor.shortcut;

    const std::optional<std::vector<quader::QuaderInputChord>> parsed =
        quader::quader_input_parse_shortcut(descriptor.shortcut);
    ASSERT_TRUE(parsed.has_value()) << descriptor.shortcut;
    ASSERT_EQ(parsed->size(), 1u) << descriptor.shortcut;

    const quader::QuaderViewportKeybindingDispatch dispatch =
        editor.commands().dispatch_viewport_input_binding(parsed->front(),
                                                          state, true, false);
    const std::optional<quader::QuaderCommandDescriptor> command =
        editor.commands().descriptor(*command_target);
    if (command.has_value() && !command->enabled) {
      EXPECT_EQ(dispatch.kind,
                quader::QuaderViewportKeybindingDispatch::Kind::NoMatch)
          << descriptor.target_id << " -> " << *command_target;
      ++checked;
      continue;
    }
    if (dispatch.kind ==
        quader::QuaderViewportKeybindingDispatch::Kind::NoMatch) {
      // The command predicate can be enabled while its app-owned semantic
      // scope (for example Face or Edge mode) does not match this fixture.
      ++checked;
      continue;
    }
    if (quader::viewport_target_requires_router_dispatch(*command_target)) {
      ASSERT_EQ(
          dispatch.kind,
          quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut)
          << descriptor.target_id << " -> " << *command_target;
      EXPECT_TRUE(same_viewport_shortcut(contract_shortcut, dispatch.shortcut))
          << descriptor.target_id << " -> " << *command_target;
    } else {
      EXPECT_EQ(dispatch.kind,
                quader::QuaderViewportKeybindingDispatch::Kind::Command)
          << descriptor.target_id << " -> " << *command_target;
      EXPECT_EQ(dispatch.command_id, *command_target)
          << descriptor.target_id << " -> " << *command_target;
      quader::ViewportShortcut router_shortcut;
      EXPECT_FALSE(quader::viewport_router_shortcut_for_target_id(
          *command_target, router_shortcut))
          << descriptor.target_id << " -> " << *command_target;
    }
    ++checked;
  }

    EXPECT_GT(checked, 0);

    const std::optional<std::vector<quader::QuaderInputChord>> pivot =
        quader::quader_input_parse_shortcut("P");
    ASSERT_TRUE(pivot.has_value());
    ASSERT_EQ(pivot->size(), 1u);
    const quader::QuaderViewportKeybindingDispatch pivot_dispatch =
        editor.commands().dispatch_viewport_input_binding(pivot->front(),
                                                          state, true, false);
    ASSERT_EQ(pivot_dispatch.kind,
              quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(pivot_dispatch.shortcut.kind,
              quader::ViewportShortcutKind::ActivateTool);
    EXPECT_EQ(pivot_dispatch.shortcut.tool, quader::ViewportToolId::Pivot);

    EXPECT_TRUE(has_keybinding(
        rules, quader::quader_command::kModelingApplyActivePreview.string(),
        "Space"));
    EXPECT_TRUE(has_keybinding(
        rules, quader::quader_command::kModelingCancelActivePreview.string(),
        "Escape"));

    const std::optional<std::vector<quader::QuaderInputChord>> space =
        quader::quader_input_parse_shortcut("Space");
    ASSERT_TRUE(space.has_value());
    ASSERT_EQ(space->size(), 1u);
    const quader::QuaderViewportKeybindingDispatch space_without_preview =
        editor.commands().dispatch_viewport_input_binding(space->front(), state,
                                                          true, false);
    ASSERT_EQ(space_without_preview.kind,
              quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(space_without_preview.shortcut.kind,
              quader::ViewportShortcutKind::ToggleMaximized);
    const quader::QuaderViewportKeybindingDispatch space_with_preview =
        editor.commands().dispatch_viewport_input_binding(
            space->front(), state, true, false,
            quader::QuaderViewportKeybindingContext{.operation_preview_active =
                                                        true});
    EXPECT_EQ(space_with_preview.kind,
              quader::QuaderViewportKeybindingDispatch::Kind::Command);
    EXPECT_EQ(space_with_preview.command_id,
              quader::quader_command::kModelingApplyActivePreview.string());

    const std::optional<std::vector<quader::QuaderInputChord>> escape =
        quader::quader_input_parse_shortcut("Escape");
    ASSERT_TRUE(escape.has_value());
    ASSERT_EQ(escape->size(), 1u);
    const quader::QuaderViewportKeybindingDispatch escape_without_preview =
        editor.commands().dispatch_viewport_input_binding(escape->front(),
                                                          state, true, false);
    EXPECT_EQ(escape_without_preview.kind,
              quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
    const quader::QuaderViewportKeybindingDispatch escape_with_drag =
        editor.commands().dispatch_viewport_input_binding(
            escape->front(), state, true, false,
            quader::QuaderViewportKeybindingContext{
                .active_transform_drag = true});
    ASSERT_EQ(escape_with_drag.kind,
              quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(escape_with_drag.shortcut.kind,
              quader::ViewportShortcutKind::CancelActiveTool);
    const quader::QuaderViewportKeybindingDispatch escape_with_preview =
        editor.commands().dispatch_viewport_input_binding(
            escape->front(), state, true, false,
            quader::QuaderViewportKeybindingContext{.operation_preview_active =
                                                        true});
    EXPECT_EQ(escape_with_preview.kind,
              quader::QuaderViewportKeybindingDispatch::Kind::Command);
    EXPECT_EQ(escape_with_preview.command_id,
              quader::quader_command::kModelingCancelActivePreview.string());
}

TEST(EditorApiTests, DefaultKeybindingProjectionDoesNotDuplicateRuleTuples) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    std::unordered_set<std::string> seen;
    for (const quader::QuaderKeybindingRule& rule : editor.commands().keybindings()) {
      const std::string key = rule.target_id + "|" + rule.shortcut + "|" +
                              rule.when + "|" + rule.source;
      EXPECT_TRUE(seen.insert(key).second) << key;
    }
}

TEST(EditorApiTests, ViewportNavigationActionCatalogDrivesDefaultsAndResolution) {
  const std::filesystem::path path =
      keybinding_test_path("viewport-action-catalog");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  const std::vector<quader::QuaderKeybindingRule> rules =
      editor.commands().keybindings();
  quader::ViewportToolState state;
  for (const quader::ViewportInputActionBindingDescriptor &descriptor :
       quader::viewport_input_action_binding_catalog()) {
    state = {};
    if (descriptor.when.find("activeTool.linear_array") !=
        std::string_view::npos) {
      state.active_tool = quader::ViewportToolId::LinearArray;
    }
    if (descriptor.when.find("activeTool.radial_array") !=
        std::string_view::npos) {
      state.active_tool = quader::ViewportToolId::RadialArray;
    }
    if (descriptor.when.find("activeTool.cut") != std::string_view::npos) {
      state.active_tool = quader::ViewportToolId::Cut;
    }
    EXPECT_TRUE(
        has_keybinding(rules, descriptor.target_id, descriptor.shortcut))
        << descriptor.target_id << " / " << descriptor.shortcut;

    quader::ViewportInputActionKind catalog_action =
        quader::ViewportInputActionKind::None;
    ASSERT_TRUE(quader::viewport_input_action_for_target_id(
        descriptor.target_id, catalog_action))
        << descriptor.target_id;
    EXPECT_EQ(catalog_action, descriptor.action);

    const std::optional<std::vector<quader::QuaderInputChord>> parsed =
        quader::quader_input_parse_shortcut(descriptor.shortcut);
    ASSERT_TRUE(parsed.has_value()) << descriptor.shortcut;
    ASSERT_EQ(parsed->size(), 1u) << descriptor.shortcut;

    quader::ViewportInputActionKind resolved_action =
        quader::ViewportInputActionKind::None;
    const bool navigation_only_action =
        descriptor.when.find("!navigation.active") == std::string_view::npos &&
        descriptor.when.find("navigation.active") != std::string_view::npos;
    const bool fly_only_action =
        descriptor.when.find("navigation.flyActive") != std::string_view::npos;
    const bool modal_only_action =
        descriptor.when.find("!modalTool.active") == std::string_view::npos &&
        descriptor.when.find("modalTool.active") != std::string_view::npos;
    quader::QuaderViewportKeybindingContext input_context;
    if (navigation_only_action) {
      input_context.navigation_active = true;
    }
    if (fly_only_action) {
      input_context.navigation_active = true;
      input_context.fly_navigation_active = true;
    }
    if (modal_only_action) {
      input_context.modal_tool_active = true;
    }
    ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
        parsed->front(), state, true, false, resolved_action, input_context))
        << descriptor.target_id;
    EXPECT_EQ(resolved_action, descriptor.action) << descriptor.target_id;
  }
}

TEST(EditorApiTests,
     MaterialMouseActionsUseDistinctScopedEditableClickBindings) {
  const std::filesystem::path path =
      keybinding_test_path("material-mouse-actions");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  constexpr std::string_view kSampleTarget =
      "viewport.material.sample_face";
  constexpr std::string_view kApplyActiveTarget =
      "viewport.material.apply_active_to_selection";
  constexpr std::string_view kApplyFaceTarget =
      "viewport.material.apply_face_to_mesh";
  const std::vector<quader::QuaderKeybindingCatalogEntry> catalog =
      editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *sample =
      find_keybinding_catalog_entry(catalog, kSampleTarget);
  const quader::QuaderKeybindingCatalogEntry *apply_active =
      find_keybinding_catalog_entry(catalog, kApplyActiveTarget);
  const quader::QuaderKeybindingCatalogEntry *apply_face =
      find_keybinding_catalog_entry(catalog, kApplyFaceTarget);
  ASSERT_NE(sample, nullptr);
  ASSERT_NE(apply_active, nullptr);
  ASSERT_NE(apply_face, nullptr);

  EXPECT_EQ(sample->action_id, kSampleTarget);
  EXPECT_EQ(apply_active->action_id, kApplyActiveTarget);
  EXPECT_EQ(apply_face->action_id, kApplyFaceTarget);
  EXPECT_NE(apply_active->action_id,
            quader::quader_command::kApplyMaterial.string());
  EXPECT_EQ(sample->shortcut, "Shift+RightMouseClick");
  EXPECT_EQ(apply_active->shortcut, "Ctrl+RightMouseClick");
  EXPECT_EQ(apply_face->shortcut, "Ctrl+RightMouseDoubleClick");
  EXPECT_EQ(sample->scope_id, "viewport.selection.any");
  EXPECT_EQ(apply_active->scope_id, "viewport.selection.any");
  EXPECT_EQ(apply_face->scope_id, "viewport.selection.any");
  EXPECT_NE(sample->when.find("selectionMode.vertex"), std::string::npos);
  EXPECT_NE(sample->when.find("selectionMode.edge"), std::string::npos);
  EXPECT_NE(sample->when.find("selectionMode.face"), std::string::npos);
  EXPECT_NE(sample->when.find("selectionMode.object"), std::string::npos);
  EXPECT_NE(sample->when.find("selectionMode.group"), std::string::npos);
  EXPECT_NE(apply_face->when.find("selectionMode.group"),
            std::string::npos);
  EXPECT_NE(apply_active->when.find("selectionMode.vertex"),
            std::string::npos);
  EXPECT_NE(apply_active->when.find("selectionMode.edge"),
            std::string::npos);
  EXPECT_NE(apply_active->when.find("selectionMode.face"),
            std::string::npos);
  EXPECT_NE(apply_active->when.find("selectionMode.object"),
            std::string::npos);
  EXPECT_NE(apply_active->when.find("selectionMode.group"),
            std::string::npos);

  const quader::QuaderKeybindingCatalogEntry *keyboard_apply =
      find_keybinding_catalog_entry(
          catalog, quader::quader_command::kApplyMaterial.string());
  ASSERT_NE(keyboard_apply, nullptr);
  EXPECT_EQ(keyboard_apply->shortcut, "Shift+T");
  EXPECT_NE(keyboard_apply->action_id, apply_active->action_id);

  const auto resolve = [&](std::string_view shortcut,
                           quader::ViewportSelectionMode mode) {
    quader::ViewportToolState state;
    state.selection_mode = mode;
    quader::ViewportInputActionKind action =
        quader::ViewportInputActionKind::None;
    const bool matched = editor.commands().resolve_viewport_input_action(
        single_input_chord(shortcut), state, true, false, action);
    return std::pair{matched, action};
  };

  EXPECT_EQ(resolve("Shift+RightMouseClick",
                    quader::ViewportSelectionMode::Vertex),
            (std::pair{true,
                       quader::ViewportInputActionKind::SampleFaceMaterial}));
  EXPECT_EQ(resolve("Ctrl+RightMouseClick",
                    quader::ViewportSelectionMode::Face),
            (std::pair{
                true, quader::ViewportInputActionKind::
                          ApplyActiveMaterialToSelection}));
  EXPECT_EQ(resolve("Ctrl+RightMouseDoubleClick",
                    quader::ViewportSelectionMode::Edge),
            (std::pair{true,
                       quader::ViewportInputActionKind::
                           ApplyFaceMaterialToMesh}));
  EXPECT_EQ(resolve("Ctrl+RightMouseClick",
                    quader::ViewportSelectionMode::Edge),
            (std::pair{
                true, quader::ViewportInputActionKind::
                          ApplyActiveMaterialToSelection}));
  EXPECT_EQ(resolve("Shift+RightMouseClick",
                    quader::ViewportSelectionMode::Group),
            (std::pair{true,
                       quader::ViewportInputActionKind::SampleFaceMaterial}));
  EXPECT_EQ(resolve("Ctrl+RightMouseDoubleClick",
                    quader::ViewportSelectionMode::Group),
            (std::pair{true,
                       quader::ViewportInputActionKind::
                           ApplyFaceMaterialToMesh}));
  EXPECT_EQ(resolve("Ctrl+RightMouseClick",
                    quader::ViewportSelectionMode::Group),
            (std::pair{
                true, quader::ViewportInputActionKind::
                          ApplyActiveMaterialToSelection}));
  EXPECT_FALSE(resolve("RightMouseClick",
                       quader::ViewportSelectionMode::Face)
                   .first);
  EXPECT_FALSE(resolve("Ctrl+Shift+RightMouseClick",
                       quader::ViewportSelectionMode::Face)
                   .first);
  EXPECT_EQ(resolve("Ctrl+RightMouseClick",
                    quader::ViewportSelectionMode::Object)
                .second,
            quader::ViewportInputActionKind::ApplyActiveMaterialToSelection);
  EXPECT_EQ(resolve("Ctrl+RightMouseDoubleClick",
                    quader::ViewportSelectionMode::Object)
                .second,
            quader::ViewportInputActionKind::ApplyFaceMaterialToMesh);

  const quader::QuaderKeybindingAssignmentAnalysis conflict =
      editor.commands().analyze_shortcut_assignment(
          sample->key, "Ctrl+RightMouseClick");
  ASSERT_TRUE(conflict.ok);
  EXPECT_NE(std::find_if(conflict.conflicts.begin(), conflict.conflicts.end(),
                         [&](const auto &entry) {
                           return entry.action == apply_active->key;
                         }),
            conflict.conflicts.end());

  const quader::QuaderKeybindingActionKey sample_key = sample->key;
  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(sample_key, "Alt+RightMouseClick")
                  .ok());
  EXPECT_FALSE(resolve("Shift+RightMouseClick",
                       quader::ViewportSelectionMode::Face)
                   .first);
  EXPECT_EQ(resolve("Alt+RightMouseClick",
                    quader::ViewportSelectionMode::Face),
            (std::pair{true,
                       quader::ViewportInputActionKind::SampleFaceMaterial}));
  const std::vector<quader::QuaderKeybindingCatalogEntry> updated_catalog =
      editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *updated_sample =
      find_keybinding_catalog_entry(updated_catalog, kSampleTarget);
  ASSERT_NE(updated_sample, nullptr);
  EXPECT_EQ(updated_sample->shortcut, "Alt+RightMouseClick");
}

TEST(EditorApiTests,
     MaterialMouseActionsRejectReservedBareRmbButAcceptEditablePairs) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(
                      keybinding_test_path("material-mouse-reservations"))
                  .ok());

  constexpr std::array<std::string_view, 3> kMaterialTargets{
      "viewport.material.sample_face",
      "viewport.material.apply_active_to_selection",
      "viewport.material.apply_face_to_mesh",
  };
  const std::vector<quader::QuaderKeybindingCatalogEntry> catalog =
      editor.commands().keybinding_catalog();
  std::array<quader::QuaderKeybindingActionKey, 3> actions;
  for (std::size_t index = 0u; index < kMaterialTargets.size(); ++index) {
    const quader::QuaderKeybindingCatalogEntry *entry =
        find_keybinding_catalog_entry(catalog, kMaterialTargets[index]);
    ASSERT_NE(entry, nullptr);
    actions[index] = entry->key;
  }

  constexpr std::array<std::string_view, 4> kReservedBareRmbGestures{
      "RightMouse",
      "RightMouseClick",
      "RightMouseDoubleClick",
      "RightMouseDrag",
  };
  for (const quader::QuaderKeybindingActionKey &action : actions) {
    for (const std::string_view shortcut : kReservedBareRmbGestures) {
      const quader::QuaderKeybindingAssignmentAnalysis analysis =
          editor.commands().analyze_shortcut_assignment(action, shortcut);
      EXPECT_FALSE(analysis.ok) << shortcut;
      EXPECT_NE(analysis.message.find("reserved"), std::string::npos)
          << shortcut;
      const quader::QuaderApiVoidResult rejected =
          editor.preferences().set_action_shortcut(
              action, std::string(shortcut),
              quader::QuaderKeybindingConflictPolicy::Replace);
      EXPECT_FALSE(rejected.ok()) << shortcut;
    }
  }

  const quader::QuaderKeybindingAssignmentAnalysis modified_rmb =
      editor.commands().analyze_shortcut_assignment(
          actions[0], "Alt+RightMouseClick");
  EXPECT_TRUE(modified_rmb.ok) << modified_rmb.message;
  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(actions[0], "Alt+RightMouseClick")
                  .ok());

  const quader::QuaderKeybindingAssignmentAnalysis left_click =
      editor.commands().analyze_shortcut_assignment(
          actions[1], "Alt+LeftMouseClick");
  const quader::QuaderKeybindingAssignmentAnalysis left_double =
      editor.commands().analyze_shortcut_assignment(
          actions[2], "Alt+LeftMouseDoubleClick");
  EXPECT_TRUE(left_click.ok) << left_click.message;
  EXPECT_TRUE(left_double.ok) << left_double.message;
  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(actions[1], "Alt+LeftMouseClick")
                  .ok());
  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(actions[2],
                                       "Alt+LeftMouseDoubleClick")
                  .ok());
}

TEST(EditorApiTests, MaterialMouseActionResolverMarksOnlyOneShotFlags) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(
                      keybinding_test_path("material-mouse-action-flags"))
                  .ok());
  constexpr std::string_view kApplyActiveTarget =
      "viewport.material.apply_active_to_selection";
  constexpr std::string_view kApplyFaceTarget =
      "viewport.material.apply_face_to_mesh";
  const std::vector<quader::QuaderKeybindingCatalogEntry> catalog =
      editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *apply_active =
      find_keybinding_catalog_entry(catalog, kApplyActiveTarget);
  const quader::QuaderKeybindingCatalogEntry *apply_face =
      find_keybinding_catalog_entry(catalog, kApplyFaceTarget);
  ASSERT_NE(apply_active, nullptr);
  ASSERT_NE(apply_face, nullptr);
  const quader::QuaderKeybindingActionKey apply_active_key =
      apply_active->key;
  const quader::QuaderKeybindingActionKey apply_face_key = apply_face->key;

  quader::ViewportToolState state;
  state.selection_mode = quader::ViewportSelectionMode::Face;
  std::vector<quader::QuaderInputChord> suppressed_chords;
  double linear_array_wheel_remainder = 0.0;
  double radial_array_wheel_remainder = 0.0;
  const auto resolved_frame = [&](std::string_view shortcut) {
    quader::ViewportInputFrame input;
    input.viewport_focused = true;
    input.pressed_input_chords.push_back(single_input_chord(shortcut));
    quader::ViewportKeybindingResolver::resolve(
        editor, state, input, suppressed_chords,
        linear_array_wheel_remainder, radial_array_wheel_remainder);
    return input;
  };

  quader::ViewportInputFrame input =
      resolved_frame("Shift+RightMouseClick");
  EXPECT_TRUE(input.sample_face_material_pressed);
  EXPECT_FALSE(input.apply_active_material_to_selection_pressed);
  EXPECT_FALSE(input.apply_active_material_delay_for_double_click);
  EXPECT_FALSE(input.apply_face_material_to_mesh_pressed);

  input = resolved_frame("Ctrl+RightMouseClick");
  EXPECT_FALSE(input.sample_face_material_pressed);
  EXPECT_TRUE(input.apply_active_material_to_selection_pressed);
  EXPECT_TRUE(input.apply_active_material_delay_for_double_click);
  EXPECT_FALSE(input.apply_face_material_to_mesh_pressed);

  input = resolved_frame("Ctrl+RightMouseDoubleClick");
  EXPECT_FALSE(input.sample_face_material_pressed);
  EXPECT_FALSE(input.apply_active_material_to_selection_pressed);
  EXPECT_FALSE(input.apply_active_material_delay_for_double_click);
  EXPECT_TRUE(input.apply_face_material_to_mesh_pressed);

  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(apply_active_key, "Ctrl+Alt+F18")
                  .ok());
  input = resolved_frame("Ctrl+Alt+F18");
  EXPECT_TRUE(input.apply_active_material_to_selection_pressed);
  EXPECT_FALSE(input.apply_active_material_delay_for_double_click);

  ASSERT_TRUE(editor.preferences()
                  .reset_action_shortcut(apply_active_key)
                  .ok());
  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(
                      apply_face_key, "Alt+RightMouseDoubleClick")
                  .ok());
  input = resolved_frame("Ctrl+RightMouseClick");
  EXPECT_TRUE(input.apply_active_material_to_selection_pressed);
  EXPECT_FALSE(input.apply_active_material_delay_for_double_click)
      << "an unmatched mouse double-click binding must not delay the action";

  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(apply_active_key,
                                       "Alt+LeftMouseClick")
                  .ok());
  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(apply_face_key,
                                       "Alt+LeftMouseDoubleClick")
                  .ok());
  input = resolved_frame("Alt+LeftMouseClick");
  EXPECT_TRUE(input.apply_active_material_to_selection_pressed);
  EXPECT_TRUE(input.apply_active_material_delay_for_double_click)
      << "matching editable mouse click/double-click pairs need disambiguation";

  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(apply_active_key, "Mouse4Click")
                  .ok());
  ASSERT_TRUE(editor.preferences()
                  .set_action_shortcut(apply_face_key,
                                       "Mouse4DoubleClick")
                  .ok());
  input = resolved_frame("Mouse4Click");
  EXPECT_TRUE(input.apply_active_material_to_selection_pressed);
  EXPECT_TRUE(input.apply_active_material_delay_for_double_click)
      << "auxiliary mouse click/double-click pairs need disambiguation";

  input.pressed_input_chords.clear();
  quader::ViewportKeybindingResolver::resolve(
      editor, state, input, suppressed_chords, linear_array_wheel_remainder,
      radial_array_wheel_remainder);
  EXPECT_FALSE(input.sample_face_material_pressed);
  EXPECT_FALSE(input.apply_active_material_to_selection_pressed);
  EXPECT_FALSE(input.apply_active_material_delay_for_double_click);
  EXPECT_FALSE(input.apply_face_material_to_mesh_pressed);
}

TEST(EditorApiTests,
     ArrayWheelBindingsResolveOnlyForTheActiveNonNavigatingTool) {
  const std::filesystem::path path =
      keybinding_test_path("radial-array-wheel-bindings");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  quader::ViewportToolState state{
      .active_tool = quader::ViewportToolId::RadialArray,
  };
  quader::ViewportInputActionKind action =
      quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Shift+WheelUp"), state, true, false,
      action));
  EXPECT_EQ(action,
            quader::ViewportInputActionKind::RadialArrayIncreaseCopies);

  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Shift+WheelDown"), state, true,
      false, action));
  EXPECT_EQ(action,
            quader::ViewportInputActionKind::RadialArrayDecreaseCopies);

  state.active_tool = quader::ViewportToolId::LinearArray;
  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Shift+WheelUp"), state, true, false,
      action));
  EXPECT_EQ(action,
            quader::ViewportInputActionKind::LinearArrayIncreaseCopies);

  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Shift+WheelDown"), state, true,
      false, action));
  EXPECT_EQ(action,
            quader::ViewportInputActionKind::LinearArrayDecreaseCopies);

  state.active_tool = quader::ViewportToolId::Select;
  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Shift+WheelUp"), state, true, false,
      action));

  state.active_tool = quader::ViewportToolId::RadialArray;
  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Shift+WheelUp"), state, true, false,
      action, quader::QuaderViewportKeybindingContext{
                  .navigation_active = true,
              }));
}

TEST(EditorApiTests, ViewportModalToolGesturesUseKeybindingTargets) {
  const std::filesystem::path path =
      keybinding_test_path("modal-tool-bindings");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  quader::ViewportToolState state;
  const quader::QuaderViewportKeybindingContext modal_context{
      .modal_tool_active = true};
  quader::ViewportInputActionKind action =
      quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Space"), state, true, false, action,
      modal_context));
  EXPECT_EQ(action, quader::ViewportInputActionKind::ModalToolFinalize);

  state.active_tool = quader::ViewportToolId::Cut;
  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Enter"), state, true, false, action,
      modal_context));
  EXPECT_EQ(action, quader::ViewportInputActionKind::None);

  const quader::QuaderViewportKeybindingContext viewport_tool_context{
      .active_tool_owns_viewport = true};
  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Space"), state, true, false, action,
      viewport_tool_context));
  EXPECT_EQ(action, quader::ViewportInputActionKind::ModalToolFinalize);

  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("F"), state, true, false, action,
      modal_context));
  EXPECT_EQ(action, quader::ViewportInputActionKind::ModalToolModeCycle);

  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Space"), state, true, false, action));

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut("viewport.modal_tool.finalize", "Ctrl+Enter")
                  .ok());
  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Space"), state, true, false, action,
      modal_context));
  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Ctrl+Enter"), state, true, false,
      action, modal_context));
  EXPECT_EQ(action, quader::ViewportInputActionKind::ModalToolFinalize);

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut("viewport.modal_tool.mode_cycle", "Shift+F")
                  .ok());
  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("F"), state, true, false, action,
      modal_context));
  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Shift+F"), state, true, false, action,
      modal_context));
  EXPECT_EQ(action, quader::ViewportInputActionKind::ModalToolModeCycle);
}

TEST(EditorApiTests, ViewportKeybindingContextsSeparateNavigationFromToolShortcuts) {
  const std::filesystem::path path =
      keybinding_test_path("navigation-contexts");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  quader::ViewportToolState state;
  const quader::QuaderViewportKeybindingDispatch tool_dispatch =
      editor.commands().dispatch_viewport_keybinding(quader::ViewportKey::W, {},
                                                     state, true, false);
  ASSERT_EQ(tool_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
  EXPECT_EQ(tool_dispatch.shortcut.kind,
            quader::ViewportShortcutKind::ActivateTool);
  EXPECT_EQ(tool_dispatch.shortcut.tool, quader::ViewportToolId::Move);

  const quader::QuaderViewportKeybindingDispatch navigation_dispatch =
      editor.commands().dispatch_viewport_keybinding(
          quader::ViewportKey::W, {}, state, true, false,
          quader::QuaderViewportKeybindingContext{
              .navigation_active = true,
              .fly_navigation_active = true,
          });
  EXPECT_EQ(navigation_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);

  quader::ViewportInputActionKind action =
      quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("RightMouseDrag"), state, true, false,
      action,
      quader::QuaderViewportKeybindingContext{
          .navigation_active = true,
          .fly_navigation_active = true,
      }));
  EXPECT_EQ(action, quader::ViewportInputActionKind::Fly);

  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("W"), state, true, false, action,
      quader::QuaderViewportKeybindingContext{
          .navigation_active = true,
          .fly_navigation_active = true,
      }));
  EXPECT_EQ(action, quader::ViewportInputActionKind::FlyForward);

  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("D"), state, true, false, action));
  EXPECT_EQ(action, quader::ViewportInputActionKind::QuickPivot);

  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("D"), state, true, false, action,
      quader::QuaderViewportKeybindingContext{
          .navigation_active = true,
          .fly_navigation_active = true,
      }));
  EXPECT_EQ(action, quader::ViewportInputActionKind::FlyRight);

  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("D"), state, true, false, action,
      quader::QuaderViewportKeybindingContext{.navigation_active = true}));

  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("D"), state, true, false, action,
      quader::QuaderViewportKeybindingContext{.modal_tool_active = true}));

  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("D"), state, true, false, action,
      quader::QuaderViewportKeybindingContext{
          .active_tool_owns_viewport = true}));

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut("viewport.navigation.fly_forward", "Up")
                  .ok());
  action = quader::ViewportInputActionKind::None;
  EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("W"), state, true, false, action,
      quader::QuaderViewportKeybindingContext{
          .navigation_active = true,
          .fly_navigation_active = true,
      }));
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Up"), state, true, false, action,
      quader::QuaderViewportKeybindingContext{
          .navigation_active = true,
          .fly_navigation_active = true,
      }));
  EXPECT_EQ(action, quader::ViewportInputActionKind::FlyForward);
}

TEST(EditorApiTests, PublicMutationsMarkStandaloneProjectDirty) {
  auto expect_dirty_after = [](auto mutate) {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    EXPECT_FALSE(editor.project().dirty());
    mutate(editor);
    EXPECT_TRUE(editor.project().dirty());
  };

  expect_dirty_after([](quader::QuaderEditorApi &editor) {
    quader::QuaderCreateEntityArgs entity_args;
    entity_args.name = "Dirty Entity";
    const quader::QuaderApiResult<quader::QuaderEntityRef> entity =
        editor.scene().create_entity(entity_args);
    ASSERT_TRUE(entity.ok()) << entity.error_message();
  });
  expect_dirty_after([](quader::QuaderEditorApi &editor) {
    quader::QuaderMaterialArgs material_args;
    material_args.name = "Dirty Material";
    material_args.base_color = {0.8f, 0.7f, 0.6f};
    const quader::QuaderApiResult<quader::QuaderAssetRef> material =
        editor.assets().add_material(material_args);
    ASSERT_TRUE(material.ok()) << material.error_message();
  });
  expect_dirty_after([](quader::QuaderEditorApi &editor) {
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
        editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Dirty Modeling Box");
    ASSERT_TRUE(box.ok()) << box.error_message();
  });
}

TEST(EditorApiTests, RetiredModelingActionCommandAliasesDoNotRouteThroughCommandRegistry) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    EXPECT_EQ(
        editor.commands().command_id_for_modeling_action("duplicate_meshes"),
        quader::quader_modeling_command_target_id("duplicate_meshes"));
    EXPECT_EQ(editor.commands()
                  .execute("modeling.action.duplicate_meshes")
                  .error_message(),
              "Unknown command id.");
    EXPECT_NE(
        editor.commands().execute("modeling.duplicate_meshes").error_message(),
        "Unknown command id.");
    for (const std::string_view retired_command_id : {
             std::string_view{"modeling.action.delete_selection"},
             std::string_view{"modeling.delete_selection"},
             std::string_view{"history.undo"},
             std::string_view{"history.redo"},
             std::string_view{"history.repeat_last_action"},
             std::string_view{"selection.clear"},
             std::string_view{"edit.select_all"},
             std::string_view{"edit.invert_selection"},
             std::string_view{"selection.select_all"},
             std::string_view{"selection.select_inverse"},
             std::string_view{"selection.invert"},
             std::string_view{"selection.select_edge_direction"},
             std::string_view{"selection.select_edge_loops"},
             std::string_view{"selection.select_edge_ring"},
             std::string_view{"selection.select_faces_with_same_material"},
             std::string_view{"selection.select_coplanar_faces"},
             std::string_view{"selection.select_ngons"},
             std::string_view{"select.face.ngons"},
             std::string_view{"selection.select_similar_meshes"},
             std::string_view{"selection.grow"},
             std::string_view{"selection.shrink"},
             std::string_view{"scene.group_selection"},
             std::string_view{"scene.ungroup_selection"},
         }) {
      EXPECT_EQ(editor.commands().execute(retired_command_id).error_message(),
                "Unknown command id.")
          << retired_command_id;
    }
}

TEST(EditorApiTests,
     SelectionCommandsUseCanonicalIdsModeScopesAndDeliberateKeybindings) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(
                      keybinding_test_path("selection-commands"))
                  .ok());

  constexpr std::array kCanonicalCommands{
      quader::quader_command::kSelectAll,
      quader::quader_command::kSelectInverse,
      quader::quader_command::kSelectToggleBackfaceSelection,
      quader::quader_command::kSelectEdgeDirection,
      quader::quader_command::kSelectEdgeLoops,
      quader::quader_command::kSelectEdgeRing,
      quader::quader_command::kSelectFacesWithSameMaterial,
      quader::quader_command::kSelectCoplanarFaces,
      quader::quader_command::kSelectFaceNormal,
      quader::quader_command::kSelectMeshNGons,
      quader::quader_command::kSelectSimilarMeshes,
      quader::quader_command::kSelectGrow,
      quader::quader_command::kSelectShrink,
  };
  const std::unordered_set<std::string> registered =
      available_command_ids(editor);
  const std::unordered_set<std::string> typed = typed_command_ids();
  for (const quader::QuaderCommandId command : kCanonicalCommands) {
    EXPECT_TRUE(registered.contains(std::string(command.string())))
        << command.string();
    EXPECT_TRUE(typed.contains(std::string(command.string())))
        << command.string();
    EXPECT_TRUE(command_descriptor(editor, command).has_value())
        << command.string();
  }
  ASSERT_TRUE(command_descriptor(
                  editor,
                  quader::quader_command::kSelectToggleBackfaceSelection)
                  .has_value());
  EXPECT_EQ(command_descriptor(
                editor,
                quader::quader_command::kSelectToggleBackfaceSelection)
                ->label,
            "Toggle Backface Selection");
  for (const std::string_view retired : {
           std::string_view{"edit.select_all"},
           std::string_view{"edit.invert_selection"},
       }) {
    EXPECT_FALSE(registered.contains(std::string(retired))) << retired;
    EXPECT_FALSE(typed.contains(std::string(retired))) << retired;
    EXPECT_FALSE(command_descriptor(editor, quader::QuaderCommandId{retired})
                     .has_value())
        << retired;
    EXPECT_EQ(editor.commands().execute(retired).error_message(),
              "Unknown command id.")
        << retired;
  }

  const std::vector<quader::QuaderKeybindingCatalogEntry> catalog =
      editor.commands().keybinding_catalog();
  /// Stores the expected shortcut scope for one canonical Select command.
  struct ScopeExpectation {
    quader::QuaderCommandId command;
    std::string_view scope;
    bool deliberately_unassigned = true;
  };
  constexpr std::array kScopeExpectations{
      ScopeExpectation{quader::quader_command::kSelectAll,
                       "viewport.selection.any", false},
      ScopeExpectation{quader::quader_command::kSelectInverse,
                       "viewport.selection.any", false},
      ScopeExpectation{
          quader::quader_command::kSelectToggleBackfaceSelection,
          "viewport.selection.any", false},
      ScopeExpectation{
          quader::quader_command::kSelectEdgeDirection,
          "viewport.selection.edge"},
      ScopeExpectation{quader::quader_command::kSelectEdgeLoops,
                       "viewport.selection.edge"},
      ScopeExpectation{quader::quader_command::kSelectEdgeRing,
                       "viewport.selection.edge"},
      ScopeExpectation{
          quader::quader_command::kSelectFacesWithSameMaterial,
          "viewport.selection.face"},
      ScopeExpectation{quader::quader_command::kSelectCoplanarFaces,
                       "viewport.selection.face"},
      ScopeExpectation{quader::quader_command::kSelectFaceNormal,
                       "viewport.selection.face"},
      ScopeExpectation{quader::quader_command::kSelectMeshNGons,
                       "viewport.selection.object"},
      ScopeExpectation{quader::quader_command::kSelectSimilarMeshes,
                       "viewport.selection.object"},
      ScopeExpectation{quader::quader_command::kSelectGrow,
                       "viewport.selection.component"},
      ScopeExpectation{quader::quader_command::kSelectShrink,
                       "viewport.selection.component"},
  };
  for (const ScopeExpectation &expected : kScopeExpectations) {
    const quader::QuaderKeybindingCatalogEntry *entry =
        find_keybinding_catalog_entry(catalog, expected.command.string());
    ASSERT_NE(entry, nullptr) << expected.command.string();
    EXPECT_EQ(entry->scope_id, expected.scope) << expected.command.string();
    if (expected.deliberately_unassigned) {
      EXPECT_TRUE(entry->shortcut.empty()) << expected.command.string();
      EXPECT_TRUE(entry->default_shortcut.empty())
          << expected.command.string();
      EXPECT_TRUE(entry->unassigned) << expected.command.string();
      EXPECT_TRUE(editor.commands().shortcut_for(expected.command).empty())
          << expected.command.string();
    }
  }
  EXPECT_EQ(editor.commands().shortcut_for(
                quader::quader_command::kSelectAll),
            "Ctrl+A");
  EXPECT_EQ(editor.commands().shortcut_for(
                quader::quader_command::kSelectInverse),
            "Ctrl+I");
  EXPECT_EQ(editor.commands().shortcut_for(
                quader::quader_command::kSelectToggleBackfaceSelection),
            "Ctrl+Shift+F9");
  quader::ViewportToolState keybinding_state;
  for (const quader::ViewportSelectionMode mode : {
           quader::ViewportSelectionMode::Vertex,
           quader::ViewportSelectionMode::Edge,
           quader::ViewportSelectionMode::Face,
           quader::ViewportSelectionMode::Object,
           quader::ViewportSelectionMode::Group,
       }) {
    keybinding_state.selection_mode = mode;
    const quader::QuaderViewportKeybindingDispatch dispatch =
        editor.commands().dispatch_viewport_input_binding(
            single_input_chord("Ctrl+Shift+F9"), keybinding_state, true,
            false);
    ASSERT_EQ(dispatch.kind,
              quader::QuaderViewportKeybindingDispatch::Kind::Command);
    EXPECT_EQ(dispatch.command_id,
              quader::quader_command::kSelectToggleBackfaceSelection.string());
  }

  const auto expect_enabled = [&](quader::QuaderCommandId command,
                                  bool expected) {
    const std::optional<quader::QuaderCommandDescriptor> descriptor =
        command_descriptor(editor, command);
    ASSERT_TRUE(descriptor.has_value()) << command.string();
    EXPECT_EQ(descriptor->enabled, expected) << command.string();
  };
  const auto expect_specialized_disabled = [&] {
    for (const quader::QuaderCommandId command :
         std::span{kCanonicalCommands}.subspan(3U)) {
      expect_enabled(command, false);
    }
  };

  expect_enabled(quader::quader_command::kSelectAll, true);
  expect_enabled(quader::quader_command::kSelectInverse, true);
  expect_enabled(quader::quader_command::kSelectToggleBackfaceSelection,
                 true);
  expect_specialized_disabled();

  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Selection Command Modes");
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  expect_enabled(quader::quader_command::kSelectSimilarMeshes, true);
  expect_enabled(quader::quader_command::kSelectMeshNGons, true);
  expect_enabled(quader::quader_command::kSelectEdgeDirection, false);
  expect_enabled(quader::quader_command::kSelectFacesWithSameMaterial,
                 false);
  expect_enabled(quader::quader_command::kSelectGrow, false);
  expect_enabled(quader::quader_command::kSelectShrink, false);

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(box, 1, 2).ok());
  expect_enabled(quader::quader_command::kSelectEdgeDirection, true);
  expect_enabled(quader::quader_command::kSelectEdgeLoops, true);
  expect_enabled(quader::quader_command::kSelectEdgeRing, true);
  expect_enabled(quader::quader_command::kSelectSimilarMeshes, false);
  expect_enabled(quader::quader_command::kSelectGrow, true);
  expect_enabled(quader::quader_command::kSelectShrink, true);

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box, 1).ok());
  expect_enabled(quader::quader_command::kSelectEdgeDirection, false);
  expect_enabled(quader::quader_command::kSelectFacesWithSameMaterial,
                 true);
  expect_enabled(quader::quader_command::kSelectCoplanarFaces, true);
  expect_enabled(quader::quader_command::kSelectFaceNormal, true);
  expect_enabled(quader::quader_command::kSelectMeshNGons, false);
  expect_enabled(quader::quader_command::kSelectGrow, true);
  expect_enabled(quader::quader_command::kSelectShrink, true);

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.selection().select_vertex(box, 1).ok());
  expect_enabled(quader::quader_command::kSelectCoplanarFaces, false);
  expect_enabled(quader::quader_command::kSelectFaceNormal, false);
  expect_enabled(quader::quader_command::kSelectMeshNGons, false);
  expect_enabled(quader::quader_command::kSelectGrow, true);
  expect_enabled(quader::quader_command::kSelectShrink, true);

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Group).ok());
  expect_enabled(quader::quader_command::kSelectAll, true);
  expect_enabled(quader::quader_command::kSelectInverse, true);
  expect_specialized_disabled();
}

TEST(EditorApiTests, CommandRegistryCoversEveryAppOperationAndViewportTool) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const std::unordered_set<std::string> ids = available_command_ids(editor);
    const std::unordered_set<std::string> typed_ids = typed_command_ids();
    const std::unordered_set<std::string> generated_modeling_ids{
        quader::quader_modeling_command_target_id("duplicate_meshes"),
    };

    for (const std::string& id : ids) {
      EXPECT_TRUE(typed_ids.contains(id) || generated_modeling_ids.contains(id))
          << id;
    }

    for (const std::string &typed_id : typed_ids) {
      EXPECT_TRUE(ids.contains(typed_id)) << typed_id;
    }
    for (const std::string &generated_id : generated_modeling_ids) {
      EXPECT_TRUE(ids.contains(generated_id)) << generated_id;
    }

    for (const quader::QuaderSelectionMode mode : {
                 quader::QuaderSelectionMode::Object,
                 quader::QuaderSelectionMode::Vertex,
                 quader::QuaderSelectionMode::Edge,
                 quader::QuaderSelectionMode::Face,
         }) {
        CommandTestHarness harness;
        setup_selection_for_mode(harness.editor, mode);
        for (const quader::QuaderActionSurface surface : {
                     quader::QuaderActionSurface::OperationsPanel,
                     quader::QuaderActionSurface::ViewportContextMenu,
             }) {
          const std::vector<quader::QuaderActionItem> actions =
              harness.editor.modeling().available_actions({
                  .surface = surface,
                  .include_disabled = true,
                  .include_hidden = true,
              });
          for (const quader::QuaderActionItem &action : actions) {
            SCOPED_TRACE(action.action_id);
            const std::string command =
                command_id_for_context_action(harness.editor, action);
            EXPECT_FALSE(command.empty()) << action.action_id;
            CommandTestHarness command_route_harness;
            EXPECT_NE(command_route_harness.editor.commands()
                          .execute(command)
                          .error_message(),
                      "Unknown command id.")
                << command;
          }
        }
    }
}

TEST(EditorApiTests, ModelingActionSetExposesActiveOperationPanelForSelectionModeShelf) {
  const quader::QuaderActionQuery query{
      .surface = quader::QuaderActionSurface::OperationsPanel,
      .include_disabled = true,
      .include_hidden = false,
  };

  CommandTestHarness object_harness;
  setup_selection_for_mode(object_harness.editor,
                           quader::QuaderSelectionMode::Object);
  const quader::QuaderActionSet object_actions =
      object_harness.editor.modeling().available_action_set(query);
  ASSERT_EQ(object_actions.panels.size(), 1u);
  const quader::QuaderActionPanel *object_panel =
      find_action_panel(object_actions, "object_operations");
  ASSERT_NE(object_panel, nullptr);
  EXPECT_EQ(object_panel->visibility, quader::QuaderActionVisibility::Enabled);
  ASSERT_FALSE(object_panel->actions.empty());
  EXPECT_EQ(object_panel->actions.front().action_id, "snap_meshes_to_floor");
  EXPECT_FALSE(object_panel->actions.front().rebuilds_mesh_on_success);
  const quader::QuaderActionItem *combine =
      find_action_item(*object_panel, "combine_mesh");
  ASSERT_NE(combine, nullptr);
  EXPECT_EQ(combine->visibility, quader::QuaderActionVisibility::Disabled);

  CommandTestHarness face_harness;
  setup_selection_for_mode(face_harness.editor,
                           quader::QuaderSelectionMode::Face);
  const quader::QuaderActionSet face_actions =
      face_harness.editor.modeling().available_action_set(query);
  ASSERT_EQ(face_actions.panels.size(), 1u);
  const quader::QuaderActionPanel *face_panel =
      find_action_panel(face_actions, "face_operations");
  ASSERT_NE(face_panel, nullptr);
  EXPECT_EQ(face_panel->visibility, quader::QuaderActionVisibility::Enabled);
  ASSERT_FALSE(face_panel->actions.empty());
  EXPECT_EQ(face_panel->actions.front().action_id, "bridge_faces");
  const quader::QuaderActionItem *thicken =
      find_action_item(*face_panel, "thicken_faces");
  ASSERT_NE(thicken, nullptr);
  EXPECT_EQ(thicken->operation_id, "thicken");

  CommandTestHarness group_harness;
  create_selected_box(group_harness.editor);
  ASSERT_TRUE(group_harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Group)
                  .ok());
  EXPECT_TRUE(group_harness.editor.modeling()
                  .available_action_set(query)
                  .panels.empty());
}

TEST(EditorApiTests,
     SnapToFloorCommandsUseDedicatedIdsUnassignedScopesAndNativeActions) {
  CommandTestHarness object_harness;
  setup_selection_for_mode(object_harness.editor,
                           quader::QuaderSelectionMode::Object);

  constexpr std::array kCommands{
      quader::quader_command::kModelingSnapMeshesToFloor,
      quader::quader_command::kModelingSnapVerticesToFloor,
  };
  const std::unordered_set<std::string> typed = typed_command_ids();
  for (const quader::QuaderCommandId command : kCommands) {
    EXPECT_TRUE(typed.contains(std::string(command.string())));
    const std::optional<quader::QuaderCommandDescriptor> descriptor =
        object_harness.editor.commands().descriptor(command);
    ASSERT_TRUE(descriptor.has_value()) << command.string();
    EXPECT_TRUE(descriptor->shortcut.empty()) << command.string();
  }

  EXPECT_EQ(object_harness.editor.commands().command_id_for_modeling_action(
                "snap_meshes_to_floor"),
            quader::quader_command::kModelingSnapMeshesToFloor.string());
  EXPECT_EQ(object_harness.editor.commands().command_id_for_modeling_action(
                "snap_vertices_to_floor"),
            quader::quader_command::kModelingSnapVerticesToFloor.string());
  EXPECT_EQ(object_harness.editor.commands()
                .descriptor(quader::quader_command::kModelingSnapMeshesToFloor)
                ->label,
            "Snap Meshes to Floor");
  EXPECT_EQ(
      object_harness.editor.commands()
          .descriptor(quader::quader_command::kModelingSnapVerticesToFloor)
          ->label,
      "Snap Vertices to Floor");

  const std::vector<quader::QuaderKeybindingCatalogEntry> catalog =
      object_harness.editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *mesh_keybinding =
      find_keybinding_catalog_entry(
          catalog,
          quader::quader_command::kModelingSnapMeshesToFloor.string());
  const quader::QuaderKeybindingCatalogEntry *vertex_keybinding =
      find_keybinding_catalog_entry(
          catalog,
          quader::quader_command::kModelingSnapVerticesToFloor.string());
  ASSERT_NE(mesh_keybinding, nullptr);
  ASSERT_NE(vertex_keybinding, nullptr);
  EXPECT_EQ(mesh_keybinding->scope_id, "viewport.selection.object");
  EXPECT_EQ(vertex_keybinding->scope_id, "viewport.selection.vertex");
  EXPECT_TRUE(mesh_keybinding->default_shortcut.empty());
  EXPECT_TRUE(vertex_keybinding->default_shortcut.empty());
  EXPECT_TRUE(mesh_keybinding->unassigned);
  EXPECT_TRUE(vertex_keybinding->unassigned);

  const quader::QuaderActionQuery operations_query{
      .surface = quader::QuaderActionSurface::OperationsPanel,
      .include_disabled = true,
  };
  const quader::QuaderActionSet object_actions =
      object_harness.editor.modeling().available_action_set(operations_query);
  const quader::QuaderActionPanel *object_panel = find_action_panel(
      object_actions, "object_operations");
  ASSERT_NE(object_panel, nullptr);
  ASSERT_FALSE(object_panel->actions.empty());
  const quader::QuaderActionItem &mesh_action = object_panel->actions.front();
  EXPECT_EQ(mesh_action.action_id, "snap_meshes_to_floor");
  EXPECT_EQ(mesh_action.label, "Snap Meshes to Floor");
  EXPECT_TRUE(mesh_action.operation_id.empty());
  EXPECT_EQ(mesh_action.kind, quader::QuaderActionKind::Operation);
  EXPECT_FALSE(mesh_action.rebuilds_mesh_on_success);

  CommandTestHarness vertex_harness;
  setup_selection_for_mode(vertex_harness.editor,
                           quader::QuaderSelectionMode::Vertex);
  const quader::QuaderActionSet vertex_actions =
      vertex_harness.editor.modeling().available_action_set(operations_query);
  const quader::QuaderActionPanel *vertex_panel = find_action_panel(
      vertex_actions, "vertex_operations");
  ASSERT_NE(vertex_panel, nullptr);
  const auto snap_active = std::find_if(
      vertex_panel->actions.begin(), vertex_panel->actions.end(),
      [](const auto &action) {
        return action.action_id == "snap_vertices_to_active";
      });
  ASSERT_NE(snap_active, vertex_panel->actions.end());
  const auto snap_floor = std::find_if(
      vertex_panel->actions.begin(), vertex_panel->actions.end(),
      [](const auto &action) {
        return action.action_id == "snap_vertices_to_floor";
      });
  ASSERT_NE(snap_floor, vertex_panel->actions.end());
  EXPECT_EQ(std::distance(vertex_panel->actions.begin(), snap_floor),
            std::distance(vertex_panel->actions.begin(), snap_active) + 1);
  EXPECT_EQ(snap_floor->label, "Snap Vertices to Floor");
  EXPECT_TRUE(snap_floor->operation_id.empty());
  EXPECT_TRUE(snap_floor->rebuilds_mesh_on_success);

  const quader::QuaderActionQuery context_query{
      .surface = quader::QuaderActionSurface::ViewportContextMenu,
      .include_disabled = true,
  };
  const quader::QuaderActionSet object_context =
      object_harness.editor.modeling().available_action_set(context_query);
  const quader::QuaderActionPanel *object_context_panel =
      find_action_panel(object_context, "viewport_context_menu");
  ASSERT_NE(object_context_panel, nullptr);
  ASSERT_FALSE(object_context_panel->actions.empty());
  EXPECT_EQ(object_context_panel->actions.front().action_id,
            "snap_meshes_to_floor");

  const quader::QuaderActionSet vertex_context =
      vertex_harness.editor.modeling().available_action_set(context_query);
  const quader::QuaderActionPanel *vertex_context_panel =
      find_action_panel(vertex_context, "viewport_context_menu");
  ASSERT_NE(vertex_context_panel, nullptr);
  const auto context_snap_active = std::find_if(
      vertex_context_panel->actions.begin(),
      vertex_context_panel->actions.end(), [](const auto &action) {
        return action.action_id == "snap_vertices_to_active";
      });
  const auto context_snap_floor = std::find_if(
      vertex_context_panel->actions.begin(),
      vertex_context_panel->actions.end(), [](const auto &action) {
        return action.action_id == "snap_vertices_to_floor";
      });
  ASSERT_NE(context_snap_active, vertex_context_panel->actions.end());
  ASSERT_NE(context_snap_floor, vertex_context_panel->actions.end());
  EXPECT_EQ(
      std::distance(vertex_context_panel->actions.begin(),
                    context_snap_floor),
      std::distance(vertex_context_panel->actions.begin(),
                    context_snap_active) +
          1);

  CommandTestHarness empty_harness;
  const quader::QuaderActionSet empty_object_actions =
      empty_harness.editor.modeling().available_action_set(operations_query);
  const quader::QuaderActionPanel *empty_object_panel =
      find_action_panel(empty_object_actions, "object_operations");
  ASSERT_NE(empty_object_panel, nullptr);
  const quader::QuaderActionItem *disabled_mesh =
      find_action_item(*empty_object_panel, "snap_meshes_to_floor");
  ASSERT_NE(disabled_mesh, nullptr);
  EXPECT_EQ(disabled_mesh->visibility,
            quader::QuaderActionVisibility::Disabled);
  EXPECT_EQ(disabled_mesh->disabled_reason_key,
            "modeling.action.disabled.snap_meshes_to_floor_selection");

  ASSERT_TRUE(empty_harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Vertex)
                  .ok());
  const quader::QuaderActionSet empty_vertex_actions =
      empty_harness.editor.modeling().available_action_set(operations_query);
  const quader::QuaderActionPanel *empty_vertex_panel =
      find_action_panel(empty_vertex_actions, "vertex_operations");
  ASSERT_NE(empty_vertex_panel, nullptr);
  const quader::QuaderActionItem *disabled_vertex =
      find_action_item(*empty_vertex_panel, "snap_vertices_to_floor");
  ASSERT_NE(disabled_vertex, nullptr);
  EXPECT_EQ(disabled_vertex->visibility,
            quader::QuaderActionVisibility::Disabled);
  EXPECT_EQ(disabled_vertex->disabled_reason_key,
            "modeling.action.disabled.snap_vertices_to_floor_selection");
}

TEST(EditorApiTests,
     SnapToFloorSelectionHandlesReplaceSelectionAndRoutePublicIntent) {
  {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef source =
        create_selected_box(harness.editor, "Object Handle Source");
    ASSERT_TRUE(harness.editor.modeling()
                    .mesh(source)
                    .transform()
                    .translate({0.0f, 5.0f, 0.0f})
                    .ok());
    const quader::QuaderModelingObjectRef receiver =
        create_selected_box(harness.editor, "Object Handle Receiver");
    ASSERT_EQ(harness.editor.selection().summary().active_object_id,
              receiver.modeling_object_id);

    const quader::QuaderApiVoidResult snapped =
        harness.editor.modeling().mesh(source).select().snap_to_floor();
    ASSERT_TRUE(snapped.ok()) << snapped.error_message();
    EXPECT_TRUE(snapped.changed());
    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(selection.selected_object_count, 1u);
    EXPECT_EQ(selection.active_object_id, source.modeling_object_id);
  }

  {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef source =
        create_selected_box(harness.editor, "Vertex Handle Source");
    ASSERT_TRUE(harness.editor.modeling()
                    .mesh(source)
                    .transform()
                    .translate({0.0f, 5.0f, 0.0f})
                    .ok());
    const quader::QuaderModelingObjectRef receiver =
        create_selected_box(harness.editor, "Vertex Handle Receiver");
    ASSERT_EQ(harness.editor.selection().summary().active_object_id,
              receiver.modeling_object_id);

    constexpr quader::QuaderElementId kVertex = 1;
    const quader::QuaderApiVoidResult snapped = harness.editor.modeling()
                                                      .mesh(source)
                                                      .vertices()
                                                      .only({kVertex})
                                                      .snap_to_floor();
    ASSERT_TRUE(snapped.ok()) << snapped.error_message();
    EXPECT_TRUE(snapped.changed());
    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Vertex);
    EXPECT_EQ(selection.selected_vertex_count, 1u);
    EXPECT_EQ(selection.active_object_id, source.modeling_object_id);
    EXPECT_EQ(selection.active_vertex_id, kVertex);
  }
}

TEST(EditorApiTests,
     SnapToFloorStaysDisabledWhileAModelingPreviewOwnsMutation) {
  CommandTestHarness harness;
  setup_selection_for_mode(harness.editor, quader::QuaderSelectionMode::Object);
  const std::string preview_command =
      harness.editor.commands().command_id_for_modeling_action("half_cut");
  ASSERT_TRUE(harness.editor.commands().execute(preview_command).ok());
  ASSERT_TRUE(harness.editor.modeling().can_cancel_active_tool_or_preview());

  EXPECT_FALSE(harness.editor.modeling().can_snap_selected_meshes_to_floor());
  const std::optional<quader::QuaderCommandDescriptor> descriptor =
      harness.editor.commands().descriptor(
          quader::quader_command::kModelingSnapMeshesToFloor);
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_FALSE(descriptor->enabled);

  const quader::QuaderActionSet actions =
      harness.editor.modeling().available_action_set({
          .surface = quader::QuaderActionSurface::OperationsPanel,
          .include_disabled = true,
      });
  const quader::QuaderActionPanel *panel =
      find_action_panel(actions, "object_operations");
  ASSERT_NE(panel, nullptr);
  const quader::QuaderActionItem *snap =
      find_action_item(*panel, "snap_meshes_to_floor");
  ASSERT_NE(snap, nullptr);
  EXPECT_EQ(snap->visibility, quader::QuaderActionVisibility::Disabled);
  EXPECT_EQ(snap->disabled_reason_key,
            "modeling.action.disabled.snap_to_floor_active_tool");

  const quader::QuaderApiVoidResult rejected =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  EXPECT_EQ(rejected.error_message(),
            "Apply or cancel the active tool before snapping to floor.");
  EXPECT_TRUE(harness.editor.modeling().can_cancel_active_tool_or_preview());
  ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
}

TEST(EditorApiTests, EnabledContextMenuOperationsExecuteThroughCommandRegistry) {
  int routed_action_count = 0;
  int successful_action_count = 0;
  for (const quader::QuaderSelectionMode mode : {
           quader::QuaderSelectionMode::Object,
           quader::QuaderSelectionMode::Vertex,
           quader::QuaderSelectionMode::Edge,
           quader::QuaderSelectionMode::Face,
       }) {
    CommandTestHarness list_harness;
    setup_selection_for_mode(list_harness.editor, mode);
    const std::vector<quader::QuaderActionItem> actions =
        list_harness.editor.modeling().available_actions({
            .surface = quader::QuaderActionSurface::ViewportContextMenu,
            .include_disabled = false,
            .include_hidden = false,
        });

    for (const quader::QuaderActionItem &action : actions) {
      SCOPED_TRACE(action.action_id);
      CommandTestHarness execute_harness;
      setup_selection_for_mode(execute_harness.editor, mode);
      const std::string command_id =
          command_id_for_context_action(execute_harness.editor, action);
      const quader::QuaderApiVoidResult result =
          execute_harness.editor.commands().execute(command_id);
      ++routed_action_count;
      EXPECT_TRUE(result.ok() ||
                  known_non_success_command_message(result.error_message()))
          << command_id << ": " << result.error_message();
      if (!result.ok()) {
        continue;
      }
      ++successful_action_count;
      EXPECT_TRUE(result.changed() ||
                  execute_harness.editor.query().history().can_undo ||
                  execute_harness.editor.query().tool_status().cut_active ||
                  execute_harness.editor.query().tool_status().mirror_active)
          << command_id;
    }
    }
    EXPECT_GT(routed_action_count, 0);
    EXPECT_GT(successful_action_count, 0);
}

TEST(EditorApiTests, ActivePreviewCommandsApplyAndCancelSliceQuadWithoutUi) {
    {
        CommandTestHarness harness;
        const quader::QuaderModelingObjectRef box =
            create_selected_box(harness.editor, "Slice Apply Command Box");
        ASSERT_TRUE(harness.editor.selection()
                        .set_mode(quader::QuaderSelectionMode::Face)
                        .ok());
        quader::QuaderPickSelectRequest pick_face;
        pick_face.ray = {
            .origin = {0.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
        };
        const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
            harness.editor.selection().pick_select(pick_face);
        ASSERT_TRUE(picked.ok()) << picked.error_message();
        ASSERT_EQ(picked.value().selected_face_count, 1u);
        const std::optional<quader::QuaderMeshSnapshot> before = harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(before.has_value());

        const quader::QuaderApiVoidResult preview =
            harness.editor.commands().execute(
                harness.editor.commands().command_id_for_modeling_action(
                    "slice_quad"));
        ASSERT_TRUE(preview.ok()) << preview.error_message();
        EXPECT_TRUE(preview.changed());
        const std::optional<quader::QuaderMeshSnapshot> preview_mesh =
            harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(preview_mesh.has_value());
        EXPECT_GT(preview_mesh->vertex_count, before->vertex_count);
        EXPECT_GT(preview_mesh->primitive_count, before->primitive_count);

        quader::QuaderPickSelectRequest miss_pick;
        miss_pick.ray = {
            .origin = {100.0f, 100.0f, 100.0f},
            .direction = {1.0f, 0.0f, 0.0f},
        };
        const quader::QuaderApiResult<quader::QuaderSelectionSummary> missed =
            harness.editor.selection().pick_select(miss_pick);
        ASSERT_TRUE(missed.ok()) << missed.error_message();
        EXPECT_EQ(missed.value().selected_face_count, 4u);
        const std::optional<quader::QuaderMeshSnapshot> preview_after_miss =
            harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(preview_after_miss.has_value());
        EXPECT_EQ(preview_after_miss->vertex_count, preview_mesh->vertex_count);
        EXPECT_EQ(preview_after_miss->primitive_count,
                  preview_mesh->primitive_count);

        const quader::QuaderApiVoidResult apply =
            harness.editor.commands().execute(
                quader::quader_command::kModelingApplyActivePreview);
        ASSERT_TRUE(apply.ok()) << apply.error_message();
        EXPECT_TRUE(apply.changed());
        EXPECT_EQ(harness.editor.query().history().undo_label,
                  "Apply Tool Preview");
        const std::optional<quader::QuaderMeshSnapshot> committed = harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(committed.has_value());
        EXPECT_EQ(committed->vertex_count, preview_mesh->vertex_count);
        EXPECT_EQ(committed->primitive_count, preview_mesh->primitive_count);
        EXPECT_FALSE(
            harness.editor.commands()
                .execute(quader::quader_command::kModelingApplyActivePreview)
                .ok());
    }

    {
        CommandTestHarness harness;
        const quader::QuaderModelingObjectRef box =
            create_selected_box(harness.editor, "Slice Cancel Command Box");
        ASSERT_TRUE(harness.editor.selection()
                        .set_mode(quader::QuaderSelectionMode::Face)
                        .ok());
        quader::QuaderPickSelectRequest pick_face;
        pick_face.ray = {
            .origin = {0.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
        };
        const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
            harness.editor.selection().pick_select(pick_face);
        ASSERT_TRUE(picked.ok()) << picked.error_message();
        ASSERT_EQ(picked.value().selected_face_count, 1u);
        const std::optional<quader::QuaderMeshSnapshot> before = harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(before.has_value());

        const quader::QuaderApiVoidResult preview =
            harness.editor.commands().execute(
                harness.editor.commands().command_id_for_modeling_action(
                    "slice_quad"));
        ASSERT_TRUE(preview.ok()) << preview.error_message();
        const std::optional<quader::QuaderMeshSnapshot> preview_mesh =
            harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(preview_mesh.has_value());
        EXPECT_GT(preview_mesh->vertex_count, before->vertex_count);

        const quader::QuaderApiVoidResult cancel =
            harness.editor.commands().execute(
                quader::quader_command::kModelingCancelActivePreview);
        ASSERT_TRUE(cancel.ok()) << cancel.error_message();
        EXPECT_TRUE(cancel.changed());
        const std::optional<quader::QuaderMeshSnapshot> restored = harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(restored.has_value());
        EXPECT_EQ(restored->vertex_count, before->vertex_count);
        EXPECT_EQ(restored->primitive_count, before->primitive_count);
        EXPECT_FALSE(
            harness.editor.commands()
                .execute(quader::quader_command::kModelingCancelActivePreview)
                .ok());
    }
}

TEST(EditorApiTests, HistoryUndoCancelsActiveBevelPreviewBeforeDocumentHistory) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Undo Bevel Preview Box");
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(editor.selection().select_edge(box, 1, 2).ok());
    ASSERT_TRUE(editor.selection().select_all().ok());
    ASSERT_GT(editor.selection().summary().selected_edge_count, 0u);

    const std::optional<quader::QuaderMeshSnapshot> before = editor.query().mesh(box.mesh);
    ASSERT_TRUE(before.has_value());
    const quader::QuaderHistorySnapshot history_before_preview =
        editor.query().history();
    EXPECT_EQ(history_before_preview.can_undo, editor.history().can_undo());

    const quader::QuaderApiVoidResult preview = editor.commands().execute(
        editor.commands().command_id_for_modeling_action("bevel_edges"));
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    EXPECT_TRUE(preview.changed());
    EXPECT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());
    EXPECT_TRUE(editor.history().can_undo());
    EXPECT_TRUE(editor.query().history().can_undo);
    const std::optional<quader::QuaderMeshSnapshot> preview_mesh =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(preview_mesh.has_value());
    EXPECT_GT(preview_mesh->primitive_count, before->primitive_count);

    const quader::QuaderApiVoidResult undo =
        editor.commands().execute(quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(undo.changed());
    EXPECT_FALSE(editor.modeling().can_cancel_active_tool_or_preview());
    EXPECT_TRUE(entity_named(editor, "Undo Bevel Preview Box").has_value());
    EXPECT_EQ(editor.query().history().undo_label,
              history_before_preview.undo_label);

    const std::optional<quader::QuaderMeshSnapshot> restored = editor.query().mesh(box.mesh);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->vertex_count, before->vertex_count);
    EXPECT_EQ(restored->primitive_count, before->primitive_count);
}

TEST(EditorApiTests, ProjectSaveRejectsUncommittedModelingAndNativePreviews) {
  {
    const std::filesystem::path path = project_test_path("preview-save");
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Unsaved Preview Box");
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(editor.selection().select_edge(box, 1, 2).ok());
    ASSERT_TRUE(editor.selection().select_all().ok());
    ASSERT_TRUE(editor.commands()
                    .execute(editor.commands().command_id_for_modeling_action(
                        "bevel_edges"))
                    .ok());

    const quader::QuaderApiVoidResult saved =
        save_scene_as_native(editor, path);
    EXPECT_FALSE(saved.ok());
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());
  }

  {
    const std::filesystem::path path = project_test_path("pivot-save");
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    create_selected_box(editor, "Unsaved Pivot Box");
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());

    const quader::QuaderApiVoidResult saved =
        save_scene_as_native(editor, path);
    EXPECT_FALSE(saved.ok());
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(editor.tools().active_settings().active);
  }
}

TEST(EditorApiTests,
     SelectionModeChangeCancelsPreviewBeforeApplyingTheNewMode) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Preview Mode Change Box");
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(box, 1, 2).ok());
  ASSERT_TRUE(editor.selection().select_all().ok());
  const std::optional<quader::QuaderMeshSnapshot> before =
      editor.query().mesh(box.mesh);
  ASSERT_TRUE(before.has_value());

  const quader::QuaderApiVoidResult preview = editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges"));
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  ASSERT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());

  const quader::QuaderApiVoidResult changed =
      editor.selection().set_mode(quader::QuaderSelectionMode::Face);
  ASSERT_TRUE(changed.ok()) << changed.error_message();
  EXPECT_TRUE(changed.changed());
  EXPECT_FALSE(editor.modeling().can_cancel_active_tool_or_preview());
  EXPECT_EQ(editor.selection().summary().mode,
            quader::QuaderSelectionMode::Face);
  const std::optional<quader::QuaderMeshSnapshot> restored =
      editor.query().mesh(box.mesh);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->vertex_count, before->vertex_count);
  EXPECT_EQ(restored->primitive_count, before->primitive_count);
  EXPECT_FALSE(editor.commands()
                   .execute(quader::quader_command::kModelingCancelActivePreview)
                   .ok());
}

TEST(EditorApiTests, HistoryRedoIsUnavailableWhileActivePreviewCanBeCanceled) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderModelingObjectRef preview_box =
        create_selected_box(editor, "Redo Preview Box");
    create_selected_box(editor, "Redo Target Box");
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(editor.history().can_redo());
    EXPECT_TRUE(editor.query().history().can_redo);
    EXPECT_FALSE(entity_named(editor, "Redo Target Box").has_value());

    ASSERT_TRUE(editor.selection().select_object(preview_box).ok());
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(editor.selection().select_edge(preview_box, 1, 2).ok());
    ASSERT_TRUE(editor.selection().select_all().ok());
    ASSERT_GT(editor.selection().summary().selected_edge_count, 0u);

    const quader::QuaderApiVoidResult preview = editor.commands().execute(
        editor.commands().command_id_for_modeling_action("bevel_edges"));
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    EXPECT_TRUE(preview.changed());
    EXPECT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());
    EXPECT_FALSE(editor.history().can_redo());
    EXPECT_FALSE(editor.query().history().can_redo);

    const quader::QuaderApiVoidResult redo =
        editor.commands().execute(quader::quader_command::kEditRedo);
    EXPECT_FALSE(redo.ok());
    EXPECT_FALSE(redo.changed());
    EXPECT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());
    EXPECT_FALSE(entity_named(editor, "Redo Target Box").has_value());

    const quader::QuaderApiVoidResult canceled = editor.commands().execute(
        quader::quader_command::kModelingCancelActivePreview);
    ASSERT_TRUE(canceled.ok()) << canceled.error_message();
    EXPECT_TRUE(editor.history().can_redo());
}

TEST(EditorApiTests,
     ModelingCommandDescriptorsUseTheModelingOwnersExactAvailability) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;

  const std::optional<quader::QuaderCommandDescriptor> without_selection =
      command_descriptor(editor,
                         quader::quader_command::kModelingRemoveDoubles);
  ASSERT_TRUE(without_selection.has_value());
  EXPECT_FALSE(editor.modeling().can_execute_tool("remove_doubles"));
  EXPECT_FALSE(without_selection->enabled);
  const quader::QuaderApiVoidResult rejected =
      editor.commands().execute(
          quader::quader_command::kModelingRemoveDoubles);
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());

  create_selected_box(editor, "Remove Doubles Availability Box");
  const std::optional<quader::QuaderCommandDescriptor> with_selection =
      command_descriptor(editor,
                         quader::quader_command::kModelingRemoveDoubles);
  ASSERT_TRUE(with_selection.has_value());
  EXPECT_TRUE(editor.modeling().can_execute_tool("remove_doubles"));
  EXPECT_TRUE(with_selection->enabled);
}

TEST(EditorApiTests,
     HistoryUndoCancelsNativeToolPreviewBeforeChangingDocumentHistory) {
  const auto run = [](quader::ViewportToolId tool, std::string name) {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    create_selected_box(editor, name);
    const quader::QuaderHistorySnapshot before = editor.query().history();
    ASSERT_TRUE(editor.tools().activate(tool).ok());
    if (tool == quader::ViewportToolId::Pivot) {
      ASSERT_TRUE(
          editor.tools().execute_action("pivot", "pivot.align.next").ok());
    }

    EXPECT_TRUE(editor.history().can_undo());
    EXPECT_FALSE(editor.history().can_redo());
    EXPECT_TRUE(editor.query().history().can_undo);
    EXPECT_FALSE(editor.query().history().can_redo);

    const quader::QuaderApiVoidResult cancel =
        editor.commands().execute(quader::quader_command::kEditUndo);
    ASSERT_TRUE(cancel.ok()) << cancel.error_message();
    EXPECT_TRUE(cancel.changed());
    EXPECT_FALSE(editor.tools().active_settings().active);
    EXPECT_TRUE(entity_named(editor, name).has_value());
    EXPECT_EQ(editor.query().history().undo_label, before.undo_label);

    const quader::QuaderApiVoidResult undo =
        editor.commands().execute(quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_FALSE(entity_named(editor, name).has_value());
  };

  run(quader::ViewportToolId::Pivot, "Undo Pivot Preview Box");
  run(quader::ViewportToolId::LinearArray, "Undo Array Preview Box");
  run(quader::ViewportToolId::CreatePrimitive,
      "Undo Primitive Preview Box");
}

TEST(EditorApiTests, HistoryUndoAfterSliceQuadFinalizeRestoresPreOperationMeshAndSelection) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Undo Slice Quad Box");
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_face(box, 1).ok());
    ASSERT_EQ(editor.selection().summary().selected_face_count, 1u);

    const std::optional<quader::QuaderMeshSnapshot> before = editor.query().mesh(box.mesh);
    ASSERT_TRUE(before.has_value());
    const quader::QuaderHistorySnapshot history_before_preview =
        editor.query().history();

    const quader::QuaderApiVoidResult preview = editor.commands().execute(
        editor.commands().command_id_for_modeling_action("slice_quad"));
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    const std::optional<quader::QuaderMeshSnapshot> preview_mesh =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(preview_mesh.has_value());
    EXPECT_GT(preview_mesh->vertex_count, before->vertex_count);
    EXPECT_GT(preview_mesh->primitive_count, before->primitive_count);

    const quader::QuaderApiVoidResult apply = editor.commands().execute(
        quader::quader_command::kModelingApplyActivePreview);
    ASSERT_TRUE(apply.ok()) << apply.error_message();
    EXPECT_EQ(editor.query().history().undo_label, "Apply Tool Preview");
    const quader::QuaderSelectionSummary committed_selection =
        editor.selection().summary();
    EXPECT_GT(committed_selection.selected_face_count, 1u);

    const quader::QuaderApiVoidResult undo =
        editor.commands().execute(quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(undo.changed());
    EXPECT_TRUE(entity_named(editor, "Undo Slice Quad Box").has_value());
    EXPECT_EQ(editor.selection().summary().selected_face_count, 1u);
    EXPECT_EQ(editor.query().history().undo_label,
              history_before_preview.undo_label);

    const std::optional<quader::QuaderMeshSnapshot> restored = editor.query().mesh(box.mesh);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->vertex_count, before->vertex_count);
    EXPECT_EQ(restored->primitive_count, before->primitive_count);
}

TEST(EditorApiTests, MixedUndoRedoPreviewStackPreservesTexturedMaterial) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Mixed Stack Textured Box");
    const TexturedMaterialRefs refs =
        create_textured_material(editor, "Mixed Stack Material");
    ASSERT_NE(refs.material, quader::kInvalidAssetId);

    const quader::QuaderApiVoidResult assigned =
        editor.assets().assign_material_to_entity(box.entity, refs.material);
    ASSERT_TRUE(assigned.ok()) << assigned.error_message();
    EXPECT_TRUE(assigned.changed());
    expect_entity_textured(editor, box.entity, refs);

    const std::optional<quader::QuaderMeshSnapshot> original = editor.query().mesh(box.mesh);
    ASSERT_TRUE(original.has_value());

    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(editor.selection().select_edge(box, 1, 2).ok());
    ASSERT_TRUE(editor.selection().select_all().ok());
    const quader::QuaderApiVoidResult bevel_preview = editor.commands().execute(
        editor.commands().command_id_for_modeling_action("bevel_edges"));
    ASSERT_TRUE(bevel_preview.ok()) << bevel_preview.error_message();
    EXPECT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());
    const std::optional<quader::QuaderMeshSnapshot> bevel_preview_mesh =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(bevel_preview_mesh.has_value());
    EXPECT_GT(bevel_preview_mesh->primitive_count, original->primitive_count);

    const quader::QuaderApiVoidResult cancel_preview_via_undo =
        editor.commands().execute(quader::quader_command::kEditUndo);
    ASSERT_TRUE(cancel_preview_via_undo.ok())
        << cancel_preview_via_undo.error_message();
    EXPECT_TRUE(cancel_preview_via_undo.changed());
    EXPECT_FALSE(editor.modeling().can_cancel_active_tool_or_preview());
    const std::optional<quader::QuaderMeshSnapshot> after_preview_cancel =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(after_preview_cancel.has_value());
    EXPECT_EQ(after_preview_cancel->vertex_count, original->vertex_count);
    EXPECT_EQ(after_preview_cancel->primitive_count, original->primitive_count);
    expect_entity_textured(editor, box.entity, refs);

    const quader::QuaderApiVoidResult undo_material =
        editor.commands().execute(quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo_material.ok()) << undo_material.error_message();
    EXPECT_TRUE(undo_material.changed());
    const std::optional<quader::QuaderEntitySnapshot> unassigned = editor.query().entity(box.entity);
    ASSERT_TRUE(unassigned.has_value());
    EXPECT_FALSE(unassigned->has_material);

    const quader::QuaderApiVoidResult redo_material =
        editor.commands().execute(quader::quader_command::kEditRedo);
    ASSERT_TRUE(redo_material.ok()) << redo_material.error_message();
    EXPECT_TRUE(redo_material.changed());
    expect_entity_textured(editor, box.entity, refs);

    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_TRUE(editor.selection().select_face(box, 1).ok());
    ASSERT_EQ(editor.selection().summary().selected_face_count, 1u);
    const std::optional<quader::QuaderMeshSnapshot> before_slice =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(before_slice.has_value());

    const quader::QuaderApiVoidResult slice_preview = editor.commands().execute(
        editor.commands().command_id_for_modeling_action("slice_quad"));
    ASSERT_TRUE(slice_preview.ok()) << slice_preview.error_message();
    const quader::QuaderApiVoidResult apply_slice = editor.commands().execute(
        quader::quader_command::kModelingApplyActivePreview);
    ASSERT_TRUE(apply_slice.ok()) << apply_slice.error_message();
    const std::optional<quader::QuaderMeshSnapshot> sliced = editor.query().mesh(box.mesh);
    ASSERT_TRUE(sliced.has_value());
    EXPECT_GT(sliced->vertex_count, before_slice->vertex_count);
    EXPECT_GT(sliced->primitive_count, before_slice->primitive_count);
    EXPECT_GT(editor.selection().summary().selected_face_count, 1u);
    expect_entity_textured(editor, box.entity, refs);

    const quader::QuaderApiVoidResult undo_slice =
        editor.commands().execute(quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo_slice.ok()) << undo_slice.error_message();
    EXPECT_TRUE(undo_slice.changed());
    const std::optional<quader::QuaderMeshSnapshot> after_slice_undo =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(after_slice_undo.has_value());
    EXPECT_EQ(after_slice_undo->vertex_count, before_slice->vertex_count);
    EXPECT_EQ(after_slice_undo->primitive_count, before_slice->primitive_count);
    EXPECT_EQ(editor.selection().summary().selected_face_count, 1u);
    expect_entity_textured(editor, box.entity, refs);

    const quader::QuaderApiVoidResult redo_slice =
        editor.commands().execute(quader::quader_command::kEditRedo);
    ASSERT_TRUE(redo_slice.ok()) << redo_slice.error_message();
    EXPECT_TRUE(redo_slice.changed());
    const std::optional<quader::QuaderMeshSnapshot> after_slice_redo =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(after_slice_redo.has_value());
    EXPECT_EQ(after_slice_redo->vertex_count, sliced->vertex_count);
    EXPECT_EQ(after_slice_redo->primitive_count, sliced->primitive_count);
    EXPECT_GT(editor.selection().summary().selected_face_count, 1u);
    expect_entity_textured(editor, box.entity, refs);

    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
    ASSERT_TRUE(editor.selection().select_object(box).ok());
    const quader::QuaderApiVoidResult delete_selection =
        editor.commands().execute(quader::quader_command::kDeleteSelection);
    ASSERT_TRUE(delete_selection.ok()) << delete_selection.error_message();
    EXPECT_TRUE(delete_selection.changed());
    EXPECT_FALSE(entity_by_id(editor, box.entity).has_value());

    const quader::QuaderApiVoidResult undo_delete =
        editor.commands().execute(quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo_delete.ok()) << undo_delete.error_message();
    EXPECT_TRUE(undo_delete.changed());
    ASSERT_TRUE(entity_by_id(editor, box.entity).has_value());
    const std::optional<quader::QuaderMeshSnapshot> restored_after_delete =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(restored_after_delete.has_value());
    EXPECT_EQ(restored_after_delete->vertex_count, sliced->vertex_count);
    EXPECT_EQ(restored_after_delete->primitive_count, sliced->primitive_count);
    expect_entity_textured(editor, box.entity, refs);

    const quader::QuaderApiVoidResult redo_delete =
        editor.commands().execute(quader::quader_command::kEditRedo);
    ASSERT_TRUE(redo_delete.ok()) << redo_delete.error_message();
    EXPECT_TRUE(redo_delete.changed());
    EXPECT_FALSE(entity_by_id(editor, box.entity).has_value());

    const quader::QuaderApiVoidResult undo_delete_again =
        editor.commands().execute(quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo_delete_again.ok()) << undo_delete_again.error_message();
    EXPECT_TRUE(undo_delete_again.changed());
    ASSERT_TRUE(entity_by_id(editor, box.entity).has_value());
    expect_entity_textured(editor, box.entity, refs);
}

TEST(EditorApiTests, SliceQuadPreviewRebuildsAfterDirectFaceAddWithoutUi) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(harness.editor, "Slice Direct Add Command Box");
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().select_face(box, 1).ok());

    const quader::QuaderApiVoidResult preview =
        harness.editor.commands().execute(
            harness.editor.commands().command_id_for_modeling_action(
                "slice_quad"));
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    const std::optional<quader::QuaderMeshSnapshot> one_face_preview =
        harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(one_face_preview.has_value());

    const quader::QuaderApiVoidResult add_face =
        harness.editor.selection().select_face(
            box, 2, quader::QuaderSelectionEdit::Add);
    ASSERT_TRUE(add_face.ok()) << add_face.error_message();
    EXPECT_TRUE(add_face.changed());
    const quader::QuaderSelectionSummary summary = harness.editor.selection().summary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Face);
    EXPECT_GT(summary.selected_face_count, 1u);

    const std::optional<quader::QuaderMeshSnapshot> two_face_preview =
        harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(two_face_preview.has_value());
    EXPECT_GT(two_face_preview->vertex_count, one_face_preview->vertex_count);
    EXPECT_GT(two_face_preview->primitive_count,
              one_face_preview->primitive_count);
}

TEST(EditorApiTests, DeleteCommandRouteRemovesSameObjectAndExposesUndoLabel) {
    CommandTestHarness harness;
    EXPECT_EQ(harness.editor.commands().command_id_for_modeling_action(
                  "delete_selection"),
              std::string(quader::quader_command::kDeleteSelection.string()));

    const quader::QuaderModelingObjectRef first =
        create_selected_box(harness.editor, "Delete Route First");
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> second =
        harness.editor.modeling().create_box(
            quader::QuaderBounds{.min = {4.0f, -1.0f, -1.0f},
                                 .max = {6.0f, 1.0f, 1.0f}},
            "Delete Route Second");
    ASSERT_TRUE(second.ok()) << second.error_message();
    ASSERT_TRUE(harness.editor.selection().select_object(first).ok());

    const quader::QuaderApiVoidResult deleted =
        harness.editor.commands().execute(
            quader::quader_command::kDeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.error_message();
    EXPECT_TRUE(deleted.changed());
    EXPECT_FALSE(harness.editor.query().entity(first.entity).has_value());
    EXPECT_TRUE(harness.editor.query().entity(second.value().entity).has_value());
    EXPECT_EQ(harness.editor.query().history().undo_label, "Delete Selection");

    const quader::QuaderApiVoidResult undo = harness.editor.history().undo();
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(harness.editor.query().entity(first.entity).has_value());
    EXPECT_TRUE(harness.editor.query().entity(second.value().entity).has_value());
}

TEST(EditorApiTests, RightClickPickOnUnselectedObjectChangesSelectionBeforeContextDelete) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef left =
        create_selected_box(harness.editor, "Context Left");
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
        harness.editor.modeling().create_box(
            quader::QuaderBounds{.min = {4.0f, -1.0f, -1.0f},
                                 .max = {6.0f, 1.0f, 1.0f}},
            "Context Right");
    ASSERT_TRUE(right.ok()) << right.error_message();
    ASSERT_TRUE(harness.editor.selection().select_object(left).ok());

    quader::QuaderPickSelectRequest right_click_pick;
    right_click_pick.ray = {
        .origin = {5.0f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    right_click_pick.preserve_selection_on_miss = true;
    right_click_pick.preserve_selection_on_selected_hit = true;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
        harness.editor.selection().pick_select(right_click_pick);
    ASSERT_TRUE(picked.ok()) << picked.error_message();
    EXPECT_EQ(picked.value().active_object_id,
              right.value().modeling_object_id);

    const quader::QuaderApiVoidResult deleted =
        harness.editor.commands().execute(
            quader::quader_command::kDeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.error_message();
    EXPECT_TRUE(harness.editor.query().entity(left.entity).has_value());
    EXPECT_FALSE(harness.editor.query().entity(right.value().entity).has_value());
}

TEST(EditorApiTests, ObjectPickSelectionRoutesThroughDocumentHistory) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderModelingObjectRef left =
        create_selected_box(editor, "Pick History Left");
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
        editor.modeling().create_box(
            quader::QuaderBounds{.min = {4.0f, -1.0f, -1.0f},
                                 .max = {6.0f, 1.0f, 1.0f}},
            "Pick History Right");
    ASSERT_TRUE(right.ok()) << right.error_message();
    ASSERT_TRUE(editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    ASSERT_TRUE(editor.selection().select_object(left).ok());

    quader::QuaderPickSelectRequest pick_right;
    pick_right.ray = {
        .origin = {5.0f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
        editor.selection().pick_select(pick_right);
    ASSERT_TRUE(picked.ok()) << picked.error_message();
    EXPECT_TRUE(picked.changed());
    EXPECT_EQ(picked.mutation_kind, quader::QuaderMutationKind::Document);
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{right.value().entity});
    EXPECT_EQ(editor.query().history().undo_label, "Select Entities");

    const quader::QuaderApiVoidResult undo = editor.history().undo();
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(undo.changed());
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{left.entity});

    const quader::QuaderApiVoidResult redo = editor.history().redo();
    ASSERT_TRUE(redo.ok()) << redo.error_message();
    EXPECT_TRUE(redo.changed());
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{right.value().entity});
}

TEST(EditorApiTests, BoxSelectApiSelectsContainedObjectByViewportRectangle) {
    CommandTestHarness harness;
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> left =
        harness.editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Box Select Left");
    ASSERT_TRUE(left.ok()) << left.error_message();
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
        harness.editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Box Select Right");
    ASSERT_TRUE(right.ok()) << right.error_message();
    ASSERT_TRUE(harness.editor.scene()
                    .set_transform(left.value().entity,
                                   quader::QuaderTransform{
                                       .position = {-20.0f, 0.0f, 0.0f}})
                    .ok());
    ASSERT_TRUE(harness.editor.scene()
                    .set_transform(right.value().entity,
                                   quader::QuaderTransform{
                                       .position = {20.0f, 0.0f, 0.0f}})
                    .ok());
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().clear().ok());

    quader::QuaderBoxSelectRequest request;
    request.camera = api_orthographic_camera();
    request.start = {25.0f, 45.0f};
    request.end = {35.0f, 55.0f};
    request.edit = quader::QuaderSelectionEdit::Replace;
    request.rule = quader::QuaderBoxSelectionRule::Contain;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> selected =
        harness.editor.selection().box_select(request);

    ASSERT_TRUE(selected.ok()) << selected.error_message();
    EXPECT_EQ(selected.value().mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(selected.value().selected_object_count, 1u);
    EXPECT_EQ(selected_entities(harness.editor),
              std::vector<quader::EntityId>{left.value().entity});
}

TEST(EditorApiTests, ObjectBoxSelectionRoutesThroughDocumentHistory) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> left =
        editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Box History Left");
    ASSERT_TRUE(left.ok()) << left.error_message();
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
        editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Box History Right");
    ASSERT_TRUE(right.ok()) << right.error_message();
    ASSERT_TRUE(editor.scene()
                    .set_transform(left.value().entity,
                                   quader::QuaderTransform{
                                       .position = {-20.0f, 0.0f, 0.0f}})
                    .ok());
    ASSERT_TRUE(editor.scene()
                    .set_transform(right.value().entity,
                                   quader::QuaderTransform{
                                       .position = {20.0f, 0.0f, 0.0f}})
                    .ok());
    ASSERT_TRUE(editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    ASSERT_TRUE(editor.selection().select_object(right.value()).ok());

    quader::QuaderBoxSelectRequest request;
    request.camera = api_orthographic_camera();
    request.start = {25.0f, 45.0f};
    request.end = {35.0f, 55.0f};
    request.edit = quader::QuaderSelectionEdit::Replace;
    request.rule = quader::QuaderBoxSelectionRule::Contain;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> selected =
        editor.selection().box_select(request);

    ASSERT_TRUE(selected.ok()) << selected.error_message();
    EXPECT_TRUE(selected.changed());
    EXPECT_EQ(selected.mutation_kind, quader::QuaderMutationKind::Document);
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{left.value().entity});
    EXPECT_EQ(editor.query().history().undo_label, "Select Entities");

    const quader::QuaderApiVoidResult undo = editor.history().undo();
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(undo.changed());
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{right.value().entity});

    const quader::QuaderApiVoidResult redo = editor.history().redo();
    ASSERT_TRUE(redo.ok()) << redo.error_message();
    EXPECT_TRUE(redo.changed());
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{left.value().entity});
}

TEST(EditorApiTests, BoxSelectApiTogglesObjectSelection) {
    CommandTestHarness harness;
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> left =
        harness.editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Box Toggle Left");
    ASSERT_TRUE(left.ok()) << left.error_message();
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
        harness.editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Box Toggle Right");
    ASSERT_TRUE(right.ok()) << right.error_message();
    ASSERT_TRUE(harness.editor.scene()
                    .set_transform(left.value().entity,
                                   quader::QuaderTransform{
                                       .position = {-20.0f, 0.0f, 0.0f}})
                    .ok());
    ASSERT_TRUE(harness.editor.scene()
                    .set_transform(right.value().entity,
                                   quader::QuaderTransform{
                                       .position = {20.0f, 0.0f, 0.0f}})
                    .ok());
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().select_object(left.value()).ok());

    quader::QuaderBoxSelectRequest request;
    request.camera = api_orthographic_camera();
    request.start = {25.0f, 45.0f};
    request.end = {35.0f, 55.0f};
    request.edit = quader::QuaderSelectionEdit::Toggle;
    request.rule = quader::QuaderBoxSelectionRule::Contain;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> removed =
        harness.editor.selection().box_select(request);

    ASSERT_TRUE(removed.ok()) << removed.error_message();
    EXPECT_EQ(removed.value().mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(removed.value().selected_object_count, 0u);
    EXPECT_TRUE(selected_entities(harness.editor).empty());
}

TEST(EditorApiTests, BoxSelectApiPreservesActiveFaceFromBaseline) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef object =
        create_selected_box(harness.editor, "Face Baseline Box");
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().select_face(object, 1).ok());
    quader::QuaderSelectionSummary summary = harness.editor.selection().summary();
    ASSERT_EQ(summary.mode, quader::QuaderSelectionMode::Face);
    ASSERT_EQ(summary.active_face_id, 1);
    ASSERT_EQ(summary.selected_face_count, 1u);

    quader::QuaderBoxSelectRequest request;
    request.camera = api_orthographic_camera();
    request.start = {0.0f, 0.0f};
    request.end = {1.0f, 1.0f};
    request.edit = quader::QuaderSelectionEdit::Add;
    request.rule = quader::QuaderBoxSelectionRule::Contain;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> selected =
        harness.editor.selection().box_select(request);

    ASSERT_TRUE(selected.ok()) << selected.error_message();
    EXPECT_EQ(selected.value().mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(selected.value().active_kind,
              quader::QuaderActiveSelectionKind::Face);
    EXPECT_EQ(selected.value().active_face_id, 1);
    EXPECT_EQ(selected.value().selected_face_count, 1u);
}

TEST(EditorApiTests,
     ComponentBoxSelectionPublishesActiveEdgeAndVertexForLocalOrientation) {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderModelingObjectRef object =
        create_polygon_mesh(editor, 3U, "Local Rectangle Triangle");
    ASSERT_TRUE(editor.selection().select_object(object).ok());
    ASSERT_TRUE(editor.viewports()
                    .set_transform_space(quader::QuaderTransformSpace::Local)
                    .ok());

    const auto expect_same_orientation =
        [](const quader::QuaderTransform3 &actual,
           const quader::QuaderTransform3 &expected) {
          EXPECT_NEAR(actual.x_axis.x, expected.x_axis.x, 1.0e-5f);
          EXPECT_NEAR(actual.x_axis.y, expected.x_axis.y, 1.0e-5f);
          EXPECT_NEAR(actual.x_axis.z, expected.x_axis.z, 1.0e-5f);
          EXPECT_NEAR(actual.y_axis.x, expected.y_axis.x, 1.0e-5f);
          EXPECT_NEAR(actual.y_axis.y, expected.y_axis.y, 1.0e-5f);
          EXPECT_NEAR(actual.y_axis.z, expected.y_axis.z, 1.0e-5f);
          EXPECT_NEAR(actual.z_axis.x, expected.z_axis.x, 1.0e-5f);
          EXPECT_NEAR(actual.z_axis.y, expected.z_axis.y, 1.0e-5f);
          EXPECT_NEAR(actual.z_axis.z, expected.z_axis.z, 1.0e-5f);
        };

    quader::QuaderBoxSelectRequest request;
    request.camera = api_orthographic_camera();
    request.start = {48.0f, 48.0f};
    request.end = {52.0f, 52.0f};
    request.edit = quader::QuaderSelectionEdit::Add;
    request.rule = quader::QuaderBoxSelectionRule::Contain;

    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_TRUE(editor.selection().select_edge(object, 1, 2).ok());
    const quader::QuaderApiResult<quader::QuaderSelectionSummary>
        rectangle_edge = editor.selection().box_select(request);
    ASSERT_TRUE(rectangle_edge.ok()) << rectangle_edge.error_message();
    EXPECT_EQ(rectangle_edge.value().selected_edge_count, 3u);
    EXPECT_EQ(rectangle_edge.value().active_kind,
              quader::QuaderActiveSelectionKind::Edge);
    EXPECT_EQ(rectangle_edge.value().active_edge_a, 1);
    EXPECT_EQ(rectangle_edge.value().active_edge_b, 2);
    const std::optional<quader::QuaderTransform3> rectangle_edge_orientation =
        editor.query().selection_transform_orientation();
    ASSERT_TRUE(rectangle_edge_orientation.has_value());
    EXPECT_GT(std::abs(rectangle_edge_orientation->x_axis.y), 0.25f);

    ASSERT_TRUE(editor.selection().select_edge(object, 1, 2).ok());
    const std::optional<quader::QuaderTransform3> direct_edge_orientation =
        editor.query().selection_transform_orientation();
    ASSERT_TRUE(direct_edge_orientation.has_value());
    expect_same_orientation(*rectangle_edge_orientation,
                            *direct_edge_orientation);

    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_TRUE(editor.selection().select_vertex(object, 1).ok());
    const quader::QuaderApiResult<quader::QuaderSelectionSummary>
        rectangle_vertex = editor.selection().box_select(request);
    ASSERT_TRUE(rectangle_vertex.ok()) << rectangle_vertex.error_message();
    EXPECT_EQ(rectangle_vertex.value().selected_vertex_count, 3u);
    EXPECT_EQ(rectangle_vertex.value().active_kind,
              quader::QuaderActiveSelectionKind::Vertex);
    EXPECT_EQ(rectangle_vertex.value().active_vertex_id, 1);
    const std::optional<quader::QuaderTransform3>
        rectangle_vertex_orientation =
            editor.query().selection_transform_orientation();
    ASSERT_TRUE(rectangle_vertex_orientation.has_value());
    EXPECT_GT(std::abs(rectangle_vertex_orientation->x_axis.y), 0.25f);

    ASSERT_TRUE(editor.selection().select_vertex(object, 1).ok());
    const std::optional<quader::QuaderTransform3> direct_vertex_orientation =
        editor.query().selection_transform_orientation();
    ASSERT_TRUE(direct_vertex_orientation.has_value());
    expect_same_orientation(*rectangle_vertex_orientation,
                            *direct_vertex_orientation);
}

TEST(EditorApiTests, ComponentPickSelectionRemainsRuntimeOwned) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    (void)create_selected_box(editor, "Runtime Pick Component Box");
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
    const quader::QuaderHistorySnapshot history_before_pick =
        editor.query().history();

    quader::QuaderPickSelectRequest pick_face;
    pick_face.ray = {
        .origin = {0.0f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
        editor.selection().pick_select(pick_face);

    ASSERT_TRUE(picked.ok()) << picked.error_message();
    EXPECT_TRUE(picked.changed());
    EXPECT_EQ(picked.mutation_kind, quader::QuaderMutationKind::Modeling);
    EXPECT_EQ(picked.value().selected_face_count, 1u);
    EXPECT_EQ(editor.query().history().can_undo,
              history_before_pick.can_undo);
    EXPECT_EQ(editor.query().history().undo_label,
              history_before_pick.undo_label);
    EXPECT_EQ(editor.query().history().redo_label,
              history_before_pick.redo_label);
}

TEST(EditorApiTests, GroupModeBoxSelectsContainingGroupByViewportRectangle) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef left =
        create_selected_box(harness.editor, "Box Group Left");
    ASSERT_TRUE(harness.editor.scene()
                    .set_transform(left.entity,
                                   quader::QuaderTransform{
                                       .position = {-20.0f, 0.0f, 0.0f}})
                    .ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> left_group =
        harness.editor.scene().group_selection("Box Group A");
    ASSERT_TRUE(left_group.ok()) << left_group.error_message();

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
        harness.editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Box Group Right");
    ASSERT_TRUE(right.ok()) << right.error_message();
    ASSERT_TRUE(harness.editor.scene()
                    .set_transform(right.value().entity,
                                   quader::QuaderTransform{
                                       .position = {20.0f, 0.0f, 0.0f}})
                    .ok());
    ASSERT_TRUE(harness.editor.selection().select_object(right.value()).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> right_group =
        harness.editor.scene().group_selection("Box Group B");
    ASSERT_TRUE(right_group.ok()) << right_group.error_message();

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Group)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().clear().ok());
    quader::QuaderBoxSelectRequest request;
    request.camera = api_orthographic_camera();
    request.start = {25.0f, 45.0f};
    request.end = {35.0f, 55.0f};
    request.edit = quader::QuaderSelectionEdit::Replace;
    request.rule = quader::QuaderBoxSelectionRule::Contain;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> selected =
        harness.editor.selection().box_select(request);

    ASSERT_TRUE(selected.ok()) << selected.error_message();
    EXPECT_EQ(selected.value().mode, quader::QuaderSelectionMode::Group);
    EXPECT_EQ(selected.value().selected_object_count, 1u);
    const std::optional<quader::QuaderEntitySnapshot> left_group_snapshot =
        entity_by_id(harness.editor, left_group.value().id);
    const std::optional<quader::QuaderEntitySnapshot> right_group_snapshot =
        entity_by_id(harness.editor, right_group.value().id);
    ASSERT_TRUE(left_group_snapshot.has_value());
    ASSERT_TRUE(right_group_snapshot.has_value());
    EXPECT_TRUE(left_group_snapshot->selected);
    EXPECT_TRUE(left_group_snapshot->active);
    EXPECT_FALSE(right_group_snapshot->selected);
}

TEST(EditorApiTests, RightClickPickWithExistingFaceSelectionPreservesComponentSelectionBeforeContextDelete) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef left =
        create_selected_box(harness.editor, "Context Face Left");
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
        harness.editor.modeling().create_box(
            quader::QuaderBounds{.min = {4.0f, -1.0f, -1.0f},
                                 .max = {6.0f, 1.0f, 1.0f}},
            "Context Face Right");
    ASSERT_TRUE(right.ok()) << right.error_message();
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face)
                    .ok());
    quader::QuaderPickSelectRequest pick_left_front;
    pick_left_front.ray = {
        .origin = {0.0f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    const quader::QuaderApiResult<quader::QuaderSelectionSummary>
        picked_left_front =
            harness.editor.selection().pick_select(pick_left_front);
    ASSERT_TRUE(picked_left_front.ok()) << picked_left_front.error_message();
    ASSERT_EQ(picked_left_front.value().active_object_id,
              left.modeling_object_id);
    ASSERT_EQ(picked_left_front.value().selected_face_count, 1u);

    quader::QuaderPickSelectRequest pick_left_back;
    pick_left_back.ray = {
        .origin = {0.0f, 0.0f, -5.0f},
        .direction = {0.0f, 0.0f, 1.0f},
    };
    pick_left_back.shift_pressed = true;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary>
        picked_left_back =
            harness.editor.selection().pick_select(pick_left_back);
    ASSERT_TRUE(picked_left_back.ok()) << picked_left_back.error_message();
    ASSERT_EQ(picked_left_back.value().active_object_id,
              left.modeling_object_id);
    ASSERT_EQ(picked_left_back.value().selected_face_count, 2u);
    const int active_face_before_context =
        picked_left_back.value().active_face_id;
    const std::optional<quader::QuaderMeshAsset> left_before =
        harness.editor.query().compiled_object_mesh(left);
    ASSERT_TRUE(left_before.has_value());
    const std::optional<quader::QuaderMeshAsset> right_before =
        harness.editor.query().compiled_object_mesh(right.value());
    ASSERT_TRUE(right_before.has_value());

    quader::QuaderPickSelectRequest right_click_pick;
    right_click_pick.ray = {
        .origin = {5.0f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    right_click_pick.preserve_selection_on_miss = true;
    right_click_pick.preserve_selection_on_selected_hit = true;
    right_click_pick.preserve_existing_component_selection_on_hit = true;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
        harness.editor.selection().pick_select(right_click_pick);
    ASSERT_TRUE(picked.ok()) << picked.error_message();
    EXPECT_EQ(picked.value().mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(picked.value().active_object_id, left.modeling_object_id);
    EXPECT_EQ(picked.value().active_face_id, active_face_before_context);
    EXPECT_EQ(picked.value().selected_face_count, 2u);

    const quader::QuaderApiVoidResult deleted =
        harness.editor.commands().execute(
            quader::quader_command::kDeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.error_message();
    EXPECT_TRUE(harness.editor.query().entity(left.entity).has_value());
    EXPECT_TRUE(harness.editor.query().entity(right.value().entity).has_value());
    const std::optional<quader::QuaderMeshAsset> left_after =
        harness.editor.query().compiled_object_mesh(left);
    ASSERT_TRUE(left_after.has_value());
    EXPECT_LT(left_after->indices.size(), left_before->indices.size());
    const std::optional<quader::QuaderMeshAsset> right_after =
        harness.editor.query().compiled_object_mesh(right.value());
    ASSERT_TRUE(right_after.has_value());
    EXPECT_EQ(right_after->indices.size(), right_before->indices.size());
}

TEST(EditorApiTests, GroupModeRightClickPickSelectsGroupBeforeContextDelete) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef left =
        create_selected_box(harness.editor, "Context Group Left");
    const quader::QuaderApiResult<quader::QuaderEntityRef> left_group =
        harness.editor.scene().group_selection("Context Group A");
    ASSERT_TRUE(left_group.ok()) << left_group.error_message();

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
        harness.editor.modeling().create_box(
            quader::QuaderBounds{.min = {4.0f, -1.0f, -1.0f},
                                 .max = {6.0f, 1.0f, 1.0f}},
            "Context Group Right");
    ASSERT_TRUE(right.ok()) << right.error_message();
    ASSERT_TRUE(harness.editor.selection().select_object(right.value()).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> right_group =
        harness.editor.scene().group_selection("Context Group B");
    ASSERT_TRUE(right_group.ok()) << right_group.error_message();

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Group)
                    .ok());
    ASSERT_TRUE(
        harness.editor.selection().select_object(left_group.value().id).ok());

    quader::QuaderPickSelectRequest right_click_pick;
    right_click_pick.ray = {
        .origin = {5.0f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    right_click_pick.preserve_selection_on_miss = true;
    right_click_pick.preserve_selection_on_selected_hit = true;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
        harness.editor.selection().pick_select(right_click_pick);
    ASSERT_TRUE(picked.ok()) << picked.error_message();
    const std::optional<quader::QuaderEntitySnapshot> picked_group =
        entity_by_id(harness.editor, right_group.value().id);
    ASSERT_TRUE(picked_group.has_value());
    EXPECT_TRUE(picked_group->active);

    const quader::QuaderApiVoidResult deleted =
        harness.editor.commands().execute(
            quader::quader_command::kDeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.error_message();
    EXPECT_TRUE(
        harness.editor.query().entity(left_group.value().id).has_value());
    EXPECT_TRUE(harness.editor.query().entity(left.entity).has_value());
    EXPECT_FALSE(
        harness.editor.query().entity(right_group.value().id).has_value());
    EXPECT_FALSE(harness.editor.query().entity(right.value().entity).has_value());
}

TEST(EditorApiTests, ViewportHandlesUseWorkspaceWithoutRuntimeHooks) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiVoidResult quad_layout =
        editor.viewports().set_layout(quader::ViewportLayoutId::FourQuad);
    EXPECT_TRUE(quad_layout.ok()) << quad_layout.error_message();
    EXPECT_EQ(editor.viewports().layout(), quader::ViewportLayoutId::FourQuad);
    EXPECT_EQ(editor.viewports().active_pane_count(), 4u);

    EXPECT_TRUE(editor.viewports().set_grid_preset(4).ok());
    EXPECT_EQ(editor.viewports().grid_preset(), 4);
    EXPECT_TRUE(editor.viewports().set_grid_preset(999).ok());
    EXPECT_EQ(editor.viewports().grid_preset(), 10);
    EXPECT_TRUE(editor.viewports().set_grid_preset(-5).ok());
    EXPECT_EQ(editor.viewports().grid_preset(), 1);

    EXPECT_EQ(editor.viewports().transform_space(),
              quader::QuaderTransformSpace::Global);
    EXPECT_EQ(editor.viewports().transform_space_revision(), 0u);
    const quader::QuaderApiVoidResult unchanged_space =
        editor.viewports().set_transform_space(
            quader::QuaderTransformSpace::Global);
    EXPECT_TRUE(unchanged_space.ok());
    EXPECT_FALSE(unchanged_space.changed());
    EXPECT_EQ(editor.viewports().transform_space_revision(), 0u);
    const quader::QuaderApiVoidResult local_space =
        editor.viewports().set_transform_space(
            quader::QuaderTransformSpace::Local);
    EXPECT_TRUE(local_space.ok());
    EXPECT_TRUE(local_space.changed());
    EXPECT_EQ(local_space.mutation_kind, quader::QuaderMutationKind::Runtime);
    EXPECT_EQ(editor.viewports().transform_space(),
              quader::QuaderTransformSpace::Local);
    EXPECT_EQ(editor.viewports().transform_space_revision(), 1u);
    EXPECT_TRUE(editor.viewports().toggle_transform_space().changed());
    EXPECT_EQ(editor.viewports().transform_space(),
              quader::QuaderTransformSpace::Global);
    EXPECT_EQ(editor.viewports().transform_space_revision(), 2u);

    auto viewport = editor.viewports().main();
    EXPECT_TRUE(
        viewport.set_shading_mode(quader::QuaderShadingMode::Wireframe).ok());
    EXPECT_EQ(
        editor.viewports().shading_mode(quader::ViewportPaneId::Perspective),
        quader::QuaderShadingMode::Wireframe);
    EXPECT_TRUE(
        viewport.set_shading_mode(quader::QuaderShadingMode::Material).ok());
    EXPECT_EQ(
        editor.viewports().shading_mode(quader::ViewportPaneId::Perspective),
        quader::QuaderShadingMode::Material);
    EXPECT_TRUE(
        viewport.set_shading_mode(quader::QuaderShadingMode::Solid).ok());
    EXPECT_EQ(
        editor.viewports().shading_mode(quader::ViewportPaneId::Perspective),
        quader::QuaderShadingMode::Solid);
    constexpr std::array<quader::QuaderShadingMode, 6> kDebugModes{
        quader::QuaderShadingMode::DiffuseOnly,
        quader::QuaderShadingMode::SpecularOnly,
        quader::QuaderShadingMode::Albedo,
        quader::QuaderShadingMode::Roughness,
        quader::QuaderShadingMode::Metalness,
        quader::QuaderShadingMode::Normal,
    };
    for (quader::QuaderShadingMode mode : kDebugModes) {
      EXPECT_TRUE(viewport.set_shading_mode(mode).ok());
      EXPECT_EQ(editor.viewports().shading_mode(
                    quader::ViewportPaneId::Perspective),
                mode);
    }
    EXPECT_FALSE(
        viewport.set_shading_mode(quader::QuaderShadingMode::XRay).ok());
    EXPECT_EQ(
        editor.viewports().shading_mode(quader::ViewportPaneId::Perspective),
        quader::QuaderShadingMode::Normal);

    EXPECT_TRUE(viewport.set_view(quader::ViewportView::Top).ok());
    EXPECT_EQ(editor.viewports().view(quader::ViewportPaneId::Perspective), quader::ViewportView::Top);
    EXPECT_TRUE(editor.commands()
                    .execute(quader::quader_command::kViewportViewPerspective)
                    .ok());
    EXPECT_EQ(editor.viewports().view(quader::ViewportPaneId::Perspective), quader::ViewportView::Perspective);

    EXPECT_TRUE(viewport.camera().focus_origin().ok());
    EXPECT_FALSE(viewport.camera().frame_selection().ok());
    EXPECT_FALSE(editor.viewports().spawn_window().ok());
}

TEST(EditorApiTests, ViewportFocusCommandsUseRuntimeHooksForFocusedPane) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    ASSERT_TRUE(
        editor.viewports().set_layout(quader::ViewportLayoutId::FourQuad).ok());
    quader::ViewportWorkspaceService& workspace =
            quader::QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
    ASSERT_TRUE(workspace.focus_pane(quader::ViewportPaneId::Top));

    int frame_all_requests = 0;
    int frame_selection_requests = 0;
    quader::ViewportPaneId requested_pane = quader::ViewportPaneId::Perspective;
    editor.viewports().set_runtime_hooks(quader::QuaderViewportRuntimeHooks{
        .frame_all =
            [&](quader::ViewportPaneId pane) {
              ++frame_all_requests;
              requested_pane = pane;
              return quader::QuaderApiVoidResult{
                  .ok = true,
                  .changed = true,
                  .message = "Focus All",
                  .mutation_kind = quader::QuaderMutationKind::Runtime,
                  .revisions = editor.revisions(),
              };
            },
        .frame_selection =
            [&](quader::ViewportPaneId pane) {
              ++frame_selection_requests;
              requested_pane = pane;
              return quader::QuaderApiVoidResult{
                  .ok = true,
                  .changed = true,
                  .message = "Focus Selection",
                  .mutation_kind = quader::QuaderMutationKind::Runtime,
                  .revisions = editor.revisions(),
              };
            },
    });

    const quader::QuaderApiVoidResult focus_all_result =
        editor.commands().execute(quader::quader_command::kViewportFocusAll);

    ASSERT_TRUE(focus_all_result.ok()) << focus_all_result.error_message();
    EXPECT_TRUE(focus_all_result.changed());
    EXPECT_EQ(focus_all_result.error_message(), "Focus All");
    EXPECT_EQ(frame_all_requests, 1);
    EXPECT_EQ(requested_pane, quader::ViewportPaneId::Top);
    ASSERT_TRUE(editor.commands().last_status().has_value());
    expect_same_api_status(*editor.commands().last_status(), focus_all_result);

    requested_pane = quader::ViewportPaneId::Perspective;
    const quader::QuaderApiVoidResult result =
        editor.commands().execute(quader::quader_command::kViewportFrameSelection);

    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    EXPECT_EQ(result.error_message(), "Focus Selection");
    EXPECT_EQ(frame_selection_requests, 1);
    EXPECT_EQ(requested_pane, quader::ViewportPaneId::Top);
    ASSERT_TRUE(editor.commands().last_status().has_value());
    expect_same_api_status(*editor.commands().last_status(), result);
}

TEST(EditorApiTests, ViewportViewCommandsTargetFocusedPane) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    ASSERT_TRUE(
        editor.viewports().set_layout(quader::ViewportLayoutId::FourQuad).ok());
    quader::ViewportWorkspaceService& workspace =
            quader::QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
    ASSERT_TRUE(workspace.focus_pane(quader::ViewportPaneId::Top));
    ASSERT_EQ(editor.viewports().focused_pane(), quader::ViewportPaneId::Top);
    ASSERT_EQ(editor.viewports().view(quader::ViewportPaneId::Perspective), quader::ViewportView::Perspective);

    const quader::QuaderApiVoidResult result =
        editor.commands().execute(quader::quader_command::kViewportViewFront);
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    EXPECT_EQ(editor.viewports().view(quader::ViewportPaneId::Top), quader::ViewportView::Front);
    EXPECT_EQ(editor.viewports().view(quader::ViewportPaneId::Perspective), quader::ViewportView::Perspective);
}

TEST(EditorApiTests, ObjectHandlesSelectionConveniencesAndSceneBatchesUsePublicApi) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
        editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Handle Box");
    ASSERT_TRUE(created.ok()) << created.error_message();

    quader::QuaderObjectHandle box = editor.object(created.value());
    EXPECT_TRUE(box.rename("Public Handle Box").ok());
    EXPECT_TRUE(box.set_visible(false).ok());
    EXPECT_TRUE(box.set_locked(true).ok());
    EXPECT_TRUE(box.set_locked(false).ok());
    EXPECT_TRUE(editor.selection().select_object(created.value().entity).ok());
    EXPECT_TRUE(editor.selection()
                    .select_objects({created.value()},
                                    quader::QuaderSelectionEdit::Replace)
                    .ok());
    EXPECT_TRUE(
        editor.selection().select_vertices(created.value(), {0, 1}).ok());

    const quader::QuaderApiResult<quader::QuaderEntityRef> duplicate = box.duplicate();
    EXPECT_FALSE(duplicate.ok());
    EXPECT_NE(duplicate.error_message().find("modeling object"),
              std::string::npos);

    quader::QuaderCreateEntityArgs first_args;
    first_args.name = "First";
    const quader::QuaderApiResult<quader::QuaderEntityRef> first =
        editor.scene().create_entity(first_args);
    ASSERT_TRUE(first.ok()) << first.error_message();
    EXPECT_NE(first.value().editor_object_id, quader::kInvalidEditorObjectId);
    const std::optional<quader::QuaderEntitySnapshot> first_snapshot =
        editor.query().entity(first.value().id);
    ASSERT_TRUE(first_snapshot.has_value());
    EXPECT_EQ(first_snapshot->editor_object_id, first.value().editor_object_id);

    quader::QuaderCreateEntityArgs second_args;
    second_args.name = "Second";
    const quader::QuaderApiResult<quader::QuaderEntityRef> second =
        editor.scene().create_entity(second_args);
    ASSERT_TRUE(second.ok()) << second.error_message();

    quader::QuaderTransform moved;
    moved.position = {3.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult batch =
        editor.scene()
            .batch("Arrange")
            .rename(first.value().id, "Renamed First")
            .set_transform(first.value().id, moved)
            .set_visible(second.value().id, false)
            .set_locked(second.value().id, true)
            .commit();
    ASSERT_TRUE(batch.ok()) << batch.error_message();
    EXPECT_TRUE(batch.changed());

    std::optional<quader::QuaderEntitySnapshot> first_after_batch =
        entity_by_id(editor, first.value().id);
    ASSERT_TRUE(first_after_batch.has_value());
    EXPECT_EQ(first_after_batch->name, "Renamed First");
    EXPECT_EQ(first_after_batch->transform.position.x, 3.0f);
    std::optional<quader::QuaderEntitySnapshot> second_after_batch =
        entity_by_id(editor, second.value().id);
    ASSERT_TRUE(second_after_batch.has_value());
    EXPECT_FALSE(second_after_batch->visible);
    EXPECT_TRUE(second_after_batch->locked);

    EXPECT_TRUE(editor.history().undo().ok());
    first_after_batch = entity_by_id(editor, first.value().id);
    ASSERT_TRUE(first_after_batch.has_value());
    EXPECT_EQ(first_after_batch->name, "First");
    second_after_batch = entity_by_id(editor, second.value().id);
    ASSERT_TRUE(second_after_batch.has_value());
    EXPECT_TRUE(second_after_batch->visible);
    EXPECT_FALSE(second_after_batch->locked);
}

TEST(EditorApiTests, SceneBatchUsesPendingStateForRepeatedEntityEdits) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs args;
    args.name = "Repeated Batch Entity";
    const quader::QuaderApiResult<quader::QuaderEntityRef> entity =
        editor.scene().create_entity(args);
    ASSERT_TRUE(entity.ok()) << entity.error_message();

    quader::QuaderTransform first_move;
    first_move.position = {1.0f, 0.0f, 0.0f};
    quader::QuaderTransform second_move;
    second_move.position = {2.0f, 0.0f, 0.0f};

    const quader::QuaderApiVoidResult batch =
        editor.scene()
            .batch("Repeated Batch")
            .set_visible(entity.value().id, false)
            .set_visible(entity.value().id, true)
            .set_locked(entity.value().id, true)
            .set_locked(entity.value().id, false)
            .set_transform(entity.value().id, first_move)
            .set_transform(entity.value().id, second_move)
            .commit();
    ASSERT_TRUE(batch.ok()) << batch.error_message();
    EXPECT_TRUE(batch.changed());

    std::optional<quader::QuaderEntitySnapshot> snapshot =
        entity_by_id(editor, entity.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->visible);
    EXPECT_FALSE(snapshot->locked);
    EXPECT_EQ(snapshot->transform.position.x, 2.0f);

    ASSERT_TRUE(editor.history().undo().ok());
    snapshot = entity_by_id(editor, entity.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->visible);
    EXPECT_FALSE(snapshot->locked);
    EXPECT_EQ(snapshot->transform.position.x, 0.0f);

    ASSERT_TRUE(editor.history().redo().ok());
    snapshot = entity_by_id(editor, entity.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->visible);
    EXPECT_FALSE(snapshot->locked);
    EXPECT_EQ(snapshot->transform.position.x, 2.0f);
}

TEST(EditorApiTests, SceneBatchTransformLockChecksUsePendingParentAndLockState) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs locked_args;
    locked_args.name = "Batch Unlock Then Transform";
    const quader::QuaderApiResult<quader::QuaderEntityRef> locked_entity =
        editor.scene().create_entity(locked_args);
    ASSERT_TRUE(locked_entity.ok()) << locked_entity.error_message();
    ASSERT_TRUE(editor.scene().set_locked(locked_entity.value().id, true).ok());

    quader::QuaderTransform moved_after_unlock;
    moved_after_unlock.position = {5.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult unlock_then_move =
        editor.scene()
            .batch("Unlock Then Move")
            .set_locked(locked_entity.value().id, false)
            .set_transform(locked_entity.value().id, moved_after_unlock)
            .commit();
    ASSERT_TRUE(unlock_then_move.ok()) << unlock_then_move.error_message();
    EXPECT_TRUE(unlock_then_move.changed());

    std::optional<quader::QuaderEntitySnapshot> snapshot =
        entity_by_id(editor, locked_entity.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FALSE(snapshot->locked);
    EXPECT_NEAR(snapshot->transform.position.x, 5.0f, 0.001f);

    quader::QuaderCreateEntityArgs parent_args;
    parent_args.name = "Batch Locked Parent";
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
        editor.scene().create_entity(parent_args);
    ASSERT_TRUE(parent.ok()) << parent.error_message();
    ASSERT_TRUE(editor.scene().set_locked(parent.value().id, true).ok());

    quader::QuaderCreateEntityArgs child_args;
    child_args.name = "Batch Reparent Then Transform";
    const quader::QuaderApiResult<quader::QuaderEntityRef> child =
        editor.scene().create_entity(child_args);
    ASSERT_TRUE(child.ok()) << child.error_message();

    quader::QuaderTransform skipped_move;
    skipped_move.position = {9.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult reparent_then_move =
        editor.scene()
            .batch("Reparent Then Move")
            .reparent(child.value().id, parent.value().id)
            .set_transform(child.value().id, skipped_move)
            .commit();
    ASSERT_TRUE(reparent_then_move.ok()) << reparent_then_move.error_message();
    EXPECT_TRUE(reparent_then_move.changed());

    snapshot = entity_by_id(editor, child.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->parent, parent.value().id);
    EXPECT_NEAR(snapshot->transform.position.x, 0.0f, 0.001f);
}

TEST(EditorApiTests, LockingSelectedEntitiesClearsSelectionAndUndoRestoresIt) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs parent_args;
    parent_args.name = "Lock Parent";
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
        editor.scene().create_entity(parent_args);
    ASSERT_TRUE(parent.ok()) << parent.error_message();

    quader::QuaderCreateEntityArgs child_args;
    child_args.name = "Lock Child";
    child_args.parent = parent.value().id;
    const quader::QuaderApiResult<quader::QuaderEntityRef> child =
        editor.scene().create_entity(child_args);
    ASSERT_TRUE(child.ok()) << child.error_message();

    ASSERT_TRUE(editor.selection()
                    .select_objects({child.value().id}, child.value().id)
                    .ok());
    ASSERT_TRUE(entity_by_id(editor, child.value().id)->selected);
    ASSERT_TRUE(entity_by_id(editor, child.value().id)->active);

    const quader::QuaderApiVoidResult locked =
        editor.scene().set_locked(parent.value().id, true);
    ASSERT_TRUE(locked.ok()) << locked.error_message();
    EXPECT_TRUE(locked.changed());

    std::optional<quader::QuaderEntitySnapshot> child_snapshot =
        entity_by_id(editor, child.value().id);
    ASSERT_TRUE(child_snapshot.has_value());
    EXPECT_FALSE(child_snapshot->selected);
    EXPECT_FALSE(child_snapshot->active);
    EXPECT_FALSE(editor.query().selection().has_selection);

    ASSERT_TRUE(editor.history().undo().ok());
    child_snapshot = entity_by_id(editor, child.value().id);
    ASSERT_TRUE(child_snapshot.has_value());
    EXPECT_TRUE(child_snapshot->selected);
    EXPECT_TRUE(child_snapshot->active);
    EXPECT_TRUE(editor.query().selection().has_selection);

    ASSERT_TRUE(editor.history().redo().ok());
    child_snapshot = entity_by_id(editor, child.value().id);
    ASSERT_TRUE(child_snapshot.has_value());
    EXPECT_FALSE(child_snapshot->selected);
    EXPECT_FALSE(child_snapshot->active);
}

TEST(EditorApiTests, LockingParentClearsSelectedModelingDescendantAndUndoRestoresIt) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs parent_args;
    parent_args.name = "Lock Mesh Parent";
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
        editor.scene().create_entity(parent_args);
    ASSERT_TRUE(parent.ok()) << parent.error_message();

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
        editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Locked Descendant Box");
    ASSERT_TRUE(box.ok()) << box.error_message();
    ASSERT_TRUE(editor.scene().reparent(box.value().entity, parent.value().id, true).ok());
    ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
    EXPECT_TRUE(editor.modeling().selection_summary().has_selection);

    const quader::QuaderApiVoidResult locked =
        editor.scene().set_locked(parent.value().id, true);
    ASSERT_TRUE(locked.ok()) << locked.error_message();
    EXPECT_TRUE(locked.changed());
    EXPECT_FALSE(editor.query().selection().has_selection);
    EXPECT_FALSE(editor.modeling().selection_summary().has_selection);

    ASSERT_TRUE(editor.history().undo().ok());
    quader::QuaderSelectionSummary summary =
        editor.modeling().selection_summary();
    EXPECT_TRUE(summary.has_selection);
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(summary.active_object_id, box.value().modeling_object_id);
    const std::optional<quader::QuaderEntitySnapshot> box_after_undo =
        entity_by_id(editor, box.value().entity);
    ASSERT_TRUE(box_after_undo.has_value());
    EXPECT_TRUE(box_after_undo->selected);
    EXPECT_TRUE(box_after_undo->active);

    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_FALSE(editor.query().selection().has_selection);
    EXPECT_FALSE(editor.modeling().selection_summary().has_selection);
}

TEST(EditorApiTests,
     LockedEntitiesCannotBeUserSelectedButCanBeInspectedAndUnlocked) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
        editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Locked Selection Target");
    ASSERT_TRUE(box.ok()) << box.error_message();
    ASSERT_TRUE(editor.scene().set_locked(box.value().entity, true).ok());

    const quader::QuaderApiVoidResult selected =
        editor.selection().select_object(box.value().entity);
    ASSERT_TRUE(selected.ok()) << selected.error_message();
    EXPECT_FALSE(entity_by_id(editor, box.value().entity)->selected);
    EXPECT_FALSE(entity_by_id(editor, box.value().entity)->active);
    EXPECT_FALSE(editor.query().selection().has_selection);

    const quader::QuaderApiVoidResult inspected =
        editor.selection().select_objects_for_inspection(
            {box.value().entity}, box.value().entity);
    ASSERT_TRUE(inspected.ok()) << inspected.error_message();
    EXPECT_TRUE(inspected.changed());

    std::optional<quader::QuaderEntitySnapshot> snapshot =
        entity_by_id(editor, box.value().entity);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->locked);
    EXPECT_TRUE(snapshot->selected);
    EXPECT_TRUE(snapshot->active);
    EXPECT_TRUE(editor.query().selection().has_selection);

    const std::optional<quader::QuaderCommandDescriptor> delete_descriptor =
        command_descriptor(editor, quader::quader_command::kDeleteSelection);
    ASSERT_TRUE(delete_descriptor.has_value());
    EXPECT_FALSE(delete_descriptor->enabled);
    const std::optional<quader::QuaderCommandDescriptor> duplicate_descriptor =
        command_descriptor(editor, quader::quader_command::kDuplicateSelection);
    ASSERT_TRUE(duplicate_descriptor.has_value());
    EXPECT_FALSE(duplicate_descriptor->enabled);

    const quader::QuaderApiVoidResult deleted =
        editor.commands().execute(quader::quader_command::kDeleteSelection);
    EXPECT_FALSE(deleted.changed());
    EXPECT_TRUE(entity_by_id(editor, box.value().entity).has_value());

    const quader::QuaderApiVoidResult duplicated =
        editor.commands().execute(quader::quader_command::kDuplicateSelection);
    EXPECT_FALSE(duplicated.changed());
    EXPECT_EQ(user_entity_count(editor), 1u);

    const quader::QuaderApiVoidResult unlocked =
        editor.scene().set_locked(box.value().entity, false);
    ASSERT_TRUE(unlocked.ok()) << unlocked.error_message();
    EXPECT_TRUE(unlocked.changed());
    snapshot = entity_by_id(editor, box.value().entity);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FALSE(snapshot->locked);
    EXPECT_TRUE(snapshot->selected);
    EXPECT_TRUE(snapshot->active);
}

TEST(EditorApiTests, SceneLightCreationUsesExplicitApiLightDefaults) {
  EXPECT_EQ(quader::default_quader_point_light_component().type,
            quader::QuaderLightType::Point);
  EXPECT_EQ(quader::default_quader_directional_light_component().type,
            quader::QuaderLightType::Directional);
  EXPECT_EQ(quader::QuaderCreatePointLightArgs{}.light.type,
            quader::QuaderLightType::Point);
  EXPECT_EQ(quader::QuaderCreateDirectionalLightArgs{}.light.type,
            quader::QuaderLightType::Directional);

  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderApiResult<quader::QuaderEntityRef> point =
      editor.scene().create_point_light();
  ASSERT_TRUE(point.ok()) << point.error_message();
  const quader::QuaderApiResult<quader::QuaderEntityRef> directional =
      editor.scene().create_directional_light();
  ASSERT_TRUE(directional.ok()) << directional.error_message();

  const std::optional<quader::QuaderEntitySnapshot> point_snapshot =
      entity_by_id(editor, point.value().id);
  ASSERT_TRUE(point_snapshot.has_value());
  ASSERT_TRUE(point_snapshot->has_light);
  EXPECT_EQ(point_snapshot->light.type, quader::QuaderLightType::Point);

  const std::optional<quader::QuaderEntitySnapshot> directional_snapshot =
      entity_by_id(editor, directional.value().id);
  ASSERT_TRUE(directional_snapshot.has_value());
  ASSERT_TRUE(directional_snapshot->has_light);
  EXPECT_EQ(directional_snapshot->light.type,
            quader::QuaderLightType::Directional);
}

TEST(EditorApiTests, ScenePointLightApiCreatesSnapshotsAndEditsThroughHistory) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreatePointLightArgs args;
    args.name = "Key Light";
    args.transform.position = {1.0f, 2.0f, 3.0f};
    args.light.color = {1.0f, 0.75f, 0.5f};
    args.light.intensity = 950.0f;
    args.light.range = 14.0f;
    args.light.casts_shadows = true;
    args.select_created = true;

    const quader::QuaderApiResult<quader::QuaderEntityRef> created =
        editor.scene().create_point_light(args);
    ASSERT_TRUE(created.ok()) << created.error_message();
    ASSERT_NE(created.value().id, quader::kInvalidEntityId);

    std::optional<quader::QuaderEntitySnapshot> snapshot =
        entity_by_id(editor, created.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->name, "Key Light");
    EXPECT_EQ(snapshot->kind, quader::QuaderEntityKind::Light);
    EXPECT_TRUE(snapshot->has_light);
    EXPECT_EQ(snapshot->light.type, quader::QuaderLightType::Point);
    EXPECT_FLOAT_EQ(snapshot->light.color.y, 0.75f);
    EXPECT_FLOAT_EQ(snapshot->light.intensity, 950.0f);
    EXPECT_FLOAT_EQ(snapshot->light.range, 14.0f);
    EXPECT_TRUE(snapshot->light.casts_shadows);
    EXPECT_FLOAT_EQ(snapshot->transform.position.z, 3.0f);
    EXPECT_TRUE(snapshot->selected);
    EXPECT_TRUE(snapshot->active);

    quader::QuaderLightComponent edited = snapshot->light;
    edited.color = {0.25f, 0.5f, 1.0f};
    edited.intensity = 1200.0f;
    edited.range = 6.5f;
    edited.casts_shadows = false;
    const quader::QuaderApiVoidResult set_light =
        editor.scene().set_light_component(created.value().id, edited);
    ASSERT_TRUE(set_light.ok()) << set_light.error_message();
    EXPECT_TRUE(set_light.changed());

    snapshot = entity_by_id(editor, created.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FLOAT_EQ(snapshot->light.color.z, 1.0f);
    EXPECT_FLOAT_EQ(snapshot->light.intensity, 1200.0f);
    EXPECT_FLOAT_EQ(snapshot->light.range, 6.5f);
    EXPECT_FALSE(snapshot->light.casts_shadows);

    ASSERT_TRUE(editor.history().undo().ok());
    snapshot = entity_by_id(editor, created.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FLOAT_EQ(snapshot->light.color.z, 0.5f);
    EXPECT_FLOAT_EQ(snapshot->light.range, 14.0f);
    EXPECT_TRUE(snapshot->light.casts_shadows);

    ASSERT_TRUE(editor.history().redo().ok());
    snapshot = entity_by_id(editor, created.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FLOAT_EQ(snapshot->light.color.z, 1.0f);
    EXPECT_FLOAT_EQ(snapshot->light.range, 6.5f);
    EXPECT_FALSE(snapshot->light.casts_shadows);
}

TEST(EditorApiTests, ScenePointLightLiveEditsApplyImmediatelyAndCoalesceByMergeKey) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreatePointLightArgs args;
    args.light.intensity = 950.0f;
    args.light.range = 14.0f;
    const quader::QuaderApiResult<quader::QuaderEntityRef> created =
        editor.scene().create_point_light(args);
    ASSERT_TRUE(created.ok()) << created.error_message();

    const std::optional<quader::QuaderEntitySnapshot> before_snapshot =
        entity_by_id(editor, created.value().id);
    ASSERT_TRUE(before_snapshot.has_value());
    const quader::QuaderLightComponent before = before_snapshot->light;

    quader::QuaderLightComponent middle = before;
    middle.intensity = 1100.0f;
    quader::QuaderApiVoidResult set = editor.scene().set_light_component(
        created.value().id, middle,
        quader::QuaderSceneEditOptions{
            .merge_key = "inspector.light.intensity.gesture"});
    ASSERT_TRUE(set.ok()) << set.error_message();
    EXPECT_TRUE(set.changed());
    EXPECT_FLOAT_EQ(entity_by_id(editor, created.value().id)->light.intensity,
                    1100.0f);

    quader::QuaderLightComponent after = middle;
    after.intensity = 1400.0f;
    set = editor.scene().set_light_component(
        created.value().id, after,
        quader::QuaderSceneEditOptions{
            .merge_key = "inspector.light.intensity.gesture"});
    ASSERT_TRUE(set.ok()) << set.error_message();
    EXPECT_TRUE(set.changed());
    EXPECT_FLOAT_EQ(entity_by_id(editor, created.value().id)->light.intensity,
                    1400.0f);

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_FLOAT_EQ(entity_by_id(editor, created.value().id)->light.intensity,
                    before.intensity);

    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_FLOAT_EQ(entity_by_id(editor, created.value().id)->light.intensity,
                    after.intensity);
}

TEST(EditorApiTests, SceneCreatePointLightCommandCreatesSelectedPointLight) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiVoidResult result = editor.commands().execute(
        quader::quader_command::kSceneCreatePointLight);
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());

    const std::optional<quader::QuaderEntitySnapshot> snapshot =
        entity_named(editor, "Point Light");
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->kind, quader::QuaderEntityKind::Light);
    EXPECT_TRUE(snapshot->has_light);
    EXPECT_EQ(snapshot->light.type, quader::QuaderLightType::Point);
    EXPECT_TRUE(snapshot->selected);
    EXPECT_TRUE(snapshot->active);
    ASSERT_TRUE(scene_root(editor).has_value());
    EXPECT_EQ(snapshot->parent, scene_root(editor)->id);
}

TEST(EditorApiTests, SceneCreateDirectionalLightCommandCreatesSelectedDirectionalLight) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiVoidResult result = editor.commands().execute(
        quader::quader_command::kSceneCreateDirectionalLight);
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());

    const std::optional<quader::QuaderEntitySnapshot> snapshot =
        entity_named(editor, "Directional Light");
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->kind, quader::QuaderEntityKind::Light);
    EXPECT_TRUE(snapshot->has_light);
    EXPECT_EQ(snapshot->light.type, quader::QuaderLightType::Directional);
    EXPECT_TRUE(snapshot->selected);
    EXPECT_TRUE(snapshot->active);
    ASSERT_TRUE(scene_root(editor).has_value());
    EXPECT_EQ(snapshot->parent, scene_root(editor)->id);
}

TEST(EditorApiTests, DeleteSelectionDeletesSelectedLightsThroughSceneAwareRoute) {
  auto exercise = [](quader::QuaderCommandId create_command,
                     std::string_view expected_name,
                     quader::QuaderLightType expected_type) {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;

    const quader::QuaderApiVoidResult created =
        editor.commands().execute(create_command);
    ASSERT_TRUE(created.ok()) << created.error_message();
    const std::optional<quader::QuaderEntitySnapshot> before =
        entity_named(editor, expected_name);
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(before->has_light);
    EXPECT_EQ(before->light.type, expected_type);
    EXPECT_TRUE(before->selected);
    EXPECT_TRUE(before->active);

    const std::optional<quader::QuaderCommandDescriptor> delete_command =
        command_descriptor(editor, quader::quader_command::kDeleteSelection);
    ASSERT_TRUE(delete_command.has_value());
    EXPECT_TRUE(delete_command->enabled);

    const quader::QuaderApiVoidResult deleted =
        editor.commands().execute(quader::quader_command::kDeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.error_message();
    EXPECT_TRUE(deleted.changed());
    EXPECT_FALSE(entity_by_id(editor, before->id).has_value());
    EXPECT_EQ(editor.query().history().undo_label, "Delete Selection");

    ASSERT_TRUE(editor.history().undo().ok());
    std::optional<quader::QuaderEntitySnapshot> restored =
        entity_by_id(editor, before->id);
    ASSERT_TRUE(restored.has_value());
    EXPECT_TRUE(restored->has_light);
    EXPECT_EQ(restored->light.type, expected_type);
    EXPECT_TRUE(restored->selected);
    EXPECT_TRUE(restored->active);

    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_FALSE(entity_by_id(editor, before->id).has_value());
  };

  exercise(quader::quader_command::kSceneCreatePointLight, "Point Light",
           quader::QuaderLightType::Point);
  exercise(quader::quader_command::kSceneCreateDirectionalLight,
           "Directional Light", quader::QuaderLightType::Directional);
}

TEST(EditorApiTests, ScenePointLightSelectionFeedsQuerySummaryAndCenterInComponentMode) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok);

    quader::QuaderCreatePointLightArgs args;
    args.transform.position = {2.0f, 3.0f, 4.0f};
    args.select_created = true;
    const quader::QuaderApiResult<quader::QuaderEntityRef> light =
        editor.scene().create_point_light(args);
    ASSERT_TRUE(light.ok()) << light.error_message();

    const quader::QuaderSelectionSummary selection = editor.query().selection();
    EXPECT_TRUE(selection.has_selection);
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Light);
    EXPECT_EQ(selection.selected_object_count, 1u);

    const std::optional<quader::QVec3> center =
        editor.query().selection_center();
    ASSERT_TRUE(center.has_value());
    EXPECT_FLOAT_EQ(center->x, 2.0f);
    EXPECT_FLOAT_EQ(center->y, 3.0f);
    EXPECT_FLOAT_EQ(center->z, 4.0f);
}

TEST(EditorApiTests, EntitySelectionClearsStaleComponentSelectionBeforeImmediateMove) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Stale Component Box");
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_face(box, 1).ok());
    EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Face);
    const std::optional<quader::QuaderEntitySnapshot> box_before =
        entity_by_id(editor, box.entity);
    ASSERT_TRUE(box_before.has_value());

    quader::QuaderCreatePointLightArgs args;
    args.name = "Selection Move Light";
    args.transform.position = {5.0f, 0.0f, 0.0f};
    args.select_created = true;
    const quader::QuaderApiResult<quader::QuaderEntityRef> light =
        editor.scene().create_point_light(args);
    ASSERT_TRUE(light.ok()) << light.error_message();

    quader::QuaderSelectionSummary selection = editor.query().selection();
    EXPECT_TRUE(selection.has_selection);
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Light);
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{light.value().id});
    EXPECT_EQ(editor.modeling().selection_summary().active_kind,
              quader::QuaderActiveSelectionKind::Light);

    const quader::QuaderApiVoidResult moved =
        editor.modeling().move_selection({2.0f, 0.0f, 0.0f});
    ASSERT_TRUE(moved.ok()) << moved.error_message();
    EXPECT_TRUE(moved.changed());

    std::optional<quader::QuaderEntitySnapshot> box_after =
        entity_by_id(editor, box.entity);
    std::optional<quader::QuaderEntitySnapshot> light_after =
        entity_by_id(editor, light.value().id);
    ASSERT_TRUE(box_after.has_value());
    ASSERT_TRUE(light_after.has_value());
    EXPECT_NEAR(box_after->transform.position.x,
                box_before->transform.position.x, 0.001f);
    EXPECT_NEAR(light_after->transform.position.x, 7.0f, 0.001f);

    ASSERT_TRUE(editor.history().undo().ok());
    light_after = entity_by_id(editor, light.value().id);
    ASSERT_TRUE(light_after.has_value());
    EXPECT_NEAR(light_after->transform.position.x, 5.0f, 0.001f);
    selection = editor.query().selection();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Light);

    ASSERT_TRUE(editor.history().redo().ok());
    light_after = entity_by_id(editor, light.value().id);
    ASSERT_TRUE(light_after.has_value());
    EXPECT_NEAR(light_after->transform.position.x, 7.0f, 0.001f);
    EXPECT_EQ(editor.query().selection().active_kind,
              quader::QuaderActiveSelectionKind::Light);
}

TEST(EditorApiTests, MixedEntitySelectionMoveSelectionTransformsMeshAndLight) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Mixed Entity Move Box");

    quader::QuaderCreatePointLightArgs args;
    args.name = "Mixed Entity Move Light";
    args.transform.position = {10.0f, 0.0f, 0.0f};
    const quader::QuaderApiResult<quader::QuaderEntityRef> light =
        editor.scene().create_point_light(args);
    ASSERT_TRUE(light.ok()) << light.error_message();

    ASSERT_TRUE(editor.selection().select_object(box).ok());
    ASSERT_TRUE(
        editor.selection()
            .select_object(light.value().id, quader::QuaderSelectionEdit::Add)
            .ok());

    quader::QuaderSelectionSummary selection = editor.query().selection();
    EXPECT_TRUE(selection.has_selection);
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(selection.selected_object_count, 2u);
    std::vector<quader::EntityId> expected_selected{box.entity,
                                                    light.value().id};
    std::sort(expected_selected.begin(), expected_selected.end());
    EXPECT_EQ(selected_entities(editor), expected_selected);

    const quader::QuaderApiVoidResult moved =
        editor.modeling().move_selection({3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(moved.ok()) << moved.error_message();
    EXPECT_TRUE(moved.changed());

    const std::optional<quader::QuaderEntitySnapshot> box_after =
        entity_by_id(editor, box.entity);
    const std::optional<quader::QuaderEntitySnapshot> light_after =
        entity_by_id(editor, light.value().id);
    ASSERT_TRUE(box_after.has_value());
    ASSERT_TRUE(light_after.has_value());
    EXPECT_NEAR(box_after->transform.position.x, 3.0f, 0.001f);
    EXPECT_NEAR(light_after->transform.position.x, 13.0f, 0.001f);
}

TEST(EditorApiTests, EntitySelectionTransformSelectionRoutesLightThroughSceneTransformHistory) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreatePointLightArgs args;
    args.name = "Transform Selection Light";
    args.transform.position = {4.0f, 1.0f, 0.0f};
    args.select_created = true;
    const quader::QuaderApiResult<quader::QuaderEntityRef> light =
        editor.scene().create_point_light(args);
    ASSERT_TRUE(light.ok()) << light.error_message();

    quader::QuaderTransform3 transform;
    transform.origin = {2.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult transformed =
        editor.modeling().transform_selection(transform);
    ASSERT_TRUE(transformed.ok()) << transformed.error_message();
    EXPECT_TRUE(transformed.changed());
    EXPECT_EQ(editor.query().history().undo_label, "Transform Selection");

    std::optional<quader::QuaderEntitySnapshot> snapshot =
        entity_by_id(editor, light.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_NEAR(snapshot->transform.position.x, 6.0f, 0.001f);
    EXPECT_NEAR(snapshot->transform.position.y, 1.0f, 0.001f);

    ASSERT_TRUE(editor.history().undo().ok());
    snapshot = entity_by_id(editor, light.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_NEAR(snapshot->transform.position.x, 4.0f, 0.001f);

    ASSERT_TRUE(editor.history().redo().ok());
    snapshot = entity_by_id(editor, light.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_NEAR(snapshot->transform.position.x, 6.0f, 0.001f);
}

TEST(EditorApiTests, MixedEntitySelectionTransformSelectionTransformsMeshAndLight) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Mixed Transform Box");
    quader::QuaderCreatePointLightArgs args;
    args.name = "Mixed Transform Light";
    args.transform.position = {8.0f, 0.0f, 0.0f};
    const quader::QuaderApiResult<quader::QuaderEntityRef> light =
        editor.scene().create_point_light(args);
    ASSERT_TRUE(light.ok()) << light.error_message();

    ASSERT_TRUE(
        editor.selection()
            .select_objects({box.entity, light.value().id}, light.value().id)
            .ok());

    quader::QuaderTransform3 transform;
    transform.origin = {-2.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult transformed =
        editor.modeling().transform_selection(transform);
    ASSERT_TRUE(transformed.ok()) << transformed.error_message();
    EXPECT_TRUE(transformed.changed());

    const std::optional<quader::QuaderEntitySnapshot> box_after =
        entity_by_id(editor, box.entity);
    const std::optional<quader::QuaderEntitySnapshot> light_after =
        entity_by_id(editor, light.value().id);
    ASSERT_TRUE(box_after.has_value());
    ASSERT_TRUE(light_after.has_value());
    EXPECT_NEAR(box_after->transform.position.x, -2.0f, 0.001f);
    EXPECT_NEAR(light_after->transform.position.x, 6.0f, 0.001f);
}

TEST(EditorApiTests, PublicDuplicateSelectionDuplicatesLightEntityThroughSceneHierarchy) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreatePointLightArgs args;
    args.name = "Duplicate Entity Light";
    args.transform.position = {1.0f, 2.0f, 3.0f};
    args.select_created = true;
    const quader::QuaderApiResult<quader::QuaderEntityRef> source =
        editor.scene().create_point_light(args);
    ASSERT_TRUE(source.ok()) << source.error_message();

    const quader::QuaderApiVoidResult duplicated =
        editor.commands().execute(quader::quader_command::kDuplicateSelection);
    ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
    EXPECT_TRUE(duplicated.changed());
    EXPECT_EQ(editor.query().history().undo_label, "Duplicate Selection");
    EXPECT_EQ(user_entity_count(editor), 2u);

    const std::vector<quader::EntityId> selected = selected_entities(editor);
    ASSERT_EQ(selected.size(), 1u);
    const quader::EntityId duplicate_entity = selected.front();
    EXPECT_NE(duplicate_entity, source.value().id);
    std::optional<quader::QuaderEntitySnapshot> duplicate_snapshot =
        entity_by_id(editor, duplicate_entity);
    ASSERT_TRUE(duplicate_snapshot.has_value());
    EXPECT_TRUE(duplicate_snapshot->has_light);
    EXPECT_EQ(duplicate_snapshot->light.type, quader::QuaderLightType::Point);
    EXPECT_NEAR(duplicate_snapshot->transform.position.x, 1.0f, 0.001f);
    EXPECT_TRUE(duplicate_snapshot->selected);
    EXPECT_TRUE(duplicate_snapshot->active);

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_FALSE(entity_by_id(editor, duplicate_entity).has_value());
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{source.value().id});

    ASSERT_TRUE(editor.history().redo().ok());
    duplicate_snapshot = entity_by_id(editor, duplicate_entity);
    ASSERT_TRUE(duplicate_snapshot.has_value());
    EXPECT_TRUE(duplicate_snapshot->has_light);
    EXPECT_TRUE(duplicate_snapshot->selected);
    EXPECT_TRUE(duplicate_snapshot->active);
}

TEST(EditorApiTests, SceneRootSelectionCanEditSettingsButCannotBeDeletedOrDuplicated) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const std::optional<quader::QuaderEntitySnapshot> root = scene_root(editor);
    ASSERT_TRUE(root.has_value());

    const quader::QuaderApiVoidResult selected_root =
        editor.selection().select_objects({root->id}, root->id);
    ASSERT_TRUE(selected_root.ok()) << selected_root.error_message();
    EXPECT_TRUE(selected_root.changed());
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{root->id});

    const quader::QuaderSelectionSummary root_selection =
        editor.query().selection();
    EXPECT_TRUE(root_selection.has_selection);
    EXPECT_EQ(root_selection.mode, quader::QuaderSelectionMode::Object);

    quader::QuaderSceneSettings edited = root->scene_settings;
    edited.environment.intensity += 0.5f;
    const quader::QuaderApiVoidResult settings_changed =
        editor.scene().set_scene_settings(root->id, edited);
    ASSERT_TRUE(settings_changed.ok()) << settings_changed.error_message();
    EXPECT_TRUE(settings_changed.changed());
    expect_scene_settings_eq(scene_root_settings(editor), edited);

    const std::optional<quader::QuaderCommandDescriptor> duplicate_descriptor =
        command_descriptor(editor, quader::quader_command::kDuplicateSelection);
    ASSERT_TRUE(duplicate_descriptor.has_value());
    EXPECT_FALSE(duplicate_descriptor->enabled);

    const std::size_t before_count = user_entity_count(editor);
    const quader::QuaderApiVoidResult duplicate =
        editor.commands().execute(quader::quader_command::kDuplicateSelection);
    EXPECT_FALSE(duplicate.changed());
    EXPECT_EQ(user_entity_count(editor), before_count);
    EXPECT_TRUE(scene_root(editor).has_value());

    const quader::QuaderApiVoidResult deleted =
        editor.commands().execute(quader::quader_command::kDeleteSelection);
    EXPECT_FALSE(deleted.changed());
    EXPECT_EQ(user_entity_count(editor), before_count);
    EXPECT_TRUE(scene_root(editor).has_value());
}

TEST(EditorApiTests, ObjectSelectAllAndInvertUseSceneEntitiesAndExcludeLockedAndRoot) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderModelingObjectRef child_box =
        create_selected_box(editor, "Select All Child Box");
    quader::QuaderCreatePointLightArgs child_light_args;
    child_light_args.name = "Select All Child Light";
    const quader::QuaderApiResult<quader::QuaderEntityRef> child_light =
        editor.scene().create_point_light(child_light_args);
    ASSERT_TRUE(child_light.ok()) << child_light.error_message();
    ASSERT_TRUE(editor.selection()
                    .select_objects({child_box.entity, child_light.value().id},
                                    child_light.value().id)
                    .ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> group =
        editor.scene().group_selection("Select All Group");
    ASSERT_TRUE(group.ok()) << group.error_message();

    const quader::QuaderModelingObjectRef loose_box =
        create_selected_box(editor, "Select All Loose Box");
    quader::QuaderCreatePointLightArgs loose_light_args;
    loose_light_args.name = "Select All Loose Light";
    const quader::QuaderApiResult<quader::QuaderEntityRef> loose_light =
        editor.scene().create_point_light(loose_light_args);
    ASSERT_TRUE(loose_light.ok()) << loose_light.error_message();
    const quader::QuaderModelingObjectRef locked_box =
        create_selected_box(editor, "Select All Locked Box");
    ASSERT_TRUE(editor.scene().set_locked(locked_box.entity, true).ok());

    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
    const quader::QuaderApiVoidResult selected_all =
        editor.selection().select_all();
    ASSERT_TRUE(selected_all.ok()) << selected_all.error_message();
    EXPECT_TRUE(selected_all.changed());

    std::vector<quader::EntityId> expected{
        group.value().id, child_box.entity,       child_light.value().id,
        loose_box.entity, loose_light.value().id,
    };
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(selected_entities(editor), expected);

    const std::optional<quader::QuaderEntitySnapshot> root = scene_root(editor);
    ASSERT_TRUE(root.has_value());
    const std::vector<quader::EntityId> after_select_all =
        selected_entities(editor);
    EXPECT_EQ(
        std::find(after_select_all.begin(), after_select_all.end(), root->id),
        after_select_all.end());
    EXPECT_EQ(std::find(after_select_all.begin(), after_select_all.end(),
                        locked_box.entity),
              after_select_all.end());

    const quader::QuaderApiVoidResult inverted_all =
        editor.selection().select_inverse();
    ASSERT_TRUE(inverted_all.ok()) << inverted_all.error_message();
    EXPECT_TRUE(inverted_all.changed());
    EXPECT_TRUE(selected_entities(editor).empty());

    ASSERT_TRUE(editor.selection().select_object(loose_light.value().id).ok());
    const quader::QuaderApiVoidResult inverted_one =
        editor.selection().select_inverse();
    ASSERT_TRUE(inverted_one.ok()) << inverted_one.error_message();
    expected.erase(
        std::remove(expected.begin(), expected.end(), loose_light.value().id),
        expected.end());
    EXPECT_EQ(selected_entities(editor), expected);
}

TEST(EditorApiTests,
     ComponentSelectAllAndInverseOnlyAffectMeshesWithSelectedComponents) {
  const auto create_three_boxes = [](quader::QuaderEditorApi &editor) {
    std::array<quader::QuaderModelingObjectRef, 3> boxes;
    boxes[0] = create_selected_box(editor, "Component Scope A");
    for (std::size_t index = 1; index < boxes.size(); ++index) {
      const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
          editor.modeling().create_box(
              quader::QuaderBounds{
                  .min = {static_cast<float>(index * 4) - 1.0f, -1.0f, -1.0f},
                  .max = {static_cast<float>(index * 4) + 1.0f, 1.0f, 1.0f},
              },
              "Component Scope Mesh");
      EXPECT_TRUE(created.ok()) << created.error_message();
      if (created.ok()) {
        boxes[index] = created.value();
      }
    }
    return boxes;
  };

  const auto seed_two_meshes = [](
                                   quader::QuaderEditorApi &editor,
                                   const std::array<
                                       quader::QuaderModelingObjectRef, 3>
                                       &boxes) {
    ASSERT_TRUE(editor.selection()
                    .select_objects(
                        {boxes[0].entity, boxes[1].entity, boxes[2].entity},
                        boxes[2].entity)
                    .ok());
    ASSERT_TRUE(editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Vertex)
                    .ok());
    ASSERT_TRUE(editor.selection()
                    .select_vertex(boxes[0], 1,
                                   quader::QuaderSelectionEdit::Add)
                    .ok());
    ASSERT_TRUE(editor.selection()
                    .select_vertex(boxes[1], 1,
                                   quader::QuaderSelectionEdit::Add)
                    .ok());
    ASSERT_EQ(editor.selection().summary().selected_vertex_count, 2u);
    EXPECT_EQ(selected_entities(editor),
              (std::vector<quader::EntityId>{boxes[0].entity,
                                             boxes[1].entity,
                                             boxes[2].entity}));
  };

  {
    CommandTestHarness harness;
    const auto boxes = create_three_boxes(harness.editor);
    seed_two_meshes(harness.editor, boxes);

    const quader::QuaderApiVoidResult selected_all =
        harness.editor.selection().select_all();
    ASSERT_TRUE(selected_all.ok()) << selected_all.error_message();
    EXPECT_TRUE(selected_all.changed());
    EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count, 16u);
    EXPECT_EQ(selected_entities(harness.editor),
              (std::vector<quader::EntityId>{boxes[0].entity,
                                             boxes[1].entity}));
  }

  {
    CommandTestHarness harness;
    const auto boxes = create_three_boxes(harness.editor);
    seed_two_meshes(harness.editor, boxes);

    const quader::QuaderApiVoidResult inverted =
        harness.editor.selection().select_inverse();
    ASSERT_TRUE(inverted.ok()) << inverted.error_message();
    EXPECT_TRUE(inverted.changed());
    EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count, 14u);
    EXPECT_EQ(selected_entities(harness.editor),
              (std::vector<quader::EntityId>{boxes[0].entity,
                                             boxes[1].entity}));
  }
}

TEST(EditorApiTests,
     ComponentSelectAllUsesAStickyHoveredMeshWithoutASeedSelection) {
  /// Describes one component-mode variant of the sticky Select All contract.
  struct ModeCase final {
    quader::QuaderSelectionMode mode;
    quader::QuaderActiveSelectionKind hover_kind;
    std::size_t expected_count;
  };
  constexpr std::array<ModeCase, 3> kCases{
      ModeCase{quader::QuaderSelectionMode::Vertex,
               quader::QuaderActiveSelectionKind::Vertex, 8u},
      ModeCase{quader::QuaderSelectionMode::Edge,
               quader::QuaderActiveSelectionKind::Edge, 12u},
      ModeCase{quader::QuaderSelectionMode::Face,
               quader::QuaderActiveSelectionKind::Face, 6u},
  };

  for (const ModeCase &mode_case : kCases) {
    SCOPED_TRACE(static_cast<int>(mode_case.mode));
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Sticky Select All Scope");
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_TRUE(editor.selection().set_mode(mode_case.mode).ok());
    ASSERT_TRUE(selected_entities(editor).empty());
    ASSERT_EQ(editor.selection().summary().selected_vertex_count, 0u);
    ASSERT_EQ(editor.selection().summary().selected_edge_count, 0u);
    ASSERT_EQ(editor.selection().summary().selected_face_count, 0u);

    const quader::QuaderApiResult<quader::QuaderHoverSummary> hovered = [&]() {
      switch (mode_case.mode) {
      case quader::QuaderSelectionMode::Vertex:
        return editor.selection().hover_vertex(box, 1);
      case quader::QuaderSelectionMode::Edge:
        return editor.selection().hover_edge(box, 1, 2);
      case quader::QuaderSelectionMode::Face:
        return editor.selection().hover_face(box, 1);
      case quader::QuaderSelectionMode::Object:
      case quader::QuaderSelectionMode::Group:
        break;
      }
      return editor.selection().hover_face(box, 1);
    }();
    ASSERT_TRUE(hovered.ok()) << hovered.error_message();
    EXPECT_EQ(hovered.value().target.object_id, box.modeling_object_id);
    EXPECT_EQ(hovered.value().target.kind, mode_case.hover_kind);

    quader::QuaderHoverRequest miss;
    miss.ray = {
        .origin = {100.0f, 100.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    const quader::QuaderApiResult<quader::QuaderHoverSummary> missed =
        editor.selection().hover(miss);
    ASSERT_TRUE(missed.ok()) << missed.error_message();
    EXPECT_LT(missed.value().target.object_id, 0);
    EXPECT_EQ(missed.value().target.kind,
              quader::QuaderActiveSelectionKind::None);

    const quader::QuaderApiVoidResult inverted =
        editor.selection().select_inverse();
    ASSERT_TRUE(inverted.ok()) << inverted.error_message();
    EXPECT_FALSE(inverted.changed());
    EXPECT_EQ(editor.selection().summary().selected_vertex_count, 0u);
    EXPECT_EQ(editor.selection().summary().selected_edge_count, 0u);
    EXPECT_EQ(editor.selection().summary().selected_face_count, 0u);
    EXPECT_TRUE(selected_entities(editor).empty());

    const quader::QuaderApiVoidResult selected_all =
        editor.selection().select_all();
    ASSERT_TRUE(selected_all.ok()) << selected_all.error_message();
    EXPECT_TRUE(selected_all.changed());
    const quader::QuaderSelectionSummary selected = editor.selection().summary();
    switch (mode_case.mode) {
    case quader::QuaderSelectionMode::Vertex:
      EXPECT_EQ(selected.selected_vertex_count, mode_case.expected_count);
      break;
    case quader::QuaderSelectionMode::Edge:
      EXPECT_EQ(selected.selected_edge_count, mode_case.expected_count);
      break;
    case quader::QuaderSelectionMode::Face:
      EXPECT_EQ(selected.selected_face_count, mode_case.expected_count);
      break;
    case quader::QuaderSelectionMode::Object:
    case quader::QuaderSelectionMode::Group:
      break;
    }
    EXPECT_EQ(selected_entities(editor),
              (std::vector<quader::EntityId>{box.entity}));
  }
}

TEST(EditorApiTests,
     ComponentSelectAllUnionsStickyHoverWithComponentSelectedMeshes) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef selected_box =
      create_selected_box(editor, "Selected Component Scope");
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> sticky_box =
      editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {3.0f, -1.0f, -1.0f},
              .max = {5.0f, 1.0f, 1.0f},
          },
          "Sticky Component Scope");
  ASSERT_TRUE(sticky_box.ok()) << sticky_box.error_message();
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> untouched_box =
      editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {7.0f, -1.0f, -1.0f},
              .max = {9.0f, 1.0f, 1.0f},
          },
          "Untouched Component Scope");
  ASSERT_TRUE(untouched_box.ok()) << untouched_box.error_message();

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(editor.selection().select_face(selected_box, 1).ok());
  ASSERT_EQ(editor.selection().summary().selected_face_count, 1u);
  ASSERT_TRUE(editor.selection().hover_face(sticky_box.value(), 1).ok());

  quader::QuaderHoverRequest miss;
  miss.ray = {
      .origin = {100.0f, 100.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  ASSERT_TRUE(editor.selection().hover(miss).ok());

  const quader::QuaderApiVoidResult selected_all =
      editor.selection().select_all();
  ASSERT_TRUE(selected_all.ok()) << selected_all.error_message();
  EXPECT_TRUE(selected_all.changed());
  EXPECT_EQ(editor.selection().summary().selected_face_count, 12u);
  EXPECT_EQ(selected_entities(editor),
            (std::vector<quader::EntityId>{selected_box.entity,
                                           sticky_box.value().entity}));
  const std::optional<quader::QuaderEntitySnapshot> untouched =
      editor.query().entity(untouched_box.value().entity);
  ASSERT_TRUE(untouched.has_value());
  EXPECT_FALSE(untouched->selected);
}

TEST(EditorApiTests,
     ComponentSelectAllIgnoresAStaleStickyCandidate) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef selected_box =
      create_selected_box(editor, "Live Component Scope");

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(editor.selection().select_face(selected_box, 1).ok());
  quader::QuaderModelingObjectRef stale_reference = selected_box;
  stale_reference.modeling_object_id = 123456789;
  ASSERT_TRUE(editor.selection().hover_face(stale_reference, 1).ok());
  ASSERT_EQ(editor.selection().summary().selected_face_count, 1u);

  const quader::QuaderApiVoidResult selected_all =
      editor.selection().select_all();
  ASSERT_TRUE(selected_all.ok()) << selected_all.error_message();
  EXPECT_TRUE(selected_all.changed());
  EXPECT_EQ(editor.selection().summary().selected_face_count, 6u);
  EXPECT_EQ(selected_entities(editor),
            (std::vector<quader::EntityId>{selected_box.entity}));
}

TEST(EditorApiTests,
     SelectSimilarMeshesUsesExactLocalGeometryAcrossIndependentObjects) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderBounds matching_bounds{
      .min = {-1.0f, -1.0f, -1.0f},
      .max = {1.0f, 1.0f, 1.0f},
  };
  const quader::QuaderModelingObjectRef source =
      create_selected_box(editor, "Similar Geometry Source");
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef>
      independent_match =
          editor.modeling().create_box(matching_bounds,
                                       "Similar Geometry Independent Match");
  ASSERT_TRUE(independent_match.ok()) << independent_match.error_message();
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> mismatch =
      editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {2.0f, 1.0f, 1.0f},
          },
          "Similar Geometry Different Width");
  ASSERT_TRUE(mismatch.ok()) << mismatch.error_message();

  ASSERT_TRUE(
      editor.scene()
          .set_transform(independent_match.value().entity,
                         quader::QuaderTransform{
                             .position = {8.0f, -3.0f, 5.0f},
                             .rotation_euler = {0.25f, 0.5f, 1.0f},
                             .scale = {2.0f, 3.0f, 4.0f},
                         })
          .ok());
  const quader::QuaderApiResult<quader::QuaderAssetRef> source_material =
      editor.assets().add_material(
          quader::QuaderMaterialArgs{.name = "Similar Source Material"});
  const quader::QuaderApiResult<quader::QuaderAssetRef> match_material =
      editor.assets().add_material(
          quader::QuaderMaterialArgs{.name = "Similar Match Material"});
  ASSERT_TRUE(source_material.ok()) << source_material.error_message();
  ASSERT_TRUE(match_material.ok()) << match_material.error_message();
  ASSERT_TRUE(editor.assets()
                  .assign_material_to_entity(source.entity,
                                             source_material.value().id)
                  .ok());
  ASSERT_TRUE(editor.assets()
                  .assign_material_to_entity(independent_match.value().entity,
                                             match_material.value().id)
                  .ok());

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(source).ok());
  ASSERT_EQ(selected_entities(editor),
            std::vector<quader::EntityId>{source.entity});
  const quader::QuaderOverlayFrame warm_overlay =
      editor.render().selection_overlay_frame();
  EXPECT_TRUE(std::any_of(
      warm_overlay.lines.begin(), warm_overlay.lines.end(),
      [&](const quader::QuaderOverlayLine &line) {
        return line.layer == quader::QuaderOverlayLayer::SelectedEdge &&
               line.element.object_id == source.modeling_object_id;
      }));
  const quader::QuaderApiVoidResult selected_similar =
      editor.selection().select_similar_meshes();
  ASSERT_TRUE(selected_similar.ok()) << selected_similar.error_message();
  EXPECT_TRUE(selected_similar.changed());

  std::vector<quader::EntityId> expected_selection{
      source.entity,
      independent_match.value().entity,
  };
  std::sort(expected_selection.begin(), expected_selection.end());
  EXPECT_EQ(selected_entities(editor), expected_selection);
  const std::optional<quader::QuaderEntitySnapshot> source_after =
      entity_by_id(editor, source.entity);
  const std::optional<quader::QuaderEntitySnapshot> mismatch_after =
      entity_by_id(editor, mismatch.value().entity);
  ASSERT_TRUE(source_after.has_value());
  ASSERT_TRUE(mismatch_after.has_value());
  EXPECT_TRUE(source_after->active);
  EXPECT_FALSE(mismatch_after->selected);

  const quader::QuaderOverlayFrame expanded_overlay =
      editor.render().selection_overlay_frame();
  for (const int modeling_object_id : {
           source.modeling_object_id,
           independent_match.value().modeling_object_id,
       }) {
    EXPECT_TRUE(std::any_of(
        expanded_overlay.lines.begin(), expanded_overlay.lines.end(),
        [&](const quader::QuaderOverlayLine &line) {
          return line.layer == quader::QuaderOverlayLayer::SelectedEdge &&
                 line.element.object_id == modeling_object_id;
        }))
        << "Every selected mesh must receive its own selected topology wire "
           "after the cached selection expands.";
  }

  const std::uint64_t stable_selection_revision =
      editor.selection().summary().selection_revision;
  const quader::QuaderApiVoidResult already_complete =
      editor.selection().select_similar_meshes();
  ASSERT_TRUE(already_complete.ok()) << already_complete.error_message();
  EXPECT_FALSE(already_complete.changed());
  EXPECT_EQ(editor.selection().summary().selection_revision,
            stable_selection_revision);
}

TEST(EditorApiTests,
     SelectSimilarMeshesMatchesDirectAndOneMemberInstanceTargetsAtomically) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef direct =
      create_selected_box(editor, "Similar Direct Mesh");

  const quader::QuaderModelingObjectRef linked_member =
      create_selected_box(editor, "Similar Linked Member");
  ASSERT_TRUE(editor.scene()
                  .set_transform(linked_member.entity,
                                 quader::QuaderTransform{
                                     .position = {7.0f, -3.0f, 2.0f},
                                     .rotation_euler = {0.2f, 0.4f, 0.6f},
                                     .scale = {2.0f, 3.0f, 4.0f},
                                 })
                  .ok());
  ASSERT_TRUE(editor.selection().select_object(linked_member).ok());
  const auto linked_created =
      editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(linked_created.ok()) << linked_created.error_message();
  const quader::EntityId first_linked =
      linked_created.value().occurrence.root.id;
  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kDuplicateSelection)
                  .ok());
  ASSERT_EQ(selected_entities(editor).size(), 1u);
  const quader::EntityId second_linked = selected_entities(editor).front();

  const quader::QuaderModelingObjectRef independent_member =
      create_selected_box(editor, "Similar Independent Member");
  ASSERT_TRUE(editor.scene()
                  .set_transform(independent_member.entity,
                                 quader::QuaderTransform{
                                     .position = {-11.0f, 5.0f, 9.0f},
                                     .rotation_euler = {0.9f, 0.1f, 0.3f},
                                     .scale = {0.5f, 1.5f, 2.5f},
                                 })
                  .ok());
  ASSERT_TRUE(editor.selection().select_object(independent_member).ok());
  const auto independent_created =
      editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(independent_created.ok())
      << independent_created.error_message();
  const quader::EntityId independent =
      independent_created.value().occurrence.root.id;

  const quader::QuaderApiResult<quader::QuaderModelingObjectRef>
      mismatch_member = editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {2.0f, 1.0f, 1.0f},
          },
          "Similar Instance Geometry Mismatch");
  ASSERT_TRUE(mismatch_member.ok()) << mismatch_member.error_message();
  ASSERT_TRUE(editor.selection().select_object(mismatch_member.value()).ok());
  const auto mismatch_created =
      editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(mismatch_created.ok()) << mismatch_created.error_message();
  const quader::EntityId mismatch =
      mismatch_created.value().occurrence.root.id;

  const quader::QuaderModelingObjectRef visible_member =
      create_selected_box(editor, "Similar Visible Instance Member");
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> hidden_extra =
      editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {2.0f, 1.0f, 1.0f},
          },
          "Similar Hidden Instance Member");
  ASSERT_TRUE(hidden_extra.ok()) << hidden_extra.error_message();
  ASSERT_TRUE(editor.selection()
                  .select_objects(
                      {visible_member.entity, hidden_extra.value().entity},
                      visible_member.entity)
                  .ok());
  const auto visible_only_created =
      editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(visible_only_created.ok())
      << visible_only_created.error_message();
  const quader::EntityId visible_only_occurrence =
      visible_only_created.value().occurrence.root.id;
  ASSERT_TRUE(
      editor.scene().set_visible(hidden_extra.value().entity, false).ok());

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(direct).ok());
  const quader::QuaderApiVoidResult selected_from_direct =
      editor.selection().select_similar_meshes();
  ASSERT_TRUE(selected_from_direct.ok())
      << selected_from_direct.error_message();
  ASSERT_TRUE(selected_from_direct.changed());

  std::vector<quader::EntityId> expected{
      direct.entity,
      first_linked,
      second_linked,
      independent,
      visible_only_occurrence,
  };
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(selected_entities(editor), expected);
  ASSERT_TRUE(editor.query().entity(direct.entity).has_value());
  EXPECT_TRUE(editor.query().entity(direct.entity)->active);
  EXPECT_FALSE(editor.query().entity(mismatch)->selected);

  ASSERT_TRUE(editor.selection().select_object(first_linked).ok());
  const quader::QuaderApiVoidResult selected_from_occurrence =
      editor.selection().select_similar_meshes();
  ASSERT_TRUE(selected_from_occurrence.ok())
      << selected_from_occurrence.error_message();
  EXPECT_EQ(selected_entities(editor), expected);
  ASSERT_TRUE(editor.query().entity(first_linked).has_value());
  EXPECT_TRUE(editor.query().entity(first_linked)->active);
}

TEST(EditorApiTests,
     SelectSimilarMeshesComparesWholeInstanceArrangementAtomically) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const auto create_pair_occurrence =
      [&](float center, float half_spacing, std::string_view name,
          bool wide_second = false) -> quader::EntityId {
    const quader::QuaderModelingObjectRef first =
        create_selected_box(editor, std::string(name) + " First");
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> second =
        wide_second
            ? editor.modeling().create_box(
                  quader::QuaderBounds{
                      .min = {-1.0f, -1.0f, -1.0f},
                      .max = {2.0f, 1.0f, 1.0f},
                  },
                  std::string(name) + " Wide Second")
            : editor.modeling().create_box(
                  quader::QuaderBounds{
                      .min = {-1.0f, -1.0f, -1.0f},
                      .max = {1.0f, 1.0f, 1.0f},
                  },
                  std::string(name) + " Second");
    if (!second.ok()) {
      ADD_FAILURE() << second.error_message();
      return quader::kInvalidEntityId;
    }
    if (!editor.scene()
             .set_transform(first.entity,
                            quader::QuaderTransform{
                                .position = {center - half_spacing, 0.0f,
                                             0.0f}})
             .ok() ||
        !editor.scene()
             .set_transform(second.value().entity,
                            quader::QuaderTransform{
                                .position = {center + half_spacing, 0.0f,
                                             0.0f}})
             .ok() ||
        !editor.selection()
             .select_objects({first.entity, second.value().entity},
                             first.entity)
             .ok()) {
      ADD_FAILURE() << "Could not prepare the instance member arrangement.";
      return quader::kInvalidEntityId;
    }
    const auto created = editor.modeling().create_instance_from_selection();
    if (!created.ok()) {
      ADD_FAILURE() << created.error_message();
      return quader::kInvalidEntityId;
    }
    return created.value().occurrence.root.id;
  };

  const quader::EntityId source =
      create_pair_occurrence(0.0f, 2.0f, "Similar Pair Source");
  ASSERT_NE(source, quader::kInvalidEntityId);
  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kDuplicateSelection)
                  .ok());
  ASSERT_EQ(selected_entities(editor).size(), 1u);
  const quader::EntityId linked = selected_entities(editor).front();
  const quader::EntityId independent =
      create_pair_occurrence(10.0f, 2.0f, "Similar Pair Independent");
  const quader::EntityId arrangement_mismatch =
      create_pair_occurrence(20.0f, 3.0f,
                             "Similar Pair Arrangement Mismatch");
  const quader::EntityId geometry_mismatch =
      create_pair_occurrence(30.0f, 2.0f,
                             "Similar Pair Geometry Mismatch", true);
  ASSERT_NE(independent, quader::kInvalidEntityId);
  ASSERT_NE(arrangement_mismatch, quader::kInvalidEntityId);
  ASSERT_NE(geometry_mismatch, quader::kInvalidEntityId);

  ASSERT_TRUE(editor.selection().select_object(source).ok());
  const quader::QuaderApiVoidResult selected =
      editor.selection().select_similar_meshes();
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  ASSERT_TRUE(selected.changed());

  std::vector<quader::EntityId> expected{source, linked, independent};
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(selected_entities(editor), expected);
  EXPECT_TRUE(editor.query().entity(source)->active);
  EXPECT_FALSE(editor.query().entity(arrangement_mismatch)->selected);
  EXPECT_FALSE(editor.query().entity(geometry_mismatch)->selected);
  for (const quader::EntityId selected_entity : selected_entities(editor)) {
    const std::optional<quader::QuaderEntitySnapshot> snapshot =
        editor.query().entity(selected_entity);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->has_instance_occurrence);
  }
}

TEST(EditorApiTests,
     SelectMeshNGonsCapturesDirectMeshesThenEntersFaceMode) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef first =
      create_polygon_mesh(editor, 5U, "First N-Gon Mesh");
  const quader::QuaderModelingObjectRef second =
      create_polygon_mesh(editor, 6U, "Second N-Gon Mesh");
  const quader::QuaderModelingObjectRef quad =
      create_polygon_mesh(editor, 4U, "Quad Mesh");
  ASSERT_TRUE(editor.selection()
                  .select_objects({first, quad, second})
                  .ok());
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());

  const quader::QuaderApiVoidResult selected =
      editor.selection().select_mesh_ngons();
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected.changed());
  const quader::QuaderSelectionSummary summary =
      editor.selection().summary();
  EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Face);
  EXPECT_EQ(summary.selected_face_count, 2U);
  EXPECT_EQ(summary.active_object_id, second.modeling_object_id);
  EXPECT_EQ(selected_entities(editor),
            (std::vector<quader::EntityId>{first.entity, second.entity}));

  CommandTestHarness instance_harness;
  quader::QuaderEditorApi &instance_editor = instance_harness.editor;
  const quader::QuaderModelingObjectRef instanced =
      create_polygon_mesh(instance_editor, 5U, "Instanced N-Gon Mesh");
  ASSERT_TRUE(instance_editor.selection().select_object(instanced).ok());
  ASSERT_TRUE(instance_editor.modeling().create_instance().ok());
  ASSERT_EQ(instance_editor.selection().summary().mode,
            quader::QuaderSelectionMode::Object);
  const quader::QuaderApiVoidResult ignored_occurrence =
      instance_editor.selection().select_mesh_ngons();
  ASSERT_TRUE(ignored_occurrence.ok())
      << ignored_occurrence.error_message();
  EXPECT_FALSE(ignored_occurrence.changed());
  EXPECT_EQ(instance_editor.selection().summary().mode,
            quader::QuaderSelectionMode::Object);
}

TEST(EditorApiTests,
     SelectFaceNormalUsesWorldFacingAndInverseTransposeForMirrors) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef first =
      create_selected_box(editor, "Normal Seed Box");
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> mirrored =
      editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {-1.0F, -1.0F, -1.0F},
              .max = {1.0F, 1.0F, 1.0F},
          },
          "Normal Mirrored Box");
  ASSERT_TRUE(mirrored.ok()) << mirrored.error_message();
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> tilted =
      editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {-1.0F, -1.0F, -1.0F},
              .max = {1.0F, 1.0F, 1.0F},
          },
          "Normal Tilted Box");
  ASSERT_TRUE(tilted.ok()) << tilted.error_message();
  ASSERT_TRUE(
      editor.scene()
          .set_transform(mirrored.value().entity,
                         quader::QuaderTransform{
                             .position = {6.0F, 0.0F, 0.0F},
                             .rotation_euler = {0.0F, 0.0F, 0.75F},
                             .scale = {-2.0F, 3.0F, 0.5F},
                         })
          .ok());
  ASSERT_TRUE(
      editor.scene()
          .set_transform(tilted.value().entity,
                         quader::QuaderTransform{
                             .position = {12.0F, 0.0F, 0.0F},
                             .rotation_euler = {0.00001F, 0.0F, 0.0F},
                         })
          .ok());

  ASSERT_TRUE(editor.selection()
                  .select_objects({first, mirrored.value(), tilted.value()})
                  .ok());
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection()
                  .select_face(first, 1,
                               quader::QuaderSelectionEdit::Add)
                  .ok());
  ASSERT_EQ(editor.selection().summary().selected_face_count, 1U);
  ASSERT_EQ(selected_entities(editor).size(), 3U);

  const quader::QuaderApiVoidResult selected_normal =
      editor.selection().select_face_normal();
  ASSERT_TRUE(selected_normal.ok()) << selected_normal.error_message();
  EXPECT_TRUE(selected_normal.changed());
  EXPECT_EQ(editor.selection().summary().selected_face_count, 2U);
  EXPECT_EQ(selected_entities(editor),
            (std::vector<quader::EntityId>{first.entity,
                                           mirrored.value().entity}));
}

TEST(EditorApiTests,
     SelectEdgeDirectionMatchesAcrossObjectsInWorldSpace) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef first =
      create_selected_box(editor, "Direction Seed Box");
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
          "Direction Rotated Box");
  ASSERT_TRUE(second.ok()) << second.error_message();
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> third =
      editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
          "Direction Non-Matching Rotated Box");
  ASSERT_TRUE(third.ok()) << third.error_message();
  ASSERT_TRUE(
      editor.scene()
          .set_transform(second.value().entity,
                         quader::QuaderTransform{
                             .position = {6.0f, 0.0f, 0.0f},
                             .rotation_euler = {0.0f, 0.0f, 1.5707963f},
                         })
          .ok());
  ASSERT_TRUE(
      editor.scene()
          .set_transform(third.value().entity,
                         quader::QuaderTransform{
                             .position = {12.0f, 0.0f, 0.0f},
                             .rotation_euler = {0.0f, 0.0f, 0.7853982f},
                         })
          .ok());

  ASSERT_TRUE(editor.selection()
                  .select_objects({first, second.value(), third.value()})
                  .ok());
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection()
                  .select_edge(first, 1, 2,
                               quader::QuaderSelectionEdit::Add)
                  .ok());
  ASSERT_EQ(editor.selection().summary().selected_edge_count, 1u);
  ASSERT_EQ(selected_entities(editor).size(), 3u);

  const quader::QuaderApiVoidResult selected_direction =
      editor.selection().select_edge_direction();
  ASSERT_TRUE(selected_direction.ok()) << selected_direction.error_message();
  EXPECT_TRUE(selected_direction.changed());
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 8u);
  EXPECT_EQ(selected_entities(editor).size(), 2u);
  const std::optional<quader::QuaderEntitySnapshot> non_matching_after =
      entity_by_id(editor, third.value().entity);
  ASSERT_TRUE(non_matching_after.has_value());
  EXPECT_FALSE(non_matching_after->selected)
      << "A 45-degree rotation must not be compared as an unchanged local "
         "edge direction.";

#ifndef NDEBUG
  const nlohmann::json selection = nlohmann::json::parse(
      editor.dev_tools().current_selection_json())["selection"];
  const auto object = std::find_if(
      selection["objects"].begin(), selection["objects"].end(),
      [&](const nlohmann::json &candidate) {
        return candidate["modelingObjectId"].get<int>() ==
               second.value().modeling_object_id;
      });
  ASSERT_NE(object, selection["objects"].end());
  std::vector<std::pair<int, int>> rotated_box_edges;
  for (const nlohmann::json &edge : (*object)["selection"]["edges"]) {
    int a = edge["a"].get<int>();
    int b = edge["b"].get<int>();
    if (b < a) {
      std::swap(a, b);
    }
    rotated_box_edges.emplace_back(a, b);
  }
  std::sort(rotated_box_edges.begin(), rotated_box_edges.end());
  EXPECT_EQ(rotated_box_edges,
            (std::vector<std::pair<int, int>>{
                {1, 4},
                {2, 3},
                {5, 6},
                {7, 8},
            }))
      << "The rotated mesh must match its local Y edges to the seed's world X "
         "direction.";
#endif
}

TEST(EditorApiTests, MixedEntitySelectionGroupsMeshAndLightThroughSceneHierarchy) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Mixed Entity Group Box");
    quader::QuaderCreatePointLightArgs args;
    args.name = "Mixed Entity Group Light";
    const quader::QuaderApiResult<quader::QuaderEntityRef> light =
        editor.scene().create_point_light(args);
    ASSERT_TRUE(light.ok()) << light.error_message();

    ASSERT_TRUE(
        editor.selection()
            .select_objects({box.entity, light.value().id}, light.value().id)
            .ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> group =
        editor.scene().group_selection("Mixed Entity Group");
    ASSERT_TRUE(group.ok()) << group.error_message();
    EXPECT_TRUE(group.changed());

    const std::optional<quader::QuaderEntitySnapshot> group_snapshot =
        entity_by_id(editor, group.value().id);
    const std::optional<quader::QuaderEntitySnapshot> box_snapshot =
        entity_by_id(editor, box.entity);
    const std::optional<quader::QuaderEntitySnapshot> light_snapshot =
        entity_by_id(editor, light.value().id);
    ASSERT_TRUE(group_snapshot.has_value());
    ASSERT_TRUE(box_snapshot.has_value());
    ASSERT_TRUE(light_snapshot.has_value());
    EXPECT_EQ(group_snapshot->kind, quader::QuaderEntityKind::Empty);
    EXPECT_TRUE(group_snapshot->selected);
    EXPECT_TRUE(group_snapshot->active);
    EXPECT_EQ(box_snapshot->parent, group.value().id);
    EXPECT_EQ(light_snapshot->parent, group.value().id);
}

TEST(EditorApiTests, SceneSettingsPropertiesUndoAndRedoIndividually) {
    auto exercise = [](std::string_view label, auto mutate) {
        SCOPED_TRACE(std::string(label));
        CommandTestHarness harness;
        quader::QuaderEditorApi& editor = harness.editor;
        const std::optional<quader::QuaderEntitySnapshot> root =
            scene_root(editor);
        ASSERT_TRUE(root.has_value());
        ASSERT_TRUE(root->has_scene_settings);

        const quader::QuaderSceneSettings before = root->scene_settings;
        quader::QuaderSceneSettings edited = before;
        mutate(edited);

        const quader::QuaderApiVoidResult set =
            editor.scene().set_scene_settings(root->id, edited);
        ASSERT_TRUE(set.ok()) << set.error_message();
        EXPECT_TRUE(set.changed());
        expect_scene_settings_eq(scene_root_settings(editor), edited);

        const quader::QuaderApiVoidResult undo =
            editor.commands().execute(quader::quader_command::kEditUndo);
        ASSERT_TRUE(undo.ok()) << undo.error_message();
        EXPECT_TRUE(undo.changed());
        expect_scene_settings_eq(scene_root_settings(editor), before);

        const quader::QuaderApiVoidResult redo =
            editor.commands().execute(quader::quader_command::kEditRedo);
        ASSERT_TRUE(redo.ok()) << redo.error_message();
        EXPECT_TRUE(redo.changed());
        expect_scene_settings_eq(scene_root_settings(editor), edited);
    };

    exercise("environment.environmentId",
             [](quader::QuaderSceneSettings &settings) {
               settings.environment.environment_id = "pillars_2k";
             });
    exercise("environment.enabled", [](quader::QuaderSceneSettings& settings) {
        settings.environment.enabled = false;
    });
    exercise("environment.showBackground",
             [](quader::QuaderSceneSettings &settings) {
               settings.environment.show_background = false;
             });
    exercise("environment.clearColor",
             [](quader::QuaderSceneSettings &settings) {
               settings.environment.clear_color = {0.25f, 0.5f, 0.75f};
             });
    exercise("environment.intensity", [](quader::QuaderSceneSettings& settings) {
        settings.environment.intensity = 2.5f;
    });
    exercise("environment.background_intensity",
             [](quader::QuaderSceneSettings &settings) {
               settings.environment.background_intensity = 0.35f;
             });
    exercise("environment.rotationY",
             [](quader::QuaderSceneSettings &settings) {
               settings.environment.rotation_y = 1.25f;
             });
    exercise("camera.aperture", [](quader::QuaderSceneSettings& settings) {
        settings.camera.aperture = 8.0f;
    });
    exercise("camera.shutter_speed", [](quader::QuaderSceneSettings &settings) {
      settings.camera.shutter_speed = 1.0f / 60.0f;
    });
    exercise("camera.sensitivity", [](quader::QuaderSceneSettings& settings) {
        settings.camera.sensitivity = 400.0f;
    });
    exercise("camera.nearPlane", [](quader::QuaderSceneSettings &settings) {
      settings.camera.near_plane = 0.2f;
    });
    exercise("camera.farPlane", [](quader::QuaderSceneSettings &settings) {
      settings.camera.far_plane = 2500.0f;
    });
    exercise("postProcess.enabled", [](quader::QuaderSceneSettings &settings) {
      settings.post_process.enabled = false;
    });
    exercise("postProcess.antiAliasing",
             [](quader::QuaderSceneSettings &settings) {
               settings.post_process.anti_aliasing = false;
             });
    exercise("postProcess.ambientOcclusion",
             [](quader::QuaderSceneSettings &settings) {
               settings.post_process.ambient_occlusion = true;
             });
    exercise("postProcess.ambientOcclusionIntensity",
             [](quader::QuaderSceneSettings &settings) {
               settings.post_process.ambient_occlusion_intensity = 3.0f;
             });
    exercise("postProcess.ambientOcclusionRadius",
             [](quader::QuaderSceneSettings &settings) {
               settings.post_process.ambient_occlusion_radius = 1.4f;
             });
    exercise("postProcess.bloom", [](quader::QuaderSceneSettings &settings) {
      settings.post_process.bloom = true;
    });
    exercise("postProcess.bloomStrength",
             [](quader::QuaderSceneSettings &settings) {
               settings.post_process.bloom_strength = 0.7f;
             });
    exercise("postProcess.dithering",
             [](quader::QuaderSceneSettings &settings) {
               settings.post_process.dithering = false;
             });
    exercise("postProcess.toneMapping",
             [](quader::QuaderSceneSettings &settings) {
               settings.post_process.tone_mapping =
                   quader::QuaderToneMapping::DisplayRange;
             });
}

TEST(EditorApiTests, SceneSettingsLiveEditsApplyImmediatelyAndCoalesceByMergeKey) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    ASSERT_FALSE(editor.history().can_undo());

    const std::optional<quader::QuaderEntitySnapshot> root = scene_root(editor);
    ASSERT_TRUE(root.has_value());
    ASSERT_TRUE(root->has_scene_settings);
    const quader::QuaderSceneSettings before = root->scene_settings;

    quader::QuaderSceneSettings middle = before;
    middle.environment.intensity = 2.0f;
    quader::QuaderApiVoidResult set = editor.scene().set_scene_settings(
        root->id, middle,
        quader::QuaderSceneEditOptions{
            .merge_key = "inspector.environment.intensity.gesture"});
    ASSERT_TRUE(set.ok()) << set.error_message();
    EXPECT_TRUE(set.changed());
    expect_scene_settings_eq(scene_root_settings(editor), middle);

    quader::QuaderSceneSettings after = middle;
    after.environment.intensity = 3.0f;
    set = editor.scene().set_scene_settings(
        root->id, after,
        quader::QuaderSceneEditOptions{
            .merge_key = "inspector.environment.intensity.gesture"});
    ASSERT_TRUE(set.ok()) << set.error_message();
    EXPECT_TRUE(set.changed());
    expect_scene_settings_eq(scene_root_settings(editor), after);

    ASSERT_TRUE(editor.history().undo().ok());
    expect_scene_settings_eq(scene_root_settings(editor), before);
    EXPECT_FALSE(editor.history().can_undo());

    ASSERT_TRUE(editor.history().redo().ok());
    expect_scene_settings_eq(scene_root_settings(editor), after);
}

TEST(EditorApiTests, SceneBatchReparentPreservesWorldTransformAgainstPendingParentTransform) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs parent_args;
    parent_args.name = "Batch Reparent Parent";
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
        editor.scene().create_entity(parent_args);
    ASSERT_TRUE(parent.ok()) << parent.error_message();

    quader::QuaderCreateEntityArgs child_args;
    child_args.name = "Batch Reparent Child";
    child_args.transform.position = {2.0f, 0.0f, 0.0f};
    const quader::QuaderApiResult<quader::QuaderEntityRef> child =
        editor.scene().create_entity(child_args);
    ASSERT_TRUE(child.ok()) << child.error_message();

    quader::QuaderTransform parent_move;
    parent_move.position = {10.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult batch =
        editor.scene()
            .batch("Batch Reparent Preserve")
            .set_transform(parent.value().id, parent_move)
            .reparent(child.value().id, parent.value().id, true)
            .commit();
    ASSERT_TRUE(batch.ok()) << batch.error_message();
    EXPECT_TRUE(batch.changed());

    std::optional<quader::QuaderEntitySnapshot> parent_snapshot =
        entity_by_id(editor, parent.value().id);
    std::optional<quader::QuaderEntitySnapshot> child_snapshot =
        entity_by_id(editor, child.value().id);
    ASSERT_TRUE(parent_snapshot.has_value());
    ASSERT_TRUE(child_snapshot.has_value());
    EXPECT_EQ(child_snapshot->parent, parent.value().id);
    EXPECT_NEAR(parent_snapshot->transform.position.x, 10.0f, 0.001f);
    EXPECT_NEAR(child_snapshot->transform.position.x, -8.0f, 0.001f);

    ASSERT_TRUE(editor.history().undo().ok());
    parent_snapshot = entity_by_id(editor, parent.value().id);
    child_snapshot = entity_by_id(editor, child.value().id);
    ASSERT_TRUE(parent_snapshot.has_value());
    ASSERT_TRUE(child_snapshot.has_value());
    ASSERT_TRUE(scene_root(editor).has_value());
    EXPECT_EQ(child_snapshot->parent, scene_root(editor)->id);
    EXPECT_NEAR(parent_snapshot->transform.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(child_snapshot->transform.position.x, 2.0f, 0.001f);

    ASSERT_TRUE(editor.history().redo().ok());
    parent_snapshot = entity_by_id(editor, parent.value().id);
    child_snapshot = entity_by_id(editor, child.value().id);
    ASSERT_TRUE(parent_snapshot.has_value());
    ASSERT_TRUE(child_snapshot.has_value());
    EXPECT_EQ(child_snapshot->parent, parent.value().id);
    EXPECT_NEAR(parent_snapshot->transform.position.x, 10.0f, 0.001f);
    EXPECT_NEAR(child_snapshot->transform.position.x, -8.0f, 0.001f);
}

TEST(EditorApiTests, SceneBatchReparentCanKeepLocalTransformAgainstPendingParentTransform) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs parent_args;
    parent_args.name = "Batch Reparent Local Parent";
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
        editor.scene().create_entity(parent_args);
    ASSERT_TRUE(parent.ok()) << parent.error_message();

    quader::QuaderCreateEntityArgs child_args;
    child_args.name = "Batch Reparent Local Child";
    child_args.transform.position = {2.0f, 0.0f, 0.0f};
    const quader::QuaderApiResult<quader::QuaderEntityRef> child =
        editor.scene().create_entity(child_args);
    ASSERT_TRUE(child.ok()) << child.error_message();

    quader::QuaderTransform parent_move;
    parent_move.position = {10.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult batch =
        editor.scene()
            .batch("Batch Reparent Local")
            .set_transform(parent.value().id, parent_move)
            .reparent(child.value().id, parent.value().id, false)
            .commit();
    ASSERT_TRUE(batch.ok()) << batch.error_message();
    EXPECT_TRUE(batch.changed());

    std::optional<quader::QuaderEntitySnapshot> parent_snapshot =
        entity_by_id(editor, parent.value().id);
    std::optional<quader::QuaderEntitySnapshot> child_snapshot =
        entity_by_id(editor, child.value().id);
    ASSERT_TRUE(parent_snapshot.has_value());
    ASSERT_TRUE(child_snapshot.has_value());
    EXPECT_EQ(child_snapshot->parent, parent.value().id);
    EXPECT_NEAR(parent_snapshot->transform.position.x, 10.0f, 0.001f);
    EXPECT_NEAR(child_snapshot->transform.position.x, 2.0f, 0.001f);

    ASSERT_TRUE(editor.history().undo().ok());
    parent_snapshot = entity_by_id(editor, parent.value().id);
    child_snapshot = entity_by_id(editor, child.value().id);
    ASSERT_TRUE(parent_snapshot.has_value());
    ASSERT_TRUE(child_snapshot.has_value());
    ASSERT_TRUE(scene_root(editor).has_value());
    EXPECT_EQ(child_snapshot->parent, scene_root(editor)->id);
    EXPECT_NEAR(parent_snapshot->transform.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(child_snapshot->transform.position.x, 2.0f, 0.001f);

    ASSERT_TRUE(editor.history().redo().ok());
    parent_snapshot = entity_by_id(editor, parent.value().id);
    child_snapshot = entity_by_id(editor, child.value().id);
    ASSERT_TRUE(parent_snapshot.has_value());
    ASSERT_TRUE(child_snapshot.has_value());
    EXPECT_EQ(child_snapshot->parent, parent.value().id);
    EXPECT_NEAR(parent_snapshot->transform.position.x, 10.0f, 0.001f);
    EXPECT_NEAR(child_snapshot->transform.position.x, 2.0f, 0.001f);
}

TEST(EditorApiTests, SelectionApiExposesTypedTargetsThroughPublicQueries) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> first =
        editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Selection First");
    ASSERT_TRUE(first.ok()) << first.error_message();
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> second =
        editor.modeling().create_box(
            quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
            "Selection Second");
    ASSERT_TRUE(second.ok()) << second.error_message();

    ASSERT_TRUE(editor.selection().select_object(first.value()).ok());
    quader::QuaderSelectionSummary summary =
        editor.modeling().selection_summary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(summary.active_object_id, first.value().modeling_object_id);
    const std::optional<quader::QuaderEntitySnapshot> first_snapshot =
        entity_by_id(editor, first.value().entity);
    ASSERT_TRUE(first_snapshot.has_value());
    EXPECT_TRUE(first_snapshot->active);

    ASSERT_TRUE(editor.selection().select_face(first.value(), 1).ok());
    summary = editor.modeling().selection_summary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(summary.active_object_id, first.value().modeling_object_id);
    EXPECT_EQ(summary.active_face_id, 1);
    EXPECT_EQ(summary.selected_face_count, 1u);

    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
    ASSERT_TRUE(
        editor.selection()
            .select_objects({first.value().entity, second.value().entity},
                            second.value().entity)
            .ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> group =
        editor.scene().group_selection("Selection Group");
    ASSERT_TRUE(group.ok()) << group.error_message();
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Group).ok());
    summary = editor.modeling().selection_summary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Group);
    const std::optional<quader::QuaderEntitySnapshot> group_snapshot =
        entity_by_id(editor, group.value().id);
    ASSERT_TRUE(group_snapshot.has_value());
    EXPECT_TRUE(group_snapshot->active);
    EXPECT_TRUE(group_snapshot->selected);
}

TEST(EditorApiTests,
     ComponentSelectionPreservesExplicitActiveElementAcrossIncrementalEdits) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderModelingObjectRef object =
        create_selected_box(editor, "Active Component Box");
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());

    ASSERT_TRUE(editor.selection().select_vertices(object, {1, 2, 3}).ok());
    quader::QuaderSelectionSummary summary = editor.selection().summary();
    ASSERT_EQ(summary.selected_vertex_count, 3u);
    EXPECT_EQ(summary.active_kind, quader::QuaderActiveSelectionKind::Vertex);
    EXPECT_EQ(summary.active_vertex_id, 3);

    ASSERT_TRUE(editor.selection()
                    .select_vertex(object, 4, quader::QuaderSelectionEdit::Add)
                    .ok());
    summary = editor.selection().summary();
    EXPECT_EQ(summary.active_vertex_id, 4);

    ASSERT_TRUE(
        editor.selection()
            .select_vertex(object, 1, quader::QuaderSelectionEdit::Remove)
            .ok());
    summary = editor.selection().summary();
    EXPECT_EQ(summary.active_vertex_id, 4);

    ASSERT_TRUE(
        editor.selection()
            .select_vertex(object, 4, quader::QuaderSelectionEdit::Toggle)
            .ok());
    summary = editor.selection().summary();
    EXPECT_EQ(summary.selected_vertex_count, 2u);
    EXPECT_EQ(summary.active_vertex_id, 2);
}

TEST(EditorApiTests, MixedModelingAndSceneHistoryReplaysChronologicallyThroughPublicHistory) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateBoxArgs args;
    args.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
    args.name = "Chronology Box";
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
        editor.modeling().create_box(args);
    ASSERT_TRUE(created.ok()) << created.error_message();

    const quader::QuaderApiVoidResult renamed =
            editor.scene().rename(created.value().entity, "Chronology Box Renamed");
    ASSERT_TRUE(renamed.ok()) << renamed.error_message();
    EXPECT_TRUE(entity_named(editor, "Chronology Box Renamed").has_value());

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(entity_named(editor, "Chronology Box").has_value());
    EXPECT_FALSE(entity_named(editor, "Chronology Box Renamed").has_value());

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_FALSE(entity_named(editor, "Chronology Box").has_value());
    EXPECT_FALSE(entity_named(editor, "Chronology Box Renamed").has_value());

    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_TRUE(entity_named(editor, "Chronology Box").has_value());
    EXPECT_FALSE(entity_named(editor, "Chronology Box Renamed").has_value());

    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_TRUE(entity_named(editor, "Chronology Box Renamed").has_value());
    EXPECT_FALSE(entity_named(editor, "Chronology Box").has_value());
}

TEST(EditorApiTests, AppCommandsAndDirectOperationsShareChronologicalUndoStack) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderModelingObjectRef original =
        create_selected_box(editor, "Chronology Command Box");

    const quader::QuaderApiVoidResult duplicated = editor.commands().execute(
        editor.commands().command_id_for_modeling_action("duplicate_meshes"));
    ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
    ASSERT_EQ(user_entity_count(editor), 2u);
  EXPECT_EQ(editor.query().history().undo_label, "Duplicate Selected Meshes");

    quader::QuaderTransform moved;
    moved.position = {3.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult transformed =
        editor.scene().set_transform(original.entity, moved);
    ASSERT_TRUE(transformed.ok()) << transformed.error_message();
    std::optional<quader::QuaderEntitySnapshot> original_snapshot =
        entity_by_id(editor, original.entity);
    ASSERT_TRUE(original_snapshot.has_value());
    EXPECT_NEAR(original_snapshot->transform.position.x, 3.0f, 0.001f);
    EXPECT_EQ(editor.query().history().undo_label, "Set Transform");

    const std::vector<quader::EntityId> selected_before_delete =
        selected_entities(editor);
    ASSERT_FALSE(selected_before_delete.empty());
    const quader::QuaderApiVoidResult deleted =
        editor.commands().execute(quader::quader_command::kDeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.error_message();
    EXPECT_EQ(editor.query().history().undo_label, "Delete Selection");

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(user_entity_count(editor), 2u);
    EXPECT_EQ(editor.query().history().redo_label, "Delete Selection");

    ASSERT_TRUE(editor.history().undo().ok());
    original_snapshot = entity_by_id(editor, original.entity);
    ASSERT_TRUE(original_snapshot.has_value());
    EXPECT_NEAR(original_snapshot->transform.position.x, 0.0f, 0.001f);
    EXPECT_EQ(editor.query().history().redo_label, "Set Transform");

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(user_entity_count(editor), 1u);
  EXPECT_EQ(editor.query().history().redo_label, "Duplicate Selected Meshes");

    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(user_entity_count(editor), 2u);
    ASSERT_TRUE(editor.history().redo().ok());
    original_snapshot = entity_by_id(editor, original.entity);
    ASSERT_TRUE(original_snapshot.has_value());
    EXPECT_NEAR(original_snapshot->transform.position.x, 3.0f, 0.001f);
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_LT(user_entity_count(editor), 2u);
}

TEST(EditorApiTests, DuplicateMeshesHistoryRestoresSceneOnlyDuplicateState) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs parent_args;
    parent_args.name = "Duplicate State Parent";
    parent_args.kind = quader::QuaderEntityKind::Empty;
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
        editor.scene().create_entity(parent_args);
    ASSERT_TRUE(parent.ok()) << parent.error_message();

    const quader::QuaderModelingObjectRef source =
        create_selected_box(editor, "Duplicate State Source");
    ASSERT_TRUE(editor.scene().reparent(source.entity, parent.value().id, false).ok());

    quader::QuaderTransform transform;
    transform.position = {2.0f, 3.0f, 4.0f};
    transform.rotation_euler = {0.25f, 0.5f, 0.75f};
    ASSERT_TRUE(editor.scene().set_transform(source.entity, transform).ok());
    ASSERT_TRUE(editor.scene().set_visible(source.entity, false).ok());

    const quader::QuaderApiResult<quader::QuaderAssetRef> material =
        editor.assets().add_material(
            quader::QuaderMaterialArgs{.name = "Duplicate State Material"});
    ASSERT_TRUE(material.ok()) << material.error_message();
    ASSERT_TRUE(
        editor.assets()
            .assign_material_to_entity(source.entity, material.value().id)
            .ok());

    ASSERT_TRUE(editor.selection().select_object(source).ok());
    const quader::QuaderApiVoidResult duplicated = editor.commands().execute(
        editor.commands().command_id_for_modeling_action("duplicate_meshes"));
    ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();

    std::vector<quader::EntityId> selected = selected_entities(editor);
    ASSERT_EQ(selected.size(), 1u);
    const quader::EntityId duplicate_entity = selected.front();
    EXPECT_NE(duplicate_entity, source.entity);
    std::optional<quader::QuaderEntitySnapshot> duplicate_snapshot =
        entity_by_id(editor, duplicate_entity);
    ASSERT_TRUE(duplicate_snapshot.has_value());
    EXPECT_EQ(duplicate_snapshot->parent, parent.value().id);
    EXPECT_NEAR(duplicate_snapshot->transform.position.x, 2.0f, 0.001f);
    EXPECT_NEAR(duplicate_snapshot->transform.rotation_euler.z, 0.75f, 0.001f);
    EXPECT_FALSE(duplicate_snapshot->visible);
    EXPECT_FALSE(duplicate_snapshot->locked);
    EXPECT_TRUE(duplicate_snapshot->has_material);
    EXPECT_EQ(duplicate_snapshot->material, material.value().id);
    EXPECT_TRUE(duplicate_snapshot->has_mesh);
    EXPECT_NE(duplicate_snapshot->mesh, source.mesh);

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_FALSE(entity_by_id(editor, duplicate_entity).has_value());

    ASSERT_TRUE(editor.history().redo().ok());
    duplicate_snapshot = entity_by_id(editor, duplicate_entity);
    ASSERT_TRUE(duplicate_snapshot.has_value());
    EXPECT_EQ(duplicate_snapshot->parent, parent.value().id);
    EXPECT_NEAR(duplicate_snapshot->transform.position.x, 2.0f, 0.001f);
    EXPECT_NEAR(duplicate_snapshot->transform.rotation_euler.z, 0.75f, 0.001f);
    EXPECT_FALSE(duplicate_snapshot->visible);
    EXPECT_FALSE(duplicate_snapshot->locked);
    EXPECT_TRUE(duplicate_snapshot->has_material);
    EXPECT_EQ(duplicate_snapshot->material, material.value().id);
    EXPECT_TRUE(duplicate_snapshot->has_mesh);
    EXPECT_NE(duplicate_snapshot->mesh, source.mesh);
}

TEST(EditorApiTests, DuplicateMeshesActionCommandPreservesSceneOnlyDuplicateState) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs parent_args;
    parent_args.name = "Duplicate Action Parent";
    parent_args.kind = quader::QuaderEntityKind::Empty;
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
        editor.scene().create_entity(parent_args);
    ASSERT_TRUE(parent.ok()) << parent.error_message();

    const quader::QuaderModelingObjectRef source =
        create_selected_box(editor, "Duplicate Action Source");
    ASSERT_TRUE(editor.scene().reparent(source.entity, parent.value().id, false).ok());

    quader::QuaderTransform transform;
    transform.position = {4.0f, 5.0f, 6.0f};
    transform.rotation_euler = {0.5f, 0.25f, 1.0f};
    ASSERT_TRUE(editor.scene().set_transform(source.entity, transform).ok());
    ASSERT_TRUE(editor.scene().set_visible(source.entity, false).ok());

    const quader::QuaderApiResult<quader::QuaderAssetRef> material =
        editor.assets().add_material(
            quader::QuaderMaterialArgs{.name = "Duplicate Action Material"});
    ASSERT_TRUE(material.ok()) << material.error_message();
    ASSERT_TRUE(
        editor.assets()
            .assign_material_to_entity(source.entity, material.value().id)
            .ok());

    ASSERT_TRUE(editor.selection().select_object(source).ok());
    const quader::QuaderApiVoidResult duplicated = editor.commands().execute(
        editor.commands().command_id_for_modeling_action("duplicate_meshes"));
    ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();

    const std::vector<quader::EntityId> selected = selected_entities(editor);
    ASSERT_EQ(selected.size(), 1u);
    const quader::EntityId duplicate_entity = selected.front();
    EXPECT_NE(duplicate_entity, source.entity);
    const std::optional<quader::QuaderEntitySnapshot> duplicate_snapshot =
        entity_by_id(editor, duplicate_entity);
    ASSERT_TRUE(duplicate_snapshot.has_value());
    EXPECT_EQ(duplicate_snapshot->parent, parent.value().id);
    EXPECT_NEAR(duplicate_snapshot->transform.position.x, 4.0f, 0.001f);
    EXPECT_NEAR(duplicate_snapshot->transform.rotation_euler.z, 1.0f, 0.001f);
    EXPECT_FALSE(duplicate_snapshot->visible);
    EXPECT_FALSE(duplicate_snapshot->locked);
    EXPECT_TRUE(duplicate_snapshot->has_material);
    EXPECT_EQ(duplicate_snapshot->material, material.value().id);
    EXPECT_TRUE(duplicate_snapshot->has_mesh);
    EXPECT_NE(duplicate_snapshot->mesh, source.mesh);
}

TEST(EditorApiTests, PublicNoopOperationsPreserveRedoUntilChangedAppCommand) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Redo Noop Box");
    const quader::QuaderApiVoidResult renamed = editor.scene().rename(box.entity, "Redo Noop Renamed");
    ASSERT_TRUE(renamed.ok()) << renamed.error_message();
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(editor.history().can_redo());

    const quader::QuaderApiVoidResult no_op_rename =
        editor.scene().rename(box.entity, "Redo Noop Box");
    ASSERT_TRUE(no_op_rename.ok()) << no_op_rename.error_message();
    EXPECT_FALSE(no_op_rename.changed());
    EXPECT_TRUE(editor.history().can_redo());
    EXPECT_EQ(editor.query().history().redo_label, "Rename Entity");

    const quader::QuaderApiVoidResult duplicate =
        editor.commands().execute(quader::quader_command::kDuplicateSelection);
    ASSERT_TRUE(duplicate.ok()) << duplicate.error_message();
    EXPECT_TRUE(duplicate.changed());
    EXPECT_FALSE(editor.history().can_redo());
}

TEST(EditorApiTests, PublicAssetMutationsRejectDanglingReferences) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderMaterialAsset invalid_material;
    invalid_material.name = "Dangling Texture Material";
    invalid_material.texture_slots.emplace("baseColor", 9000u);
    const quader::QuaderApiResult<quader::QuaderAssetRef> added =
        editor.assets().add_material(invalid_material);
    EXPECT_FALSE(added.ok());
    EXPECT_EQ(added.error_message(), "Texture asset does not exist");

    const quader::QuaderApiResult<quader::QuaderAssetRef> valid_material =
        editor.assets().add_material(
            quader::QuaderMaterialArgs{.name = "Valid Material"});
    ASSERT_TRUE(valid_material.ok()) << valid_material.error_message();
    const quader::QuaderApiVoidResult replaced =
        editor.assets().replace_material(valid_material.value().id,
                                         invalid_material);
    EXPECT_FALSE(replaced.ok());
    EXPECT_EQ(replaced.error_message(), "Texture asset does not exist");

    quader::QuaderMaterialAsset unsupported_material;
    unsupported_material.name = "Unsupported Texture Slot Material";
    unsupported_material.texture_slots.emplace("emissiveTexture", 9000u);
    const quader::QuaderApiResult<quader::QuaderAssetRef> unsupported_added =
        editor.assets().add_material(unsupported_material);
    EXPECT_FALSE(unsupported_added.ok());
    EXPECT_EQ(unsupported_added.error_message(),
              "Unsupported material texture slot");

    quader::QuaderTextureAsset texture;
    texture.name = "Aliased Base Color";
    texture.usage = quader::QuaderTextureUsage::BaseColor;
    const quader::QuaderApiResult<quader::QuaderAssetRef> texture_ref =
        editor.assets().add_texture(std::move(texture));
    ASSERT_TRUE(texture_ref.ok()) << texture_ref.error_message();

    const quader::QuaderApiVoidResult aliased_slot =
        editor.assets().set_material_texture(
            valid_material.value().id, "baseColor", texture_ref.value().id);
    ASSERT_TRUE(aliased_slot.ok()) << aliased_slot.error_message();

    const quader::QuaderApiVoidResult unsupported_slot =
        editor.assets().set_material_texture(
            valid_material.value().id, "emissiveTexture",
            texture_ref.value().id);
    EXPECT_FALSE(unsupported_slot.ok());
    EXPECT_EQ(unsupported_slot.error_message(),
              "Unsupported material texture slot");

    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Dangling Mesh Component Box");
    quader::QuaderMeshComponent invalid_mesh;
    invalid_mesh.mesh = quader::kInvalidAssetId;
    const quader::QuaderApiVoidResult invalid_mesh_result =
        editor.scene().set_mesh_component(box.entity, invalid_mesh);
    EXPECT_FALSE(invalid_mesh_result.ok());
    EXPECT_EQ(invalid_mesh_result.error_message(), "Mesh asset is invalid");

    quader::QuaderMeshComponent missing_mesh;
    missing_mesh.mesh = 9100u;
    const quader::QuaderApiVoidResult missing_mesh_result =
        editor.scene().set_mesh_component(box.entity, missing_mesh);
    EXPECT_FALSE(missing_mesh_result.ok());
    EXPECT_EQ(missing_mesh_result.error_message(), "Mesh asset does not exist");
}

TEST(EditorApiTests, DirectApiStatusesUseFullRevisionStamp) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderRevisionStamp initial = editor.revisions();
    const quader::QuaderApiVoidResult scene_save_failure =
        editor.scene_documents().save_as_native_path(
            *editor.scene_documents().active_scene_id(), {});
    EXPECT_FALSE(scene_save_failure.ok());
    EXPECT_EQ(scene_save_failure.revisions.project_revision,
              initial.project_revision);
    EXPECT_EQ(scene_save_failure.revisions.scene_revision,
              initial.scene_revision);
    EXPECT_EQ(scene_save_failure.revisions.asset_revision,
              initial.asset_revision);
    EXPECT_EQ(scene_save_failure.revisions.modeling_content_revision,
              initial.modeling_content_revision);
    EXPECT_EQ(scene_save_failure.revisions.modeling_selection_revision,
              initial.modeling_selection_revision);

    const quader::QuaderApiVoidResult unknown_setting =
        editor.tools().set_setting("edge_bevel", "__missing__", 1.0);
    EXPECT_FALSE(unknown_setting.ok());
    EXPECT_EQ(unknown_setting.mutation_kind, quader::QuaderMutationKind::None);
    EXPECT_EQ(unknown_setting.revisions.modeling_content_revision,
              editor.revisions().modeling_content_revision);

    const quader::QuaderApiVoidResult updated_setting =
        editor.tools().set_setting("edge_bevel", "width", 0.25);
    EXPECT_TRUE(updated_setting.ok()) << updated_setting.error_message();
    EXPECT_EQ(updated_setting.mutation_kind,
              quader::QuaderMutationKind::Modeling);
    EXPECT_EQ(updated_setting.revisions.modeling_content_revision,
              editor.revisions().modeling_content_revision);
    EXPECT_EQ(updated_setting.revisions.modeling_selection_revision,
              editor.revisions().modeling_selection_revision);

}

TEST(EditorApiTests, ToolsApiSetsCutKeepModeThroughPublicSetting) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    create_selected_box(editor, "Cut Setting Box");
    const quader::QuaderApiVoidResult activate_cut =
        editor.tools().activate(quader::ViewportToolId::Cut);
    ASSERT_TRUE(activate_cut.ok()) << activate_cut.error_message();
    const quader::QuaderToolSettingsTarget cut_settings =
        editor.tools().active_settings();
    ASSERT_TRUE(cut_settings.active);
    EXPECT_EQ(cut_settings.tool_id, "cut");
    EXPECT_EQ(
        cut_settings.description,
        "Draw a cutting plane through the selection and choose which side to "
        "keep.");
    EXPECT_TRUE(cut_settings.can_reset);
    const quader::QuaderToolSettingControl *keep_mode =
        find_tool_setting_control(cut_settings, "operation.cut.keep_mode");
    ASSERT_NE(keep_mode, nullptr);
    EXPECT_EQ(keep_mode->kind, quader::QuaderToolSettingControlKind::Choice);

    const quader::QuaderApiVoidResult updated_cut_setting =
        editor.tools().set_setting(
            "cut", "operation.cut.keep_mode",
            static_cast<double>(
                static_cast<int>(quader::QuaderCutMode::DiscardRight)));
    ASSERT_TRUE(updated_cut_setting.ok())
        << updated_cut_setting.error_message();
  EXPECT_EQ(editor.query().tool_status().cut_mode,
              quader::QuaderCutMode::DiscardRight);

    const std::uint64_t edited_revision =
        editor.tools().active_settings_revision();
    const quader::QuaderApiVoidResult reset =
        editor.tools().reset_active_settings();
    ASSERT_TRUE(reset.ok()) << reset.error_message();
    EXPECT_TRUE(reset.changed());
    EXPECT_EQ(editor.query().tool_status().cut_mode,
              quader::QuaderCutMode::KeepBoth);
    const std::uint64_t reset_revision =
        editor.tools().active_settings_revision();
    EXPECT_GT(reset_revision, edited_revision);

    const quader::QuaderApiVoidResult repeated =
        editor.tools().reset_active_settings();
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.changed());
    EXPECT_EQ(editor.tools().active_settings_revision(), reset_revision);
}

TEST(EditorApiTests, HalfCutPreviewExposesLiveAxisAndPartSettings) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    create_selected_box(editor, "Half-Cut Settings Box");
    const std::string command_id =
        editor.commands().command_id_for_modeling_action("half_cut");
    EXPECT_EQ(command_id,
              std::string(quader::quader_command::kModelingHalfCut.string()));
    const quader::QuaderApiVoidResult preview =
        editor.commands().execute(command_id);
    ASSERT_TRUE(preview.ok()) << preview.error_message();

    quader::QuaderToolSettingsTarget settings =
        editor.tools().active_settings();
    ASSERT_TRUE(settings.active);
    EXPECT_TRUE(settings.preview_active);
    EXPECT_EQ(settings.tool_id, "half_cut");
  EXPECT_EQ(settings.title, "Half-Cut Selected Meshes");
    EXPECT_EQ(
        settings.description,
        "Split selected meshes along a local axis and choose how the parts are "
        "produced.");
    EXPECT_TRUE(settings.can_reset);
    EXPECT_TRUE(settings.can_apply);
    EXPECT_TRUE(settings.can_cancel);

    const quader::QuaderToolSettingControl *axis =
        find_tool_setting_control(settings, "operation.half_cut.axis");
    ASSERT_NE(axis, nullptr);
    EXPECT_EQ(axis->kind, quader::QuaderToolSettingControlKind::Choice);
    EXPECT_EQ(axis->choice_value, "0");
    ASSERT_EQ(axis->choices.size(), 3u);
    EXPECT_EQ(axis->choices[0].label, "X");
    EXPECT_EQ(axis->choices[1].label, "Y");
    EXPECT_EQ(axis->choices[2].label, "Z");

    const quader::QuaderToolSettingControl *separate =
        find_tool_setting_control(
            settings, "operation.half_cut.separate_parts");
    const quader::QuaderToolSettingControl *fill =
        find_tool_setting_control(settings, "operation.half_cut.fill_parts");
    ASSERT_NE(separate, nullptr);
    ASSERT_NE(fill, nullptr);
    EXPECT_EQ(separate->kind, quader::QuaderToolSettingControlKind::Toggle);
    EXPECT_EQ(fill->kind, quader::QuaderToolSettingControlKind::Toggle);
    EXPECT_FALSE(separate->bool_value);
    EXPECT_TRUE(fill->bool_value);

    ASSERT_TRUE(editor.tools()
                    .set_setting("half_cut", "operation.half_cut.axis", 2.0)
                    .ok());
    ASSERT_TRUE(editor.tools()
                    .set_setting("half_cut",
                                 "operation.half_cut.separate_parts", true)
                    .ok());
    ASSERT_TRUE(editor.tools()
                    .set_setting("half_cut", "operation.half_cut.fill_parts",
                                 false)
                    .ok());

    settings = editor.tools().active_settings();
    axis = find_tool_setting_control(settings, "operation.half_cut.axis");
    separate = find_tool_setting_control(
        settings, "operation.half_cut.separate_parts");
    fill = find_tool_setting_control(settings, "operation.half_cut.fill_parts");
    ASSERT_NE(axis, nullptr);
    ASSERT_NE(separate, nullptr);
    ASSERT_NE(fill, nullptr);
    EXPECT_EQ(axis->choice_value, "2");
    EXPECT_TRUE(separate->bool_value);
    EXPECT_FALSE(fill->bool_value);

    const quader::QuaderApiVoidResult reset =
        editor.tools().reset_active_settings();
    ASSERT_TRUE(reset.ok()) << reset.error_message();
    EXPECT_TRUE(reset.changed());
    settings = editor.tools().active_settings();
    axis = find_tool_setting_control(settings, "operation.half_cut.axis");
    separate = find_tool_setting_control(
        settings, "operation.half_cut.separate_parts");
    fill =
        find_tool_setting_control(settings, "operation.half_cut.fill_parts");
    ASSERT_NE(axis, nullptr);
    ASSERT_NE(separate, nullptr);
    ASSERT_NE(fill, nullptr);
    EXPECT_EQ(axis->choice_value, "0");
    EXPECT_FALSE(separate->bool_value);
    EXPECT_TRUE(fill->bool_value);

    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
    EXPECT_FALSE(editor.tools().active_settings().active);
}

TEST(EditorApiTests,
     HalfCutSeparatedPreviewPartsPreserveSourceSceneIdentityAcrossToggles) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
        editor.scene().create_entity({
            .name = "Half-Cut Parent",
            .kind = quader::QuaderEntityKind::Empty,
        });
    ASSERT_TRUE(parent.ok()) << parent.error_message();
    const quader::QuaderModelingObjectRef source =
        create_selected_box(editor, "Half-Cut Identity Source");
    ASSERT_TRUE(editor.scene().reparent(source.entity, parent.value().id, false)
                    .ok());

    quader::QuaderTransform transform;
    transform.position = {4.0f, 5.0f, 6.0f};
    transform.rotation_euler = {0.25f, 0.5f, 0.75f};
    ASSERT_TRUE(editor.scene().set_transform(source.entity, transform).ok());
    ASSERT_TRUE(editor.scene().set_visible(source.entity, false).ok());
    const quader::QuaderApiResult<quader::QuaderAssetRef> material =
        editor.assets().add_material(
            quader::QuaderMaterialArgs{.name = "Half-Cut Material"});
    ASSERT_TRUE(material.ok()) << material.error_message();
    ASSERT_TRUE(editor.assets()
                    .assign_material_to_entity(source.entity,
                                               material.value().id)
                    .ok());

    ASSERT_TRUE(editor.commands()
                    .execute(editor.commands().command_id_for_modeling_action(
                        "half_cut"))
                    .ok());

    const auto separated_part = [&]()
        -> std::optional<quader::QuaderEntitySnapshot> {
      for (const quader::QuaderEntitySnapshot &candidate :
           editor.query().entities()) {
        if (candidate.id != source.entity && candidate.has_mesh) {
          return candidate;
        }
      }
      return std::nullopt;
    };

    ASSERT_TRUE(editor.tools()
                    .set_setting("half_cut",
                                 "operation.half_cut.separate_parts", true)
                    .ok());
    std::optional<quader::QuaderEntitySnapshot> part = separated_part();
    ASSERT_TRUE(part.has_value());
    EXPECT_EQ(part->parent, parent.value().id);
    EXPECT_NEAR(part->transform.position.x, 4.0f, 0.001f);
    EXPECT_NEAR(part->transform.position.y, 5.0f, 0.001f);
    EXPECT_NEAR(part->transform.position.z, 6.0f, 0.001f);
    EXPECT_NEAR(part->transform.rotation_euler.z, 0.75f, 0.001f);
    EXPECT_FALSE(part->visible);
    EXPECT_TRUE(part->has_material);
    EXPECT_EQ(part->material, material.value().id);

    ASSERT_TRUE(editor.tools()
                    .set_setting("half_cut",
                                 "operation.half_cut.separate_parts", false)
                    .ok());
    EXPECT_FALSE(separated_part().has_value());

    ASSERT_TRUE(editor.tools()
                    .set_setting("half_cut",
                                 "operation.half_cut.separate_parts", true)
                    .ok());
    part = separated_part();
    ASSERT_TRUE(part.has_value());
    EXPECT_EQ(part->parent, parent.value().id);
    EXPECT_NEAR(part->transform.position.x, 4.0f, 0.001f);
    EXPECT_FALSE(part->visible);
    EXPECT_TRUE(part->has_material);
    EXPECT_EQ(part->material, material.value().id);

    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
    EXPECT_FALSE(separated_part().has_value());
    EXPECT_TRUE(entity_by_id(editor, source.entity).has_value());
}

TEST(EditorApiTests,
     HalfCutSeparatedPreviewPreservesEachSourceIdentityForMultipleMeshes) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiResult<quader::QuaderEntityRef> first_parent =
        editor.scene().create_entity({
            .name = "First Half-Cut Parent",
            .kind = quader::QuaderEntityKind::Empty,
        });
    const quader::QuaderApiResult<quader::QuaderEntityRef> second_parent =
        editor.scene().create_entity({
            .name = "Second Half-Cut Parent",
            .kind = quader::QuaderEntityKind::Empty,
        });
    ASSERT_TRUE(first_parent.ok()) << first_parent.error_message();
    ASSERT_TRUE(second_parent.ok()) << second_parent.error_message();

    const quader::QuaderModelingObjectRef first =
        create_selected_box(editor, "First Half-Cut Source");
    const quader::QuaderModelingObjectRef second =
        create_selected_box(editor, "Second Half-Cut Source");
    ASSERT_TRUE(editor.scene()
                    .reparent(first.entity, first_parent.value().id, false)
                    .ok());
    ASSERT_TRUE(editor.scene()
                    .reparent(second.entity, second_parent.value().id, false)
                    .ok());

    quader::QuaderTransform first_transform;
    first_transform.position = {3.0f, 4.0f, 5.0f};
    quader::QuaderTransform second_transform;
    second_transform.position = {-6.0f, 7.0f, 8.0f};
    ASSERT_TRUE(editor.scene()
                    .set_transform(first.entity, first_transform)
                    .ok());
    ASSERT_TRUE(editor.scene()
                    .set_transform(second.entity, second_transform)
                    .ok());
    ASSERT_TRUE(editor.scene().set_visible(first.entity, false).ok());

    const auto first_material = editor.assets().add_material(
        quader::QuaderMaterialArgs{.name = "First Half-Cut Material"});
    const auto second_material = editor.assets().add_material(
        quader::QuaderMaterialArgs{.name = "Second Half-Cut Material"});
    ASSERT_TRUE(first_material.ok()) << first_material.error_message();
    ASSERT_TRUE(second_material.ok()) << second_material.error_message();
    ASSERT_TRUE(editor.assets()
                    .assign_material_to_entity(first.entity,
                                               first_material.value().id)
                    .ok());
    ASSERT_TRUE(editor.assets()
                    .assign_material_to_entity(second.entity,
                                               second_material.value().id)
                    .ok());
    ASSERT_TRUE(editor.selection()
                    .select_objects({first.entity, second.entity},
                                    second.entity)
                    .ok());

    ASSERT_TRUE(editor.commands()
                    .execute(editor.commands().command_id_for_modeling_action(
                        "half_cut"))
                    .ok());
    ASSERT_TRUE(editor.tools()
                    .set_setting("half_cut",
                                 "operation.half_cut.separate_parts", true)
                    .ok());

    std::vector<quader::QuaderEntitySnapshot> separated_parts;
    for (const quader::QuaderEntitySnapshot &candidate :
         editor.query().entities()) {
      if (candidate.has_mesh && candidate.id != first.entity &&
          candidate.id != second.entity) {
        separated_parts.push_back(candidate);
      }
    }
    ASSERT_EQ(separated_parts.size(), 2U);

    const auto part_for_parent = [&](quader::EntityId parent)
        -> const quader::QuaderEntitySnapshot * {
      const auto found = std::ranges::find_if(
          separated_parts, [parent](const quader::QuaderEntitySnapshot &part) {
            return part.parent == parent;
          });
      return found == separated_parts.end() ? nullptr : &*found;
    };
    const quader::QuaderEntitySnapshot *first_part =
        part_for_parent(first_parent.value().id);
    const quader::QuaderEntitySnapshot *second_part =
        part_for_parent(second_parent.value().id);
    ASSERT_NE(first_part, nullptr);
    ASSERT_NE(second_part, nullptr);
    EXPECT_NEAR(first_part->transform.position.x, 3.0f, 0.001f);
    EXPECT_FALSE(first_part->visible);
    EXPECT_EQ(first_part->material, first_material.value().id);
    EXPECT_NEAR(second_part->transform.position.x, -6.0f, 0.001f);
    EXPECT_TRUE(second_part->visible);
    EXPECT_EQ(second_part->material, second_material.value().id);

    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
    EXPECT_EQ(editor.query().modeling_objects().size(), 2U);
}

TEST(EditorApiTests,
     RadialArraySettingsStayAvailableWhileTheCenterIsBeingPlaced) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  (void)create_selected_box(editor, "Radial Array Settings Source");

  const quader::QuaderApiVoidResult activated =
      editor.tools().activate(quader::ViewportToolId::RadialArray);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  EXPECT_EQ(editor.tools().active_tool(), "radial_array");

  quader::QuaderToolSettingsTarget settings =
      editor.tools().active_settings();
  ASSERT_TRUE(settings.active);
  EXPECT_FALSE(settings.preview_active);
  EXPECT_EQ(settings.tool_id, "radial_array");
  EXPECT_EQ(settings.title, "Radial Array");
  EXPECT_EQ(settings.description,
            "Choose a center and arrange copies around its axis.");
  EXPECT_TRUE(settings.can_reset);
  EXPECT_TRUE(settings.apply_visible);
  EXPECT_FALSE(settings.can_apply);
  EXPECT_TRUE(settings.can_cancel);

  const quader::QuaderToolSettingControl *degrees =
      find_tool_setting_control(settings, "radial_array.degrees");
  const quader::QuaderToolSettingControl *copies =
      find_tool_setting_control(settings, "radial_array.copies");
  const quader::QuaderToolSettingControl *repeats =
      find_tool_setting_control(settings, "radial_array.repeats");
  const quader::QuaderToolSettingControl *spacing =
      find_tool_setting_control(settings, "radial_array.repeat_spacing");
  ASSERT_NE(degrees, nullptr);
  ASSERT_NE(copies, nullptr);
  ASSERT_NE(repeats, nullptr);
  ASSERT_NE(spacing, nullptr);
  EXPECT_DOUBLE_EQ(degrees->number_value, 360.0);
  EXPECT_DOUBLE_EQ(degrees->min_value, 0.0);
  EXPECT_DOUBLE_EQ(degrees->max_value, 360.0);
  EXPECT_DOUBLE_EQ(copies->number_value, 8.0);
  EXPECT_DOUBLE_EQ(copies->min_value, 2.0);
  EXPECT_DOUBLE_EQ(copies->max_value, 128.0);
  EXPECT_DOUBLE_EQ(repeats->number_value, 1.0);
  EXPECT_DOUBLE_EQ(repeats->min_value, 1.0);
  EXPECT_DOUBLE_EQ(repeats->max_value, 128.0);
  EXPECT_DOUBLE_EQ(spacing->number_value, 1.0);
  EXPECT_DOUBLE_EQ(spacing->min_value, 0.0);
  EXPECT_DOUBLE_EQ(spacing->max_value, 16384.0);
  EXPECT_DOUBLE_EQ(spacing->step, 0.001);

  ASSERT_TRUE(editor.tools()
                  .set_setting("radial_array", "radial_array.degrees", 270.0)
                  .ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("radial_array", "radial_array.copies", 16.0)
                  .ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("radial_array", "radial_array.repeats", 2.0)
                  .ok());
  ASSERT_TRUE(
      editor.tools()
          .set_setting("radial_array", "radial_array.repeat_spacing", 2.5)
          .ok());

  settings = editor.tools().active_settings();
  const quader::QuaderToolSettingControl *changed_degrees =
      find_tool_setting_control(settings, "radial_array.degrees");
  const quader::QuaderToolSettingControl *changed_copies =
      find_tool_setting_control(settings, "radial_array.copies");
  const quader::QuaderToolSettingControl *changed_repeats =
      find_tool_setting_control(settings, "radial_array.repeats");
  const quader::QuaderToolSettingControl *changed_spacing =
      find_tool_setting_control(settings, "radial_array.repeat_spacing");
  ASSERT_NE(changed_degrees, nullptr);
  ASSERT_NE(changed_copies, nullptr);
  ASSERT_NE(changed_repeats, nullptr);
  ASSERT_NE(changed_spacing, nullptr);
  EXPECT_DOUBLE_EQ(changed_degrees->number_value, 270.0);
  EXPECT_DOUBLE_EQ(changed_copies->number_value, 16.0);
  EXPECT_DOUBLE_EQ(changed_repeats->number_value, 2.0);
  EXPECT_DOUBLE_EQ(changed_repeats->max_value, 64.0);
  EXPECT_DOUBLE_EQ(changed_spacing->number_value, 2.5);

  const quader::QuaderApiVoidResult reset =
      editor.tools().reset_active_settings();
  ASSERT_TRUE(reset.ok()) << reset.error_message();
  EXPECT_TRUE(reset.changed());
  settings = editor.tools().active_settings();
  degrees = find_tool_setting_control(settings, "radial_array.degrees");
  copies = find_tool_setting_control(settings, "radial_array.copies");
  repeats = find_tool_setting_control(settings, "radial_array.repeats");
  spacing =
      find_tool_setting_control(settings, "radial_array.repeat_spacing");
  ASSERT_NE(degrees, nullptr);
  ASSERT_NE(copies, nullptr);
  ASSERT_NE(repeats, nullptr);
  ASSERT_NE(spacing, nullptr);
  EXPECT_DOUBLE_EQ(degrees->number_value, 360.0);
  EXPECT_DOUBLE_EQ(copies->number_value, 8.0);
  EXPECT_DOUBLE_EQ(repeats->number_value, 1.0);
  EXPECT_DOUBLE_EQ(spacing->number_value, 1.0);

  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
}

TEST(EditorApiTests, EdgeBevelToolSettingsUseOperationSchemaWidthLimits) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Edge Bevel Schema Settings Source");
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(box, 1, 2).ok());
  ASSERT_TRUE(editor.selection().select_all().ok());
  ASSERT_TRUE(editor.commands()
                  .execute(editor.commands().command_id_for_modeling_action(
                      "bevel_edges"))
                  .ok());

  const quader::QuaderToolSettingsTarget settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *width = find_tool_setting_control(
      settings, "operation.edge_bevel.width");
  ASSERT_NE(width, nullptr);
  EXPECT_DOUBLE_EQ(width->min_value, 0.0);
  EXPECT_DOUBLE_EQ(width->max_value, 512.0);
  EXPECT_DOUBLE_EQ(width->step, 0.001);

  ASSERT_TRUE(editor.tools()
                  .set_setting("edge_bevel", "operation.edge_bevel.width",
                               900.0)
                  .ok());
  const quader::QuaderToolSettingsTarget normalized_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *normalized =
      find_tool_setting_control(normalized_settings,
                                "operation.edge_bevel.width");
  ASSERT_NE(normalized, nullptr);
  EXPECT_DOUBLE_EQ(normalized->number_value, 512.0);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(EditorApiTests,
     LinearArrayToolAcceptsMultipleMeshesAndKeepsSharedEndpointFixed) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;

  EXPECT_FALSE(editor.tools().can_activate(quader::ViewportToolId::LinearArray));
  const std::optional<quader::QuaderCommandDescriptor> unavailable =
      command_descriptor(editor,
                         quader::quader_command::kToolsActivateLinearArray);
  ASSERT_TRUE(unavailable.has_value());
  EXPECT_FALSE(unavailable->enabled);
  EXPECT_TRUE(unavailable->shortcut.empty());

  const quader::QuaderModelingObjectRef source =
      create_selected_box(editor, "Linear Array Tool Source");
  const quader::QuaderModelingObjectRef second =
      create_selected_box(editor, "Linear Array Tool Second");
  ASSERT_TRUE(editor.scene()
                  .set_transform(second.entity,
                                 {.position = {10.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects({source.entity, second.entity}, source.entity)
                  .ok());
  ASSERT_TRUE(editor.tools().can_activate(quader::ViewportToolId::LinearArray));
  const quader::QuaderApiVoidResult activated =
      editor.tools().activate(quader::ViewportToolId::LinearArray);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  EXPECT_EQ(editor.tools().active_tool(), "linear_array");

  const quader::QuaderToolSettingsTarget initial_settings =
      editor.tools().active_settings();
  ASSERT_TRUE(initial_settings.active);
  EXPECT_TRUE(initial_settings.preview_active);
  EXPECT_EQ(initial_settings.tool_id, "linear_array");
  EXPECT_EQ(initial_settings.title, "Linear Array");
  EXPECT_EQ(initial_settings.description,
            "Drag an endpoint to create evenly spaced copies along a line.");
  EXPECT_TRUE(initial_settings.can_reset);
  EXPECT_TRUE(initial_settings.can_apply);
  EXPECT_TRUE(initial_settings.can_cancel);
  const quader::QuaderToolSettingControl *count =
      find_tool_setting_control(initial_settings, "linear_array.count");
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(count->kind, quader::QuaderToolSettingControlKind::Number);
  EXPECT_EQ(count->label, "Num Copies");
  EXPECT_DOUBLE_EQ(count->number_value, 4.0);
  EXPECT_DOUBLE_EQ(count->min_value, 2.0);
  EXPECT_DOUBLE_EQ(count->max_value, 128.0);

  std::vector<quader::QuaderToolPreviewMesh> previews =
      editor.render().tool_preview_meshes();
  ASSERT_EQ(previews.size(), 2u);
  EXPECT_EQ(previews[0].source_entity, second.entity);
  EXPECT_EQ(previews[1].source_entity, source.entity);
  ASSERT_EQ(previews[0].instance_transforms.size(), 3u);
  ASSERT_EQ(previews[1].instance_transforms.size(), 3u);
  EXPECT_NEAR(previews[0].instance_transforms[0].values[12], 22.0f,
              kTestPositionEpsilon);
  EXPECT_NEAR(previews[0].instance_transforms[1].values[12], 34.0f,
              kTestPositionEpsilon);
  EXPECT_NEAR(previews[0].instance_transforms[2].values[12], 46.0f,
              kTestPositionEpsilon);
  EXPECT_NEAR(previews[1].instance_transforms[0].values[12], 12.0f,
              kTestPositionEpsilon);
  EXPECT_NEAR(previews[1].instance_transforms[2].values[12], 36.0f,
              kTestPositionEpsilon);

  const quader::QuaderApiVoidResult changed_count = editor.tools().set_setting(
      "linear_array", "linear_array.count", 6.0);
  ASSERT_TRUE(changed_count.ok()) << changed_count.error_message();
  previews = editor.render().tool_preview_meshes();
  ASSERT_EQ(previews.size(), 2u);
  ASSERT_EQ(previews[0].instance_transforms.size(), 5u);
  ASSERT_EQ(previews[1].instance_transforms.size(), 5u);
  EXPECT_NEAR(previews[0].instance_transforms.front().values[12], 17.2f,
              kTestPositionEpsilon);
  EXPECT_NEAR(previews[0].instance_transforms.back().values[12], 46.0f,
              kTestPositionEpsilon);
  EXPECT_NEAR(previews[1].instance_transforms.front().values[12], 7.2f,
              kTestPositionEpsilon);
  EXPECT_NEAR(previews[1].instance_transforms.back().values[12], 36.0f,
              kTestPositionEpsilon);

  const quader::QuaderApiVoidResult reset =
      editor.tools().reset_active_settings();
  ASSERT_TRUE(reset.ok()) << reset.error_message();
  EXPECT_TRUE(reset.changed());
  const quader::QuaderToolSettingsTarget reset_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *reset_count =
      find_tool_setting_control(reset_settings, "linear_array.count");
  ASSERT_NE(reset_count, nullptr);
  EXPECT_DOUBLE_EQ(reset_count->number_value, 4.0);
  previews = editor.render().tool_preview_meshes();
  ASSERT_EQ(previews.size(), 2u);
  EXPECT_EQ(previews[0].instance_transforms.size(), 3u);
  EXPECT_EQ(previews[1].instance_transforms.size(), 3u);

  const quader::QuaderApiVoidResult canceled =
      editor.tools().cancel_active_tool_preview();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_TRUE(editor.render().tool_preview_meshes().empty());
  EXPECT_EQ(user_entity_count(editor), 2u);
}

TEST(EditorApiTests, LinearArrayToolApplyUsesTheAtomicModelingCommand) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef primary =
      create_selected_box(editor, "Linear Array Apply Primary");
  const quader::QuaderModelingObjectRef second =
      create_selected_box(editor, "Linear Array Apply Second");
  ASSERT_TRUE(editor.scene()
                  .set_transform(second.entity,
                                 {.position = {10.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects({primary.entity, second.entity},
                                  primary.entity)
                  .ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::LinearArray).ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("linear_array", "linear_array.count", 3.0)
                  .ok());
  const quader::QuaderApiVoidResult applied =
      editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  EXPECT_TRUE(applied.changed());
  EXPECT_EQ(user_entity_count(editor), 6u);
  EXPECT_EQ(selected_entities(editor).size(), 6u);
  EXPECT_EQ(editor.query().history().undo_label, "Linear Array");
  EXPECT_EQ(editor.tools().active_tool(), "select");

  std::vector<float> positions;
  std::optional<quader::QuaderEntitySnapshot> active;
  for (const quader::QuaderEntitySnapshot &entity : editor.query().entities()) {
    if (entity.is_scene_root) {
      continue;
    }
    positions.push_back(entity.transform.position.x);
    if (entity.active) {
      active = entity;
    }
  }
  std::sort(positions.begin(), positions.end());
  ASSERT_EQ(positions.size(), 6u);
  EXPECT_NEAR(positions[0], 0.0f, kTestPositionEpsilon);
  EXPECT_NEAR(positions[1], 10.0f, kTestPositionEpsilon);
  EXPECT_NEAR(positions[2], 18.0f, kTestPositionEpsilon);
  EXPECT_NEAR(positions[3], 28.0f, kTestPositionEpsilon);
  EXPECT_NEAR(positions[4], 36.0f, kTestPositionEpsilon);
  EXPECT_NEAR(positions[5], 46.0f, kTestPositionEpsilon);
  ASSERT_TRUE(active.has_value());
  EXPECT_NEAR(active->transform.position.x, 36.0f, kTestPositionEpsilon);

  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(user_entity_count(editor), 2u);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(user_entity_count(editor), 6u);
  EXPECT_EQ(selected_entities(editor).size(), 6u);
}

TEST(EditorApiTests, LinearArrayToolEnforcesTheShared1024ElementLimit) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  std::vector<quader::EntityId> sources;
  for (int index = 0; index < 9; ++index) {
    sources.push_back(create_selected_box(
                          editor, "Linear Limit Source " +
                                      std::to_string(index))
                          .entity);
  }
  ASSERT_TRUE(editor.selection().select_objects(sources, sources.back()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::LinearArray).ok());

  const quader::QuaderToolSettingsTarget settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *count =
      find_tool_setting_control(settings, "linear_array.count");
  ASSERT_NE(count, nullptr);
  EXPECT_DOUBLE_EQ(count->max_value, 113.0);
  const quader::QuaderApiVoidResult oversized = editor.tools().set_setting(
      "linear_array", "linear_array.count", 114.0);
  EXPECT_FALSE(oversized.ok());
  EXPECT_FALSE(oversized.changed());
  EXPECT_EQ(user_entity_count(editor), 9u);
  EXPECT_EQ(editor.tools().active_tool(), "linear_array");
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(EditorApiTests,
     NativeToolActivationCancelsOperationPreviewBeforeTakingItsSnapshot) {
  for (const quader::ViewportToolId native_tool :
       {quader::ViewportToolId::Pivot,
        quader::ViewportToolId::LinearArray}) {
    SCOPED_TRACE(static_cast<int>(native_tool));
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Native Activation Preview Source");
    const std::filesystem::path clean_path = project_test_path(
        native_tool == quader::ViewportToolId::Pivot
            ? "native-activation-pivot-preview"
            : "native-activation-array-preview");
    ASSERT_TRUE(save_scene_as_native(editor, clean_path).ok());
    ASSERT_FALSE(editor.project().dirty());
    const std::optional<quader::QuaderMeshSnapshot> mesh_before =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(mesh_before.has_value());
    const quader::QuaderHistorySnapshot history_before =
        editor.query().history();

    const quader::QuaderApiVoidResult preview = editor.commands().execute(
        editor.commands().command_id_for_modeling_action("half_cut"));
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    ASSERT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());

    const quader::QuaderApiVoidResult activated =
        editor.tools().activate(native_tool);
    ASSERT_TRUE(activated.ok()) << activated.error_message();
    EXPECT_TRUE(activated.changed());
    EXPECT_FALSE(editor.modeling().can_cancel_active_tool_or_preview());
    EXPECT_FALSE(editor.project().dirty());
    EXPECT_TRUE(editor.tools().active_settings().active);
    EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);

    const std::optional<quader::QuaderMeshSnapshot> mesh_after =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(mesh_after.has_value());
    EXPECT_EQ(mesh_after->vertex_count, mesh_before->vertex_count);
    EXPECT_EQ(mesh_after->primitive_count, mesh_before->primitive_count);
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
    std::error_code ignored;
    std::filesystem::remove(clean_path, ignored);
  }
}

TEST(EditorApiTests,
     RuntimeToolActivationCancelsOperationPreviewThroughOwnerBoundary) {
  const std::filesystem::path clean_path =
      project_test_path("runtime-activation-operation-preview");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Runtime Activation Preview Source");
  ASSERT_TRUE(save_scene_as_native(editor, clean_path).ok());
  ASSERT_FALSE(editor.project().dirty());
  const std::optional<quader::QuaderMeshSnapshot> mesh_before =
      editor.query().mesh(box.mesh);
  ASSERT_TRUE(mesh_before.has_value());
  const quader::QuaderHistorySnapshot history_before = editor.query().history();

  const quader::QuaderApiVoidResult preview = editor.commands().execute(
      editor.commands().command_id_for_modeling_action("half_cut"));
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  ASSERT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());

  const quader::QuaderApiVoidResult activated =
      editor.tools().activate(quader::ViewportToolId::Move);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  EXPECT_TRUE(activated.changed());
  EXPECT_EQ(editor.tools().active_tool(), "move");
  EXPECT_FALSE(editor.modeling().can_cancel_active_tool_or_preview());
  EXPECT_FALSE(editor.tools().active_settings().active);
  EXPECT_FALSE(editor.project().dirty());
  EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);

  const std::optional<quader::QuaderMeshSnapshot> mesh_after =
      editor.query().mesh(box.mesh);
  ASSERT_TRUE(mesh_after.has_value());
  EXPECT_EQ(mesh_after->vertex_count, mesh_before->vertex_count);
  EXPECT_EQ(mesh_after->primitive_count, mesh_before->primitive_count);

  std::error_code ignored;
  std::filesystem::remove(clean_path, ignored);
}

TEST(EditorApiTests,
     UnavailableRuntimeActivationPreservesTheExistingNativePreview) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef left =
      create_selected_box(editor, "Runtime Preflight Group Left");
  const quader::QuaderModelingObjectRef right =
      create_selected_box(editor, "Runtime Preflight Group Right");
  ASSERT_TRUE(editor.selection()
                  .select_objects({left.entity, right.entity}, right.entity)
                  .ok());
  const quader::QuaderApiResult<quader::QuaderEntityRef> group =
      editor.scene().group_selection("Runtime Preflight Group");
  ASSERT_TRUE(group.ok()) << group.error_message();

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  ASSERT_TRUE(
      editor.tools().execute_action("pivot", "pivot.align.next").ok());
  const std::optional<quader::QVec3> pivot_before =
      editor.query().selection_transform_pivot();
  ASSERT_TRUE(pivot_before.has_value());

  EXPECT_FALSE(editor.tools().can_activate(quader::ViewportToolId::Cut));
  const std::optional<quader::QuaderCommandDescriptor> cut_command =
      command_descriptor(editor, quader::quader_command::kToolsActivateCut);
  ASSERT_TRUE(cut_command.has_value());
  EXPECT_FALSE(cut_command->enabled);

  const quader::QuaderApiVoidResult rejected =
      editor.tools().activate(quader::ViewportToolId::Cut);
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  EXPECT_EQ(editor.tools().active_tool(), "pivot");
  const quader::QuaderToolSettingsTarget settings =
      editor.tools().active_settings();
  EXPECT_TRUE(settings.active);
  EXPECT_EQ(settings.tool_id, "pivot");
  const std::optional<quader::QVec3> pivot_after =
      editor.query().selection_transform_pivot();
  ASSERT_TRUE(pivot_after.has_value());
  expect_vec3_near(*pivot_after, *pivot_before);
}

TEST(EditorApiTests,
     PivotActivationCancelsSceneTransformDragBeforeTakingItsSnapshot) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Native Activation Drag Source");
  const quader::QuaderHistorySnapshot history_before = editor.query().history();

  const quader::QuaderViewportCamera camera{
      .orthographic = true,
      .origin = {0.0f, 0.0f, 10.0f},
      .x_axis = {1.0f, 0.0f, 0.0f},
      .y_axis = {0.0f, 1.0f, 0.0f},
      .z_axis = {0.0f, 0.0f, 1.0f},
      .viewport_size = {100.0f, 100.0f},
      .orthographic_size = 100.0,
      .fov_degrees = 60.0,
  };
  const quader::QuaderRay ray{
      .origin = {0.0f, 0.0f, 10.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const auto drag = editor.modeling().begin_transform_drag({
      .tool = quader::QuaderTransformTool::Move,
      .operation = quader::QuaderTransformOperation::Transform,
      .axis = quader::QuaderTransformAxis::X,
      .screen_position = {0.0f, 0.0f},
      .screen_pivot = {0.0f, 0.0f},
      .screen_tip = {10.0f, 0.0f},
      .has_world_direction = true,
      .world_direction = {1.0f, 0.0f, 0.0f},
      .ray = ray,
      .camera = camera,
      .grid_size = 1.0,
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
  });
  ASSERT_TRUE(drag.ok()) << drag.error_message();
  ASSERT_TRUE(drag.value().active);
  const auto updated = editor.modeling().update_transform_drag({
      .screen_position = {2.0f, 0.0f},
      .ray = ray,
      .camera = camera,
      .grid_size = 1.0,
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
  });
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  ASSERT_TRUE(updated.value().changed);
  ASSERT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());

  const quader::QuaderApiVoidResult activated =
      editor.tools().activate(quader::ViewportToolId::Pivot);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  EXPECT_FALSE(editor.modeling().can_cancel_active_tool_or_preview());
  const std::optional<quader::QuaderEntitySnapshot> restored =
      editor.query().entity(box.entity);
  ASSERT_TRUE(restored.has_value());
  expect_vec3_near(restored->transform.position, {});
  const std::optional<quader::QVec3> pivot =
      editor.query().selection_transform_pivot();
  ASSERT_TRUE(pivot.has_value());
  expect_vec3_near(*pivot, {});
  EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);
}

TEST(EditorApiTests,
     NativeToolActivationCancelsRuntimeModalAndPriorNativeState) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  create_selected_box(editor, "Native Activation Modal Source");
  const quader::QuaderHistorySnapshot history_before = editor.query().history();

  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.query().tool_status().modal_tool_owns_viewport);
  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::LinearArray).ok());
  EXPECT_FALSE(editor.query().tool_status().modal_tool_owns_viewport);
  EXPECT_EQ(editor.query().tool_status().active_tool,
            quader::QuaderViewportTool::Select);
  EXPECT_EQ(editor.tools().active_tool(), "linear_array");

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  EXPECT_EQ(editor.tools().active_tool(), "pivot");
  EXPECT_TRUE(editor.render().tool_preview_meshes().empty());
  ASSERT_TRUE(
      editor.tools().execute_action("pivot", "pivot.align.next").ok());

  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::LinearArray).ok());
  EXPECT_EQ(editor.tools().active_tool(), "linear_array");
  const std::optional<quader::QuaderEntitySnapshot> unchanged =
      entity_named(editor, "Native Activation Modal Source");
  ASSERT_TRUE(unchanged.has_value());
  expect_vec3_near(unchanged->transform.position, {});
  EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);
}

TEST(EditorApiTests,
     UnavailableNativeActivationPreservesTheExistingPreview) {
  {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Preserved Modeling Preview Source");
    const std::optional<quader::QuaderMeshSnapshot> before =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(editor.selection().select_edge(box, 1, 2).ok());
    ASSERT_TRUE(editor.selection().select_all().ok());
    ASSERT_TRUE(editor.commands()
                    .execute(editor.commands().command_id_for_modeling_action(
                        "bevel_edges"))
                    .ok());
    ASSERT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());
    const std::optional<quader::QuaderMeshSnapshot> preview =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(preview.has_value());
    ASSERT_GT(preview->primitive_count, before->primitive_count);

    const quader::QuaderApiVoidResult rejected =
        editor.tools().activate(quader::ViewportToolId::Pivot);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());
    const quader::QuaderToolSettingsTarget settings =
        editor.tools().active_settings();
    EXPECT_TRUE(settings.active);
    EXPECT_EQ(settings.tool_id, "edge_bevel");
    const std::optional<quader::QuaderMeshSnapshot> after_rejection =
        editor.query().mesh(box.mesh);
    ASSERT_TRUE(after_rejection.has_value());
    EXPECT_EQ(after_rejection->vertex_count, preview->vertex_count);
    EXPECT_EQ(after_rejection->primitive_count, preview->primitive_count);
  }

  {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    create_selected_box(editor, "Preserved Native Preview Source");
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Group).ok());
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
    ASSERT_TRUE(
        editor.tools().execute_action("pivot", "pivot.align.next").ok());
    const std::optional<quader::QVec3> preview_before =
        editor.query().selection_transform_pivot();
    ASSERT_TRUE(preview_before.has_value());
    ASSERT_FALSE(
        editor.tools().can_activate(quader::ViewportToolId::LinearArray));

    const quader::QuaderApiVoidResult rejected =
        editor.tools().activate(quader::ViewportToolId::LinearArray);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_EQ(editor.tools().active_tool(), "pivot");
    EXPECT_TRUE(editor.tools().active_settings().active);
    const std::optional<quader::QVec3> preview_after =
        editor.query().selection_transform_pivot();
    ASSERT_TRUE(preview_after.has_value());
    expect_vec3_near(*preview_after, *preview_before);
  }
}

TEST(EditorApiTests,
     LinearArrayPublicApiCreatesOrderedIndependentCopiesInOneHistoryItem) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef source =
      create_selected_box(editor, "Linear Array Source");
  const quader::QuaderApiResult<quader::QuaderEntityRef> child =
      editor.scene().create_entity(quader::QuaderCreateEntityArgs{
          .name = "Source Child",
          .kind = quader::QuaderEntityKind::Empty,
          .parent = source.entity,
      });
  ASSERT_TRUE(child.ok()) << child.error_message();
  ASSERT_TRUE(editor.selection().select_object(source).ok());

  const quader::QuaderApiResult<quader::QuaderLinearArrayResult> array =
      editor.modeling().create_linear_array(quader::QuaderLinearArrayArgs{
          .source = source,
          .endpoint_world = {6.0f, 0.0f, 0.0f},
          .count = 4,
      });
  ASSERT_TRUE(array.ok()) << array.error_message();
  ASSERT_TRUE(array.changed());
  ASSERT_EQ(array.value().elements.size(), 4u);
  EXPECT_EQ(array.value().elements.front().entity, source.entity);
  EXPECT_EQ(array.value().elements.front().modeling_object_id,
            source.modeling_object_id);
  EXPECT_EQ(editor.query().history().undo_label, "Linear Array");

  std::unordered_set<quader::EntityId> entities;
  std::unordered_set<quader::QuaderModelingObjectId> modeling_ids;
  std::unordered_set<quader::QuaderEditorObjectId> editor_ids;
  std::unordered_set<quader::AssetId> meshes;
  std::unordered_set<std::string> uuids;
  std::vector<quader::EntityId> expected_selection;
  std::vector<quader::QuaderEntitySnapshot> snapshots;
  for (std::size_t index = 0; index < array.value().elements.size(); ++index) {
    const quader::QuaderModelingObjectRef &element =
        array.value().elements[index];
    entities.insert(element.entity);
    modeling_ids.insert(element.modeling_object_id);
    editor_ids.insert(element.editor_object_id);
    meshes.insert(element.mesh);
    uuids.insert(element.entity_uuid);
    expected_selection.push_back(element.entity);
    const std::optional<quader::QuaderEntitySnapshot> snapshot =
        editor.query().entity(element.entity);
    ASSERT_TRUE(snapshot.has_value());
    snapshots.push_back(*snapshot);
    expect_vec3_near(snapshot->transform.position,
                     {static_cast<float>(index) * 2.0f, 0.0f, 0.0f});
    EXPECT_EQ(snapshot->parent,
              editor.query().entity(source.entity)->parent);
    if (index > 0u) {
      EXPECT_EQ(child_count(editor, snapshot->id), 0u);
    }
  }
  EXPECT_EQ(entities.size(), 4u);
  EXPECT_EQ(modeling_ids.size(), 4u);
  EXPECT_EQ(editor_ids.size(), 4u);
  EXPECT_EQ(meshes.size(), 4u);
  EXPECT_EQ(uuids.size(), 4u);
  std::sort(expected_selection.begin(), expected_selection.end());
  EXPECT_EQ(selected_entities(editor), expected_selection);
  EXPECT_TRUE(snapshots.back().active);
  EXPECT_EQ(child_count(editor, source.entity), 1u);

  const quader::QuaderApiVoidResult undo = editor.history().undo();
  ASSERT_TRUE(undo.ok()) << undo.error_message();
  ASSERT_TRUE(editor.query().entity(source.entity).has_value());
  EXPECT_EQ(editor.query().entity(source.entity)->uuid, source.entity_uuid);
  for (std::size_t index = 1u; index < snapshots.size(); ++index) {
    EXPECT_FALSE(editor.query().entity(snapshots[index].id).has_value());
  }

  const quader::QuaderApiVoidResult redo = editor.history().redo();
  ASSERT_TRUE(redo.ok()) << redo.error_message();
  for (std::size_t index = 1u; index < snapshots.size(); ++index) {
    const std::optional<quader::QuaderEntitySnapshot> restored =
        editor.query().entity(snapshots[index].id);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->uuid, snapshots[index].uuid);
    EXPECT_EQ(restored->editor_object_id, snapshots[index].editor_object_id);
    expect_vec3_near(restored->transform.position,
                     snapshots[index].transform.position);
  }
}

TEST(EditorApiTests, LinearArrayResolvesWorldEndpointThroughSourceParent) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
      editor.scene().create_entity(quader::QuaderCreateEntityArgs{
          .name = "Array Parent",
          .kind = quader::QuaderEntityKind::Empty,
          .transform = {.position = {10.0f, 0.0f, 0.0f}},
      });
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  const quader::QuaderModelingObjectRef source =
      create_selected_box(editor, "Parented Array Source");
  ASSERT_TRUE(editor.scene().reparent(source.entity, parent.value().id, false).ok());
  ASSERT_TRUE(editor.scene()
                  .set_transform(source.entity,
                                 {.position = {2.0f, 0.0f, 0.0f}})
                  .ok());

  const quader::QuaderApiResult<quader::QuaderLinearArrayResult> array =
      editor.modeling().create_linear_array(quader::QuaderLinearArrayArgs{
          .source = source,
          .endpoint_world = {18.0f, 0.0f, 0.0f},
          .count = 4,
      });
  ASSERT_TRUE(array.ok()) << array.error_message();
  ASSERT_EQ(array.value().elements.size(), 4u);
  for (std::size_t index = 0; index < array.value().elements.size(); ++index) {
    const std::optional<quader::QuaderEntitySnapshot> snapshot =
        editor.query().entity(array.value().elements[index].entity);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->parent, parent.value().id);
    expect_vec3_near(snapshot->transform.position,
                     {2.0f + static_cast<float>(index) * 2.0f, 0.0f, 0.0f});
  }
}

TEST(EditorApiTests, LinearArrayAllowsZeroSpanOverlap) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef source =
      create_selected_box(editor, "Overlap Array Source");
  const quader::QuaderApiResult<quader::QuaderLinearArrayResult> array =
      editor.modeling().create_linear_array(quader::QuaderLinearArrayArgs{
          .source = source,
          .endpoint_world = {},
          .count = 3,
      });
  ASSERT_TRUE(array.ok()) << array.error_message();
  ASSERT_EQ(array.value().elements.size(), 3u);
  for (const quader::QuaderModelingObjectRef &element :
       array.value().elements) {
    const std::optional<quader::QuaderEntitySnapshot> snapshot =
        editor.query().entity(element.entity);
    ASSERT_TRUE(snapshot.has_value());
    expect_vec3_near(snapshot->transform.position, {});
  }
}

TEST(EditorApiTests, LinearArrayRejectsAnyStaleSourceBeforeApply) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef primary =
      create_selected_box(editor, "Stale Linear Array Source");
  const quader::QuaderModelingObjectRef changed_source =
      create_selected_box(editor, "Changed Linear Array Source");
  ASSERT_TRUE(editor.selection()
                  .select_objects({primary.entity, changed_source.entity},
                                  primary.entity)
                  .ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::LinearArray).ok());
  ASSERT_TRUE(editor.scene()
                  .set_transform(changed_source.entity,
                                 {.position = {1.0f, 0.0f, 0.0f}})
                  .ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_TRUE(editor.render().tool_preview_meshes().empty());
  const quader::QuaderApiVoidResult applied =
      editor.tools().commit_active_tool_preview();
  EXPECT_FALSE(applied.ok());
  EXPECT_EQ(user_entity_count(editor), 2u);
  EXPECT_FALSE(editor.tools().active_settings().active);
}

TEST(EditorApiTests,
     RadialArrayPublicApiPreservesSourceRingAndAngleOrderAtomically) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef first =
      create_selected_box(editor, "Radial Array First Source");
  ASSERT_TRUE(editor.scene()
                  .set_transform(first.entity,
                                 {.position = {2.0f, 0.0f, 0.0f}})
                  .ok());
  const quader::QuaderModelingObjectRef second =
      create_selected_box(editor, "Radial Array Second Source");
  ASSERT_TRUE(editor.scene()
                  .set_transform(second.entity,
                                 {.position = {0.0f, 0.0f, 3.0f}})
                  .ok());

  const quader::QuaderApiResult<quader::QuaderRadialArrayResult> array =
      editor.modeling().create_radial_array(quader::QuaderRadialArrayArgs{
          .sources = {second, first},
          .center_world = {},
          .plane_normal_world = {0.0f, 1.0f, 0.0f},
          .degrees = 180.0,
          .copies = 3u,
          .repeats = 2u,
          .repeat_spacing = 1.0,
      });
  ASSERT_TRUE(array.ok()) << array.error_message();
  ASSERT_TRUE(array.changed());
  ASSERT_EQ(array.value().source_arrays.size(), 2u);
  EXPECT_EQ(array.value().source_arrays[0].source.entity, second.entity);
  EXPECT_EQ(array.value().source_arrays[1].source.entity, first.entity);
  EXPECT_EQ(editor.query().history().undo_label, "Radial Array");

  const std::array<std::array<quader::QVec3, 6>, 2> expected_positions = {{
      {{{0.0f, 0.0f, 3.0f},
        {3.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, -3.0f},
        {0.0f, 0.0f, 4.0f},
        {4.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, -4.0f}}},
      {{{2.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, -2.0f},
        {-2.0f, 0.0f, 0.0f},
        {3.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, -3.0f},
        {-3.0f, 0.0f, 0.0f}}},
  }};
  std::vector<quader::QuaderEntitySnapshot> created_snapshots;
  for (std::size_t source_index = 0;
       source_index < array.value().source_arrays.size(); ++source_index) {
    const quader::QuaderRadialArraySourceResult &source_array =
        array.value().source_arrays[source_index];
    ASSERT_EQ(source_array.elements.size(), 6u);
    EXPECT_EQ(source_array.elements.front().entity,
              source_array.source.entity);
    for (std::size_t element_index = 0;
         element_index < source_array.elements.size(); ++element_index) {
      const std::optional<quader::QuaderEntitySnapshot> snapshot =
          editor.query().entity(source_array.elements[element_index].entity);
      ASSERT_TRUE(snapshot.has_value());
      expect_vec3_near(snapshot->transform.position,
                       expected_positions[source_index][element_index]);
      if (element_index > 0u) {
        created_snapshots.push_back(*snapshot);
      }
    }
  }

  const quader::QuaderApiVoidResult undo = editor.history().undo();
  ASSERT_TRUE(undo.ok()) << undo.error_message();
  EXPECT_TRUE(editor.query().entity(first.entity).has_value());
  EXPECT_TRUE(editor.query().entity(second.entity).has_value());
  for (const quader::QuaderEntitySnapshot &snapshot : created_snapshots) {
    EXPECT_FALSE(editor.query().entity(snapshot.id).has_value());
  }

  const quader::QuaderApiVoidResult redo = editor.history().redo();
  ASSERT_TRUE(redo.ok()) << redo.error_message();
  for (const quader::QuaderEntitySnapshot &snapshot : created_snapshots) {
    const std::optional<quader::QuaderEntitySnapshot> restored =
        editor.query().entity(snapshot.id);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->uuid, snapshot.uuid);
    EXPECT_EQ(restored->editor_object_id, snapshot.editor_object_id);
    expect_vec3_near(restored->transform.position,
                     snapshot.transform.position);
  }
}

TEST(EditorApiTests, RadialArrayRejectsDuplicateSourcesAndOversizedBatches) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef source =
      create_selected_box(editor, "Radial Array Validation Source");
  const quader::QuaderHistorySnapshot history_before =
      editor.query().history();

  const quader::QuaderApiResult<quader::QuaderRadialArrayResult> duplicate =
      editor.modeling().create_radial_array(quader::QuaderRadialArrayArgs{
          .sources = {source, source},
      });
  EXPECT_FALSE(duplicate.ok());
  EXPECT_FALSE(duplicate.changed());
  EXPECT_EQ(user_entity_count(editor), 1u);

  const quader::QuaderApiResult<quader::QuaderRadialArrayResult> oversized =
      editor.modeling().create_radial_array(quader::QuaderRadialArrayArgs{
          .sources = {source},
          .copies = 128u,
          .repeats = 9u,
      });
  EXPECT_FALSE(oversized.ok());
  EXPECT_FALSE(oversized.changed());
  EXPECT_EQ(user_entity_count(editor), 1u);
  const quader::QuaderHistorySnapshot history_after =
      editor.query().history();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);
}

TEST(EditorApiTests, RadialArrayAccepts1024ElementsAndRejects1025Atomically) {
  {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderModelingObjectRef source =
        create_selected_box(editor, "Radial Array Exact Limit Source");
    const quader::QuaderApiResult<quader::QuaderRadialArrayResult> exact =
        editor.modeling().create_radial_array(quader::QuaderRadialArrayArgs{
            .sources = {source},
            .center_world = {},
            .copies = 2u,
            .repeats = 512u,
            .repeat_spacing = 0.0,
        });
    ASSERT_TRUE(exact.ok()) << exact.error_message();
    ASSERT_EQ(exact.value().source_arrays.size(), 1u);
    EXPECT_EQ(exact.value().source_arrays.front().elements.size(), 1024u);
    EXPECT_EQ(user_entity_count(editor), 1024u);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(user_entity_count(editor), 1u);
  }

  {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    std::vector<quader::QuaderModelingObjectRef> sources;
    for (int index = 0; index < 5; ++index) {
      sources.push_back(create_selected_box(
          editor, "Radial Array Oversized Source " + std::to_string(index)));
    }
    const quader::QuaderHistorySnapshot history_before =
        editor.query().history();
    const quader::QuaderApiResult<quader::QuaderRadialArrayResult> oversized =
        editor.modeling().create_radial_array(quader::QuaderRadialArrayArgs{
            .sources = sources,
            .center_world = {},
            .copies = 5u,
            .repeats = 41u,
        });
    EXPECT_FALSE(oversized.ok());
    EXPECT_FALSE(oversized.changed());
    EXPECT_EQ(user_entity_count(editor), 5u);
    const quader::QuaderHistorySnapshot history_after =
        editor.query().history();
    EXPECT_EQ(history_after.undo_label, history_before.undo_label);
    EXPECT_EQ(history_after.redo_label, history_before.redo_label);
  }
}

TEST(EditorApiTests,
     RadialArrayRejectsParentShearWithoutMutatingTheSceneOrHistory) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
      editor.scene().create_entity(quader::QuaderCreateEntityArgs{
          .name = "Radial Array Nonuniform Parent",
          .kind = quader::QuaderEntityKind::Empty,
          .transform = {.rotation_euler = {0.3f, 0.2f, 0.1f},
                        .scale = {2.0f, 1.0f, 0.5f}},
      });
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  const quader::QuaderModelingObjectRef source =
      create_selected_box(editor, "Radial Array Shear Source");
  ASSERT_TRUE(
      editor.scene().reparent(source.entity, parent.value().id, false).ok());
  ASSERT_TRUE(editor.scene()
                  .set_transform(source.entity,
                                 {.position = {2.0f, 0.5f, -1.0f},
                                  .rotation_euler = {0.2f, 0.4f, 0.1f},
                                  .scale = {1.0f, 0.8f, 1.2f}})
                  .ok());
  const std::optional<quader::QuaderEntitySnapshot> source_before =
      editor.query().entity(source.entity);
  ASSERT_TRUE(source_before.has_value());
  const quader::QuaderHistorySnapshot history_before = editor.query().history();
  const std::size_t entities_before = user_entity_count(editor);

  const quader::QuaderApiResult<quader::QuaderRadialArrayResult> array =
      editor.modeling().create_radial_array(quader::QuaderRadialArrayArgs{
          .sources = {source},
          .center_world = {},
          .plane_normal_world = {0.0f, 1.0f, 0.0f},
          .degrees = 90.0,
          .copies = 2u,
      });
  EXPECT_FALSE(array.ok());
  EXPECT_FALSE(array.changed());
  EXPECT_EQ(user_entity_count(editor), entities_before);
  const std::optional<quader::QuaderEntitySnapshot> source_after =
      editor.query().entity(source.entity);
  ASSERT_TRUE(source_after.has_value());
  EXPECT_EQ(source_after->parent, source_before->parent);
  expect_vec3_near(source_after->transform.position,
                   source_before->transform.position);
  expect_vec3_near(source_after->transform.rotation_euler,
                   source_before->transform.rotation_euler);
  expect_vec3_near(source_after->transform.scale,
                   source_before->transform.scale);
  const quader::QuaderHistorySnapshot history_after = editor.query().history();
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);
}

TEST(EditorApiTests, DefaultSingleMeshTransformPivotUsesSelectedBoundsCenter) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> left =
      editor.modeling().create_box(
          quader::QuaderBounds{.min = {-4.0f, -1.0f, -1.0f},
                               .max = {-2.0f, 1.0f, 1.0f}},
          "Default Pivot Left");
  ASSERT_TRUE(left.ok()) << left.error_message();
  ASSERT_TRUE(editor.selection().select_object(left.value()).ok());
  std::optional<quader::QVec3> pivot =
      editor.query().selection_transform_pivot();
  ASSERT_TRUE(pivot.has_value());
  expect_vec3_near(*pivot, {-3.0f, 0.0f, 0.0f});

  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
      editor.modeling().create_box(
          quader::QuaderBounds{.min = {2.0f, -1.0f, -1.0f},
                               .max = {4.0f, 1.0f, 1.0f}},
          "Default Pivot Right");
  ASSERT_TRUE(right.ok()) << right.error_message();
  ASSERT_TRUE(editor.selection().select_object(right.value()).ok());
  pivot = editor.query().selection_transform_pivot();
  ASSERT_TRUE(pivot.has_value());
  expect_vec3_near(*pivot, {3.0f, 0.0f, 0.0f});
}

TEST(EditorApiTests, PivotToolAppliesMeshOriginRebaseWithoutMovingWorldBounds) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Pivot Mesh Box");
  const std::optional<TestWorldBounds> before_bounds =
      compiled_object_world_bounds_translation_only(editor, box);
  ASSERT_TRUE(before_bounds.has_value());

  const quader::QuaderApiVoidResult activated =
      editor.tools().activate(quader::ViewportToolId::Pivot);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  EXPECT_EQ(editor.tools().active_tool(), "pivot");
  const quader::QuaderToolSettingsTarget settings =
      editor.tools().active_settings();
  ASSERT_TRUE(settings.active);
  EXPECT_TRUE(settings.preview_active);
  EXPECT_EQ(settings.tool_id, "pivot");
  EXPECT_EQ(settings.description,
            "Move the selected object's pivot and choose which features it "
            "can snap to.");
  EXPECT_TRUE(settings.can_reset);
  EXPECT_TRUE(settings.can_apply);
  EXPECT_TRUE(settings.can_cancel);
  ASSERT_NE(find_tool_setting_control(settings, "pivot.align.prev"), nullptr);
  ASSERT_NE(find_tool_setting_control(settings, "pivot.align.next"), nullptr);
  EXPECT_EQ(count_tool_setting_controls_of_kind(
                settings, quader::QuaderToolSettingControlKind::Action),
            2u);
  EXPECT_EQ(find_tool_setting_control(settings, "pivot.align.top_center"),
            nullptr);
  constexpr std::string_view kGridSnapSetting = "pivot.snap.grid";
  const quader::QuaderToolSettingControl *grid =
      find_tool_setting_control(settings, kGridSnapSetting);
  ASSERT_NE(grid, nullptr);
  EXPECT_EQ(grid->kind, quader::QuaderToolSettingControlKind::Toggle);
  EXPECT_TRUE(grid->bool_value);

  const quader::QuaderApiVoidResult disable_grid =
      editor.tools().set_setting("pivot", kGridSnapSetting, false);
  ASSERT_TRUE(disable_grid.ok()) << disable_grid.error_message();
  const quader::QuaderToolSettingsTarget settings_after_grid_toggle =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *grid_after =
      find_tool_setting_control(settings_after_grid_toggle, kGridSnapSetting);
  ASSERT_NE(grid_after, nullptr);
  EXPECT_FALSE(grid_after->bool_value);

  const quader::QuaderApiVoidResult reset =
      editor.tools().reset_active_settings();
  ASSERT_TRUE(reset.ok()) << reset.error_message();
  EXPECT_TRUE(reset.changed());
  const quader::QuaderToolSettingsTarget reset_pivot_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *reset_grid =
      find_tool_setting_control(reset_pivot_settings, kGridSnapSetting);
  ASSERT_NE(reset_grid, nullptr);
  EXPECT_TRUE(reset_grid->bool_value);

  const quader::QuaderApiVoidResult unknown_action =
      editor.tools().execute_action("pivot", "pivot.align.__missing__");
  EXPECT_FALSE(unknown_action.ok());
  EXPECT_EQ(unknown_action.error_message(), "Unknown pivot align action.");

  const quader::QuaderApiVoidResult aligned_bottom =
      editor.tools().execute_action("pivot", "pivot.align.next");
  ASSERT_TRUE(aligned_bottom.ok()) << aligned_bottom.error_message();
  const quader::QuaderApiVoidResult aligned_top =
      editor.tools().execute_action("pivot", "pivot.align.next");
  ASSERT_TRUE(aligned_top.ok()) << aligned_top.error_message();
  const quader::QuaderApiVoidResult applied =
      editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  EXPECT_TRUE(applied.changed());
  EXPECT_EQ(editor.tools().active_tool(), "move");
  EXPECT_EQ(editor.query().history().undo_label, "Set Pivot");

  const std::optional<quader::QuaderEntitySnapshot> after =
      editor.query().entity(box.entity);
  ASSERT_TRUE(after.has_value());
  expect_vec3_near(after->transform.position, {0.0f, 1.0f, 0.0f});
  const std::optional<quader::QVec3> applied_transform_pivot =
      editor.query().selection_transform_pivot();
  ASSERT_TRUE(applied_transform_pivot.has_value());
  expect_vec3_near(*applied_transform_pivot, {0.0f, 1.0f, 0.0f});
  const std::optional<TestWorldBounds> after_bounds =
      compiled_object_world_bounds_translation_only(editor, box);
  ASSERT_TRUE(after_bounds.has_value());
  expect_bounds_near(*after_bounds, *before_bounds);

  const quader::QuaderApiVoidResult undo = editor.history().undo();
  ASSERT_TRUE(undo.ok()) << undo.error_message();
  const std::optional<quader::QuaderEntitySnapshot> undone =
      editor.query().entity(box.entity);
  ASSERT_TRUE(undone.has_value());
  expect_vec3_near(undone->transform.position, {});
  const std::optional<quader::QVec3> undone_transform_pivot =
      editor.query().selection_transform_pivot();
  ASSERT_TRUE(undone_transform_pivot.has_value());
  expect_vec3_near(*undone_transform_pivot, {});
  const std::optional<TestWorldBounds> undo_bounds =
      compiled_object_world_bounds_translation_only(editor, box);
  ASSERT_TRUE(undo_bounds.has_value());
  expect_bounds_near(*undo_bounds, *before_bounds);

  const quader::QuaderApiVoidResult redo = editor.history().redo();
  ASSERT_TRUE(redo.ok()) << redo.error_message();
  const std::optional<quader::QuaderEntitySnapshot> redone =
      editor.query().entity(box.entity);
  ASSERT_TRUE(redone.has_value());
  expect_vec3_near(redone->transform.position, {0.0f, 1.0f, 0.0f});
  const std::optional<quader::QVec3> redone_transform_pivot =
      editor.query().selection_transform_pivot();
  ASSERT_TRUE(redone_transform_pivot.has_value());
  expect_vec3_near(*redone_transform_pivot, {0.0f, 1.0f, 0.0f});
  const std::optional<TestWorldBounds> redo_bounds =
      compiled_object_world_bounds_translation_only(editor, box);
  ASSERT_TRUE(redo_bounds.has_value());
  expect_bounds_near(*redo_bounds, *before_bounds);
}

TEST(EditorApiTests, PivotToolCancelDiscardsMeshPreview) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Pivot Cancel Box");
  const std::optional<TestWorldBounds> before_bounds =
      compiled_object_world_bounds_translation_only(editor, box);
  ASSERT_TRUE(before_bounds.has_value());
  const quader::QuaderHistorySnapshot history_before =
      editor.query().history();

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  ASSERT_TRUE(
      editor.tools().execute_action("pivot", "pivot.align.bottom_center").ok());
  const quader::QuaderApiVoidResult canceled =
      editor.tools().cancel_active_tool_preview();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_TRUE(canceled.changed());
  EXPECT_FALSE(editor.tools().active_settings().active);

  const std::optional<quader::QuaderEntitySnapshot> after =
      editor.query().entity(box.entity);
  ASSERT_TRUE(after.has_value());
  expect_vec3_near(after->transform.position, {});
  const std::optional<TestWorldBounds> after_bounds =
      compiled_object_world_bounds_translation_only(editor, box);
  ASSERT_TRUE(after_bounds.has_value());
  expect_bounds_near(*after_bounds, *before_bounds);
  EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);
}

TEST(EditorApiTests, PivotToolActivationReducesMultiSelectionToActiveTarget) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef first =
      create_selected_box(editor, "Pivot Multi A");
  const quader::QuaderModelingObjectRef second =
      create_selected_box(editor, "Pivot Multi B");
  ASSERT_TRUE(editor.selection()
                  .select_objects({first.entity, second.entity}, second.entity)
                  .ok());

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  EXPECT_EQ(selected_entities(editor), std::vector<quader::EntityId>{second.entity});
  const std::optional<quader::QuaderEntitySnapshot> active =
      editor.query().entity(second.entity);
  ASSERT_TRUE(active.has_value());
  EXPECT_TRUE(active->active);

  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(selected_entities(editor), std::vector<quader::EntityId>{second.entity});
}

TEST(EditorApiTests, PivotToolAllowsStandaloneMeshSelectedInGroupMode) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Group Mode Leaf Pivot Box");
  ASSERT_TRUE(editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Group)
                  .ok());

  EXPECT_TRUE(editor.tools().can_activate(quader::ViewportToolId::Pivot));
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  ASSERT_TRUE(
      editor.tools().execute_action("pivot", "pivot.align.bottom_center").ok());
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());

  const std::optional<quader::QuaderEntitySnapshot> applied =
      editor.query().entity(box.entity);
  ASSERT_TRUE(applied.has_value());
  expect_vec3_near(applied->transform.position, {0.0f, -1.0f, 0.0f});
  const std::optional<quader::QVec3> pivot =
      editor.query().selection_transform_pivot();
  ASSERT_TRUE(pivot.has_value());
  expect_vec3_near(*pivot, {0.0f, -1.0f, 0.0f});

  const auto expect_stable_group_projection = [&]() {
    const quader::QuaderSelectionSummary summary = editor.selection().summary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Group);
    EXPECT_EQ(summary.selected_object_count, 1u);
    const quader::QuaderApiVoidResult repeated_group_mode =
        editor.selection().set_mode(quader::QuaderSelectionMode::Group);
    ASSERT_TRUE(repeated_group_mode.ok())
        << repeated_group_mode.error_message();
    EXPECT_FALSE(repeated_group_mode.changed());
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{box.entity});
  };

  expect_stable_group_projection();
  ASSERT_TRUE(editor.history().undo().ok());
  expect_stable_group_projection();
  ASSERT_TRUE(editor.history().redo().ok());
  expect_stable_group_projection();
}

TEST(EditorApiTests, RepeatedPivotActivationPreservesTheLivePreview) {
  const auto run = [](bool typed_command) {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Repeated Pivot Box");
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
    ASSERT_TRUE(
        editor.tools().execute_action("pivot", "pivot.align.next").ok());

    const quader::QuaderApiVoidResult repeated =
        typed_command
            ? editor.commands().execute(
                  quader::quader_tool_activation_command_target_id("pivot"))
            : editor.tools().activate(quader::ViewportToolId::Pivot);
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.changed());
    ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());

    const std::optional<quader::QuaderEntitySnapshot> applied =
        editor.query().entity(box.entity);
    ASSERT_TRUE(applied.has_value());
    expect_vec3_near(applied->transform.position, {0.0f, -1.0f, 0.0f});
  };

  run(false);
  run(true);
}

TEST(EditorApiTests, PivotIgnoresUnrelatedSceneAndAssetRevisions) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef target =
      create_selected_box(editor, "Revision Stable Pivot Target");
  const quader::QuaderModelingObjectRef unrelated =
      create_selected_box(editor, "Unrelated Revision Source");
  ASSERT_TRUE(editor.selection().select_object(target).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  ASSERT_TRUE(
      editor.tools().execute_action("pivot", "pivot.align.next").ok());
  const quader::QuaderToolSettingsTarget settings_before =
      editor.tools().active_settings();
  ASSERT_TRUE(settings_before.active);
  ASSERT_TRUE(settings_before.preview_active);

  ASSERT_TRUE(editor.scene()
                  .rename(unrelated.entity, "Unrelated Renamed Entity")
                  .ok());
  const quader::QuaderApiResult<quader::QuaderAssetRef> unrelated_asset =
      editor.assets().add_standalone_mesh(
          triangle_mesh("Unrelated Revision Mesh"));
  ASSERT_TRUE(unrelated_asset.ok()) << unrelated_asset.error_message();

  EXPECT_EQ(editor.tools().active_tool(), "pivot");
  const quader::QuaderToolSettingsTarget settings_after =
      editor.tools().active_settings();
  EXPECT_TRUE(settings_after.active);
  EXPECT_TRUE(settings_after.preview_active);
  EXPECT_EQ(settings_after.tool_id, "pivot");
  ASSERT_TRUE(
      editor.tools().execute_action("pivot", "pivot.align.prev").ok());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(EditorApiTests, PivotRejectsStaleSelectionAndIneligibleTargets) {
  {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderModelingObjectRef first =
        create_selected_box(editor, "Stale Pivot A");
    const quader::QuaderModelingObjectRef second =
        create_selected_box(editor, "Stale Pivot B");
    ASSERT_TRUE(editor.selection().select_object(first).ok());
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
    ASSERT_TRUE(
        editor.tools().execute_action("pivot", "pivot.align.next").ok());
    ASSERT_TRUE(editor.selection().select_object(second).ok());

    EXPECT_EQ(editor.tools().active_tool(), "select");
    EXPECT_FALSE(editor.tools().active_settings().active);
    EXPECT_FALSE(editor.tools().commit_active_tool_preview().ok());
    const std::optional<quader::QuaderEntitySnapshot> unchanged =
        editor.query().entity(first.entity);
    ASSERT_TRUE(unchanged.has_value());
    expect_vec3_near(unchanged->transform.position, {});
    EXPECT_NE(editor.query().history().undo_label, "Set Pivot");
  }

  {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderApiResult<quader::QuaderEntityRef> light =
        editor.scene().create_point_light();
    ASSERT_TRUE(light.ok()) << light.error_message();
    ASSERT_TRUE(editor.selection().select_object(light.value().id).ok());
    EXPECT_FALSE(editor.tools().can_activate(quader::ViewportToolId::Pivot));
    EXPECT_FALSE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  }

  {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Locked Pivot Box");
    ASSERT_TRUE(editor.scene().set_locked(box.entity, true).ok());
    EXPECT_FALSE(editor.tools().can_activate(quader::ViewportToolId::Pivot));
    EXPECT_FALSE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  }

  {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Hidden Pivot Box");
    ASSERT_TRUE(editor.scene().set_visible(box.entity, false).ok());
    EXPECT_FALSE(editor.tools().can_activate(quader::ViewportToolId::Pivot));
    EXPECT_FALSE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  }

  {
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    create_selected_box(editor, "Component Pivot Box");
    ASSERT_TRUE(editor.modeling()
                    .set_selection_mode(quader::QuaderSelectionMode::Vertex)
                    .ok());
    EXPECT_FALSE(editor.tools().can_activate(quader::ViewportToolId::Pivot));
    EXPECT_FALSE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  }
}

TEST(EditorApiTests, PivotQuickAlignActionsUseAxisStableBoundsPositions) {
  /**
   * Describes one quick-align action and the expected rebased origin position.
   */
  struct AlignCase {
      std::string_view action;
      quader::QVec3 expected_position;
  };
  constexpr std::array kCases{
      AlignCase{"pivot.align.center", {0.0f, 0.0f, 0.0f}},
      AlignCase{"pivot.align.bottom_center", {0.0f, -1.0f, 0.0f}},
      AlignCase{"pivot.align.top_center", {0.0f, 1.0f, 0.0f}},
      AlignCase{"pivot.align.bottom_left_front", {-1.0f, -1.0f, -1.0f}},
      AlignCase{"pivot.align.bottom_left_back", {-1.0f, -1.0f, 1.0f}},
      AlignCase{"pivot.align.bottom_right_front", {1.0f, -1.0f, -1.0f}},
      AlignCase{"pivot.align.bottom_right_back", {1.0f, -1.0f, 1.0f}},
      AlignCase{"pivot.align.top_left_front", {-1.0f, 1.0f, -1.0f}},
      AlignCase{"pivot.align.top_left_back", {-1.0f, 1.0f, 1.0f}},
      AlignCase{"pivot.align.top_right_front", {1.0f, 1.0f, -1.0f}},
      AlignCase{"pivot.align.top_right_back", {1.0f, 1.0f, 1.0f}},
  };

  for (const AlignCase &align : kCases) {
    SCOPED_TRACE(align.action);
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    const quader::QuaderModelingObjectRef box =
        create_selected_box(editor, "Pivot Align Box");
    const std::optional<TestWorldBounds> before_bounds =
        compiled_object_world_bounds_translation_only(editor, box);
    ASSERT_TRUE(before_bounds.has_value());

    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
    const quader::QuaderApiVoidResult aligned =
        editor.tools().execute_action("pivot", align.action);
    ASSERT_TRUE(aligned.ok()) << aligned.error_message();
    const quader::QuaderApiVoidResult applied =
        editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(applied.ok()) << applied.error_message();

    const std::optional<quader::QuaderEntitySnapshot> entity =
        editor.query().entity(box.entity);
    ASSERT_TRUE(entity.has_value());
    expect_vec3_near(entity->transform.position, align.expected_position);
    const std::optional<TestWorldBounds> after_bounds =
        compiled_object_world_bounds_translation_only(editor, box);
    ASSERT_TRUE(after_bounds.has_value());
    expect_bounds_near(*after_bounds, *before_bounds);
  }
}

TEST(EditorApiTests, PivotQuickAlignCycleSkipsDuplicateCenterPreset) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Pivot Align Cycle Box");
  const std::optional<TestWorldBounds> before_bounds =
      compiled_object_world_bounds_translation_only(editor, box);
  ASSERT_TRUE(before_bounds.has_value());

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  const quader::QuaderApiVoidResult next =
      editor.tools().execute_action("pivot", "pivot.align.next");
  ASSERT_TRUE(next.ok()) << next.error_message();
  const quader::QuaderApiVoidResult applied =
      editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(applied.ok()) << applied.error_message();

  const std::optional<quader::QuaderEntitySnapshot> entity =
      editor.query().entity(box.entity);
  ASSERT_TRUE(entity.has_value());
  expect_vec3_near(entity->transform.position, {0.0f, -1.0f, 0.0f});
  const std::optional<TestWorldBounds> after_bounds =
      compiled_object_world_bounds_translation_only(editor, box);
  ASSERT_TRUE(after_bounds.has_value());
  expect_bounds_near(*after_bounds, *before_bounds);
}

TEST(EditorApiTests, PivotToolAppliesGroupPivotWithoutMovingChildren) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef left =
      create_selected_box(editor, "Pivot Group Left");
  const quader::QuaderModelingObjectRef right =
      create_selected_box(editor, "Pivot Group Right");
  ASSERT_TRUE(editor.scene()
                  .set_transform(left.entity,
                                 quader::QuaderTransform{
                                     .position = {-3.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.scene()
                  .set_transform(right.entity,
                                 quader::QuaderTransform{
                                     .position = {3.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects({left.entity, right.entity}, right.entity)
                  .ok());
  const quader::QuaderApiResult<quader::QuaderEntityRef> group =
      editor.scene().group_selection("Pivot Group");
  ASSERT_TRUE(group.ok()) << group.error_message();

  const std::optional<quader::QVec3> left_before =
      entity_world_origin_translation_only(editor, left.entity);
  const std::optional<quader::QVec3> right_before =
      entity_world_origin_translation_only(editor, right.entity);
  ASSERT_TRUE(left_before.has_value());
  ASSERT_TRUE(right_before.has_value());

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  ASSERT_TRUE(
      editor.tools().execute_action("pivot", "pivot.align.top_center").ok());
  const quader::QuaderApiVoidResult applied =
      editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  EXPECT_EQ(editor.query().history().undo_label, "Set Pivot");

  const std::optional<quader::QuaderEntitySnapshot> group_after =
      editor.query().entity(group.value().id);
  ASSERT_TRUE(group_after.has_value());
  expect_vec3_near(group_after->transform.position, {0.0f, 1.0f, 0.0f});
  const std::optional<quader::QVec3> group_transform_pivot =
      editor.query().selection_transform_pivot();
  ASSERT_TRUE(group_transform_pivot.has_value());
  expect_vec3_near(*group_transform_pivot, {0.0f, 1.0f, 0.0f});
  expect_vec3_near(*entity_world_origin_translation_only(editor, left.entity),
                   *left_before);
  expect_vec3_near(*entity_world_origin_translation_only(editor, right.entity),
                   *right_before);

  ASSERT_TRUE(editor.history().undo().ok());
  const std::optional<quader::QuaderEntitySnapshot> group_undone =
      editor.query().entity(group.value().id);
  ASSERT_TRUE(group_undone.has_value());
  expect_vec3_near(group_undone->transform.position, {});
  expect_vec3_near(*entity_world_origin_translation_only(editor, left.entity),
                   *left_before);
  expect_vec3_near(*entity_world_origin_translation_only(editor, right.entity),
                   *right_before);

  ASSERT_TRUE(editor.history().redo().ok());
  const std::optional<quader::QuaderEntitySnapshot> group_redone =
      editor.query().entity(group.value().id);
  ASSERT_TRUE(group_redone.has_value());
  expect_vec3_near(group_redone->transform.position, {0.0f, 1.0f, 0.0f});
  expect_vec3_near(*entity_world_origin_translation_only(editor, left.entity),
                   *left_before);
  expect_vec3_near(*entity_world_origin_translation_only(editor, right.entity),
                   *right_before);
}

TEST(EditorApiTests, PivotToolCancelDiscardsGroupPreview) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef left =
      create_selected_box(editor, "Pivot Cancel Group Left");
  const quader::QuaderModelingObjectRef right =
      create_selected_box(editor, "Pivot Cancel Group Right");
  ASSERT_TRUE(editor.selection()
                  .select_objects({left.entity, right.entity}, right.entity)
                  .ok());
  const quader::QuaderApiResult<quader::QuaderEntityRef> group =
      editor.scene().group_selection("Pivot Cancel Group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  const std::optional<quader::QVec3> left_before =
      entity_world_origin_translation_only(editor, left.entity);
  const std::optional<quader::QVec3> right_before =
      entity_world_origin_translation_only(editor, right.entity);
  ASSERT_TRUE(left_before.has_value());
  ASSERT_TRUE(right_before.has_value());
  const quader::QuaderHistorySnapshot history_before =
      editor.query().history();

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  ASSERT_TRUE(
      editor.tools().execute_action("pivot", "pivot.align.bottom_center").ok());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());

  const std::optional<quader::QuaderEntitySnapshot> group_after =
      editor.query().entity(group.value().id);
  ASSERT_TRUE(group_after.has_value());
  expect_vec3_near(group_after->transform.position, {});
  expect_vec3_near(*entity_world_origin_translation_only(editor, left.entity),
                   *left_before);
  expect_vec3_near(*entity_world_origin_translation_only(editor, right.entity),
                   *right_before);
  EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);
}

TEST(EditorApiTests, PublicHistoryUndoRedoMatchesCommandApiStatus) {
  CommandTestHarness direct_history;
  create_selected_box(direct_history.editor, "Direct History Box");
  const quader::QuaderApiVoidResult direct_undo =
      direct_history.editor.history().undo();
  ASSERT_TRUE(direct_undo.ok()) << direct_undo.error_message();
  EXPECT_TRUE(direct_undo.changed());
  EXPECT_FALSE(
      entity_named(direct_history.editor, "Direct History Box").has_value());

  CommandTestHarness command_history;
  create_selected_box(command_history.editor, "Command History Box");
  const quader::QuaderApiVoidResult command_undo =
      command_history.editor.commands().execute(
          quader::quader_command::kEditUndo);
  expect_same_api_status(command_undo, direct_undo);
  EXPECT_FALSE(
      entity_named(command_history.editor, "Command History Box").has_value());

  const quader::QuaderApiVoidResult direct_redo =
      direct_history.editor.history().redo();
  ASSERT_TRUE(direct_redo.ok()) << direct_redo.error_message();
  EXPECT_TRUE(direct_redo.changed());
  EXPECT_TRUE(
      entity_named(direct_history.editor, "Direct History Box").has_value());

  const quader::QuaderApiVoidResult command_redo =
      command_history.editor.commands().execute(
          quader::quader_command::kEditRedo);
  expect_same_api_status(command_redo, direct_redo);
  EXPECT_TRUE(
      entity_named(command_history.editor, "Command History Box").has_value());
}

TEST(EditorApiTests, QueryHistorySnapshotMatchesPublicHistoryApiSnapshot) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    create_selected_box(editor, "History Snapshot Box");
    auto expect_snapshots_match = [&] {
      const quader::QuaderHistorySnapshot history = editor.history().snapshot();
      const quader::QuaderHistorySnapshot query = editor.query().history();
      EXPECT_EQ(query.can_undo, history.can_undo);
      EXPECT_EQ(query.can_redo, history.can_redo);
      EXPECT_EQ(query.undo_label, history.undo_label);
      EXPECT_EQ(query.redo_label, history.redo_label);
    };

    expect_snapshots_match();
    ASSERT_TRUE(editor.history().undo().ok());
    expect_snapshots_match();
    ASSERT_TRUE(editor.history().redo().ok());
    expect_snapshots_match();
}

TEST(EditorApiTests,
     HistoryUndoUsesDocumentHistoryWhenOnlyIdleLegacyModalToolIsActive) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    create_selected_box(editor, "Idle Modal Undo Box");
    ASSERT_TRUE(editor.history().can_undo());

    const quader::QuaderApiVoidResult activate_poly =
        editor.tools().activate(quader::ViewportToolId::Poly);
    ASSERT_TRUE(activate_poly.ok()) << activate_poly.error_message();
    EXPECT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());

    const quader::QuaderApiVoidResult undo =
        editor.commands().execute(quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(undo.changed());
    EXPECT_FALSE(entity_named(editor, "Idle Modal Undo Box").has_value());
}

TEST(EditorApiTests,
     SceneInstancesCreateLinkedDuplicateEditScopeAndFlattenOccurrence) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const std::uint64_t initial_instance_edit_revision =
      editor.modeling().instance_edit_revision();
  EXPECT_EQ(editor.modeling().instance_edit_revision(),
            initial_instance_edit_revision);
  EXPECT_FALSE(editor.modeling().exit_instance_edit().ok());
  EXPECT_EQ(editor.modeling().instance_edit_revision(),
            initial_instance_edit_revision);
  const quader::QuaderModelingObjectRef member =
      create_selected_box(editor, "Instance Member");
  ASSERT_TRUE(editor.scene()
                  .set_transform(member.entity,
                                 quader::QuaderTransform{
                                     .position = {3.0f, 2.0f, -1.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection().select_object(member).ok());

  ASSERT_TRUE(editor.modeling().can_create_instance());
  const std::optional<quader::QuaderCommandDescriptor> create_command =
      command_descriptor(editor, quader::quader_command::kEditCreateInstance);
  ASSERT_TRUE(create_command.has_value());
  EXPECT_TRUE(create_command->enabled);
  const quader::QuaderApiVoidResult created = editor.commands().execute(
      quader::quader_command::kEditCreateInstance);
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(created.changed());

  const std::vector<quader::EntityId> first_selection =
      selected_entities(editor);
  ASSERT_EQ(first_selection.size(), 1u);
  const quader::EntityId first_occurrence = first_selection.front();
  const std::optional<quader::QuaderEntitySnapshot> first_snapshot =
      editor.query().entity(first_occurrence);
  ASSERT_TRUE(first_snapshot.has_value());
  EXPECT_EQ(first_snapshot->kind, quader::QuaderEntityKind::Instance);
  ASSERT_TRUE(first_snapshot->has_instance_occurrence);
  const quader::EntityId definition = first_snapshot->instance_definition;
  ASSERT_NE(definition, quader::kInvalidEntityId);

  const std::optional<quader::QuaderEntitySnapshot> definition_snapshot =
      editor.query().entity(definition);
  ASSERT_TRUE(definition_snapshot.has_value());
  EXPECT_TRUE(definition_snapshot->is_instance_definition);
  EXPECT_FALSE(definition_snapshot->visible);
  EXPECT_FALSE(definition_snapshot->selectable);
  const std::optional<quader::QuaderEntitySnapshot> canonical_member =
      editor.query().entity(member.entity);
  ASSERT_TRUE(canonical_member.has_value());
  EXPECT_EQ(canonical_member->parent, definition);

  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kDuplicateSelection)
                  .ok());
  const std::vector<quader::EntityId> duplicate_selection =
      selected_entities(editor);
  ASSERT_EQ(duplicate_selection.size(), 1u);
  const quader::EntityId second_occurrence = duplicate_selection.front();
  ASSERT_NE(second_occurrence, first_occurrence);
  const std::optional<quader::QuaderEntitySnapshot> second_snapshot =
      editor.query().entity(second_occurrence);
  ASSERT_TRUE(second_snapshot.has_value());
  EXPECT_TRUE(second_snapshot->has_instance_occurrence);
  EXPECT_EQ(second_snapshot->instance_definition, definition);

  ASSERT_TRUE(editor.modeling().can_enter_instance_edit(second_occurrence));
  ASSERT_TRUE(
      editor.modeling().enter_instance_edit(second_occurrence).ok());
  const std::uint64_t entered_instance_edit_revision =
      editor.modeling().instance_edit_revision();
  EXPECT_GT(entered_instance_edit_revision, initial_instance_edit_revision);
  const quader::QuaderInstanceEditState edit_state =
      editor.modeling().instance_edit_state();
  EXPECT_TRUE(edit_state.active);
  EXPECT_EQ(edit_state.occurrence.id, second_occurrence);
  EXPECT_EQ(edit_state.definition.id, definition);
  EXPECT_FALSE(edit_state.scene_name.empty());
  EXPECT_FALSE(edit_state.instance_name.empty());
  EXPECT_EQ(editor.modeling().instance_edit_revision(),
            entered_instance_edit_revision);

  // Outside entities are unselectable in definition edit scope, while the
  // canonical member remains the one editable source shared by both roots.
  ASSERT_TRUE(editor.scene().select(first_occurrence).ok());
  EXPECT_TRUE(selected_entities(editor).empty());
  ASSERT_TRUE(editor.scene().select(member.entity).ok());
  const std::optional<quader::QuaderEntitySnapshot> selected_member =
      editor.query().entity(member.entity);
  ASSERT_TRUE(selected_member.has_value());
  EXPECT_TRUE(selected_member->selected);
  ASSERT_TRUE(editor.scene()
                  .set_transform(member.entity,
                                 quader::QuaderTransform{
                                     .position = {5.0f, 2.0f, -1.0f}})
                  .ok());
  EXPECT_FLOAT_EQ(editor.query().entity(member.entity)->transform.position.x,
                  5.0f);

  ASSERT_TRUE(editor.modeling().can_exit_instance_edit());
  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kEditExitInstanceEdit)
                  .ok());
  const std::uint64_t exited_instance_edit_revision =
      editor.modeling().instance_edit_revision();
  EXPECT_GT(exited_instance_edit_revision, entered_instance_edit_revision);
  EXPECT_FALSE(editor.modeling().instance_edit_state().active);
  EXPECT_EQ(editor.modeling().instance_edit_revision(),
            exited_instance_edit_revision);
  EXPECT_EQ(selected_entities(editor),
            std::vector<quader::EntityId>{second_occurrence});

  ASSERT_TRUE(editor.modeling().can_flatten_instance());
  const quader::QuaderApiVoidResult flattened = editor.commands().execute(
      quader::quader_command::kEditFlattenInstance);
  ASSERT_TRUE(flattened.ok()) << flattened.error_message();
  EXPECT_FALSE(editor.query().entity(second_occurrence).has_value());
  ASSERT_TRUE(editor.query().entity(first_occurrence).has_value());
  EXPECT_TRUE(editor.query().entity(definition).has_value());
  const std::vector<quader::EntityId> flattened_selection =
      selected_entities(editor);
  ASSERT_EQ(flattened_selection.size(), 1u);
  const std::optional<quader::QuaderEntitySnapshot> flattened_mesh =
      editor.query().entity(flattened_selection.front());
  ASSERT_TRUE(flattened_mesh.has_value());
  EXPECT_EQ(flattened_mesh->kind, quader::QuaderEntityKind::Mesh);
  EXPECT_TRUE(flattened_mesh->has_modeling_object);

  ASSERT_TRUE(editor.history().undo().ok());
  const std::optional<quader::QuaderEntitySnapshot> restored_occurrence =
      editor.query().entity(second_occurrence);
  ASSERT_TRUE(restored_occurrence.has_value());
  EXPECT_TRUE(restored_occurrence->has_instance_occurrence);
  EXPECT_EQ(restored_occurrence->instance_definition, definition);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_FALSE(editor.query().entity(second_occurrence).has_value());
}

TEST(EditorApiTests, SceneInstanceFlattenBakesTextureMappingAndPreservesSettings) {
  for (const bool linked : {false, true}) {
    SCOPED_TRACE(linked ? "linked" : "final occurrence");
    CommandTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    quader_poly::Document topology;
    const std::array corners{
        quader_poly::add_vertex(topology, {-1.0f, -1.0f, 0.0f}),
        quader_poly::add_vertex(topology, {1.0f, -1.0f, 0.0f}),
        quader_poly::add_vertex(topology, {1.0f, 1.0f, 0.0f}),
        quader_poly::add_vertex(topology, {-1.0f, 1.0f, 0.0f}),
    };
    ASSERT_NE(quader_poly::add_face(topology, corners),
              quader_poly::kInvalidElementId);
    auto &face = topology.faces.front();
    face.uvs = {{0.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 2.0f}, {0.0f, 3.0f}};
    face.texture_transform = {
        .scale = {2.0f, -0.5f},
        .shift_uv = {0.25f, -0.125f},
        .rotation_degrees = 30.0f,
    };
    const auto created = editor.modeling().create_mesh(
        quader::QuaderPolygonDocumentInternalAccess::from_backend(topology),
        quader::QuaderCreateMeshArgs{.name = "Textured Instance"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    const auto member = created.value();
    ASSERT_TRUE(editor.selection().select_object(member).ok());
    ASSERT_TRUE(editor.modeling().create_instance().ok());
    quader::EntityId occurrence = selected_entities(editor).front();
    const quader::EntityId definition =
        editor.query().entity(occurrence)->instance_definition;
    auto transform = editor.query().entity(occurrence)->transform;
    transform.scale = {2.0f, 3.0f, 1.0f};
    ASSERT_TRUE(editor.scene().set_transform(occurrence, transform).ok());
    ASSERT_TRUE(editor.query().entity(occurrence)->texture_sampling_from_local);
    const auto occurrence_mapping =
        *editor.query().entity(occurrence)->texture_sampling_from_local;
    if (linked) {
      ASSERT_TRUE(editor.commands()
                      .execute(quader::quader_command::kDuplicateSelection).ok());
      occurrence = selected_entities(editor).front();
      ASSERT_TRUE(editor.query().entity(occurrence)->texture_sampling_from_local);
      EXPECT_EQ(editor.query().entity(occurrence)
                    ->texture_sampling_from_local->values,
                occurrence_mapping.values);
    }

    const auto member_snapshot = editor.query().entity(member.entity);
    ASSERT_TRUE(member_snapshot.has_value());
    ASSERT_EQ(member_snapshot->parent, definition);
    const auto &member_transform = member_snapshot->transform;
    const quader::QMat4 definition_from_member = quader::compose_transform({
        .position = member_transform.position,
        .rotation = quader::quat_from_euler_radians(
            member_transform.rotation_euler),
        .scale = member_transform.scale,
    });
    const quader::QMat4 effective = quader::from_glm(
        quader::to_glm(member_snapshot->texture_sampling_from_local.value_or(
            quader::QMat4{})) *
        quader::to_glm(quader::inverse(definition_from_member)) *
        quader::to_glm(occurrence_mapping) *
        quader::to_glm(definition_from_member));
    const auto &matrix = effective.values;
    const quader_poly::Transform3 sampling{
        .x_axis = {matrix[0], matrix[1], matrix[2]},
        .y_axis = {matrix[4], matrix[5], matrix[6]},
        .z_axis = {matrix[8], matrix[9], matrix[10]},
        .origin = {matrix[12], matrix[13], matrix[14]},
    };
    auto expected = editor.query().compiled_object_mesh(member);
    ASSERT_TRUE(expected.has_value());
    const auto original_vertices = expected->vertices;
    for (auto &vertex : expected->vertices) {
      vertex.uv = quader_poly::evaluate_texture_mapping(
          vertex.uv, vertex.position, vertex.uv_geometric_normal,
          vertex.uv_carrier, sampling);
    }
    const auto flattened = editor.modeling().flatten_selected_instances();
    ASSERT_TRUE(flattened.ok()) << flattened.error_message();
    ASSERT_EQ(flattened.value().root_entities.size(), 1u);
    const quader::EntityId output = flattened.value().root_entities.front().id;
    const auto output_object = editor.query().modeling_object_for_entity(output);
    ASSERT_TRUE(output_object.has_value());
    const auto check_output = [&] {
      const auto actual = editor.query().compiled_object_mesh(*output_object);
      ASSERT_TRUE(actual.has_value());
      ASSERT_EQ(actual->face_textures.size(), 1u);
      const auto &settings = actual->face_textures.front();
      EXPECT_FLOAT_EQ(settings.scale.x, 2.0f);
      EXPECT_FLOAT_EQ(settings.scale.y, -0.5f);
      EXPECT_FLOAT_EQ(settings.shift_uv.x, 0.25f);
      EXPECT_FLOAT_EQ(settings.shift_uv.y, -0.125f);
      EXPECT_FLOAT_EQ(settings.rotation_degrees, 30.0f);
      ASSERT_EQ(actual->vertices.size(), expected->vertices.size());
      for (std::size_t index = 0; index < actual->vertices.size(); ++index) {
        EXPECT_NEAR(actual->vertices[index].uv.x,
                    expected->vertices[index].uv.x, 0.00001f);
        EXPECT_NEAR(actual->vertices[index].uv.y,
                    expected->vertices[index].uv.y, 0.00001f);
      }
      EXPECT_FALSE(editor.query().entity(output)->texture_sampling_from_local);
    };
    check_output();
    if (linked) {
      const auto source = editor.query().compiled_object_mesh(member);
      ASSERT_TRUE(source.has_value());
      ASSERT_EQ(source->vertices.size(), original_vertices.size());
      for (std::size_t index = 0; index < source->vertices.size(); ++index) {
        EXPECT_FLOAT_EQ(source->vertices[index].uv.x,
                        original_vertices[index].uv.x);
        EXPECT_FLOAT_EQ(source->vertices[index].uv.y,
                        original_vertices[index].uv.y);
      }
    }
    EXPECT_EQ(editor.query().history().undo_label, "Flatten Instance");
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(editor.query().entity(definition).has_value());
    ASSERT_TRUE(editor.query().entity(occurrence)->texture_sampling_from_local);
    EXPECT_EQ(editor.query().entity(occurrence)
                  ->texture_sampling_from_local->values,
              occurrence_mapping.values);
    ASSERT_TRUE(editor.history().redo().ok());
    check_output();
  }
}

TEST(EditorApiTests, SceneInstanceLastFlattenAndCreateUndoPreservePlacement) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef member =
      create_selected_box(editor, "Last Instance Member");
  ASSERT_TRUE(editor.scene()
                  .set_transform(member.entity,
                                 quader::QuaderTransform{
                                     .position = {-4.0f, 1.0f, 6.0f}})
                  .ok());
  const std::optional<quader::QVec3> before =
      entity_world_origin_translation_only(editor, member.entity);
  ASSERT_TRUE(before.has_value());
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId occurrence = selected_entities(editor).front();
  const quader::EntityId definition =
      editor.query().entity(occurrence)->instance_definition;

  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_FALSE(editor.query().entity(occurrence).has_value());
  EXPECT_FALSE(editor.query().entity(definition).has_value());
  const std::optional<quader::QVec3> after_create_undo =
      entity_world_origin_translation_only(editor, member.entity);
  ASSERT_TRUE(after_create_undo.has_value());
  expect_vec3_near(*after_create_undo, *before);

  ASSERT_TRUE(editor.history().redo().ok());
  ASSERT_TRUE(editor.query().entity(occurrence).has_value());
  ASSERT_TRUE(editor.modeling().flatten_instance().ok());
  EXPECT_FALSE(editor.query().entity(occurrence).has_value());
  EXPECT_FALSE(editor.query().entity(definition).has_value());
  const std::optional<quader::QVec3> after_flatten =
      entity_world_origin_translation_only(editor, member.entity);
  ASSERT_TRUE(after_flatten.has_value());
  expect_vec3_near(*after_flatten, *before);

  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(editor.query().entity(occurrence).has_value());
  EXPECT_TRUE(editor.query().entity(definition).has_value());
  ASSERT_TRUE(editor.history().redo().ok());
  expect_vec3_near(
      *entity_world_origin_translation_only(editor, member.entity), *before);
}

TEST(EditorApiTests,
     SceneInstanceSingleRootNameSurvivesCreateAndFinalFlatten) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef member =
      create_selected_box(editor, "Custom Source Mesh Name");

  const quader::QuaderApiVoidResult created =
      editor.modeling().create_instance();
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(created.changed());
  const std::vector<quader::EntityId> created_selection =
      selected_entities(editor);
  ASSERT_EQ(created_selection.size(), 1u);
  const quader::EntityId occurrence = created_selection.front();
  const std::optional<quader::QuaderEntitySnapshot> created_occurrence =
      editor.query().entity(occurrence);
  ASSERT_TRUE(created_occurrence.has_value());
  EXPECT_EQ(created_occurrence->kind, quader::QuaderEntityKind::Instance);
  EXPECT_EQ(created_occurrence->name, "Custom Source Mesh Name");

  ASSERT_TRUE(editor.scene().rename(occurrence, "Renamed Instance").ok());
  const quader::QuaderApiVoidResult flattened =
      editor.modeling().flatten_instance();
  ASSERT_TRUE(flattened.ok()) << flattened.error_message();
  ASSERT_TRUE(flattened.changed());

  const std::vector<quader::EntityId> flattened_selection =
      selected_entities(editor);
  ASSERT_EQ(flattened_selection.size(), 1u);
  EXPECT_EQ(flattened_selection.front(), member.entity);
  const std::optional<quader::QuaderEntitySnapshot> flattened_mesh =
      editor.query().entity(member.entity);
  ASSERT_TRUE(flattened_mesh.has_value());
  EXPECT_EQ(flattened_mesh->kind, quader::QuaderEntityKind::Mesh);
  EXPECT_EQ(flattened_mesh->name, "Renamed Instance");
}

TEST(EditorApiTests,
     SceneInstanceLinkedFlattenTransfersOccurrenceNameToIndependentMesh) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef member =
      create_selected_box(editor, "Linked Source Mesh Name");
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId surviving_occurrence =
      selected_entities(editor).front();
  const quader::EntityId definition =
      editor.query().entity(surviving_occurrence)->instance_definition;

  ASSERT_TRUE(editor.modeling().duplicate_selected_entity_hierarchy().ok());
  const quader::EntityId flattened_occurrence =
      selected_entities(editor).front();
  ASSERT_NE(flattened_occurrence, surviving_occurrence);
  ASSERT_TRUE(
      editor.scene().rename(flattened_occurrence, "Renamed Linked Instance")
          .ok());

  const quader::QuaderApiVoidResult flattened =
      editor.modeling().flatten_instance();
  ASSERT_TRUE(flattened.ok()) << flattened.error_message();
  ASSERT_TRUE(flattened.changed());
  EXPECT_TRUE(editor.query().entity(surviving_occurrence).has_value());
  EXPECT_TRUE(editor.query().entity(definition).has_value());
  EXPECT_EQ(editor.query().entity(member.entity)->parent, definition);

  const std::vector<quader::EntityId> flattened_selection =
      selected_entities(editor);
  ASSERT_EQ(flattened_selection.size(), 1u);
  const quader::EntityId independent_mesh = flattened_selection.front();
  EXPECT_NE(independent_mesh, member.entity);
  const std::optional<quader::QuaderEntitySnapshot> flattened_mesh =
      editor.query().entity(independent_mesh);
  ASSERT_TRUE(flattened_mesh.has_value());
  EXPECT_EQ(flattened_mesh->kind, quader::QuaderEntityKind::Mesh);
  EXPECT_EQ(flattened_mesh->name, "Renamed Linked Instance");
  EXPECT_EQ(editor.query().entity(surviving_occurrence)->name,
            "Linked Source Mesh Name");
}

TEST(EditorApiTests,
     SceneInstancePublicMutationsReturnSceneQualifiedTypedResults) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef member =
      create_selected_box(editor, "Typed Instance Member");

  const auto created =
      editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(created.changed());
  EXPECT_EQ(created.value().occurrence.scene_session_id,
            member.scene_session_id);
  EXPECT_EQ(created.value().definition.scene_session_id,
            member.scene_session_id);
  ASSERT_NE(created.value().occurrence.root.id,
            quader::kInvalidEntityId);
  ASSERT_NE(created.value().definition.root.id,
            quader::kInvalidEntityId);
  EXPECT_EQ(created.value().occurrence.definition.id,
            created.value().definition.root.id);
  ASSERT_EQ(created.value().definition.members.size(), 1u);
  EXPECT_EQ(created.value().definition.members.front().id, member.entity);

  const auto flattened = editor.modeling().flatten_selected_instances();
  ASSERT_TRUE(flattened.ok()) << flattened.error_message();
  ASSERT_TRUE(flattened.changed());
  EXPECT_EQ(flattened.value().scene_session_id, member.scene_session_id);
  ASSERT_EQ(flattened.value().root_entities.size(), 1u);
  EXPECT_EQ(flattened.value().root_entities.front().id, member.entity);
}

TEST(EditorApiTests,
     SceneInstancePluralFlattenHandlesLinkedAndFinalOccurrencesAtomically) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef member =
      create_selected_box(editor, "Plural Instance Member");
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId first = selected_entities(editor).front();
  const quader::EntityId definition =
      editor.query().entity(first)->instance_definition;
  ASSERT_TRUE(editor.scene()
                  .set_transform(first,
                                 quader::QuaderTransform{
                                     .position = {-6.0f, 1.0f, 0.0f}})
                  .ok());

  ASSERT_TRUE(editor.modeling().duplicate_selected_entity_hierarchy().ok());
  const quader::EntityId second = selected_entities(editor).front();
  ASSERT_TRUE(editor.scene()
                  .set_transform(second,
                                 quader::QuaderTransform{
                                     .position = {0.0f, 1.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.modeling().duplicate_selected_entity_hierarchy().ok());
  const quader::EntityId third = selected_entities(editor).front();
  ASSERT_TRUE(editor.scene()
                  .set_transform(third,
                                 quader::QuaderTransform{
                                     .position = {6.0f, 1.0f, 0.0f}})
                  .ok());
  ASSERT_EQ(editor.query().entity(second)->instance_definition, definition);
  ASSERT_EQ(editor.query().entity(third)->instance_definition, definition);

  ASSERT_TRUE(editor.selection()
                  .select_objects({first, second}, second)
                  .ok());
  EXPECT_TRUE(editor.modeling().can_flatten_selected_instances());
  EXPECT_TRUE(editor.modeling().can_flatten_instance());
  const quader::QuaderApiResult<quader::QuaderInstanceFlattenResult> partial =
      editor.modeling().flatten_selected_instances();
  ASSERT_TRUE(partial.ok()) << partial.error_message();
  ASSERT_TRUE(partial.changed());
  EXPECT_EQ(editor.query().history().undo_label, "Flatten Instance");
  EXPECT_FALSE(editor.query().entity(first).has_value());
  EXPECT_FALSE(editor.query().entity(second).has_value());
  EXPECT_TRUE(editor.query().entity(third).has_value());
  EXPECT_TRUE(editor.query().entity(definition).has_value());
  EXPECT_EQ(editor.query().entity(member.entity)->parent, definition);

  const std::vector<quader::EntityId> partial_outputs =
      selected_entities(editor);
  ASSERT_EQ(partial_outputs.size(), 2u);
  std::vector<float> partial_world_x;
  std::unordered_set<quader::AssetId> partial_assets;
  for (const quader::EntityId output : partial_outputs) {
    const std::optional<quader::QuaderEntitySnapshot> snapshot =
        editor.query().entity(output);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->kind, quader::QuaderEntityKind::Mesh);
    EXPECT_TRUE(snapshot->has_modeling_object);
    partial_assets.insert(snapshot->mesh);
    const std::optional<quader::QVec3> world =
        entity_world_origin_translation_only(editor, output);
    ASSERT_TRUE(world.has_value());
    partial_world_x.push_back(world->x);
  }
  EXPECT_EQ(partial_assets.size(), 2u);
  std::sort(partial_world_x.begin(), partial_world_x.end());
  ASSERT_EQ(partial_world_x.size(), 2u);
  EXPECT_NEAR(partial_world_x[0], -6.0f, kTestPositionEpsilon);
  EXPECT_NEAR(partial_world_x[1], 0.0f, kTestPositionEpsilon);

  // One undo restores both linked occurrences and removes both independent
  // outputs, proving the plural mutation is one history item.
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(editor.query().entity(first).has_value());
  EXPECT_TRUE(editor.query().entity(second).has_value());
  EXPECT_TRUE(editor.query().entity(third).has_value());
  EXPECT_TRUE(editor.query().entity(definition).has_value());
  for (const quader::EntityId output : partial_outputs) {
    EXPECT_FALSE(editor.query().entity(output).has_value());
  }
  EXPECT_EQ(selected_entities(editor),
            (std::vector<quader::EntityId>{first, second}));

  // Flattening every occurrence duplicates all but the dynamically final one;
  // that final occurrence adopts the canonical member and removes the hidden
  // definition. The legacy singular name remains a compatibility wrapper.
  ASSERT_TRUE(editor.selection()
                  .select_objects({first, second, third}, third)
                  .ok());
  ASSERT_TRUE(editor.modeling().can_flatten_instance());
  const quader::QuaderApiVoidResult all = editor.modeling().flatten_instance();
  ASSERT_TRUE(all.ok()) << all.error_message();
  ASSERT_TRUE(all.changed());
  EXPECT_EQ(editor.query().history().undo_label, "Flatten Instance");
  EXPECT_FALSE(editor.query().entity(first).has_value());
  EXPECT_FALSE(editor.query().entity(second).has_value());
  EXPECT_FALSE(editor.query().entity(third).has_value());
  EXPECT_FALSE(editor.query().entity(definition).has_value());

  const std::vector<quader::EntityId> all_outputs = selected_entities(editor);
  ASSERT_EQ(all_outputs.size(), 3u);
  EXPECT_NE(std::find(all_outputs.begin(), all_outputs.end(), member.entity),
            all_outputs.end());
  std::vector<float> all_world_x;
  std::unordered_set<quader::AssetId> all_assets;
  for (const quader::EntityId output : all_outputs) {
    const std::optional<quader::QuaderEntitySnapshot> snapshot =
        editor.query().entity(output);
    ASSERT_TRUE(snapshot.has_value());
    all_assets.insert(snapshot->mesh);
    const std::optional<quader::QVec3> world =
        entity_world_origin_translation_only(editor, output);
    ASSERT_TRUE(world.has_value());
    all_world_x.push_back(world->x);
  }
  EXPECT_EQ(all_assets.size(), 3u);
  std::sort(all_world_x.begin(), all_world_x.end());
  ASSERT_EQ(all_world_x.size(), 3u);
  EXPECT_NEAR(all_world_x[0], -6.0f, kTestPositionEpsilon);
  EXPECT_NEAR(all_world_x[1], 0.0f, kTestPositionEpsilon);
  EXPECT_NEAR(all_world_x[2], 6.0f, kTestPositionEpsilon);

  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(editor.query().entity(first).has_value());
  EXPECT_TRUE(editor.query().entity(second).has_value());
  EXPECT_TRUE(editor.query().entity(third).has_value());
  EXPECT_TRUE(editor.query().entity(definition).has_value());
  EXPECT_EQ(editor.query().entity(member.entity)->parent, definition);
  EXPECT_EQ(selected_entities(editor),
            (std::vector<quader::EntityId>{first, second, third}));
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_FALSE(editor.query().entity(definition).has_value());
  EXPECT_EQ(selected_entities(editor), all_outputs);
}

TEST(EditorApiTests,
     SceneInstancePluralFlattenSupportsIndependentDefinitionsInOneUndoItem) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef first_member =
      create_selected_box(editor, "First Definition Member");
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId first_occurrence = selected_entities(editor).front();
  const quader::EntityId first_definition =
      editor.query().entity(first_occurrence)->instance_definition;

  const quader::QuaderModelingObjectRef second_member =
      create_selected_box(editor, "Second Definition Member");
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId second_occurrence = selected_entities(editor).front();
  const quader::EntityId second_definition =
      editor.query().entity(second_occurrence)->instance_definition;
  ASSERT_NE(first_definition, second_definition);

  ASSERT_TRUE(editor.selection()
                  .select_objects({first_occurrence, second_occurrence},
                                  second_occurrence)
                  .ok());
  ASSERT_TRUE(editor.modeling().can_flatten_selected_instances());
  ASSERT_TRUE(editor.modeling().flatten_selected_instances().ok());
  EXPECT_EQ(editor.query().history().undo_label, "Flatten Instance");
  EXPECT_FALSE(editor.query().entity(first_occurrence).has_value());
  EXPECT_FALSE(editor.query().entity(second_occurrence).has_value());
  EXPECT_FALSE(editor.query().entity(first_definition).has_value());
  EXPECT_FALSE(editor.query().entity(second_definition).has_value());
  EXPECT_EQ(selected_entities(editor),
            (std::vector<quader::EntityId>{first_member.entity,
                                           second_member.entity}));

  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(editor.query().entity(first_occurrence).has_value());
  EXPECT_TRUE(editor.query().entity(second_occurrence).has_value());
  EXPECT_TRUE(editor.query().entity(first_definition).has_value());
  EXPECT_TRUE(editor.query().entity(second_definition).has_value());
  EXPECT_EQ(editor.query().entity(first_member.entity)->parent,
            first_definition);
  EXPECT_EQ(editor.query().entity(second_member.entity)->parent,
            second_definition);
  EXPECT_EQ(selected_entities(editor),
            (std::vector<quader::EntityId>{first_occurrence,
                                           second_occurrence}));
}

TEST(EditorApiTests,
     SceneInstancePickingResolvesOccurrencesAndScopesComponentsToEditRoot) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef member =
      create_selected_box(editor, "Pickable Instance Member");
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId first_occurrence = selected_entities(editor).front();
  ASSERT_TRUE(editor.scene()
                  .set_transform(first_occurrence,
                                 quader::QuaderTransform{
                                     .position = {-4.0f, 0.0f, 0.0f}})
                  .ok());

  ASSERT_TRUE(editor.modeling().duplicate_selected_entity_hierarchy().ok());
  const quader::EntityId second_occurrence = selected_entities(editor).front();
  ASSERT_NE(second_occurrence, first_occurrence);
  ASSERT_TRUE(editor.scene()
                  .set_transform(second_occurrence,
                                 quader::QuaderTransform{
                                     .position = {4.0f, 0.0f, 0.0f}})
                  .ok());

  const quader::QuaderModelingObjectRef context =
      create_selected_box(editor, "Instance Edit Context Mesh");
  ASSERT_TRUE(editor.scene()
                  .set_transform(context.entity,
                                 quader::QuaderTransform{
                                     .position = {10.0f, 0.0f, 0.0f}})
                  .ok());

  const auto pick_at_x = [&](float x, bool preserve_on_miss = false) {
    quader::QuaderPickSelectRequest request;
    request.ray = {
        .origin = {x, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    request.preserve_selection_on_miss = preserve_on_miss;
    return editor.selection().pick_select(request);
  };
  const auto preview_hover_at_x = [&](float x) {
    quader::QuaderHoverRequest request;
    request.ray = {
        .origin = {x, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    return editor.selection().preview_hover(request);
  };

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  const auto first_pick = pick_at_x(-4.0f);
  ASSERT_TRUE(first_pick.ok()) << first_pick.error_message();
  EXPECT_TRUE(first_pick.value().has_selection);
  EXPECT_EQ(first_pick.value().active_kind,
            quader::QuaderActiveSelectionKind::Object);
  EXPECT_EQ(first_pick.value().selected_object_count, 1u);
  EXPECT_TRUE(editor.modeling().can_flatten_instance());
  EXPECT_EQ(selected_entities(editor),
            std::vector<quader::EntityId>{first_occurrence});
  const auto second_pick = pick_at_x(4.0f);
  ASSERT_TRUE(second_pick.ok()) << second_pick.error_message();
  EXPECT_EQ(selected_entities(editor),
            std::vector<quader::EntityId>{second_occurrence});

  // Formal occurrences expose no component targets until their definition is
  // entered, and changing away from Object mode clears the atomic root.
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Group)
                  .ok());
  EXPECT_TRUE(selected_entities(editor).empty());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  EXPECT_TRUE(selected_entities(editor).empty());
  const auto outside_component_pick = pick_at_x(4.0f);
  ASSERT_TRUE(outside_component_pick.ok())
      << outside_component_pick.error_message();
  EXPECT_EQ(outside_component_pick.value().selected_face_count, 0u);
  const auto outside_direct_component =
      editor.selection().select_face(member, 0);
  EXPECT_FALSE(outside_direct_component.ok());
  EXPECT_FALSE(outside_direct_component.changed());

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(editor.selection().select_object(second_occurrence).ok());
  ASSERT_TRUE(editor.modeling().enter_instance_edit(second_occurrence).ok());

  // Object-mode member hits use the active occurrence transform even though
  // the canonical entities live below the hidden definition root.
  const auto active_object_pick = pick_at_x(4.0f);
  ASSERT_TRUE(active_object_pick.ok()) << active_object_pick.error_message();
  ASSERT_TRUE(editor.query().entity(member.entity).has_value());
  EXPECT_TRUE(editor.query().entity(member.entity)->selected);
  EXPECT_TRUE(editor.query().entity(member.entity)->active);
  const std::optional<quader::QVec3> object_center =
      editor.query().selection_center();
  const std::optional<quader::QVec3> object_gizmo_pivot =
      editor.query().selection_transform_pivot();
  ASSERT_TRUE(object_center.has_value());
  ASSERT_TRUE(object_gizmo_pivot.has_value());
  expect_vec3_near(*object_center, {4.0f, 0.0f, 0.0f});
  expect_vec3_near(*object_gizmo_pivot, *object_center);
  const auto sibling_object_pick = pick_at_x(-4.0f, true);
  ASSERT_TRUE(sibling_object_pick.ok())
      << sibling_object_pick.error_message();
  EXPECT_TRUE(editor.query().entity(member.entity)->selected);
  EXPECT_TRUE(editor.query().entity(member.entity)->active);
  const auto context_object_pick = pick_at_x(10.0f, true);
  ASSERT_TRUE(context_object_pick.ok())
      << context_object_pick.error_message();
  EXPECT_TRUE(editor.query().entity(member.entity)->selected);
  EXPECT_TRUE(editor.query().entity(member.entity)->active);

  // Object-mode transforms remain world-space editor intent even when the
  // active occurrence rotates the canonical definition's display space.
  ASSERT_TRUE(editor.scene()
                  .set_transform(second_occurrence,
                                 quader::QuaderTransform{
                                     .position = {4.0f, 0.0f, 0.0f},
                                     .rotation_euler = {0.0f, 0.0f, 1.5707963f}})
                  .ok());
  const std::optional<quader::QVec3> rotated_center_before_move =
      editor.query().selection_center();
  ASSERT_TRUE(rotated_center_before_move.has_value());
  const quader::QuaderApiVoidResult moved_in_world_x =
      editor.modeling().move_selection({1.0f, 0.0f, 0.0f});
  ASSERT_TRUE(moved_in_world_x.ok()) << moved_in_world_x.error_message();
  ASSERT_TRUE(moved_in_world_x.changed());
  const std::optional<quader::QVec3> rotated_center_after_move =
      editor.query().selection_center();
  ASSERT_TRUE(rotated_center_after_move.has_value());
  expect_vec3_near(*rotated_center_after_move,
                   *rotated_center_before_move +
                       quader::QVec3{1.0f, 0.0f, 0.0f});
  ASSERT_TRUE(editor.history().undo().ok());
  const std::optional<quader::QVec3> rotated_center_after_undo =
      editor.query().selection_center();
  ASSERT_TRUE(rotated_center_after_undo.has_value());
  expect_vec3_near(*rotated_center_after_undo, *rotated_center_before_move);

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face)
                  .ok());

  const auto active_pick = pick_at_x(4.0f);
  ASSERT_TRUE(active_pick.ok()) << active_pick.error_message();
  EXPECT_EQ(active_pick.value().active_object_id, member.modeling_object_id);
  EXPECT_EQ(active_pick.value().selected_face_count, 1u);
  const std::optional<quader::QVec3> face_center =
      editor.query().selection_center();
  const std::optional<quader::QVec3> face_gizmo_pivot =
      editor.query().selection_transform_pivot();
  ASSERT_TRUE(face_center.has_value());
  ASSERT_TRUE(face_gizmo_pivot.has_value());
  expect_vec3_near(*face_center, {4.0f, 0.0f, 1.0f});
  expect_vec3_near(*face_gizmo_pivot, *face_center);
  const quader::QuaderHoverSummary active_hover = preview_hover_at_x(4.0f);
  EXPECT_EQ(active_hover.target.object_id, member.modeling_object_id);
  EXPECT_EQ(active_hover.target.kind,
            quader::QuaderActiveSelectionKind::Face);

  // Sibling occurrences and ordinary context geometry remain visible render
  // context, but they cannot replace the active definition selection.
  const auto sibling_pick = pick_at_x(-4.0f, true);
  ASSERT_TRUE(sibling_pick.ok()) << sibling_pick.error_message();
  EXPECT_EQ(sibling_pick.value().active_object_id, member.modeling_object_id);
  EXPECT_EQ(sibling_pick.value().selected_face_count, 1u);
  const auto context_pick = pick_at_x(10.0f, true);
  ASSERT_TRUE(context_pick.ok()) << context_pick.error_message();
  EXPECT_EQ(context_pick.value().active_object_id, member.modeling_object_id);
  EXPECT_EQ(context_pick.value().selected_face_count, 1u);
  EXPECT_LT(preview_hover_at_x(-4.0f).target.object_id, 0);
  EXPECT_LT(preview_hover_at_x(10.0f).target.object_id, 0);

  // Bulk component selection and marquee candidates use the same active
  // definition universe instead of leaking to the context mesh.
  ASSERT_TRUE(editor.selection().select_all().ok());
  EXPECT_EQ(editor.selection().summary().selected_face_count, 6u);
  ASSERT_TRUE(editor.selection().select_inverse().ok());
  EXPECT_EQ(editor.selection().summary().selected_face_count, 0u);

  ASSERT_TRUE(editor.selection().hover_face(member, 1).ok());
  quader::QuaderHoverRequest sticky_miss;
  sticky_miss.ray = {
      .origin = {100.0f, 100.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  ASSERT_TRUE(editor.selection().hover(sticky_miss).ok());
  const quader::QuaderApiVoidResult sticky_select_all =
      editor.selection().select_all();
  ASSERT_TRUE(sticky_select_all.ok()) << sticky_select_all.error_message();
  EXPECT_TRUE(sticky_select_all.changed());
  EXPECT_EQ(editor.selection().summary().selected_face_count, 6u);

  quader::QuaderBoxSelectRequest active_box;
  active_box.camera = api_orthographic_camera();
  active_box.start = {52.0f, 48.0f};
  active_box.end = {56.0f, 52.0f};
  active_box.edit = quader::QuaderSelectionEdit::Replace;
  active_box.rule = quader::QuaderBoxSelectionRule::Contain;
  const auto active_box_pick = editor.selection().box_select(active_box);
  ASSERT_TRUE(active_box_pick.ok()) << active_box_pick.error_message();
  EXPECT_EQ(active_box_pick.value().selected_face_count, 6u);

  quader::QuaderBoxSelectRequest sibling_box = active_box;
  sibling_box.start = {44.0f, 48.0f};
  sibling_box.end = {48.0f, 52.0f};
  const auto sibling_box_pick = editor.selection().box_select(sibling_box);
  ASSERT_TRUE(sibling_box_pick.ok()) << sibling_box_pick.error_message();
  EXPECT_EQ(sibling_box_pick.value().selected_face_count, 0u);

  ASSERT_TRUE(editor.modeling().exit_instance_edit().ok());
  EXPECT_EQ(editor.selection().summary().mode,
            quader::QuaderSelectionMode::Object);
  EXPECT_EQ(selected_entities(editor),
            std::vector<quader::EntityId>{second_occurrence});
  const auto first_after_exit = pick_at_x(-4.0f);
  ASSERT_TRUE(first_after_exit.ok()) << first_after_exit.error_message();
  EXPECT_EQ(selected_entities(editor),
            std::vector<quader::EntityId>{first_occurrence});
}

TEST(EditorApiTests,
     SceneInstanceDragDuplicateIsLinkedAndUsesOneCancelableHistoryItem) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  create_selected_box(editor, "Drag-Duplicated Instance Member");
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId source = selected_entities(editor).front();
  const quader::EntityId definition =
      editor.query().entity(source)->instance_definition;

  const quader::QuaderViewportCamera camera{
      .orthographic = true,
      .origin = {0.0f, 0.0f, 10.0f},
      .x_axis = {1.0f, 0.0f, 0.0f},
      .y_axis = {0.0f, 1.0f, 0.0f},
      .z_axis = {0.0f, 0.0f, 1.0f},
      .viewport_size = {100.0f, 100.0f},
      .orthographic_size = 100.0,
      .fov_degrees = 60.0,
  };
  const quader::QuaderRay ray{
      .origin = {0.0f, 0.0f, 10.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const auto begin_duplicate_drag = [&]() {
    return editor.modeling().begin_transform_drag({
        .tool = quader::QuaderTransformTool::Move,
        .operation = quader::QuaderTransformOperation::Transform,
        .axis = quader::QuaderTransformAxis::X,
        .screen_position = {0.0f, 0.0f},
        .screen_pivot = {0.0f, 0.0f},
        .screen_tip = {10.0f, 0.0f},
        .has_world_direction = true,
        .world_direction = {1.0f, 0.0f, 0.0f},
        .ray = ray,
        .camera = camera,
        .grid_size = 1.0,
        .vertex_snap_radius = 0.0,
        .grid_snap_enabled = false,
        .duplicate_selected = true,
    });
  };
  const auto update_duplicate_drag = [&]() {
    return editor.modeling().update_transform_drag({
        .screen_position = {2.0f, 0.0f},
        .ray = ray,
        .camera = camera,
        .grid_size = 1.0,
        .vertex_snap_radius = 0.0,
        .grid_snap_enabled = false,
    });
  };
  const auto occurrence_count = [&]() {
    const std::vector<quader::QuaderEntitySnapshot> entities =
        editor.query().entities();
    return static_cast<std::size_t>(std::count_if(
        entities.begin(), entities.end(),
        [definition](const quader::QuaderEntitySnapshot &entity) {
          return entity.has_instance_occurrence &&
                 entity.instance_definition == definition;
        }));
  };

  const auto begin = begin_duplicate_drag();
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  ASSERT_TRUE(begin.value().active);
  EXPECT_FALSE(begin.value().preview_driven);
  EXPECT_TRUE(begin.value().scene_transform);
  ASSERT_EQ(occurrence_count(), 2u);
  const quader::EntityId duplicate = selected_entities(editor).front();
  ASSERT_NE(duplicate, source);
  ASSERT_EQ(editor.query().entity(duplicate)->instance_definition, definition);

  const auto update = update_duplicate_drag();
  ASSERT_TRUE(update.ok()) << update.error_message();
  ASSERT_TRUE(update.value().changed);
  const quader::QuaderTransform duplicate_transform =
      editor.query().entity(duplicate)->transform;
  EXPECT_GT(duplicate_transform.position.x, 0.001f);
  EXPECT_NEAR(editor.query().entity(source)->transform.position.x, 0.0f,
              kTestPositionEpsilon);

  const auto committed = editor.modeling().commit_transform_drag();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(committed.value().changed);
  EXPECT_EQ(editor.query().history().undo_label, "Duplicate Instance");

  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_FALSE(editor.query().entity(duplicate).has_value());
  EXPECT_EQ(occurrence_count(), 1u);
  EXPECT_EQ(selected_entities(editor), std::vector<quader::EntityId>{source});
  ASSERT_TRUE(editor.history().redo().ok());
  ASSERT_TRUE(editor.query().entity(duplicate).has_value());
  EXPECT_EQ(editor.query().entity(duplicate)->instance_definition, definition);
  expect_vec3_near(editor.query().entity(duplicate)->transform.position,
                   duplicate_transform.position);
  EXPECT_EQ(selected_entities(editor),
            std::vector<quader::EntityId>{duplicate});

  ASSERT_TRUE(editor.history().undo().ok());
  const quader::QuaderHistorySnapshot before_canceled_drag =
      editor.query().history();
  const auto cancel_begin = begin_duplicate_drag();
  ASSERT_TRUE(cancel_begin.ok()) << cancel_begin.error_message();
  const quader::EntityId canceled_duplicate = selected_entities(editor).front();
  ASSERT_NE(canceled_duplicate, source);
  ASSERT_EQ(occurrence_count(), 2u);
  const quader::QuaderApiVoidResult canceled =
      editor.modeling().cancel_transform_drag();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  ASSERT_TRUE(canceled.changed());
  EXPECT_FALSE(editor.query().entity(canceled_duplicate).has_value());
  EXPECT_EQ(occurrence_count(), 1u);
  EXPECT_EQ(selected_entities(editor), std::vector<quader::EntityId>{source});
  const quader::QuaderHistorySnapshot after_canceled_drag =
      editor.query().history();
  EXPECT_EQ(after_canceled_drag.undo_label, before_canceled_drag.undo_label);
  EXPECT_EQ(after_canceled_drag.redo_label, before_canceled_drag.redo_label);
}

TEST(EditorApiTests, DeletingFinalInstanceOccurrenceCollectsDefinitionAndUndoes) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef member =
      create_selected_box(editor, "Deleted Instance Member");
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId occurrence = selected_entities(editor).front();
  const quader::EntityId definition =
      editor.query().entity(occurrence)->instance_definition;

  const quader::QuaderApiVoidResult deleted =
      editor.commands().execute(quader::quader_command::kDeleteSelection);
  ASSERT_TRUE(deleted.ok()) << deleted.error_message();
  EXPECT_TRUE(deleted.changed());
  EXPECT_FALSE(editor.query().entity(occurrence).has_value());
  EXPECT_FALSE(editor.query().entity(definition).has_value());
  EXPECT_FALSE(editor.query().entity(member.entity).has_value());

  ASSERT_TRUE(editor.history().undo().ok());
  const std::optional<quader::QuaderEntitySnapshot> restored =
      editor.query().entity(occurrence);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->instance_definition, definition);
  EXPECT_TRUE(editor.query().entity(definition).has_value());
  EXPECT_TRUE(editor.query().entity(member.entity).has_value());

  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_FALSE(editor.query().entity(occurrence).has_value());
  EXPECT_FALSE(editor.query().entity(definition).has_value());
}

TEST(EditorApiTests,
     SceneInstanceCreationComposesMultipleMeshAndEmptyHierarchyRoots) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;

  const quader::QuaderModelingObjectRef loose_mesh =
      create_selected_box(editor, "Loose Instance Member");
  ASSERT_TRUE(editor.scene()
                  .set_transform(loose_mesh.entity,
                                 quader::QuaderTransform{
                                     .position = {-4.0f, 1.0f, 0.0f}})
                  .ok());
  const quader::QuaderApiResult<quader::QuaderEntityRef> group =
      editor.scene().create_entity({
          .name = "Instance Empty Group",
          .kind = quader::QuaderEntityKind::Empty,
      });
  ASSERT_TRUE(group.ok()) << group.error_message();
  const quader::QuaderApiResult<quader::QuaderEntityRef> nested_empty =
      editor.scene().create_entity({
          .name = "Nested Instance Empty",
          .kind = quader::QuaderEntityKind::Empty,
          .parent = group.value().id,
      });
  ASSERT_TRUE(nested_empty.ok()) << nested_empty.error_message();
  ASSERT_TRUE(editor.scene()
                  .set_transform(group.value().id,
                                 quader::QuaderTransform{
                                     .position = {3.0f, 2.0f, 0.0f}})
                  .ok());
  const quader::QuaderModelingObjectRef nested_mesh =
      create_selected_box(editor, "Nested Instance Member");
  ASSERT_TRUE(editor.scene()
                  .set_transform(nested_mesh.entity,
                                 quader::QuaderTransform{
                                     .position = {5.0f, 3.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.scene()
                  .reparent(nested_mesh.entity, nested_empty.value().id, true)
                  .ok());

  const std::optional<quader::QVec3> loose_world_before =
      entity_world_origin_translation_only(editor, loose_mesh.entity);
  const std::optional<quader::QVec3> nested_world_before =
      entity_world_origin_translation_only(editor, nested_mesh.entity);
  ASSERT_TRUE(loose_world_before.has_value());
  ASSERT_TRUE(nested_world_before.has_value());
  ASSERT_TRUE(editor.selection()
                  .select_objects({loose_mesh.entity, group.value().id},
                                  group.value().id)
                  .ok());

  const auto created = editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(created.changed());
  ASSERT_EQ(created.value().definition.members.size(), 2u);
  const quader::EntityId occurrence = created.value().occurrence.root.id;
  const quader::EntityId definition = created.value().definition.root.id;
  EXPECT_EQ(editor.query().entity(loose_mesh.entity)->parent, definition);
  EXPECT_EQ(editor.query().entity(group.value().id)->parent, definition);
  EXPECT_EQ(editor.query().entity(nested_empty.value().id)->parent,
            group.value().id);
  EXPECT_EQ(editor.query().entity(nested_mesh.entity)->parent,
            nested_empty.value().id);

  const auto flattened = editor.modeling().flatten_selected_instances();
  ASSERT_TRUE(flattened.ok()) << flattened.error_message();
  ASSERT_TRUE(flattened.changed());
  EXPECT_FALSE(editor.query().entity(occurrence).has_value());
  EXPECT_FALSE(editor.query().entity(definition).has_value());
  const std::optional<quader::QVec3> loose_world_after =
      entity_world_origin_translation_only(editor, loose_mesh.entity);
  const std::optional<quader::QVec3> nested_world_after =
      entity_world_origin_translation_only(editor, nested_mesh.entity);
  ASSERT_TRUE(loose_world_after.has_value());
  ASSERT_TRUE(nested_world_after.has_value());
  expect_vec3_near(*loose_world_after, *loose_world_before);
  expect_vec3_near(*nested_world_after, *nested_world_before);
}

TEST(EditorApiTests,
     SceneInstanceEditAddsDuplicatesDeletesMembersAndKeepsSiblingLive) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef original =
      create_selected_box(editor, "Original Definition Member");
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId first_occurrence = selected_entities(editor).front();
  const quader::EntityId definition =
      editor.query().entity(first_occurrence)->instance_definition;
  ASSERT_TRUE(editor.modeling().duplicate_selected_entity_hierarchy().ok());
  const quader::EntityId sibling_occurrence = selected_entities(editor).front();
  ASSERT_TRUE(editor.scene()
                  .set_transform(sibling_occurrence,
                                 quader::QuaderTransform{
                                     .position = {10.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.modeling().enter_instance_edit(first_occurrence).ok());

  const auto added_result = editor.modeling().create_box(
      quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                           .max = {1.0f, 1.0f, 1.0f}},
      "Added Definition Member");
  ASSERT_TRUE(added_result.ok()) << added_result.error_message();
  const quader::QuaderModelingObjectRef added = added_result.value();
  ASSERT_EQ(editor.query().entity(added.entity)->parent, definition);
  ASSERT_TRUE(editor.scene()
                  .set_transform(added.entity,
                                 quader::QuaderTransform{
                                     .position = {2.0f, 0.0f, 0.0f}})
                  .ok());

  ASSERT_TRUE(editor.modeling().exit_instance_edit().ok());
  ASSERT_TRUE(editor.selection().select_object(sibling_occurrence).ok());
  const auto flattened_sibling =
      editor.modeling().flatten_selected_instances();
  ASSERT_TRUE(flattened_sibling.ok())
      << flattened_sibling.error_message();
  ASSERT_TRUE(flattened_sibling.changed());
  EXPECT_TRUE(editor.query().entity(first_occurrence).has_value());
  EXPECT_FALSE(editor.query().entity(sibling_occurrence).has_value());
  EXPECT_TRUE(editor.query().entity(definition).has_value());
  ASSERT_EQ(flattened_sibling.value().root_entities.size(), 2u);
  quader::EntityId sibling_added = quader::kInvalidEntityId;
  for (const quader::QuaderEntityRef &root :
       flattened_sibling.value().root_entities) {
    const std::optional<quader::QVec3> root_world =
        entity_world_origin_translation_only(editor, root.id);
    ASSERT_TRUE(root_world.has_value());
    if (std::abs(root_world->x - 12.0f) <= kTestPositionEpsilon) {
      sibling_added = root.id;
    }
  }
  ASSERT_NE(sibling_added, quader::kInvalidEntityId)
      << "the sibling flatten must include the member added during edit mode";
  const std::optional<quader::QVec3> sibling_added_world =
      entity_world_origin_translation_only(editor, sibling_added);
  ASSERT_TRUE(sibling_added_world.has_value());
  EXPECT_NEAR(sibling_added_world->x, 12.0f, kTestPositionEpsilon);

  ASSERT_TRUE(editor.modeling().enter_instance_edit(first_occurrence).ok());

  ASSERT_TRUE(editor.selection().select_object(added).ok());
  ASSERT_TRUE(editor.modeling().duplicate_selected_entity_hierarchy().ok());
  const std::vector<quader::EntityId> duplicate_selection =
      selected_entities(editor);
  ASSERT_EQ(duplicate_selection.size(), 1u);
  const quader::EntityId duplicate = duplicate_selection.front();
  ASSERT_NE(duplicate, added.entity);
  EXPECT_EQ(editor.query().entity(duplicate)->parent, definition);

  const quader::QuaderApiVoidResult deleted_duplicate =
      editor.commands().execute(quader::quader_command::kDeleteSelection);
  ASSERT_TRUE(deleted_duplicate.ok()) << deleted_duplicate.error_message();
  ASSERT_TRUE(deleted_duplicate.changed());
  EXPECT_FALSE(editor.query().entity(duplicate).has_value());

  ASSERT_TRUE(editor.selection().select_object(added).ok());
  const quader::QuaderApiVoidResult deleted_added =
      editor.commands().execute(quader::quader_command::kDeleteSelection);
  ASSERT_TRUE(deleted_added.ok()) << deleted_added.error_message();
  ASSERT_TRUE(deleted_added.changed());
  EXPECT_FALSE(editor.query().entity(added.entity).has_value());

  ASSERT_TRUE(editor.selection().select_object(original).ok());
  const quader::QuaderApiVoidResult rejected_final_delete =
      editor.commands().execute(quader::quader_command::kDeleteSelection);
  EXPECT_FALSE(rejected_final_delete.ok());
  EXPECT_FALSE(rejected_final_delete.changed());
  EXPECT_NE(rejected_final_delete.error_message().find("retain at least one"),
            std::string::npos);
  EXPECT_TRUE(editor.query().entity(original.entity).has_value());
  EXPECT_EQ(editor.query().entity(original.entity)->parent, definition);
}

TEST(EditorApiTests,
     SceneInstanceCommandsGuardNativeToolsAndExitCancelsPendingTool) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef member =
      create_selected_box(editor, "Native Tool Guard Member");

  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::LinearArray).ok());
  EXPECT_EQ(editor.tools().active_tool(), "linear_array");
  EXPECT_FALSE(editor.modeling().can_create_instance());
  const auto rejected_create =
      editor.modeling().create_instance_from_selection();
  EXPECT_FALSE(rejected_create.ok());
  EXPECT_FALSE(rejected_create.changed());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());

  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId occurrence = selected_entities(editor).front();
  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::LinearArray).ok());
  EXPECT_EQ(editor.tools().active_tool(), "linear_array");
  EXPECT_FALSE(editor.modeling().can_flatten_selected_instances());
  EXPECT_FALSE(editor.modeling().can_enter_instance_edit(occurrence));
  const auto rejected_flatten =
      editor.modeling().flatten_selected_instances();
  EXPECT_FALSE(rejected_flatten.ok());
  EXPECT_FALSE(rejected_flatten.changed());
  const quader::QuaderApiVoidResult rejected_enter =
      editor.modeling().enter_instance_edit(occurrence);
  EXPECT_FALSE(rejected_enter.ok());
  EXPECT_FALSE(rejected_enter.changed());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());

  const quader::QuaderApiVoidResult entered =
      editor.modeling().enter_instance_edit(occurrence);
  ASSERT_TRUE(entered.ok()) << entered.error_message();
  EXPECT_EQ(entered.mutation_kind, quader::QuaderMutationKind::Runtime);
  ASSERT_TRUE(editor.selection().select_object(member).ok());
  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::LinearArray).ok());
  EXPECT_EQ(editor.tools().active_tool(), "linear_array");
  const quader::QuaderApiVoidResult exited =
      editor.modeling().exit_instance_edit();
  ASSERT_TRUE(exited.ok()) << exited.error_message();
  ASSERT_TRUE(exited.changed());
  EXPECT_EQ(exited.mutation_kind, quader::QuaderMutationKind::Runtime);
  EXPECT_NE(editor.tools().active_tool(), "linear_array");
  EXPECT_FALSE(editor.modeling().instance_edit_state().active);
}

TEST(EditorApiTests, InstanceEditRejectsLockedAndHiddenOccurrences) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  (void)create_selected_box(editor, "Guarded Edit Member");
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId occurrence = selected_entities(editor).front();

  ASSERT_TRUE(editor.scene().set_locked(occurrence, true).ok());
  EXPECT_FALSE(editor.modeling().can_enter_instance_edit(occurrence));
  EXPECT_FALSE(editor.modeling().enter_instance_edit(occurrence).ok());
  ASSERT_TRUE(editor.scene().set_locked(occurrence, false).ok());

  ASSERT_TRUE(editor.scene().set_visible(occurrence, false).ok());
  EXPECT_FALSE(editor.modeling().can_enter_instance_edit(occurrence));
  EXPECT_FALSE(editor.modeling().enter_instance_edit(occurrence).ok());
  ASSERT_TRUE(editor.scene().set_visible(occurrence, true).ok());

  EXPECT_TRUE(editor.modeling().can_enter_instance_edit(occurrence));
}

TEST(EditorApiTests,
     InvalidatedInstanceEditContextCancelsTransientStateAndRestoresObjectMode) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef member =
      create_selected_box(editor, "Invalidated Edit Member");
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId occurrence = selected_entities(editor).front();
  ASSERT_TRUE(editor.modeling().enter_instance_edit(occurrence).ok());
  const std::uint64_t entered_instance_edit_revision =
      editor.modeling().instance_edit_revision();
  ASSERT_TRUE(editor.selection().select_object(member).ok());
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  EXPECT_EQ(editor.selection().summary().mode,
            quader::QuaderSelectionMode::Face);

  const quader::QuaderApiVoidResult destroyed =
      editor.scene().destroy(occurrence);
  ASSERT_TRUE(destroyed.ok()) << destroyed.error_message();
  ASSERT_TRUE(destroyed.changed());
  const std::uint64_t invalidated_instance_edit_revision =
      editor.modeling().instance_edit_revision();
  EXPECT_GT(invalidated_instance_edit_revision,
            entered_instance_edit_revision);
  const quader::QuaderInstanceEditState state =
      editor.modeling().instance_edit_state();
  EXPECT_FALSE(state.active);
  EXPECT_EQ(editor.selection().summary().mode,
            quader::QuaderSelectionMode::Object);
  EXPECT_TRUE(selected_entities(editor).empty());
  EXPECT_FALSE(editor.modeling().can_exit_instance_edit());
  EXPECT_EQ(editor.modeling().instance_edit_revision(),
            invalidated_instance_edit_revision);
}

TEST(EditorApiTests,
     SceneInstanceEditTransitionIsRuntimeOnlyAndTransient) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef member =
      create_selected_box(editor, "Transient Overlay Member");
  const quader::QuaderApiResult<quader::QuaderAssetRef> material =
      editor.assets().add_material(
          quader::QuaderMaterialArgs{.name = "Transient Overlay Material"});
  ASSERT_TRUE(material.ok()) << material.error_message();
  ASSERT_TRUE(editor.assets()
                  .assign_material_to_entity(member.entity,
                                             material.value().id)
                  .ok());
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const quader::EntityId occurrence = selected_entities(editor).front();

  const bool dirty_before = editor.project().dirty();
  const quader::QuaderHistorySnapshot history_before = editor.query().history();
  const auto material_before = editor.query().entity(member.entity);
  ASSERT_TRUE(material_before.has_value());
  ASSERT_TRUE(material_before->has_material);

  const quader::QuaderApiVoidResult entered =
      editor.modeling().enter_instance_edit(occurrence);
  ASSERT_TRUE(entered.ok()) << entered.error_message();
  ASSERT_TRUE(entered.changed());
  EXPECT_EQ(entered.mutation_kind, quader::QuaderMutationKind::Runtime);
  EXPECT_EQ(editor.project().dirty(), dirty_before);
  EXPECT_EQ(editor.query().history().can_undo, history_before.can_undo);
  EXPECT_EQ(editor.query().history().can_redo, history_before.can_redo);
  EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);
  EXPECT_EQ(editor.query().history().redo_label, history_before.redo_label);
  const quader::QuaderApiVoidResult exited =
      editor.modeling().exit_instance_edit();
  ASSERT_TRUE(exited.ok()) << exited.error_message();
  ASSERT_TRUE(exited.changed());
  EXPECT_EQ(exited.mutation_kind, quader::QuaderMutationKind::Runtime);
  EXPECT_EQ(editor.project().dirty(), dirty_before);
  EXPECT_EQ(editor.query().history().can_undo, history_before.can_undo);
  EXPECT_EQ(editor.query().history().can_redo, history_before.can_redo);
  EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);
  EXPECT_EQ(editor.query().history().redo_label, history_before.redo_label);
  const auto material_after = editor.query().entity(member.entity);
  ASSERT_TRUE(material_after.has_value());
  EXPECT_TRUE(material_after->has_material);
  EXPECT_EQ(material_after->material, material_before->material);
}

TEST(EditorApiTests,
     RequestedVisibilityViewShadingLayoutAndGridBindingsDrivePublicCommands) {
  const std::filesystem::path path =
      keybinding_test_path("requested-viewport-defaults");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());

  const quader::QuaderModelingObjectRef first =
      create_selected_box(editor, "Hidden Shortcut A");
  const quader::QuaderModelingObjectRef second =
      create_selected_box(editor, "Hidden Shortcut B");
  const quader::QuaderModelingObjectRef third =
      create_selected_box(editor, "Local Visibility Baseline");
  ASSERT_TRUE(
      editor.selection().select_objects({first.entity, second.entity}).ok());
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());

  quader::ViewportToolState state;
  state.selection_mode = quader::ViewportSelectionMode::Object;
  const auto dispatch_and_execute =
      [&](std::string_view shortcut,
          quader::QuaderCommandId expected_command) {
        const quader::QuaderViewportKeybindingDispatch dispatch =
            editor.commands().dispatch_viewport_input_binding(
                single_input_chord(shortcut), state, true, false);
        EXPECT_EQ(dispatch.kind,
                  quader::QuaderViewportKeybindingDispatch::Kind::Command)
            << shortcut;
        EXPECT_EQ(dispatch.command_id, expected_command.string()) << shortcut;
        if (dispatch.kind !=
            quader::QuaderViewportKeybindingDispatch::Kind::Command) {
          return;
        }
         EXPECT_TRUE(editor.commands().execute(dispatch.command_id).ok())
             << shortcut;
       };

  const auto dispatch_and_apply_view =
      [&](std::string_view shortcut, quader::ViewportView expected_view,
          quader::QuaderCommandId expected_command) {
        const quader::QuaderViewportKeybindingDispatch dispatch =
            editor.commands().dispatch_viewport_input_binding(
                single_input_chord(shortcut), state, true, false);
        ASSERT_EQ(
            dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut)
            << shortcut;
        EXPECT_EQ(dispatch.shortcut.kind,
                  quader::ViewportShortcutKind::SetView)
            << shortcut;
        EXPECT_EQ(dispatch.shortcut.view, expected_view) << shortcut;
        EXPECT_TRUE(editor.commands().execute(expected_command).ok())
            << shortcut;
      };

  const auto dispatch_and_apply_grid =
      [&](std::string_view shortcut, int current_preset,
          int expected_preset,
          quader::QuaderViewportKeybindingContext input_context = {}) {
        ASSERT_TRUE(editor.viewports().set_grid_preset(current_preset).ok());
        state.grid_preset = current_preset;
        const quader::QuaderViewportKeybindingDispatch dispatch =
            editor.commands().dispatch_viewport_input_binding(
                single_input_chord(shortcut), state, true, false,
                input_context);
        ASSERT_EQ(
            dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut)
            << shortcut;
        EXPECT_EQ(dispatch.shortcut.kind,
                  quader::ViewportShortcutKind::SetGridPreset)
            << shortcut;
        EXPECT_EQ(dispatch.shortcut.grid_preset, expected_preset) << shortcut;
        ASSERT_TRUE(
            editor.viewports().set_grid_preset(dispatch.shortcut.grid_preset)
                .ok());
      };

  EXPECT_EQ(editor.commands().shortcut_for(
                quader::quader_command::kViewportToggleLocalVisibility),
            "Backslash");
  dispatch_and_execute(
      "Backslash",
      quader::quader_command::kViewportToggleLocalVisibility);
  EXPECT_TRUE(editor.viewports().local_visibility_active());
  dispatch_and_execute(
      "Backslash",
      quader::quader_command::kViewportToggleLocalVisibility);
  EXPECT_FALSE(editor.viewports().local_visibility_active());
  EXPECT_EQ(editor.commands()
                .dispatch_viewport_input_binding(single_input_chord("Slash"),
                                                 state, true, false)
                .kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  EXPECT_EQ(editor.commands()
                .dispatch_viewport_input_binding(
                    single_input_chord("KeypadDivide"), state, true, false)
                .kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  EXPECT_EQ(editor.commands()
                .dispatch_viewport_input_binding(
                    single_input_chord("Backslash"), state, false, false)
                .kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  EXPECT_EQ(editor.commands()
                .dispatch_viewport_input_binding(
                    single_input_chord("Backslash"), state, true, true)
                .kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  for (const quader::QuaderViewportKeybindingContext blocked_context : {
           quader::QuaderViewportKeybindingContext{.navigation_active = true},
           quader::QuaderViewportKeybindingContext{.modal_tool_active = true},
           quader::QuaderViewportKeybindingContext{
               .active_tool_owns_viewport = true},
           quader::QuaderViewportKeybindingContext{
               .active_transform_drag = true},
           quader::QuaderViewportKeybindingContext{
               .operation_preview_active = true},
       }) {
    EXPECT_EQ(editor.commands()
                  .dispatch_viewport_input_binding(
                      single_input_chord("Backslash"), state, true, false,
                      blocked_context)
                  .kind,
              quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  }

  EXPECT_EQ(editor.commands().shortcut_for(
                quader::quader_command::kViewportToggleTransformSpace),
            "T");
  EXPECT_EQ(editor.commands().shortcut_for(
                quader::quader_command::kApplyMaterial),
            "Shift+T");
  EXPECT_EQ(editor.viewports().transform_space(),
            quader::QuaderTransformSpace::Global);
  dispatch_and_execute(
      "T", quader::quader_command::kViewportToggleTransformSpace);
  EXPECT_EQ(editor.viewports().transform_space(),
            quader::QuaderTransformSpace::Local);
  dispatch_and_execute(
      "T", quader::quader_command::kViewportToggleTransformSpace);
  EXPECT_EQ(editor.viewports().transform_space(),
            quader::QuaderTransformSpace::Global);
  EXPECT_EQ(editor.commands()
                .dispatch_viewport_input_binding(single_input_chord("T"),
                                                 state, false, false)
                .kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  EXPECT_EQ(editor.commands()
                .dispatch_viewport_input_binding(single_input_chord("T"),
                                                 state, true, true)
                .kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  for (const quader::QuaderViewportKeybindingContext blocked_context : {
           quader::QuaderViewportKeybindingContext{.navigation_active = true},
           quader::QuaderViewportKeybindingContext{.modal_tool_active = true},
           quader::QuaderViewportKeybindingContext{
               .active_tool_owns_viewport = true},
           quader::QuaderViewportKeybindingContext{
               .active_transform_drag = true},
           quader::QuaderViewportKeybindingContext{
               .operation_preview_active = true},
       }) {
    EXPECT_EQ(editor.commands()
                  .dispatch_viewport_input_binding(single_input_chord("T"),
                                                   state, true, false,
                                                   blocked_context)
                  .kind,
              quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  }
  EXPECT_TRUE(editor.query().entity(third.entity).has_value());

  dispatch_and_execute("H", quader::quader_command::kSceneHideSelection);
  ASSERT_TRUE(editor.query().entity(first.entity).has_value());
  ASSERT_TRUE(editor.query().entity(second.entity).has_value());
  EXPECT_FALSE(editor.query().entity(first.entity)->visible);
  EXPECT_FALSE(editor.query().entity(second.entity)->visible);

  dispatch_and_execute("Alt+H",
                       quader::quader_command::kSceneUnhideHiddenObjects);
  EXPECT_TRUE(editor.query().entity(first.entity)->visible);
  EXPECT_TRUE(editor.query().entity(second.entity)->visible);

  dispatch_and_apply_view("F1", quader::ViewportView::Top,
                          quader::quader_command::kViewportViewTop);
  EXPECT_EQ(editor.viewports().view(editor.viewports().focused_pane()),
            quader::ViewportView::Top);
  dispatch_and_apply_view("F2", quader::ViewportView::Side,
                          quader::quader_command::kViewportViewRight);
  EXPECT_EQ(editor.viewports().view(editor.viewports().focused_pane()),
            quader::ViewportView::Side);
  dispatch_and_apply_view("F3", quader::ViewportView::Front,
                          quader::quader_command::kViewportViewFront);
  EXPECT_EQ(editor.viewports().view(editor.viewports().focused_pane()),
            quader::ViewportView::Front);
  dispatch_and_apply_view("F4", quader::ViewportView::Perspective,
                          quader::quader_command::kViewportViewPerspective);
  EXPECT_EQ(editor.viewports().view(editor.viewports().focused_pane()),
            quader::ViewportView::Perspective);

  dispatch_and_execute("F5",
                       quader::quader_command::kViewportShadingWireframe);
  EXPECT_EQ(editor.viewports().shading_mode(editor.viewports().focused_pane()),
            quader::QuaderShadingMode::Wireframe);
  dispatch_and_execute("F6",
                       quader::quader_command::kViewportShadingShaded);
  EXPECT_EQ(editor.viewports().shading_mode(editor.viewports().focused_pane()),
            quader::QuaderShadingMode::Solid);
  dispatch_and_execute("F7",
                       quader::quader_command::kViewportShadingRendered);
  EXPECT_EQ(editor.viewports().shading_mode(editor.viewports().focused_pane()),
            quader::QuaderShadingMode::Material);

  dispatch_and_execute("Ctrl+1",
                       quader::quader_command::kViewportLayoutSingle);
  EXPECT_EQ(editor.viewports().layout(), quader::ViewportLayoutId::Single);
  dispatch_and_execute("Ctrl+2",
                       quader::quader_command::kViewportLayoutTwoVertical);
  EXPECT_EQ(editor.viewports().layout(), quader::ViewportLayoutId::TwoVertical);
  dispatch_and_execute("Ctrl+3",
                       quader::quader_command::kViewportLayoutTwoHorizontal);
  EXPECT_EQ(editor.viewports().layout(),
            quader::ViewportLayoutId::TwoHorizontal);
  dispatch_and_execute("Ctrl+4",
                       quader::quader_command::kViewportLayoutFourQuad);
  EXPECT_EQ(editor.viewports().layout(), quader::ViewportLayoutId::FourQuad);

  dispatch_and_apply_grid("Alt+WheelUp", 6, 7);
  EXPECT_EQ(editor.viewports().grid_preset(), 7);
  dispatch_and_apply_grid("Alt+WheelDown", 7, 6);
  EXPECT_EQ(editor.viewports().grid_preset(), 6);
  dispatch_and_apply_grid("Alt+MiddleMouseClick", 10,
                          quader::viewport_default_grid_preset(),
                          {.navigation_active = true,
                           .orbit_navigation_active = true});
  EXPECT_EQ(editor.viewports().grid_preset(), 6);

  const quader::QuaderViewportKeybindingDispatch removed_grid_f8 =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("F8"), state, true, false);
  EXPECT_EQ(removed_grid_f8.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);
  for (int preset = 1; preset <= 10; ++preset) {
    EXPECT_TRUE(editor.commands()
                    .shortcut_for(
                        editor.commands().keybinding_target_id_for_grid_preset(
                            preset))
                    .empty());
  }
}

TEST(EditorApiTests,
     KeybindingCatalogCoversMappedUnmappedAndScopedTargets) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(
                      keybinding_test_path("showcase-catalog"))
                  .ok());

  const std::vector<quader::QuaderCommandDescriptor> commands =
      editor.commands().available();
  const std::vector<quader::QuaderKeybindingRule> rules =
      editor.commands().keybindings();
  const std::vector<quader::QuaderKeybindingCatalogEntry> catalog =
      editor.commands().keybinding_catalog();

#ifndef NDEBUG
  EXPECT_EQ(commands.size(), 187u);
#else
  EXPECT_EQ(commands.size(), 188u);
#endif

  std::unordered_set<std::string> expected_target_ids;
  std::unordered_set<std::string> command_ids;
  for (const quader::QuaderCommandDescriptor &command : commands) {
    expected_target_ids.insert(command.id);
    command_ids.insert(command.id);
  }
  for (const quader::QuaderKeybindingRule &rule : rules) {
    expected_target_ids.insert(rule.target_id);
  }
  for (int preset = 1; preset <= 10; ++preset) {
    expected_target_ids.insert(
        editor.commands().keybinding_target_id_for_grid_preset(preset));
  }

  std::unordered_set<std::string> actual_target_ids;
  std::unordered_set<std::string> actual_rows;
  std::unordered_set<std::string> visible_label_contexts;
  for (const quader::QuaderKeybindingCatalogEntry &entry : catalog) {
    actual_target_ids.insert(entry.target_ids.begin(), entry.target_ids.end());
    const std::string row_key = entry.action_id + "|" + entry.scope_id;
    EXPECT_TRUE(actual_rows.insert(row_key).second) << row_key;
    const std::string label_context = entry.label + "|" + entry.scope_id;
    EXPECT_TRUE(visible_label_contexts.insert(label_context).second)
        << label_context;
    EXPECT_FALSE(entry.action_id.empty());
    EXPECT_FALSE(entry.scope_id.empty());
    EXPECT_FALSE(entry.scope_label.empty());
    EXPECT_FALSE(entry.scope_localization_key.empty());
    EXPECT_FALSE(entry.target_id.empty());
    EXPECT_FALSE(entry.target_ids.empty());
    EXPECT_FALSE(entry.label.empty()) << entry.target_id;
    EXPECT_FALSE(entry.localization_key.empty()) << entry.target_id;
    EXPECT_FALSE(entry.when.empty()) << entry.target_id;
    for (const std::string &target_id : entry.target_ids) {
      std::vector<const quader::QuaderKeybindingRule *> target_rules;
      for (const quader::QuaderKeybindingRule &rule : rules) {
        if (rule.target_id == target_id) {
          target_rules.push_back(&rule);
        }
      }
      EXPECT_LE(target_rules.size(), 1u) << target_id;
      if (entry.shortcut.empty()) {
        EXPECT_TRUE(target_rules.empty()) << target_id;
      } else {
        ASSERT_EQ(target_rules.size(), 1u) << target_id;
        EXPECT_EQ(target_rules.front()->shortcut, entry.shortcut) << target_id;
      }
    }
  }
  for (const std::string &target_id : expected_target_ids) {
    EXPECT_TRUE(actual_target_ids.contains(target_id))
        << "Catalog is missing target " << target_id;
  }
  for (const std::string &target_id : actual_target_ids) {
    EXPECT_TRUE(expected_target_ids.contains(target_id))
        << "Catalog contains unexpected target " << target_id;
  }
  const std::size_t non_command_target_count =
      static_cast<std::size_t>(std::count_if(
          actual_target_ids.begin(), actual_target_ids.end(),
          [&](const std::string &target_id) {
            return !command_ids.contains(target_id);
          }));
  EXPECT_EQ(non_command_target_count, 43u);

  for (const std::string &typed_id : typed_command_ids()) {
    EXPECT_TRUE(actual_target_ids.contains(typed_id)) << typed_id;
  }

  EXPECT_FALSE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kEditRedo.string(), "Ctrl+Y"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kEditRedo.string(),
      "Ctrl+Shift+Z"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kDeleteSelection.string(), "Delete"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog,
      quader::quader_command::kSelectToggleBackfaceSelection.string(),
      "Ctrl+Shift+F9"));
  EXPECT_FALSE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kDeleteSelection.string(),
      "Backspace"));

  const std::string vertex_target =
      editor.commands().keybinding_target_id_for_selection_mode(
          quader::QuaderSelectionMode::Vertex);
  EXPECT_TRUE(has_keybinding_catalog_entry(catalog, vertex_target, "1"));
  EXPECT_FALSE(
      has_keybinding_catalog_entry(catalog, vertex_target, "Keypad1"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, "viewport.modal_tool.finalize", "Space"));
  EXPECT_FALSE(has_keybinding_catalog_entry(
      catalog, "viewport.modal_tool.finalize", "Enter"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kSceneHideSelection.string(), "H"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kSceneUnhideHiddenObjects.string(),
      "Alt+H"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportViewTop.string(), "F1"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportViewRight.string(), "F2"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportViewFront.string(), "F3"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportViewPerspective.string(),
      "F4"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog,
      quader::quader_command::kViewportToggleLocalVisibility.string(),
      "Backslash"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog,
      quader::quader_command::kViewportToggleTransformSpace.string(), "T"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportShadingWireframe.string(),
      "F5"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportShadingShaded.string(),
      "F6"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportShadingRendered.string(),
      "F7"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportLayoutSingle.string(),
      "Ctrl+1"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportLayoutTwoVertical.string(),
      "Ctrl+2"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportLayoutTwoHorizontal.string(),
      "Ctrl+3"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportLayoutFourQuad.string(),
      "Ctrl+4"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportGridIncrement.string(),
      "Alt+WheelUp"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportGridDecrement.string(),
      "Alt+WheelDown"));
  EXPECT_TRUE(has_keybinding_catalog_entry(
      catalog, quader::quader_command::kViewportGridReset.string(),
      "Alt+MiddleMouseClick"));

  for (int preset = 1; preset <= 10; ++preset) {
    const std::string preset_target =
        editor.commands().keybinding_target_id_for_grid_preset(preset);
    const quader::QuaderKeybindingCatalogEntry *preset_entry =
        find_keybinding_catalog_entry(catalog, preset_target);
    ASSERT_NE(preset_entry, nullptr);
    EXPECT_TRUE(preset_entry->shortcut.empty()) << preset_target;
    EXPECT_TRUE(preset_entry->default_shortcut.empty()) << preset_target;
  }

  EXPECT_EQ(std::count_if(catalog.begin(), catalog.end(), [](const auto &entry) {
              return entry.label == "Radially Align Selected Components";
            }),
            1);
  const quader::QuaderKeybindingCatalogEntry *finalize_primitive =
      find_keybinding_catalog_entry(
          catalog,
          quader::quader_command::kToolsApplyPrimitiveCreation.string());
  const quader::QuaderKeybindingCatalogEntry *finalize_modal =
      find_keybinding_catalog_entry(catalog,
                                    "viewport.modal_tool.finalize");
  ASSERT_NE(finalize_primitive, nullptr);
  ASSERT_NE(finalize_modal, nullptr);
  EXPECT_EQ(finalize_primitive, finalize_modal);
  EXPECT_EQ(finalize_primitive->action_id, "tools.finalize");
  EXPECT_EQ(finalize_primitive->label, "Finalize Tool");
  EXPECT_EQ(finalize_primitive->context,
            quader::QuaderKeybindingContextKind::ModalTool);
  EXPECT_EQ(finalize_primitive->scope_id, "viewport.tool.modal");
  EXPECT_EQ(finalize_primitive->shortcut, "Space");

  const quader::QuaderKeybindingCatalogEntry *cancel_primitive =
      find_keybinding_catalog_entry(
          catalog,
          quader::quader_command::kToolsCancelPrimitiveCreation.string());
  const quader::QuaderKeybindingCatalogEntry *cancel_active =
      find_keybinding_catalog_entry(catalog, "viewport.cancel_active_tool");
  ASSERT_NE(cancel_primitive, nullptr);
  ASSERT_NE(cancel_active, nullptr);
  EXPECT_EQ(cancel_primitive, cancel_active);
  EXPECT_EQ(cancel_primitive->action_id, "tools.cancel");
  EXPECT_EQ(cancel_primitive->label, "Cancel Tool");
  EXPECT_EQ(cancel_primitive->context,
            quader::QuaderKeybindingContextKind::ModalTool);
  EXPECT_EQ(cancel_primitive->shortcut, "Escape");
  EXPECT_NE(cancel_primitive->when.find("transformDrag.active"),
            std::string::npos);

  const quader::QuaderKeybindingCatalogEntry *radial_align =
      find_keybinding_catalog_entry(
          catalog,
          editor.commands().command_id_for_modeling_action("radial_align"));
  ASSERT_NE(radial_align, nullptr);
  EXPECT_EQ(radial_align->context,
            quader::QuaderKeybindingContextKind::Viewport);
  EXPECT_EQ(radial_align->scope_id, "viewport.selection.component");

  const quader::QuaderKeybindingCatalogEntry *reset_fly_speed =
      find_keybinding_catalog_entry(
          catalog, "viewport.navigation.reset_fly_speed");
  ASSERT_NE(reset_fly_speed, nullptr);
  EXPECT_EQ(reset_fly_speed->context,
            quader::QuaderKeybindingContextKind::FlyNavigation);
  EXPECT_NE(reset_fly_speed->when.find("navigation.flyActive"),
            std::string::npos);

  const quader::QuaderKeybindingCatalogEntry *cycle_cut_keep_mode =
      find_keybinding_catalog_entry(catalog,
                                    "viewport.modal_tool.mode_cycle");
  ASSERT_NE(cycle_cut_keep_mode, nullptr);
  EXPECT_EQ(cycle_cut_keep_mode->label, "Cycle Cut Keep Mode");
  EXPECT_EQ(cycle_cut_keep_mode->context,
            quader::QuaderKeybindingContextKind::ModalTool);
  EXPECT_NE(cycle_cut_keep_mode->when.find("activeTool.cut"),
            std::string::npos);

  const quader::QuaderKeybindingCatalogEntry *interpolated_bridge_edges =
      find_keybinding_catalog_entry(
          catalog,
          editor.commands().command_id_for_modeling_action(
              "interpolated_bridge_edges"));
  const quader::QuaderKeybindingCatalogEntry *interpolated_bridge_faces =
      find_keybinding_catalog_entry(
          catalog,
          editor.commands().command_id_for_modeling_action(
              "interpolated_bridge_faces"));
  ASSERT_NE(interpolated_bridge_edges, nullptr);
  ASSERT_NE(interpolated_bridge_faces, nullptr);
  EXPECT_EQ(interpolated_bridge_edges, interpolated_bridge_faces);
  EXPECT_EQ(interpolated_bridge_edges->action_id,
            "modeling.preview.interpolated_bridge");

  const quader::QuaderKeybindingCatalogEntry *spawn_viewport_window =
      find_keybinding_catalog_entry(
          catalog, quader::quader_command::kViewportSpawnWindow.string());
  ASSERT_NE(spawn_viewport_window, nullptr);
  EXPECT_EQ(spawn_viewport_window->context,
            quader::QuaderKeybindingContextKind::Global);

  const quader::QuaderKeybindingCatalogEntry *fly =
      find_keybinding_catalog_entry(catalog, "viewport.navigation.fly");
  const quader::QuaderKeybindingCatalogEntry *zoom =
      find_keybinding_catalog_entry(catalog, "viewport.navigation.zoom");
  const quader::QuaderKeybindingCatalogEntry *zoom_to_cursor =
      find_keybinding_catalog_entry(
          catalog, "viewport.navigation.zoom_to_cursor");
  ASSERT_NE(fly, nullptr);
  ASSERT_NE(zoom, nullptr);
  ASSERT_NE(zoom_to_cursor, nullptr);
  EXPECT_EQ(fly->label, "Fly / Orthographic Pan");
  EXPECT_EQ(zoom->label, "Zoom / Adjust Fly Speed");
  EXPECT_EQ(zoom_to_cursor->label,
            "Zoom to Cursor / Adjust Fly Speed");

  const quader::QuaderKeybindingCatalogEntry *orbit =
      find_keybinding_catalog_entry(catalog, "viewport.navigation.orbit");
  ASSERT_NE(orbit, nullptr);
  EXPECT_EQ(orbit->shortcut, "MiddleMouseDrag");

  const quader::QuaderKeybindingCatalogEntry *create_instance =
      find_keybinding_catalog_entry(
          catalog, quader::quader_command::kEditCreateInstance.string());
  ASSERT_NE(create_instance, nullptr);
  EXPECT_TRUE(create_instance->shortcut.empty());
  EXPECT_EQ(create_instance->when, "!textInputActive");
  const auto create_instance_command =
      std::find_if(commands.begin(), commands.end(), [](const auto &command) {
        return command.id ==
               quader::quader_command::kEditCreateInstance.string();
      });
  ASSERT_NE(create_instance_command, commands.end());
  EXPECT_FALSE(create_instance_command->enabled);
}

TEST(EditorApiTests,
     KeybindingCatalogKeepsContextForRemovedTargetsAndShowsOverrides) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(
                      keybinding_test_path("showcase-effective"))
                  .ok());

  const std::vector<quader::QuaderKeybindingCatalogEntry> before =
      editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *before_undo =
      find_keybinding_catalog_entry(
          before, quader::quader_command::kEditUndo.string());
  const quader::QuaderKeybindingCatalogEntry *before_orbit =
      find_keybinding_catalog_entry(before, "viewport.navigation.orbit");
  ASSERT_NE(before_undo, nullptr);
  ASSERT_NE(before_orbit, nullptr);
  const auto undo_context = before_undo->context;
  const auto orbit_context = before_orbit->context;
  const std::string undo_when = before_undo->when;
  const std::string orbit_when = before_orbit->when;

  ASSERT_TRUE(editor.preferences()
                  .remove_shortcut(quader::quader_command::kEditUndo)
                  .ok());
  ASSERT_TRUE(editor.preferences()
                  .remove_shortcut("viewport.navigation.orbit")
                  .ok());
  const std::vector<quader::QuaderKeybindingCatalogEntry> removed =
      editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *removed_undo =
      find_keybinding_catalog_entry(
          removed, quader::quader_command::kEditUndo.string());
  const quader::QuaderKeybindingCatalogEntry *removed_orbit =
      find_keybinding_catalog_entry(removed, "viewport.navigation.orbit");
  ASSERT_NE(removed_undo, nullptr);
  ASSERT_NE(removed_orbit, nullptr);
  EXPECT_TRUE(removed_undo->shortcut.empty());
  EXPECT_TRUE(removed_orbit->shortcut.empty());
  EXPECT_EQ(removed_undo->context, undo_context);
  EXPECT_EQ(removed_orbit->context, orbit_context);
  EXPECT_EQ(removed_undo->when, undo_when);
  EXPECT_EQ(removed_orbit->when, orbit_when);

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kEditUndo,
                                "Ctrl+Alt+Z")
                  .ok());
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut("viewport.navigation.orbit",
                                "Alt+LeftMouseDrag")
                  .ok());
  const std::vector<quader::QuaderKeybindingCatalogEntry> overridden =
      editor.commands().keybinding_catalog();
  const quader::QuaderKeybindingCatalogEntry *overridden_undo =
      find_keybinding_catalog_entry(
          overridden, quader::quader_command::kEditUndo.string());
  const quader::QuaderKeybindingCatalogEntry *overridden_orbit =
      find_keybinding_catalog_entry(overridden,
                                    "viewport.navigation.orbit");
  ASSERT_NE(overridden_undo, nullptr);
  ASSERT_NE(overridden_orbit, nullptr);
  EXPECT_EQ(overridden_undo->shortcut, "Ctrl+Alt+Z");
  EXPECT_EQ(overridden_orbit->shortcut, "Alt+LeftMouseDrag");
  EXPECT_EQ(overridden_undo->context, undo_context);
  EXPECT_EQ(overridden_orbit->context, orbit_context);
  EXPECT_EQ(overridden_undo->when, undo_when);
  EXPECT_EQ(overridden_orbit->when, orbit_when);
}

TEST(EditorApiLayerTests,
     PublicFacetSnapshotsHistoryAndShortcutFreeRemovalCommandStayCoherent) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const auto group = editor.scene().create_entity(
      quader::QuaderCreateEntityArgs{
          .name = "Layer Group",
          .kind = quader::QuaderEntityKind::Empty,
      });
  ASSERT_TRUE(group.ok()) << group.error_message();
  const auto child = editor.scene().create_entity(
      quader::QuaderCreateEntityArgs{
          .name = "Layer Child",
          .kind = quader::QuaderEntityKind::Mesh,
          .parent = group.value.id,
      });
  ASSERT_TRUE(child.ok()) << child.error_message();
  ASSERT_TRUE(editor.scene().select(group.value.id).ok());

  const std::uint64_t initial_layer_revision =
      editor.revisions().scene_layer_revision;
  const auto first = editor.layers().create_from_selection();
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(first.changed());
  EXPECT_GT(first.revisions.scene_layer_revision, initial_layer_revision);
  EXPECT_EQ(first.value.name, "Layer");
  EXPECT_EQ(first.value.members,
            std::vector<quader::EntityId>(
                {group.value.id, child.value.id}));
  EXPECT_GT(editor.revisions().scene_layer_revision,
            initial_layer_revision);
  EXPECT_GT(editor.snapshot().revisions.scene_layer_revision,
            initial_layer_revision);
  EXPECT_EQ(editor.layers().revision(),
            editor.revisions().scene_layer_revision);
  ASSERT_EQ(editor.snapshot().layers.size(), 1u);
  ASSERT_EQ(editor.api().layers().all().size(), 1u);
  ASSERT_TRUE(editor.query().entity(child.value.id).has_value());
  EXPECT_EQ(editor.query().entity(child.value.id)->layer_id,
            first.value.id);

  const auto second = editor.layers().create();
  ASSERT_TRUE(second.ok()) << second.error_message();
  EXPECT_EQ(second.value.name, "Layer_1");
  ASSERT_TRUE(editor.layers().add_selection(second.value.id).ok());
  EXPECT_EQ(editor.query().entity(group.value.id)->layer_id,
            second.value.id);
  EXPECT_EQ(editor.query().entity(child.value.id)->layer_id,
            second.value.id);

  const std::uint64_t stable_revision =
      editor.revisions().scene_layer_revision;
  const std::string stable_undo_label = editor.query().history().undo_label;
  const bool stable_dirty = editor.project().dirty();
  const quader::QuaderApiVoidResult unchanged =
      editor.layers().add_selection(second.value.id);
  EXPECT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(editor.revisions().scene_layer_revision, stable_revision);
  EXPECT_EQ(editor.query().history().undo_label, stable_undo_label);
  EXPECT_EQ(editor.project().dirty(), stable_dirty);

  const quader::QuaderApiVoidResult wrong_layer_removal =
      editor.layers().remove_selection(first.value.id);
  EXPECT_TRUE(wrong_layer_removal.ok());
  EXPECT_FALSE(wrong_layer_removal.changed());
  const quader::QuaderApiVoidResult layer_removal =
      editor.layers().remove_selection(second.value.id);
  ASSERT_TRUE(layer_removal.ok());
  ASSERT_TRUE(layer_removal.changed());
  EXPECT_EQ(editor.query().entity(group.value.id)->layer_id,
            quader::kInvalidQuaderLayerId);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(editor.query().entity(group.value.id)->layer_id,
            second.value.id);

  const quader::QuaderApiVoidResult emptied =
      editor.layers().empty(second.value.id);
  ASSERT_TRUE(emptied.ok());
  ASSERT_TRUE(emptied.changed());
  EXPECT_TRUE(editor.query().entity(group.value.id).has_value());
  EXPECT_TRUE(editor.query().entity(child.value.id).has_value());
  EXPECT_EQ(editor.query().entity(group.value.id)->layer_id,
            quader::kInvalidQuaderLayerId);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(editor.query().entity(group.value.id)->layer_id,
            second.value.id);

  const auto descriptor = editor.commands().descriptor(
      quader::quader_command::kSceneLayersRemoveSelection);
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_TRUE(descriptor->enabled);
  EXPECT_TRUE(descriptor->shortcut.empty());
  const quader::QuaderApiVoidResult removed = editor.commands().execute(
      quader::quader_command::kSceneLayersRemoveSelection);
  ASSERT_TRUE(removed.ok()) << removed.error_message();
  ASSERT_TRUE(removed.changed());
  EXPECT_EQ(editor.query().entity(group.value.id)->layer_id,
            quader::kInvalidQuaderLayerId);
  EXPECT_EQ(editor.query().entity(child.value.id)->layer_id,
            quader::kInvalidQuaderLayerId);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(editor.query().entity(group.value.id)->layer_id,
            second.value.id);

  const std::array<quader::QuaderLayerId, 2> deleted = {
      first.value.id,
      second.value.id,
  };
  const quader::QuaderApiVoidResult destroyed =
      editor.layers().destroy(deleted);
  ASSERT_TRUE(destroyed.ok()) << destroyed.error_message();
  ASSERT_TRUE(destroyed.changed());
  EXPECT_TRUE(editor.layers().all().empty());
  EXPECT_EQ(editor.query().history().undo_label, "Delete Layers");
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_EQ(editor.layers().all().size(), 2u);
  EXPECT_EQ(editor.query().entity(group.value.id)->layer_id,
            second.value.id);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(editor.layers().all().empty());
  EXPECT_TRUE(editor.query().entity(group.value.id).has_value());
  EXPECT_TRUE(editor.query().entity(child.value.id).has_value());
}

TEST(EditorApiLayerTests,
     LayerMutationMarksCleanProjectDirtyAndUndoRestoresLayerState) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_FALSE(editor.project().dirty());

  const quader::QuaderApiResult<quader::QuaderLayerSnapshot> created =
      editor.layers().create();
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(created.changed());
  EXPECT_TRUE(editor.project().dirty());
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(editor.project().dirty());
  EXPECT_TRUE(editor.layers().all().empty());
}

TEST(EditorApiLayerTests,
     LayerLockSnapshotAndHistoryPreserveObjectLocalLockAndSelection) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const auto entity = editor.scene().create_entity(
      quader::QuaderCreateEntityArgs{
          .name = "Layer Lock Member",
          .kind = quader::QuaderEntityKind::Mesh,
      });
  ASSERT_TRUE(entity.ok()) << entity.error_message();
  ASSERT_TRUE(editor.scene().select(entity.value.id).ok());
  const auto created = editor.layers().create_from_selection();
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(created.changed());
  EXPECT_FALSE(created.value.locked);

  const std::uint64_t revision_before = editor.layers().revision();
  const quader::QuaderApiVoidResult locked =
      editor.layers().set_locked(created.value.id, true);
  ASSERT_TRUE(locked.ok()) << locked.error_message();
  ASSERT_TRUE(locked.changed());
  ASSERT_TRUE(editor.layers().layer(created.value.id).has_value());
  EXPECT_TRUE(editor.layers().layer(created.value.id)->locked);
  EXPECT_EQ(editor.layers().revision(), revision_before + 1u);
  ASSERT_TRUE(editor.query().entity(entity.value.id).has_value());
  EXPECT_FALSE(editor.query().entity(entity.value.id)->locked)
      << "the layer mask must not rewrite the entity-local lock";
  EXPECT_TRUE(selected_entities(editor).empty());
  EXPECT_EQ(editor.query().history().undo_label, "Set Layer Lock");

  const std::uint64_t stable_revision = editor.layers().revision();
  const quader::QuaderApiVoidResult unchanged =
      editor.layers().set_locked(created.value.id, true);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(editor.layers().revision(), stable_revision);
  EXPECT_EQ(editor.query().history().undo_label, "Set Layer Lock");

  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.layers().layer(created.value.id).has_value());
  EXPECT_FALSE(editor.layers().layer(created.value.id)->locked);
  EXPECT_EQ(selected_entities(editor),
            std::vector<quader::EntityId>{entity.value.id});
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(editor.layers().layer(created.value.id)->locked);
  EXPECT_TRUE(selected_entities(editor).empty());

  quader::QuaderTransform blocked_transform;
  blocked_transform.position = {8.0f, 0.0f, 0.0f};
  const quader::QuaderApiVoidResult batch_move =
      editor.scene()
          .batch("Move Layer-Locked Entity")
          .set_transform(entity.value.id, blocked_transform)
          .commit();
  ASSERT_TRUE(batch_move.ok()) << batch_move.error_message();
  EXPECT_FALSE(batch_move.changed());
  ASSERT_TRUE(editor.query().entity(entity.value.id).has_value());
  EXPECT_FLOAT_EQ(editor.query().entity(entity.value.id)->transform.position.x,
                  0.0f);
}

TEST(EditorApiLayerTests,
     LayerLockAndLockedAssignmentProjectRuntimeObjectSelection) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef first =
      create_selected_box(editor, "Layer-Locked Runtime Selection");
  const auto layer = editor.layers().create_from_selection();
  ASSERT_TRUE(layer.ok()) << layer.error_message();
  ASSERT_TRUE(editor.modeling().selection_summary().has_selection);

  const quader::QuaderApiVoidResult locked =
      editor.layers().set_locked(layer.value().id, true);
  ASSERT_TRUE(locked.ok()) << locked.error_message();
  ASSERT_TRUE(locked.changed());
  EXPECT_TRUE(selected_entities(editor).empty());
  EXPECT_FALSE(editor.modeling().selection_summary().has_selection);

  ASSERT_TRUE(editor.history().undo().ok());
  quader::QuaderSelectionSummary summary =
      editor.modeling().selection_summary();
  EXPECT_TRUE(summary.has_selection);
  EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Object);
  EXPECT_EQ(summary.active_object_id, first.modeling_object_id);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(selected_entities(editor).empty());
  EXPECT_FALSE(editor.modeling().selection_summary().has_selection);

  const quader::QuaderModelingObjectRef second =
      create_selected_box(editor, "Assign Runtime Selection To Locked Layer");
  ASSERT_TRUE(editor.modeling().selection_summary().has_selection);
  const quader::QuaderApiVoidResult assigned =
      editor.layers().add_selection(layer.value().id);
  ASSERT_TRUE(assigned.ok()) << assigned.error_message();
  ASSERT_TRUE(assigned.changed());
  EXPECT_TRUE(selected_entities(editor).empty());
  EXPECT_FALSE(editor.modeling().selection_summary().has_selection);

  ASSERT_TRUE(editor.history().undo().ok());
  summary = editor.modeling().selection_summary();
  EXPECT_TRUE(summary.has_selection);
  EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Object);
  EXPECT_EQ(summary.active_object_id, second.modeling_object_id);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(selected_entities(editor).empty());
  EXPECT_FALSE(editor.modeling().selection_summary().has_selection);
}

TEST(EditorApiLayerTests,
     ModelingDuplicateAndDeleteUndoPreserveLayerMembership) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef source =
      create_selected_box(editor, "Layered Modeling Source");
  const auto layer = editor.layers().create_from_selection();
  ASSERT_TRUE(layer.ok()) << layer.error_message();
  ASSERT_EQ(editor.query().entity(source.entity)->layer_id, layer.value().id);

  const quader::QuaderApiVoidResult duplicated = editor.commands().execute(
      quader::quader_command::kDuplicateSelection);
  ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
  ASSERT_TRUE(duplicated.changed());
  const std::vector<quader::EntityId> duplicate_selection =
      selected_entities(editor);
  ASSERT_EQ(duplicate_selection.size(), 1u);
  const quader::EntityId duplicate = duplicate_selection.front();
  ASSERT_NE(duplicate, source.entity);
  ASSERT_TRUE(editor.query().entity(duplicate).has_value());
  EXPECT_EQ(editor.query().entity(duplicate)->layer_id, layer.value().id);

  const quader::QuaderApiVoidResult deleted = editor.commands().execute(
      quader::quader_command::kDeleteSelection);
  ASSERT_TRUE(deleted.ok()) << deleted.error_message();
  ASSERT_TRUE(deleted.changed());
  EXPECT_FALSE(editor.query().entity(duplicate).has_value());

  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.query().entity(duplicate).has_value());
  EXPECT_EQ(editor.query().entity(duplicate)->layer_id, layer.value().id);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_FALSE(editor.query().entity(duplicate).has_value());
  ASSERT_TRUE(editor.history().redo().ok());
  ASSERT_TRUE(editor.query().entity(duplicate).has_value());
  EXPECT_EQ(editor.query().entity(duplicate)->layer_id, layer.value().id);
}

TEST(EditorApiLayerTests,
     LayerLockedAncestorBlocksBatchTransformForFutureUnassignedChild) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const auto group = editor.scene().create_entity(
      quader::QuaderCreateEntityArgs{
          .name = "Layer-Locked Group",
          .kind = quader::QuaderEntityKind::Empty,
      });
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(editor.scene().select(group.value.id).ok());
  const auto layer = editor.layers().create_from_selection();
  ASSERT_TRUE(layer.ok()) << layer.error_message();

  const auto child = editor.scene().create_entity(
      quader::QuaderCreateEntityArgs{
          .name = "Future Unassigned Child",
          .kind = quader::QuaderEntityKind::Mesh,
          .parent = group.value.id,
      });
  ASSERT_TRUE(child.ok()) << child.error_message();
  ASSERT_TRUE(editor.query().entity(child.value.id).has_value());
  EXPECT_EQ(editor.query().entity(child.value.id)->layer_id,
            quader::kInvalidQuaderLayerId);
  ASSERT_TRUE(editor.layers().set_locked(layer.value.id, true).ok());

  quader::QuaderTransform blocked_transform;
  blocked_transform.position = {9.0f, 0.0f, 0.0f};
  const quader::QuaderApiVoidResult batch_move =
      editor.scene()
          .batch("Move Child of Layer-Locked Group")
          .set_transform(child.value.id, blocked_transform)
          .commit();
  ASSERT_TRUE(batch_move.ok()) << batch_move.error_message();
  EXPECT_FALSE(batch_move.changed());
  ASSERT_TRUE(editor.query().entity(child.value.id).has_value());
  EXPECT_FLOAT_EQ(editor.query().entity(child.value.id)->transform.position.x,
                  0.0f);
}
