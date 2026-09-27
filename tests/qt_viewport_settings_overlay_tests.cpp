////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "core/localization_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/scene_session.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_viewport_panel.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPushButton>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QToolButton>
#include <QTimer>

#include <memory>
#include <optional>
#include <array>
#include <filesystem>
#include <string_view>
#include <utility>

namespace {

/// Hosts actual settings and native pane widgets without initializing a renderer.
class QtViewportSettingsOverlayTests : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    session_ = std::make_unique<quader::SceneSession>();
    editor_ = std::make_unique<quader::QuaderEditorApi>(
        quader::QuaderApiSessionInternalAccess::from_scene_session(*session_));
    ASSERT_TRUE(editor_->commands().set_keybinding_storage_path({}).ok());
    ASSERT_TRUE(session_->viewport_workspace().set_layout(
        quader::ViewportLayoutId::FourQuad));
    host_ = std::make_unique<quader::QtViewportPanel>();
    host_->setAttribute(Qt::WA_DontShowOnScreen, true);
    host_->resize(1000, 800);
    host_->attach_workspace(session_->viewport_workspace());
    host_->set_startup_presentation_blocked(false);
    settings_ = new quader::QtToolSettingsPanel;
    host_->set_settings_panel(settings_);
    settings_->bind(*editor_);
    host_->show();
    refresh();
  }

  void TearDown() override {
    if (settings_ != nullptr) {
      settings_->unbind();
    }
    if (host_ != nullptr) {
      host_->abandon_interactions();
    }
    host_.reset();
    settings_ = nullptr;
    editor_.reset();
    session_.reset();
    application_.reset();
  }

  void refresh() {
    settings_->refresh();
    host_->refresh_workspace_layout();
    host_->refresh_settings_overlay(*editor_);
    QCoreApplication::processEvents();
  }

  QFrame *frame(quader::ViewportPaneId pane) const {
    return host_->findChild<QFrame *>(QStringLiteral("viewportPane_%1").arg(
        QString::fromUtf8(quader::viewport_pane_stable_id(pane).data())));
  }

  void expect_anchored(int inset) {
    QWidget *parent = settings_->parentWidget();
    ASSERT_NE(parent, nullptr);
    EXPECT_FALSE(settings_->isHidden());
    EXPECT_TRUE(parent->rect().contains(settings_->geometry()));
    EXPECT_EQ(settings_->x(), inset);
    EXPECT_EQ(parent->height() - settings_->geometry().bottom() - 1, inset);
  }

  void process_input() {
    host_->process_input(*editor_, session_->scene(), session_->assets(),
        session_->modeling(), {}, session_->native_tools(),
        &session_->point_placement(), 1.0 / 60.0);
  }

  void focus_surface(quader::ViewportPaneId pane) {
    auto *previous = host_->surface(session_->viewport_workspace().focused_pane());
    auto *target = host_->surface(pane);
    ASSERT_NE(previous, nullptr);
    ASSERT_NE(target, nullptr);
    // Native pointer transfer also leaves the previous child window. Keeping
    // two synthetic surfaces hovered routes later frames to the stale one.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(previous, &leave);
    QFocusEvent blur(QEvent::FocusOut, Qt::OtherFocusReason);
    QApplication::sendEvent(previous, &blur);
    QFocusEvent focus(QEvent::FocusIn, Qt::OtherFocusReason);
    QApplication::sendEvent(target, &focus);
    ASSERT_EQ(session_->viewport_workspace().focused_pane(), pane);
  }

  void expect_settings_fit_without_scrolling() {
    auto *scroll = settings_->findChild<QScrollArea *>(
        QStringLiteral("tool_settings.scroll"));
    ASSERT_NE(scroll, nullptr);
    ASSERT_TRUE(settings_->expanded());
    ASSERT_TRUE(scroll->isVisible());
    EXPECT_EQ(scroll->verticalScrollBar()->maximum(), 0);
    EXPECT_GE(scroll->viewport()->height(), scroll->widget()->sizeHint().height());
    EXPECT_EQ(settings_->findChild<QPushButton *>(
                  QStringLiteral("tool_settings.reset_defaults")), nullptr);
    for (const auto *name : {"tool_settings.apply", "tool_settings.cancel"}) {
      auto *action = settings_->findChild<QPushButton *>(QString::fromUtf8(name));
      ASSERT_NE(action, nullptr);
      if (action->isVisible()) {
        EXPECT_GE(action->height(), action->minimumSizeHint().height());
      }
    }
    expect_anchored(6);
  }

  int argc_ = 1;
  char application_name_[40] = "quader-viewport-settings-overlay-tests";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
  std::unique_ptr<quader::SceneSession> session_;
  std::unique_ptr<quader::QuaderEditorApi> editor_;
  std::unique_ptr<quader::QtViewportPanel> host_;
  quader::QtToolSettingsPanel *settings_ = nullptr;
  quader::LocalizationService localization_;
};

/// Counts presentation mutations independently of the host's retained-work counter.
class OverlayPresentationEvents final : public QObject {
public:
  int mutations = 0;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::ParentChange:
    case QEvent::ZOrderChange:
      ++mutations;
      break;
    default:
      break;
    }
    return QObject::eventFilter(watched, event);
  }
};

