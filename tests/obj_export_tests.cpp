////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "io/mesh/obj_writer.hpp"
#include "scene/components.hpp"

#include <gtest/gtest.h>
#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/**
 * Owns an isolated standalone editor for public OBJ export tests.
 */
struct ObjExportHarness {
  quader::QuaderStandaloneEditor standalone = quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
};

/**
 * Owns an internal session fixture for non-modeling mesh entity setup.
 */
struct InternalObjExportHarness {
  quader::SceneSession session;
  quader::QuaderEditorApi editor;

  InternalObjExportHarness()
      : session(),
        editor(quader::QuaderApiSessionInternalAccess::from_scene_session(
            session)) {}
};

std::filesystem::path obj_test_root(std::string_view name) {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("quader-obj-export-" + std::string(name));
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);
  return root;
}

std::string read_text_file(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
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

quader::MeshAsset valid_internal_triangle_mesh() {
  quader::MeshAsset mesh;
  quader::MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices = {
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
  geometry.indices = {0, 1, 2};
  return mesh;
}

quader::QuaderModelingObjectRef
create_selected_box(quader::QuaderEditorApi &editor, std::string name) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      editor.modeling().create_box(
          quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                               .max = {1.0f, 1.0f, 1.0f}},
          std::move(name));
  EXPECT_TRUE(created.ok()) << created.error_message();
  EXPECT_TRUE(editor.selection().select_object(created.value()).ok());
  return created.value();
}

quader::EntityId create_selected_plain_mesh_with_asset(
    quader::SceneSession &session, quader::QuaderEditorApi &editor,
    std::string name, quader::MeshAsset mesh) {
  mesh.name = name + " Mesh";
  const quader::AssetId mesh_id = session.assets().add_mesh(std::move(mesh));
  const quader::EntityId entity =
      session.scene().create_entity(std::move(name), quader::EntityKind::Mesh);
  session.scene().set_mesh(entity, quader::MeshComponent{.mesh = mesh_id});
  EXPECT_TRUE(editor.selection().select_objects({entity}, entity).ok());
  return entity;
}

quader::EntityId create_selected_plain_mesh(quader::SceneSession &session,
                                            quader::QuaderEditorApi &editor,
                                            std::string name) {
  return create_selected_plain_mesh_with_asset(
      session, editor, std::move(name), valid_internal_triangle_mesh());
}

void expect_same_revisions(const quader::QuaderRevisionStamp &actual,
                           const quader::QuaderRevisionStamp &expected) {
  EXPECT_EQ(actual.project_revision, expected.project_revision);
  EXPECT_EQ(actual.scene_revision, expected.scene_revision);
  EXPECT_EQ(actual.scene_layer_revision, expected.scene_layer_revision);
  EXPECT_EQ(actual.asset_revision, expected.asset_revision);
  EXPECT_EQ(actual.modeling_content_revision,
            expected.modeling_content_revision);
  EXPECT_EQ(actual.modeling_selection_revision,
            expected.modeling_selection_revision);
}

std::vector<float> obj_vertex_x_values(std::string_view obj_text) {
  std::vector<float> values;
  std::istringstream lines{std::string(obj_text)};
  std::string line;
  while (std::getline(lines, line)) {
    if (line.rfind("v ", 0) != 0) {
      continue;
    }
    std::istringstream vertex(line.substr(2));
    float x = 0.0f;
    vertex >> x;
    values.push_back(x);
  }
  return values;
}

std::vector<quader::QVec2> obj_texture_coordinates(std::string_view obj_text) {
  std::vector<quader::QVec2> values;
  std::istringstream lines{std::string(obj_text)};
  std::string line;
  while (std::getline(lines, line)) {
    if (line.rfind("vt ", 0) == 0) {
      std::istringstream coordinates(line.substr(3));
      quader::QVec2 uv;
      coordinates >> uv.x >> uv.y;
      values.push_back(uv);
    }
  }
  return values;
}

} // namespace

