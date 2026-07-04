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
  std::ifstream file(path);
  std::string text;
  std::string line;
  while (std::getline(file, line)) {
    text += line;
    text += '\n';
  }
  return text;
}

bool contains_json_string_pair(std::string_view text, std::string_view key,
                               std::string_view value) {
  const std::string compact =
      "\"" + std::string(key) + "\":\"" + std::string(value) + "\"";
  const std::string pretty =
      "\"" + std::string(key) + "\": \"" + std::string(value) + "\"";
  return text.find(compact) != std::string_view::npos ||
         text.find(pretty) != std::string_view::npos;
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
    EXPECT_TRUE(command_descriptor(editor, quader::quader_command::kProjectSave)
                    .has_value());
    EXPECT_TRUE(
        command_descriptor(editor, quader::quader_command::kProjectSaveAs)
            .has_value());
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
        "Ctrl+N");
    EXPECT_EQ(
        editor.commands().shortcut_for(quader::quader_command::kProjectOpen),
        "Ctrl+O");
    EXPECT_EQ(
        editor.commands().shortcut_for(quader::quader_command::kProjectSave),
        "Ctrl+S");
    EXPECT_EQ(
        editor.commands().shortcut_for(quader::quader_command::kProjectSaveAs),
        "Ctrl+Shift+S");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneCreatePointLight),
              "");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kSceneCreateDirectionalLight),
              "");
    EXPECT_EQ(editor.commands().shortcut_for(
                  quader::quader_command::kViewportFrameSelection),
              "Home");
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
    EXPECT_FALSE(editor.commands().execute("__missing_command__").ok());
    EXPECT_FALSE(editor.commands().execute("__missing_command__"));
    EXPECT_EQ(editor.commands().execute("__missing_command__").error_message(),
              "Unknown command id.");
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
}

TEST(EditorApiTests, ProjectDocumentCommandsRouteThroughProjectApi) {
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
  compare_project_command(
      quader::quader_command::kProjectSave,
      [](quader::QuaderProjectApi &project) { return project.save(); });
  compare_project_command(
      quader::quader_command::kProjectSaveAs,
      [](quader::QuaderProjectApi &project) { return project.save_as({}); });
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

  const quader::QuaderApiVoidResult saved = editor.project().save_as(path);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  EXPECT_TRUE(saved.changed());
  EXPECT_EQ(editor.project().path(), path);
  EXPECT_FALSE(editor.project().dirty());
  EXPECT_TRUE(std::filesystem::exists(path));
  const std::string saved_text = read_text_file(path);
  EXPECT_TRUE(
      contains_json_string_pair(saved_text, "kind", "quader.project-document"));
  EXPECT_NE(saved_text.find("\"sceneSettings\""), std::string::npos);
  EXPECT_TRUE(
      contains_json_string_pair(saved_text, "environmentId", "pillars_2k"));
  EXPECT_NE(saved_text.find("\"postProcess\""), std::string::npos);
  EXPECT_NE(saved_text.find("\"light\""), std::string::npos);
  EXPECT_NE(saved_text.find("\"Persisted Key Light\""), std::string::npos);
  EXPECT_EQ(saved_text.find("\"session\""), std::string::npos);

  CommandTestHarness open_harness;
  quader::QuaderEditorApi &opened = open_harness.editor;
  const quader::QuaderRevisionStamp before_open_revisions = opened.revisions();
  const quader::QuaderApiVoidResult open_result = opened.project().open(path);
  ASSERT_TRUE(open_result.ok()) << open_result.error_message();
  EXPECT_TRUE(open_result.changed());
  EXPECT_EQ(opened.project().path(), path);
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
  EXPECT_EQ(selected_entities(opened),
            std::vector<quader::EntityId>{opened_light->id});

  const quader::QuaderApiVoidResult moved =
      opened.modeling().move_selection({0.25f, 0.0f, 0.0f});
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  EXPECT_TRUE(moved.changed());
  EXPECT_TRUE(opened.project().dirty());
  const quader::QuaderApiVoidResult saved_again = opened.project().save();
  ASSERT_TRUE(saved_again.ok()) << saved_again.error_message();
  EXPECT_FALSE(opened.project().dirty());
}