TEST_F(QtViewportSettingsOverlayTests,
       SettingsFollowTheFocusedPaneWithoutActivatingItsRememberedTool) {
  EXPECT_TRUE(settings_->isHidden());
  EXPECT_FALSE(settings_->has_settings());
  EXPECT_TRUE(settings_->expanded());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  refresh();

  ASSERT_TRUE(settings_->has_settings());
  ASSERT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Top));
  EXPECT_FALSE(settings_->isHidden());
  auto *header = settings_->findChild<QToolButton *>(
      QStringLiteral("tool_settings.target_title"));
  ASSERT_NE(header, nullptr);
  EXPECT_EQ(header->text(), QStringLiteral("Create Box"));
  EXPECT_TRUE(header->isChecked());
  EXPECT_EQ(header->arrowType(), Qt::DownArrow);
  header->click();
  refresh();
  EXPECT_FALSE(settings_->expanded());
  ASSERT_TRUE(editor_->tools().set_setting(quader::kCreatePrimitiveToolId,
      quader::kBoxSubdivisionsXSetting, 4.0).ok());
  refresh();
  EXPECT_FALSE(settings_->expanded());
  const auto origin = editor_->tools().active_settings_presentation_origin();
  const auto tool_revision = editor_->tools().active_tool_revision();
  const auto settings_revision = editor_->tools().active_settings_revision();
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Front));
  refresh();
  EXPECT_FALSE(settings_->has_settings());
  EXPECT_TRUE(settings_->isHidden());
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
  EXPECT_EQ(editor_->tools().active_tool_revision(), tool_revision);
  EXPECT_EQ(editor_->tools().active_settings_revision(), settings_revision);
  EXPECT_TRUE(editor_->tools().has_active_primitive_creation());
  EXPECT_EQ(session_->viewport_workspace().selected_tool(quader::ViewportPaneId::Top),
            quader::ViewportToolId::CreatePrimitive);
  EXPECT_EQ(session_->viewport_workspace().selected_tool(quader::ViewportPaneId::Front),
            quader::ViewportToolId::Select);
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  refresh();
  EXPECT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Top));
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_TRUE(settings_->expanded());

  // A complete cancel/start between refreshes still represents a new operation.
  ASSERT_TRUE(editor_->tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Side));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  refresh();
  EXPECT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Side));
  EXPECT_TRUE(settings_->expanded());

  settings_->set_expanded(false);
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Select).ok());
  refresh();
  EXPECT_FALSE(settings_->has_settings());
  EXPECT_TRUE(settings_->isHidden());
  EXPECT_FALSE(header->isVisible());
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  refresh();
  EXPECT_TRUE(settings_->expanded());
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Side));
}

TEST_F(QtViewportSettingsOverlayTests,
       FocusPresentsMoveSettingsAndHidesRotateScaleWithoutSettlingTheActiveTool) {
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Move).ok());
  ASSERT_TRUE(editor_->tools().set_setting("move", "auto_merge_vertices", false).ok());
  refresh();
  ASSERT_EQ(settings_->target_id(), "move");
  auto *auto_merge = settings_->findChild<QCheckBox *>(
      QStringLiteral("tool_settings.control.auto_merge_vertices"));
  ASSERT_NE(auto_merge, nullptr);
  EXPECT_FALSE(auto_merge->isChecked());

  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Side));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Rotate).ok());
  refresh();
  EXPECT_FALSE(settings_->has_settings());
  EXPECT_TRUE(settings_->isHidden());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Front));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Scale).ok());
  refresh();
  EXPECT_FALSE(settings_->has_settings());
  EXPECT_TRUE(settings_->isHidden());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Perspective));
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(
      quader::QuaderPrimitiveCreationShape::Box).ok());
  refresh();
  ASSERT_TRUE(settings_->has_settings());
  const auto origin = editor_->tools().active_settings_presentation_origin();
  const auto active_tool = editor_->tools().active_tool();
  const auto tool_revision = editor_->tools().active_tool_revision();
  const auto settings_revision = editor_->tools().active_settings_revision();

  for (const auto pane : {quader::ViewportPaneId::Top,
                          quader::ViewportPaneId::Side,
                          quader::ViewportPaneId::Front,
                          quader::ViewportPaneId::Perspective}) {
    SCOPED_TRACE(quader::viewport_pane_stable_id(pane));
    ASSERT_TRUE(session_->viewport_workspace().focus_pane(pane));
    refresh();
    process_input();
    refresh();
    EXPECT_EQ(editor_->tools().active_tool(), active_tool);
    EXPECT_EQ(editor_->tools().active_tool_revision(), tool_revision);
    EXPECT_EQ(editor_->tools().active_settings_revision(), settings_revision);
    EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
    EXPECT_TRUE(editor_->tools().has_active_primitive_creation());
    EXPECT_EQ(session_->primitive_creation().shape_count(), 0u);
    EXPECT_TRUE(session_->modeling().objects().empty());
    EXPECT_FALSE(editor_->history().can_undo());
    if (pane == quader::ViewportPaneId::Top) {
      ASSERT_EQ(settings_->target_id(), "move");
      EXPECT_EQ(settings_->parentWidget(), frame(pane));
      auto_merge = settings_->findChild<QCheckBox *>(
          QStringLiteral("tool_settings.control.auto_merge_vertices"));
      ASSERT_NE(auto_merge, nullptr);
      EXPECT_TRUE(auto_merge->isVisible());
      EXPECT_FALSE(auto_merge->isChecked());
    } else if (pane == quader::ViewportPaneId::Perspective) {
      EXPECT_TRUE(settings_->has_settings());
      EXPECT_FALSE(settings_->isHidden());
      EXPECT_EQ(settings_->parentWidget(), frame(pane));
    } else {
      EXPECT_FALSE(settings_->has_settings());
      EXPECT_TRUE(settings_->isHidden());
    }
  }
}

