////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "assets/asset_registry.hpp"
#include "assets/mesh_geometry_projection.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/tool_session_defaults.hpp"
#include "editor/tools/uv_peel_tool_service.hpp"
#include "render/scene_render_bridge.hpp"
#include "scene/scene_document.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

/// Keeps public workflow calls attached to an inspectable native scene owner.
struct UvPeelHarness final {
  explicit UvPeelHarness(std::shared_ptr<ToolSessionDefaults> defaults = {})
      : session(nullptr, nullptr, std::move(defaults)) {}
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
};

/// Cleans up the isolated project used for the resident-scene transition test.
class ScopedPeelProjectDirectory final {
public:
  ScopedPeelProjectDirectory() {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("quader-uv-peel-tool-" + std::to_string(tick));
    std::error_code error;
    std::filesystem::create_directories(path_, error);
    EXPECT_FALSE(error) << error.message();
  }

  ~ScopedPeelProjectDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

QuaderPolygonDocument peel_strip_document() {
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {-2.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(document, {0.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(document, {2.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(document, {-2.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(document, {0.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(document, {2.0f, 1.0f, 0.0f}),
  };
  (void)quader_poly::add_face(
      document, std::array{vertices[0], vertices[1], vertices[4], vertices[3]});
  (void)quader_poly::add_face(
      document, std::array{vertices[1], vertices[2], vertices[5], vertices[4]});
  return QuaderPolygonDocumentInternalAccess::from_backend(std::move(document));
}

QuaderModelingObjectRef create_strip(QuaderEditorApi &editor,
                                    std::string name = "Peel strip") {
  const auto created = editor.modeling().create_mesh(
      peel_strip_document(), {.name = std::move(name)});
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.ok() ? created.value() : QuaderModelingObjectRef{};
}

QuaderModelingObjectRef create_circular_strip(QuaderEditorApi &editor,
                                             std::string name) {
  quader_poly::Document document;
  std::array<quader_poly::ElementId, 9U> vertices{};
  for (std::size_t station = 0U; station < 3U; ++station) {
    const double angle = 0.2 * double(station);
    for (std::size_t lane = 0U; lane < 3U; ++lane) {
      const double radius = 1.0 + double(lane);
      vertices[station*3U+lane] = quader_poly::add_vertex(document,
          {float(radius*std::cos(angle)), 0.0F, float(radius*std::sin(angle))});
    }
  }
  for (std::size_t station = 0U; station < 2U; ++station) {
    for (std::size_t lane = 0U; lane < 2U; ++lane) {
      const std::size_t first = station*3U+lane;
      (void)quader_poly::add_face(document,
          std::array{vertices[first], vertices[first+3U],
                     vertices[first+4U], vertices[first+1U]});
      document.faces.back().uvs = {{0.0F,0.0F}, {1.0F,0.0F},
                                   {1.0F,1.0F}, {0.0F,1.0F}};
    }
  }
  const auto created = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
      {.name = std::move(name)});
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.ok() ? created.value() : QuaderModelingObjectRef{};
}

quader_poly::Document peel_authored_document(UvPeelHarness &harness,
                                            QuaderModelingObjectRef object) {
  const auto snapshot = harness.session.modeling().polygon_document_read_snapshot(
      object.modeling_object_id, true);
  EXPECT_TRUE(snapshot.ok()) << snapshot.error_message();
  if (!snapshot.ok()) return {};
  const auto view = snapshot.value().document.view();
  quader_poly::Document document;
  document.vertices.assign(view.vertices.begin(), view.vertices.end());
  document.faces.assign(view.faces.begin(), view.faces.end());
  document.hard_normal_edges.assign(view.hard_normal_edges.begin(), view.hard_normal_edges.end());
  document.soft_normal_edges.assign(view.soft_normal_edges.begin(), view.soft_normal_edges.end());
  document.next_vertex_id = view.next_vertex_id;
  document.next_face_id = view.next_face_id;
  return document;
}

void expect_peel_authored_equal(const quader_poly::Document &actual,
                                const quader_poly::Document &expected) {
  EXPECT_TRUE(quader_poly::has_same_geometry(actual, expected));
  ASSERT_EQ(actual.faces.size(), expected.faces.size());
  for (std::size_t index = 0U; index < actual.faces.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &face = actual.faces[index];
    const auto &reference = expected.faces[index];
    EXPECT_EQ(face.id, reference.id);
    EXPECT_EQ(face.vertices, reference.vertices);
    EXPECT_EQ(face.material_slot, reference.material_slot);
    EXPECT_EQ(face.texture_transform, reference.texture_transform);
    EXPECT_EQ(face.circular_uv_interpolation, reference.circular_uv_interpolation);
    ASSERT_EQ(face.uvs.size(), reference.uvs.size());
    for (std::size_t corner = 0U; corner < face.uvs.size(); ++corner) {
      EXPECT_FLOAT_EQ(face.uvs[corner].x, reference.uvs[corner].x);
      EXPECT_FLOAT_EQ(face.uvs[corner].y, reference.uvs[corner].y);
    }
  }
}

void expect_circular_faces(const quader_poly::Document &document, bool expected) {
  ASSERT_EQ(document.faces.size(), 4U);
  for (const auto &face : document.faces) {
    EXPECT_EQ(face.circular_uv_interpolation.has_value(), expected) << face.id;
  }
}

void select_circular_guides(QuaderEditorApi &editor,
                            QuaderModelingObjectRef object) {
  ASSERT_TRUE(editor.selection().select_components({
      .object = object, .kind = QuaderComponentKind::Edge,
      .edges = {{2,5}, {5,8}},
      .active = {.object = object, .kind = QuaderComponentKind::Edge,
                 .edge_a = 2, .edge_b = 5},
  }).ok());
}

void enter_peel_edit(QuaderEditorApi &editor,
                     std::initializer_list<QuaderModelingObjectRef> objects) {
  ASSERT_TRUE(editor.selection().select_objects(objects).ok());
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Edge).ok());
}

void refresh(UvPeelHarness &harness) {
  const auto refreshed = harness.session.uv_peel().refresh();
  ASSERT_TRUE(refreshed.ok) << refreshed.message;
}

void expect_history_equal(const QuaderHistorySnapshot &actual,
                          const QuaderHistorySnapshot &expected) {
  EXPECT_EQ(actual.can_undo, expected.can_undo);
  EXPECT_EQ(actual.can_redo, expected.can_redo);
  EXPECT_EQ(actual.undo_label, expected.undo_label);
  EXPECT_EQ(actual.redo_label, expected.redo_label);
}

void expect_mesh_equal(const std::optional<QuaderMeshAsset> &mesh,
                       const QuaderMeshAsset &expected) {
  ASSERT_TRUE(mesh.has_value());
  EXPECT_EQ(mesh->indices, expected.indices);
  ASSERT_EQ(mesh->primitives.size(), expected.primitives.size());
  for (std::size_t index = 0; index < mesh->primitives.size(); ++index) {
    EXPECT_EQ(mesh->primitives[index].index_offset,
              expected.primitives[index].index_offset);
    EXPECT_EQ(mesh->primitives[index].index_count,
              expected.primitives[index].index_count);
    EXPECT_EQ(mesh->primitives[index].material_slot,
              expected.primitives[index].material_slot);
  }
  ASSERT_EQ(mesh->vertices.size(), expected.vertices.size());
  for (std::size_t index = 0; index < mesh->vertices.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &actual = mesh->vertices[index];
    const auto &reference = expected.vertices[index];
    EXPECT_EQ(actual.position.x, reference.position.x);
    EXPECT_EQ(actual.position.y, reference.position.y);
    EXPECT_EQ(actual.position.z, reference.position.z);
    EXPECT_EQ(actual.normal.x, reference.normal.x);
    EXPECT_EQ(actual.normal.y, reference.normal.y);
    EXPECT_EQ(actual.normal.z, reference.normal.z);
    EXPECT_EQ(actual.uv.x, reference.uv.x);
    EXPECT_EQ(actual.uv.y, reference.uv.y);
    EXPECT_EQ(actual.uv_carrier, reference.uv_carrier);
    EXPECT_EQ(actual.uv_geometric_normal.x, reference.uv_geometric_normal.x);
    EXPECT_EQ(actual.uv_geometric_normal.y, reference.uv_geometric_normal.y);
    EXPECT_EQ(actual.uv_geometric_normal.z, reference.uv_geometric_normal.z);
    EXPECT_EQ(actual.rgba, reference.rgba);
  }
  ASSERT_EQ(mesh->face_textures.size(), expected.face_textures.size());
  for (std::size_t index = 0; index < mesh->face_textures.size(); ++index) {
    const auto &actual = mesh->face_textures[index];
    const auto &reference = expected.face_textures[index];
    EXPECT_EQ(actual.face, reference.face);
    EXPECT_EQ(actual.scale.x, reference.scale.x);
    EXPECT_EQ(actual.scale.y, reference.scale.y);
    EXPECT_EQ(actual.shift_uv.x, reference.shift_uv.x);
    EXPECT_EQ(actual.shift_uv.y, reference.shift_uv.y);
    EXPECT_EQ(actual.rotation_degrees, reference.rotation_degrees);
  }
}

void expect_uv_bounds(const std::optional<QuaderMeshAsset> &mesh,
                      float minimum_u, float maximum_u, float minimum_v,
                      float maximum_v) {
  ASSERT_TRUE(mesh.has_value());
  ASSERT_FALSE(mesh->vertices.empty());
  QVec2 minimum = mesh->vertices.front().uv;
  QVec2 maximum = minimum;
  for (const auto &vertex : mesh->vertices) {
    minimum.x = std::min(minimum.x, vertex.uv.x);
    minimum.y = std::min(minimum.y, vertex.uv.y);
    maximum.x = std::max(maximum.x, vertex.uv.x);
    maximum.y = std::max(maximum.y, vertex.uv.y);
  }
  EXPECT_NEAR(minimum.x, minimum_u, 0.00001f);
  EXPECT_NEAR(maximum.x, maximum_u, 0.00001f);
  EXPECT_NEAR(minimum.y, minimum_v, 0.00001f);
  EXPECT_NEAR(maximum.y, maximum_v, 0.00001f);
}

void set_options(QuaderEditorApi &editor, const QuaderUvPeelOptions &options) {
  auto &tools = editor.tools();
  ASSERT_TRUE(tools.set_setting("uv_peel", "uv_peel.axis",
                                options.axis == QuaderUvPeelAxis::U ? "u" : "v")
                  .ok());
  ASSERT_TRUE(tools.set_setting("uv_peel", "uv_peel.world_space",
                                options.world_space).ok());
  ASSERT_TRUE(tools.set_setting("uv_peel", "uv_peel.smooth_circular_flow",
                                options.smooth_circular_flow).ok());
  const std::array values{
      std::pair{"uv_peel.u_repeat", options.u_repeat},
      std::pair{"uv_peel.v_repeat", options.v_repeat},
      std::pair{"uv_peel.u_offset", options.u_offset},
      std::pair{"uv_peel.v_offset", options.v_offset},
  };
  for (const auto &[id, value] : values) {
    const auto result = tools.set_setting("uv_peel", id, value);
    ASSERT_TRUE(result.ok()) << id << ": " << result.error_message();
  }
}

void expect_direct_peel_parity(
    const std::optional<QuaderMeshAsset> &preview,
    const QuaderUvPeelOptions &options,
    std::vector<std::pair<QuaderElementId, QuaderElementId>> edges = {{2, 5}},
    std::pair<QuaderElementId, QuaderElementId> active = {2, 5}) {
  UvPeelHarness reference;
  const auto strip = create_strip(reference.editor);
  enter_peel_edit(reference.editor, {strip});
  ASSERT_TRUE(reference.editor.selection().select_components({
      .object = strip,
      .kind = QuaderComponentKind::Edge,
      .edges = std::move(edges),
      .active = {.object = strip, .kind = QuaderComponentKind::Edge,
                 .edge_a = active.first, .edge_b = active.second},
  }).ok());
  const auto peeled = reference.editor.modeling().peel_uv({
      .options = options,
      .selection_token = reference.editor.modeling().uv_peel_settings().selection_token,
  });
  ASSERT_TRUE(peeled.ok()) << peeled.error_message();
  const auto expected = reference.editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(expected.has_value());
  expect_mesh_equal(preview, *expected);
}

TEST(UvPeelToolServiceTests, EmptyComponentsActivateOnlyWithinExplicitEditMode) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  EXPECT_FALSE(editor.tools().can_activate("uv_peel"));
  EXPECT_FALSE(editor.tools().activate("uv_peel").ok());
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Object);
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  EXPECT_TRUE(editor.tools().can_activate("uv_peel"));
  const auto before = editor.history().snapshot();
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Edge);
  const auto settings = editor.tools().active_settings();
  EXPECT_TRUE(settings.active);
  EXPECT_EQ(settings.tool_id, "uv_peel");
  EXPECT_TRUE(settings.apply_visible);
  EXPECT_FALSE(settings.can_apply);
  EXPECT_TRUE(settings.can_cancel);
  EXPECT_TRUE(settings.can_reset);
  EXPECT_FALSE(settings.preview_active);
  EXPECT_FALSE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Edge);
  expect_history_equal(editor.history().snapshot(), before);
}

TEST(UvPeelToolServiceTests,
     PreselectedGuidesPreviewImmediatelyAndCancelPreservesGuidesAndSettings) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  const auto original = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(original.has_value());
  harness.session.history().clear();
  const QuaderUvPeelOptions options{
      .world_space = false, .u_repeat = 2.0, .v_repeat = 3.0,
      .u_offset = 0.25, .v_offset = -0.5,
  };
  ASSERT_TRUE(editor.modeling().set_uv_peel_options(options).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  expect_uv_bounds(editor.query().compiled_object_mesh(strip),
                   0.25f, 2.25f, -2.0f, 1.0f);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Edge);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 1u);
  EXPECT_EQ(editor.selection().summary().active_edge_a, 2);
  EXPECT_EQ(editor.selection().summary().active_edge_b, 5);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, options);
  EXPECT_FALSE(editor.history().can_undo());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  expect_uv_bounds(editor.query().compiled_object_mesh(strip),
                   0.25f, 2.25f, -2.0f, 1.0f);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(UvPeelToolServiceTests,
     GuideAddRemoveAndActiveChangesRebuildAgainstOriginalUvs) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  const auto original = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(original.has_value());
  ASSERT_TRUE(editor.selection().clear().ok());
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  const QuaderUvPeelOptions options{.world_space = false, .u_offset = 0.25};
  set_options(editor, options);
  const auto undo_count = harness.session.history().undo_count();
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  refresh(harness);
  EXPECT_TRUE(harness.session.uv_peel().active());
  EXPECT_EQ(harness.session.history().undo_count(), undo_count);
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  expect_direct_peel_parity(editor.query().compiled_object_mesh(strip), options);

  ASSERT_TRUE(editor.selection().select_edge(
      strip, 1, 4, QuaderSelectionEdit::Add).ok());
  refresh(harness);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 2u);
  expect_direct_peel_parity(editor.query().compiled_object_mesh(strip), options,
                           {{1, 4}, {2, 5}}, {1, 4});

  ASSERT_TRUE(editor.selection().select_components({
      .object = strip,
      .kind = QuaderComponentKind::Edge,
      .edges = {{1, 4}, {2, 5}},
      .active = {.object = strip, .kind = QuaderComponentKind::Edge,
                 .edge_a = 2, .edge_b = 5},
  }).ok());
  refresh(harness);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 2u);
  expect_direct_peel_parity(editor.query().compiled_object_mesh(strip), options,
                           {{1, 4}, {2, 5}}, {2, 5});

  ASSERT_TRUE(editor.selection().select_edge(
      strip, 1, 4, QuaderSelectionEdit::Remove).ok());
  refresh(harness);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 1u);
  expect_direct_peel_parity(editor.query().compiled_object_mesh(strip), options);
  ASSERT_TRUE(editor.selection().clear().ok());
  refresh(harness);
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  EXPECT_FALSE(editor.tools().active_settings().can_apply);
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  refresh(harness);
  EXPECT_TRUE(editor.tools().active_settings().can_apply);
  expect_direct_peel_parity(editor.query().compiled_object_mesh(strip), options);
  EXPECT_TRUE(harness.session.uv_peel().active());
  EXPECT_EQ(harness.session.history().undo_count(), undo_count);
}

