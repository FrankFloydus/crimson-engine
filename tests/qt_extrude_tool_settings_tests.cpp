////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "ui/qt_tool_settings_panel.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <string_view>

namespace quader {
namespace {

const QuaderToolSettingControl *find_extrude_control(
    const QuaderToolSettingsTarget &target, std::string_view id) {
  const auto found = std::ranges::find_if(
      target.controls, [id](const auto &control) { return control.id == id; });
  return found == target.controls.end() ? nullptr : &*found;
}

void expect_same_extrude_geometry(const QuaderMeshAsset &actual,
                                  const QuaderMeshAsset &expected) {
  EXPECT_EQ(actual.indices, expected.indices);
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.x,
                    expected.vertices[index].position.x);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.y,
                    expected.vertices[index].position.y);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.z,
                    expected.vertices[index].position.z);
  }
}

void expect_extrude_defaults(QuaderEditorApi &editor, std::string_view type,
                            double amount) {
  const auto settings = editor.tools().active_settings();
  ASSERT_TRUE(settings.active);
  EXPECT_EQ(settings.tool_id, "extrude");
  const auto *type_control = find_extrude_control(settings, "extrude.type");
  const auto *amount_control = find_extrude_control(settings, "extrude.amount");
  ASSERT_NE(type_control, nullptr);
  ASSERT_NE(amount_control, nullptr);
  EXPECT_EQ(type_control->choice_value, type);
  EXPECT_DOUBLE_EQ(amount_control->number_value, amount);
}

TEST(QtExtrudeToolSettingsTests,
     TypeAmountAndExtrudeActionEditOneResultAndPreserveDecimalPrecision) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-extrude-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto created = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Extrusion Settings Source");
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto source = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(editor, "region", 1.0);

  QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  auto *type = panel.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.extrude.type"));
  QPointer<QDoubleSpinBox> amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  auto *extrude = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.control.extrude.apply"));
  ASSERT_NE(type, nullptr);
  ASSERT_NE(amount, nullptr);
  ASSERT_NE(extrude, nullptr);
  EXPECT_EQ(type->count(), 3);
  EXPECT_GE(amount->decimals(), 2);
  EXPECT_LT(amount->minimum(), 0.0);
  EXPECT_FALSE(extrude->isEnabled());

  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  panel.refresh();
  EXPECT_TRUE(extrude->isEnabled());
  const auto after_selection = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(after_selection.has_value());
  expect_same_extrude_geometry(*after_selection, *source);

  const int individual = type->findData(QStringLiteral("individual"));
  ASSERT_GE(individual, 0);
  type->setCurrentIndex(individual);
  amount->setValue(2.15);
  ASSERT_TRUE(QMetaObject::invokeMethod(amount.data(), "editingFinished",
                                       Qt::DirectConnection));
  extrude->click();
  panel.refresh();
  // A new operation publishes its initiating-pane origin and rebuilds the
  // controls. Continue through the newly presented numeric editor.
  amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  ASSERT_NE(amount, nullptr);
  expect_extrude_defaults(editor, "individual", 2.15);
  const auto first_result = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(first_result.has_value());
  EXPECT_GT(first_result->indices.size(), source->indices.size());
  EXPECT_EQ(editor.selection().summary().selected_face_count, 1U);

  amount->setValue(-1.25);
  ASSERT_TRUE(QMetaObject::invokeMethod(amount.data(), "editingFinished",
                                       Qt::DirectConnection));
  QCoreApplication::processEvents();
  panel.refresh();
  expect_extrude_defaults(editor, "individual", -1.25);
  const auto adjusted = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(adjusted.has_value());
  EXPECT_EQ(adjusted->indices.size(), first_result->indices.size());
  EXPECT_EQ(adjusted->vertices.size(), first_result->vertices.size());

  ASSERT_TRUE(editor.history().undo().ok());
  const auto undone = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(undone.has_value());
  expect_same_extrude_geometry(*undone, *source);
  ASSERT_TRUE(editor.history().redo().ok());
  const auto redone = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(redone.has_value());
  expect_same_extrude_geometry(*redone, *adjusted);
  panel.unbind();
}

