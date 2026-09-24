////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "assets/texture_asset.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/input/quader_input_binding.hpp"
#include "editor/input/viewport_keybinding_resolver.hpp"
#include "editor/modeling/linear_array_layout.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "math/glm_bridge.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "render/scene_render_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"
#include "scene/scene_transform_pivot.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

void expect_vec3_near(QVec3 actual, QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, 0.0001f);
  EXPECT_NEAR(actual.y, expected.y, 0.0001f);
  EXPECT_NEAR(actual.z, expected.z, 0.0001f);
}

void expect_matrix_near(const QMat4 &actual, const QMat4 &expected) {
  for (std::size_t component = 0; component < actual.values.size();
       ++component) {
    EXPECT_NEAR(actual.values[component], expected.values[component], 0.0001f);
  }
}

QMat4 effective_instance_member_world(const SceneDocument &scene,
                                      EntityId occurrence,
                                      EntityId definition,
                                      EntityId member) {
  const std::optional<QMat4> definition_inverse =
      try_scene_matrix_inverse(scene.world_transform_matrix(definition));
  EXPECT_TRUE(definition_inverse.has_value());
  if (!definition_inverse.has_value()) {
    return {};
  }
  return scene_transform_multiply(
      scene.world_transform_matrix(occurrence),
      scene_transform_multiply(*definition_inverse,
                               scene.world_transform_matrix(member)));
}

quader_poly::Document asymmetric_array_document() {
  quader_poly::Document document;
  const quader_poly::ElementId origin =
      quader_poly::add_vertex(document, {});
  const quader_poly::ElementId x =
      quader_poly::add_vertex(document, {4.0f, 0.0f, 0.0f});
  const quader_poly::ElementId y =
      quader_poly::add_vertex(document, {0.0f, 2.0f, 0.0f});
  const quader_poly::ElementId z =
      quader_poly::add_vertex(document, {0.0f, 0.0f, 3.0f});
  const std::array first_face{origin, x, y};
  const std::array second_face{origin, z, x};
  EXPECT_NE(quader_poly::add_face(document, first_face),
            quader_poly::kInvalidElementId);
  EXPECT_NE(quader_poly::add_face(document, second_face),
            quader_poly::kInvalidElementId);
  return document;
}

QuaderModelingObjectRef create_asymmetric_array_mesh(
    QuaderEditorApi &editor, std::string name) {
  const QuaderApiResult<QuaderModelingObjectRef> created =
      editor.modeling().create_mesh(
          QuaderPolygonDocumentInternalAccess::from_backend(
              asymmetric_array_document()),
          {.name = std::move(name)});
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

SceneBounds exact_transformed_geometry_bounds(const MeshAsset &mesh,
                                              const QMat4 &world_transform) {
  const std::vector<MeshVertex> &vertices = mesh.geometry().vertices;
  EXPECT_FALSE(vertices.empty());
  if (vertices.empty()) {
    return {};
  }
  const QVec3 first = transform_point(world_transform,
                                     vertices.front().position);
  SceneBounds bounds{.min = first, .max = first};
  for (std::size_t index = 1u; index < vertices.size(); ++index) {
    include_point(bounds,
                  transform_point(world_transform,
                                  vertices[index].position));
  }
  return bounds;
}

TEST(LinearArrayLayoutTests, PreservesOrientationAndIncludesSharedEndpoint) {
  const QMat4 source_world = compose_transform(QTransform{
      .position = {4.0f, 3.0f, 2.0f},
      .scale = {2.0f, 1.0f, 0.5f},
  });
  const LinearArrayLayoutResult layout = build_linear_array_layout({
      .source_world_transform = source_world,
      .span_world = {6.0f, -3.0f, 9.0f},
      .count = 4u,
  });
  ASSERT_TRUE(layout.ok) << layout.message;
  ASSERT_EQ(layout.generated_world_transforms.size(), 3u);
  expect_vec3_near(transform_point(layout.generated_world_transforms[0], {}),
                   {6.0f, 2.0f, 5.0f});
  expect_vec3_near(transform_point(layout.generated_world_transforms[2], {}),
                   {10.0f, 0.0f, 11.0f});
  expect_vec3_near(
      transform_vector(layout.generated_world_transforms[1],
                       {1.0f, 0.0f, 0.0f}),
      transform_vector(source_world, {1.0f, 0.0f, 0.0f}));
}

TEST(NativeViewportToolCursorTests,
     PivotAndLinearHandlesTrackHoverDragReleaseAndCancellation) {
  for (const ViewportToolId tool_id :
       {ViewportToolId::Pivot, ViewportToolId::LinearArray}) {
    SCOPED_TRACE(static_cast<int>(tool_id));
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    const auto source = editor.modeling().create_box(
        QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                     .max = {1.0f, 1.0f, 1.0f}},
        "Cursor Source");
    ASSERT_TRUE(source.ok()) << source.error_message();
    ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
    ASSERT_TRUE(editor.tools().activate(tool_id).ok());
    ViewportCameraController camera;
    camera.set_viewport_size(800.0f, 600.0f);
    camera.focus_origin(16.0f);
    camera.view_axis(ViewportViewAxis::Y, 1);
    const auto handle_world = [&]() {
      return tool_id == ViewportToolId::Pivot
                 ? session.pivot().handle_state().world_position
                 : session.linear_array().handle_state().endpoint_world;
    };
    ViewportInputFrame input;
    input.viewport_hovered = true;
    input.viewport_focused = true;
    input.viewport_size = {800.0f, 600.0f};
    input.mouse_local = camera.project_world_point(handle_world()).position;
    input.mouse_screen = input.mouse_local;
    ViewportInputResult input_result;
    bool handles_visible = true;
    const auto frame = [&](bool pressed, bool down, bool released) {
      return session.native_tools().handle_pointer_frame(
          session.scene(), session.assets(), &session.point_placement(), camera,
          {.input = input,
           .input_result = input_result,
           .handles_visible = handles_visible,
           .left_pressed = pressed,
           .left_down = down,
           .left_released = released,
           .left_click_local = input.mouse_local});
    };
    (void)frame(false, false, false);
    EXPECT_EQ(session.native_tools().pointer_cursor(),
              ViewportPointerCursor::OpenHand);
    const std::uint64_t scene_revision = session.scene().revision();
    (void)frame(false, false, false);
    EXPECT_EQ(session.scene().revision(), scene_revision);
    EXPECT_EQ(session.native_tools().pointer_cursor(),
              ViewportPointerCursor::OpenHand);
    (void)frame(true, true, false);
    EXPECT_EQ(session.native_tools().pointer_cursor(),
              ViewportPointerCursor::ClosedHand);
    EXPECT_TRUE(session.native_tools().pointer_dragging());
    input.mouse_local.x += 24.0f;
    (void)frame(false, true, false);
    EXPECT_EQ(session.native_tools().pointer_cursor(),
              ViewportPointerCursor::ClosedHand);
    input.mouse_local = camera.project_world_point(handle_world()).position;
    (void)frame(false, false, true);
    EXPECT_FALSE(session.native_tools().pointer_dragging());
    EXPECT_EQ(session.native_tools().pointer_cursor(),
              ViewportPointerCursor::OpenHand);

    for (int interruption = 0; interruption < 5; ++interruption) {
      SCOPED_TRACE(interruption);
      input.mouse_local = camera.project_world_point(handle_world()).position;
      (void)frame(true, true, false);
      ASSERT_TRUE(session.native_tools().pointer_dragging());
      input_result.navigation_active = interruption == 0;
      input.viewport_focused = interruption != 1;
      handles_visible = interruption != 2;
      input.pointer_interaction_canceled = interruption == 3;
      input.text_input_active = interruption == 4;
      (void)frame(false, true, false);
      EXPECT_EQ(session.native_tools().pointer_cursor(),
                ViewportPointerCursor::Default);
      EXPECT_FALSE(session.native_tools().pointer_dragging());
      input_result.navigation_active = false;
      input.viewport_focused = true;
      handles_visible = true;
      input.pointer_interaction_canceled = false;
      input.text_input_active = false;
      (void)frame(false, false, true);
    }
    input.mouse_local = camera.project_world_point(handle_world()).position;
    (void)frame(false, false, false);
    EXPECT_EQ(session.native_tools().pointer_cursor(),
              ViewportPointerCursor::OpenHand);
    session.native_tools().clear_pointer_drag_state();
    EXPECT_EQ(session.native_tools().pointer_cursor(),
              ViewportPointerCursor::Default);
    input.mouse_local = {-100.0f, -100.0f};
    (void)frame(false, false, false);
    EXPECT_EQ(session.native_tools().pointer_cursor(),
              ViewportPointerCursor::Default);
    input.mouse_local = camera.project_world_point(handle_world()).position;
    (void)frame(false, false, false);
    ASSERT_EQ(session.native_tools().pointer_cursor(),
              ViewportPointerCursor::OpenHand);
    ASSERT_TRUE(session.native_tools().cancel_active().ok);
    EXPECT_EQ(session.native_tools().pointer_cursor(),
              ViewportPointerCursor::Default);
  }
}