TEST_F(QtViewportSettingsOverlayTests,
       AllArrayToolsExposeDefaultOnDimensionsCheckboxWithoutHistoryChanges) {
  quader::apply_qt_obsidian_application_theme(QApplication::palette());
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(review_directory));
  const auto box = editor_->modeling().create_box(
      {.min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f}}, "Array source");
  const auto path = editor_->modeling().create_curve({
      .points = {{.position = {}}, {.position = {4.0f, 0.0f, 0.0f}}}, .name = "Array path"});
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(path.ok());
  for (const auto tool : {"linear_array", "radial_array", "path_array"}) {
    SCOPED_TRACE(tool);
    ASSERT_TRUE(editor_->selection().select_object(
        std::string_view(tool) == "path_array" ? path.value() : box.value()).ok());
    ASSERT_TRUE(editor_->tools().activate(tool).ok());
    refresh();
    auto *checkbox = settings_->findChild<QCheckBox *>(QStringLiteral(
        "tool_settings.control.%1.show_bounding_box_dimensions").arg(QString::fromUtf8(tool)));
    ASSERT_NE(checkbox, nullptr);
    ASSERT_TRUE(checkbox->isVisible());
    EXPECT_TRUE(checkbox->isChecked());
    ASSERT_EQ(editor_->tools().active_array_bounding_box_dimensions(), std::optional<bool>{true});
    const auto history = editor_->history().snapshot();
    std::uint64_t scale_revision = 1u;
    for (float scale : {1.0f, 2.0f, 1.0f}) {
      const quader::QtUiScaleMetrics metrics{scale, ++scale_revision};
      settings_->apply_ui_scale(metrics);
      host_->apply_ui_scale(metrics);
      refresh();
      checkbox->click();
      refresh();
      EXPECT_FALSE(checkbox->isChecked());
      EXPECT_EQ(editor_->tools().active_array_bounding_box_dimensions(), std::optional<bool>{false});
      for (int idle = 0; idle < 8; ++idle) refresh();
      EXPECT_FALSE(checkbox->isChecked());
      checkbox->click();
      refresh();
      EXPECT_TRUE(checkbox->isChecked());
      if (!review_directory.isEmpty()) {
        auto *scroll = settings_->findChild<QScrollArea *>(QStringLiteral("tool_settings.scroll"));
        ASSERT_NE(scroll, nullptr);
        scroll->ensureWidgetVisible(checkbox);
        QCoreApplication::processEvents();
        ASSERT_TRUE(settings_->grab().save(QDir(review_directory).filePath(
            QStringLiteral("%1_dimensions_settings_%2_%3.png")
                .arg(QString::fromUtf8(tool)).arg(scale).arg(scale_revision))));
      }
    }
    EXPECT_EQ(editor_->history().snapshot().undo_label, history.undo_label);
    EXPECT_EQ(editor_->history().snapshot().redo_label, history.redo_label);
    EXPECT_EQ(editor_->tools().active_tool(), tool);
    ASSERT_TRUE(editor_->tools().cancel_active_tool_preview().ok());
  }
}