TEST(UvPeelToolServiceTests, EverySettingAndResetRecalculateWithoutAccumulation) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  auto options = QuaderUvPeelOptions{};
  const auto verify = [&] {
    refresh(harness);
    EXPECT_EQ(editor.modeling().uv_peel_settings().options, options);
    expect_direct_peel_parity(editor.query().compiled_object_mesh(strip), options);
  };
  options.axis = QuaderUvPeelAxis::V;
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.axis", "v").ok());
  verify();
  options.world_space = false;
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.world_space", false).ok());
  verify();
  options.smooth_circular_flow = true;
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.smooth_circular_flow", true).ok());
  verify();
  options.u_repeat = 2.0;
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.u_repeat", 2.0).ok());
  verify();
  options.v_repeat = 3.0;
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.v_repeat", 3.0).ok());
  verify();
  options.u_offset = 0.25;
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.u_offset", 0.25).ok());
  verify();
  options.v_offset = -0.5;
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.v_offset", -0.5).ok());
  verify();
  options = {};
  ASSERT_TRUE(editor.tools().reset_active_settings().ok());
  verify();
}

TEST(UvPeelToolServiceTests,
     CircularFlowToggleRecomputesFromSourceAndSupportsCancelApplyUndoRedo) {
  const auto defaults = std::make_shared<ToolSessionDefaults>();
  UvPeelHarness harness(defaults);
  auto &editor = harness.editor;
  EXPECT_FALSE(editor.modeling().uv_peel_settings().options.smooth_circular_flow);
  const auto strip = create_circular_strip(editor, "Circular flow lifecycle");
  enter_peel_edit(editor, {strip});
  select_circular_guides(editor, strip);
  const auto original = peel_authored_document(harness, strip);
  harness.session.history().clear();
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  const auto ordinary = peel_authored_document(harness, strip);
  expect_circular_faces(ordinary, false);
  ASSERT_TRUE(editor.tools().active_settings().can_apply);

  const auto toggle = editor.tools().set_setting(
      "uv_peel", "uv_peel.smooth_circular_flow", true);
  ASSERT_TRUE(toggle.ok()) << toggle.error_message();
  const auto smooth = peel_authored_document(harness, strip);
  expect_circular_faces(smooth, true);
  EXPECT_TRUE(quader_poly::has_same_geometry(smooth, original));
  EXPECT_EQ(harness.session.history().undo_count(), 0U);
  const auto revision = editor.tools().active_settings_revision();
  const auto content_revision = editor.revisions().modeling_content_revision;
  const auto unchanged = editor.tools().set_setting(
      "uv_peel", "uv_peel.smooth_circular_flow", true);
  ASSERT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(editor.tools().active_settings_revision(), revision);
  EXPECT_EQ(editor.revisions().modeling_content_revision, content_revision);
  for (double invalid : {-1.0, 0.5, 2.0}) {
    EXPECT_FALSE(editor.tools().set_setting(
        "uv_peel", "uv_peel.smooth_circular_flow", invalid).ok());
  }
  expect_peel_authored_equal(peel_authored_document(harness, strip), smooth);
  ASSERT_TRUE(editor.tools().set_setting(
      "uv_peel", "uv_peel.smooth_circular_flow", false).ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), ordinary);
  ASSERT_TRUE(editor.tools().set_setting(
      "uv_peel", "uv_peel.smooth_circular_flow", true).ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), smooth);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), original);
  EXPECT_FALSE(editor.history().can_undo());
  EXPECT_TRUE(editor.modeling().uv_peel_settings().options.smooth_circular_flow);
  UvPeelHarness borrowed(defaults);
  EXPECT_TRUE(borrowed.editor.modeling().uv_peel_settings().options.smooth_circular_flow);

  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), smooth);
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_EQ(editor.history().snapshot().undo_label, "UV Peel");
  EXPECT_EQ(harness.session.history().undo_count(), 1U);
  expect_peel_authored_equal(peel_authored_document(harness, strip), smooth);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), original);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), smooth);
  EXPECT_TRUE(editor.modeling().uv_peel_settings().options.smooth_circular_flow);

  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "uv_peel", "uv_peel.smooth_circular_flow", false).ok());
  const auto cleared = peel_authored_document(harness, strip);
  expect_circular_faces(cleared, false);
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  ASSERT_TRUE(editor.history().undo().ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), smooth);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), cleared);
  EXPECT_FALSE(editor.modeling().uv_peel_settings().options.smooth_circular_flow);
}

