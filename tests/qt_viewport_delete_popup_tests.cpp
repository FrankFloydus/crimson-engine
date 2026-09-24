////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/qt_viewport_delete_popup.hpp"
#include "viewport/viewport_input_router.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include <array>
#include <string_view>

namespace {

using quader::ModelingSelectionMode;
using quader::ModelingSelectionSummary;
using quader::QtViewportDeletePopupAction;
using quader::QtViewportDeletePopupKind;
using quader::qt_viewport_delete_popup_actions;
using quader::qt_viewport_delete_popup_kind;

const QtViewportDeletePopupAction &find_action(
    const std::array<QtViewportDeletePopupAction, 5> &actions,
    std::string_view id) {
  for (const QtViewportDeletePopupAction &action : actions) {
    if (action.id == id) {
      return action;
    }
  }
  ADD_FAILURE() << "Delete popup action not found: " << id;
  return actions.front();
}

} // namespace

TEST(QtViewportDeletePopupTests,
     ObjectGroupAndSelectedHierarchyRootsUseConfirmation) {
  EXPECT_EQ(qt_viewport_delete_popup_kind(
                {.mode = ModelingSelectionMode::Object}, false),
            QtViewportDeletePopupKind::ObjectConfirmation);
  EXPECT_EQ(qt_viewport_delete_popup_kind(
                {.mode = ModelingSelectionMode::Group}, false),
            QtViewportDeletePopupKind::ObjectConfirmation);
  // An Outliner-selected light or group is a whole-object target even when
  // the modeling editor remains in a component mode.
  EXPECT_EQ(qt_viewport_delete_popup_kind(
                {.mode = ModelingSelectionMode::Vertex}, true),
            QtViewportDeletePopupKind::ObjectConfirmation);

  EXPECT_EQ(qt_viewport_delete_popup_kind(
                {.mode = ModelingSelectionMode::Vertex}, false),
            QtViewportDeletePopupKind::ComponentMenu);
  EXPECT_EQ(qt_viewport_delete_popup_kind(
                {.mode = ModelingSelectionMode::Edge}, false),
            QtViewportDeletePopupKind::ComponentMenu);
  EXPECT_EQ(qt_viewport_delete_popup_kind(
                {.mode = ModelingSelectionMode::Face}, false),
            QtViewportDeletePopupKind::ComponentMenu);
}

TEST(QtViewportDeletePopupTests,
     SubSelectionRoutesDirectlyToCanonicalDeleteCommand) {
  const ModelingSelectionSummary curve_controls{
      .mode = ModelingSelectionMode::Object,
      .has_selection = true,
      .selected_object_count = 1u,
      .sub_selection_kind =
          quader::ModelingSubSelectionKind::CurveControl,
      .selected_sub_element_count = 2u,
      .sub_selection_revision = 7u,
  };

  EXPECT_EQ(qt_viewport_delete_popup_kind(curve_controls, false),
            QtViewportDeletePopupKind::DirectDeleteCommand);
  EXPECT_EQ(qt_viewport_delete_popup_kind(curve_controls, true),
            QtViewportDeletePopupKind::DirectDeleteCommand)
      << "the selected Curve owner must not override its active controls";
}

TEST(QtViewportDeletePopupTests,
     VertexModeEnablesDeleteAndDissolveVertexCommandsOnly) {
  const auto actions = qt_viewport_delete_popup_actions(
      {.mode = ModelingSelectionMode::Vertex, .selected_vertex_count = 2U});

  EXPECT_TRUE(find_action(actions, "delete_vertices").enabled);
  EXPECT_TRUE(find_action(actions, "dissolve_vertices").enabled);
  EXPECT_FALSE(find_action(actions, "delete_face").enabled);
  EXPECT_FALSE(find_action(actions, "delete_edges").enabled);
  EXPECT_FALSE(find_action(actions, "dissolve_edges").enabled);
  EXPECT_EQ(find_action(actions, "delete_vertices").command_id.string(),
            quader::quader_command::kDeleteSelection.string());
  EXPECT_EQ(find_action(actions, "dissolve_vertices").command_id.string(),
            quader::quader_command::kModelingDissolveVertices.string());
}

