////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/extrude_handle_layout.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "editor/tools/extrude_viewport_interaction.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMetaObject>
#include <QMouseEvent>
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

void drag_and_release_extrude_gizmo(SceneSession &session) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0F, 600.0F);
  camera.focus_origin(6.0F);
  camera.set_view_direction(
      session.extrude().handle_state().world_direction * -1.0F);
  const auto layout = build_extrude_handle_layout(
      camera, session.extrude().handle_state());
  ASSERT_TRUE(layout.valid);
  const auto press = layout.button_center_screen;
  ASSERT_EQ(hit_extrude_handle(layout, press), ExtrudeHandlePart::NewExtrusion);
  ExtrudeViewportInteraction interaction;
  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0F, 600.0F};
  input.mouse_local = input.mouse_screen = press;
  ViewportInputResult result;
  const auto frame = [&](bool pressed, bool down, bool released) {
    return interaction.handle_frame(
        session.extrude(), camera,
        {.input = input,
         .input_result = result,
         .left_pressed = pressed,
         .left_down = down,
         .left_released = released,
         .left_click_local = press},
        &session.modeling());
  };
  ASSERT_TRUE(frame(true, true, false).claim_interaction_pane);
  input.mouse_local = input.mouse_screen = press + QVec2{0.0F, -30.0F};
  ASSERT_TRUE(frame(false, true, false).command_result.ok);
  ASSERT_TRUE(session.extrude().dragging());
  // The release sample may advance beyond the last held frame.
  input.mouse_local = input.mouse_screen = press + QVec2{0.0F, -45.0F};
  const auto released = frame(false, false, true);
  ASSERT_TRUE(released.command_result.ok) << released.command_result.message;
  EXPECT_FALSE(interaction.dragging());
  EXPECT_FALSE(session.extrude().dragging());
  EXPECT_TRUE(session.extrude().preview_active());
}

TEST(QtExtrudeToolSettingsTests,
     TypeAmountAndApplyEditOnePendingResultAndPreserveDecimalPrecision) {
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
  expect_extrude_defaults(editor, "region", 0.0);
  const auto undo_count = session.history().undo_count();

  QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  auto *type = panel.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.extrude.type"));
  QPointer<QDoubleSpinBox> amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  auto *apply = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.apply"));
  ASSERT_NE(type, nullptr);
  ASSERT_NE(amount, nullptr);
  ASSERT_NE(apply, nullptr);
  EXPECT_EQ(panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.control.extrude.apply")), nullptr);
  EXPECT_EQ(type->count(), 3);
  EXPECT_GE(amount->decimals(), 2);
  EXPECT_LT(amount->minimum(), 0.0);
  EXPECT_FALSE(apply->isEnabled());

  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  panel.refresh();
  EXPECT_TRUE(apply->isEnabled());
  EXPECT_TRUE(session.extrude().preview_active());
  EXPECT_DOUBLE_EQ(session.extrude().amount(), 0.0);
  EXPECT_EQ(session.history().undo_count(), undo_count);
  const auto after_selection = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(after_selection.has_value());
  expect_same_extrude_geometry(*after_selection, *source);

  const int individual = type->findData(QStringLiteral("individual"));
  ASSERT_GE(individual, 0);
  type->setCurrentIndex(individual);
  amount->setValue(2.15);
  ASSERT_TRUE(QMetaObject::invokeMethod(amount.data(), "editingFinished",
                                       Qt::DirectConnection));
  QCoreApplication::processEvents();
  ASSERT_TRUE(editor.commands().execute(
      quader_command::kToolsActivateExtrude).ok());
  panel.refresh();
  EXPECT_TRUE(apply->isEnabled());
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
  EXPECT_EQ(session.history().undo_count(), undo_count);

  apply->click();
  panel.refresh();
  EXPECT_FALSE(session.extrude().preview_active());
  EXPECT_FALSE(session.extrude().adjustable());
  expect_extrude_defaults(editor, "individual", 0.0);
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_EQ(session.history().undo_count(), undo_count + 1);
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
  auto *apply = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.apply"));
  ASSERT_NE(type, nullptr);
  ASSERT_NE(amount, nullptr);
  ASSERT_NE(apply, nullptr);
  const auto revision = editor.tools().active_settings_revision();
  const auto work = session.extrude().work_stats_for_tests();
  for (int frame = 0; frame < 20; ++frame) {
    panel.refresh();
  }
  EXPECT_EQ(type->currentData().toString(), QStringLiteral("region"));
  EXPECT_DOUBLE_EQ(amount->value(), 0.0);
  EXPECT_EQ(editor.tools().active_settings_revision(), revision);
  EXPECT_EQ(session.extrude().work_stats_for_tests().source_capture_count,
            work.source_capture_count);
  EXPECT_EQ(session.extrude().work_stats_for_tests().geometry_update_count,
            work.geometry_update_count);
  EXPECT_EQ(panel.findChild<QComboBox *>(type->objectName()), type);
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(amount->objectName()), amount);
  EXPECT_EQ(panel.findChild<QPushButton *>(apply->objectName()), apply);

  const auto unchanged = editor.tools().set_setting("extrude", "extrude.amount", 0.0);
  ASSERT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(editor.tools().active_settings_revision(), revision);
  panel.unbind();
}