TEST_F(QtViewportSettingsOverlayTests,
       NativeFocusAndHoverPresentPaneSettingsWithoutSettlingPendingExtrusion) {
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Move).ok());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Side));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Rotate).ok());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Front));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Scale).ok());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Perspective));
  const auto created = editor_->modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Focused pane extrusion");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Extrude).ok());
  ASSERT_TRUE(editor_->selection().select_face(created.value(), 1).ok());
  ASSERT_TRUE(editor_->tools().set_setting("extrude", "extrude.amount", 1.0).ok());
  refresh();
  ASSERT_TRUE(editor_->tools().active_settings().preview_active);
  ASSERT_EQ(settings_->target_id(), "extrude");
  const auto origin = editor_->tools().active_settings_presentation_origin();
  const auto before = editor_->query().compiled_object_mesh(created.value());
  ASSERT_TRUE(before.has_value());
  const auto history_before = editor_->history().snapshot();
  const auto settings_revision = editor_->tools().active_settings_revision();
  for (const auto pane : {quader::ViewportPaneId::Top,
                          quader::ViewportPaneId::Side,
                          quader::ViewportPaneId::Front,
                          quader::ViewportPaneId::Perspective}) {
    SCOPED_TRACE(quader::viewport_pane_stable_id(pane));
    auto *surface = host_->surface(pane);
    ASSERT_NE(surface, nullptr);
    ASSERT_NO_FATAL_FAILURE(focus_surface(pane));
    const QPointF position(surface->width() * 0.6, surface->height() * 0.4);
    QMouseEvent hover(QEvent::MouseMove, position, position,
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &hover);
    process_input();
    refresh();
    EXPECT_EQ(session_->viewport_workspace().focused_pane(), pane);
    EXPECT_EQ(editor_->tools().active_tool(), "extrude");
    EXPECT_TRUE(editor_->tools().active_settings().preview_active);
    EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
    EXPECT_EQ(editor_->tools().active_settings_revision(), settings_revision);
    const auto history = editor_->history().snapshot();
    EXPECT_EQ(history.can_undo, history_before.can_undo);
    EXPECT_EQ(history.can_redo, history_before.can_redo);
    EXPECT_EQ(history.undo_label, history_before.undo_label);
    EXPECT_EQ(history.redo_label, history_before.redo_label);
    const auto after = editor_->query().compiled_object_mesh(created.value());
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->indices, before->indices);
    ASSERT_EQ(after->vertices.size(), before->vertices.size());
    for (std::size_t index = 0; index < before->vertices.size(); ++index) {
      const auto &actual = after->vertices[index].position;
      const auto &expected = before->vertices[index].position;
      EXPECT_EQ(actual.x, expected.x);
      EXPECT_EQ(actual.y, expected.y);
      EXPECT_EQ(actual.z, expected.z);
    }
    if (pane == quader::ViewportPaneId::Top) {
      EXPECT_EQ(settings_->target_id(), "move");
      EXPECT_FALSE(settings_->isHidden());
      EXPECT_EQ(settings_->parentWidget(), frame(pane));
      ASSERT_NE(settings_->findChild<QCheckBox *>(
          QStringLiteral("tool_settings.control.auto_merge_vertices")), nullptr);
    } else if (pane == quader::ViewportPaneId::Perspective) {
      EXPECT_EQ(settings_->target_id(), "extrude");
      EXPECT_FALSE(settings_->isHidden());
      EXPECT_EQ(settings_->parentWidget(), frame(pane));
      auto *apply = settings_->findChild<QPushButton *>(
          QStringLiteral("tool_settings.apply"));
      ASSERT_NE(apply, nullptr);
      EXPECT_TRUE(apply->isEnabled());
      EXPECT_TRUE(apply->isVisible());
    } else {
      EXPECT_FALSE(settings_->has_settings());
      EXPECT_TRUE(settings_->isHidden());
    }
  }
}

TEST_F(QtViewportSettingsOverlayTests,
       PrimitiveVariantStartsANewLocalizedSettingsOperationWithoutATypePicker) {
  ASSERT_TRUE(localization_.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "it-IT"));
  settings_->set_localization(localization_);
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(quader::QuaderPrimitiveCreationShape::Box).ok());
  refresh();
  auto *header = settings_->findChild<QToolButton *>(QStringLiteral("tool_settings.target_title"));
  ASSERT_NE(header, nullptr);
  EXPECT_EQ(header->text(), QStringLiteral("Crea box"));
  EXPECT_EQ(settings_->findChild<QComboBox *>(QStringLiteral("tool_settings.control.primitive.type")), nullptr);
  settings_->set_expanded(false);
  const auto first_origin = editor_->tools().active_settings_presentation_origin();
  const auto repeated = editor_->tools().begin_primitive_creation(quader::QuaderPrimitiveCreationShape::Box);
  ASSERT_TRUE(repeated.ok());
  EXPECT_FALSE(repeated.changed());
  refresh();
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), first_origin);
  EXPECT_FALSE(settings_->expanded());
  EXPECT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Top));

  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Side));
  refresh();
  EXPECT_TRUE(settings_->isHidden());
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), first_origin);
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(quader::QuaderPrimitiveCreationShape::Sphere).ok());
  refresh();
  EXPECT_EQ(header->text(), QStringLiteral("Crea sfera"));
  EXPECT_TRUE(settings_->expanded());
  EXPECT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Side));
  EXPECT_GT(editor_->tools().active_settings_presentation_origin().serial, first_origin.serial);
  EXPECT_EQ(settings_->findChild<QComboBox *>(QStringLiteral("tool_settings.control.primitive.type")), nullptr);
  auto *sphere_type = settings_->findChild<QComboBox *>(QStringLiteral("tool_settings.control.primitive.sphere.type"));
  ASSERT_NE(sphere_type, nullptr);
  EXPECT_EQ(sphere_type->count(), 2);
  const auto sphere_origin = editor_->tools().active_settings_presentation_origin();
  ASSERT_TRUE(editor_->tools().set_setting(quader::kCreatePrimitiveToolId,
                                         quader::kSphereTypeSetting, "quad").ok());
  refresh();
  EXPECT_NE(settings_->findChild<QWidget *>(QStringLiteral("tool_settings.control.primitive.sphere.subdivision")), nullptr);
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), sphere_origin);
  EXPECT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Side));
}

