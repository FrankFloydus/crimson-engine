////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/array/path_array_tool_service.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/interactions/selection_edit.hpp"
#include "editor/session/scene_session.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kTestTolerance = 0.0005F;

struct PathArrayHarness final {
  explicit PathArrayHarness(
      std::shared_ptr<PathArraySessionDefaults> defaults =
          std::make_shared<PathArraySessionDefaults>())
      : session(std::move(defaults)),
        editor(QuaderApiSessionInternalAccess::from_scene_session(session)) {}

  SceneSession session;
  QuaderEditorApi editor;
};

QuaderModelingObjectRef create_path(QuaderEditorApi &editor, QVec3 end,
                                    std::string name = "Path") {
  const QuaderApiResult<QuaderModelingObjectRef> created =
      editor.modeling().create_curve({
          .points = {{.position = {}},
                     {.position = {end.x, end.y, end.z}}},
          .name = std::move(name),
      });
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.ok() ? created.value() : QuaderModelingObjectRef{};
}

QuaderModelingObjectRef create_box(QuaderEditorApi &editor,
                                   std::string name = "Array Source") {
  const QuaderApiResult<QuaderModelingObjectRef> created =
      editor.modeling().create_box(
          QuaderBounds{.min = {-0.5F, -0.5F, -0.5F},
                       .max = {0.5F, 0.5F, 0.5F}},
          std::move(name));
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.ok() ? created.value() : QuaderModelingObjectRef{};
}

QuaderEntityRef create_empty(QuaderEditorApi &editor, std::string name,
                             EntityId parent = kInvalidEntityId,
                             QuaderTransform transform = {}) {
  const QuaderApiResult<QuaderEntityRef> created = editor.scene().create_entity({
      .name = std::move(name),
      .kind = QuaderEntityKind::Empty,
      .parent = parent,
      .transform = transform,
  });
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.ok() ? created.value() : QuaderEntityRef{};
}

void select_path(PathArrayHarness &harness,
                 QuaderModelingObjectRef path) {
  ASSERT_TRUE(
      harness.editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(path).ok());
  const CommandResult refreshed = harness.session.curve().refresh();
  ASSERT_TRUE(refreshed.ok) << refreshed.message;
}

void expect_matrix_near(const QMat4 &actual, const QMat4 &expected) {
  for (std::size_t index = 0U; index < actual.values.size(); ++index) {
    EXPECT_NEAR(actual.values[index], expected.values[index], kTestTolerance)
        << "matrix component " << index;
  }
}

void expect_world_origins_match(std::span<const EntityId> roots,
                                const SceneDocument &scene,
                                std::span<const QMat4> expected) {
  ASSERT_EQ(roots.size(), expected.size());
  std::vector<bool> matched(expected.size(), false);
  for (const EntityId root : roots) {
    const QVec3 origin =
        transform_point(scene.world_transform_matrix(root), {});
    bool found = false;
    for (std::size_t index = 0U; index < expected.size(); ++index) {
      if (matched[index]) {
        continue;
      }
      const QVec3 candidate = transform_point(expected[index], {});
      if (std::abs(origin.x - candidate.x) <= kTestTolerance &&
          std::abs(origin.y - candidate.y) <= kTestTolerance &&
          std::abs(origin.z - candidate.z) <= kTestTolerance) {
        matched[index] = true;
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "no preview station matched root " << root;
  }
  EXPECT_TRUE(std::ranges::all_of(matched, [](bool value) { return value; }));
}

std::vector<EntityId> hierarchy_roots_named(const SceneDocument &scene,
                                            std::string_view prefix) {
  std::vector<EntityId> matches;
  for (const EntityId entity : scene.entities()) {
    const NameComponent *name = scene.name(entity);
    if (name != nullptr &&
        name->name.starts_with(prefix)) {
      matches.push_back(entity);
    }
  }
  std::ranges::sort(matches);
  return matches;
}

TEST(PathArrayToolServiceTests,
     ActivationRequiresExactlyOneNondegenerateCurveInObjectMode) {
  PathArrayHarness harness;
  PathArrayToolService &tool = harness.session.path_array();

  EXPECT_FALSE(tool.can_activate());
  const QuaderModelingObjectRef mesh = create_box(harness.editor);
  ASSERT_TRUE(harness.editor.selection().select_object(mesh).ok());
  EXPECT_FALSE(tool.can_activate());

  const QuaderApiResult<QuaderModelingObjectRef> degenerate =
      harness.editor.modeling().create_curve({
          .points = {{.position = {1.0F, 2.0F, 3.0F}},
                     {.position = {1.0F, 2.0F, 3.0F}}},
          .name = "Degenerate Path",
      });
  ASSERT_TRUE(degenerate.ok()) << degenerate.error_message();
  EXPECT_FALSE(tool.can_activate());

  const QuaderModelingObjectRef first =
      create_path(harness.editor, {6.0F, 0.0F, 0.0F}, "First Path");
  const QuaderModelingObjectRef second =
      create_path(harness.editor, {0.0F, 6.0F, 0.0F}, "Second Path");
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({first.entity, second.entity}, first.entity)
                  .ok());
  EXPECT_FALSE(tool.can_activate());

  ASSERT_TRUE(harness.editor.selection().select_object(mesh).ok());
  ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(
      harness.editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  EXPECT_FALSE(tool.can_activate());
  ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(
      harness.editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(first).ok());
  EXPECT_TRUE(tool.can_activate());
  const CommandResult activated = tool.activate();
  ASSERT_TRUE(activated.ok) << activated.message;
  EXPECT_EQ(tool.stage(), PathArrayToolStage::AwaitingSource);
  EXPECT_EQ(tool.path_entity(), first.entity);
}

TEST(PathArrayToolServiceTests,
     CancelRestoresExactCurveSelectionModeAndControlSubselection) {
  PathArrayHarness harness;
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {12.0F, 0.0F, 0.0F});
  const QuaderModelingObjectRef source = create_box(harness.editor);
  select_path(harness, path);

  CurveToolService &curve = harness.session.curve();
  const CurveToolPreviewState &curve_preview = curve.preview_state();
  ASSERT_TRUE(curve_preview.document.has_value());
  const CurveControlTarget anchor{curve_preview.document->points()[0].id,
                                  CurveControlKind::Anchor};
  const std::array selected{anchor};
  ASSERT_TRUE(curve.edit_control_selection(
      selected, quader_editor_core::SelectionEditOperation::Replace, nullptr,
      anchor));
  const CurveControlSelection selection_before = curve.control_selection();

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.select_source(source.entity).ok);
  EXPECT_EQ(harness.session.scene().selected_entities(),
            std::vector<EntityId>{source.entity});
  EXPECT_TRUE(curve.control_selection().empty());

  const CommandResult canceled = tool.cancel();
  ASSERT_TRUE(canceled.ok) << canceled.message;
  EXPECT_FALSE(tool.has_session());
  EXPECT_EQ(harness.session.selection().mode(), ModelingSelectionMode::Object);
  EXPECT_EQ(harness.session.scene().selected_entities(),
            std::vector<EntityId>{path.entity});
  EXPECT_EQ(harness.session.scene().primary_selection(), path.entity);
  EXPECT_EQ(curve.control_selection(), selection_before);
}

