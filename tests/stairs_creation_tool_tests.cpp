////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/tool_session_defaults.hpp"
#include "core/localization_service.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QImage>
#include <QTemporaryDir>
#include <QStyle>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace quader {
namespace {

ModelingBoxPreview stairs_placement(float width = 4.0F,
                                    float height = 2.0F,
                                    float depth = 8.0F) {
  ModelingBoxPreview preview;
  preview.ok = true;
  preview.valid = true;
  preview.drag_state = ModelingBoxDragState::Height;
  preview.corner_count = 8U;
  preview.corners = {{
      {-width * 0.5F, 0.0F, -depth * 0.5F},
      {-width * 0.5F, 0.0F, depth * 0.5F},
      {width * 0.5F, 0.0F, depth * 0.5F},
      {width * 0.5F, 0.0F, -depth * 0.5F},
      {-width * 0.5F, height, -depth * 0.5F},
      {-width * 0.5F, height, depth * 0.5F},
      {width * 0.5F, height, depth * 0.5F},
      {width * 0.5F, height, -depth * 0.5F},
  }};
  return preview;
}

struct StairsMeshSnapshot {
  std::vector<std::array<float, 3U>> positions;
  std::vector<std::uint32_t> indices;
  bool operator==(const StairsMeshSnapshot &) const = default;
};

StairsMeshSnapshot stairs_mesh_snapshot(const QuaderEditorApi &editor) {
  const auto objects = editor.query().modeling_objects();
  EXPECT_EQ(objects.size(), 1U);
  if (objects.size() != 1U) return {};
  const auto object = editor.query().modeling_object_for_id(
      objects.front().modeling_object_id);
  EXPECT_TRUE(object.has_value());
  if (!object) return {};
  const auto mesh = editor.query().compiled_object_mesh(*object);
  EXPECT_TRUE(mesh.has_value());
  if (!mesh) return {};
  StairsMeshSnapshot result;
  result.indices.assign(mesh->indices.begin(), mesh->indices.end());
  for (const auto &vertex : mesh->vertices) {
    result.positions.push_back(
        {vertex.position.x, vertex.position.y, vertex.position.z});
  }
  return result;
}

struct StairsCreationHarness {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};

  void create() {
    const auto activated = editor.tools().begin_stairs_creation();
    ASSERT_TRUE(activated.ok()) << activated.error_message();
    const auto accepted = session.primitive_creation().accept_preview(
        stairs_placement(), "Stairs");
    ASSERT_TRUE(accepted.ok) << accepted.message;
  }
};

TEST(StairsCreationToolTests, DedicatedCommandExposesGroupedSettingsAndOneMesh) {
  StairsCreationHarness harness;
  const auto activated = harness.editor.commands().execute(
      quader_command::kToolsActivateCreateStairs);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  EXPECT_EQ(harness.editor.tools().active_tool(), "create_stairs");
  const auto settings = harness.editor.tools().active_settings();
  EXPECT_EQ(settings.tool_id, "create_stairs");
  ASSERT_EQ(settings.groups.size(), 4U);
  EXPECT_TRUE(std::ranges::none_of(settings.controls, [](const auto &control) {
    return control.id == "primitive.type" ||
           control.id == "primitive.flip_faces";
  }));
  const auto accepted = harness.session.primitive_creation().accept_preview(
      stairs_placement(), "Stairs");
  ASSERT_TRUE(accepted.ok) << accepted.message;
  EXPECT_EQ(harness.editor.query().modeling_objects().size(), 1U);
  const auto state = harness.editor.tools().stairs_creation_state();
  ASSERT_TRUE(state.has_value());
  EXPECT_TRUE(state->preview_active);
  EXPECT_EQ(state->actual_step_count, 10U);
  EXPECT_DOUBLE_EQ(state->specification.height, 2.0);
  EXPECT_NEAR(state->riser_height, 0.2, 1.0e-12);
  EXPECT_FALSE(stairs_mesh_snapshot(harness.editor).indices.empty());
  const auto placed_settings = harness.editor.tools().active_settings();
  for (const char *id : {"stairs.width", "stairs.depth", "stairs.height", "stairs.preferred_riser"}) {
    const auto control = std::ranges::find(placed_settings.controls, id,
                                          &QuaderToolSettingControl::id);
    ASSERT_NE(control, placed_settings.controls.end()) << id;
    EXPECT_DOUBLE_EQ(control->number_grid_scale, 1.0) << id;
  }
}