TEST(UvPeelToolServiceTests, CircularFlowRepeatKeepsCapturedOptionAndAuthoredMapping) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto first = create_circular_strip(editor, "Circular flow source");
  const auto second = create_circular_strip(editor, "Circular flow repeat");
  enter_peel_edit(editor, {first, second});
  select_circular_guides(editor, first);
  const QuaderUvPeelOptions captured{
      .world_space = false, .u_repeat = 1.5, .v_repeat = 2.0,
      .u_offset = 0.25, .v_offset = -0.5, .smooth_circular_flow = true};
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  set_options(editor, captured);
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  const auto applied = peel_authored_document(harness, first);
  expect_circular_faces(applied, true);
  const QuaderUvPeelOptions displayed{.u_repeat = 7.0, .v_offset = 4.0};
  ASSERT_TRUE(editor.modeling().set_uv_peel_options(displayed).ok());
  select_circular_guides(editor, second);
  const auto original = peel_authored_document(harness, second);
  const auto repeated = editor.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_TRUE(repeated.changed());
  expect_peel_authored_equal(peel_authored_document(harness, second), applied);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, displayed);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_peel_authored_equal(peel_authored_document(harness, second), original);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_peel_authored_equal(peel_authored_document(harness, second), applied);
}

TEST(UvPeelToolServiceTests,
     CircularFlowComponentTransformPublishesReconciledPreviewAndHistory) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_circular_strip(editor, "Circular component transform");
  enter_peel_edit(editor, {strip});
  select_circular_guides(editor, strip);
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "uv_peel", "uv_peel.smooth_circular_flow", true).ok());
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  const auto original = peel_authored_document(harness, strip);
  expect_circular_faces(original, true);
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.selection().select_vertex(strip, 1).ok());
  harness.session.history().clear();
  SceneRenderBridge bridge;
  std::vector<RenderToolPreviewMeshRecord> published;
  const QuaderRenderApiRuntimeAccess::ToolPreviewScenePublisher publisher =
      [&](RenderToolPreviewSceneSnapshot records) {
        published = std::move(records.meshes);
        return ToolPreviewSubmissionResult::Published;
      };
  const auto synchronize = [&] {
    const auto result = QuaderRenderApiRuntimeAccess::sync_tool_previews(
        editor.render(), bridge, {}, publisher);
    ASSERT_TRUE(result.ok()) << result.error_message();
  };
  const auto expect_transformed = [&](const quader_poly::Document &document,
                                       const MeshGeometryPayload &geometry) {
    ASSERT_EQ(document.vertices.size(), original.vertices.size());
    ASSERT_EQ(document.faces.size(), original.faces.size());
    ASSERT_EQ(geometry.primitives.size(), original.faces.size());
    for (std::size_t index = 0U; index < document.vertices.size(); ++index) {
      const auto &vertex = document.vertices[index];
      const auto &before = original.vertices[index];
      EXPECT_EQ(vertex.id, before.id);
      EXPECT_FLOAT_EQ(vertex.position.x, before.position.x+(vertex.id == 1U ? 0.125F : 0.0F));
      EXPECT_FLOAT_EQ(vertex.position.y, before.position.y);
      EXPECT_FLOAT_EQ(vertex.position.z, before.position.z);
    }
    for (std::size_t index = 0U; index < document.faces.size(); ++index) {
      const auto &face = document.faces[index];
      const auto &before = original.faces[index];
      const bool touched = std::ranges::find(face.vertices, 1U) != face.vertices.end();
      EXPECT_EQ(face.vertices, before.vertices);
      EXPECT_EQ(face.circular_uv_interpolation.has_value(), !touched);
      ASSERT_EQ(face.uvs.size(), before.uvs.size());
      for (std::size_t corner = 0U; corner < face.uvs.size(); ++corner) {
        EXPECT_TRUE(std::isfinite(face.uvs[corner].x));
        EXPECT_TRUE(std::isfinite(face.uvs[corner].y));
        if (!touched) {
          EXPECT_FLOAT_EQ(face.uvs[corner].x, before.uvs[corner].x);
          EXPECT_FLOAT_EQ(face.uvs[corner].y, before.uvs[corner].y);
        }
      }
      if (!touched) EXPECT_EQ(face.circular_uv_interpolation, before.circular_uv_interpolation);
      const auto &primitive = geometry.primitives[index];
      ASSERT_LE(std::size_t(primitive.index_offset)+primitive.index_count, geometry.indices.size());
      for (std::size_t corner = primitive.index_offset;
           corner < std::size_t(primitive.index_offset)+primitive.index_count; ++corner) {
        ASSERT_LT(geometry.indices[corner], geometry.vertices.size());
        const auto &vertex = geometry.vertices[geometry.indices[corner]];
        EXPECT_TRUE(std::isfinite(vertex.uv.x));
        EXPECT_TRUE(std::isfinite(vertex.uv.y));
        if (touched) {
          EXPECT_EQ(vertex.circular_uv_index, 0U);
        } else {
          ASSERT_GT(vertex.circular_uv_index, 0U);
          ASSERT_LE(vertex.circular_uv_index, geometry.circular_uv_interpolations.size());
          EXPECT_EQ(geometry.circular_uv_interpolations[vertex.circular_uv_index-1U],
                    *before.circular_uv_interpolation);
        }
      }
    }
  };
  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor.modeling().update_transform_translate({0.125F,0.0F,0.0F}).ok());
  ASSERT_NO_FATAL_FAILURE(synchronize());
  ASSERT_EQ(published.size(), 1U);
  EXPECT_EQ(published.front().source_entity, strip.entity);
  MeshAsset preview = published.front().mesh;
  if (published.front().geometry_projection) {
    preview.share_geometry(published.front().geometry_projection->materialized_geometry());
  }
  ASSERT_TRUE(preview.authored_polygon_document);
  const quader_poly::Document expected = *preview.authored_polygon_document;
  ASSERT_NO_FATAL_FAILURE(expect_transformed(expected, preview.geometry()));
  EXPECT_EQ(harness.session.history().undo_count(), 0U);
  ASSERT_TRUE(editor.modeling().cancel_transform_preview().ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), original);
  ASSERT_NO_FATAL_FAILURE(synchronize());
  EXPECT_TRUE(published.empty());
  EXPECT_FALSE(editor.history().can_undo());

  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor.modeling().update_transform_translate({0.125F,0.0F,0.0F}).ok());
  ASSERT_TRUE(editor.modeling().commit_transform_preview("Move circular component").ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), expected);
  const auto accepted_mesh = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(accepted_mesh.has_value());
  const auto *accepted = harness.session.assets().mesh(accepted_mesh->id);
  ASSERT_NE(accepted, nullptr);
  ASSERT_NO_FATAL_FAILURE(expect_transformed(peel_authored_document(harness, strip), accepted->geometry()));
  EXPECT_EQ(harness.session.history().undo_count(), 1U);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), original);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_peel_authored_equal(peel_authored_document(harness, strip), expected);
}