TEST(PathArrayToolServiceTests,
     AncestorMutationsAreBlockedAndInvalidationRestoresActivationBaseline) {
  PathArrayHarness harness;
  const QuaderEntityRef path_grandparent =
      create_empty(harness.editor, "Path Grandparent");
  const QuaderEntityRef path_parent =
      create_empty(harness.editor, "Path Parent", path_grandparent.id);
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {12.0F, 0.0F, 0.0F});
  ASSERT_TRUE(
      harness.editor.scene().reparent(path.entity, path_parent.id, false).ok());

  const QuaderEntityRef source_ancestor =
      create_empty(harness.editor, "Source Ancestor");
  const QuaderEntityRef source_root = create_empty(
      harness.editor, "Source Root", source_ancestor.id);
  const QuaderModelingObjectRef source = create_box(harness.editor);
  ASSERT_TRUE(
      harness.editor.scene().reparent(source.entity, source_root.id, false).ok());
  select_path(harness, path);

  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const CurveControlTarget anchor{
      curve.preview_state().document->points()[0].id,
      CurveControlKind::Anchor};
  const std::array controls{anchor};
  ASSERT_TRUE(curve.edit_control_selection(
      controls, quader_editor_core::SelectionEditOperation::Replace, nullptr,
      anchor));
  const CurveControlSelection control_baseline = curve.control_selection();

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.select_source(source_root.id).ok);
  ASSERT_TRUE(tool.preview_valid());
  EXPECT_TRUE(curve.control_selection().empty());
  harness.session.history().clear();

  const QuaderApiVoidResult blocked_path_parent =
      harness.editor.scene().set_transform(
          path_grandparent.id,
          {.position = {3.0F, 0.0F, 0.0F}});
  EXPECT_FALSE(blocked_path_parent.ok());
  const QuaderApiVoidResult blocked_source_parent =
      harness.editor.scene().set_transform(
          source_ancestor.id,
          {.position = {0.0F, 4.0F, 0.0F}});
  EXPECT_FALSE(blocked_source_parent.ok());
  EXPECT_FALSE(harness.session.history().can_undo());
  const QVec3 path_parent_position =
      harness.session.scene().transform(path_grandparent.id)->position;
  const QVec3 source_parent_position =
      harness.session.scene().transform(source_ancestor.id)->position;
  EXPECT_FLOAT_EQ(path_parent_position.x, 0.0F);
  EXPECT_FLOAT_EQ(path_parent_position.y, 0.0F);
  EXPECT_FLOAT_EQ(path_parent_position.z, 0.0F);
  EXPECT_FLOAT_EQ(source_parent_position.x, 0.0F);
  EXPECT_FLOAT_EQ(source_parent_position.y, 0.0F);
  EXPECT_FLOAT_EQ(source_parent_position.z, 0.0F);
  EXPECT_TRUE(tool.has_session());
  EXPECT_TRUE(tool.preview_valid());

  // Simulate an external document replacement/edit that bypassed the command
  // blocker. The next lifecycle refresh must cancel the now-invalid session
  // and restore the exact activation selection memento.
  ASSERT_TRUE(harness.session.scene().set_transform(
      path_grandparent.id,
      TransformComponent{.position = {1.0F, 0.0F, 0.0F}}));
  EXPECT_FALSE(tool.active());
  const CommandResult refreshed = tool.refresh();
  ASSERT_TRUE(refreshed.ok) << refreshed.message;
  EXPECT_TRUE(refreshed.changed);
  EXPECT_FALSE(tool.has_session());
  EXPECT_EQ(harness.session.selection().mode(), ModelingSelectionMode::Object);
  EXPECT_EQ(harness.session.scene().selected_entities(),
            std::vector<EntityId>{path.entity});
  EXPECT_EQ(harness.session.scene().primary_selection(), path.entity);
  EXPECT_EQ(curve.control_selection(), control_baseline);
}

TEST(PathArrayToolServiceTests,
     RemembersEveryOptionAcrossCancelApplyAndSceneReplacement) {
  const auto defaults = std::make_shared<PathArraySessionDefaults>();
  const auto expect_vector = [](QVec3 actual, QVec3 expected) {
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
  };
  for (int run = 0; run < 3; ++run) {
    PathArrayHarness harness(defaults);
    const auto path = create_path(harness.editor, {18.0F, 0.0F, 0.0F});
    const auto source = create_box(harness.editor);
    select_path(harness, path);
    PathArrayToolService &tool = harness.session.path_array();
    ASSERT_TRUE(tool.activate().ok);
    EXPECT_FALSE(tool.has_source());
    if (run == 0) {
      ASSERT_TRUE(tool.select_source(source.entity).ok);
      ASSERT_TRUE(tool.set_count(6U).ok);
      ASSERT_TRUE(tool.set_distance(2.25).ok);
      ASSERT_TRUE(tool.set_follow_path(false).ok);
      ASSERT_TRUE(tool.set_forward_axis(PathArraySignedAxis::NegativeZ).ok);
      ASSERT_TRUE(tool.set_rotation_offset_degrees({2, 3, 4}).ok);
      ASSERT_TRUE(tool.set_rotation_random_min_degrees({-10, -20, -30}).ok);
      ASSERT_TRUE(tool.set_rotation_random_max_degrees({10, 20, 30}).ok);
      ASSERT_TRUE(tool.set_scale_offset_factors({1.1F, 0.9F, 1.2F}).ok);
      ASSERT_TRUE(tool.set_scale_random_min_factors({0.5F, 0.6F, 0.7F}).ok);
      ASSERT_TRUE(tool.set_scale_random_max_factors({1.5F, 1.6F, 1.7F}).ok);
      ASSERT_TRUE(tool.set_show_bounding_box_dimensions(false).ok);
      // Cancel resolves any valid option batch that has not been rendered yet.
      ASSERT_TRUE(tool.cancel().ok);
      ASSERT_TRUE(tool.activate().ok);
      EXPECT_FALSE(tool.has_source());
    }
    EXPECT_EQ(tool.count(), 6U);
    EXPECT_DOUBLE_EQ(tool.distance(), 2.25);
    EXPECT_FALSE(tool.follow_path());
    EXPECT_FALSE(tool.show_bounding_box_dimensions());
    EXPECT_EQ(tool.forward_axis(), PathArraySignedAxis::NegativeZ);
    expect_vector(tool.rotation_offset_degrees(), {2, 3, 4});
    expect_vector(tool.rotation_random_min_degrees(), {-10, -20, -30});
    expect_vector(tool.rotation_random_max_degrees(), {10, 20, 30});
    expect_vector(tool.scale_offset_factors(), {1.1F, 0.9F, 1.2F});
    expect_vector(tool.scale_random_min_factors(), {0.5F, 0.6F, 0.7F});
    expect_vector(tool.scale_random_max_factors(), {1.5F, 1.6F, 1.7F});
    EXPECT_FALSE(tool.set_count(kPathArrayMaximumCount + 1U).ok);
    EXPECT_FALSE(tool.set_scale_offset_factors({0, 1, 1}).ok);
    EXPECT_EQ(defaults->count, 6U);
    EXPECT_FLOAT_EQ(defaults->scale_offset_factors.x, 1.1F);
    ASSERT_TRUE(tool.select_source(source.entity).ok);
    ASSERT_TRUE(tool.preview_valid());
    ASSERT_TRUE(tool.apply().ok);
  }
  PathArrayHarness fresh;
  const auto path = create_path(fresh.editor, {18.0F, 0.0F, 0.0F});
  select_path(fresh, path);
  ASSERT_TRUE(fresh.session.path_array().activate().ok);
  EXPECT_EQ(fresh.session.path_array().count(), kPathArrayDefaultCount);
  EXPECT_TRUE(fresh.session.path_array().follow_path());
  EXPECT_TRUE(fresh.session.path_array().show_bounding_box_dimensions());
}

