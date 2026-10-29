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
#include "assets/asset_registry.hpp"
#include "core/localization_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/scene_session.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "render/render_system.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_dock_title_presentation.hpp"
#include "ui/qt_panel_section_header.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "support/qt_viewport_input_test_helpers.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QMainWindow>
#include <QMoveEvent>
#include <QWindow>
#include <QPushButton>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOptionDockWidget>
#include <QStyleOptionComboBox>
#include <QSpinBox>
#include <QToolButton>
#include <QTimer>

#include <memory>
#include <algorithm>
#include <limits>
#include <optional>
#include <array>
#include <filesystem>
#include <string_view>
#include <utility>

namespace {

using quader::test_support::resize_viewport_surface;
using quader::test_support::send_visible_viewport_event;

/// Hosts actual docked settings and native pane widgets without a renderer.
class QtToolSettingsDockTests : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    quader::apply_qt_obsidian_application_theme(QApplication::palette());
    scale_controller_ =
        std::make_unique<quader::QtUiScaleApplicationController>(*application_);
    ASSERT_EQ(QApplication::style(), &scale_controller_->style());
    ASSERT_TRUE(localization_.load(
        std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "en-US"));
    session_ = std::make_unique<quader::SceneSession>();
    editor_ = std::make_unique<quader::QuaderEditorApi>(
        quader::QuaderApiSessionInternalAccess::from_scene_session(*session_));
    ASSERT_TRUE(editor_->commands().set_keybinding_storage_path({}).ok());
    ASSERT_TRUE(session_->viewport_workspace().set_layout(
        quader::ViewportLayoutId::FourQuad));
    window_ = std::make_unique<QMainWindow>();
    window_->setAttribute(Qt::WA_DontShowOnScreen, true);
    window_->resize(1340, 1100);
    host_ = std::make_unique<quader::QtViewportPanel>();
    window_->setCentralWidget(host_.get());
    host_->attach_workspace(session_->viewport_workspace());
    host_->set_startup_presentation_blocked(false);
    outliner_dock_ = new QDockWidget(QStringLiteral("Outliner"), window_.get());
    outliner_dock_->setObjectName(QStringLiteral("dock_outliner"));
    quader::install_palette_aware_dock_title_presentation(*outliner_dock_);
    outliner_dock_->setWidget(new QWidget(outliner_dock_));
    window_->addDockWidget(Qt::LeftDockWidgetArea, outliner_dock_);
    settings_dock_ = new QDockWidget(QStringLiteral("Tool Settings"), window_.get());
    settings_dock_->setObjectName(QStringLiteral("dock_tool_settings"));
    quader::install_palette_aware_dock_title_presentation(*settings_dock_);
    settings_ = new quader::QtToolSettingsPanel(settings_dock_);
    settings_->set_localization(localization_);
    settings_dock_->setWidget(settings_);
    window_->addDockWidget(Qt::LeftDockWidgetArea, settings_dock_);
    window_->splitDockWidget(outliner_dock_, settings_dock_, Qt::Vertical);
    window_->resizeDocks({settings_dock_}, {340}, Qt::Horizontal);
    window_->resizeDocks({outliner_dock_, settings_dock_}, {200, 850}, Qt::Vertical);
    settings_->presentation_changed = [this] { ++settings_presentation_updates_; };
    settings_->bind(*editor_);
    window_->show();
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
    window_.reset();
    settings_ = nullptr;
    editor_.reset();
    session_.reset();
    scale_controller_.reset();
    application_.reset();
  }

  void refresh() {
    settings_->refresh();
    host_->refresh_workspace_layout();
    QCoreApplication::processEvents();
  }

  void expect_docked() {
    ASSERT_EQ(settings_->parentWidget(), settings_dock_);
    EXPECT_FALSE(settings_->isHidden());
    EXPECT_FALSE(settings_dock_->isFloating());
    EXPECT_EQ(window_->dockWidgetArea(settings_dock_), Qt::LeftDockWidgetArea);
    EXPECT_TRUE(settings_dock_->rect().contains(settings_->geometry()));
    EXPECT_EQ(settings_dock_->x(), outliner_dock_->x());
    EXPECT_GT(settings_dock_->y(), outliner_dock_->geometry().bottom());
    EXPECT_FALSE(host_->isAncestorOf(settings_));
  }

  void resize_dock_content(int width, int height) {
    window_->resize(width + 1000, height + 240);
    window_->resizeDocks({settings_dock_}, {width}, Qt::Horizontal);
    window_->resizeDocks({outliner_dock_, settings_dock_}, {200, height}, Qt::Vertical);
    QCoreApplication::processEvents();
  }

  const quader::QtUiScaleMetrics &apply_scale(float factor) {
    scale_controller_->apply_factor(factor);
    const auto &metrics = scale_controller_->metrics();
    settings_->apply_ui_scale(metrics);
    host_->apply_ui_scale(metrics);
    quader::apply_ui_scale_to_palette_aware_dock_title_presentation(
        *settings_dock_, metrics);
    quader::apply_ui_scale_to_palette_aware_dock_title_presentation(
        *outliner_dock_, metrics);
    return metrics;
  }

  void prepare_empty_component_context() {
    const auto target = editor_->modeling().create_box(
        {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Dock component target");
    ASSERT_TRUE(target.ok()) << target.error_message();
    ASSERT_TRUE(editor_->selection().select_object(target.value()).ok());
    ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
    session_->history().clear();
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
    expect_docked();
  }

  int argc_ = 1;
  char application_name_[40] = "quader-tool-settings-dock-tests";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
  std::unique_ptr<quader::QtUiScaleApplicationController> scale_controller_;
  std::unique_ptr<quader::SceneSession> session_;
  std::unique_ptr<quader::QuaderEditorApi> editor_;
  std::unique_ptr<QMainWindow> window_;
  std::unique_ptr<quader::QtViewportPanel> host_;
  QDockWidget *outliner_dock_ = nullptr;
  QDockWidget *settings_dock_ = nullptr;
  std::uint64_t settings_presentation_updates_ = 0u;
  quader::QtToolSettingsPanel *settings_ = nullptr;
  quader::LocalizationService localization_;
};

/// Counts actual dock-content mutations independently of preferred-size changes.
class DockPresentationEvents final : public QObject {
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

TEST_F(QtToolSettingsDockTests,
       SettingsStayDockedAndPreserveCollapseAcrossFocusChanges) {
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_FALSE(settings_->has_settings());
  EXPECT_TRUE(settings_->expanded());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  refresh();

  ASSERT_TRUE(settings_->has_settings());
  ASSERT_EQ(settings_->parentWidget(), settings_dock_);
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
  EXPECT_TRUE(settings_->has_settings());
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_FALSE(settings_->expanded());
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
  EXPECT_EQ(editor_->tools().active_tool_revision(), tool_revision);
  EXPECT_EQ(editor_->tools().active_settings_revision(), settings_revision);
  EXPECT_TRUE(editor_->tools().has_active_primitive_creation());
  EXPECT_EQ(session_->viewport_workspace().selected_tool(quader::ViewportPaneId::Top),
            quader::ViewportToolId::CreatePrimitive);
  EXPECT_EQ(session_->viewport_workspace().selected_tool(quader::ViewportPaneId::Front),
            quader::ViewportToolId::CreatePrimitive);
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  refresh();
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_FALSE(settings_->expanded());

  // A complete cancel/start between refreshes still represents a new operation.
  ASSERT_TRUE(editor_->tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Side));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  refresh();
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  EXPECT_TRUE(settings_->expanded());

  settings_->set_expanded(false);
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Select).ok());
  refresh();
  EXPECT_FALSE(settings_->has_settings());
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_FALSE(header->isVisible());
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  refresh();
  EXPECT_TRUE(settings_->expanded());
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
}