TEST(NativeViewportToolCursorTests,
     HiddenRadialCenterKeepsPlacementAndLockingAvailable) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto source = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
      "Radial Cursor Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.scene()
                  .set_transform(source.value().entity,
                                 {.position = {6.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::RadialArray).ok());
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(16.0f);
  camera.view_axis(ViewportViewAxis::Y, 1);
  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_local = camera.project_world_point({}).position;
  ViewportInputResult input_result;
  bool handles_visible = true;
  const auto frame = [&](bool pressed = false) {
    input.mouse_screen = input.mouse_local;
    return session.native_tools().handle_pointer_frame(
        session.scene(), session.assets(), &session.point_placement(), camera,
        {.input = input,
         .input_result = input_result,
         .handles_visible = handles_visible,
         .left_pressed = pressed,
         .left_down = pressed,
         .left_click_local = input.mouse_local});
  };

  (void)frame();
  const RadialArrayCenterState visible_candidate =
      session.radial_array().center_state();
  ASSERT_TRUE(visible_candidate.visible);
  ASSERT_FALSE(visible_candidate.locked);
  EXPECT_EQ(session.native_tools().pointer_cursor(),
            ViewportPointerCursor::OpenHand);

  handles_visible = false;
  (void)frame();
  ASSERT_TRUE(session.radial_array().active());
  ASSERT_TRUE(session.radial_array().center_state().visible);
  EXPECT_FALSE(session.radial_array().center_locked());
  expect_vec3_near(session.radial_array().center_state().world_position,
                   visible_candidate.world_position);
  EXPECT_EQ(session.native_tools().pointer_cursor(),
            ViewportPointerCursor::Default);

  const QVec3 moved_center{0.0f, 0.0f, 2.0f};
  input.mouse_local = camera.project_world_point(moved_center).position;
  (void)frame();
  ASSERT_TRUE(session.radial_array().center_state().visible);
  expect_vec3_near(session.radial_array().center_state().world_position,
                   moved_center);
  EXPECT_EQ(session.native_tools().pointer_cursor(),
            ViewportPointerCursor::Default);

  const auto entity_count_before = session.scene().entities().size();
  const auto undo_count_before = session.history().undo_count();
  const auto locked = frame(true);
  EXPECT_TRUE(locked.radial_array_consumed);
  EXPECT_FALSE(locked.sync_active_tool);
  EXPECT_TRUE(session.radial_array().active());
  EXPECT_TRUE(session.radial_array().center_locked());
  expect_vec3_near(session.radial_array().center_state().world_position,
                   moved_center);
  EXPECT_EQ(session.scene().entities().size(), entity_count_before);
  EXPECT_EQ(session.history().undo_count(), undo_count_before);
  EXPECT_EQ(session.native_tools().pointer_cursor(),
            ViewportPointerCursor::Default);
}

class RadialArrayViewportInputTests : public testing::Test {
protected:
  void SetUp() override {
    keybindings_path_ = std::filesystem::temp_directory_path() /
                       (std::string("quader-radial-input-") +
                        testing::UnitTest::GetInstance()->current_test_info()->name() +
                        ".json");
    std::error_code ignored;
    std::filesystem::remove(keybindings_path_, ignored);
    ASSERT_TRUE(editor_.commands().set_keybinding_storage_path(keybindings_path_).ok());
    const auto created = editor_.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Radial Input Source");
    ASSERT_TRUE(created.ok()) << created.error_message();
    source_ = created.value().entity;
    ASSERT_TRUE(editor_.scene().set_transform(source_,
        {.position = {6.0f, 0.0f, 0.0f}}).ok());
    ASSERT_TRUE(editor_.selection().select_object(created.value()).ok());
    ASSERT_TRUE(editor_.tools().activate(ViewportToolId::RadialArray).ok());
    camera_.set_viewport_size(800.0f, 600.0f);
    camera_.focus_origin(16.0f);
    camera_.view_axis(ViewportViewAxis::Y, 1);
    input_.viewport_hovered = true;
    input_.viewport_focused = true;
    input_.active_tool_owns_viewport = true;
    input_.viewport_size = {800.0f, 600.0f};
    input_.mouse_local = camera_.project_world_point({}).position;
    initial_entities_ = session_.scene().entities();
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove(keybindings_path_, ignored);
  }

  ViewportNativeToolInteractionResult frame(
      bool pressed = false, bool down = false, bool released = false,
      std::optional<QVec2> click = std::nullopt,
      bool suppress_hover = false) {
    input_.mouse_screen = input_.mouse_local;
    return session_.native_tools().handle_pointer_frame(
        session_.scene(), session_.assets(), &session_.point_placement(), camera_,
        {.input = input_,
         .input_result = input_result_,
         .suppress_hover_work = suppress_hover,
         .left_pressed = pressed,
         .left_down = down,
         .left_released = released,
         .left_click_local = click.value_or(input_.mouse_local),
         .radial_array_wheel_remainder = &radial_wheel_remainder_});
  }

  static QuaderInputChord chord(std::string_view text) {
    return *quader_input_parse_chord(text);
  }

  static std::vector<QuaderWheelInputEdge> wheel(
      double delta, std::string_view base_chord = "Shift+Wheel") {
    const std::string direction =
        std::string(base_chord) + (delta >= 0.0 ? "Up" : "Down");
    return {{chord(base_chord), delta}, {chord(direction), delta}};
  }

  void resolve(std::vector<QuaderInputChord> pressed = {},
               std::vector<QuaderWheelInputEdge> wheels = {}) {
    input_.pressed_input_chords = std::move(pressed);
    input_.wheel_input_edges = std::move(wheels);
    input_.keybinding_input_consumed = false;
    input_.modal_tool_finalize_pressed = false;
    ViewportKeybindingResolver::resolve(
        editor_, {.active_tool = ViewportToolId::RadialArray}, input_,
        suppressed_chords_, linear_wheel_remainder_, radial_wheel_remainder_);
  }

  SceneSession session_;
  QuaderEditorApi editor_{
      QuaderApiSessionInternalAccess::from_scene_session(session_)};
  EntityId source_ = kInvalidEntityId;
  std::vector<EntityId> initial_entities_;
  ViewportCameraController camera_;
  ViewportInputFrame input_;
  ViewportInputResult input_result_;
  double radial_wheel_remainder_ = 0.0;
  double linear_wheel_remainder_ = 0.0;
  std::vector<QuaderInputChord> suppressed_chords_;

private:
  std::filesystem::path keybindings_path_;
};

TEST_F(RadialArrayViewportInputTests,
       PassivePreviewNeedsNoKeyboardFocusAndFreezesWhileIneligible) {
  input_.viewport_focused = false;
  const auto initial = frame();
  ASSERT_TRUE(session_.radial_array().has_valid_center());
  ASSERT_TRUE(session_.radial_array().preview_state().active);
  EXPECT_TRUE(initial.mark_overlay_dirty);
  const auto center = session_.radial_array().center_state();
  const auto preview_revision = session_.radial_array().preview_state().revision;
  EXPECT_FALSE(frame().mark_overlay_dirty);
  EXPECT_EQ(session_.radial_array().preview_state().revision, preview_revision);

  input_.mouse_local = camera_.project_world_point({0.0f, 0.0f, 3.0f}).position;
  const auto expect_retained = [&]() {
    EXPECT_FALSE(frame().mark_overlay_dirty);
    EXPECT_EQ(session_.radial_array().center_state().revision, center.revision);
    EXPECT_EQ(session_.radial_array().preview_state().revision, preview_revision);
    expect_vec3_near(session_.radial_array().center_state().world_position,
                     center.world_position);
  };
  input_.viewport_hovered = false;
  expect_retained();
  input_.viewport_hovered = true;
  input_.text_input_active = true;
  expect_retained();
  input_.text_input_active = false;
  input_.navigation_active = true;
  expect_retained();
  input_.navigation_active = false;
  input_result_.navigation_active = true;
  expect_retained();
  input_result_.navigation_active = false;
  input_result_.camera_changed = true;
  expect_retained();
  input_result_.camera_changed = false;
  EXPECT_FALSE(frame(false, false, false, std::nullopt, true).mark_overlay_dirty);
  EXPECT_EQ(session_.radial_array().center_state().revision, center.revision);

  EXPECT_TRUE(frame().mark_overlay_dirty);
  expect_vec3_near(session_.radial_array().center_state().world_position,
                   {0.0f, 0.0f, 3.0f});
  EXPECT_GT(session_.radial_array().preview_state().revision, preview_revision);
  EXPECT_EQ(session_.scene().entities().size(), initial_entities_.size());
}