TEST(QtViewportDeletePopupTests,
     EdgeModeEnablesDeleteAndDissolveEdgeCommandsOnly) {
  const auto actions = qt_viewport_delete_popup_actions(
      {.mode = ModelingSelectionMode::Edge, .selected_edge_count = 3U});

  EXPECT_TRUE(find_action(actions, "delete_edges").enabled);
  EXPECT_TRUE(find_action(actions, "dissolve_edges").enabled);
  EXPECT_FALSE(find_action(actions, "delete_vertices").enabled);
  EXPECT_FALSE(find_action(actions, "dissolve_vertices").enabled);
  EXPECT_FALSE(find_action(actions, "delete_face").enabled);
  EXPECT_EQ(find_action(actions, "delete_edges").command_id.string(),
            quader::quader_command::kDeleteSelection.string());
  EXPECT_EQ(find_action(actions, "dissolve_edges").command_id.string(),
            quader::quader_command::kModelingDissolveEdges.string());
}

TEST(QtViewportDeletePopupTests, FaceModeEnablesDeleteFaceOnly) {
  const auto actions = qt_viewport_delete_popup_actions(
      {.mode = ModelingSelectionMode::Face, .selected_face_count = 1U});

  EXPECT_TRUE(find_action(actions, "delete_face").enabled);
  EXPECT_FALSE(find_action(actions, "delete_vertices").enabled);
  EXPECT_FALSE(find_action(actions, "dissolve_vertices").enabled);
  EXPECT_FALSE(find_action(actions, "delete_edges").enabled);
  EXPECT_FALSE(find_action(actions, "dissolve_edges").enabled);
  EXPECT_EQ(find_action(actions, "delete_face").command_id.string(),
            quader::quader_command::kDeleteSelection.string());
}

TEST(QtViewportDeletePopupTests, EmptyComponentSelectionDisablesEveryAction) {
  const auto actions = qt_viewport_delete_popup_actions(
      {.mode = ModelingSelectionMode::Vertex});
  for (const QtViewportDeletePopupAction &action : actions) {
    EXPECT_FALSE(action.enabled) << action.id;
  }
}