TEST_F(QtToolSettingsDockTests,
       FocusKeepsTheActiveSettingsTargetWithoutSettlingTheSharedOperation) {
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context());
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
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
  EXPECT_FALSE(settings_->isHidden());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Front));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Scale).ok());
  refresh();
  EXPECT_FALSE(settings_->has_settings());
  EXPECT_FALSE(settings_->isHidden());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Perspective));
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Object).ok());
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
    EXPECT_EQ(session_->modeling().objects().size(), 1u);
    EXPECT_FALSE(editor_->history().can_undo());
    EXPECT_EQ(session_->viewport_workspace().selected_tool(pane),
              quader::ViewportToolId::CreatePrimitive);
    EXPECT_EQ(settings_->target_id(), quader::kCreatePrimitiveToolId);
    EXPECT_TRUE(settings_->has_settings());
    EXPECT_FALSE(settings_->isHidden());
    EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  }
}

TEST_F(QtToolSettingsDockTests,
       AutoMergeContentHidesOutsideVertexModeWithoutHidingTheDockOrIdleWork) {
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Move).ok());
  refresh();
  EXPECT_FALSE(settings_->has_settings());
  EXPECT_FALSE(settings_->isHidden());
  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context());
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor_->tools().set_setting("move", "auto_merge_vertices", false).ok());
  const auto origin = editor_->tools().active_settings_presentation_origin();

  const auto expect_unchanged_refresh = [&] {
    refresh();
    DockPresentationEvents events;
    settings_->installEventFilter(&events);
    const auto updates = settings_presentation_updates_;
    const auto revision = editor_->tools().active_settings_revision();
    const auto geometry = settings_->geometry();
    for (int frame_index = 0; frame_index < 12; ++frame_index) refresh();
    EXPECT_EQ(settings_presentation_updates_, updates);
    EXPECT_EQ(editor_->tools().active_settings_revision(), revision);
    EXPECT_EQ(settings_->geometry(), geometry);
    EXPECT_EQ(events.mutations, 0);
    settings_->removeEventFilter(&events);
  };

  for (const auto mode : {quader::QuaderSelectionMode::Edge,
                          quader::QuaderSelectionMode::Face,
                          quader::QuaderSelectionMode::Object,
                          quader::QuaderSelectionMode::Group}) {
    SCOPED_TRACE(static_cast<int>(mode));
    refresh();
    ASSERT_TRUE(settings_->has_settings());
    EXPECT_FALSE(settings_->isHidden());
    EXPECT_EQ(settings_->parentWidget(), settings_dock_);
    auto *toggle = settings_->findChild<QCheckBox *>(
        QStringLiteral("tool_settings.control.auto_merge_vertices"));
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->isChecked());
    expect_unchanged_refresh();

    const bool object_mode = mode == quader::QuaderSelectionMode::Object ||
                             mode == quader::QuaderSelectionMode::Group;
    ASSERT_TRUE(editor_->selection().set_mode(object_mode ? quader::QuaderSelectionMode::Object
                                               : quader::QuaderSelectionMode::Vertex).ok());
    ASSERT_TRUE(editor_->selection().set_mode(mode).ok());
    refresh();
    EXPECT_FALSE(settings_->has_settings());
    EXPECT_FALSE(settings_->isHidden());
    EXPECT_EQ(settings_->findChild<QCheckBox *>(
        QStringLiteral("tool_settings.control.auto_merge_vertices")), nullptr);
    expect_unchanged_refresh();
    EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
    ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  }
  refresh();
  EXPECT_TRUE(settings_->has_settings());
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  EXPECT_FALSE(editor_->history().can_undo());
}