TEST_F(RadialArrayViewportInputTests,
       CompleteClickLocksPressPositionAndApplyPublishesOneUndoStep) {
  (void)frame();
  const QVec2 press = input_.mouse_local;
  const auto center_before = session_.radial_array().center_state().world_position;
  const auto undo_count_before = session_.history().undo_count();
  input_.mouse_local = camera_.project_world_point({0.0f, 0.0f, 3.0f}).position;
  const auto locked = frame(true, false, true, press);
  EXPECT_FALSE(locked.sync_active_tool);
  ASSERT_TRUE(session_.radial_array().active());
  ASSERT_TRUE(session_.radial_array().center_locked());
  const auto locked_center = session_.radial_array().center_state();
  expect_vec3_near(locked_center.world_position, center_before);
  const auto locked_preview_revision = session_.radial_array().preview_state().revision;
  EXPECT_FALSE(frame().mark_overlay_dirty);
  EXPECT_EQ(session_.radial_array().center_state().revision, locked_center.revision);
  EXPECT_EQ(session_.radial_array().preview_state().revision, locked_preview_revision);
  EXPECT_FALSE(frame(true, false, true).sync_active_tool);
  EXPECT_TRUE(session_.radial_array().active());
  EXPECT_EQ(session_.radial_array().center_state().revision, locked_center.revision);
  EXPECT_EQ(session_.scene().entities(), initial_entities_);
  EXPECT_EQ(session_.history().undo_count(), undo_count_before);

  ASSERT_TRUE(editor_.tools()
                  .set_setting("radial_array", "radial_array.degrees", 180.0)
                  .ok());
  ASSERT_TRUE(editor_.tools()
                  .set_setting("radial_array", "radial_array.copies", 4.0)
                  .ok());
  ASSERT_TRUE(session_.radial_array().active());
  ASSERT_TRUE(session_.radial_array().center_locked());
  const auto &edited_preview = session_.radial_array().preview_state();
  EXPECT_GT(edited_preview.revision, locked_preview_revision);
  const auto expected = edited_preview.sources.front().generated_world_transforms;
  ASSERT_EQ(expected.size(), 3u);
  expect_vec3_near(session_.radial_array().center_state().world_position, center_before);
  EXPECT_EQ(session_.scene().entities(), initial_entities_);
  EXPECT_EQ(session_.history().undo_count(), undo_count_before);

  const auto applied = editor_.tools().commit_active_tool_preview();
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  ASSERT_FALSE(session_.radial_array().active());
  ASSERT_EQ(session_.scene().entities().size(),
            initial_entities_.size() + expected.size());
  std::size_t generated = 0u;
  for (const EntityId entity : session_.scene().entities()) {
    if (std::find(initial_entities_.begin(), initial_entities_.end(), entity) !=
        initial_entities_.end()) {
      continue;
    }
    ASSERT_LT(generated, expected.size());
    expect_matrix_near(session_.scene().world_transform_matrix(entity),
                        expected[generated++]);
  }
  EXPECT_EQ(generated, expected.size());
  EXPECT_EQ(editor_.query().history().undo_label, "Radial Array");
  EXPECT_EQ(session_.history().undo_count(), undo_count_before + 1u);
  EXPECT_FALSE(frame().consumed);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(session_.scene().entities().size(), initial_entities_.size());
  EXPECT_EQ(session_.history().undo_count(), undo_count_before);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(session_.scene().entities().size(),
            initial_entities_.size() + expected.size());
  EXPECT_EQ(session_.history().undo_count(), undo_count_before + 1u);
}

TEST_F(RadialArrayViewportInputTests,
       HeldCenterLockPreservesReleaseOwnershipAndInvalidPressCannotLockRetainedCenter) {
  (void)frame();
  const auto retained = session_.radial_array().center_state();
  const float invalid = std::numeric_limits<float>::quiet_NaN();
  input_.modal_tool_finalize_pressed = true;
  EXPECT_FALSE(frame(true, false, true, QVec2{invalid, invalid}).sync_active_tool);
  ASSERT_TRUE(session_.radial_array().active());
  EXPECT_FALSE(session_.radial_array().center_locked());
  EXPECT_EQ(session_.scene().entities().size(), initial_entities_.size());
  EXPECT_EQ(session_.radial_array().center_state().revision, retained.revision);

  input_.modal_tool_finalize_pressed = false;
  input_.viewport_focused = false;
  EXPECT_FALSE(frame(true, false, true).sync_active_tool);
  EXPECT_TRUE(session_.radial_array().active());
  EXPECT_FALSE(session_.radial_array().center_locked());
  input_.viewport_focused = true;
  EXPECT_FALSE(frame(true, true, false).sync_active_tool);
  ASSERT_TRUE(session_.radial_array().active());
  ASSERT_TRUE(session_.radial_array().center_locked());
  EXPECT_EQ(session_.scene().entities(), initial_entities_);
  EXPECT_TRUE(frame(false, true, false).consumed);
  resolve({chord("Enter")});
  EXPECT_TRUE(frame(false, true, false).sync_active_tool);
  ASSERT_FALSE(session_.radial_array().active());
  EXPECT_TRUE(frame(false, false, true).consumed);
  EXPECT_FALSE(frame().consumed);
  EXPECT_EQ(session_.scene().entities().size(),
            initial_entities_.size() + kRadialArrayDefaultCopies - 1u);
}

TEST_F(RadialArrayViewportInputTests, EnterAppliesTheRetainedValidCenter) {
  resolve({chord("Enter")});
  (void)frame();
  ASSERT_TRUE(session_.radial_array().active());
  ASSERT_TRUE(session_.radial_array().has_valid_center());
  input_.viewport_hovered = false;
  resolve({chord("Enter")});
  EXPECT_TRUE(frame().sync_active_tool);
  EXPECT_FALSE(session_.radial_array().active());
  EXPECT_EQ(session_.scene().entities().size(),
            initial_entities_.size() + kRadialArrayDefaultCopies - 1u);
}

TEST_F(RadialArrayViewportInputTests,
       WheelStepsKeepPhysicalOrderAtBoundsAndRetainOnlyEligibleFractions) {
  ASSERT_TRUE(session_.radial_array().set_copies(kRadialArrayMaximumCopies).ok);
  auto events = wheel(0.75);
  const auto down = wheel(-1.0);
  events.insert(events.end(), down.begin(), down.end());
  resolve({}, events);
  EXPECT_FLOAT_EQ(input_.mouse_wheel, 0.0f);
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayMaximumCopies - 1u);
  EXPECT_DOUBLE_EQ(radial_wheel_remainder_, 0.0);

  ASSERT_TRUE(session_.radial_array().set_copies(kRadialArrayMinimumCopies).ok);
  events = wheel(-2.0);
  const auto up = wheel(1.0);
  events.insert(events.end(), up.begin(), up.end());
  resolve({}, events);
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayMinimumCopies + 1u);
  resolve({}, wheel(0.4));
  (void)frame();
  EXPECT_NEAR(radial_wheel_remainder_, 0.4, 0.000001);
  resolve({}, wheel(0.6));
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayMinimumCopies + 2u);
  EXPECT_DOUBLE_EQ(radial_wheel_remainder_, 0.0);

  resolve({}, wheel(0.7));
  (void)frame();
  input_.text_input_active = true;
  resolve({}, wheel(1.0));
  (void)frame();
  EXPECT_DOUBLE_EQ(radial_wheel_remainder_, 0.0);
  input_.text_input_active = false;
  resolve({}, wheel(0.3));
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayMinimumCopies + 2u);
  input_.navigation_active = true;
  resolve({}, wheel(1.0));
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayMinimumCopies + 2u);
  EXPECT_DOUBLE_EQ(radial_wheel_remainder_, 0.0);
}