TEST(ObjExportTests, SinglePlainMeshSelectionWritesChosenObjPath) {
  const std::filesystem::path root = obj_test_root("single");
  const std::filesystem::path path = root / "chosen.obj";
  InternalObjExportHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  (void)create_selected_plain_mesh(harness.session, editor, "Plain Mesh");

  ASSERT_TRUE(editor.project().can_export_selected_meshes_as_obj());
  const std::optional<quader::QuaderCommandDescriptor> command =
      command_descriptor(editor,
                         quader::quader_command::kProjectExportSelectionObj);
  ASSERT_TRUE(command.has_value());
  EXPECT_TRUE(command->enabled);

  const bool dirty_before = editor.project().dirty();
  const bool can_undo_before = editor.history().can_undo();
  const quader::QuaderRevisionStamp revisions_before = editor.revisions();
  const quader::QuaderApiResult<quader::QuaderObjExportResult> exported =
      editor.project().export_selected_meshes_as_obj(path);
  ASSERT_TRUE(exported.ok()) << exported.error_message();
  EXPECT_FALSE(exported.changed());
  EXPECT_EQ(exported.mutation_kind, quader::QuaderMutationKind::None);
  ASSERT_EQ(exported.value().paths.size(), 1U);
  EXPECT_EQ(exported.value().paths.front(), path);
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(editor.project().dirty(), dirty_before);
  EXPECT_EQ(editor.history().can_undo(), can_undo_before);
  expect_same_revisions(editor.revisions(), revisions_before);

  const std::string obj = read_text_file(path);
  EXPECT_NE(obj.find("# Quader OBJ export\n"), std::string::npos);
  EXPECT_NE(obj.find("o Plain_Mesh\n"), std::string::npos);
  EXPECT_NE(obj.find("\nv 0"), std::string::npos);
  EXPECT_NE(obj.find("\nvt 0"), std::string::npos);
  EXPECT_NE(obj.find("\nvn 0"), std::string::npos);
  EXPECT_NE(obj.find("\nf 1/1/1 2/2/2 3/3/3\n"), std::string::npos);
}

TEST(ObjExportTests, MultipleMeshSelectionsWriteSuffixedSiblingObjs) {
  const std::filesystem::path root = obj_test_root("multiple");
  const std::filesystem::path chosen = root / "Base.obj";
  ObjExportHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef first =
      create_selected_box(editor, "Dup Mesh");
  const quader::QuaderModelingObjectRef second =
      create_selected_box(editor, "Dup Mesh");
  ASSERT_TRUE(editor.selection()
                  .select_objects({first.entity, second.entity}, second.entity)
                  .ok());

  const quader::QuaderApiResult<quader::QuaderObjExportResult> exported =
      editor.project().export_selected_meshes_as_obj(chosen);
  ASSERT_TRUE(exported.ok()) << exported.error_message();
  ASSERT_EQ(exported.value().paths.size(), 2U);
  EXPECT_EQ(exported.value().paths[0], root / "Base_Dup_Mesh.obj");
  EXPECT_EQ(exported.value().paths[1], root / "Base_Dup_Mesh_2.obj");
  EXPECT_TRUE(std::filesystem::exists(root / "Base_Dup_Mesh.obj"));
  EXPECT_TRUE(std::filesystem::exists(root / "Base_Dup_Mesh_2.obj"));
}