TEST(PathArrayToolServiceTests,
     BoundsVisibilityAndVisualOnlyResetRetainPreviewAndHistory) {
  PathArrayHarness harness;
  const auto path = create_path(harness.editor, {12.0F, 0.0F, 0.0F});
  const auto source = create_box(harness.editor);
  select_path(harness, path);
  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.select_source(source.entity).ok);
  const PathArrayPreviewState &preview = tool.preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_FALSE(preview.scene.meshes.empty());
  const auto *roots = preview.root_world_transforms.data();
  const auto *meshes = preview.scene.meshes.data();
  const auto *instances = preview.scene.meshes.front().instance_transforms.data();
  const std::uint64_t preview_revision = preview.revision;
  const std::uint64_t preview_scene_revision = preview.scene.revision;
  const PathArrayLayoutWorkStats work = tool.layout_work_stats_for_tests();
  const std::uint64_t scene_revision = harness.session.scene().revision();
  const std::uint64_t history_revision = harness.session.history().revision();
  const std::uint64_t settings_revision = tool.tool_settings_revision();

  const auto expect_retained_preview = [&] {
    const PathArrayPreviewState &current = tool.preview_state();
    EXPECT_EQ(current.revision, preview_revision);
    EXPECT_EQ(current.scene.revision, preview_scene_revision);
    EXPECT_EQ(current.root_world_transforms.data(), roots);
    EXPECT_EQ(current.scene.meshes.data(), meshes);
    EXPECT_EQ(current.scene.meshes.front().instance_transforms.data(), instances);
    EXPECT_EQ(tool.layout_work_stats_for_tests().backbone_build_count,
              work.backbone_build_count);
    EXPECT_EQ(tool.layout_work_stats_for_tests().layout_evaluation_count,
              work.layout_evaluation_count);
    EXPECT_EQ(harness.session.scene().revision(), scene_revision);
    EXPECT_EQ(harness.session.history().revision(), history_revision);
  };

  EXPECT_TRUE(tool.show_bounding_box_dimensions());
  const CommandResult hidden = tool.set_show_bounding_box_dimensions(false);
  ASSERT_TRUE(hidden.ok);
  EXPECT_TRUE(hidden.changed);
  EXPECT_FALSE(tool.show_bounding_box_dimensions());
  EXPECT_EQ(tool.tool_settings_revision(), settings_revision + 1U);
  expect_retained_preview();
  const CommandResult unchanged = tool.set_show_bounding_box_dimensions(false);
  ASSERT_TRUE(unchanged.ok);
  EXPECT_FALSE(unchanged.changed);
  EXPECT_EQ(tool.tool_settings_revision(), settings_revision + 1U);
  expect_retained_preview();

  const CommandResult reset = tool.reset_settings();
  ASSERT_TRUE(reset.ok);
  EXPECT_TRUE(reset.changed);
  EXPECT_TRUE(tool.show_bounding_box_dimensions());
  EXPECT_EQ(tool.tool_settings_revision(), settings_revision + 2U);
  expect_retained_preview();
  const CommandResult unchanged_reset = tool.reset_settings();
  ASSERT_TRUE(unchanged_reset.ok);
  EXPECT_FALSE(unchanged_reset.changed);
  EXPECT_EQ(tool.tool_settings_revision(), settings_revision + 2U);
  expect_retained_preview();
}

TEST(PathArrayToolServiceTests,
     BoundsVisibilitySurvivesPendingLayoutPublicationAndCancellation) {
  const auto defaults = std::make_shared<PathArraySessionDefaults>();
  PathArrayHarness harness(defaults);
  const auto path = create_path(harness.editor, {12.0F, 0.0F, 0.0F});
  const auto source = create_box(harness.editor);
  select_path(harness, path);
  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.select_source(source.entity).ok);
  ASSERT_TRUE(tool.preview_valid());
  ASSERT_TRUE(tool.set_count(6U).ok);
  ASSERT_TRUE(tool.set_show_bounding_box_dimensions(false).ok);
  EXPECT_FALSE(defaults->show_bounding_box_dimensions);
  EXPECT_EQ(defaults->count, kPathArrayDefaultCount);
  ASSERT_TRUE(tool.preview_valid());
  EXPECT_EQ(defaults->count, 6U);
  EXPECT_FALSE(defaults->show_bounding_box_dimensions);
  ASSERT_TRUE(tool.set_count(5U).ok);
  ASSERT_TRUE(tool.set_show_bounding_box_dimensions(true).ok);
  ASSERT_TRUE(tool.cancel().ok);
  EXPECT_EQ(defaults->count, 5U);
  EXPECT_TRUE(defaults->show_bounding_box_dimensions);
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_TRUE(tool.show_bounding_box_dimensions());
}

TEST(PathArrayToolServiceTests,
     SessionDefaultsFitFirstPathRememberExactSpacingAndReset) {
  const auto defaults = std::make_shared<PathArraySessionDefaults>();
  {
    PathArrayHarness first(defaults);
    const QuaderModelingObjectRef path =
        create_path(first.editor, {12.0F, 0.0F, 0.0F});
    select_path(first, path);
    PathArrayToolService &tool = first.session.path_array();
    ASSERT_TRUE(tool.activate().ok);
    EXPECT_EQ(tool.count(), 4U);
    EXPECT_NEAR(tool.distance(), 4.0, 0.0001);
    ASSERT_TRUE(tool.set_distance(1.5).ok);
    EXPECT_DOUBLE_EQ(defaults->remembered_distance.value(), 1.5);
    ASSERT_TRUE(tool.cancel().ok);
  }

  {
    PathArrayHarness shorter(defaults);
    const QuaderModelingObjectRef path =
        create_path(shorter.editor, {3.0F, 0.0F, 0.0F});
    select_path(shorter, path);
    PathArrayToolService &tool = shorter.session.path_array();
    ASSERT_TRUE(tool.activate().ok);
    EXPECT_NEAR(tool.maximum_distance(), 3.0, 0.0001);
    EXPECT_NEAR(tool.distance(), 1.5, 0.0001)
        << "activation must preserve the exact remembered spacing";

    ASSERT_TRUE(tool.set_count(2U).ok);
    ASSERT_TRUE(tool.set_distance(0.25).ok);
    ASSERT_TRUE(tool.set_follow_path(false).ok);
    ASSERT_TRUE(tool.set_forward_axis(PathArraySignedAxis::NegativeZ).ok);
    ASSERT_TRUE(
        tool.set_rotation_offset_degrees({2.0F, 3.0F, 4.0F}).ok);
    ASSERT_TRUE(tool.set_rotation_random_min_degrees(
                        {-10.0F, -20.0F, -30.0F})
                    .ok);
    ASSERT_TRUE(tool.set_rotation_random_max_degrees(
                        {10.0F, 20.0F, 30.0F})
                    .ok);
    ASSERT_TRUE(tool.set_scale_offset_factors({1.1F, 0.9F, 1.2F}).ok);
    ASSERT_TRUE(
        tool.set_scale_random_min_factors({0.5F, 0.6F, 0.7F}).ok);
    ASSERT_TRUE(
        tool.set_scale_random_max_factors({1.5F, 1.6F, 1.7F}).ok);
    ASSERT_TRUE(tool.set_show_bounding_box_dimensions(false).ok);
    const CommandResult reset = tool.reset_settings();
    ASSERT_TRUE(reset.ok) << reset.message;
    EXPECT_EQ(tool.count(), kPathArrayDefaultCount);
    EXPECT_NEAR(tool.distance(), 1.0, 0.0001);
    EXPECT_TRUE(tool.follow_path());
    EXPECT_TRUE(tool.show_bounding_box_dimensions());
    EXPECT_EQ(tool.forward_axis(), PathArraySignedAxis::PositiveX);
    EXPECT_EQ(tool.rotation_offset_degrees().x, 0.0F);
    EXPECT_EQ(tool.rotation_random_min_degrees().y, 0.0F);
    EXPECT_EQ(tool.rotation_random_max_degrees().z, 0.0F);
    EXPECT_EQ(tool.scale_offset_factors().x, 1.0F);
    EXPECT_EQ(tool.scale_random_min_factors().y, 1.0F);
    EXPECT_EQ(tool.scale_random_max_factors().z, 1.0F);
    EXPECT_NEAR(defaults->remembered_distance.value(), 1.0, 0.0001);
    ASSERT_TRUE(tool.cancel().ok);
  }

  PathArrayHarness longer(defaults);
  const QuaderModelingObjectRef path =
      create_path(longer.editor, {30.0F, 0.0F, 0.0F});
  select_path(longer, path);
  ASSERT_TRUE(longer.session.path_array().activate().ok);
  EXPECT_NEAR(longer.session.path_array().maximum_distance(), 30.0, 0.0001);
  EXPECT_NEAR(longer.session.path_array().distance(), 1.0, 0.0001)
      << "later activations use the session default instead of refitting";
}