TEST_F(RadialArrayViewportInputTests,
       RepeatWheelStepsPreserveLimitsFractionsAndPhysicalReversals) {
  const std::uint32_t maximum_repeats = session_.radial_array().maximum_repeats();
  ASSERT_TRUE(session_.radial_array().set_repeats(maximum_repeats).ok);
  auto events = wheel(0.75, "Ctrl+Wheel");
  const auto down = wheel(-1.0, "Ctrl+Wheel");
  events.insert(events.end(), down.begin(), down.end());
  resolve({}, events);
  EXPECT_FLOAT_EQ(input_.mouse_wheel, 0.0f);
  (void)frame();
  EXPECT_EQ(session_.radial_array().repeats(), maximum_repeats - 1u);
  EXPECT_DOUBLE_EQ(radial_wheel_remainder_, 0.0);

  ASSERT_TRUE(session_.radial_array().set_repeats(1u).ok);
  events = wheel(-2.0, "Ctrl+Wheel");
  const auto up = wheel(1.0, "Ctrl+Wheel");
  events.insert(events.end(), up.begin(), up.end());
  resolve({}, events);
  (void)frame();
  EXPECT_EQ(session_.radial_array().repeats(), 2u);
  resolve({}, wheel(0.4, "Ctrl+Wheel"));
  (void)frame();
  EXPECT_NEAR(radial_wheel_remainder_, 0.4, 0.000001);
  resolve({}, wheel(0.6, "Ctrl+Wheel"));
  (void)frame();
  EXPECT_EQ(session_.radial_array().repeats(), 3u);
  EXPECT_DOUBLE_EQ(radial_wheel_remainder_, 0.0);

  resolve({}, wheel(0.75, "Ctrl+Wheel"));
  (void)frame();
  resolve({}, wheel(-0.25, "Ctrl+Wheel"));
  (void)frame();
  EXPECT_NEAR(radial_wheel_remainder_, 0.5, 0.000001);
  resolve({}, wheel(-1.5, "Ctrl+Wheel"));
  (void)frame();
  EXPECT_EQ(session_.radial_array().repeats(), 2u);
  EXPECT_DOUBLE_EQ(radial_wheel_remainder_, 0.0);
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayDefaultCopies);

  ASSERT_TRUE(session_.radial_array().set_copies(kRadialArrayMaximumCopies).ok);
  resolve({}, wheel(1000.0, "Ctrl+Wheel"));
  (void)frame();
  EXPECT_EQ(session_.radial_array().repeats(),
            session_.radial_array().maximum_repeats());
  resolve({}, wheel(-1000.0, "Ctrl+Wheel"));
  (void)frame();
  EXPECT_EQ(session_.radial_array().repeats(), kRadialArrayMinimumRepeats);
}

TEST_F(RadialArrayViewportInputTests,
       CopyAndRepeatFractionsNeverTransferWithinOrBetweenFrames) {
  resolve({}, wheel(0.6));
  (void)frame();
  EXPECT_NEAR(radial_wheel_remainder_, 0.6, 0.000001);
  resolve({}, wheel(0.4, "Ctrl+Wheel"));
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayDefaultCopies);
  EXPECT_EQ(session_.radial_array().repeats(), kRadialArrayDefaultRepeats);
  EXPECT_NEAR(radial_wheel_remainder_, 0.4, 0.000001);
  resolve({}, wheel(0.6, "Ctrl+Wheel"));
  (void)frame();
  EXPECT_EQ(session_.radial_array().repeats(), kRadialArrayDefaultRepeats + 1u);
  resolve({}, wheel(0.7, "Ctrl+Wheel"));
  (void)frame();
  resolve({}, wheel(0.3));
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayDefaultCopies);
  EXPECT_NEAR(radial_wheel_remainder_, 0.3, 0.000001);
  resolve({}, wheel(0.7));
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayDefaultCopies + 1u);
  EXPECT_DOUBLE_EQ(radial_wheel_remainder_, 0.0);

  auto events = wheel(0.8);
  const auto repeat_fraction = wheel(0.8, "Ctrl+Wheel");
  const auto copy_fraction = wheel(0.2);
  events.insert(events.end(), repeat_fraction.begin(), repeat_fraction.end());
  events.insert(events.end(), copy_fraction.begin(), copy_fraction.end());
  resolve({}, events);
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayDefaultCopies + 1u);
  EXPECT_EQ(session_.radial_array().repeats(), kRadialArrayDefaultRepeats + 1u);
  EXPECT_NEAR(radial_wheel_remainder_, 0.2, 0.000001);
}

TEST_F(RadialArrayViewportInputTests,
       RepeatFractionsClearWhenTextNavigationOrFocusBlocksInput) {
  ASSERT_TRUE(session_.radial_array().set_repeats(4u).ok);
  const auto check_blocked = [&](bool &state, bool blocked) {
    const auto repeats_before = session_.radial_array().repeats();
    resolve({}, wheel(0.75, "Ctrl+Wheel"));
    (void)frame();
    EXPECT_NEAR(radial_wheel_remainder_, 0.75, 0.000001);
    state = blocked;
    resolve({}, wheel(1.0, "Ctrl+Wheel"));
    (void)frame();
    EXPECT_EQ(session_.radial_array().repeats(), repeats_before);
    EXPECT_DOUBLE_EQ(radial_wheel_remainder_, 0.0);
    state = !blocked;
    resolve({}, wheel(0.25, "Ctrl+Wheel"));
    (void)frame();
    EXPECT_EQ(session_.radial_array().repeats(), repeats_before);
    resolve({}, wheel(0.75, "Ctrl+Wheel"));
    (void)frame();
    EXPECT_EQ(session_.radial_array().repeats(), repeats_before + 1u);
    EXPECT_DOUBLE_EQ(radial_wheel_remainder_, 0.0);
  };
  check_blocked(input_.text_input_active, true);
  check_blocked(input_.navigation_active, true);
  check_blocked(input_.viewport_focused, false);
}

TEST_F(RadialArrayViewportInputTests,
       PlainWheelRemainsNavigationAndClearsPendingArrayFractions) {
  resolve({}, wheel(0.75));
  (void)frame();
  resolve({}, wheel(1.0, "Wheel"));
  EXPECT_FLOAT_EQ(input_.mouse_wheel, 1.0f);
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayDefaultCopies);
  EXPECT_EQ(session_.radial_array().repeats(), kRadialArrayDefaultRepeats);
  EXPECT_DOUBLE_EQ(radial_wheel_remainder_, 0.0);
  resolve({}, wheel(0.25));
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayDefaultCopies);
}

TEST_F(RadialArrayViewportInputTests,
       RepeatedInstanceTogglesAndRemappedCopyKeysStaySemantic) {
  const bool initial = session_.radial_array().create_instances();
  resolve({chord("I"), chord("I")});
  (void)frame();
  EXPECT_EQ(session_.radial_array().create_instances(), initial);
  resolve({chord("I"), chord("I"), chord("I")});
  (void)frame();
  EXPECT_EQ(session_.radial_array().create_instances(), !initial);
  input_.text_input_active = true;
  resolve({chord("I")});
  (void)frame();
  EXPECT_EQ(session_.radial_array().create_instances(), !initial);
  input_.text_input_active = false;
  input_.navigation_active = true;
  resolve({chord("I")});
  (void)frame();
  EXPECT_EQ(session_.radial_array().create_instances(), !initial);
  input_.navigation_active = false;
  ASSERT_TRUE(editor_.preferences().set_shortcut(
      "viewport.radial_array.increase_copies", "Ctrl+Shift+WheelUp").ok());
  resolve({}, wheel(1.0));
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayDefaultCopies);
  const auto remapped_wheel = wheel(1.0, "Ctrl+Shift+Wheel");
  resolve({}, remapped_wheel);
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayDefaultCopies + 1u);
  ASSERT_TRUE(editor_.preferences().set_shortcut(
      "viewport.radial_array.increase_copies", "K").ok());
  resolve({chord("K"), chord("K")});
  (void)frame();
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayDefaultCopies + 3u);
}

TEST_F(RadialArrayViewportInputTests,
       RepeatShortcutOverridesDriveTheSameBoundedService) {
  ASSERT_TRUE(editor_.preferences().set_shortcut(
      "viewport.radial_array.increase_repeats", "Ctrl+Shift+WheelUp").ok());
  resolve({}, wheel(1.0, "Ctrl+Wheel"));
  (void)frame();
  EXPECT_EQ(session_.radial_array().repeats(), kRadialArrayDefaultRepeats);
  resolve({}, wheel(1.0, "Ctrl+Shift+Wheel"));
  (void)frame();
  EXPECT_EQ(session_.radial_array().repeats(), kRadialArrayDefaultRepeats + 1u);
  ASSERT_TRUE(editor_.preferences().set_shortcut(
      "viewport.radial_array.increase_repeats", "K").ok());
  ASSERT_TRUE(editor_.preferences().set_shortcut(
      "viewport.radial_array.decrease_repeats", "J").ok());
  resolve({chord("K"), chord("K"), chord("J")});
  (void)frame();
  EXPECT_EQ(session_.radial_array().repeats(), kRadialArrayDefaultRepeats + 2u);
  EXPECT_EQ(session_.radial_array().copies(), kRadialArrayDefaultCopies);
}