TEST_F(QtViewportSettingsOverlayTests,
       PrimitiveSwitchRejectsHeldFootprintThenAcceptsItsCompletedShape) {
  auto *surface = host_->surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  surface->resize(800, 600);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(surface, &focus);
  ASSERT_TRUE(session_->viewport_workspace().camera(quader::ViewportPaneId::Perspective)
      .restore_perspective_pose({.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(quader::QuaderPrimitiveCreationShape::Box).ok());
  process_input();
  QMouseEvent press(QEvent::MouseButtonPress, {300, 300}, {300, 300},
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &press);
  process_input();
  QMouseEvent drag(QEvent::MouseMove, {500, 430}, {500, 430},
                  Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &drag);
  process_input();
  ASSERT_EQ(session_->modeling().box_drag_state(), quader::ModelingBoxDragState::Footprint);
  const auto authored = session_->modeling().box_preview();
  const auto origin = editor_->tools().active_settings_presentation_origin();
  const auto rejected = editor_->tools().begin_primitive_creation(
      quader::QuaderPrimitiveCreationShape::Sphere);
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  EXPECT_EQ(session_->primitive_creation().active_kind(), quader::PrimitiveCreationKind::Box);
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
  EXPECT_EQ(session_->modeling().box_drag_state(), quader::ModelingBoxDragState::Footprint);
  const auto retained = session_->modeling().box_preview();
  ASSERT_EQ(retained.footprint_count, authored.footprint_count);
  for (std::size_t index = 0; index < authored.footprint_count; ++index) {
    EXPECT_FLOAT_EQ(retained.footprint[index].x, authored.footprint[index].x);
    EXPECT_FLOAT_EQ(retained.footprint[index].y, authored.footprint[index].y);
    EXPECT_FLOAT_EQ(retained.footprint[index].z, authored.footprint[index].z);
  }
  // A rejected switch preserves the original press and its matching release.
  process_input();
  QMouseEvent release(QEvent::MouseButtonRelease, {500, 430}, {500, 430},
                     Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &release);
  process_input();
  ASSERT_EQ(session_->modeling().objects().size(), 1U);
  EXPECT_EQ(session_->primitive_creation().shape_count(), 1U);
  EXPECT_EQ(session_->modeling().box_drag_state(), quader::ModelingBoxDragState::Idle);
  ASSERT_TRUE(editor_->tools().live_primitive_creation().has_value());
  EXPECT_EQ(editor_->tools().live_primitive_creation()->shape, quader::QuaderPrimitiveCreationShape::Box);
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(quader::QuaderPrimitiveCreationShape::Sphere).ok());
  process_input();
  EXPECT_EQ(session_->history().undo_count(), 1U);
  EXPECT_EQ(session_->primitive_creation().shape_count(), 0U);
  EXPECT_FALSE(editor_->tools().live_primitive_creation().has_value());
  QApplication::sendEvent(surface, &press);
  process_input();
  QApplication::sendEvent(surface, &release);
  process_input();
  ASSERT_EQ(session_->primitive_creation().shape_count(), 1u);
  EXPECT_EQ(session_->modeling().objects().size(), 2U);
  ASSERT_TRUE(editor_->tools().live_primitive_creation().has_value());
  EXPECT_EQ(editor_->tools().live_primitive_creation()->shape, quader::QuaderPrimitiveCreationShape::Sphere);
}

TEST_F(QtViewportSettingsOverlayTests,
       GeometryTracksPaneScaleAndLayoutWhileUnchangedFramesDoNoPresentationWork) {
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_anchored(6));
  EXPECT_EQ(settings_->width(), 320);
  settings_->set_expanded(false);
  refresh();
  EXPECT_EQ(settings_->width(), 160);
  settings_->set_expanded(true);
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_anchored(6));
  EXPECT_EQ(settings_->width(), 320);

  // Four panes at this height cannot fit even the compact Box settings body.
  host_->resize(700, 360);
  const quader::QtUiScaleMetrics scaled(1.5F, 1);
  settings_->apply_ui_scale(scaled);
  host_->apply_ui_scale(scaled);
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_anchored(9));
  auto *scroll = settings_->findChild<QScrollArea *>(QStringLiteral("tool_settings.scroll"));
  ASSERT_NE(scroll, nullptr);
  EXPECT_GT(scroll->verticalScrollBar()->maximum(), 0);
  auto *subdivisions = settings_->findChild<QSpinBox *>(
      QStringLiteral("tool_settings.control.primitive.box.subdivisions_z"));
  ASSERT_NE(subdivisions, nullptr);
  scroll->ensureWidgetVisible(subdivisions);
  QCoreApplication::processEvents();
  const QRect field_bounds(subdivisions->mapTo(scroll->viewport(), QPoint()),
                          subdivisions->size());
  EXPECT_TRUE(scroll->viewport()->rect().intersects(field_bounds));
  for (const auto *name : {"tool_settings.cancel"}) {
    auto *action = settings_->findChild<QPushButton *>(QString::fromUtf8(name));
    ASSERT_NE(action, nullptr);
    ASSERT_TRUE(action->isVisible());
    EXPECT_TRUE(settings_->rect().contains(
        QRect(action->mapTo(settings_, QPoint()), action->size())));
  }

  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Perspective));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session_->viewport_workspace().set_layout(quader::ViewportLayoutId::Single));
  refresh();
  EXPECT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Perspective));
  ASSERT_NO_FATAL_FAILURE(expect_anchored(9));
  ASSERT_TRUE(session_->viewport_workspace().set_layout(quader::ViewportLayoutId::FourQuad));
  refresh();
  EXPECT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Perspective));
  ASSERT_NO_FATAL_FAILURE(expect_anchored(9));

  OverlayPresentationEvents events;
  settings_->installEventFilter(&events);
  const auto updates_before = host_->settings_overlay_update_count();
  const QRect geometry_before = settings_->geometry();
  for (int frame_index = 0; frame_index < 12; ++frame_index) {
    refresh();
  }
  EXPECT_EQ(settings_->geometry(), geometry_before);
  EXPECT_EQ(host_->settings_overlay_update_count(), updates_before);
  EXPECT_EQ(events.mutations, 0);
  settings_->removeEventFilter(&events);
}

