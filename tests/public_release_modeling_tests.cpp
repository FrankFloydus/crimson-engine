////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "support/behavior_scenario.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "core/release_features.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/tool_session_defaults.hpp"
#include "support/command_observable_state.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string_view>
#include <utility>

namespace {

using namespace quader;

/// Captures every observable domain, including selection, previews and revisions.
template <typename Action>
void expect_release_rejection(QuaderEditorApi &editor, Action action) {
  const auto before = test::capture_command_observable_state(editor);
  const auto result = action();
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_EQ(result.error_message(), release_feature_unavailable_message());
  const auto difference = test::diff_command_observable_state(
      before, test::capture_command_observable_state(editor));
  EXPECT_TRUE(difference.empty()) << difference.summary;
}

class PublicReleaseModelingTests : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(current_product_release(), kPublicRelease01);
    const auto first_result = editor().modeling().create_box_mesh(
        QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Retained selection");
    ASSERT_TRUE(first_result.ok()) << first_result.error_message();
    first_ = first_result.value();
    const auto second_result = editor().modeling().create_box_mesh(
        QuaderBounds{.min = {3.0f, 0.0f, 0.0f}, .max = {5.0f, 2.0f, 2.0f}},
        "Restricted action target");
    ASSERT_TRUE(second_result.ok()) << second_result.error_message();
    second_ = second_result.value();
    ASSERT_TRUE(first_.select().replace().ok());
  }

  QuaderEditorApi &editor() { return standalone_.editor(); }
  QuaderStandaloneEditor standalone_;
  QuaderMeshHandle first_;
  QuaderMeshHandle second_;
};