TEST(QtExtrudeToolSettingsTests,
     IdleRefreshRetainsWidgetsAndSkipsSelectionCaptureAndGeometryWork) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-extrude-retained-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto created = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Retained Extrusion Source");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.refresh();
  auto *type = panel.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.extrude.type"));
  auto *amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  auto *extrude = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.control.extrude.apply"));
  ASSERT_NE(type, nullptr);
  ASSERT_NE(amount, nullptr);
  ASSERT_NE(extrude, nullptr);
  const auto revision = editor.tools().active_settings_revision();
  const auto work = session.extrude().work_stats_for_tests();
  for (int frame = 0; frame < 20; ++frame) {
    panel.refresh();
  }
  EXPECT_EQ(type->currentData().toString(), QStringLiteral("region"));
  EXPECT_DOUBLE_EQ(amount->value(), 1.0);
  EXPECT_EQ(editor.tools().active_settings_revision(), revision);
  EXPECT_EQ(session.extrude().work_stats_for_tests().source_capture_count,
            work.source_capture_count);
  EXPECT_EQ(session.extrude().work_stats_for_tests().geometry_update_count,
            work.geometry_update_count);
  EXPECT_EQ(panel.findChild<QComboBox *>(type->objectName()), type);
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(amount->objectName()), amount);
  EXPECT_EQ(panel.findChild<QPushButton *>(extrude->objectName()), extrude);

  const auto unchanged = editor.tools().set_setting("extrude", "extrude.amount", 1.0);
  ASSERT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(editor.tools().active_settings_revision(), revision);
  panel.unbind();
}

TEST(QtExtrudeToolSettingsTests,
     NativeEscapeDispatchCancelsOnlyTheDragBeforeExitingOnTheNextEscape) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto created = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Extrusion Escape Drag Source");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  const auto committed = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(committed.has_value());
  const auto history = editor.history().snapshot();
  ASSERT_TRUE(session.extrude().begin_drag().ok);
  ASSERT_TRUE(session.extrude().update_drag(3.0).ok);
  ASSERT_TRUE(session.extrude().dragging());
  const ViewportToolState state{
      .active_tool = ViewportToolId::Extrude,
      .selection_mode = ViewportSelectionMode::Face};
  const QuaderInputChord escape{
      .device = QuaderInputDevice::Keyboard,
      .key = QuaderKeyCode::Escape};
  const auto cancel = [&]() {
    const auto dispatch = editor.commands().dispatch_native_tool_settings_input_binding(
        escape, state);
    EXPECT_EQ(dispatch.kind, QuaderNativeToolSettingsDispatch::Kind::Intent);
    EXPECT_EQ(dispatch.intent, QuaderNativeToolSettingsIntent::Cancel);
    return editor.commands().execute_native_tool_settings_intent(dispatch.intent);
  };
  ASSERT_TRUE(cancel().ok());
  EXPECT_EQ(editor.tools().active_tool(), "extrude");
  EXPECT_FALSE(session.extrude().dragging());
  const auto canceled = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(canceled.has_value());
  expect_same_extrude_geometry(*canceled, *committed);
  EXPECT_EQ(editor.selection().summary().selected_face_count, 1U);
  EXPECT_EQ(editor.history().snapshot().undo_label, history.undo_label);
  expect_extrude_defaults(editor, "region", 1.0);
  ASSERT_TRUE(cancel().ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_EQ(editor.history().snapshot().undo_label, history.undo_label);
}

TEST(QtExtrudeToolSettingsTests,
     SharedDefaultsSurviveSceneAndProjectChangesButResetWithANewSession) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto root = std::filesystem::path(temporary.path().toStdWString());
  ASSERT_TRUE(std::filesystem::create_directory(root / "first-project"));
  ASSERT_TRUE(std::filesystem::create_directory(root / "second-project"));
  ProjectSession project(root / "recent-scenes.json");
  QuaderEditorApi editor(project);
  const auto first_project = editor.project().create(root / "first-project");
  ASSERT_TRUE(first_project.ok()) << first_project.error_message();
  const auto first_scene = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(first_scene.has_value());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(editor, "region", 1.0);
  ASSERT_TRUE(editor.tools()
                  .set_setting("extrude", "extrude.type", "individual")
                  .ok());
  ASSERT_TRUE(editor.tools().set_setting("extrude", "extrude.amount", 2.15).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Select).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(editor, "individual", 2.15);

  const auto second_scene = editor.scene_documents().new_scene();
  ASSERT_TRUE(second_scene.ok()) << second_scene.error_message();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(editor, "individual", 2.15);
  ASSERT_TRUE(editor.scene_documents().activate(*first_scene).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(editor, "individual", 2.15);

  ASSERT_TRUE(editor.project().close().ok());
  const auto second_project = editor.project().create(root / "second-project");
  ASSERT_TRUE(second_project.ok()) << second_project.error_message();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(editor, "individual", 2.15);

  SceneSession fresh_session;
  QuaderEditorApi fresh_editor(
      QuaderApiSessionInternalAccess::from_scene_session(fresh_session));
  ASSERT_TRUE(fresh_editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(fresh_editor, "region", 1.0);
}

} // namespace
} // namespace quader