TEST(UvPeelToolServiceTests, EmptySpacePickClearsGuidesAndRestoresOriginalUvs) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  const auto original = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(original.has_value());
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  const auto missed = editor.selection().pick_select({
      .ray = {.origin = {20.0f, 20.0f, 10.0f},
              .direction = {0.0f, 0.0f, -1.0f}},
  });
  ASSERT_TRUE(missed.ok()) << missed.error_message();
  refresh(harness);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 0u);
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  EXPECT_FALSE(editor.tools().active_settings().can_apply);
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
}

TEST(UvPeelToolServiceTests, RepeatedMarqueeToggleUsesTheCapturedDragBaseline) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 1, 4).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  const auto baseline = editor.selection().capture_box_selection_baseline();
  ASSERT_TRUE(baseline.valid());
  const QuaderBoxSelectRequest request{
      .camera = {.orthographic = true,
                 .origin = {0.0f, 0.0f, 10.0f},
                 .viewport_size = {100.0f, 100.0f},
                 .orthographic_size = 100.0},
      .start = {49.5f, 48.5f},
      .end = {50.5f, 51.5f},
      .edit = QuaderSelectionEdit::Toggle,
      .rule = QuaderBoxSelectionRule::Contain,
  };
  const auto first = editor.selection().box_select(request, baseline);
  ASSERT_TRUE(first.ok()) << first.error_message();
  refresh(harness);
  ASSERT_EQ(editor.selection().summary().selected_edge_count, 2u);
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  const auto first_preview = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(first_preview.has_value());
  const auto second = editor.selection().box_select(request, baseline);
  ASSERT_TRUE(second.ok()) << second.error_message();
  refresh(harness);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 2u);
  EXPECT_TRUE(editor.tools().active_settings().can_apply);
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *first_preview);
}