TEST(EditorApiTests, ProjectCommandsUseRuntimePathHooksForEmptyPaths) {
  const std::filesystem::path path = project_test_path("runtime-hooks");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  int save_path_requests = 0;
  int open_path_requests = 0;
  editor.project().set_runtime_hooks(quader::QuaderProjectRuntimeHooks{
      .choose_open_path = [&]() -> std::optional<std::filesystem::path> {
        ++open_path_requests;
        return path;
      },
      .choose_save_as_path = [&]() -> std::optional<std::filesystem::path> {
        ++save_path_requests;
        return path;
      },
  });

  (void)create_selected_box(editor, "Hook Saved Box");
  const quader::QuaderApiVoidResult saved =
      editor.commands().execute(quader::quader_command::kProjectSave);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  EXPECT_EQ(save_path_requests, 1);
  EXPECT_EQ(editor.project().path(), path);
  EXPECT_TRUE(std::filesystem::exists(path));

  ASSERT_TRUE(
      editor.commands().execute(quader::quader_command::kProjectNew).ok());
  EXPECT_FALSE(entity_named(editor, "Hook Saved Box").has_value());

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

TEST(EditorApiTests, GlobalCommandKeybindingsResolveWithoutViewportFocus) {
  const std::filesystem::path path =
      keybinding_test_path("global-command-shortcuts");
  CommandTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
  create_selected_box(editor, "Global Undo Shortcut Box");

  quader::ViewportToolState state;
  const quader::QuaderViewportKeybindingDispatch open_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+O"), state, false, false);
  ASSERT_EQ(open_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(open_dispatch.command_id,
            quader::quader_command::kProjectOpen.string());

  const quader::QuaderViewportKeybindingDispatch save_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+S"), state, false, false);
  ASSERT_EQ(save_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(save_dispatch.command_id,
            quader::quader_command::kProjectSave.string());

  const quader::QuaderViewportKeybindingDispatch save_as_dispatch =
      editor.commands().dispatch_viewport_input_binding(
          single_input_chord("Ctrl+Shift+S"), state, false, false);
  ASSERT_EQ(save_as_dispatch.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::Command);
  EXPECT_EQ(save_as_dispatch.command_id,
            quader::quader_command::kProjectSaveAs.string());

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

  const quader::QuaderViewportKeybindingDispatch
      perspective_without_viewport_focus =
          editor.commands().dispatch_viewport_input_binding(
              single_input_chord("P"), state, false, false);
  EXPECT_EQ(perspective_without_viewport_focus.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);

  const quader::QuaderViewportKeybindingDispatch
      perspective_with_viewport_focus =
          editor.commands().dispatch_viewport_input_binding(
              single_input_chord("P"), state, true, false);
  ASSERT_EQ(perspective_with_viewport_focus.kind,
            quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
  EXPECT_EQ(perspective_with_viewport_focus.shortcut.kind,
            quader::ViewportShortcutKind::SetView);
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

    const std::optional<std::vector<quader::QuaderInputChord>> perspective =
        quader::quader_input_parse_shortcut("P");
    ASSERT_TRUE(perspective.has_value());
    ASSERT_EQ(perspective->size(), 1u);
    const quader::QuaderViewportKeybindingDispatch perspective_dispatch =
        editor.commands().dispatch_viewport_input_binding(perspective->front(),
                                                          state, true, false);
    ASSERT_EQ(perspective_dispatch.kind,
              quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(perspective_dispatch.shortcut.kind,
              quader::ViewportShortcutKind::SetView);
    EXPECT_EQ(perspective_dispatch.shortcut.view,
              quader::ViewportView::Perspective);

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

TEST(EditorApiTests, LockedEntitiesCannotBeSelectedThroughPublicSelectionApi) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs args;
    args.name = "Locked Selection Target";
    const quader::QuaderApiResult<quader::QuaderEntityRef> entity =
        editor.scene().create_entity(args);
    ASSERT_TRUE(entity.ok()) << entity.error_message();
    ASSERT_TRUE(editor.scene().set_locked(entity.value().id, true).ok());

    const quader::QuaderApiVoidResult selected =
        editor.selection().select_object(entity.value().id);
    ASSERT_TRUE(selected.ok()) << selected.error_message();
    EXPECT_FALSE(entity_by_id(editor, entity.value().id)->selected);
    EXPECT_FALSE(entity_by_id(editor, entity.value().id)->active);
    EXPECT_FALSE(editor.query().selection().has_selection);
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
    unsupported_material.texture_slots.emplace("normalTexture", 9000u);
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
            valid_material.value().id, "normalTexture", texture_ref.value().id);
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
    const quader::QuaderApiVoidResult project_unsupported =
        editor.project().save();
    EXPECT_FALSE(project_unsupported.ok());
    EXPECT_EQ(project_unsupported.revisions.project_revision,
              initial.project_revision);
    EXPECT_EQ(project_unsupported.revisions.scene_revision,
              initial.scene_revision);
    EXPECT_EQ(project_unsupported.revisions.asset_revision,
              initial.asset_revision);
    EXPECT_EQ(project_unsupported.revisions.modeling_content_revision,
              initial.modeling_content_revision);
    EXPECT_EQ(project_unsupported.revisions.modeling_selection_revision,
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