TEST(ObjExportTests,
     MultipleMeshExportPreservesEveryExistingFileWhenOneTargetIsInvalid) {
  const std::filesystem::path root = obj_test_root("multiple-atomic-failure");
  const std::filesystem::path chosen = root / "Atomic.obj";
  const std::filesystem::path first_path = root / "Atomic_First.obj";
  const std::filesystem::path second_path = root / "Atomic_Second.obj";
  ObjExportHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef first =
      create_selected_box(editor, "First");
  const quader::QuaderModelingObjectRef second =
      create_selected_box(editor, "Second");
  ASSERT_TRUE(editor.selection()
                  .select_objects({first.entity, second.entity}, second.entity)
                  .ok());

  {
    std::ofstream existing(first_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(existing.is_open());
    existing << "existing first export\n";
  }
  ASSERT_TRUE(std::filesystem::create_directory(second_path));

  const quader::QuaderApiResult<quader::QuaderObjExportResult> exported =
      editor.project().export_selected_meshes_as_obj(chosen);
  EXPECT_FALSE(exported.ok());
  EXPECT_EQ(read_text_file(first_path), "existing first export\n");
  EXPECT_TRUE(std::filesystem::is_directory(second_path));

  for (const std::filesystem::directory_entry &entry :
       std::filesystem::directory_iterator(root)) {
    EXPECT_EQ(entry.path().filename().string().find(".quader-obj-"),
              std::string::npos)
        << entry.path();
  }
}

TEST(ObjExportTests, ModelingMeshExportUsesLocalMeshSpace) {
  const std::filesystem::path root = obj_test_root("local-space");
  const std::filesystem::path path = root / "moved.obj";
  ObjExportHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  const quader::QuaderModelingObjectRef box =
      create_selected_box(editor, "Moved Box");
  quader::QuaderTransform transform;
  transform.position = {25.0f, -8.0f, 3.0f};
  transform.scale = {4.0f, 4.0f, 4.0f};
  ASSERT_TRUE(editor.scene().set_transform(box.entity, transform).ok());

  const quader::QuaderApiResult<quader::QuaderObjExportResult> exported =
      editor.project().export_selected_meshes_as_obj(path);
  ASSERT_TRUE(exported.ok()) << exported.error_message();
  const std::vector<float> x_values = obj_vertex_x_values(read_text_file(path));
  ASSERT_FALSE(x_values.empty());
  for (const float x : x_values) {
    EXPECT_LE(std::abs(x), 1.001f);
  }
}

TEST(ObjExportTests, ExportsEffectiveFaceAndPlacementUvsWithoutMutatingMesh) {
  const std::filesystem::path path =
      obj_test_root("effective-texture") / "textured.obj";
  InternalObjExportHarness harness;
  quader_poly::Document document;
  document.vertices = {
      {.id = 1u, .position = {0.0f, 0.0f, 0.0f}},
      {.id = 2u, .position = {1.0f, 0.0f, 0.0f}},
      {.id = 3u, .position = {0.0f, 1.0f, 0.0f}},
  };
  quader_poly::Face face;
  face.id = 1u;
  face.vertices = {1u, 2u, 3u};
  face.texture_transform = {
      .scale = {2.0f, -0.5f},
      .shift_uv = {0.25f, -0.125f},
      .rotation_degrees = 30.0f,
  };
  document.faces.push_back(face);
  document.next_vertex_id = 4u;
  document.next_face_id = 2u;
  quader::MeshAsset mesh =
      quader::MeshProjectionService::compile_polygon_document_mesh(document);
  mesh.replace_authored_polygon_document(document);
  const quader::EntityId entity = create_selected_plain_mesh_with_asset(
      harness.session, harness.editor, "Textured", std::move(mesh));
  quader::TextureMappingComponent mapping;
  mapping.sampling_from_local.values[0] = 2.0f;
  mapping.sampling_from_local.values[12] = 3.0f;
  ASSERT_TRUE(harness.session.scene().set_texture_mapping(entity, mapping));
  const quader::AssetId asset = harness.session.scene().mesh(entity)->mesh;
  const auto before = harness.session.assets().mesh(asset)->geometry_payload();
  const auto revisions = harness.editor.revisions();

  const auto result = harness.editor.project().export_selected_meshes_as_obj(path);
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto coordinates = obj_texture_coordinates(read_text_file(path));
  ASSERT_EQ(coordinates.size(), before->vertices.size());
  const auto neutral_mapping =
      quader::MeshProjectionService::texture_mapping_transform(
          mapping.sampling_from_local);
  for (std::size_t index = 0; index < coordinates.size(); ++index) {
    const quader::MeshVertex &vertex = before->vertices[index];
    const quader::QVec2 expected = quader_poly::evaluate_texture_mapping(
        vertex.uv, vertex.position, vertex.uv_geometric_normal,
        vertex.uv_carrier, neutral_mapping);
    EXPECT_NEAR(coordinates[index].x, expected.x, 0.00001f);
    EXPECT_NEAR(coordinates[index].y, expected.y, 0.00001f);
  }
  EXPECT_EQ(harness.session.assets().mesh(asset)->geometry_payload(), before);
  expect_same_revisions(harness.editor.revisions(), revisions);
  EXPECT_EQ(obj_vertex_x_values(read_text_file(path)),
            (std::vector<float>{0.0f, 1.0f, 0.0f}));
}

TEST(ObjExportTests, NonObjectMeshSelectionsDisableCommandAndDoNotWriteFiles) {
  const std::filesystem::path root = obj_test_root("disabled");
  ObjExportHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;

  auto expect_disabled_export = [&](std::string_view name) {
    const std::filesystem::path path = root / (std::string(name) + ".obj");
    EXPECT_FALSE(editor.project().can_export_selected_meshes_as_obj());
    const std::optional<quader::QuaderCommandDescriptor> command =
        command_descriptor(editor,
                           quader::quader_command::kProjectExportSelectionObj);
    ASSERT_TRUE(command.has_value());
    EXPECT_FALSE(command->enabled);
    const quader::QuaderApiResult<quader::QuaderObjExportResult> exported =
        editor.project().export_selected_meshes_as_obj(path);
    EXPECT_FALSE(exported.ok());
    EXPECT_FALSE(std::filesystem::exists(path));
  };

  expect_disabled_export("none");

  const quader::QuaderModelingObjectRef component_box =
      create_selected_box(editor, "Component Box");
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().select_face(component_box, 0).ok());
  expect_disabled_export("component");

  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  const quader::QuaderModelingObjectRef first =
      create_selected_box(editor, "Grouped A");
  const quader::QuaderModelingObjectRef second =
      create_selected_box(editor, "Grouped B");
  ASSERT_TRUE(editor.selection()
                  .select_objects({first.entity, second.entity}, second.entity)
                  .ok());
  const quader::QuaderApiResult<quader::QuaderEntityRef> group =
      editor.scene().group_selection("Group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Group)
                  .ok());
  expect_disabled_export("group");

  quader::QuaderCreatePointLightArgs light_args;
  light_args.name = "Only Light";
  light_args.select_created = true;
  const quader::QuaderApiResult<quader::QuaderEntityRef> light =
      editor.scene().create_point_light(light_args);
  ASSERT_TRUE(light.ok()) << light.error_message();
  expect_disabled_export("light");
}