TEST(UvPeelToolServiceTests, InvalidSettingsLeaveTheLastAcceptedPreviewUntouched) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  const QuaderUvPeelOptions options{.world_space = false, .u_offset = 0.25};
  set_options(editor, options);
  refresh(harness);
  const auto accepted = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(accepted.has_value());
  const auto history = editor.history().snapshot();
  const std::array invalid{
      std::pair{"uv_peel.u_repeat", 0.0},
      std::pair{"uv_peel.v_repeat", -1.0},
      std::pair{"uv_peel.u_repeat", std::numeric_limits<double>::infinity()},
      std::pair{"uv_peel.u_offset", std::numeric_limits<double>::quiet_NaN()},
      std::pair{"uv_peel.v_offset", std::numeric_limits<double>::infinity()},
      std::pair{"uv_peel.axis", 99.0},
  };
  for (const auto &[id, value] : invalid) {
    SCOPED_TRACE(id);
    const auto rejected = editor.tools().set_setting("uv_peel", id, value);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_EQ(editor.modeling().uv_peel_settings().options, options);
    expect_mesh_equal(editor.query().compiled_object_mesh(strip), *accepted);
    expect_history_equal(editor.history().snapshot(), history);
    EXPECT_TRUE(editor.tools().active_settings().can_apply);
  }
}