TEST_F(QtToolSettingsDockTests,
       NativeDockMatchesFlatInspectorHierarchyAcrossScaleAndNarrowWidths) {
  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context());
  const QPalette native_palette = QApplication::style()->standardPalette();
  const auto &scale_controller = *scale_controller_;
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(review_directory));
  struct LayoutCase final {
    float scale;
    int width;
    int height;
  };
  constexpr std::array kLayouts{
      LayoutCase{1.0F, 340, 700}, LayoutCase{1.0F, 240, 700},
      LayoutCase{2.0F, 640, 1400}, LayoutCase{2.0F, 480, 1400},
      LayoutCase{1.0F, 320, 700}, LayoutCase{1.0F, 340, 700}};
  int capture_cycle = 0;
  const auto apply_layout = [&](const LayoutCase &layout) {
    apply_scale(layout.scale);
    resize_dock_content(layout.width, layout.height);
    refresh();
  };
  const auto set_palette = [&](const QPalette &palette) {
    window_->setPalette(palette);
    outliner_dock_->setPalette(palette);
    settings_dock_->setPalette(palette);
    settings_->setPalette(palette);
    for (QWidget *widget : settings_->findChildren<QWidget *>()) {
      widget->setPalette(palette);
    }
    QCoreApplication::processEvents();
  };
  for (const auto tool : {quader::ViewportToolId::Move,
                          quader::ViewportToolId::CreatePrimitive}) {
    SCOPED_TRACE(static_cast<int>(tool));
    ASSERT_TRUE(editor_->tools().activate(tool).ok());
    refresh();
    ASSERT_TRUE(settings_->has_settings());
    const auto origin = editor_->tools().active_settings_presentation_origin();
    const auto settings_revision = editor_->tools().active_settings_revision();
    const auto history = editor_->history().snapshot();
    auto *header = dynamic_cast<quader::QtPanelSectionHeader *>(
        settings_->findChild<QToolButton *>(QStringLiteral("tool_settings.target_title")));
    ASSERT_NE(header, nullptr);
    auto *scroll = settings_->findChild<QScrollArea *>(QStringLiteral("tool_settings.scroll"));
    ASSERT_NE(scroll, nullptr);
    const auto verify_readable_content = [&](const char *state) {
      SCOPED_TRACE(state);
      // Font/style changes can queue a label refresh followed by another
      // QScrollArea layout request. Settle the same native chain as Inspector.
      for (int pass = 0; pass < 3; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents();
        window_->layout()->activate();
        settings_->layout()->activate();
      }
      ASSERT_NO_FATAL_FAILURE(expect_docked());
      EXPECT_EQ(header->presentation(), quader::QtPanelSectionHeader::Presentation::Flat);
      EXPECT_GE(header->width(), header->fontMetrics().horizontalAdvance(header->text()) +
                                    scale_controller.metrics().px(42));
      EXPECT_EQ(settings_dock_->windowTitle(), QStringLiteral("Tool Settings"));
      QStyleOptionDockWidget dock_option;
      dock_option.initFrom(settings_dock_);
      dock_option.title = settings_dock_->windowTitle();
      dock_option.closable = true;
      dock_option.floatable = true;
      dock_option.movable = true;
      const QRect title_rect = settings_dock_->style()->subElementRect(
          QStyle::SE_DockWidgetTitleBarText, &dock_option, settings_dock_);
      EXPECT_GE(title_rect.width(), settings_dock_->fontMetrics().horizontalAdvance(
                                        settings_dock_->windowTitle()));
      if (settings_->expanded()) {
        EXPECT_TRUE(scroll->isVisible());
        EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
        EXPECT_LE(scroll->widget()->width(), scroll->viewport()->width());
        for (QAbstractSpinBox *spin : settings_->findChildren<QAbstractSpinBox *>()) {
          if (!spin->isVisible()) continue;
          auto *line = spin->findChild<QLineEdit *>();
          ASSERT_NE(line, nullptr);
          EXPECT_GE(line->contentsRect().width(),
                    line->fontMetrics().horizontalAdvance(line->text()));
          // Native single-line editors may omit a pixel of font line leading.
          // Reserve the actual numeric glyph ink rather than paragraph leading.
          const QRect glyph_bounds =
              line->fontMetrics().tightBoundingRect(line->displayText());
          EXPECT_GE(line->contentsRect().height(), glyph_bounds.height());
          EXPECT_TRUE(scroll->widget()->rect().contains(
              QRect(spin->mapTo(scroll->widget(), QPoint{}), spin->size())));
        }
        for (QLabel *label : settings_->findChildren<QLabel *>()) {
          if (!label->isVisible() || label->text().isEmpty()) continue;
          EXPECT_GE(label->contentsRect().height(), label->fontMetrics().height());
          if (!label->wordWrap()) {
            EXPECT_GE(label->contentsRect().width(),
                      label->fontMetrics().horizontalAdvance(label->text()));
          }
        }
      } else {
        EXPECT_FALSE(scroll->isVisible());
        EXPECT_TRUE(header->isVisible());
        EXPECT_EQ(header->geometry().top(), settings_->rect().top());
      }
      EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
      EXPECT_EQ(editor_->tools().active_settings_revision(), settings_revision);
      EXPECT_EQ(editor_->history().snapshot().undo_label, history.undo_label);
      EXPECT_EQ(editor_->history().snapshot().redo_label, history.redo_label);
    };
    const auto capture = [&](const QString &state) {
      if (review_directory.isEmpty()) return;
      ASSERT_TRUE(settings_dock_->grab().save(QDir(review_directory).filePath(
          QStringLiteral("tool_settings_dock_%1_%2_scale_%3_width_%4_cycle_%5.png")
              .arg(tool == quader::ViewportToolId::Move ? QStringLiteral("move")
                                                      : QStringLiteral("box"))
              .arg(state).arg(scale_controller.metrics().factor())
              .arg(settings_->width()).arg(capture_cycle++))));
    };
    for (const auto &layout : kLayouts) {
      SCOPED_TRACE(layout.scale);
      SCOPED_TRACE(layout.width);
      apply_layout(layout);
      EXPECT_LE(settings_->width(), layout.width);
      ASSERT_NO_FATAL_FAILURE(verify_readable_content("dark"));
      capture(QStringLiteral("dark"));
    }

    apply_layout(LayoutCase{1.0F, 240, 700});
    window_->activateWindow();
    header->setFocus(Qt::TabFocusReason);
    QCoreApplication::processEvents();
    EXPECT_TRUE(header->hasFocus());
    ASSERT_NO_FATAL_FAILURE(verify_readable_content("header-focus"));
    capture(QStringLiteral("header-focus"));
    const QSize expanded_size = settings_->size();
    settings_->set_expanded(false);
    refresh();
    EXPECT_EQ(settings_->size(), expanded_size);
    ASSERT_NO_FATAL_FAILURE(verify_readable_content("collapsed"));
    capture(QStringLiteral("collapsed"));
    settings_->set_expanded(true);
    refresh();
    QWidget *field = settings_->findChild<QAbstractSpinBox *>();
    if (field == nullptr) field = settings_->findChild<QCheckBox *>();
    ASSERT_NE(field, nullptr);
    field->setFocus(Qt::TabFocusReason);
    QCoreApplication::processEvents();
    EXPECT_TRUE(field->hasFocus());
    ASSERT_NO_FATAL_FAILURE(verify_readable_content("field-focus"));
    capture(QStringLiteral("field-focus"));

    window_->setLayoutDirection(Qt::RightToLeft);
    for (const auto &layout : {LayoutCase{1.0F, 240, 700},
                               LayoutCase{2.0F, 480, 1400}}) {
      apply_layout(layout);
      ASSERT_NO_FATAL_FAILURE(verify_readable_content("rtl"));
      capture(QStringLiteral("rtl"));
    }
    window_->setLayoutDirection(Qt::LeftToRight);
    quader::apply_qt_application_theme_for_contrast_preference(
        native_palette, Qt::ContrastPreference::HighContrast);
    set_palette(QPalette{});
    for (const auto &layout : {LayoutCase{1.0F, 240, 700},
                               LayoutCase{2.0F, 480, 1400}}) {
      apply_layout(layout);
      QStyleOption option;
      option.initFrom(header);
      EXPECT_FALSE(quader::qt_application_style_active(&option));
      ASSERT_NO_FATAL_FAILURE(verify_readable_content("native-contrast"));
      capture(QStringLiteral("native-contrast"));
    }
    quader::apply_qt_application_theme_for_contrast_preference(
        native_palette, Qt::ContrastPreference::NoPreference);
    QPalette light_palette = native_palette;
    light_palette.setColor(QPalette::Window, Qt::white);
    light_palette.setColor(QPalette::Base, Qt::white);
    light_palette.setColor(QPalette::AlternateBase, QColor(242, 242, 242));
    light_palette.setColor(QPalette::Button, Qt::white);
    light_palette.setColor(QPalette::WindowText, Qt::black);
    light_palette.setColor(QPalette::Text, Qt::black);
    light_palette.setColor(QPalette::ButtonText, Qt::black);
    for (const auto &layout : {LayoutCase{1.0F, 240, 700},
                               LayoutCase{2.0F, 480, 1400}}) {
      apply_layout(layout);
      // Dock style updates resolve their application palette during reflow.
      // Reapply the explicit widget override after those style transitions.
      set_palette(light_palette);
      EXPECT_EQ(header->palette().color(QPalette::Window), QColor(Qt::white));
      QStyleOption option;
      option.initFrom(header);
      EXPECT_FALSE(quader::qt_application_style_active(&option));
      ASSERT_NO_FATAL_FAILURE(verify_readable_content("explicit-light"));
      capture(QStringLiteral("explicit-light"));
    }
    set_palette(QPalette{});
    apply_layout(LayoutCase{1.0F, 240, 700});
    ASSERT_NO_FATAL_FAILURE(verify_readable_content("scale-down"));
    capture(QStringLiteral("scale-down"));
  }
}