TEST(QtViewportDeletePopupTests,
     RetainsPrewarmedObjectConfirmationAcrossEveryDismissalAndDelete) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-delete-popup-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setStyle(QStringLiteral("Fusion"));

  QWidget viewport_owner;
  viewport_owner.setFocusPolicy(Qt::StrongFocus);
  viewport_owner.resize(640, 480);
  viewport_owner.show();
  viewport_owner.activateWindow();
  viewport_owner.setFocus(Qt::ShortcutFocusReason);
  QCoreApplication::processEvents();

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto object = editor.modeling().create_box(
      quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                           .max = {1.0f, 1.0f, 1.0f}},
      "Retained Delete Confirmation Box");
  ASSERT_TRUE(object.ok()) << object.error_message();
  ASSERT_TRUE(editor.selection().select_object(object.value()).ok());

  quader::ViewportInputRouter input_router;
  quader::LocalizationService localization;
  quader::QtViewportDeletePopupPresenter presenter(viewport_owner);
  presenter.prewarm();

  QMessageBox *const confirmation =
      viewport_owner.findChild<QMessageBox *>(
          QStringLiteral("viewport_delete_objects_confirmation"),
          Qt::FindDirectChildrenOnly);
  ASSERT_NE(confirmation, nullptr);
  QPushButton *const delete_button = confirmation->findChild<QPushButton *>(
      QStringLiteral("viewportDeleteConfirmation.delete"));
  QPushButton *const cancel_button = confirmation->findChild<QPushButton *>(
      QStringLiteral("viewportDeleteConfirmation.cancel"));
  ASSERT_NE(delete_button, nullptr);
  ASSERT_NE(cancel_button, nullptr);
  QLayout *const retained_layout = confirmation->layout();
  ASSERT_NE(retained_layout, nullptr);

  EXPECT_EQ(confirmation->parentWidget(), &viewport_owner);
  EXPECT_EQ(confirmation->windowModality(), Qt::WindowModal);
  EXPECT_FALSE(confirmation->isVisible());
  EXPECT_TRUE(confirmation->testAttribute(Qt::WA_WState_Created));
  EXPECT_EQ(confirmation->buttonRole(delete_button), QMessageBox::AcceptRole);
  EXPECT_EQ(confirmation->buttonRole(cancel_button), QMessageBox::RejectRole);
  EXPECT_EQ(confirmation->defaultButton(), delete_button);
  EXPECT_EQ(confirmation->escapeButton(), cancel_button);
  const WId retained_window_id = confirmation->winId();

  QTimer::singleShot(0, confirmation, [&]() {
    EXPECT_EQ(confirmation->winId(), retained_window_id);
    EXPECT_EQ(confirmation->objectName(),
              QStringLiteral("viewport_delete_objects_confirmation"));
    EXPECT_EQ(confirmation->windowTitle(), QStringLiteral("Delete"));
    EXPECT_EQ(confirmation->text(),
              QStringLiteral("Delete Selected Objects"));
    EXPECT_EQ(delete_button->text(), QStringLiteral("Delete"));
    EXPECT_EQ(cancel_button->text(), QStringLiteral("Cancel"));
    cancel_button->click();
  });
  EXPECT_FALSE(presenter.show(viewport_owner, {}, editor, session.modeling(),
                              input_router, localization));
  // The foreground window can be withheld by Windows when this suite follows
  // another top-level-widget test. Verify the retained focus target on the
  // viewport window; QApplication::focusWidget() is intentionally null while
  // that window is inactive.
  EXPECT_EQ(viewport_owner.focusWidget(), &viewport_owner);
  EXPECT_TRUE(editor.query().entity(object.value().entity).has_value());
  EXPECT_TRUE(confirmation->testAttribute(Qt::WA_WState_Created));
  EXPECT_EQ(confirmation->winId(), retained_window_id);

  bool escape_closed_confirmation = false;
  QTimer::singleShot(0, confirmation, [&]() {
    EXPECT_EQ(confirmation->winId(), retained_window_id);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(confirmation, &escape);
    escape_closed_confirmation = !confirmation->isVisible();
    if (!escape_closed_confirmation) {
      cancel_button->click();
    }
  });
  EXPECT_FALSE(presenter.show(viewport_owner, {}, editor, session.modeling(),
                              input_router, localization));
  EXPECT_TRUE(escape_closed_confirmation);
  EXPECT_EQ(viewport_owner.focusWidget(), &viewport_owner);
  EXPECT_TRUE(editor.query().entity(object.value().entity).has_value());
  EXPECT_TRUE(confirmation->testAttribute(Qt::WA_WState_Created));
  EXPECT_EQ(confirmation->winId(), retained_window_id);

  bool close_hid_confirmation = false;
  QTimer::singleShot(0, confirmation, [&]() {
    EXPECT_EQ(confirmation->winId(), retained_window_id);
    (void)confirmation->close();
    close_hid_confirmation = !confirmation->isVisible();
  });
  EXPECT_FALSE(presenter.show(viewport_owner, {}, editor, session.modeling(),
                              input_router, localization));
  EXPECT_TRUE(close_hid_confirmation);
  EXPECT_EQ(viewport_owner.focusWidget(), &viewport_owner);
  EXPECT_TRUE(editor.query().entity(object.value().entity).has_value());
  EXPECT_TRUE(confirmation->testAttribute(Qt::WA_WState_Created));
  EXPECT_EQ(confirmation->winId(), retained_window_id);

  QTimer::singleShot(0, confirmation, [&]() {
    EXPECT_EQ(confirmation->winId(), retained_window_id);
    EXPECT_EQ(confirmation->layout(), retained_layout);
    EXPECT_EQ(viewport_owner.findChild<QMessageBox *>(
                  QStringLiteral("viewport_delete_objects_confirmation"),
                  Qt::FindDirectChildrenOnly),
              confirmation);
    EXPECT_EQ(confirmation->findChild<QPushButton *>(
                  QStringLiteral("viewportDeleteConfirmation.delete")),
              delete_button);
    EXPECT_EQ(confirmation->findChild<QPushButton *>(
                  QStringLiteral("viewportDeleteConfirmation.cancel")),
              cancel_button);
    delete_button->click();
  });
  EXPECT_TRUE(presenter.show(viewport_owner, {}, editor, session.modeling(),
                             input_router, localization));
  EXPECT_EQ(viewport_owner.focusWidget(), &viewport_owner);
  EXPECT_TRUE(confirmation->testAttribute(Qt::WA_WState_Created));
  EXPECT_EQ(confirmation->winId(), retained_window_id);
  EXPECT_EQ(confirmation->layout(), retained_layout);
  EXPECT_FALSE(editor.query().entity(object.value().entity).has_value());
}