TEST(QtExtrudeToolSettingsTests,
     CancelDiscardsPendingAmountAndRestoresGeometrySelectionAndRedo) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-extrude-cancel-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto created = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Cancelable Extrusion Source");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.modeling().create_box(
      {.min = {3.0F, -1.0F, -1.0F}, .max = {5.0F, 1.0F, 1.0F}},
      "Preserved Redo").ok());
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  const auto source = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(source.has_value());
  const auto history = editor.history().snapshot();
  const auto undo_count = session.history().undo_count();
  const auto redo_count = session.history().redo_count();
  ASSERT_TRUE(history.can_redo);
  ASSERT_TRUE(editor.commands().execute(
      quader_command::kToolsActivateExtrude).ok());

  QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  auto *cancel = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.cancel"));
  auto *amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  ASSERT_NE(cancel, nullptr);
  ASSERT_NE(amount, nullptr);
  ASSERT_FALSE(cancel->isHidden());
  ASSERT_TRUE(cancel->isEnabled());
  amount->setValue(4.75);
  cancel->click();
  QCoreApplication::processEvents();
  panel.refresh();

  const auto canceled = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(canceled.has_value());
  expect_same_extrude_geometry(*canceled, *source);
  EXPECT_FALSE(session.extrude().preview_active());
  EXPECT_DOUBLE_EQ(session.extrude().amount(), 0.0);
  EXPECT_EQ(editor.selection().summary().selected_face_count, 1U);
  EXPECT_EQ(session.history().undo_count(), undo_count);
  EXPECT_EQ(session.history().redo_count(), redo_count);
  EXPECT_EQ(editor.history().snapshot().redo_label, history.redo_label);
  ASSERT_TRUE(editor.history().redo().ok());
  panel.unbind();
}

TEST(QtExtrudeToolSettingsTests,
     ApplyFlushesTheFinalAmountAndRecordsOneUndoableExtrusion) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-extrude-apply-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto created = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Accepted Extrusion Source");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  const auto source = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(source.has_value());
  const auto undo_count = session.history().undo_count();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  EXPECT_EQ(session.history().undo_count(), undo_count);

  QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  auto *apply = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.apply"));
  auto *amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(amount, nullptr);
  ASSERT_FALSE(apply->isHidden());
  ASSERT_TRUE(apply->isEnabled());
  amount->setValue(2.15);
  apply->click();
  QCoreApplication::processEvents();
  panel.refresh();

  expect_extrude_defaults(editor, "region", 0.0);
  amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  ASSERT_NE(amount, nullptr);
  EXPECT_DOUBLE_EQ(amount->value(), 0.0);
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_FALSE(session.extrude().adjustable());
  const auto accepted = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(accepted.has_value());
  EXPECT_GT(accepted->indices.size(), source->indices.size());
  EXPECT_EQ(session.history().undo_count(), undo_count + 1);
  ASSERT_TRUE(editor.history().undo().ok());
  const auto undone = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(undone.has_value());
  expect_same_extrude_geometry(*undone, *source);
  ASSERT_TRUE(editor.history().redo().ok());
  const auto redone = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(redone.has_value());
  expect_same_extrude_geometry(*redone, *accepted);
  panel.unbind();
}