TEST_F(QtToolSettingsDockTests,
       AllArrayToolsExposeDefaultOnDimensionsCheckboxWithoutHistoryChanges) {
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
    int capture_cycle = 0;
    for (float scale : {1.0f, 2.0f, 1.0f}) {
      const auto &metrics = apply_scale(scale);
      resize_dock_content(metrics.px(340), metrics.px(800));
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
                .arg(QString::fromUtf8(tool)).arg(scale).arg(++capture_cycle))));
      }
    }
    EXPECT_EQ(editor_->history().snapshot().undo_label, history.undo_label);
    EXPECT_EQ(editor_->history().snapshot().redo_label, history.redo_label);
    EXPECT_EQ(editor_->tools().active_tool(), tool);
    ASSERT_TRUE(editor_->tools().cancel_active_tool_preview().ok());
  }
}

TEST_F(QtToolSettingsDockTests,
       NativeFocusAndHoverKeepStickySettingsWithoutSettlingPendingExtrusion) {
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
  ASSERT_TRUE(editor_->selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
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
    const auto visible_size = surface->render_size_pixels();
    const QPointF position(visible_size.x / surface->devicePixelRatio() * 0.6,
                           visible_size.y / surface->devicePixelRatio() * 0.4);
    QMouseEvent hover(QEvent::MouseMove, position, position,
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    send_visible_viewport_event(surface, &hover);
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
    EXPECT_EQ(session_->viewport_workspace().selected_tool(pane),
              quader::ViewportToolId::Extrude);
    EXPECT_EQ(settings_->target_id(), "extrude");
    EXPECT_FALSE(settings_->isHidden());
    EXPECT_EQ(settings_->parentWidget(), settings_dock_);
    auto *apply = settings_->findChild<QPushButton *>(
        QStringLiteral("tool_settings.apply"));
    ASSERT_NE(apply, nullptr);
    EXPECT_TRUE(apply->isEnabled());
    EXPECT_TRUE(apply->isVisible());
  }

  // Editing through another pane reuses the same pending preview and origin.
  ASSERT_NO_FATAL_FAILURE(focus_surface(quader::ViewportPaneId::Top));
  const auto updated = editor_->tools().set_setting(quader::ViewportPaneId::Top,
      "extrude", "extrude.amount", 2.0);
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  refresh();
  EXPECT_TRUE(editor_->tools().active_settings().preview_active);
  EXPECT_EQ(editor_->tools().active_tool(), "extrude");
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  EXPECT_EQ(editor_->history().snapshot().undo_label, history_before.undo_label);
  const auto edited = editor_->query().compiled_object_mesh(created.value());
  ASSERT_TRUE(edited.has_value());
  EXPECT_EQ(edited->indices, before->indices);
  ASSERT_EQ(edited->vertices.size(), before->vertices.size());
  bool position_changed = false;
  for (std::size_t index = 0; index < edited->vertices.size(); ++index) {
    const auto &actual = edited->vertices[index].position;
    const auto &previous = before->vertices[index].position;
    position_changed = position_changed || actual.x != previous.x ||
                       actual.y != previous.y || actual.z != previous.z;
  }
  EXPECT_TRUE(position_changed);
}

TEST_F(QtToolSettingsDockTests,
       FocusPreservesOriginNumericDraftAndCollapseSettlesItWithoutApplyingPreview) {
  const auto created = editor_->modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Sticky extrusion draft");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor_->selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::Extrude).ok());
  ASSERT_TRUE(editor_->selection().select_face(created.value(), 1).ok());
  ASSERT_TRUE(editor_->tools().set_setting("extrude", "extrude.amount", 0.1).ok());
  window_->activateWindow();
  refresh();
  auto *amount = settings_->findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  ASSERT_NE(amount, nullptr);
  ASSERT_TRUE(amount->isEnabled());
  amount->setLocale(QLocale::c());
  amount->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_TRUE(amount->hasFocus());
  auto *line = amount->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  line->selectAll();
  for (const QChar character : QStringLiteral("0.2")) {
    QKeyEvent text(QEvent::KeyPress, character.unicode(), Qt::NoModifier,
                   QString(character));
    QCoreApplication::sendEvent(line, &text);
  }
  ASSERT_EQ(line->text(), QStringLiteral("0.2"));
  ASSERT_DOUBLE_EQ(amount->value(), 0.1);
  const auto origin = editor_->tools().active_settings_presentation_origin();
  const auto settings_revision = editor_->tools().active_settings_revision();
  const auto history_before = editor_->history().snapshot();
  const auto owner_amount = [&]() -> std::optional<double> {
    for (const auto &control : editor_->tools().active_settings().controls) {
      if (control.id == "extrude.amount") {
        return control.number_value;
      }
    }
    return std::nullopt;
  };
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  refresh();
  process_input();
  refresh();
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  EXPECT_EQ(settings_->findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount")), amount);
  EXPECT_EQ(line->text(), QStringLiteral("0.2"));
  EXPECT_DOUBLE_EQ(amount->value(), 0.1);
  EXPECT_EQ(owner_amount(), std::optional<double>{0.1});
  EXPECT_EQ(editor_->tools().active_settings_revision(), settings_revision);
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
  EXPECT_TRUE(editor_->tools().active_settings().preview_active);

  settings_->set_expanded(false);
  refresh();
  EXPECT_FALSE(settings_->expanded());
  EXPECT_DOUBLE_EQ(amount->value(), 0.2);
  EXPECT_EQ(owner_amount(), std::optional<double>{0.2});
  EXPECT_TRUE(editor_->tools().active_settings().preview_active);
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
  EXPECT_EQ(editor_->history().snapshot().undo_label, history_before.undo_label);
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Front));
  refresh();
  EXPECT_FALSE(settings_->expanded());
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  EXPECT_TRUE(editor_->tools().active_settings().preview_active);
}