TEST(LinearArrayPerformanceTests,
     MultiSourceCommitUsesOneSdkBatchAndOneNativeIdSceneScan) {
  constexpr std::uint32_t kElementCount = 65U;
  constexpr std::size_t kSourceCount = 2U;
  constexpr std::size_t kDuplicateCount =
      kSourceCount * (kElementCount - 1U);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> first =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Batch Source A");
  const QuaderApiResult<QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          QuaderBounds{.min = {-0.5f, -0.5f, -0.5f},
                       .max = {0.5f, 0.5f, 0.5f}},
          "Batch Source B");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();

  const ModelingSdkProjectionStats before =
      session.modeling().modeling_sdk_projection_stats_for_tests();
  const std::size_t scene_entities_before = session.scene().entities().size();
  const ModelingLinearArrayResult created =
      session.modeling().create_linear_array({
          .sources =
              {
                  {.entity = first.value().entity,
                   .modeling_object_id =
                       first.value().modeling_object_id},
                  {.entity = second.value().entity,
                   .modeling_object_id =
                       second.value().modeling_object_id},
              },
          .span_world = {64.0F, 0.0F, 0.0F},
          .count = kElementCount,
      });
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.changed);
  ASSERT_EQ(created.source_arrays.size(), kSourceCount);
  ASSERT_EQ(created.source_arrays[0].elements.size(), kElementCount);
  ASSERT_EQ(created.source_arrays[1].elements.size(), kElementCount);

  int previous_duplicate_id = kInvalidModelingObjectId;
  for (const ModelingLinearArraySourceResult &source : created.source_arrays) {
    for (std::size_t index = 1U; index < source.elements.size(); ++index) {
      EXPECT_GT(source.elements[index].modeling_object_id,
                previous_duplicate_id);
      previous_duplicate_id = source.elements[index].modeling_object_id;
    }
  }

  const ModelingSdkProjectionStats after =
      session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after.sdk_duplicate_batch_mutation_count,
            before.sdk_duplicate_batch_mutation_count + 1U);
  EXPECT_EQ(after.sdk_duplicate_object_count,
            before.sdk_duplicate_object_count + kDuplicateCount);
  EXPECT_EQ(after.native_object_id_scene_scan_count,
            before.native_object_id_scene_scan_count + 1U);
  EXPECT_EQ(after.native_object_id_scene_entity_visit_count,
            before.native_object_id_scene_entity_visit_count +
                scene_entities_before);
}

TEST(LinearArrayToolServiceTests,
     RemembersCountAcrossCancellationApplyAndReplacementWithoutPlacement) {
  LinearArraySessionDefaults defaults;
  for (int run = 0; run < 2; ++run) {
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    const auto source = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Remembered Linear Source");
    ASSERT_TRUE(source.ok()) << source.error_message();
    ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
    LinearArrayToolService tool(
        session.scene(), session.assets(), session.history(), session.modeling(),
        session.selection(), session.instances(), &defaults);
    ASSERT_TRUE(tool.activate(1.0).ok);
    if (run == 0) {
      ASSERT_TRUE(tool.set_count(7u).ok);
      ASSERT_TRUE(tool.set_endpoint({99.0f, 0.0f, 0.0f}, false, 1.0).ok);
      ASSERT_TRUE(tool.set_axis_constraint(LinearArrayAxisConstraint::X).ok);
      ASSERT_TRUE(tool.cancel().ok);
      ASSERT_TRUE(tool.activate(1.0).ok);
    }
    EXPECT_EQ(tool.count(), 7u);
    EXPECT_EQ(defaults.count, 7u);
    EXPECT_EQ(tool.handle_state().constraint, LinearArrayAxisConstraint::Free);
    EXPECT_NEAR(tool.handle_state().endpoint_world.x, 12.0f, 0.0001f);
    EXPECT_FALSE(tool.set_count(kLinearArrayMaximumCount + 1u).ok);
    EXPECT_EQ(defaults.count, 7u);
    ASSERT_TRUE(tool.apply().ok);
    ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
    ASSERT_TRUE(tool.activate(1.0).ok);
    EXPECT_EQ(tool.count(), 7u);
  }
  EXPECT_EQ(LinearArraySessionDefaults{}.count, kLinearArrayDefaultCount);
}

TEST(LinearArrayToolServiceTests,
     SourceCapacityDoesNotReplaceTheRememberedCount) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  std::vector<EntityId> sources;
  for (int index = 0; index < 9; ++index) {
    const auto source = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Capacity Source");
    ASSERT_TRUE(source.ok()) << source.error_message();
    sources.push_back(source.value().entity);
  }
  LinearArraySessionDefaults defaults;
  LinearArrayToolService tool(
      session.scene(), session.assets(), session.history(), session.modeling(),
      session.selection(), session.instances(), &defaults);
  ASSERT_TRUE(editor.selection().select_objects({sources.front()}, sources.front()).ok());
  ASSERT_TRUE(tool.activate(1.0).ok);
  ASSERT_TRUE(tool.set_count(kLinearArrayMaximumCount).ok);
  ASSERT_TRUE(tool.cancel().ok);
  ASSERT_TRUE(editor.selection().select_objects(sources, sources.back()).ok());
  ASSERT_TRUE(tool.activate(1.0).ok);
  EXPECT_LT(tool.count(), kLinearArrayMaximumCount);
  EXPECT_EQ(defaults.count, kLinearArrayMaximumCount);
  ASSERT_TRUE(tool.cancel().ok);
  ASSERT_TRUE(editor.selection().select_objects({sources.front()}, sources.front()).ok());
  ASSERT_TRUE(tool.activate(1.0).ok);
  EXPECT_EQ(tool.count(), kLinearArrayMaximumCount);
}

TEST(LinearArrayToolServiceTests,
     ResetRestoresTheDynamicDefaultWithOnePreviewRevision) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> source =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Reset Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());

  LinearArrayToolService &tool = session.linear_array();
  ASSERT_TRUE(tool.activate(1.0).ok);
  ASSERT_TRUE(tool.set_count(7u).ok);
  const std::uint64_t edited_revision = tool.tool_settings_revision();
  ASSERT_EQ(tool.preview_state().sources.size(), 1u);
  EXPECT_EQ(tool.preview_state().sources.front()
                .generated_world_transforms.size(),
            6u);

  const CommandResult reset = tool.reset_settings();
  ASSERT_TRUE(reset.ok) << reset.message;
  EXPECT_TRUE(reset.changed);
  EXPECT_EQ(tool.count(), kLinearArrayDefaultCount);
  EXPECT_EQ(tool.tool_settings_revision(), edited_revision + 1u);
  ASSERT_EQ(tool.preview_state().sources.size(), 1u);
  EXPECT_EQ(tool.preview_state().sources.front()
                .generated_world_transforms.size(),
            3u);

  const CommandResult repeated = tool.reset_settings();
  ASSERT_TRUE(repeated.ok) << repeated.message;
  EXPECT_FALSE(repeated.changed);
  EXPECT_EQ(tool.tool_settings_revision(), edited_revision + 1u);
}