TEST(QtExtrudeToolSettingsTests,
     ApplyResetsTheAmountAndTheNextEditUsesANewOperationSource) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-extrude-sequential-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto created = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Sequential Extrusion Settings Source");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  const auto source = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(source.has_value());
  const auto undo_count = session.history().undo_count();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  auto *apply = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.apply"));
  auto *cancel = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.cancel"));
  QPointer<QDoubleSpinBox> amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(cancel, nullptr);
  ASSERT_NE(amount, nullptr);
  amount->setValue(2.0);
  apply->click();
  panel.refresh();
  const auto first = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(first.has_value());
  EXPECT_GT(first->indices.size(), source->indices.size());
  EXPECT_EQ(session.history().undo_count(), undo_count + 1);
  EXPECT_FALSE(apply->isEnabled());
  amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  ASSERT_NE(amount, nullptr);
  EXPECT_DOUBLE_EQ(amount->value(), 0.0);

  amount->setValue(3.0);
  ASSERT_TRUE(QMetaObject::invokeMethod(amount.data(), "editingFinished",
                                       Qt::DirectConnection));
  panel.refresh();
  ASSERT_TRUE(session.extrude().preview_active());
  ASSERT_TRUE(apply->isEnabled());
  EXPECT_EQ(session.history().undo_count(), undo_count + 1);
  const auto second_preview = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(second_preview.has_value());
  EXPECT_GT(second_preview->indices.size(), first->indices.size());
  apply->click();
  panel.refresh();
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_DOUBLE_EQ(session.extrude().amount(), 0.0);
  EXPECT_EQ(session.history().undo_count(), undo_count + 2);

  amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  ASSERT_NE(amount, nullptr);
  EXPECT_DOUBLE_EQ(amount->value(), 0.0);
  amount->setValue(4.0);
  ASSERT_TRUE(QMetaObject::invokeMethod(amount.data(), "editingFinished",
                                       Qt::DirectConnection));
  panel.refresh();
  ASSERT_TRUE(session.extrude().preview_active());
  cancel->click();
  panel.refresh();
  EXPECT_DOUBLE_EQ(session.extrude().amount(), 0.0);
  EXPECT_EQ(session.history().undo_count(), undo_count + 2);
  const auto canceled = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(canceled.has_value());
  expect_same_extrude_geometry(*canceled, *second_preview);
  ASSERT_TRUE(editor.history().undo().ok());
  const auto undone_once = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(undone_once.has_value());
  expect_same_extrude_geometry(*undone_once, *first);
  ASSERT_TRUE(editor.history().undo().ok());
  const auto undone_twice = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(undone_twice.has_value());
  expect_same_extrude_geometry(*undone_twice, *source);
  panel.unbind();
}