TEST(StairsCreationToolTests, StepEditsFitHeightAndInvalidEditsRetainPreview) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.automatic_step_count",
                                false).ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.step_count", 7.0).ok());
  const auto accepted = tools.stairs_creation_state();
  ASSERT_TRUE(accepted.has_value());
  EXPECT_EQ(accepted->actual_step_count, 7U);
  EXPECT_NEAR(accepted->riser_height, 2.0 / 7.0, 1.0e-12);
  const auto mesh = stairs_mesh_snapshot(harness.editor);
  const auto content_revision = harness.session.modeling().content_revision();
  const auto settings_revision = tools.active_settings_revision();
  const auto undo_count = harness.session.history().undo_count();
  for (const double invalid : {0.0, -1.0, 7.5,
                               std::numeric_limits<double>::infinity()}) {
    SCOPED_TRACE(invalid);
    const auto rejected = tools.set_setting("create_stairs", "stairs.step_count", invalid);
    EXPECT_FALSE(rejected.ok());
    ASSERT_TRUE(tools.has_active_stairs_creation());
    EXPECT_EQ(stairs_mesh_snapshot(harness.editor), mesh);
    EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
    EXPECT_EQ(tools.active_settings_revision(), settings_revision);
    EXPECT_EQ(harness.session.history().undo_count(), undo_count);
  }
  EXPECT_EQ(tools.stairs_creation_state()->revision, accepted->revision);
}

TEST(StairsCreationToolTests, UnchangedReadsAndSettingsDoNotRepublishGeometry) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  const auto initial = tools.stairs_creation_state();
  ASSERT_TRUE(initial.has_value());
  const auto settings_revision = tools.active_settings_revision();
  const auto geometry_revision = harness.session.modeling().content_revision();
  const auto projection = harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  for (int frame = 0; frame < 20; ++frame) {
    const auto unchanged = tools.set_setting("create_stairs", "stairs.step_count",
                                             double(initial->specification.step_count));
    ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
    EXPECT_FALSE(unchanged.changed());
    EXPECT_EQ(tools.stairs_creation_state()->revision, initial->revision);
    EXPECT_EQ(tools.active_settings_revision(), settings_revision);
    EXPECT_EQ(harness.session.modeling().content_revision(), geometry_revision);
  }
  EXPECT_EQ(harness.session.modeling().modeling_sdk_projection_stats_for_tests()
                .native_selection_projection_build_count,
            projection.native_selection_projection_build_count);
}

TEST(StairsCreationToolTests, NarrowLFootprintsFitTheirFlightsAndLanding) {
  StairsCreationHarness harness;
  ASSERT_TRUE(harness.editor.tools().begin_stairs_creation().ok());
  ASSERT_TRUE(harness.editor.tools().set_setting(
      "create_stairs", "stairs.layout", "l").ok());
  const auto accepted = harness.session.primitive_creation().accept_preview(
      stairs_placement(1.0F, 2.0F, 10.0F), "Narrow L");
  ASSERT_TRUE(accepted.ok) << accepted.message;
  EXPECT_EQ(harness.editor.query().modeling_objects().size(), 1U);
  const auto state = harness.editor.tools().stairs_creation_state();
  ASSERT_TRUE(state.has_value());
  EXPECT_LE(state->specification.landing_depth, 1.0);
  EXPECT_DOUBLE_EQ(state->specification.footprint_width, 1.0);
}

TEST(StairsCreationToolTests, ResetValidatesTheUnplacedConfigurationBeforeRememberingIt) {
  StairsCreationHarness harness;
  auto &tools = harness.editor.tools();
  ASSERT_TRUE(tools.begin_stairs_creation().ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.automatic_step_count", false).ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.height", 1000.0).ok());
  const auto before = tools.stairs_creation_state();
  ASSERT_TRUE(before.has_value());
  const auto reset = tools.reset_active_settings();
  EXPECT_FALSE(reset.ok());
  const auto after = tools.stairs_creation_state();
  ASSERT_TRUE(after.has_value());
  EXPECT_FALSE(after->specification.automatic_step_count);
  EXPECT_DOUBLE_EQ(after->specification.height, before->specification.height);
  EXPECT_EQ(after->revision, before->revision);
}