TEST_F(PublicReleaseModelingTests,
       CuttingToolsRequireEditModeAndSwitchToEdgeWithinIt) {
  for (const bool knife : {false, true}) {
    SCOPED_TRACE(knife);
    ASSERT_TRUE(editor().mode().set(QuaderEditorMode::Object).ok());
    ASSERT_TRUE(editor().tools().activate(ViewportToolId::Select).ok());
    ASSERT_TRUE(editor().selection().set_mode(QuaderSelectionMode::Object).ok());
    const auto command_id = knife ? quader_command::kToolsActivateKnife
                                  : quader_command::kToolsActivateInsertEdgeLoop;
    const auto tool = knife ? ViewportToolId::Knife : ViewportToolId::InsertEdgeLoop;
    const auto command = editor().commands().descriptor(command_id);
    ASSERT_TRUE(command.has_value());
    EXPECT_FALSE(command->enabled);
    const auto before_rejected = test::capture_command_observable_state(editor());
    const auto rejected = editor().commands().execute(command_id);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    const auto rejected_difference = test::diff_command_observable_state(
        before_rejected, test::capture_command_observable_state(editor()));
    EXPECT_TRUE(rejected_difference.empty()) << rejected_difference.summary;
    ASSERT_TRUE(editor().mode().set(QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor().commands().descriptor(command_id)->enabled);
    const auto history_before = editor().history().snapshot();
    const auto content_before = editor().selection().summary().content_revision;
    const auto result = editor().commands().execute(command_id);
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_EQ(editor().selection().summary().mode, QuaderSelectionMode::Edge);
    EXPECT_EQ(editor().tools().active_tool(), knife ? "knife" : "insert_edge_loop");
    EXPECT_EQ(editor().viewports().selected_tool(), tool);
    EXPECT_EQ(editor().selection().summary().content_revision, content_before);
    EXPECT_EQ(editor().history().snapshot().undo_label, history_before.undo_label);
    ASSERT_TRUE(editor().mode().set(QuaderEditorMode::Object).ok());
    ASSERT_TRUE(first_.select().replace().ok());
    ASSERT_EQ(editor().selection().summary().mode, QuaderSelectionMode::Object);
    ASSERT_TRUE(editor().mode().set(QuaderEditorMode::Edit).ok());
    const auto history_after_selection = editor().history().snapshot();
    const auto reactivated = editor().commands().execute(command_id);
    ASSERT_TRUE(reactivated.ok()) << reactivated.error_message();
    EXPECT_TRUE(reactivated.changed());
    EXPECT_EQ(editor().selection().summary().mode, QuaderSelectionMode::Edge);
    EXPECT_EQ(editor().viewports().selected_tool(), tool);
    EXPECT_EQ(editor().selection().summary().content_revision, content_before);
    EXPECT_EQ(editor().history().snapshot().undo_label, history_after_selection.undo_label);
  }
}

TEST_F(PublicReleaseModelingTests,
       ComponentHelpersRejectBeforeReplacingSelectionOrCancelingPreview) {
  ASSERT_TRUE(editor().modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor().modeling().update_transform_translate({0.5f, 0.0f, 0.0f}).ok());
  const auto vertices = second_.vertices().only({0, 1, 2});
  const auto edges = second_.edges().only({{0, 1}, {1, 2}});
  const auto faces = second_.faces().only({0});
  expect_release_rejection(editor(), [&] { return vertices.radial_align(); });
  expect_release_rejection(editor(), [&] { return edges.radial_align(); });
  expect_release_rejection(editor(), [&] { return faces.radial_align(); });
  expect_release_rejection(editor(), [&] { return vertices.bevel(); });
  expect_release_rejection(editor(), [&] { return edges.bevel({}).begin_result(); });
  expect_release_rejection(editor(), [&] { return faces.slice({}).begin_result(); });
  expect_release_rejection(editor(), [&] { return faces.thicken({}).begin_result(); });
  expect_release_rejection(editor(), [&] { return second_.select().snap_to_floor(); });
  expect_release_rejection(editor(), [&] { return vertices.snap_to_floor(); });
  expect_release_rejection(editor(), [&] { return faces.assign_material_slot(1u); });
  expect_release_rejection(editor(), [&] { return second_.materials().assign_slot(1u); });
  ASSERT_TRUE(editor().modeling().cancel_transform_preview().ok());
}

TEST_F(PublicReleaseModelingTests, InstancingRejectionsPreservePendingToolAndPreview) {
  auto &modeling = editor().modeling();
  const auto expect_rejections = [&] {
    EXPECT_FALSE(modeling.can_create_instance_from_selection());
    EXPECT_FALSE(modeling.can_flatten_selected_instances());
    EXPECT_FALSE(modeling.can_enter_instance_edit(first_.id()));
    EXPECT_FALSE(modeling.can_exit_instance_edit());
    expect_release_rejection(editor(), [&] { return modeling.create_instance_from_selection(); });
    expect_release_rejection(editor(), [&] { return modeling.flatten_selected_instances(); });
    expect_release_rejection(editor(), [&] { return modeling.enter_instance_edit(first_.id()); });
    expect_release_rejection(editor(), [&] { return modeling.exit_instance_edit(); });
  };
  ASSERT_TRUE(modeling.begin_transform_preview().ok());
  ASSERT_TRUE(modeling.update_transform_translate({0.5f, 0.0f, 0.0f}).ok());
  expect_rejections();
  ASSERT_TRUE(modeling.cancel_transform_preview().ok());
  ASSERT_TRUE(editor().tools().activate(ViewportToolId::CreatePrimitive).ok());
  expect_rejections();
}

TEST_F(PublicReleaseModelingTests, PlacementBakingAndPivotCannotMutateSelection) {
  auto &modeling = editor().modeling();
  EXPECT_FALSE(modeling.can_snap_selected_meshes_to_floor());
  EXPECT_FALSE(modeling.can_snap_selected_vertices_to_floor());
  EXPECT_FALSE(modeling.can_align_selected_meshes());
  EXPECT_FALSE(modeling.can_distribute_selected_meshes());
  EXPECT_FALSE(modeling.can_copy_pivot_from_active());
  expect_release_rejection(editor(), [&] { return modeling.snap_selected_meshes_to_floor(); });
  expect_release_rejection(editor(), [&] { return modeling.snap_selected_vertices_to_floor(); });
  expect_release_rejection(editor(), [&] {
    return modeling.align_selected_meshes(QuaderTransformAxis::X, QuaderMeshAlignment::Center);
  });
  expect_release_rejection(editor(), [&] {
    return modeling.distribute_selected_meshes(QuaderTransformAxis::X, QuaderMeshDistribution::Gaps);
  });
  expect_release_rejection(editor(), [&] { return modeling.copy_pivot_from_active(); });
  for (const auto mode : {QuaderTransformBakeMode::Transforms,
                          QuaderTransformBakeMode::Location,
                          QuaderTransformBakeMode::Rotation,
                          QuaderTransformBakeMode::Scale}) {
    EXPECT_FALSE(modeling.can_bake_transforms(mode));
    expect_release_rejection(editor(), [&] { return modeling.bake_transforms(mode); });
  }
}

TEST_F(PublicReleaseModelingTests, RestrictedOperationEntryPointsPreserveState) {
  auto &modeling = editor().modeling();
  for (const std::string_view action : {
           "bevel_vertices", "bevel_edges", "slice_quad", "thicken_faces",
           "create_outer_hull", "half_cut", "radial_align_vertices",
           "radial_align_edges", "radial_align_faces", "mirror_selection_x",
           "mirror_selection_y", "mirror_selection_z", "flip_horizontal",
           "flip_vertical", "cut_tool", "poly_tool", "mirror_tool"}) {
    SCOPED_TRACE(action);
    EXPECT_FALSE(modeling.can_execute_tool(action));
    expect_release_rejection(editor(), [&] { return modeling.execute_tool(action); });
    expect_release_rejection(editor(), [&] { return modeling.begin_preview(action); });
  }
  expect_release_rejection(editor(), [&] {
    return modeling.cut_selected_meshes_by_hovered_face_plane();
  });
  expect_release_rejection(editor(), [&] { return modeling.create_curve({}); });
  expect_release_rejection(editor(), [&] { return modeling.create_linear_array({}); });
  expect_release_rejection(editor(), [&] { return modeling.create_radial_array({}); });
  expect_release_rejection(editor(), [&] { return modeling.begin_curve_preview(first_.ref()); });
  expect_release_rejection(editor(), [&] { return modeling.set_curve_closed(first_.ref(), true); });
}

TEST_F(PublicReleaseModelingTests, MaterialAndUvWritesCannotDisturbGeometry) {
  auto &modeling = editor().modeling();
  expect_release_rejection(editor(), [&] { return modeling.apply_material({}); });
  expect_release_rejection(editor(), [&] { return modeling.assign_material_slot_to_selection(1u); });
  expect_release_rejection(editor(), [&] { return modeling.assign_material_slot_to_face(first_.ref(), 0, 1u); });
  expect_release_rejection(editor(), [&] { return modeling.set_face_texture_setting({}); });
  expect_release_rejection(editor(), [&] { return modeling.apply_face_texture_action({}); });
  expect_release_rejection(editor(), [&] { return modeling.transfer_face_texture_to_face(first_.id(), 0); });
  expect_release_rejection(editor(), [&] { return modeling.begin_face_texture_gesture({}); });
  expect_release_rejection(editor(), [&] { return modeling.set_face_texture_treat_as_one(true); });
  expect_release_rejection(editor(), [&] { return modeling.set_uv_peel_options({}); });
  expect_release_rejection(editor(), [&] { return modeling.peel_uv({}); });
  EXPECT_FALSE(modeling.can_project_uv());
  EXPECT_FALSE(modeling.face_texture_settings().editable);
  EXPECT_FALSE(modeling.face_texture_canvas().editable);
  EXPECT_FALSE(modeling.uv_peel_settings().editable);
  QuaderPreviewSettings settings;
  settings.interaction.edge_bevel_width = 2.0;
  expect_release_rejection(editor(), [&] { return modeling.update_preview(settings); });
}

TEST_F(PublicReleaseModelingTests, CustomMeshCreationPreservesPassiveUvData) {
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {0.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(document, {2.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(document, {0.0f, 2.0f, 0.0f}),
  };
  const auto face = quader_poly::add_face(document, vertices);
  ASSERT_NE(face, quader_poly::kInvalidElementId);
  document.faces.front().uvs = {{0.0f, 0.0f}, {2.0f, 0.0f}, {0.0f, 3.0f}};
  document.faces.front().texture_transform.scale = {1.5f, 0.75f};
  const auto created = editor().modeling().create_mesh_handle(
      QuaderPolygonDocumentInternalAccess::from_backend(document),
      {.name = "Mesh with passive UVs"});
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto compiled = created.value().compiled_mesh();
  ASSERT_TRUE(compiled.has_value());
  ASSERT_EQ(compiled->face_textures.size(), 1u);
  EXPECT_FLOAT_EQ(compiled->face_textures.front().scale.x, 1.5f);
  EXPECT_FLOAT_EQ(compiled->face_textures.front().scale.y, 0.75f);
  ASSERT_TRUE(created.value().transform().translate({0.0f, 0.0f, 1.0f}).ok());
  const auto moved = created.value().compiled_mesh();
  ASSERT_TRUE(moved.has_value());
  ASSERT_EQ(moved->face_textures.size(), 1u);
  EXPECT_FLOAT_EQ(moved->face_textures.front().scale.x, 1.5f);
  EXPECT_FLOAT_EQ(moved->face_textures.front().scale.y, 0.75f);
}

TEST_F(PublicReleaseModelingTests, PrimitiveChoicesAndEditsRespectTheRelease) {
  auto &tools = editor().tools();
  ASSERT_TRUE(editor().modeling().begin_transform_preview().ok());
  for (const auto shape : {QuaderPrimitiveCreationShape::Cone,
                           QuaderPrimitiveCreationShape::Torus}) {
    expect_release_rejection(editor(), [&] { return tools.begin_primitive_creation(shape); });
  }
  for (const auto command : {quader_command::kToolsActivateCreateCone,
                             quader_command::kToolsActivateCreateTorus}) {
    expect_release_rejection(editor(), [&] { return editor().commands().execute(command); });
  }
  ASSERT_TRUE(editor().modeling().cancel_transform_preview().ok());
  const auto activated = tools.begin_primitive_creation(QuaderPrimitiveCreationShape::Sphere);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  for (const auto type : {"cone", "torus"}) {
    expect_release_rejection(editor(), [&] {
      return tools.set_setting("create_primitive", "primitive.type", type);
    });
  }
  for (const double type : {3.0, 4.0}) {
    expect_release_rejection(editor(), [&] {
      return tools.set_setting("create_primitive", "primitive.type", type);
    });
  }
  expect_release_rejection(editor(), [&] {
    return tools.set_setting("create_primitive", "primitive.sphere.type", "quad");
  });
  expect_release_rejection(editor(), [&] {
    return tools.set_setting("create_primitive", "primitive.sphere.type", 1.0);
  });
  expect_release_rejection(editor(), [&] {
    return tools.set_setting("create_primitive", "primitive.sphere.subdivision", 4.0);
  });
  const auto settings = tools.active_settings();
  bool found_primitive_type = false;
  bool found_sphere_type = false;
  for (const auto &control : settings.controls) {
    if (control.id == "primitive.type") {
      found_primitive_type = true;
    }
    if (control.id == "primitive.sphere.type") {
      found_sphere_type = true;
      ASSERT_EQ(control.choices.size(), 1u);
      EXPECT_EQ(control.choices.front().id, "uv");
    }
  }
  EXPECT_FALSE(found_primitive_type);
  EXPECT_EQ(settings.title, "Create Sphere");
  EXPECT_EQ(tools.preferred_primitive_shape(), QuaderPrimitiveCreationShape::Sphere);
  EXPECT_TRUE(found_sphere_type);
  EXPECT_TRUE(tools.cancel_active_tool_preview().ok());
  EXPECT_EQ(tools.preferred_primitive_shape(), QuaderPrimitiveCreationShape::Sphere);
}

TEST_F(PublicReleaseModelingTests,
       UnavailablePrimitiveLaunchesPreserveDrawingAndRememberedShape) {
  for (const auto unavailable : {PrimitiveCreationKind::Cone, PrimitiveCreationKind::Torus}) {
    auto defaults = std::make_shared<ToolSessionDefaults>();
    defaults->primitive.preferred_kind = unavailable;
    SceneSession session({}, {}, defaults);
    QuaderEditorApi drawing_editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    EXPECT_EQ(drawing_editor.tools().preferred_primitive_shape(), QuaderPrimitiveCreationShape::Box);
    ASSERT_TRUE(drawing_editor.tools().begin_primitive_creation(QuaderPrimitiveCreationShape::Cylinder).ok());
    ASSERT_TRUE(session.modeling().begin_box_footprint({
        .ray = {.origin = {0.0f, 8.0f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}},
        .grid_size = 1.0}).ok);
    for (const auto shape : {QuaderPrimitiveCreationShape::Cone, QuaderPrimitiveCreationShape::Torus}) {
      expect_release_rejection(drawing_editor, [&] {
        return drawing_editor.tools().begin_primitive_creation(shape);
      });
    }
    EXPECT_EQ(drawing_editor.tools().preferred_primitive_shape(), QuaderPrimitiveCreationShape::Cylinder);
    EXPECT_EQ(session.modeling().box_drag_state(), ModelingBoxDragState::Footprint);
  }
}

TEST_F(PublicReleaseModelingTests, PersistentExtrusionCannotReplaceQuickDragState) {
  auto &tools = editor().tools();
  ASSERT_TRUE(editor().modeling().begin_transform_preview().ok());
  EXPECT_FALSE(tools.can_activate("extrude"));
  expect_release_rejection(editor(), [&] { return tools.activate("extrude"); });
  for (const auto mode : {QuaderExtrudeMode::Region, QuaderExtrudeMode::Individual,
                          QuaderExtrudeMode::AlongNormals}) {
    expect_release_rejection(editor(), [&] { return tools.activate_extrude(mode); });
  }
  for (const auto command : {quader_command::kToolsActivateExtrude,
                             quader_command::kToolsActivateExtrudeIndividual,
                             quader_command::kToolsActivateExtrudeAlongNormals}) {
    expect_release_rejection(editor(), [&] { return editor().commands().execute(command); });
  }
  expect_release_rejection(editor(), [&] { return tools.set_setting("extrude", "extrude.amount", 2.0); });
  expect_release_rejection(editor(), [&] { return tools.set_setting("", "extrude.type", "individual"); });
  expect_release_rejection(editor(), [&] { return tools.execute_action("extrude", "extrude.apply"); });
  expect_release_rejection(editor(), [&] { return editor().modeling().update_transform_extrude({0.0f, 1.0f, 0.0f}); });
  EXPECT_TRUE(editor().modeling().cancel_transform_preview().ok());
}

TEST(PublicReleaseQuickExtrusionTests, NativeFaceExtrusionPreservesActivePreview) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto created = editor.modeling().create_box_mesh(
      QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Native extrusion target");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(created.value().faces().only({1}).replace().ok());
  ASSERT_EQ(editor.selection().summary().selected_face_count, 1u);
  auto &native = session.modeling();
  const auto captured = native.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  ASSERT_TRUE(captured.source.valid());
  ASSERT_EQ(captured.face_count, 1u);
  const auto expect_native_rejection = [&](auto action) {
    const auto before = test::capture_command_observable_state(editor);
    const auto result = action();
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(result.message, release_feature_unavailable_message());
    const auto difference = test::diff_command_observable_state(
        before, test::capture_command_observable_state(editor));
    EXPECT_TRUE(difference.empty()) << difference.summary;
  };
  expect_native_rejection([&] {
    return native.apply_face_extrusion(
        captured.source, modeling::FaceExtrudeMode::Region, 0.5);
  });
  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor.modeling().update_transform_translate({0.5f, 0.0f, 0.0f}).ok());
  expect_native_rejection([&] {
    return native.apply_face_extrusion(
        captured.source, modeling::FaceExtrudeMode::Region, 0.5);
  });
  expect_native_rejection([&] {
    return native.begin_face_extrusion_preview(captured.source);
  });
  expect_native_rejection([&] {
    return native.begin_face_extrusion_adjustment_preview(captured.source);
  });
  expect_native_rejection([&] {
    return native.update_face_extrusion_preview(
        captured.source, modeling::FaceExtrudeMode::Region, 0.5);
  });
  expect_native_rejection([&] { return native.commit_face_extrusion_preview(); });
  EXPECT_TRUE(editor.modeling().cancel_transform_preview().ok());
}

TEST(PublicReleaseQuickExtrusionTests, SemanticShiftMoveDragKeepsGeometryAndHistory) {
  auto scenario = test::BehaviorScenario::new_project();
  scenario.given().unit_box("box", "Quick extrusion")
      .edit_mode()
      .selection_mode(QuaderSelectionMode::Face);
  scenario.when().pick({0.0f, 0.0f, 5.0f});
  ASSERT_EQ(scenario.editor().selection().summary().selected_face_count, 1u);
  const auto before_mesh = scenario.snapshot();
  ASSERT_EQ(before_mesh.meshes.size(), 1u);
  const auto before = test::capture_command_observable_state(scenario.editor());
  // This is the semantic drag request produced by Shift+Move, independent of Extrude Tool.
  scenario.when().begin_transform_drag({
      .tool = ModelingTransformTool::Move,
      .operation = ModelingTransformOperation::Extrude,
  }).update_transform_drag(120.0f).commit_drag();
  const auto after_mesh = scenario.snapshot();
  ASSERT_EQ(after_mesh.meshes.size(), 1u);
  EXPECT_GT(after_mesh.meshes.front().vertex_count, before_mesh.meshes.front().vertex_count);
  EXPECT_EQ(scenario.editor().selection().summary().selected_face_count, 1u);
  const auto after = test::capture_command_observable_state(scenario.editor());
  const auto undone = scenario.editor().history().undo();
  ASSERT_TRUE(undone.ok()) << undone.error_message();
  const auto undo_diff = test::diff_command_observable_state(
      before, test::capture_command_observable_state(scenario.editor()),
      test::command_observable_domain_mask(test::CommandObservableDomain::Geometry));
  EXPECT_TRUE(undo_diff.empty()) << undo_diff.summary;
  const auto redone = scenario.editor().history().redo();
  ASSERT_TRUE(redone.ok()) << redone.error_message();
  const auto redo_diff = test::diff_command_observable_state(
      after, test::capture_command_observable_state(scenario.editor()),
      test::command_observable_domain_mask(test::CommandObservableDomain::Geometry));
  EXPECT_TRUE(redo_diff.empty()) << redo_diff.summary;
}

TEST_F(PublicReleaseModelingTests, ModelingTransformsNormalsAndClipboardStillWork) {
  auto &modeling = editor().modeling();
  const auto count_before = modeling.objects().size();
  const auto translated = first_.transform().translate({1.0f, 0.0f, 0.0f});
  ASSERT_TRUE(translated.ok()) << translated.error_message();
  // Apply nonuniform world scale before rotation to keep the result TRS-representable.
  const auto scaled = modeling.scale_selection({1.1f, 1.0f, 1.0f});
  ASSERT_TRUE(scaled.ok()) << scaled.error_message();
  const auto rotated = modeling.rotate_selection({0.0f, 0.1f, 0.0f});
  ASSERT_TRUE(rotated.ok()) << rotated.error_message();
  const auto inverted = modeling.execute_tool("flip_face_normals");
  ASSERT_TRUE(inverted.ok()) << inverted.error_message();
  const auto copied = modeling.copy_selection();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  const auto pasted = modeling.paste_selection();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  EXPECT_EQ(modeling.objects().size(), count_before + 1u);
  const auto cut = modeling.cut_selection();
  ASSERT_TRUE(cut.ok()) << cut.error_message();
  EXPECT_EQ(modeling.objects().size(), count_before);
}

} // namespace