TEST(QtExtrudeToolSettingsTests,
     ReleasedGizmoKeepsApplyEnabledAndAcceptsTheFinalMouseOrTypedAmount) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-extrude-gizmo-apply-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  for (const bool start_with_preview : {false, true}) {
    for (const bool type_final_amount : {false, true}) {
      SCOPED_TRACE(start_with_preview);
      SCOPED_TRACE(type_final_amount);
      SceneSession session;
      QuaderEditorApi editor(
          QuaderApiSessionInternalAccess::from_scene_session(session));
      const auto created = editor.modeling().create_box(
          {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
          "Gizmo Apply Source");
      ASSERT_TRUE(created.ok()) << created.error_message();
      ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
      const auto source = editor.query().compiled_object_mesh(created.value());
      ASSERT_TRUE(source.has_value());
      const auto undo_count = session.history().undo_count();
      if (!start_with_preview) {
        ASSERT_TRUE(session.extrude().set_amount(0.0).ok);
      }
      ASSERT_TRUE(editor.commands().execute(
          quader_command::kToolsActivateExtrude).ok());
      if (!start_with_preview) {
        ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
        ASSERT_FALSE(session.extrude().preview_active());
      }
      QtToolSettingsPanel panel;
      panel.setAttribute(Qt::WA_DontShowOnScreen, true);
      panel.bind(editor);
      panel.show();
      QApplication::setActiveWindow(&panel);
      QCoreApplication::processEvents();
      const auto source_handle = session.extrude().handle_state();
      ASSERT_NO_FATAL_FAILURE(drag_and_release_extrude_gizmo(session));
      panel.refresh();
      auto *apply = panel.findChild<QPushButton *>(
          QStringLiteral("tool_settings.apply"));
      auto *amount = panel.findChild<QDoubleSpinBox *>(
          QStringLiteral("tool_settings.control.extrude.amount"));
      ASSERT_NE(apply, nullptr);
      ASSERT_NE(amount, nullptr);
      ASSERT_FALSE(apply->isHidden());
      ASSERT_TRUE(apply->isEnabled());
      ASSERT_TRUE(amount->isEnabled());
      EXPECT_EQ(session.history().undo_count(), undo_count);
      const double preview_amount = session.extrude().amount();
      ASSERT_GT(preview_amount, 0.0);
      const auto preview = editor.query().compiled_object_mesh(created.value());
      ASSERT_TRUE(preview.has_value());
      if (type_final_amount) {
        amount->setLocale(QLocale::c());
        amount->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        ASSERT_TRUE(amount->hasFocus());
        auto *line = amount->findChild<QLineEdit *>();
        ASSERT_NE(line, nullptr);
        line->selectAll();
        for (const QChar character : QStringLiteral("2.15")) {
          QKeyEvent text(QEvent::KeyPress, character.unicode(), Qt::NoModifier,
                         QString(character));
          QCoreApplication::sendEvent(line, &text);
        }
        ASSERT_EQ(line->text(), QStringLiteral("2.15"));
        EXPECT_DOUBLE_EQ(session.extrude().amount(), preview_amount);
      }
      const QPoint position = apply->rect().center();
      const QPoint global = apply->mapToGlobal(position);
      QMouseEvent press(QEvent::MouseButtonPress, QPointF(position), QPointF(global),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(apply, &press);
      QMouseEvent release(QEvent::MouseButtonRelease, QPointF(position), QPointF(global),
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(apply, &release);
      panel.refresh();

      EXPECT_FALSE(session.extrude().preview_active());
      EXPECT_FALSE(session.extrude().adjustable());
      EXPECT_DOUBLE_EQ(session.extrude().amount(), 0.0);
      amount = panel.findChild<QDoubleSpinBox *>(
          QStringLiteral("tool_settings.control.extrude.amount"));
      ASSERT_NE(amount, nullptr);
      EXPECT_DOUBLE_EQ(amount->value(), 0.0);
      EXPECT_FALSE(apply->isEnabled());
      EXPECT_EQ(session.history().undo_count(), undo_count + 1);
      const auto accepted = editor.query().compiled_object_mesh(created.value());
      ASSERT_TRUE(accepted.has_value());
      EXPECT_GT(accepted->indices.size(), source->indices.size());
      if (!type_final_amount) {
        expect_same_extrude_geometry(*accepted, *preview);
      } else {
        EXPECT_NEAR(dot(session.extrude().handle_state().world_position -
                            source_handle.world_position,
                        source_handle.world_direction),
                    2.15 / 32.0, 0.00001);
      }
      ASSERT_TRUE(editor.history().undo().ok());
      const auto undone = editor.query().compiled_object_mesh(created.value());
      ASSERT_TRUE(undone.has_value());
      expect_same_extrude_geometry(*undone, *source);
      ASSERT_TRUE(editor.history().redo().ok());
      const auto redone = editor.query().compiled_object_mesh(created.value());
      ASSERT_TRUE(redone.has_value());
      expect_same_extrude_geometry(*redone, *accepted);
      panel.unbind();
    }
  }
}

TEST(QtExtrudeToolSettingsTests,
     CancelAfterGizmoReleaseRestoresSourceAndPreservesHistory) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-extrude-gizmo-cancel-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto created = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Gizmo Cancel Source");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.modeling().create_box(
      {.min = {3.0F, -1.0F, -1.0F}, .max = {5.0F, 1.0F, 1.0F}},
      "Preserved Gizmo Redo").ok());
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  const auto source = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(source.has_value());
  const auto undo_count = session.history().undo_count();
  const auto redo_count = session.history().redo_count();
  ASSERT_TRUE(editor.commands().execute(
      quader_command::kToolsActivateExtrude).ok());
  QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  ASSERT_NO_FATAL_FAILURE(drag_and_release_extrude_gizmo(session));
  panel.refresh();
  auto *cancel = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.cancel"));
  ASSERT_NE(cancel, nullptr);
  ASSERT_FALSE(cancel->isHidden());
  ASSERT_TRUE(cancel->isEnabled());
  cancel->click();
  panel.refresh();

  EXPECT_FALSE(session.extrude().preview_active());
  EXPECT_DOUBLE_EQ(session.extrude().amount(), 0.0);
  const auto canceled = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(canceled.has_value());
  expect_same_extrude_geometry(*canceled, *source);
  EXPECT_EQ(editor.selection().summary().selected_face_count, 1U);
  EXPECT_EQ(session.history().undo_count(), undo_count);
  EXPECT_EQ(session.history().redo_count(), redo_count);
  ASSERT_TRUE(editor.history().redo().ok());
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
  ASSERT_TRUE(session.extrude().set_amount(1.0).ok);
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
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
  expect_extrude_defaults(editor, "region", 0.0);
  ASSERT_TRUE(cancel().ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_EQ(editor.history().snapshot().undo_label, history.undo_label);
}