TEST(RadialArrayToolServiceTests,
     ResetRestoresCoupledDefaultsAndAutomaticSpacingInOneRevision) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> source =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Radial Reset Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.scene()
                  .set_transform(source.value().entity,
                                 {.position = {6.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());

  RadialArrayToolService &tool = session.radial_array();
  ASSERT_TRUE(tool.activate(2.5).ok);
  ASSERT_TRUE(
      tool.update_center_candidate({}, {0.0f, 1.0f, 0.0f}, 2.5).ok);
  ASSERT_TRUE(tool.set_degrees(180.0).ok);
  ASSERT_TRUE(tool.set_copies(4u).ok);
  ASSERT_TRUE(tool.set_repeats(2u).ok);
  ASSERT_TRUE(tool.set_repeat_spacing(7.0).ok);
  const std::uint64_t unlocked_revision = tool.tool_settings_revision();

  const CommandResult unlocked_reset = tool.reset_settings();
  ASSERT_TRUE(unlocked_reset.ok) << unlocked_reset.message;
  EXPECT_TRUE(unlocked_reset.changed);
  EXPECT_DOUBLE_EQ(tool.degrees(), kRadialArrayDefaultDegrees);
  EXPECT_EQ(tool.copies(), kRadialArrayDefaultCopies);
  EXPECT_EQ(tool.repeats(), kRadialArrayDefaultRepeats);
  EXPECT_DOUBLE_EQ(tool.repeat_spacing(), 6.0);
  EXPECT_EQ(tool.tool_settings_revision(), unlocked_revision + 1u);

  ASSERT_TRUE(tool.lock_center().ok);
  EXPECT_NEAR(tool.repeat_spacing(), 6.0, 0.0001);
  ASSERT_TRUE(tool.set_degrees(90.0).ok);
  ASSERT_TRUE(tool.set_copies(3u).ok);
  ASSERT_TRUE(tool.set_repeats(3u).ok);
  ASSERT_TRUE(tool.set_repeat_spacing(9.0).ok);
  const std::uint64_t locked_revision = tool.tool_settings_revision();

  const CommandResult locked_reset = tool.reset_settings();
  ASSERT_TRUE(locked_reset.ok) << locked_reset.message;
  EXPECT_TRUE(locked_reset.changed);
  EXPECT_DOUBLE_EQ(tool.degrees(), kRadialArrayDefaultDegrees);
  EXPECT_EQ(tool.copies(), kRadialArrayDefaultCopies);
  EXPECT_EQ(tool.repeats(), kRadialArrayDefaultRepeats);
  EXPECT_NEAR(tool.repeat_spacing(), 6.0, 0.0001);
  EXPECT_EQ(tool.tool_settings_revision(), locked_revision + 1u);
  ASSERT_TRUE(tool.preview_state().active);

  const CommandResult repeated = tool.reset_settings();
  ASSERT_TRUE(repeated.ok) << repeated.message;
  EXPECT_FALSE(repeated.changed);
  EXPECT_EQ(tool.tool_settings_revision(), locked_revision + 1u);
}

TEST(LinearArrayToolServiceTests,
     HandleAnchorsToExplicitPrimaryVisiblePivotForOffOriginGeometry) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> primary =
      editor.modeling().create_box(
          QuaderBounds{.min = {40.0f, 1.0f, -5.0f},
                       .max = {44.0f, 5.0f, -1.0f}},
          "Linear Off-Origin Primary");
  const QuaderApiResult<QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Secondary");
  ASSERT_TRUE(primary.ok()) << primary.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.scene()
                  .set_transform(primary.value().entity,
                                 {.position = {100.0f, 20.0f, -30.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects(
                      {second.value().entity, primary.value().entity},
                      primary.value().entity)
                  .ok());

  const std::optional<QVec3> expected =
      resolve_entity_transform_visible_pivot(
          session.scene(), session.assets(), primary.value().entity);
  ASSERT_TRUE(expected.has_value());
  expect_vec3_near(*expected, {142.0f, 23.0f, -33.0f});
  const QVec3 raw_origin = transform_point(
      session.scene().world_transform_matrix(primary.value().entity), {});
  EXPECT_GT(length_squared(*expected - raw_origin), 1.0f);

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());
  const LinearArrayHandleState handle = session.linear_array().handle_state();
  ASSERT_TRUE(handle.visible);
  expect_vec3_near(handle.source_world, *expected);
  const LinearArrayPreviewState &preview =
      session.linear_array().preview_state();
  ASSERT_EQ(preview.sources.size(), 2u);
  EXPECT_EQ(preview.sources.front().source_entity, second.value().entity);
  EXPECT_EQ(preview.sources.back().source_entity, primary.value().entity);
}

TEST(LinearArrayToolServiceTests,
     HierarchyVisiblePivotChangeInvalidatesTheCapturedPreview) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> source =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Hierarchy Source");
  const QuaderApiResult<QuaderModelingObjectRef> child =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Hierarchy Child");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(child.ok()) << child.error_message();
  ASSERT_TRUE(editor.scene()
                  .reparent(child.value().entity, source.value().entity, false)
                  .ok());
  ASSERT_TRUE(editor.scene()
                  .set_transform(child.value().entity,
                                 {.position = {10.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());
  expect_vec3_near(session.linear_array().handle_state().source_world,
                   {5.0f, 0.0f, 0.0f});

  ASSERT_TRUE(editor.scene()
                  .set_transform(child.value().entity,
                                 {.position = {20.0f, 0.0f, 0.0f}})
                  .ok());
  const CommandResult refreshed = session.linear_array().refresh();
  EXPECT_TRUE(refreshed.ok);
  EXPECT_TRUE(refreshed.changed);
  EXPECT_FALSE(session.linear_array().has_session());
  EXPECT_FALSE(session.linear_array().preview_state().active);
}

TEST(LinearArrayToolServiceTests,
     MultiSourcePreviewCommitAndWheelAdjustmentUseIdenticalMatrices) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> primary =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Primary Source");
  const QuaderApiResult<QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Second Source");
  ASSERT_TRUE(primary.ok()) << primary.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.scene()
                  .set_transform(second.value().entity,
                                 {.position = {10.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects(
                      {primary.value().entity, second.value().entity},
                      primary.value().entity)
                  .ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());

  LinearArrayToolService &tool = session.linear_array();
  const LinearArrayHandleState handle = tool.handle_state();
  ASSERT_TRUE(handle.visible);
  expect_vec3_near(handle.source_world, {});
  expect_vec3_near(handle.endpoint_world, {36.0f, 0.0f, 0.0f});
  ASSERT_TRUE(tool.adjust_count(2).ok);
  EXPECT_EQ(tool.count(), 6u);
  ASSERT_TRUE(tool.adjust_count(-100).ok);
  EXPECT_EQ(tool.count(), 2u);
  EXPECT_FALSE(tool.adjust_count(-1).changed);
  ASSERT_TRUE(tool.set_count(3u).ok);

  const LinearArrayPreviewState &preview = tool.preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.sources.size(), 2u);
  EXPECT_EQ(preview.sources[0].source_entity, second.value().entity);
  EXPECT_EQ(preview.sources[1].source_entity, primary.value().entity);
  ASSERT_EQ(preview.sources[0].generated_world_transforms.size(), 2u);
  ASSERT_EQ(preview.sources[1].generated_world_transforms.size(), 2u);
  const QMat4 *const cached_transform_storage =
      preview.sources[0].generated_world_transforms.data();
  const LinearArrayPreviewState &unchanged_preview = tool.preview_state();
  EXPECT_EQ(&unchanged_preview, &preview);
  EXPECT_EQ(unchanged_preview.sources[0].generated_world_transforms.data(),
            cached_transform_storage);
  std::vector<QMat4> preview_world;
  for (const LinearArrayPreviewSource &source : preview.sources) {
    preview_world.insert(preview_world.end(),
                         source.generated_world_transforms.begin(),
                         source.generated_world_transforms.end());
  }

  const QuaderApiVoidResult committed =
      editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(committed.changed());
  EXPECT_FALSE(tool.has_session());
  EXPECT_EQ(editor.query().history().undo_label, "Linear Array");

  std::vector<EntityId> generated;
  for (const EntityId entity : session.scene().entities()) {
    if (entity != primary.value().entity && entity != second.value().entity &&
        session.scene().modeling_object(entity) != nullptr) {
      generated.push_back(entity);
    }
  }
  std::sort(generated.begin(), generated.end());
  ASSERT_EQ(generated.size(), preview_world.size());
  for (std::size_t index = 0; index < generated.size(); ++index) {
    const QMat4 committed_world =
        session.scene().world_transform_matrix(generated[index]);
    for (std::size_t component = 0;
         component < committed_world.values.size(); ++component) {
      EXPECT_NEAR(committed_world.values[component],
                  preview_world[index].values[component], 0.0001f);
    }
  }
  EXPECT_EQ(session.scene().selected_entities().size(), 6u);
  EXPECT_EQ(session.scene().primary_selection(), generated.back());
}

TEST(LinearArrayToolServiceTests,
     AnySourceWorldTransformChangeInvalidatesTheCapturedPreview) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> source =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Stale Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());
  ASSERT_TRUE(session.linear_array().preview_state().active);

  ASSERT_TRUE(editor.scene()
                  .set_transform(source.value().entity,
                                 {.position = {0.000001f, 0.0f, 0.0f}})
                  .ok());
  const CommandResult refreshed = session.linear_array().refresh();
  EXPECT_TRUE(refreshed.ok);
  EXPECT_TRUE(refreshed.changed);
  EXPECT_FALSE(session.linear_array().has_session());
  EXPECT_FALSE(session.linear_array().preview_state().active);
}

TEST(LinearArrayToolServiceTests,
     UnchangedNativeArrayPreviewDoesNotResubmitRenderExtraction) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> source =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Cached Preview");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());

  SceneRenderBridge bridge;
  std::size_t submission_count = 0u;
  const QuaderRenderApiRuntimeAccess::ToolPreviewPublisher publisher =
      [&submission_count](std::vector<RenderToolPreviewMeshRecord>) {
        ++submission_count;
        return ToolPreviewSubmissionResult::Published;
      };
  const auto sync_preview = [&]() {
    return QuaderRenderApiRuntimeAccess::sync_tool_previews(
        editor.render(), bridge, publisher);
  };
  ASSERT_TRUE(sync_preview().ok());
  const std::size_t initial_submission = submission_count;
  ASSERT_TRUE(sync_preview().ok());
  EXPECT_EQ(submission_count, initial_submission);

  TextureAsset texture;
  texture.name = "Array Preview Asset Revision";
  texture.width = 1u;
  texture.height = 1u;
  texture.generated = true;
  ASSERT_NE(session.assets().add_texture(std::move(texture)),
            kInvalidAssetId);
  ASSERT_TRUE(sync_preview().ok());
  EXPECT_GT(submission_count, initial_submission);
  const std::size_t asset_submission = submission_count;

  ASSERT_TRUE(session.linear_array().set_count(5u).ok);
  ASSERT_TRUE(sync_preview().ok());
  EXPECT_GT(submission_count, asset_submission);
  const std::size_t changed_submission = submission_count;

  ASSERT_TRUE(session.linear_array().cancel().ok);
  ASSERT_TRUE(sync_preview().ok());
  EXPECT_GT(submission_count, changed_submission);
}