TEST(StairsCreationToolTests, CalculatedValuesBelongOnlyToTheLiveSelectedShape) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  ASSERT_TRUE(tools.stairs_creation_state()->preview_active);
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  const auto state = tools.stairs_creation_state();
  EXPECT_FALSE(state.has_value());
  EXPECT_FALSE(tools.has_active_stairs_creation());
  EXPECT_EQ(harness.editor.query().modeling_objects().size(), 1U);
  // The selection command follows the accepted creation in history.
  EXPECT_EQ(harness.session.history().undo_count(), 2U);
  const auto settings = tools.active_settings();
  EXPECT_TRUE(std::ranges::none_of(settings.controls, [](const auto &control) {
    return control.id.starts_with("stairs.actual_") || control.id == "stairs.slope_degrees";
  }));
}

TEST(StairsCreationToolTests, FailedPublicationPreservesTheAcceptedDraft) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.automatic_step_count", false).ok());
  const auto before = stairs_mesh_snapshot(harness.editor);
  const auto state = tools.stairs_creation_state();
  ASSERT_TRUE(state.has_value());
  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedAssetReplace);
  const auto rejected = tools.set_setting("create_stairs", "stairs.step_count", 13.0);
  EXPECT_FALSE(rejected.ok());
  EXPECT_TRUE(tools.has_active_stairs_creation());
  EXPECT_EQ(stairs_mesh_snapshot(harness.editor), before);
  ASSERT_TRUE(tools.stairs_creation_state().has_value());
  EXPECT_EQ(tools.stairs_creation_state()->actual_step_count, state->actual_step_count);
  EXPECT_EQ(tools.stairs_creation_state()->specification.step_count,
            state->specification.step_count);
}

TEST(StairsCreationToolTests, ApplyAndToolSwitchProduceOneUndoableMesh) {
  for (const bool switch_tool : {false, true}) {
    SCOPED_TRACE(switch_tool);
    StairsCreationHarness harness;
    ASSERT_NO_FATAL_FAILURE(harness.create());
    ASSERT_TRUE(harness.editor.tools().set_setting(
        "create_stairs", "stairs.surface", "ramp").ok());
    const auto mesh = stairs_mesh_snapshot(harness.editor);
    const auto accepted = switch_tool
        ? harness.editor.tools().activate(ViewportToolId::Select)
        : harness.editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(accepted.ok()) << accepted.error_message();
    EXPECT_FALSE(harness.editor.tools().has_active_stairs_creation());
    EXPECT_EQ(stairs_mesh_snapshot(harness.editor), mesh);
    EXPECT_EQ(harness.session.history().undo_count(), 1U);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_TRUE(harness.editor.query().modeling_objects().empty());
    ASSERT_TRUE(harness.editor.history().redo().ok());
    EXPECT_EQ(stairs_mesh_snapshot(harness.editor), mesh);
  }
}

TEST(StairsCreationToolTests, DimensionHandlesRestoreHeightAndCircularBoundsOnCancel) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  const auto before = stairs_mesh_snapshot(harness.editor);
  ASSERT_TRUE(tools.begin_primitive_resize(QuaderPrimitiveResizeControl::PositiveY).ok());
  ASSERT_TRUE(tools.update_primitive_resize(1.0).ok());
  ASSERT_TRUE(tools.stairs_creation_state().has_value());
  EXPECT_DOUBLE_EQ(tools.stairs_creation_state()->specification.height, 3.0);
  EXPECT_NE(stairs_mesh_snapshot(harness.editor), before);
  ASSERT_TRUE(tools.cancel_primitive_resize().ok());
  EXPECT_DOUBLE_EQ(tools.stairs_creation_state()->specification.height, 2.0);
  EXPECT_EQ(stairs_mesh_snapshot(harness.editor), before);
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.construction", "slab").ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.layout", "spiral").ok());
  const auto circular = stairs_mesh_snapshot(harness.editor);
  const auto initial = tools.live_primitive_creation();
  ASSERT_TRUE(initial.has_value());
  ASSERT_TRUE(tools.begin_primitive_resize(QuaderPrimitiveResizeControl::PositiveX).ok());
  ASSERT_TRUE(tools.update_primitive_resize(1.0).ok());
  const auto resized = tools.live_primitive_creation();
  ASSERT_TRUE(resized.has_value());
  EXPECT_NEAR(resized->local_max.x - resized->local_min.x,
              resized->local_max.z - resized->local_min.z, 1.0e-6F);
  EXPECT_FLOAT_EQ(resized->local_min.x, initial->local_min.x);
  ASSERT_TRUE(tools.cancel_primitive_resize().ok());
  EXPECT_EQ(stairs_mesh_snapshot(harness.editor), circular);
}