TEST(QtExtrudeToolSettingsTests,
     SharedModeSurvivesSceneAndProjectChangesWithoutRememberingAppliedAmount) {
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
  const auto created = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Shared Extrusion Mode Source");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(editor, "region", 0.0);
  ASSERT_TRUE(editor.tools()
                  .set_setting("extrude", "extrude.type", "individual")
                  .ok());
  ASSERT_TRUE(editor.tools().set_setting("extrude", "extrude.amount", 2.15).ok());
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  expect_extrude_defaults(editor, "individual", 0.0);
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Select).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(editor, "individual", 0.0);

  const auto second_scene = editor.scene_documents().new_scene();
  ASSERT_TRUE(second_scene.ok()) << second_scene.error_message();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(editor, "individual", 0.0);
  ASSERT_TRUE(editor.scene_documents().activate(*first_scene).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(editor, "individual", 0.0);

  ASSERT_TRUE(editor.tools().set_setting("extrude", "extrude.amount", 3.25).ok());
  ASSERT_TRUE(editor.tools().active_settings().preview_active);
  expect_extrude_defaults(editor, "individual", 3.25);
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene =
          [](const QuaderSceneDocumentInfo &, std::string_view) {
            return QuaderDirtySceneDecision::Discard;
          },
  });
  const auto closed = editor.project().close();
  ASSERT_TRUE(closed.ok()) << closed.error_message();
  const auto second_project = editor.project().create(root / "second-project");
  ASSERT_TRUE(second_project.ok()) << second_project.error_message();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(editor, "individual", 0.0);

  SceneSession fresh_session;
  QuaderEditorApi fresh_editor(
      QuaderApiSessionInternalAccess::from_scene_session(fresh_session));
  ASSERT_TRUE(fresh_editor.tools().activate(ViewportToolId::Extrude).ok());
  expect_extrude_defaults(fresh_editor, "region", 0.0);
}

} // namespace
} // namespace quader