TEST_F(QtToolSettingsDockTests,
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
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);

  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Side));
  refresh();
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_FALSE(settings_->expanded());
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), first_origin);
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(quader::QuaderPrimitiveCreationShape::Sphere).ok());
  refresh();
  EXPECT_EQ(header->text(), QStringLiteral("Crea sfera"));
  EXPECT_TRUE(settings_->expanded());
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
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
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
}

TEST_F(QtToolSettingsDockTests,
       PrimitiveSwitchRejectsHeldFootprintThenAcceptsItsCompletedShape) {
  auto *surface = host_->surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  resize_viewport_surface(*host_, *surface, {800, 600});
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);
  ASSERT_TRUE(session_->viewport_workspace().camera(quader::ViewportPaneId::Perspective)
      .restore_perspective_pose({.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(quader::QuaderPrimitiveCreationShape::Box).ok());
  process_input();
  QMouseEvent press(QEvent::MouseButtonPress, {300, 300}, {300, 300},
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &press);
  process_input();
  QMouseEvent drag(QEvent::MouseMove, {500, 430}, {500, 430},
                  Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &drag);
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
  send_visible_viewport_event(surface, &release);
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
  send_visible_viewport_event(surface, &press);
  process_input();
  send_visible_viewport_event(surface, &release);
  process_input();
  ASSERT_EQ(session_->primitive_creation().shape_count(), 1u);
  EXPECT_EQ(session_->modeling().objects().size(), 2U);
  ASSERT_TRUE(editor_->tools().live_primitive_creation().has_value());
  EXPECT_EQ(editor_->tools().live_primitive_creation()->shape, quader::QuaderPrimitiveCreationShape::Sphere);
}

TEST_F(QtToolSettingsDockTests,
       DockGeometrySurvivesPaneLayoutsAndScaleWithoutIdlePresentationWork) {
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_docked());
  const QSize expanded_size = settings_->size();
  settings_->set_expanded(false);
  refresh();
  EXPECT_EQ(settings_->size(), expanded_size);
  settings_->set_expanded(true);
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_docked());
  EXPECT_EQ(settings_->size(), expanded_size);

  // Tool settings scroll within a deliberately short dock independently of panes.
  const auto &scaled = apply_scale(1.5F);
  const int constrained_height = scaled.px(130);
  settings_dock_->setFixedHeight(constrained_height);
  resize_dock_content(scaled.px(340), constrained_height);
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_docked());
  ASSERT_EQ(settings_dock_->height(), constrained_height);
  auto *scroll = settings_->findChild<QScrollArea *>(QStringLiteral("tool_settings.scroll"));
  ASSERT_NE(scroll, nullptr);
  EXPECT_GT(scroll->widget()->sizeHint().height(), scroll->viewport()->height());
  EXPECT_GT(scroll->verticalScrollBar()->maximum(), 0);
  auto *subdivisions = settings_->findChild<QSpinBox *>(
      QStringLiteral("tool_settings.control.primitive.box.subdivisions_z"));
  ASSERT_NE(subdivisions, nullptr);
  scroll->ensureWidgetVisible(subdivisions);
  QCoreApplication::processEvents();
  const QRect field_bounds(subdivisions->mapTo(scroll->viewport(), QPoint()),
                          subdivisions->size());
  EXPECT_TRUE(scroll->viewport()->rect().contains(field_bounds));
  auto *cancel = settings_->findChild<QPushButton *>(
      QStringLiteral("tool_settings.cancel"));
  ASSERT_NE(cancel, nullptr);
  ASSERT_TRUE(cancel->isVisible());
  EXPECT_TRUE(cancel->isEnabled());
  EXPECT_GE(cancel->height(), cancel->minimumSizeHint().height());
  EXPECT_TRUE(settings_->rect().contains(
      QRect(cancel->mapTo(settings_, QPoint()), cancel->size())));

  const auto origin = editor_->tools().active_settings_presentation_origin();
  const QRect dock_geometry = settings_dock_->geometry();
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Perspective));
  refresh();
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  ASSERT_TRUE(session_->viewport_workspace().set_layout(quader::ViewportLayoutId::Single));
  refresh();
  EXPECT_EQ(settings_dock_->geometry(), dock_geometry);
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
  ASSERT_NO_FATAL_FAILURE(expect_docked());
  ASSERT_TRUE(session_->viewport_workspace().set_layout(quader::ViewportLayoutId::FourQuad));
  refresh();
  EXPECT_EQ(settings_dock_->geometry(), dock_geometry);
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
  ASSERT_NO_FATAL_FAILURE(expect_docked());

  DockPresentationEvents events;
  settings_->installEventFilter(&events);
  const auto updates_before = settings_presentation_updates_;
  const QRect geometry_before = settings_->geometry();
  for (int frame_index = 0; frame_index < 12; ++frame_index) refresh();
  EXPECT_EQ(settings_->geometry(), geometry_before);
  EXPECT_EQ(settings_presentation_updates_, updates_before);
  EXPECT_EQ(events.mutations, 0);
  settings_->removeEventFilter(&events);
}