TEST_F(QtViewportSettingsOverlayTests,
       AcceptedPrimitiveFootprintSurvivesFocusOnAPaneWithoutSettings) {
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(
      quader::QuaderPrimitiveCreationShape::Sphere).ok());
  refresh();
  ASSERT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Top));
  auto *surface = host_->surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  surface->resize(800, 600);
  ASSERT_NO_FATAL_FAILURE(focus_surface(quader::ViewportPaneId::Perspective));
  refresh();
  EXPECT_TRUE(settings_->isHidden());
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(
      quader::QuaderPrimitiveCreationShape::Sphere).ok());
  ASSERT_TRUE(session_->viewport_workspace().camera(quader::ViewportPaneId::Perspective)
      .restore_perspective_pose({.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
  process_input();
  QMouseEvent press(QEvent::MouseButtonPress, {300, 300}, {300, 300},
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &press);
  process_input();
  QMouseEvent release(QEvent::MouseButtonRelease, {500, 430}, {500, 430},
                     Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &release);
  process_input();
  ASSERT_TRUE(editor_->tools().live_primitive_creation().has_value());
  EXPECT_EQ(session_->primitive_creation().shape_count(), 1);
  settings_->set_expanded(true);
  refresh();
  EXPECT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Perspective));
  for (const auto *name : {"tool_settings.apply", "tool_settings.cancel"}) {
    auto *action = settings_->findChild<QPushButton *>(QString::fromUtf8(name));
    ASSERT_NE(action, nullptr);
    ASSERT_TRUE(action->isVisible());
    EXPECT_TRUE(settings_->rect().contains(
        QRect(action->mapTo(settings_, QPoint()), action->size())));
  }
  const auto origin = editor_->tools().active_settings_presentation_origin();
  const auto content_revision = session_->modeling().content_revision();
  const auto settings_revision = editor_->tools().active_settings_revision();
  const bool can_undo = editor_->history().can_undo();
  ASSERT_NO_FATAL_FAILURE(focus_surface(quader::ViewportPaneId::Side));
  refresh();
  process_input();
  refresh();
  EXPECT_EQ(session_->viewport_workspace().focused_pane(), quader::ViewportPaneId::Side);
  EXPECT_TRUE(settings_->isHidden());
  EXPECT_FALSE(settings_->has_settings());
  EXPECT_TRUE(editor_->tools().live_primitive_creation().has_value());
  EXPECT_EQ(session_->primitive_creation().shape_count(), 1);
  EXPECT_EQ(session_->modeling().content_revision(), content_revision);
  EXPECT_EQ(editor_->tools().active_settings_revision(), settings_revision);
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
  EXPECT_EQ(editor_->history().can_undo(), can_undo);
  ASSERT_NO_FATAL_FAILURE(focus_surface(quader::ViewportPaneId::Perspective));
  refresh();
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Perspective));
}

TEST_F(QtViewportSettingsOverlayTests,
       ExpandedPrimitiveKeepsUsableRowsDuringFootprintDrag) {
  ASSERT_TRUE(session_->viewport_workspace().set_layout(
      quader::ViewportLayoutId::Single));
  host_->resize(1200, 1000);
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(
      quader::QuaderPrimitiveCreationShape::Box).ok());
  settings_->set_expanded(true);
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_settings_fit_without_scrolling());

  auto *surface = host_->surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  surface->resize(800, 600);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(surface, &focus);
  ASSERT_TRUE(session_->viewport_workspace().camera(quader::ViewportPaneId::Perspective)
      .restore_perspective_pose({.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
  process_input();
  const auto origin_before = editor_->tools().active_settings_presentation_origin().serial;
  QMouseEvent press(QEvent::MouseButtonPress, {300, 300}, {300, 300},
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &press);
  process_input();
  ASSERT_GT(editor_->tools().active_settings_presentation_origin().serial, origin_before);
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_settings_fit_without_scrolling());

  QMouseEvent drag(QEvent::MouseMove, {500, 430}, {500, 430},
                  Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &drag);
  process_input();
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_settings_fit_without_scrolling());
}