TEST(PathArrayToolServiceTests,
     CountChangesPreserveExactDistanceRangeAndEveryPrefixPlacement) {
  const auto defaults = std::make_shared<PathArraySessionDefaults>();
  PathArrayHarness harness(defaults);
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {127.0F, 0.0F, 0.0F});
  const QuaderModelingObjectRef source = create_box(harness.editor);
  select_path(harness, path);

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  constexpr double kDistance = 5.0;
  ASSERT_TRUE(tool.set_distance(200.0).ok);
  EXPECT_DOUBLE_EQ(tool.maximum_distance(), 200.0)
      << "the control range must always contain the stored Distance";
  ASSERT_TRUE(tool.set_count(kPathArrayMaximumCount).ok);
  EXPECT_DOUBLE_EQ(tool.distance(), 200.0);
  EXPECT_DOUBLE_EQ(tool.maximum_distance(), 200.0);
  ASSERT_TRUE(tool.set_count(kPathArrayDefaultCount).ok);
  ASSERT_TRUE(tool.set_distance(kDistance).ok);
  EXPECT_DOUBLE_EQ(tool.distance(), kDistance);
  EXPECT_DOUBLE_EQ(tool.maximum_distance(), 127.0);
  ASSERT_TRUE(defaults->remembered_distance.has_value());
  EXPECT_DOUBLE_EQ(*defaults->remembered_distance, kDistance);
  ASSERT_TRUE(tool.select_source(source.entity).ok);

  const PathArrayPreviewState initial_preview = tool.preview_state();
  ASSERT_TRUE(initial_preview.active);
  ASSERT_EQ(initial_preview.root_world_transforms.size(),
            kPathArrayDefaultCount);
  const std::vector<QMat4> initial_prefix =
      initial_preview.root_world_transforms;
  const double initial_maximum_distance = tool.maximum_distance();

  ASSERT_TRUE(tool.set_count(kPathArrayMaximumCount).ok);
  EXPECT_EQ(tool.count(), kPathArrayMaximumCount);
  EXPECT_DOUBLE_EQ(tool.maximum_distance(), initial_maximum_distance);
  EXPECT_DOUBLE_EQ(tool.distance(), kDistance);
  EXPECT_DOUBLE_EQ(*defaults->remembered_distance, kDistance);

  const PathArrayPreviewState &maximum_preview = tool.preview_state();
  ASSERT_TRUE(maximum_preview.active);
  EXPECT_DOUBLE_EQ(maximum_preview.effective_distance, kDistance);
  ASSERT_EQ(maximum_preview.root_world_transforms.size(),
            kPathArrayMaximumCount);
  ASSERT_EQ(maximum_preview.scene.meshes.size(), 1U);
  EXPECT_EQ(maximum_preview.scene.meshes.front().instance_transforms.size(),
            kPathArrayMaximumCount);
  EXPECT_EQ(maximum_preview.scene.suppression.entities,
            std::vector<EntityId>{source.entity});
  EXPECT_TRUE(maximum_preview.scene.suppression.occurrences.empty());
  for (std::size_t index = 0U; index < initial_prefix.size(); ++index) {
    EXPECT_EQ(maximum_preview.root_world_transforms[index].values,
              initial_prefix[index].values)
        << "existing prefix placement " << index << " changed with Count";
  }
  for (std::size_t index = 0U;
       index < maximum_preview.root_world_transforms.size(); ++index) {
    const QVec3 origin =
        transform_point(maximum_preview.root_world_transforms[index], {});
    EXPECT_NEAR(origin.x, static_cast<float>(index) * kDistance,
                kTestTolerance)
        << "placement " << index;
    EXPECT_NEAR(origin.y, 0.0F, kTestTolerance);
    EXPECT_NEAR(origin.z, 0.0F, kTestTolerance);
  }

  ASSERT_TRUE(tool.set_count(kPathArrayDefaultCount).ok);
  EXPECT_EQ(tool.count(), kPathArrayDefaultCount);
  EXPECT_DOUBLE_EQ(tool.maximum_distance(), initial_maximum_distance);
  EXPECT_DOUBLE_EQ(tool.distance(), kDistance);
  EXPECT_DOUBLE_EQ(*defaults->remembered_distance, kDistance);
  const PathArrayPreviewState &restored_preview = tool.preview_state();
  ASSERT_TRUE(restored_preview.active);
  EXPECT_DOUBLE_EQ(restored_preview.effective_distance, kDistance);
  EXPECT_EQ(restored_preview.root_world_transforms.size(),
            kPathArrayDefaultCount);
  EXPECT_EQ(restored_preview.scene.meshes.front().instance_transforms.size(),
            kPathArrayDefaultCount);
  for (std::size_t index = 0U; index < initial_prefix.size(); ++index) {
    EXPECT_EQ(restored_preview.root_world_transforms[index].values,
              initial_prefix[index].values);
  }
}

TEST(PathArrayToolServiceTests,
     DistanceAndCountPreviewsReuseThePreparedPathBackbone) {
  PathArrayHarness harness;
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {24.0F, 0.0F, 0.0F});
  const QuaderModelingObjectRef source = create_box(harness.editor);
  select_path(harness, path);

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_distance(1.0).ok);
  ASSERT_TRUE(tool.select_source(source.entity).ok);

  const PathArrayPreviewState first = tool.preview_state();
  ASSERT_TRUE(first.active);
  ASSERT_EQ(first.root_world_transforms.size(), kPathArrayDefaultCount);
  const PathArrayLayoutWorkStats first_work =
      tool.layout_work_stats_for_tests();
  ASSERT_EQ(first_work.backbone_build_count, 1U);

  ASSERT_TRUE(tool.set_distance(2.5).ok);
  const PathArrayPreviewState distance_preview = tool.preview_state();
  ASSERT_TRUE(distance_preview.active);
  EXPECT_GT(distance_preview.revision, first.revision);
  EXPECT_DOUBLE_EQ(distance_preview.effective_distance, 2.5);
  ASSERT_EQ(distance_preview.root_world_transforms.size(),
            kPathArrayDefaultCount);
  EXPECT_NE(distance_preview.root_world_transforms[1U].values,
            first.root_world_transforms[1U].values);
  PathArrayLayoutWorkStats work = tool.layout_work_stats_for_tests();
  EXPECT_EQ(work.backbone_build_count, first_work.backbone_build_count);
  EXPECT_EQ(work.layout_evaluation_count,
            first_work.layout_evaluation_count + 1U);

  ASSERT_TRUE(tool.set_count(7U).ok);
  const PathArrayPreviewState count_preview = tool.preview_state();
  ASSERT_TRUE(count_preview.active);
  EXPECT_GT(count_preview.revision, distance_preview.revision);
  ASSERT_EQ(count_preview.root_world_transforms.size(), 7U);
  for (std::size_t index = 0U;
       index < distance_preview.root_world_transforms.size(); ++index) {
    EXPECT_EQ(count_preview.root_world_transforms[index].values,
              distance_preview.root_world_transforms[index].values);
  }
  work = tool.layout_work_stats_for_tests();
  EXPECT_EQ(work.backbone_build_count, first_work.backbone_build_count);
  EXPECT_EQ(work.layout_evaluation_count,
            first_work.layout_evaluation_count + 2U);

  ASSERT_TRUE(tool.set_distance(4.0).ok);
  const PathArrayPreviewState second_distance_preview = tool.preview_state();
  ASSERT_TRUE(second_distance_preview.active);
  EXPECT_GT(second_distance_preview.revision, count_preview.revision);
  EXPECT_DOUBLE_EQ(second_distance_preview.effective_distance, 4.0);
  EXPECT_NE(second_distance_preview.root_world_transforms[1U].values,
            count_preview.root_world_transforms[1U].values);
  work = tool.layout_work_stats_for_tests();
  EXPECT_EQ(work.backbone_build_count, first_work.backbone_build_count);
  EXPECT_EQ(work.layout_evaluation_count,
            first_work.layout_evaluation_count + 3U);

  const std::uint64_t stable_revision = second_distance_preview.revision;
  const CommandResult unchanged = tool.set_distance(4.0);
  EXPECT_TRUE(unchanged.ok);
  EXPECT_FALSE(unchanged.changed);
  EXPECT_EQ(tool.preview_state().revision, stable_revision);
  EXPECT_EQ(tool.layout_work_stats_for_tests().backbone_build_count,
            work.backbone_build_count);
  EXPECT_EQ(tool.layout_work_stats_for_tests().layout_evaluation_count,
            work.layout_evaluation_count);

  ASSERT_TRUE(tool.set_forward_axis(PathArraySignedAxis::NegativeZ).ok);
  const PathArrayPreviewState axis_preview = tool.preview_state();
  ASSERT_TRUE(axis_preview.active);
  EXPECT_GT(axis_preview.revision, stable_revision);
  PathArrayLayoutWorkStats orientation_work =
      tool.layout_work_stats_for_tests();
  EXPECT_EQ(orientation_work.backbone_build_count,
            work.backbone_build_count + 1U);
  EXPECT_EQ(orientation_work.layout_evaluation_count,
            work.layout_evaluation_count + 1U);

  ASSERT_TRUE(tool.set_follow_path(false).ok);
  const PathArrayPreviewState no_follow_preview = tool.preview_state();
  ASSERT_TRUE(no_follow_preview.active);
  EXPECT_GT(no_follow_preview.revision, axis_preview.revision);
  orientation_work = tool.layout_work_stats_for_tests();
  EXPECT_EQ(orientation_work.backbone_build_count,
            work.backbone_build_count + 2U);
  EXPECT_EQ(orientation_work.layout_evaluation_count,
            work.layout_evaluation_count + 2U);

  ASSERT_TRUE(tool.set_forward_axis(PathArraySignedAxis::PositiveY).ok);
  const PathArrayPreviewState no_follow_axis_preview = tool.preview_state();
  ASSERT_TRUE(no_follow_axis_preview.active);
  EXPECT_GT(no_follow_axis_preview.revision, no_follow_preview.revision);
  const PathArrayLayoutWorkStats no_follow_axis_work =
      tool.layout_work_stats_for_tests();
  EXPECT_EQ(no_follow_axis_work.backbone_build_count,
            orientation_work.backbone_build_count)
      << "the signed axis is not a backbone input when Follow Path is off";
  EXPECT_EQ(no_follow_axis_work.layout_evaluation_count,
            orientation_work.layout_evaluation_count + 1U);
}