TEST_F(QtToolSettingsDockTests,
       PrimitiveFootprintCanBeCreatedElsewhereWhileSettingsKeepTheirOperationOrigin) {
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(
      quader::QuaderPrimitiveCreationShape::Sphere).ok());
  refresh();
  ASSERT_EQ(settings_->parentWidget(), settings_dock_);
  auto *surface = host_->surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  resize_viewport_surface(*host_, *surface, {800, 600});
  ASSERT_NO_FATAL_FAILURE(focus_surface(quader::ViewportPaneId::Perspective));
  refresh();
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  const auto activation_origin = editor_->tools().active_settings_presentation_origin();
  ASSERT_TRUE(session_->viewport_workspace().camera(quader::ViewportPaneId::Perspective)
      .restore_perspective_pose({.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
  process_input();
  QMouseEvent press(QEvent::MouseButtonPress, {300, 300}, {300, 300},
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &press);
  process_input();
  QMouseEvent release(QEvent::MouseButtonRelease, {500, 430}, {500, 430},
                     Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &release);
  process_input();
  ASSERT_TRUE(editor_->tools().live_primitive_creation().has_value());
  EXPECT_EQ(session_->primitive_creation().shape_count(), 1);
  settings_->set_expanded(true);
  refresh();
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), activation_origin);
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
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_TRUE(settings_->has_settings());
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  EXPECT_TRUE(editor_->tools().live_primitive_creation().has_value());
  EXPECT_EQ(session_->primitive_creation().shape_count(), 1);
  EXPECT_EQ(session_->modeling().content_revision(), content_revision);
  EXPECT_EQ(editor_->tools().active_settings_revision(), settings_revision);
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin);
  EXPECT_EQ(editor_->history().can_undo(), can_undo);
  ASSERT_NO_FATAL_FAILURE(focus_surface(quader::ViewportPaneId::Perspective));
  refresh();
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
}

TEST_F(QtToolSettingsDockTests,
       ExpandedPrimitiveKeepsUsableRowsDuringFootprintDrag) {
  ASSERT_TRUE(session_->viewport_workspace().set_layout(
      quader::ViewportLayoutId::Single));
  resize_dock_content(340, 850);
  ASSERT_TRUE(editor_->tools().begin_primitive_creation(
      quader::QuaderPrimitiveCreationShape::Box).ok());
  settings_->set_expanded(true);
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_settings_fit_without_scrolling());

  auto *surface = host_->surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  resize_viewport_surface(*host_, *surface, {800, 600});
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);
  ASSERT_TRUE(session_->viewport_workspace().camera(quader::ViewportPaneId::Perspective)
      .restore_perspective_pose({.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
  process_input();
  const auto origin_before = editor_->tools().active_settings_presentation_origin();
  QMouseEvent press(QEvent::MouseButtonPress, {300, 300}, {300, 300},
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &press);
  process_input();
  EXPECT_EQ(editor_->tools().active_settings_presentation_origin(), origin_before);
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_settings_fit_without_scrolling());

  QMouseEvent drag(QEvent::MouseMove, {500, 430}, {500, 430},
                  Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &drag);
  process_input();
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_settings_fit_without_scrolling());
}

TEST_F(QtToolSettingsDockTests,
       ExpandedDockReusesItsAvailableHeightWhenSwitchingToBevel) {
  ASSERT_TRUE(session_->viewport_workspace().set_layout(
      quader::ViewportLayoutId::Single));
  resize_dock_content(340, 850);
  const auto source = editor_->modeling().create_box(
      quader::QuaderBounds{.min = {-1, -1, -1}, .max = {1, 1, 1}},
      "Bevel settings layout");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor_->selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  settings_->set_expanded(true);
  refresh();
  ASSERT_NO_FATAL_FAILURE(expect_settings_fit_without_scrolling());

  // Operation changes can happen between two retained UI refreshes.
  ASSERT_TRUE(editor_->tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor_->selection().select_edge(source.value(), 1, 2).ok());
  ASSERT_TRUE(editor_->selection().select_all().ok());
  ASSERT_TRUE(editor_->commands().execute(
      editor_->commands().command_id_for_modeling_action("bevel_edges")).ok());
  refresh();
  ASSERT_EQ(settings_->target_id(), "edge_bevel");
  ASSERT_NO_FATAL_FAILURE(expect_settings_fit_without_scrolling());
}