TEST_F(QtViewportSettingsOverlayTests,
       ExpandedSettingsResizeWhenSwitchingToBevel) {
  ASSERT_TRUE(session_->viewport_workspace().set_layout(
      quader::ViewportLayoutId::Single));
  host_->resize(1200, 1000);
  const auto source = editor_->modeling().create_box(
      quader::QuaderBounds{.min = {-1, -1, -1}, .max = {1, 1, 1}},
      "Bevel settings layout");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor_->selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor_->selection().select_edge(source.value(), 1, 2).ok());
  ASSERT_TRUE(editor_->selection().select_all().ok());
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  settings_->set_expanded(true);
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_settings_fit_without_scrolling());

  // Operation changes can happen between two retained UI refreshes.
  ASSERT_TRUE(editor_->tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(editor_->commands().execute(
      editor_->commands().command_id_for_modeling_action("bevel_edges")).ok());
  refresh();
  ASSERT_EQ(settings_->target_id(), "edge_bevel");
  ASSERT_NO_FATAL_FAILURE(expect_settings_fit_without_scrolling());
}

TEST_F(QtViewportSettingsOverlayTests,
       BevelSettingsChangeGeometryWhileViewportFramesContinue) {
  ASSERT_TRUE(session_->viewport_workspace().set_layout(
      quader::ViewportLayoutId::Single));
  const auto source = editor_->modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Live bevel settings");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor_->selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor_->selection().select_edge(source.value(), 1, 2).ok());
  ASSERT_TRUE(editor_->selection().select_all().ok());
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Move).ok());
  ASSERT_TRUE(editor_->commands().execute(
      editor_->commands().command_id_for_modeling_action("bevel_edges")).ok());
  ASSERT_TRUE(editor_->tools().set_setting(
      "edge_bevel", "operation.edge_bevel.width", 0.1).ok());
  settings_->set_expanded(true);
  refresh();
  process_input();
  refresh();
  const auto initial = editor_->query().compiled_object_mesh(source.value());
  ASSERT_TRUE(initial.has_value());
  auto *width = settings_->findChild<QDoubleSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.width"));
  auto *segments = settings_->findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.segments"));
  ASSERT_NE(width, nullptr);
  ASSERT_NE(segments, nullptr);

  const auto settle_with_viewport_frames = [&] {
    QEventLoop loop;
    QTimer frames;
    QObject::connect(&frames, &QTimer::timeout, &loop, [&] {
      process_input();
      refresh();
    });
    frames.start(5);
    QTimer::singleShot(120, &loop, &QEventLoop::quit);
    loop.exec();
  };
  width->setValue(0.2);
  settle_with_viewport_frames();
  EXPECT_DOUBLE_EQ(session_->modeling().interaction_settings().edge_bevel_width,
                   0.2);
  const auto widened = editor_->query().compiled_object_mesh(source.value());
  ASSERT_TRUE(widened.has_value());
  ASSERT_EQ(widened->vertices.size(), initial->vertices.size());
  bool position_changed = false;
  for (std::size_t index = 0; index < widened->vertices.size(); ++index) {
    const auto &before = initial->vertices[index].position;
    const auto &after = widened->vertices[index].position;
    position_changed = position_changed || before.x != after.x ||
                       before.y != after.y || before.z != after.z;
  }
  EXPECT_TRUE(position_changed);

  segments->setValue(3);
  settle_with_viewport_frames();
  EXPECT_EQ(session_->modeling().interaction_settings().edge_bevel_segments, 3);
  const auto subdivided = editor_->query().compiled_object_mesh(source.value());
  ASSERT_TRUE(subdivided.has_value());
  EXPECT_GT(subdivided->indices.size(), widened->indices.size());
  EXPECT_TRUE(editor_->tools().active_settings().preview_active);
}

