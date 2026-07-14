////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "core/public_command_targets.hpp"
#include "editor/input/quader_keybinding_service.hpp"
#include "editor/input/viewport_input_binding_adapter.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "support/behavior_scenario.hpp"
#include "ui/viewport_shortcut_controller.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <gtest/gtest.h>
#ifndef NDEBUG
#include <nlohmann/json.hpp>
#endif

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

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
  const std::vector<quader::QuaderCommandDescriptor> commands =
      editor.commands().available();
  const auto found =
      std::find_if(commands.begin(), commands.end(),
                   [&](const quader::QuaderCommandDescriptor &command) {
                     return command.id == command_id.string();
                   });
  return found == commands.end()
             ? std::nullopt
             : std::optional<quader::QuaderCommandDescriptor>{*found};
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
  ASSERT_TRUE(editor.selection().select_all().ok());
  if (mode == quader::QuaderSelectionMode::Face) {
    ASSERT_TRUE(editor.selection().select_face(object, 0).ok());
  } else if (mode == quader::QuaderSelectionMode::Edge) {
    ASSERT_TRUE(editor.selection().select_all().ok());
  } else if (mode == quader::QuaderSelectionMode::Vertex) {
    ASSERT_TRUE(editor.selection().select_all().ok());
  }
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
    const std::optional<quader::QuaderCommandDescriptor> spawn_window =
        command_descriptor(editor,
                           quader::quader_command::kViewportSpawnWindow);
    ASSERT_TRUE(spawn_window.has_value());
    EXPECT_FALSE(spawn_window->enabled);
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kViewportViewPerspective),
              "");
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
              "Backspace");
    EXPECT_EQ(
        editor.commands().command_id_for_modeling_action("invert_mesh"),
        std::string(quader::quader_command::kModelingInvertMesh.string()));
    EXPECT_EQ(editor.commands().command_id_for_modeling_action("bridge_edges"),
              quader::quader_modeling_command_target_id("bridge_edges"));
    EXPECT_EQ(editor.commands().command_id_for_tool_activation("knife"),
              quader::quader_tool_activation_command_target_id("knife"));
    EXPECT_FALSE(editor.commands().last_status().has_value());
    const quader::QuaderApiVoidResult missing_command =
        editor.commands().execute("__missing_command__");
    EXPECT_FALSE(missing_command.ok());
    EXPECT_FALSE(missing_command);
    EXPECT_EQ(missing_command.error_message(), "Unknown command id.");
    ASSERT_TRUE(editor.commands().last_status().has_value());
    expect_same_api_status(*editor.commands().last_status(), missing_command);
    EXPECT_EQ(editor.commands()
                  .execute("modeling.__missing_action__")
                  .error_message(),
              "Unknown command id.");
    EXPECT_EQ(editor.commands()
                  .execute(quader::quader_tool_activation_command_target_id(
                      "__missing_tool__"))
                  .error_message(),
              "Unknown command id.");

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
      quader::quader_viewport_selection_mode_target_id("face");
  EXPECT_EQ(editor.commands().shortcut_for(face_selection_target), "Keypad3");
  ASSERT_TRUE(editor.commands().resolve_viewport_shortcut(
      quader::ViewportKey::Number3, {}, state, true, false, shortcut));
  EXPECT_EQ(shortcut.kind, quader::ViewportShortcutKind::SetSelectionMode);
  EXPECT_EQ(shortcut.selection_mode, quader::ViewportSelectionMode::Face);

  ASSERT_TRUE(
      editor.preferences().set_shortcut(face_selection_target, "Ctrl+3").ok());
  EXPECT_EQ(editor.commands().shortcut_for(face_selection_target), "Ctrl+3");
  shortcut = {};
  EXPECT_FALSE(editor.commands().resolve_viewport_shortcut(
      quader::ViewportKey::Number3, {}, state, true, false, shortcut));
  ASSERT_TRUE(editor.commands().resolve_viewport_shortcut(
      quader::ViewportKey::Number3, {.primary = true}, state, true, false,
      shortcut));
  EXPECT_EQ(shortcut.kind, quader::ViewportShortcutKind::SetSelectionMode);
  EXPECT_EQ(shortcut.selection_mode, quader::ViewportSelectionMode::Face);
  ASSERT_TRUE(editor.preferences().reset_shortcut(face_selection_target).ok());
  EXPECT_EQ(editor.commands().shortcut_for(face_selection_target), "Keypad3");

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
  ASSERT_EQ(next_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(next_dispatch.command_id,
            quader::quader_command::kSceneDocumentNext.string());

  const quader::QuaderViewportKeybindingDispatch previous_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Shift+Tab"), state, false, false);
  ASSERT_EQ(previous_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(previous_dispatch.command_id,
            quader::quader_command::kSceneDocumentPrevious.string());

  const quader::QuaderViewportKeybindingDispatch focused_copy_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+C"), state, true, false);
  ASSERT_EQ(focused_copy_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(focused_copy_dispatch.command_id,
            quader::quader_command::kCopySelection.string());
  EXPECT_NE(focused_copy_dispatch.command_id,
            quader::quader_command::kViewportFrameSelection.string());

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

  const quader::QuaderViewportKeybindingDispatch redo_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Y"), state, false, false);
  ASSERT_EQ(redo_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(redo_dispatch.command_id,
            quader::quader_command::kEditRedo.string());

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
TEST(EditorApiTests, DebugCopySelectionJsonShortcutWinsOverFocusSelectionBind) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(
                      keybinding_test_path(
                          "debug-copy-selection-json-reserved-shortcut"))
                  .ok());

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kViewportFrameSelection,
                                "Ctrl+Shift+C")
                  .ok());
  EXPECT_EQ(editor.commands().shortcut_for(
                quader::quader_command::kViewportFrameSelection),
            "Ctrl+Shift+C");

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
      action));
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
                                  "Ctrl+Z", "!textInputActive && canUndo"));
  EXPECT_TRUE(has_keybinding_when(rules,
                                  quader::quader_command::kEditRedo.string(),
                                  "Ctrl+Y", "!textInputActive && canRedo"));
  EXPECT_TRUE(
      has_keybinding_when(rules, quader::quader_command::kEditRedo.string(),
                          "Ctrl+Shift+Z", "!textInputActive && canRedo"));
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
    ASSERT_EQ(escape_without_preview.kind,
              quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(escape_without_preview.shortcut.kind,
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

  action = quader::ViewportInputActionKind::None;
  ASSERT_TRUE(editor.commands().resolve_viewport_input_action(
      *quader::quader_input_parse_chord("Enter"), state, true, false, action,
      modal_context));
  EXPECT_EQ(action, quader::ViewportInputActionKind::ModalToolFinalize);

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
             std::string_view{"selection.select_all"},
             std::string_view{"selection.invert"},
             std::string_view{"scene.group_selection"},
             std::string_view{"scene.ungroup_selection"},
         }) {
      EXPECT_EQ(editor.commands().execute(retired_command_id).error_message(),
                "Unknown command id.")
          << retired_command_id;
    }
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
  EXPECT_EQ(object_panel->actions.front().action_id, "invert_mesh");
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
    create_selected_box(editor, "Unsaved Preview Box");
    ASSERT_TRUE(
        editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
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
    EXPECT_FALSE(
        viewport.set_shading_mode(quader::QuaderShadingMode::XRay).ok());
    EXPECT_EQ(
        editor.viewports().shading_mode(quader::ViewportPaneId::Perspective),
        quader::QuaderShadingMode::Solid);

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
        editor.selection().invert();
    ASSERT_TRUE(inverted_all.ok()) << inverted_all.error_message();
    EXPECT_TRUE(inverted_all.changed());
    EXPECT_TRUE(selected_entities(editor).empty());

    ASSERT_TRUE(editor.selection().select_object(loose_light.value().id).ok());
    const quader::QuaderApiVoidResult inverted_one =
        editor.selection().invert();
    ASSERT_TRUE(inverted_one.ok()) << inverted_one.error_message();
    expected.erase(
        std::remove(expected.begin(), expected.end(), loose_light.value().id),
        expected.end());
    EXPECT_EQ(selected_entities(editor), expected);
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
    EXPECT_EQ(editor.query().history().undo_label, "Duplicate Meshes");

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
    EXPECT_EQ(editor.query().history().redo_label, "Duplicate Meshes");

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
    EXPECT_EQ(settings.title, "Half-Cut");
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

  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
}

TEST(EditorApiTests, EdgeBevelToolSettingsUseOperationSchemaWidthLimits) {
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  (void)create_selected_box(editor, "Edge Bevel Schema Settings Source");
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
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

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Box).ok());
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

TEST(EditorApiTests, HistoryUndoUsesDocumentHistoryWhenOnlyIdleModalToolIsActive) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    create_selected_box(editor, "Idle Modal Undo Box");
    ASSERT_TRUE(editor.history().can_undo());

    const quader::QuaderApiVoidResult activate_box =
        editor.tools().activate(quader::ViewportToolId::Box);
    ASSERT_TRUE(activate_box.ok()) << activate_box.error_message();
    EXPECT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());

    const quader::QuaderApiVoidResult undo =
        editor.commands().execute(quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(undo.changed());
    EXPECT_FALSE(entity_named(editor, "Idle Modal Undo Box").has_value());
}