TEST_F(QtToolSettingsDockTests,
       BevelSettingsChangeGeometryWhileViewportFramesContinue) {
  ASSERT_TRUE(session_->viewport_workspace().set_layout(
      quader::ViewportLayoutId::Single));
  const auto source = editor_->modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Live bevel settings");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor_->selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
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

TEST_F(QtToolSettingsDockTests,
       BridgeTensionFieldsStayLiveWithoutWritebackAndFitScaledLayouts) {
  ASSERT_TRUE(session_->viewport_workspace().set_layout(
      quader::ViewportLayoutId::Single));
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {-1.0F, -1.0F, -2.0F}),
      quader_poly::add_vertex(document, {1.0F, -1.0F, -2.0F}),
      quader_poly::add_vertex(document, {1.0F, 1.0F, -2.0F}),
      quader_poly::add_vertex(document, {-1.0F, 1.0F, -2.0F}),
      quader_poly::add_vertex(document, {-1.0F, -1.0F, 2.0F}),
      quader_poly::add_vertex(document, {1.0F, -1.0F, 2.0F}),
      quader_poly::add_vertex(document, {1.0F, 1.0F, 2.0F}),
      quader_poly::add_vertex(document, {-1.0F, 1.0F, 2.0F})};
  const auto first = quader_poly::add_face(
      document, std::array{vertices[0], vertices[1], vertices[2], vertices[3]});
  const auto second = quader_poly::add_face(
      document, std::array{vertices[7], vertices[6], vertices[5], vertices[4]});
  ASSERT_NE(first, quader_poly::kInvalidElementId);
  ASSERT_NE(second, quader_poly::kInvalidElementId);
  const auto source = editor_->modeling().create_mesh(
      quader::QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
      {.name = "Live Bridge settings"});
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor_->selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor_->selection().select_faces(source.value(),
      {static_cast<quader::QuaderElementId>(first),
       static_cast<quader::QuaderElementId>(second)}).ok());
  ASSERT_TRUE(editor_->commands().execute(
      quader::quader_command::kModelingPreviewBridgeFaces).ok());
  settings_->set_expanded(true);
  refresh();
  ASSERT_EQ(settings_->target_id(), "bridge");
  ASSERT_EQ(editor_->tools().active_settings().controls.size(), 3u);
  auto *steps = settings_->findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.bridge.steps"));
  auto *start = settings_->findChild<QDoubleSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.bridge.start_tension"));
  auto *end = settings_->findChild<QDoubleSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.bridge.end_tension"));
  ASSERT_NE(steps, nullptr);
  ASSERT_NE(start, nullptr);
  ASSERT_NE(end, nullptr);
  EXPECT_DOUBLE_EQ(start->value(), 1.0);
  EXPECT_DOUBLE_EQ(end->value(), 1.0);
  for (const auto field : {start, end}) {
    EXPECT_DOUBLE_EQ(field->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(field->maximum(), 3.0);
  }
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
  steps->setValue(4);
  settle_with_viewport_frames();
  const auto before = editor_->query().compiled_object_mesh(source.value());
  ASSERT_TRUE(before.has_value());
  const auto content_revision = session_->modeling().content_revision();
  const auto undo_before = session_->history().undo_count();
  const auto redo_before = session_->history().redo_count();
  start->setValue(0.25);
  settle_with_viewport_frames();
  end->setValue(2.5);
  settle_with_viewport_frames();
  const auto shaped = editor_->query().compiled_object_mesh(source.value());
  ASSERT_TRUE(shaped.has_value());
  ASSERT_EQ(shaped->vertices.size(), before->vertices.size());
  EXPECT_EQ(shaped->indices, before->indices);
  bool position_changed = false;
  for (std::size_t index = 0; index < shaped->vertices.size(); ++index) {
    const auto &left = before->vertices[index].position;
    const auto &right = shaped->vertices[index].position;
    position_changed = position_changed || left.x != right.x ||
                       left.y != right.y || left.z != right.z;
  }
  EXPECT_TRUE(position_changed);
  EXPECT_DOUBLE_EQ(session_->modeling().interaction_settings().bridge_start_tension,
                   0.25);
  EXPECT_DOUBLE_EQ(session_->modeling().interaction_settings().bridge_end_tension,
                   2.5);
  EXPECT_EQ(session_->modeling().content_revision(), content_revision);
  EXPECT_EQ(session_->history().undo_count(), undo_before);
  EXPECT_EQ(session_->history().redo_count(), redo_before);
  QObject observer;
  int field_value_signals = 0;
  for (const auto field : {start, end}) {
    QObject::connect(field, qOverload<double>(&QDoubleSpinBox::valueChanged),
                     &observer, [&](double) { ++field_value_signals; });
  }
  // A viewport handle uses this same owner setter; presentation must follow it
  // without emitting a numeric edit back into the operation.
  ASSERT_TRUE(editor_->tools().set_setting(
      "bridge", "operation.bridge.start_tension", 1.75).ok());
  const auto accepted_revision = editor_->tools().active_settings_revision();
  refresh();
  EXPECT_DOUBLE_EQ(start->value(), 1.75);
  EXPECT_DOUBLE_EQ(end->value(), 2.5);
  EXPECT_EQ(field_value_signals, 0);
  EXPECT_EQ(editor_->tools().active_settings_revision(), accepted_revision);
  const auto revision_values = [&] {
    const auto revisions = editor_->revisions();
    return std::array{revisions.project_revision, revisions.scene_revision,
        revisions.scene_layer_revision, revisions.scene_settings_revision,
        revisions.asset_revision, revisions.modeling_content_revision,
        revisions.modeling_selection_revision};
  };
  const auto revisions_before_rejection = revision_values();
  EXPECT_FALSE(editor_->tools().set_setting(
      "bridge", "operation.bridge.end_tension",
      std::numeric_limits<double>::quiet_NaN()).ok());
  refresh();
  EXPECT_DOUBLE_EQ(end->value(), 2.5);
  EXPECT_EQ(field_value_signals, 0);
  EXPECT_EQ(editor_->tools().active_settings_revision(), accepted_revision);
  EXPECT_EQ(revision_values(), revisions_before_rejection);
  EXPECT_EQ(session_->history().undo_count(), undo_before);
  EXPECT_EQ(session_->history().redo_count(), redo_before);
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(review_directory));
  struct LayoutCase final {
    float scale;
    int width;
    int height;
  };
  constexpr std::array kLayouts{
      LayoutCase{1.0F, 340, 650}, LayoutCase{2.0F, 640, 1000},
      LayoutCase{1.0F, 320, 650}};
  for (const auto &layout : kLayouts) {
    SCOPED_TRACE(layout.scale);
    SCOPED_TRACE(layout.width);
    apply_scale(layout.scale);
    resize_dock_content(layout.width, layout.height);
    refresh();
    ASSERT_FALSE(settings_->isHidden());
    ASSERT_TRUE(settings_->parentWidget()->rect().contains(settings_->geometry()));
    auto *scroll = settings_->findChild<QScrollArea *>(QStringLiteral(
        "tool_settings.scroll"));
    ASSERT_NE(scroll, nullptr);
    EXPECT_EQ(scroll->verticalScrollBar()->maximum(), 0);
    for (const auto field : {static_cast<QAbstractSpinBox *>(steps),
                             static_cast<QAbstractSpinBox *>(start),
                             static_cast<QAbstractSpinBox *>(end)}) {
      ASSERT_TRUE(field->isVisible());
      auto *line = field->findChild<QLineEdit *>();
      ASSERT_NE(line, nullptr);
      EXPECT_GE(line->contentsRect().width(),
                line->fontMetrics().horizontalAdvance(line->text()));
    }
    EXPECT_EQ(settings_->findChild<QDoubleSpinBox *>(QStringLiteral(
        "tool_settings.control.operation.bridge.start_tension")), start);
    EXPECT_EQ(field_value_signals, 0);
    const auto projection = session_->modeling().modeling_sdk_projection_stats_for_tests();
    for (int idle = 0; idle < 8; ++idle) {
      process_input();
      refresh();
    }
    EXPECT_EQ(editor_->tools().active_settings_revision(), accepted_revision);
    EXPECT_EQ(session_->modeling().modeling_sdk_projection_stats_for_tests()
                  .preview_publication_count,
              projection.preview_publication_count);
    EXPECT_EQ(field_value_signals, 0);
    if (!review_directory.isEmpty()) {
      ASSERT_TRUE(settings_->grab().save(QDir(review_directory).filePath(
          QStringLiteral("bridge_settings_scale_%1_width_%2.png")
              .arg(layout.scale).arg(layout.width))));
    }
  }
  EXPECT_TRUE(editor_->tools().active_settings().preview_active);
  EXPECT_EQ(session_->history().undo_count(), undo_before);
  EXPECT_EQ(session_->history().redo_count(), redo_before);
}