TEST_F(QtViewportSettingsOverlayTests,
       ApplyIncludesTypedNumericDraftsForBevelAndThicken) {
  struct Operation {
    std::string_view action;
    std::string_view tool;
    std::string_view setting;
    quader::QuaderSelectionMode mode;
  };
  constexpr std::array kOperations{
      Operation{"bevel_edges", "edge_bevel", "operation.edge_bevel.width",
                quader::QuaderSelectionMode::Edge},
      Operation{"thicken_faces", "thicken", "operation.thicken.thickness",
                quader::QuaderSelectionMode::Face}};
  ASSERT_TRUE(session_->viewport_workspace().set_layout(
      quader::ViewportLayoutId::Single));
  host_->activateWindow();
  for (const auto &operation : kOperations) {
    SCOPED_TRACE(operation.action);
    const auto source = [&] {
      if (operation.mode == quader::QuaderSelectionMode::Edge) {
        return editor_->modeling().create_box(
            {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Typed bevel settings");
      }
      // Thicken requires an open boundary; a face of a closed box is ineligible.
      quader_poly::Document document;
      const std::array vertices{
          quader_poly::add_vertex(document, {-1.0F, -1.0F, 0.0F}),
          quader_poly::add_vertex(document, {1.0F, -1.0F, 0.0F}),
          quader_poly::add_vertex(document, {1.0F, 1.0F, 0.0F}),
          quader_poly::add_vertex(document, {-1.0F, 1.0F, 0.0F})};
      EXPECT_NE(quader_poly::add_face(document, vertices),
                quader_poly::kInvalidElementId);
      return editor_->modeling().create_mesh(
          quader::QuaderPolygonDocumentInternalAccess::from_backend(
              std::move(document)), {.name = "Typed thicken settings"});
    }();
    ASSERT_TRUE(source.ok()) << source.error_message();
    ASSERT_TRUE(editor_->selection().select_object(source.value()).ok());
    ASSERT_TRUE(editor_->selection().set_mode(operation.mode).ok());
    ASSERT_TRUE((operation.mode == quader::QuaderSelectionMode::Edge
        ? editor_->selection().select_edge(source.value(), 1, 2)
        : editor_->selection().select_face(source.value(), 1)).ok());
    const auto started = editor_->commands().execute(
        editor_->commands().command_id_for_modeling_action(operation.action));
    ASSERT_TRUE(started.ok()) << started.error_message();
    ASSERT_TRUE(editor_->tools().set_setting(
        operation.tool, operation.setting, 0.1).ok());
    settings_->set_expanded(true);
    refresh();
    auto *spin = settings_->findChild<QDoubleSpinBox *>(QStringLiteral(
        "tool_settings.control.%1").arg(QString::fromUtf8(
            operation.setting.data(), static_cast<qsizetype>(operation.setting.size()))));
    auto *apply = settings_->findChild<QPushButton *>(
        QStringLiteral("tool_settings.apply"));
    ASSERT_NE(spin, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_TRUE(spin->isEnabled());
    ASSERT_TRUE(apply->isEnabled());
    spin->setLocale(QLocale::c());
    spin->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    ASSERT_TRUE(spin->hasFocus());
    auto *line = spin->findChild<QLineEdit *>();
    ASSERT_NE(line, nullptr);
    line->selectAll();
    for (const QChar character : QStringLiteral("0.2")) {
      QKeyEvent text(QEvent::KeyPress, character.unicode(), Qt::NoModifier,
                     QString(character));
      QCoreApplication::sendEvent(line, &text);
    }
    ASSERT_EQ(line->text(), QStringLiteral("0.2"));
    ASSERT_DOUBLE_EQ(spin->value(), 0.1);
    const auto before = editor_->query().compiled_object_mesh(source.value());
    ASSERT_TRUE(before.has_value());

    // Apply deliberately does not take mouse focus. It must interpret the
    // visible draft before flushing the delayed owner write and committing.
    const QPoint position = apply->rect().center();
    const QPoint global = apply->mapToGlobal(position);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(position), QPointF(global),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(apply, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(position), QPointF(global),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(apply, &release);
    const auto owner = session_->modeling().interaction_settings();
    EXPECT_DOUBLE_EQ(operation.mode == quader::QuaderSelectionMode::Edge
                         ? owner.edge_bevel_width : owner.thicken_thickness,
                     0.2);
    EXPECT_FALSE(session_->modeling().operation_preview_active());
    const auto after = editor_->query().compiled_object_mesh(source.value());
    ASSERT_TRUE(after.has_value());
    ASSERT_EQ(after->vertices.size(), before->vertices.size());
    bool position_changed = false;
    for (std::size_t index = 0; index < after->vertices.size(); ++index) {
      const auto &a = before->vertices[index].position;
      const auto &b = after->vertices[index].position;
      position_changed = position_changed || a.x != b.x || a.y != b.y || a.z != b.z;
    }
    EXPECT_TRUE(position_changed);
  }
}

TEST_F(QtViewportSettingsOverlayTests,
       RuntimeReplacementDropsTheOldPlacementAndPreservesExpansion) {
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  settings_->set_expanded(true);
  refresh();
  ASSERT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Top));

  settings_->unbind();
  host_->abandon_interactions();
  EXPECT_TRUE(settings_->isHidden());
  EXPECT_TRUE(settings_->expanded());
  editor_.reset();
  session_.reset();
  QCoreApplication::processEvents();

  session_ = std::make_unique<quader::SceneSession>();
  editor_ = std::make_unique<quader::QuaderEditorApi>(
      quader::QuaderApiSessionInternalAccess::from_scene_session(*session_));
  ASSERT_TRUE(session_->viewport_workspace().set_layout(quader::ViewportLayoutId::FourQuad));
  host_->attach_workspace(session_->viewport_workspace());
  settings_->bind(*editor_);
  refresh();
  EXPECT_TRUE(settings_->isHidden());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Side));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Front));
  refresh();
  EXPECT_TRUE(settings_->isHidden());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Side));
  refresh();
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_TRUE(settings_->expanded());
  EXPECT_EQ(settings_->parentWidget(), frame(quader::ViewportPaneId::Side));
}

} // namespace