TEST(PathArrayToolServiceTests,
     VariationSettingsValidateLazilyAndReuseThePreparedBackbone) {
  PathArrayHarness harness;
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {24.0F, 0.0F, 0.0F});
  const QuaderModelingObjectRef source = create_box(harness.editor);
  select_path(harness, path);

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_distance(2.0).ok);
  ASSERT_TRUE(tool.select_source(source.entity).ok);
  const PathArrayPreviewState neutral = tool.preview_state();
  ASSERT_TRUE(neutral.active);
  const PathArrayLayoutWorkStats initial_work =
      tool.layout_work_stats_for_tests();
  const std::uint64_t initial_settings_revision =
      tool.tool_settings_revision();

  EXPECT_FALSE(tool.set_rotation_offset_degrees(
                       {std::numeric_limits<float>::infinity(), 0.0F, 0.0F})
                   .ok);
  EXPECT_FALSE(tool.set_scale_offset_factors({0.0F, 1.0F, 1.0F}).ok);
  EXPECT_EQ(tool.tool_settings_revision(), initial_settings_revision);
  EXPECT_EQ(tool.preview_state().revision, neutral.revision);

  ASSERT_TRUE(
      tool.set_rotation_offset_degrees({2.0F, 4.0F, 6.0F}).ok);
  ASSERT_TRUE(tool.set_rotation_random_min_degrees(
                      {-10.0F, -20.0F, -30.0F})
                  .ok);
  ASSERT_TRUE(tool.set_rotation_random_max_degrees(
                      {10.0F, 20.0F, 30.0F})
                  .ok);
  ASSERT_TRUE(tool.set_scale_offset_factors({1.1F, 0.9F, 1.2F}).ok);
  ASSERT_TRUE(
      tool.set_scale_random_min_factors({0.5F, 0.6F, 0.7F}).ok);
  ASSERT_TRUE(
      tool.set_scale_random_max_factors({1.5F, 1.6F, 1.7F}).ok);
  EXPECT_FLOAT_EQ(tool.rotation_offset_degrees().x, 2.0F);
  EXPECT_FLOAT_EQ(tool.rotation_random_min_degrees().y, -20.0F);
  EXPECT_FLOAT_EQ(tool.rotation_random_max_degrees().z, 30.0F);
  EXPECT_FLOAT_EQ(tool.scale_offset_factors().x, 1.1F);
  EXPECT_FLOAT_EQ(tool.scale_random_min_factors().y, 0.6F);
  EXPECT_FLOAT_EQ(tool.scale_random_max_factors().z, 1.7F);

  const PathArrayPreviewState varied = tool.preview_state();
  ASSERT_TRUE(varied.active);
  EXPECT_EQ(varied.root_world_transforms.front().values,
            neutral.root_world_transforms.front().values);
  EXPECT_NE(varied.root_world_transforms[1U].values,
            neutral.root_world_transforms[1U].values);
  PathArrayLayoutWorkStats work = tool.layout_work_stats_for_tests();
  EXPECT_EQ(work.backbone_build_count, initial_work.backbone_build_count);
  EXPECT_EQ(work.layout_evaluation_count,
            initial_work.layout_evaluation_count + 1U)
      << "batched setting edits should remain lazy until preview demand";

  ASSERT_TRUE(tool.reset_settings().ok);
  const PathArrayPreviewState reset = tool.preview_state();
  ASSERT_TRUE(reset.active);
  EXPECT_EQ(tool.rotation_offset_degrees().x, 0.0F);
  EXPECT_EQ(tool.scale_offset_factors().x, 1.0F);
  EXPECT_EQ(reset.root_world_transforms.front().values,
            neutral.root_world_transforms.front().values);
  work = tool.layout_work_stats_for_tests();
  EXPECT_EQ(work.backbone_build_count, initial_work.backbone_build_count);
  EXPECT_EQ(work.layout_evaluation_count,
            initial_work.layout_evaluation_count + 2U);
}

TEST(PathArrayToolServiceTests,
     RandomRangeEndpointEditsMoveCrossedOppositeEndpointsAtomically) {
  PathArrayHarness harness;
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {12.0F, 0.0F, 0.0F});
  select_path(harness, path);

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);

  ASSERT_TRUE(
      tool.set_rotation_random_min_degrees({5.0F, -2.0F, 3.0F}).ok);
  EXPECT_FLOAT_EQ(tool.rotation_random_min_degrees().x, 5.0F);
  EXPECT_FLOAT_EQ(tool.rotation_random_min_degrees().y, -2.0F);
  EXPECT_FLOAT_EQ(tool.rotation_random_max_degrees().x, 5.0F);
  EXPECT_FLOAT_EQ(tool.rotation_random_max_degrees().y, 0.0F);
  EXPECT_FLOAT_EQ(tool.rotation_random_max_degrees().z, 3.0F);

  ASSERT_TRUE(
      tool.set_rotation_random_max_degrees({2.0F, -3.0F, 4.0F}).ok);
  EXPECT_FLOAT_EQ(tool.rotation_random_min_degrees().x, 2.0F);
  EXPECT_FLOAT_EQ(tool.rotation_random_min_degrees().y, -3.0F);
  EXPECT_FLOAT_EQ(tool.rotation_random_min_degrees().z, 3.0F);
  EXPECT_FLOAT_EQ(tool.rotation_random_max_degrees().x, 2.0F);
  EXPECT_FLOAT_EQ(tool.rotation_random_max_degrees().y, -3.0F);
  EXPECT_FLOAT_EQ(tool.rotation_random_max_degrees().z, 4.0F);

  ASSERT_TRUE(tool.set_scale_random_min_factors({2.0F, 0.5F, 3.0F}).ok);
  EXPECT_FLOAT_EQ(tool.scale_random_min_factors().x, 2.0F);
  EXPECT_FLOAT_EQ(tool.scale_random_min_factors().y, 0.5F);
  EXPECT_FLOAT_EQ(tool.scale_random_max_factors().x, 2.0F);
  EXPECT_FLOAT_EQ(tool.scale_random_max_factors().y, 1.0F);
  EXPECT_FLOAT_EQ(tool.scale_random_max_factors().z, 3.0F);

  ASSERT_TRUE(tool.set_scale_random_max_factors({1.5F, 0.25F, 4.0F}).ok);
  EXPECT_FLOAT_EQ(tool.scale_random_min_factors().x, 1.5F);
  EXPECT_FLOAT_EQ(tool.scale_random_min_factors().y, 0.25F);
  EXPECT_FLOAT_EQ(tool.scale_random_min_factors().z, 3.0F);
  EXPECT_FLOAT_EQ(tool.scale_random_max_factors().x, 1.5F);
  EXPECT_FLOAT_EQ(tool.scale_random_max_factors().y, 0.25F);
  EXPECT_FLOAT_EQ(tool.scale_random_max_factors().z, 4.0F);
}