TEST(LinearArrayToolServiceTests,
     CancelAndReactivateCannotReuseAnOldRenderPreviewRevision) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> first =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear First Session");
  const QuaderApiResult<QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Second Session");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection()
                  .select_objects({first.value().entity}, first.value().entity)
                  .ok());

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());
  LinearArrayToolService &tool = session.linear_array();
  SceneRenderBridge bridge;
  std::size_t submission_count = 0u;
  const QuaderRenderApiRuntimeAccess::ToolPreviewPublisher publisher =
      [&submission_count](std::vector<RenderToolPreviewMeshRecord>) {
        ++submission_count;
        return ToolPreviewSubmissionResult::Published;
      };
  const auto sync_preview = [&]() {
    return QuaderRenderApiRuntimeAccess::sync_tool_previews(
        editor.render(), bridge, publisher);
  };
  ASSERT_TRUE(sync_preview().ok());
  const std::size_t first_submission = submission_count;

  ASSERT_TRUE(tool.cancel().ok);
  ASSERT_TRUE(editor.selection()
                  .select_objects({second.value().entity},
                                  second.value().entity)
                  .ok());
  ASSERT_TRUE(tool.activate(1.0).ok);
  ASSERT_TRUE(sync_preview().ok());
  EXPECT_GT(submission_count, first_submission);
  const LinearArrayPreviewState &preview = tool.preview_state();
  ASSERT_EQ(preview.sources.size(), 1u);
  EXPECT_EQ(preview.sources.front().source_entity, second.value().entity);
}