TEST_F(QtToolSettingsDockTests,
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
  window_->activateWindow();
  for (const auto &operation : kOperations) {
    SCOPED_TRACE(operation.action);
    ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Object).ok());
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
    ASSERT_TRUE(editor_->selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
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

TEST_F(QtToolSettingsDockTests,
       RuntimeReplacementRetainsTheDockAndAbandonsOldOperationControls) {
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  settings_->set_expanded(true);
  refresh();
  ASSERT_EQ(settings_->parentWidget(), settings_dock_);

  settings_->unbind();
  host_->abandon_interactions();
  EXPECT_FALSE(settings_->isHidden());
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
  EXPECT_FALSE(settings_->isHidden());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Side));
  ASSERT_TRUE(editor_->tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Front));
  refresh();
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
  ASSERT_TRUE(session_->viewport_workspace().focus_pane(quader::ViewportPaneId::Side));
  refresh();
  EXPECT_FALSE(settings_->isHidden());
  EXPECT_TRUE(settings_->expanded());
  EXPECT_EQ(settings_->parentWidget(), settings_dock_);
}

} // namespace


TEST(QtViewportComposedPresentationTests,
     PreparationRunsOncePerGeometryAndRejectsNestedOrBlockedPaints) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-composed-prepare-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::RenderSystem renderer;
  quader::QtViewportPanel panel(nullptr, true);
  panel.resize(900, 700);
  panel.attach_workspace(session.viewport_workspace());
  panel.set_startup_presentation_blocked(false);
  // Install the production host callbacks without creating a GPU renderer.
  EXPECT_FALSE(panel.initialize(renderer));
  std::uint64_t preparations = 0u;
  panel.set_composed_frame_prepare_callback([&]() {
    ++preparations;
    EXPECT_FALSE(panel.prepare_composed_frame());
  });

  EXPECT_TRUE(panel.prepare_composed_frame());
  EXPECT_EQ(preparations, 1u);
  for (int frame = 0; frame < 32; ++frame) {
    EXPECT_TRUE(panel.prepare_composed_frame());
  }
  EXPECT_EQ(preparations, 1u);
  auto *host = panel.findChild<QWidget *>(
      QStringLiteral("viewportSurfaceContainer_perspective"));
  ASSERT_NE(host, nullptr);
  const QPoint old_position = host->pos();
  host->move(old_position + QPoint{0, 8});
  QMoveEvent moved(host->pos(), old_position);
  QApplication::sendEvent(host, &moved);
  panel.set_startup_presentation_blocked(true);
  EXPECT_FALSE(panel.prepare_composed_frame());
  EXPECT_FALSE(panel.prepare_composed_frame());
  EXPECT_EQ(preparations, 1u);
  panel.set_startup_presentation_blocked(false);
  EXPECT_TRUE(panel.prepare_composed_frame());
  EXPECT_EQ(preparations, 2u);
  EXPECT_TRUE(panel.prepare_composed_frame());
  EXPECT_EQ(preparations, 2u);
  panel.shutdown();
}

TEST(QtViewportComposedPresentationTests,
     PaneChromeAndDockSettingsRetainWidgetCompositionWithoutNativeChildren) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-composed-widget-tree-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  ASSERT_TRUE(session.viewport_workspace().set_layout(
      quader::ViewportLayoutId::FourQuad));
  QMainWindow window;
  quader::QtViewportPanel panel(&window, true);
  window.setCentralWidget(&panel);
  panel.resize(1000, 800);
  panel.attach_workspace(session.viewport_workspace());
  panel.set_startup_presentation_blocked(false);
  QDockWidget dock(QStringLiteral("Tool Settings"), &window);
  quader::QtToolSettingsPanel settings(&dock);
  dock.setWidget(&settings);
  window.addDockWidget(Qt::LeftDockWidgetArea, &dock);

  std::size_t composed_hosts = 0u;
  std::size_t dimension_labels = 0u;
  std::size_t fly_widgets = 0u;
  for (QWidget *widget : panel.findChildren<QWidget *>()) {
    // Real popups keep their independent top-level window contract.
    if (widget->window() != &window) {
      continue;
    }
    SCOPED_TRACE(widget->objectName().toStdString());
    EXPECT_FALSE(widget->testAttribute(Qt::WA_NativeWindow));
    EXPECT_EQ(widget->internalWinId(), 0u);
    const QString name = widget->objectName();
    if (name.startsWith(QStringLiteral("viewportSurfaceContainer_"))) {
      ++composed_hosts;
    }
    if (name.startsWith(QStringLiteral("viewportEdgeLength_")) ||
        name.startsWith(QStringLiteral("viewportFootprintDimension")) ||
        name.startsWith(QStringLiteral("viewportSelectionDimension"))) {
      ++dimension_labels;
    }
    if (name.startsWith(QStringLiteral("viewportFlyCrosshair_")) ||
        name.startsWith(QStringLiteral("viewportFlySpeed_"))) {
      ++fly_widgets;
    }
  }
  EXPECT_EQ(composed_hosts, 4u);
  EXPECT_EQ(dimension_labels, 24u);
  EXPECT_EQ(fly_widgets, 8u);
  EXPECT_FALSE(settings.testAttribute(Qt::WA_NativeWindow));
  EXPECT_FALSE(panel.isAncestorOf(&settings));
  EXPECT_EQ(settings.parentWidget(), &dock);
  for (const auto pane : {quader::ViewportPaneId::Perspective,
                          quader::ViewportPaneId::Top,
                          quader::ViewportPaneId::Front,
                          quader::ViewportPaneId::Side}) {
    ASSERT_NE(panel.surface(pane), nullptr);
    EXPECT_EQ(panel.surface(pane)->handle(), nullptr);
  }
}