TEST(ObjExportTests,
     RegistryRejectedMeshDisablesCommandAndDoesNotWriteFile) {
  const std::filesystem::path root = obj_test_root("invalid-selection");
  const std::filesystem::path path = root / "invalid.obj";
  InternalObjExportHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  quader::MeshAsset invalid = valid_internal_triangle_mesh();
  invalid.edit_geometry().indices = {0, 1};
  const std::uint64_t asset_revision_before =
      harness.session.assets().revision();
  const quader::EntityId entity = create_selected_plain_mesh_with_asset(
      harness.session, editor, "Invalid Mesh", std::move(invalid));
  EXPECT_EQ(harness.session.assets().revision(), asset_revision_before);
  ASSERT_NE(harness.session.scene().mesh(entity), nullptr);
  EXPECT_EQ(harness.session.scene().mesh(entity)->mesh,
            quader::kInvalidAssetId);

  EXPECT_FALSE(editor.project().can_export_selected_meshes_as_obj());
  const std::optional<quader::QuaderCommandDescriptor> command =
      command_descriptor(editor,
                         quader::quader_command::kProjectExportSelectionObj);
  ASSERT_TRUE(command.has_value());
  EXPECT_FALSE(command->enabled);

  const quader::QuaderApiResult<quader::QuaderObjExportResult> exported =
      editor.project().export_selected_meshes_as_obj(path);
  EXPECT_FALSE(exported.ok());
  EXPECT_NE(exported.error_message().find("object-mode mesh selection"),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(ObjExportTests, EmptyPathCommandUsesObjRuntimeHookAndHandlesCancel) {
  const std::filesystem::path root = obj_test_root("hooks");
  const std::filesystem::path path = root / "hooked.obj";
  ObjExportHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  (void)create_selected_box(editor, "Hooked Box");

  int requests = 0;
  editor.project().set_runtime_hooks(quader::QuaderProjectRuntimeHooks{
      .choose_export_obj_path = [&]() -> std::optional<std::filesystem::path> {
        ++requests;
        return path;
      },
  });
  const quader::QuaderApiVoidResult command_export =
      editor.commands().execute(quader::quader_command::kProjectExportSelectionObj);
  ASSERT_TRUE(command_export.ok()) << command_export.error_message();
  EXPECT_FALSE(command_export.changed());
  EXPECT_EQ(requests, 1);
  EXPECT_TRUE(std::filesystem::exists(path));

  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  editor.project().set_runtime_hooks(quader::QuaderProjectRuntimeHooks{
      .choose_export_obj_path = [&]() -> std::optional<std::filesystem::path> {
        ++requests;
        return std::nullopt;
      },
  });
  const quader::QuaderApiVoidResult canceled =
      editor.commands().execute(quader::quader_command::kProjectExportSelectionObj);
  EXPECT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_FALSE(canceled.changed());
  EXPECT_EQ(canceled.mutation_kind, quader::QuaderMutationKind::None);
  EXPECT_EQ(requests, 2);
  EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(ObjExportTests, ObjWriterRejectsMalformedMeshData) {
  const std::filesystem::path root = obj_test_root("writer-validation");

  quader::MeshAsset empty;
  const std::filesystem::path empty_path = root / "empty.obj";
  const quader::ObjMeshWriteResult empty_result =
      quader::ObjMeshWriter::write_mesh(empty_path, empty, "Empty");
  EXPECT_FALSE(empty_result.ok);
  EXPECT_NE(empty_result.message.find("no vertices"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(empty_path));

  quader::MeshAsset non_triangle = valid_internal_triangle_mesh();
  non_triangle.edit_geometry().indices = {0, 1};
  const std::filesystem::path non_triangle_path = root / "non-triangle.obj";
  const quader::ObjMeshWriteResult non_triangle_result =
      quader::ObjMeshWriter::write_mesh(non_triangle_path, non_triangle,
                                        "Non Triangle");
  EXPECT_FALSE(non_triangle_result.ok);
  EXPECT_NE(non_triangle_result.message.find("triangle-aligned"),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(non_triangle_path));

  quader::MeshAsset out_of_bounds = valid_internal_triangle_mesh();
  out_of_bounds.edit_geometry().indices = {0, 1, 3};
  const std::filesystem::path out_of_bounds_path =
      root / "out-of-bounds.obj";
  const quader::ObjMeshWriteResult out_of_bounds_result =
      quader::ObjMeshWriter::write_mesh(out_of_bounds_path, out_of_bounds,
                                        "Out Of Bounds");
  EXPECT_FALSE(out_of_bounds_result.ok);
  EXPECT_NE(out_of_bounds_result.message.find("out of bounds"),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(out_of_bounds_path));

  quader::MeshAsset non_finite = valid_internal_triangle_mesh();
  non_finite.edit_geometry().vertices.front().position.x =
      std::numeric_limits<float>::quiet_NaN();
  const std::filesystem::path non_finite_path = root / "non-finite.obj";
  const quader::ObjMeshWriteResult non_finite_result =
      quader::ObjMeshWriter::write_mesh(non_finite_path, non_finite,
                                        "Non Finite");
  EXPECT_FALSE(non_finite_result.ok);
  EXPECT_NE(non_finite_result.message.find("not finite"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(non_finite_path));
}