TEST(StairsCreationToolTests, CancelRemovesDraftAndRemembersAcceptedOptions) {
  auto defaults = std::make_shared<ToolSessionDefaults>();
  SceneSession session({}, {}, defaults);
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().begin_primitive_creation(
      QuaderPrimitiveCreationShape::Cylinder).ok());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(editor.tools().begin_stairs_creation().ok());
  ASSERT_TRUE(session.primitive_creation().accept_preview(stairs_placement(), "Canceled").ok);
  ASSERT_TRUE(editor.tools().set_setting("create_stairs", "stairs.automatic_step_count", false).ok());
  ASSERT_TRUE(editor.tools().set_setting("create_stairs", "stairs.step_count", 7.0).ok());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_TRUE(editor.query().modeling_objects().empty());
  EXPECT_EQ(session.history().undo_count(), 0U);
  EXPECT_EQ(editor.tools().preferred_primitive_shape(),
            QuaderPrimitiveCreationShape::Cylinder);
  SceneSession reopened({}, {}, defaults);
  QuaderEditorApi other(QuaderApiSessionInternalAccess::from_scene_session(reopened));
  ASSERT_TRUE(other.tools().begin_stairs_creation().ok());
  const auto state = other.tools().stairs_creation_state();
  ASSERT_TRUE(state.has_value());
  EXPECT_FALSE(state->specification.automatic_step_count);
  EXPECT_EQ(state->specification.step_count, 7U);
}

TEST(StairsCreationToolTests, StairEditsPreserveAnotherScenesOrdinaryPrimitiveChoice) {
  auto defaults = std::make_shared<ToolSessionDefaults>();
  SceneSession first({}, {}, defaults);
  SceneSession second({}, {}, defaults);
  QuaderEditorApi first_editor(QuaderApiSessionInternalAccess::from_scene_session(first));
  QuaderEditorApi second_editor(QuaderApiSessionInternalAccess::from_scene_session(second));
  ASSERT_TRUE(first_editor.tools().begin_primitive_creation(
      QuaderPrimitiveCreationShape::Cylinder).ok());
  ASSERT_TRUE(first_editor.tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(first_editor.tools().begin_stairs_creation().ok());
  ASSERT_TRUE(second_editor.tools().begin_primitive_creation(
      QuaderPrimitiveCreationShape::Sphere).ok());
  EXPECT_EQ(first_editor.tools().preferred_primitive_shape(),
            QuaderPrimitiveCreationShape::Sphere);
  ASSERT_TRUE(first_editor.tools().set_setting(
      "create_stairs", "stairs.height", 3.0).ok());
  EXPECT_EQ(defaults->primitive.preferred_regular_kind, PrimitiveCreationKind::Sphere);
  EXPECT_EQ(first_editor.tools().preferred_primitive_shape(),
            QuaderPrimitiveCreationShape::Sphere);
  EXPECT_EQ(second_editor.tools().preferred_primitive_shape(),
            QuaderPrimitiveCreationShape::Sphere);
  ASSERT_TRUE(first_editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(defaults->primitive.preferred_regular_kind, PrimitiveCreationKind::Sphere);
}

TEST(StairsCreationToolTests, PresetsAndSurfaceChangesRegenerateTheSameObject) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  const auto original = harness.editor.query().modeling_objects().front().modeling_object_id;
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.construction", "slab").ok());
  for (const char *layout : {"straight", "l", "u", "switchback", "arc", "spiral"}) {
    SCOPED_TRACE(layout);
    const auto changed = tools.set_setting("create_stairs", "stairs.layout", layout);
    ASSERT_TRUE(changed.ok()) << changed.error_message();
    for (const char *surface : {"stairs", "ramp"}) {
      SCOPED_TRACE(surface);
      const auto surfaced = tools.set_setting("create_stairs", "stairs.surface", surface);
      ASSERT_TRUE(surfaced.ok()) << surfaced.error_message();
      const auto objects = harness.editor.query().modeling_objects();
      ASSERT_EQ(objects.size(), 1U);
      EXPECT_EQ(objects.front().modeling_object_id, original);
      EXPECT_FALSE(stairs_mesh_snapshot(harness.editor).indices.empty());
    }
  }
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.turns", 2.0).ok());
  const auto spiral = stairs_mesh_snapshot(harness.editor);
  const auto rejected = tools.set_setting("create_stairs", "stairs.construction", "solid");
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(stairs_mesh_snapshot(harness.editor), spiral);
  EXPECT_TRUE(tools.has_active_stairs_creation());
}

TEST(StairsCreationToolTests, SaveReopenRetainsOrdinaryEditableMesh) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto root = std::filesystem::path(temporary.path().toStdWString()) / "Project";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(root).ok());
  ASSERT_TRUE(editor.tools().begin_stairs_creation().ok());
  ASSERT_NE(project.active_scene(), nullptr);
  ASSERT_TRUE(project.active_scene()->session->primitive_creation().accept_preview(
      stairs_placement(), "Saved stairs").ok);
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  const auto before = stairs_mesh_snapshot(editor);
  const auto path = ProjectRelativePath::parse_portable("Scenes/Stairs.qdr");
  ASSERT_TRUE(path.ok());
  const auto saved = editor.scene_documents().save_as(project.active_scene_id(), path.value());
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  ASSERT_TRUE(editor.scene_documents().close_active().ok());
  const auto opened = editor.scene_documents().open(path.value());
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  EXPECT_FALSE(editor.tools().has_active_stairs_creation());
  EXPECT_EQ(stairs_mesh_snapshot(editor), before);
  const auto objects = editor.query().modeling_objects();
  ASSERT_EQ(objects.size(), 1U);
  auto mesh = editor.modeling().mesh(objects.front().modeling_object_id);
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(mesh.value().transform().translate({1.0F, 0.0F, 0.0F}).ok());
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(stairs_mesh_snapshot(editor), before);
}