TEST(PathArrayToolServiceTests,
     MirroredHierarchyUsesTheSourceRootsAuthoredLocalVariationFrame) {
  PathArrayHarness harness;
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {8.0F, 0.0F, 0.0F});
  const QuaderTransform parent_transform{
      .rotation_euler = {0.2F, -0.1F, 0.35F},
      .scale = {-1.5F, 0.8F, 1.2F},
  };
  const QuaderEntityRef parent = create_empty(
      harness.editor, "Mirrored Parent", kInvalidEntityId, parent_transform);
  const QuaderTransform source_transform{
      .rotation_euler = {-0.15F, 0.25F, 0.4F},
      .scale = {-2.0F, 0.75F, 1.25F},
  };
  const QuaderEntityRef source_root = create_empty(
      harness.editor, "Mirrored Source", parent.id, source_transform);
  const QuaderModelingObjectRef child =
      create_box(harness.editor, "Mirrored Source Mesh");
  ASSERT_TRUE(
      harness.editor.scene().reparent(child.entity, source_root.id, false).ok());
  select_path(harness, path);

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_follow_path(false).ok);
  ASSERT_TRUE(tool.set_count(2U).ok);
  ASSERT_TRUE(tool.set_distance(2.0).ok);
  ASSERT_TRUE(tool.select_source(source_root.id).ok);
  ASSERT_TRUE(
      tool.set_rotation_offset_degrees({0.0F, 0.0F, 30.0F}).ok);
  const PathArrayPreviewState preview = tool.preview_state();

  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.root_world_transforms.size(), 2U);
  EXPECT_EQ(preview.root_world_transforms.front().values,
            harness.session.scene()
                .world_transform_matrix(source_root.id)
                .values);
  const QQuat source_rotation =
      quat_from_euler_radians(source_transform.rotation_euler);
  const QQuat rotation_step = quat_from_euler_radians(
      {0.0F, 0.0F, 30.0F * std::numbers::pi_v<float> / 180.0F});
  const QMat4 expected_local = compose_transform(QTransform{
      .position = transform_point(
          inverse(harness.session.scene().world_transform_matrix(parent.id)),
          {2.0F, 0.0F, 0.0F}),
      .rotation = normalize(source_rotation * rotation_step),
      .scale = source_transform.scale,
  });
  const QMat4 expected_world = scene_transform_multiply(
      harness.session.scene().world_transform_matrix(parent.id),
      expected_local);
  expect_matrix_near(preview.root_world_transforms[1U], expected_world);
}

TEST(PathArrayToolServiceTests,
     InvalidSourcesAreInertAndValidReplacementPublishesWholePreview) {
  PathArrayHarness harness;
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {9.0F, 0.0F, 0.0F});
  const QuaderModelingObjectRef first =
      create_box(harness.editor, "First Source");
  const QuaderModelingObjectRef second =
      create_box(harness.editor, "Second Source");
  const QuaderModelingObjectRef other_curve =
      create_path(harness.editor, {0.0F, 9.0F, 0.0F}, "Not A Source");
  const QuaderEntityRef empty = create_empty(harness.editor, "Empty Leaf");
  select_path(harness, path);

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.select_source(first.entity).ok);
  const PathArrayPreviewState first_preview = tool.preview_state();
  ASSERT_TRUE(first_preview.active);
  ASSERT_EQ(first_preview.root_world_transforms.size(), 4U);
  ASSERT_EQ(first_preview.scene.meshes.size(), 1U);
  EXPECT_EQ(first_preview.scene.meshes.front().instance_transforms.size(), 4U);
  EXPECT_EQ(first_preview.scene.suppression.entities,
            std::vector<EntityId>{first.entity});
  EXPECT_TRUE(harness.session.scene().visibility(first.entity)->visible)
      << "preview suppression must not edit SceneDocument visibility";
  const PathArrayLayoutWorkStats first_work =
      tool.layout_work_stats_for_tests();

  for (const EntityId invalid : {path.entity, other_curve.entity, empty.id,
                                 kInvalidEntityId}) {
    const CommandResult rejected = tool.select_source(invalid);
    EXPECT_FALSE(rejected.ok);
    EXPECT_EQ(tool.source_root(), first.entity);
    EXPECT_EQ(tool.preview_state().revision, first_preview.revision);
    EXPECT_EQ(harness.session.scene().selected_entities(),
              std::vector<EntityId>{first.entity});
  }
  EXPECT_EQ(tool.layout_work_stats_for_tests().backbone_build_count,
            first_work.backbone_build_count);
  EXPECT_EQ(tool.layout_work_stats_for_tests().layout_evaluation_count,
            first_work.layout_evaluation_count);

  const CommandResult replaced = tool.select_source(second.entity);
  ASSERT_TRUE(replaced.ok) << replaced.message;
  EXPECT_EQ(tool.source_root(), second.entity);
  const PathArrayPreviewState &second_preview = tool.preview_state();
  ASSERT_TRUE(second_preview.active);
  EXPECT_GT(second_preview.revision, first_preview.revision);
  EXPECT_EQ(second_preview.scene.suppression.entities,
            std::vector<EntityId>{second.entity});
  const PathArrayLayoutWorkStats second_work =
      tool.layout_work_stats_for_tests();
  EXPECT_EQ(second_work.backbone_build_count,
            first_work.backbone_build_count + 1U);
  EXPECT_EQ(second_work.layout_evaluation_count,
            first_work.layout_evaluation_count + 2U);
  EXPECT_TRUE(harness.session.scene().visibility(first.entity)->visible);
  EXPECT_TRUE(harness.session.scene().visibility(second.entity)->visible);
}

TEST(PathArrayToolServiceTests,
     RandomPatternReturnsExactlyWhenAFormerSourceIsReselected) {
  PathArrayHarness harness;
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {12.0F, 0.0F, 0.0F});
  const QuaderModelingObjectRef first =
      create_box(harness.editor, "First Pattern Source");
  const QuaderModelingObjectRef second =
      create_box(harness.editor, "Second Pattern Source");
  select_path(harness, path);

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_follow_path(false).ok);
  ASSERT_TRUE(tool.select_source(first.entity).ok);
  ASSERT_TRUE(
      tool.set_rotation_random_min_degrees({-25.0F, -15.0F, -5.0F}).ok);
  ASSERT_TRUE(
      tool.set_rotation_random_max_degrees({25.0F, 15.0F, 5.0F}).ok);
  ASSERT_TRUE(
      tool.set_scale_random_min_factors({0.5F, 0.6F, 0.7F}).ok);
  ASSERT_TRUE(
      tool.set_scale_random_max_factors({1.5F, 1.6F, 1.7F}).ok);
  const PathArrayPreviewState first_pattern = tool.preview_state();
  ASSERT_TRUE(first_pattern.active);

  ASSERT_TRUE(tool.select_source(second.entity).ok);
  const PathArrayPreviewState second_pattern = tool.preview_state();
  ASSERT_TRUE(second_pattern.active);
  EXPECT_NE(second_pattern.root_world_transforms[1U].values,
            first_pattern.root_world_transforms[1U].values)
      << "different stable source identities should receive different seeds";

  ASSERT_TRUE(tool.select_source(first.entity).ok);
  const PathArrayPreviewState returned_pattern = tool.preview_state();
  ASSERT_TRUE(returned_pattern.active);
  ASSERT_EQ(returned_pattern.root_world_transforms.size(),
            first_pattern.root_world_transforms.size());
  for (std::size_t index = 0U;
       index < first_pattern.root_world_transforms.size(); ++index) {
    EXPECT_EQ(returned_pattern.root_world_transforms[index].values,
              first_pattern.root_world_transforms[index].values)
        << "source reselection rerolled station " << index;
  }
}