TEST(LinearArrayToolServiceTests,
     LinkedInstanceOccurrencesPreviewMembersAndUndoAsOneHistoryItem) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderEntityRef> parent =
      editor.scene().create_entity(QuaderCreateEntityArgs{
          .name = "Linear Instance Parent",
          .kind = QuaderEntityKind::Empty,
      });
  const QuaderApiResult<QuaderModelingObjectRef> first =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Instance Member A");
  const QuaderApiResult<QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          QuaderBounds{.min = {-0.5f, -0.5f, -0.5f},
                       .max = {0.5f, 0.5f, 0.5f}},
          "Linear Instance Member B");
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.scene()
                  .set_transform(second.value().entity,
                                 {.position = {4.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects(
                      {first.value().entity, second.value().entity},
                      first.value().entity)
                  .ok());
  ASSERT_TRUE(editor.modeling().create_instance().ok());

  ASSERT_EQ(session.scene().selected_entities().size(), 1u);
  const EntityId occurrence = session.scene().selected_entities().front();
  const InstanceOccurrenceComponent *source_component =
      session.scene().instance_occurrence(occurrence);
  ASSERT_NE(source_component, nullptr);
  const EntityId definition = source_component->definition;
  const std::vector<EntityId> members = session.scene().children(definition);
  ASSERT_EQ(members.size(), 2u);

  ASSERT_TRUE(editor.scene()
                  .set_transform(parent.value().id,
                                 {.position = {7.0f, 1.0f, -2.0f}})
                  .ok());
  ASSERT_TRUE(editor.scene().reparent(occurrence, parent.value().id, false).ok());
  ASSERT_TRUE(editor.scene()
                  .set_transform(occurrence,
                                 {.position = {3.0f, 2.0f, 1.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects({occurrence}, occurrence)
                  .ok());

  LinearArrayToolService &tool = session.linear_array();
  ASSERT_TRUE(tool.activate(1.0).ok);
  ASSERT_TRUE(tool.set_count(3u).ok);
  const LinearArrayHandleState handle = tool.handle_state();
  ASSERT_TRUE(handle.visible);
  ASSERT_TRUE(tool.set_endpoint(handle.source_world + QVec3{8.0f, 0.0f, 0.0f},
                                false, 1.0)
                  .ok);
  const LinearArrayPreviewState preview = tool.preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.sources.size(), members.size());
  for (const LinearArrayPreviewSource &source : preview.sources) {
    EXPECT_EQ(source.visibility_entity, occurrence);
    EXPECT_EQ(source.source_entity, source.material_entity);
    EXPECT_EQ(source.generated_world_transforms.size(), 2u);
  }

  const QuaderApiVoidResult committed =
      editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(committed.changed());
  ASSERT_NE(session.history().undo_label(), nullptr);
  EXPECT_EQ(*session.history().undo_label(), "Linear Array");

  const std::vector<EntityId> selected = session.scene().selected_entities();
  ASSERT_EQ(selected.size(), 3u);
  RenderOverlaySnapshotCache bounds_cache;
  const RenderOverlaySelectionSnapshot &selection_bounds =
      bounds_cache.selection_snapshot(session.scene(), session.assets());
  ASSERT_EQ(selection_bounds.selected_mesh_bounds.size(), selected.size());
  for (std::size_t index = 0; index < selected.size(); ++index) {
    const std::optional<SceneBounds> expected = entity_world_bounds(
        session.scene(), session.assets(), selected[index]);
    ASSERT_TRUE(expected.has_value());
    expect_vec3_near(selection_bounds.selected_mesh_bounds[index].min,
                     expected->min);
    expect_vec3_near(selection_bounds.selected_mesh_bounds[index].max,
                     expected->max);
  }
  const ViewportOverlayFrame bounds_frame =
      ViewportSceneOverlayBuilder::build_selection_frame(
          {}, selection_bounds, false, RenderOverlaySelectionMode::Object);
  EXPECT_EQ(std::ranges::count_if(
                bounds_frame.lines,
                [](const ViewportOverlayLine &line) {
                  return line.layer == ViewportOverlayLayer::GroupBounds;
                }),
            12);
  std::vector<EntityId> copies;
  for (const EntityId entity : selected) {
    if (entity != occurrence) {
      copies.push_back(entity);
    }
  }
  ASSERT_EQ(copies.size(), 2u);
  ASSERT_NE(session.scene().name(occurrence), nullptr);
  const std::string source_name = session.scene().name(occurrence)->name;
  for (std::size_t copy_index = 0; copy_index < copies.size(); ++copy_index) {
    const EntityId copy = copies[copy_index];
    const InstanceOccurrenceComponent *component =
        session.scene().instance_occurrence(copy);
    ASSERT_NE(component, nullptr);
    EXPECT_EQ(component->definition, definition);
    EXPECT_EQ(session.scene().parent(copy), parent.value().id);
    ASSERT_NE(session.scene().visibility(copy), nullptr);
    EXPECT_TRUE(session.scene().visibility(copy)->visible);
    ASSERT_NE(session.scene().lock(copy), nullptr);
    EXPECT_FALSE(session.scene().lock(copy)->locked);
    ASSERT_NE(session.scene().name(copy), nullptr);
    EXPECT_NE(session.scene().name(copy)->name.find(source_name),
              std::string::npos);

    for (const EntityId member : members) {
      const MeshComponent *member_mesh = session.scene().mesh(member);
      ASSERT_NE(member_mesh, nullptr);
      const auto source = std::find_if(
          preview.sources.begin(), preview.sources.end(),
          [member_mesh](const LinearArrayPreviewSource &candidate) {
            return candidate.mesh == member_mesh->mesh;
          });
      ASSERT_NE(source, preview.sources.end());
      expect_matrix_near(
          effective_instance_member_world(session.scene(), copy, definition,
                                          member),
          source->generated_world_transforms[copy_index]);
    }
  }

  ASSERT_TRUE(session.history().undo().ok);
  EXPECT_EQ(session.scene().selected_entities(),
            std::vector<EntityId>{occurrence});
  EXPECT_EQ(bounds_cache
                .selection_snapshot(session.scene(), session.assets())
                .selected_mesh_bounds.size(),
            1u);
  for (const EntityId copy : copies) {
    EXPECT_FALSE(session.scene().exists(copy));
  }
  ASSERT_TRUE(session.history().redo().ok);
  EXPECT_EQ(session.scene().selected_entities().size(), 3u);
  EXPECT_EQ(bounds_cache
                .selection_snapshot(session.scene(), session.assets())
                .selected_mesh_bounds.size(),
            3u);
  for (const EntityId copy : copies) {
    ASSERT_TRUE(session.scene().exists(copy));
    ASSERT_NE(session.scene().instance_occurrence(copy), nullptr);
    EXPECT_EQ(session.scene().instance_occurrence(copy)->definition,
              definition);
  }
}

TEST(RadialArrayToolServiceTests,
     LinkedInstanceOccurrencesUsePreviewMatricesAndStableRedoIdentities) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderModelingObjectRef member =
      create_asymmetric_array_mesh(editor, "Radial Instance Member");
  ASSERT_NE(member.entity, kInvalidEntityId);
  ASSERT_TRUE(editor.modeling().create_instance().ok());

  ASSERT_EQ(session.scene().selected_entities().size(), 1u);
  const EntityId occurrence = session.scene().selected_entities().front();
  const InstanceOccurrenceComponent *source_component =
      session.scene().instance_occurrence(occurrence);
  ASSERT_NE(source_component, nullptr);
  const EntityId definition = source_component->definition;
  const std::vector<EntityId> members = session.scene().children(definition);
  ASSERT_EQ(members.size(), 1u);
  ASSERT_TRUE(editor.scene()
                  .set_transform(occurrence,
                                 {.position = {4.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects({occurrence}, occurrence)
                  .ok());

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::RadialArray).ok());
  RadialArrayToolService &tool = session.radial_array();
  ASSERT_TRUE(tool.update_center_candidate({}, {0.0f, 1.0f, 0.0f}, 1.0).ok);
  ASSERT_TRUE(tool.set_degrees(150.0).ok);
  ASSERT_TRUE(tool.set_copies(3u).ok);
  ASSERT_TRUE(tool.set_create_instances(true).ok);
  ASSERT_TRUE(tool.lock_center().ok);
  const RadialArrayPreviewState preview = tool.preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.sources.size(), 1u);
  ASSERT_EQ(preview.sources.front().generated_world_transforms.size(), 2u);

  const QuaderApiVoidResult committed =
      editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(committed.changed());
  ASSERT_NE(session.history().undo_label(), nullptr);
  EXPECT_EQ(*session.history().undo_label(), "Radial Array");

  const std::vector<EntityId> selected = session.scene().selected_entities();
  ASSERT_EQ(selected.size(), 3u);
  RenderOverlaySnapshotCache bounds_cache;
  const RenderOverlaySelectionSnapshot &selection_bounds =
      bounds_cache.selection_snapshot(session.scene(), session.assets());
  ASSERT_EQ(selection_bounds.selected_mesh_bounds.size(), selected.size());
  EXPECT_EQ(
      bounds_cache.stats()
          .selection_instance_dependency_signature_build_count,
      1u);
  const MeshComponent *member_component =
      session.scene().mesh(members.front());
  ASSERT_NE(member_component, nullptr);
  const MeshAsset *member_mesh =
      session.assets().mesh(member_component->mesh);
  ASSERT_NE(member_mesh, nullptr);
  bool found_rotated_extent_tighter_than_local_box = false;
  for (std::size_t index = 0; index < selected.size(); ++index) {
    const QMat4 member_world = effective_instance_member_world(
        session.scene(), selected[index], definition, members.front());
    const SceneBounds expected = exact_transformed_geometry_bounds(
        *member_mesh, member_world);
    expect_vec3_near(selection_bounds.selected_mesh_bounds[index].min,
                     expected.min);
    expect_vec3_near(selection_bounds.selected_mesh_bounds[index].max,
                     expected.max);
    const std::optional<SceneBounds> conservative =
        world_bounds_from_local_mesh_bounds(
            member_world, local_mesh_bounds(*member_mesh));
    ASSERT_TRUE(conservative.has_value());
    found_rotated_extent_tighter_than_local_box =
        found_rotated_extent_tighter_than_local_box ||
        conservative->min.x < expected.min.x - 0.01f ||
        conservative->min.z < expected.min.z - 0.01f ||
        conservative->max.x > expected.max.x + 0.01f ||
        conservative->max.z > expected.max.z + 0.01f;
  }
  EXPECT_TRUE(found_rotated_extent_tighter_than_local_box);
  const ViewportOverlayFrame bounds_frame =
      ViewportSceneOverlayBuilder::build_selection_frame(
          {}, selection_bounds, false, RenderOverlaySelectionMode::Object);
  EXPECT_EQ(std::ranges::count_if(
                bounds_frame.lines,
                [](const ViewportOverlayLine &line) {
                  return line.layer == ViewportOverlayLayer::GroupBounds;
                }),
            12);
  std::vector<EntityId> copies;
  for (const EntityId entity : selected) {
    if (entity != occurrence) {
      copies.push_back(entity);
    }
  }
  ASSERT_EQ(copies.size(), 2u);
  for (std::size_t index = 0; index < copies.size(); ++index) {
    const InstanceOccurrenceComponent *component =
        session.scene().instance_occurrence(copies[index]);
    ASSERT_NE(component, nullptr);
    EXPECT_EQ(component->definition, definition);
    expect_matrix_near(
        effective_instance_member_world(session.scene(), copies[index],
                                        definition, members.front()),
        preview.sources.front().generated_world_transforms[index]);
  }

  ASSERT_TRUE(session.history().undo().ok);
  EXPECT_EQ(session.scene().selected_entities(),
            std::vector<EntityId>{occurrence});
  EXPECT_EQ(bounds_cache
                .selection_snapshot(session.scene(), session.assets())
                .selected_mesh_bounds.size(),
            1u);
  for (const EntityId copy : copies) {
    EXPECT_FALSE(session.scene().exists(copy));
  }
  ASSERT_TRUE(session.history().redo().ok);
  EXPECT_EQ(session.scene().selected_entities().size(), 3u);
  EXPECT_EQ(bounds_cache
                .selection_snapshot(session.scene(), session.assets())
                .selected_mesh_bounds.size(),
            3u);
  for (const EntityId copy : copies) {
    ASSERT_TRUE(session.scene().exists(copy));
    ASSERT_NE(session.scene().instance_occurrence(copy), nullptr);
    EXPECT_EQ(session.scene().instance_occurrence(copy)->definition,
              definition);
  }
}

TEST(LinearArrayToolServiceTests,
     MixedOrdinaryMeshesAndOccurrencesAreRejectedExplicitly) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> member =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Mixed Array Instance Member");
  ASSERT_TRUE(member.ok()) << member.error_message();
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const EntityId occurrence = session.scene().selected_entities().front();
  const QuaderApiResult<QuaderModelingObjectRef> ordinary =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Mixed Array Ordinary Mesh");
  ASSERT_TRUE(ordinary.ok()) << ordinary.error_message();
  ASSERT_TRUE(editor.selection()
                  .select_objects({occurrence, ordinary.value().entity},
                                  occurrence)
                  .ok());

  EXPECT_FALSE(session.linear_array().can_activate());
  EXPECT_FALSE(session.radial_array().can_activate());
  const QuaderApiVoidResult activated =
      editor.tools().activate(ViewportToolId::LinearArray);
  EXPECT_FALSE(activated.ok());
}

} // namespace
} // namespace quader