TEST(UvPeelToolServiceTests, MultipleObjectsApplyAsOneExactUndoRedoEntry) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto first = create_strip(editor, "First");
  const auto second = create_strip(editor, "Second");
  enter_peel_edit(editor, {first, second});
  ASSERT_TRUE(editor.selection().select_edge(first, 2, 5).ok());
  ASSERT_TRUE(editor.selection().select_edge(second, 2, 5, QuaderSelectionEdit::Add).ok());
  const auto original_first = editor.query().compiled_object_mesh(first);
  const auto original_second = editor.query().compiled_object_mesh(second);
  ASSERT_TRUE(original_first.has_value());
  ASSERT_TRUE(original_second.has_value());
  harness.session.history().clear();
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  const QuaderUvPeelOptions options{
      .world_space = false, .u_repeat = 2.0, .v_repeat = 3.0,
      .u_offset = 0.25, .v_offset = -0.5,
  };
  set_options(editor, options);
  refresh(harness);
  const auto preview_first = editor.query().compiled_object_mesh(first);
  const auto preview_second = editor.query().compiled_object_mesh(second);
  ASSERT_TRUE(preview_first.has_value());
  ASSERT_TRUE(preview_second.has_value());
  expect_uv_bounds(preview_first, 0.25f, 2.25f, -2.0f, 1.0f);
  expect_uv_bounds(preview_second, 0.25f, 2.25f, -2.0f, 1.0f);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);
  const auto applied = editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  EXPECT_TRUE(applied.changed());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Edge);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 2u);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, options);
  EXPECT_EQ(editor.history().snapshot().undo_label, "UV Peel");
  expect_mesh_equal(editor.query().compiled_object_mesh(first), *preview_first);
  expect_mesh_equal(editor.query().compiled_object_mesh(second), *preview_second);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_FALSE(editor.history().can_undo());
  expect_mesh_equal(editor.query().compiled_object_mesh(first), *original_first);
  expect_mesh_equal(editor.query().compiled_object_mesh(second), *original_second);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_FALSE(editor.history().can_redo());
  expect_mesh_equal(editor.query().compiled_object_mesh(first), *preview_first);
  expect_mesh_equal(editor.query().compiled_object_mesh(second), *preview_second);
}

TEST(UvPeelToolServiceTests, ReturningToOriginalUvsAppliesWithoutDiscardingRedo) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  const QuaderUvPeelOptions baseline{.world_space = false, .u_offset = 0.25};
  ASSERT_TRUE(editor.modeling().peel_uv({
      .options = baseline,
      .selection_token = editor.modeling().uv_peel_settings().selection_token,
  }).ok());
  auto alternate = baseline;
  alternate.u_offset = 0.75;
  ASSERT_TRUE(editor.modeling().peel_uv({
      .options = alternate,
      .selection_token = editor.modeling().uv_peel_settings().selection_token,
  }).ok());
  const auto redo_mesh = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(redo_mesh.has_value());
  ASSERT_TRUE(editor.history().undo().ok());
  const auto original = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(original.has_value());
  const auto history = editor.history().snapshot();
  ASSERT_TRUE(history.can_redo);
  ASSERT_TRUE(editor.modeling().set_uv_peel_options(baseline).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.u_offset", 0.75).ok());
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.u_offset", 0.25).ok());
  refresh(harness);
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  expect_history_equal(editor.history().snapshot(), history);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *redo_mesh);
}

TEST(UvPeelToolServiceTests, GuideChangesAndCancelRestoreTheCleanContentCheckpoint) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  const auto original = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(original.has_value());
  harness.session.mark_clean();
  ASSERT_FALSE(editor.project().dirty());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.u_offset", 0.25).ok());
  ASSERT_TRUE(editor.selection().select_edge(strip, 1, 4).ok());
  refresh(harness);
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  ASSERT_TRUE(editor.selection().clear().ok());
  refresh(harness);
  EXPECT_FALSE(editor.project().dirty());
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  refresh(harness);
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_FALSE(editor.project().dirty());
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
}

TEST(UvPeelToolServiceTests, NoopApplyPreservesCleanContentAfterAnIntermediatePreview) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  const QuaderUvPeelOptions options{.world_space = false, .u_offset = 0.25};
  ASSERT_TRUE(editor.modeling().peel_uv({
      .options = options,
      .selection_token = editor.modeling().uv_peel_settings().selection_token,
  }).ok());
  ASSERT_TRUE(editor.modeling().set_uv_peel_options(options).ok());
  const auto original = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(original.has_value());
  harness.session.mark_clean();
  ASSERT_FALSE(editor.project().dirty());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.u_offset", 0.75).ok());
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.u_offset", 0.25).ok());
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_FALSE(editor.project().dirty());
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
}

TEST(UvPeelToolServiceTests, CancelDoesNotHideAnUnrelatedSceneEdit) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  const auto other = create_strip(editor, "Unrelated");
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  const auto original = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(original.has_value());
  harness.session.mark_clean();
  ASSERT_FALSE(editor.project().dirty());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.u_offset", 0.25).ok());
  // Native document updates can arrive outside the command mutation blocker.
  // Such unrelated changes must contaminate the saved preview checkpoint.
  ASSERT_TRUE(harness.session.scene().set_visible(other.entity, false));
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_TRUE(editor.project().dirty());
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
  const auto after_other = editor.query().entity(other.entity);
  ASSERT_TRUE(after_other.has_value());
  EXPECT_FALSE(after_other->visible);
}

TEST(UvPeelToolServiceTests, AppliedRepeatUsesCapturedOptionsWithCurrentGuides) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto first = create_strip(editor, "First");
  const auto second = create_strip(editor, "Second");
  enter_peel_edit(editor, {first, second});
  ASSERT_TRUE(editor.selection().select_edge(first, 2, 5).ok());
  const QuaderUvPeelOptions captured{
      .axis = QuaderUvPeelAxis::V, .world_space = false,
      .u_repeat = 2.0, .v_repeat = 3.0, .u_offset = 0.25, .v_offset = -0.5,
  };
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  set_options(editor, captured);
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  const auto first_result = editor.query().compiled_object_mesh(first);
  ASSERT_TRUE(first_result.has_value());
  const QuaderUvPeelOptions displayed{
      .world_space = true, .u_repeat = 7.0, .v_repeat = 8.0,
      .u_offset = 4.0, .v_offset = 5.0,
  };
  ASSERT_TRUE(editor.modeling().set_uv_peel_options(displayed).ok());
  ASSERT_TRUE(editor.selection().select_edge(second, 2, 5).ok());
  const auto before_second = editor.query().compiled_object_mesh(second);
  ASSERT_TRUE(before_second.has_value());
  const auto repeated = editor.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_TRUE(repeated.changed());
  expect_mesh_equal(editor.query().compiled_object_mesh(second), *first_result);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, displayed);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_mesh_equal(editor.query().compiled_object_mesh(second), *before_second);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_mesh_equal(editor.query().compiled_object_mesh(second), *first_result);
}