TEST(PathArrayToolServiceTests,
     ShadowLightBudgetDynamicallyLowersCountWithoutDroppingParticipants) {
  const auto defaults = std::make_shared<PathArraySessionDefaults>();
  PathArrayHarness harness(defaults);
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {12.0F, 0.0F, 0.0F});
  const QuaderEntityRef group = create_empty(harness.editor, "Light Group");
  constexpr std::size_t kShadowLightCount = 5U;
  std::vector<EntityId> lights;
  for (std::size_t index = 0U; index < kShadowLightCount; ++index) {
    const QuaderApiResult<QuaderEntityRef> created =
        harness.editor.scene().create_point_light({
            .name = "Shadow Light " + std::to_string(index),
            .parent = group.id,
            .light = {.type = QuaderLightType::Point,
                      .casts_shadows = true},
        });
    ASSERT_TRUE(created.ok()) << created.error_message();
    lights.push_back(created.value().id);
  }
  select_path(harness, path);

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_count(7U).ok);
  const double distance_before_source = tool.distance();
  const double maximum_distance_before_source = tool.maximum_distance();
  ASSERT_TRUE(tool.select_source(group.id).ok);
  EXPECT_EQ(tool.maximum_count(), 3U)
      << "16 shadow placements divided by five lights allows three roots";
  EXPECT_EQ(tool.count(), 3U);
  EXPECT_DOUBLE_EQ(tool.distance(), distance_before_source);
  EXPECT_DOUBLE_EQ(tool.maximum_distance(), maximum_distance_before_source);
  EXPECT_FALSE(tool.set_count(4U).ok);
  EXPECT_EQ(defaults->count, 7U);

  const PathArrayPreviewState &preview = tool.preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.scene.lights.size(), kShadowLightCount);
  for (const ModelingToolPreviewLight &light : preview.scene.lights) {
    EXPECT_TRUE(light.light.casts_shadows);
    EXPECT_EQ(light.instance_transforms.size(), 3U);
  }
  ASSERT_EQ(preview.scene.suppression.entities.size(),
            kShadowLightCount + 1U);
  EXPECT_NE(std::ranges::find(preview.scene.suppression.entities, group.id),
            preview.scene.suppression.entities.end());
  for (const EntityId light : lights) {
    EXPECT_NE(std::ranges::find(preview.scene.suppression.entities, light),
              preview.scene.suppression.entities.end());
  }
  ASSERT_TRUE(tool.cancel().ok);
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_EQ(tool.count(), 7U)
      << "a temporary source-capacity clamp must not replace the authored count";
}

TEST(PathArrayToolServiceTests,
     GroupContainingDrivingCurveIsRejectedWithoutReplacingSelection) {
  PathArrayHarness harness;
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {8.0F, 0.0F, 0.0F});
  const QuaderEntityRef group = create_empty(harness.editor, "Path Owner");
  const QuaderModelingObjectRef sibling = create_box(harness.editor);
  ASSERT_TRUE(harness.editor.scene().reparent(path.entity, group.id, false).ok());
  ASSERT_TRUE(
      harness.editor.scene().reparent(sibling.entity, group.id, false).ok());
  select_path(harness, path);

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_FALSE(tool.resolve_source_candidate(group.id).has_value());
  const CommandResult selected = tool.select_source(group.id);
  EXPECT_FALSE(selected.ok);
  EXPECT_EQ(tool.stage(), PathArrayToolStage::AwaitingSource);
  EXPECT_EQ(tool.source_root(), kInvalidEntityId);
  EXPECT_EQ(harness.session.scene().selected_entities(),
            std::vector<EntityId>{path.entity});
}

TEST(PathArrayToolServiceTests,
     ApplyReusesOriginalDuplicatesFullHierarchyAndUndoRedoExactCurveBaseline) {
  PathArrayHarness harness;
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {10.0F, 0.0F, 0.0F});
  const QuaderEntityRef source_root =
      create_empty(harness.editor, "Path Array Hierarchy_007");
  const QuaderModelingObjectRef child =
      create_box(harness.editor, "Hierarchy Mesh_007");
  ASSERT_TRUE(
      harness.editor.scene().reparent(child.entity, source_root.id, false).ok());
  ASSERT_TRUE(harness.editor.scene()
                  .set_transform(source_root.id,
                                 {.position = {3.0F, 2.0F, 0.0F},
                                  .rotation_euler = {0.0F, 0.0F, 0.25F},
                                  .scale = {1.25F, 0.75F, 1.0F}})
                  .ok());
  select_path(harness, path);

  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const CurveControlTarget anchor{
      curve.preview_state().document->points()[0].id,
      CurveControlKind::Anchor};
  const std::array controls{anchor};
  ASSERT_TRUE(curve.edit_control_selection(
      controls, quader_editor_core::SelectionEditOperation::Replace, nullptr,
      anchor));
  const CurveControlSelection control_baseline = curve.control_selection();
  const QMat4 source_world_before =
      harness.session.scene().world_transform_matrix(source_root.id);
  const std::size_t entity_count_before =
      harness.session.scene().entities().size();
  harness.session.history().clear();

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.select_source(source_root.id).ok);
  ASSERT_TRUE(tool.set_count(3U).ok);
  ASSERT_TRUE(tool.set_distance(3.0).ok);
  ASSERT_TRUE(tool.set_follow_path(false).ok);
  const PathArrayPreviewState preview = tool.preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.root_world_transforms.size(), 3U);
  ASSERT_EQ(preview.scene.meshes.size(), 1U);
  EXPECT_EQ(preview.scene.meshes.front().instance_transforms.size(), 3U);

  const CommandResult applied = tool.apply();
  ASSERT_TRUE(applied.ok) << applied.message;
  ASSERT_TRUE(applied.changed);
  EXPECT_FALSE(tool.has_session());
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(), "Path Array");
  EXPECT_EQ(harness.session.scene().entities().size(),
            entity_count_before + 4U);
  EXPECT_EQ(harness.session.scene().selected_entities(),
            std::vector<EntityId>{path.entity});
  EXPECT_EQ(harness.session.selection().mode(), ModelingSelectionMode::Object);
  EXPECT_EQ(curve.control_selection(), control_baseline);

  const std::vector<EntityId> roots = hierarchy_roots_named(
      harness.session.scene(), "Path Array Hierarchy");
  ASSERT_EQ(roots.size(), 3U);
  EXPECT_NE(std::ranges::find(roots, source_root.id), roots.end());
  expect_world_origins_match(roots, harness.session.scene(),
                             preview.root_world_transforms);
  std::vector<std::string> child_names;
  for (const EntityId root : roots) {
    const std::vector<EntityId> children =
        harness.session.scene().children(root);
    ASSERT_EQ(children.size(), 1U);
    EXPECT_EQ(harness.session.scene().kind(children.front()),
              EntityKind::Mesh);
    ASSERT_NE(harness.session.scene().name(children.front()), nullptr);
    child_names.push_back(harness.session.scene().name(children.front())->name);
  }
  std::ranges::sort(child_names);
  EXPECT_EQ(child_names,
            (std::vector<std::string>{"Hierarchy Mesh_007", "Hierarchy Mesh_007_1",
                                      "Hierarchy Mesh_007_2"}));

  std::vector<std::string> uuids;
  std::vector<std::string> names;
  for (const EntityId root : roots) {
    uuids.push_back(harness.session.scene().uuid(root));
    names.push_back(harness.session.scene().name(root)->name);
  }
  auto allocated_names = names;
  std::ranges::sort(allocated_names);
  EXPECT_EQ(allocated_names,
            (std::vector<std::string>{"Path Array Hierarchy_007",
                                      "Path Array Hierarchy_007_1",
                                      "Path Array Hierarchy_007_2"}));

  const CommandResult undone = harness.session.history().undo();
  ASSERT_TRUE(undone.ok) << undone.message;
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_EQ(harness.session.scene().entities().size(), entity_count_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         source_root.id),
                     source_world_before);
  for (const EntityId root : roots) {
    EXPECT_EQ(harness.session.scene().exists(root), root == source_root.id);
  }
  EXPECT_EQ(harness.session.scene().selected_entities(),
            std::vector<EntityId>{path.entity});
  EXPECT_EQ(harness.session.selection().mode(), ModelingSelectionMode::Object);
  EXPECT_EQ(curve.control_selection(), control_baseline);

  const CommandResult redone = harness.session.history().redo();
  ASSERT_TRUE(redone.ok) << redone.message;
  EXPECT_EQ(harness.session.scene().entities().size(),
            entity_count_before + 4U);
  for (std::size_t index = 0U; index < roots.size(); ++index) {
    ASSERT_TRUE(harness.session.scene().exists(roots[index]));
    EXPECT_EQ(harness.session.scene().uuid(roots[index]), uuids[index]);
    ASSERT_NE(harness.session.scene().name(roots[index]), nullptr);
    EXPECT_EQ(harness.session.scene().name(roots[index])->name, names[index]);
  }
  expect_world_origins_match(roots, harness.session.scene(),
                             preview.root_world_transforms);
  EXPECT_EQ(harness.session.scene().selected_entities(),
            std::vector<EntityId>{path.entity});
  EXPECT_EQ(harness.session.selection().mode(), ModelingSelectionMode::Object);
  EXPECT_EQ(curve.control_selection(), control_baseline);
}