TEST(StairsCreationToolTests, NativeSettingsRemainReadableAtNarrowWidthsAndScaleChanges) {
  int argc = 1;
  char executable[] = "stairs-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  apply_qt_obsidian_application_theme(application.palette());
  auto baseline_font = application.font();
  baseline_font.setPointSizeF(std::max(10.0, baseline_font.pointSizeF()));
  application.setFont(baseline_font);
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources/locales", "en-US"));
  QtToolSettingsPanel panel;
  panel.bind(harness.editor);
  const auto review_dir = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  for (const char *locale : {"en-US", "it-IT"}) {
    ASSERT_TRUE(localization.select_locale(locale));
    panel.set_localization(localization);
    for (const float scale : {1.0F, 2.0F, 1.0F}) {
      const QtUiScaleMetrics metrics(scale);
      panel.setFont(metrics.font(baseline_font));
      panel.apply_ui_scale(metrics);
      panel.resize(metrics.px(340), metrics.px(860));
      panel.refresh();
      panel.show();
      QCoreApplication::processEvents();
      EXPECT_EQ(panel.target_id(), "create_stairs");
      EXPECT_TRUE(panel.has_settings());
      auto *title = panel.findChild<QToolButton *>(
          QStringLiteral("tool_settings.target_title"));
      ASSERT_NE(title, nullptr);
      const QString raw_title = QString::fromStdString(localization.text(
          "tool_settings.target.create_stairs", "Stairs & Ramps"));
      QString button_title = raw_title;
      button_title.replace(QLatin1Char('&'), QStringLiteral("&&"));
      EXPECT_EQ(title->text(), button_title);
      EXPECT_EQ(title->accessibleName(), raw_title);
      EXPECT_EQ(panel.windowTitle(), raw_title);
      EXPECT_LE(panel.minimumSizeHint().width(), panel.width());
      for (auto *field : panel.findChildren<QDoubleSpinBox *>()) {
        if (!field->isVisible()) continue;
        EXPECT_GT(field->width(), field->fontMetrics().horizontalAdvance("100.00"));
      }
      if (!review_dir.isEmpty()) {
        ASSERT_TRUE(QDir().mkpath(review_dir));
        EXPECT_TRUE(panel.grab().save(QDir(review_dir).filePath(
            QStringLiteral("stairs-settings-%1-%2.png")
                .arg(QString::fromUtf8(locale)).arg(int(scale * 100.0F)))));
      }
    }
  }
  apply_qt_application_theme_for_contrast_preference(
      application.style()->standardPalette(), Qt::ContrastPreference::HighContrast);
  panel.refresh();
  QCoreApplication::processEvents();
  EXPECT_TRUE(panel.has_settings());
}

} // namespace
} // namespace quader