TEST(UvPeelToolServiceTests, BranchingGuidesRestoreOriginalUvsAndRecoverInPlace) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  const auto original = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(original.has_value());
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  ASSERT_TRUE(editor.selection().select_components({
      .object = strip,
      .kind = QuaderComponentKind::Edge,
      .edges = {{1, 2}, {2, 3}, {2, 5}},
      .active = {.object = strip, .kind = QuaderComponentKind::Edge,
                 .edge_a = 2, .edge_b = 5},
  }).ok());
  (void)harness.session.uv_peel().refresh();
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  EXPECT_FALSE(editor.tools().active_settings().can_apply);
  EXPECT_FALSE(harness.session.uv_peel().preview_error().empty());
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  refresh(harness);
  EXPECT_TRUE(editor.tools().active_settings().can_apply);
  EXPECT_TRUE(harness.session.uv_peel().preview_error().empty());
}

TEST(UvPeelToolServiceTests, ToolAndSelectionModeChangesAcceptUvsWithHistory) {
  for (const bool change_mode : {false, true}) {
    SCOPED_TRACE(change_mode);
    UvPeelHarness harness;
    auto &editor = harness.editor;
    const auto strip = create_strip(editor);
    enter_peel_edit(editor, {strip});
    ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
    const auto original = editor.query().compiled_object_mesh(strip);
    ASSERT_TRUE(original.has_value());
    harness.session.history().clear();
    ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
    ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.u_offset", 0.25).ok());
    const auto preview = editor.query().compiled_object_mesh(strip);
    ASSERT_TRUE(preview.has_value());
    const auto changed = change_mode
        ? editor.selection().set_mode(QuaderSelectionMode::Face)
        : editor.tools().activate("move");
    ASSERT_TRUE(changed.ok()) << changed.error_message();
    EXPECT_EQ(editor.tools().active_tool(), change_mode ? "select" : "move");
    EXPECT_EQ(editor.selection().summary().mode,
              change_mode ? QuaderSelectionMode::Face : QuaderSelectionMode::Edge);
    expect_mesh_equal(editor.query().compiled_object_mesh(strip), *preview);
    ASSERT_TRUE(editor.history().undo().ok());
    expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
    EXPECT_FALSE(editor.history().can_undo());
    ASSERT_TRUE(editor.history().redo().ok());
    expect_mesh_equal(editor.query().compiled_object_mesh(strip), *preview);
  }
}

TEST(UvPeelToolServiceTests, UnchangedFramesDoNotRebuildOrRepublishThePreview) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.u_offset", 0.25).ok());
  refresh(harness);
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  const auto settings_revision = editor.tools().active_settings_revision();
  const auto owner_revision = harness.session.uv_peel().tool_settings_revision();
  const auto before = harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  const auto mesh = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(mesh.has_value());
  for (int frame = 0; frame < 30; ++frame) {
    SCOPED_TRACE(frame);
    refresh(harness);
    EXPECT_EQ(editor.tools().active_settings_revision(), settings_revision);
    EXPECT_EQ(harness.session.uv_peel().tool_settings_revision(), owner_revision);
    EXPECT_TRUE(editor.tools().active_settings().can_apply);
  }
  const auto after = harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after.mesh_compile_count, before.mesh_compile_count);
  EXPECT_EQ(after.asset_publication_count, before.asset_publication_count);
  EXPECT_EQ(after.scene_publication_count, before.scene_publication_count);
  EXPECT_EQ(after.preview_publication_count, before.preview_publication_count);
  EXPECT_EQ(after.uv_peel_preparation_count, before.uv_peel_preparation_count);
  EXPECT_EQ(after.uv_peel_summary_build_count, before.uv_peel_summary_build_count);
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *mesh);
}

TEST(UvPeelToolServiceTests, SceneReplacementEndsTheToolAndKeepsAcceptedOptions) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().set_setting("uv_peel", "uv_peel.u_offset", 0.25).ok());
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  const auto preview = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(preview.has_value());
  const auto blocked = editor.scene_documents().new_scene();
  EXPECT_FALSE(blocked.ok());
  EXPECT_FALSE(blocked.error_message().empty());
  EXPECT_TRUE(harness.session.dirty());
  EXPECT_TRUE(editor.history().can_undo());
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *preview);
  // A standalone replacement must first give the accepted scene a save decision.
  harness.session.mark_clean();
  ASSERT_TRUE(editor.scene_documents().new_scene().ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_FALSE(editor.query().entity(strip.entity).has_value());
  EXPECT_FALSE(editor.history().can_undo());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options.u_offset, 0.25);
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Object);
  EXPECT_FALSE(editor.tools().activate("uv_peel").ok());
  const auto replacement = create_strip(editor, "Replacement strip");
  enter_peel_edit(editor, {replacement});
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  EXPECT_FALSE(editor.tools().active_settings().can_apply);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(UvPeelToolServiceTests, SceneSwitchAcceptsPreviewAndSharesAcceptedOptions) {
  ScopedPeelProjectDirectory directory;
  ProjectSession project;
  const auto created = project.create(directory.path());
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const auto first_scene = *created.scene_session_id;
  QuaderEditorApi editor(project);
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  const auto original = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(original.has_value());
  QuaderUvPeelOptions remembered{.world_space = false, .u_offset = 0.25};
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  set_options(editor, remembered);
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  const auto first_preview = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(first_preview.has_value());
  const auto second = editor.scene_documents().new_scene();
  ASSERT_TRUE(second.ok()) << second.error_message();
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, remembered);
  remembered.v_repeat = 4.0;
  const auto second_strip = create_strip(editor, "Second scene strip");
  enter_peel_edit(editor, {second_strip});
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  set_options(editor, remembered);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(editor.scene_documents().activate(first_scene).ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Edge);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 1u);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, remembered);
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *first_preview);
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  const auto second_preview = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(second_preview.has_value());
  ASSERT_TRUE(editor.scene_documents().activate(second.value()).ok());
  ASSERT_TRUE(editor.scene_documents().activate(first_scene).ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *second_preview);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *first_preview);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
}