TEST(PathArrayToolServiceTests,
     VariedPreviewApplyUndoAndRedoUseTheExactSameRootMatrices) {
  PathArrayHarness harness;
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {10.0F, 0.0F, 0.0F});
  const QuaderModelingObjectRef source =
      create_box(harness.editor, "Varied Matrix Source");
  select_path(harness, path);
  harness.session.history().clear();
  const std::size_t entity_count_before =
      harness.session.scene().entities().size();
  const QMat4 source_before =
      harness.session.scene().world_transform_matrix(source.entity);

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_follow_path(false).ok);
  ASSERT_TRUE(tool.set_count(3U).ok);
  ASSERT_TRUE(tool.set_distance(2.0).ok);
  ASSERT_TRUE(tool.select_source(source.entity).ok);
  ASSERT_TRUE(
      tool.set_rotation_offset_degrees({5.0F, 10.0F, 15.0F}).ok);
  ASSERT_TRUE(tool.set_scale_offset_factors({1.1F, 0.9F, 1.2F}).ok);
  const PathArrayPreviewState preview = tool.preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.root_world_transforms.size(), 3U);

  const CommandResult applied = tool.apply();
  ASSERT_TRUE(applied.ok) << applied.message;
  const std::vector<EntityId> roots = hierarchy_roots_named(
      harness.session.scene(), "Varied Matrix Source");
  ASSERT_EQ(roots.size(), 3U);
  ASSERT_EQ(roots.front(), source.entity);
  for (std::size_t index = 0U; index < roots.size(); ++index) {
    expect_matrix_near(harness.session.scene().world_transform_matrix(
                           roots[index]),
                       preview.root_world_transforms[index]);
  }

  const CommandResult undone = harness.session.history().undo();
  ASSERT_TRUE(undone.ok) << undone.message;
  EXPECT_EQ(harness.session.scene().entities().size(), entity_count_before);
  expect_matrix_near(
      harness.session.scene().world_transform_matrix(source.entity),
      source_before);

  const CommandResult redone = harness.session.history().redo();
  ASSERT_TRUE(redone.ok) << redone.message;
  for (std::size_t index = 0U; index < roots.size(); ++index) {
    ASSERT_TRUE(harness.session.scene().exists(roots[index]));
    expect_matrix_near(harness.session.scene().world_transform_matrix(
                           roots[index]),
                       preview.root_world_transforms[index]);
  }
}

TEST(PathArrayToolServiceTests,
     AtomicPreviewRetainsLastValidSnapshotAfterUnrepresentableSetting) {
  const auto defaults = std::make_shared<PathArraySessionDefaults>();
  PathArrayHarness harness(defaults);
  const QuaderModelingObjectRef path =
      create_path(harness.editor, {8.0F, 8.0F, 0.0F}, "Diagonal Path");
  QuaderTransform parent_transform;
  parent_transform.scale = {2.0F, 1.0F, 1.0F};
  const QuaderEntityRef parent = create_empty(
      harness.editor, "Nonuniform Parent", kInvalidEntityId, parent_transform);
  const QuaderModelingObjectRef source = create_box(harness.editor);
  ASSERT_TRUE(harness.editor.scene().reparent(source.entity, parent.id, false).ok());
  select_path(harness, path);
  harness.session.history().clear();
  const std::size_t entity_count_before =
      harness.session.scene().entities().size();
  const QMat4 source_world_before =
      harness.session.scene().world_transform_matrix(source.entity);

  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  const CommandResult rejected_first = tool.select_source(source.entity);
  EXPECT_FALSE(rejected_first.ok);
  EXPECT_FALSE(tool.has_source());
  EXPECT_FALSE(tool.preview_valid())
      << "a 45-degree world alignment below nonuniform scale requires shear";
  EXPECT_FALSE(tool.preview_state().active);
  EXPECT_TRUE(harness.session.scene().visibility(source.entity)->visible)
      << "a failed first preview must leave the authored source visible";
  EXPECT_EQ(harness.session.scene().selected_entities(),
            std::vector<EntityId>{path.entity});

  ASSERT_TRUE(tool.set_follow_path(false).ok);
  EXPECT_FALSE(defaults->follow_path);
  ASSERT_TRUE(tool.select_source(source.entity).ok);
  ASSERT_TRUE(tool.preview_valid());
  const PathArrayPreviewState valid = tool.preview_state();
  ASSERT_TRUE(valid.active);
  ASSERT_FALSE(valid.scene.suppression.entities.empty());
  ASSERT_TRUE(tool.set_follow_path(true).ok);
  EXPECT_FALSE(tool.preview_valid());
  ASSERT_TRUE(tool.set_show_bounding_box_dimensions(false).ok);
  EXPECT_FALSE(defaults->show_bounding_box_dimensions)
      << "visual options are accepted independently of a rejected layout";
  const PathArrayPreviewState &retained = tool.preview_state();
  EXPECT_TRUE(retained.active);
  EXPECT_EQ(retained.revision, valid.revision);
  EXPECT_EQ(retained.source_root, valid.source_root);
  EXPECT_EQ(retained.scene.suppression.entities,
            valid.scene.suppression.entities);
  EXPECT_EQ(retained.root_world_transforms.size(),
            valid.root_world_transforms.size());
  EXPECT_FALSE(defaults->follow_path)
      << "a rejected preview must not replace the remembered valid options";

  const CommandResult applied = tool.apply();
  EXPECT_FALSE(applied.ok);
  EXPECT_EQ(harness.session.scene().entities().size(), entity_count_before);
  expect_matrix_near(
      harness.session.scene().world_transform_matrix(source.entity),
      source_world_before);
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_TRUE(tool.has_session());
  EXPECT_EQ(tool.source_root(), source.entity);
  EXPECT_FALSE(defaults->follow_path);
  ASSERT_TRUE(tool.cancel().ok);
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_FALSE(tool.follow_path());
  EXPECT_FALSE(tool.show_bounding_box_dimensions());
}

} // namespace
} // namespace quader