TEST(UvPeelToolServiceTests,
     FailedPreviewRestoresBorrowedAcceptedOptionsAndInvalidatesRevision) {
  const auto defaults = std::make_shared<ToolSessionDefaults>();
  UvPeelHarness first(defaults);
  UvPeelHarness second(defaults);
  auto &editor = first.editor;
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  const QuaderUvPeelOptions remembered{.world_space = false, .u_offset = 0.25};
  set_options(editor, remembered);
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  const auto revision = defaults->uv_peel.revision;
  first.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::PreviewAssetReplace);
  const auto rejected =
      editor.tools().set_setting("uv_peel", "uv_peel.u_offset", 0.75);
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, remembered);
  EXPECT_EQ(second.editor.modeling().uv_peel_settings().options, remembered);
  EXPECT_GT(defaults->uv_peel.revision, revision);
  EXPECT_EQ(second.editor.selection().summary().selected_edge_count, 0u);
  EXPECT_FALSE(second.session.modeling().uv_peel_preview_active());
  first.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  EXPECT_TRUE(editor.tools().active_settings().can_apply);
  expect_uv_bounds(editor.query().compiled_object_mesh(strip),
                   0.25f, 1.25f, -0.5f, 0.5f);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());

  UvPeelHarness fresh;
  EXPECT_EQ(fresh.editor.modeling().uv_peel_settings().options,
            QuaderUvPeelOptions{});
}

TEST(UvPeelToolServiceTests, LockedTargetsNeverPublishAndCanRecoverAfterUnlock) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto strip = create_strip(editor);
  enter_peel_edit(editor, {strip});
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  const auto original = editor.query().compiled_object_mesh(strip);
  ASSERT_TRUE(original.has_value());
  ASSERT_TRUE(editor.scene().set_selectable(strip.entity, false).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  EXPECT_FALSE(editor.tools().active_settings().can_apply);
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(editor.scene().set_selectable(strip.entity, true).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  ASSERT_TRUE(editor.selection().select_edge(strip, 2, 5).ok());
  refresh(harness);
  EXPECT_TRUE(editor.tools().active_settings().can_apply);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  expect_mesh_equal(editor.query().compiled_object_mesh(strip), *original);
}

TEST(UvPeelToolServiceTests, InstanceEditKeepsOccurrenceMappingThroughCancelAndApply) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto member = create_strip(editor);
  ASSERT_TRUE(editor.selection().select_object(member).ok());
  const auto created = editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto occurrence = created.value().occurrence.root.id;
  const auto definition = created.value().definition.root.id;
  ASSERT_TRUE(editor.scene().set_transform(occurrence, {
      .location = {5.0f, 2.0f, 0.0f}, .scale = {2.0f, 3.0f, 1.0f},
  }).ok());
  const auto before_occurrence = editor.query().entity(occurrence);
  ASSERT_TRUE(before_occurrence.has_value());
  ASSERT_TRUE(before_occurrence->texture_sampling_from_local.has_value());
  const auto mapping = before_occurrence->texture_sampling_from_local->values;
  ASSERT_TRUE(editor.modeling().enter_instance_edit(occurrence).ok());
  enter_peel_edit(editor, {member});
  ASSERT_TRUE(editor.selection().select_edge(member, 2, 5).ok());
  const auto original = editor.query().compiled_object_mesh(member);
  ASSERT_TRUE(original.has_value());
  const QuaderUvPeelOptions options{
      .world_space = false, .u_offset = 0.5, .v_offset = -0.25,
  };
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  set_options(editor, options);
  ASSERT_TRUE(editor.tools().active_settings().can_apply);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  expect_mesh_equal(editor.query().compiled_object_mesh(member), *original);
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  const auto preview = editor.query().compiled_object_mesh(member);
  ASSERT_TRUE(preview.has_value());
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  const auto after_occurrence = editor.query().entity(occurrence);
  ASSERT_TRUE(after_occurrence.has_value());
  EXPECT_EQ(after_occurrence->instance_definition, definition);
  ASSERT_TRUE(after_occurrence->texture_sampling_from_local.has_value());
  EXPECT_EQ(after_occurrence->texture_sampling_from_local->values, mapping);
  const auto after_member = editor.query().entity(member.entity);
  ASSERT_TRUE(after_member.has_value());
  EXPECT_EQ(after_member->parent, definition);
  expect_mesh_equal(editor.query().compiled_object_mesh(member), *preview);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_mesh_equal(editor.query().compiled_object_mesh(member), *original);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_mesh_equal(editor.query().compiled_object_mesh(member), *preview);
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor.modeling().exit_instance_edit().ok());
}

TEST(UvPeelToolServiceTests, InstanceContextCleanupPreservesRememberedSceneOptions) {
  UvPeelHarness harness;
  auto &editor = harness.editor;
  const auto member = create_strip(editor);
  ASSERT_TRUE(editor.selection().select_object(member).ok());
  harness.session.history().clear();
  const auto created = editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto occurrence = created.value().occurrence.root.id;
  ASSERT_TRUE(editor.modeling().enter_instance_edit(occurrence).ok());
  enter_peel_edit(editor, {member});
  ASSERT_TRUE(editor.selection().select_edge(member, 2, 5).ok());
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  const QuaderUvPeelOptions remembered{
      .axis = QuaderUvPeelAxis::V, .world_space = false,
      .u_repeat = 2.0, .v_repeat = 3.0, .u_offset = 0.25, .v_offset = -0.5,
  };
  set_options(editor, remembered);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(editor.modeling().instance_edit_state().active);
  ASSERT_EQ(harness.session.history().undo_count(), 2u);
  const auto edit_before_undo = editor.mode().state();
  ASSERT_EQ(edit_before_undo.participants.size(), 1u);
  ASSERT_EQ(edit_before_undo.participants.front().id, member.entity);

  // Explicitly choosing the member in Object Mode adds its own selection step.
  // Cancelling Peel and entering Edit add no history beyond that selection.
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.query().entity(occurrence).has_value());
  ASSERT_TRUE(editor.modeling().instance_edit_state().active);
  ASSERT_EQ(harness.session.history().undo_count(), 1u);

  // Undo removes the occurrence and restores the same member as an ordinary
  // mesh. The instance scope ends and no selected component participant survives.
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_FALSE(editor.query().entity(occurrence).has_value());
  EXPECT_FALSE(editor.modeling().instance_edit_state().active);
  const auto edit_after_undo = editor.mode().state();
  EXPECT_EQ(edit_after_undo.mode, QuaderEditorMode::Edit);
  EXPECT_EQ(edit_after_undo.generation, edit_before_undo.generation);
  EXPECT_TRUE(edit_after_undo.participants.empty());
  EXPECT_EQ(editor.selection().summary().selected_object_count, 0u);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 0u);
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Edge);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, remembered);
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, remembered);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_EQ(harness.session.history().undo_count(), 0u);

  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Object).ok());
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Object);
  EXPECT_FALSE(editor.tools().activate("uv_peel").ok());
  enter_peel_edit(editor, {member});
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, remembered);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

} // namespace
} // namespace quader
