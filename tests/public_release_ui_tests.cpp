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
#include "core/localization_service.hpp"
#include "core/release_features.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/project_session.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "ui/qt_operation_input_hints.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "render/render_system.hpp"
#include "scene/components.hpp"
#include "support/command_observable_state.hpp"
#include "ui/qt_environment_settings_popup.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/qt_command_palette.hpp"
#include "ui/qt_settings_dialog.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_snapping_toolbar.hpp"
#include "ui/qt_selection_toolbar.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "ui/qt_viewport_control_strip.hpp"
#include "ui/qt_viewport_display_toolbar.hpp"
#include "ui/qt_viewport_marking_menu.hpp"
#include "ui/ui_context.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/viewport_input_router.hpp"
#include "ui/ui_settings.hpp"
#include "ui/ui_system.hpp"

#include <QAction>
#include <QAbstractAnimation>
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QImage>
#include <QKeyEvent>
#include <QLayout>
#include <QTableView>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QPixmap>
#include <QStackedWidget>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>

namespace quader::test {
namespace {

/// Isolates retained window settings while keeping all test widgets hidden.
class PublicReleaseUiTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    ASSERT_TRUE(profile_.isValid());
    had_local_app_data_ = qEnvironmentVariableIsSet("LOCALAPPDATA");
    previous_local_app_data_ = qgetenv("LOCALAPPDATA");
    ASSERT_TRUE(qputenv("LOCALAPPDATA", profile_.path().toUtf8()));
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    ASSERT_EQ(current_product_release(), kPublicRelease01);
  }

  void TearDown() override {
    application_.reset();
    if (had_local_app_data_) {
      (void)qputenv("LOCALAPPDATA", previous_local_app_data_);
    } else {
      (void)qunsetenv("LOCALAPPDATA");
    }
  }

  [[nodiscard]] static std::filesystem::path resource_root() {
    return std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  }

private:
  QTemporaryDir profile_;
  bool had_local_app_data_ = false;
  QByteArray previous_local_app_data_;
  int argc_ = 1;
  char application_name_[32] = "quader-public-ui-tests";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
};

TEST_F(PublicReleaseUiTests, MeshMarkingMenuRetainsOnlyThePublicNormalDirection) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}}, "Public Mesh Menu");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  LocalizationService localization;
  ASSERT_TRUE(localization.load(resource_root() / "locales", "en-US"));
  ViewportInputRouter input_router;
  QWidget owner;
  owner.resize(700, 500);
  owner.show();
  owner.activateWindow();
  QCoreApplication::processEvents();
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, &owner, []() {
    ADD_FAILURE() << "The public marking-menu fixture did not finish.";
    if (auto *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });
  watchdog.start(3000);
  bool inspected = false;
  QTimer::singleShot(0, &owner, [&]() {
    auto *popup = QApplication::activePopupWidget();
    ASSERT_NE(popup, nullptr);
    ASSERT_EQ(popup->objectName(), QStringLiteral("viewport_marking_menu"));
    inspected = true;
    for (int slot = 0; slot < 8; ++slot) {
      const auto *button = popup->findChild<QToolButton *>(
          QStringLiteral("markingMenu.slot.%1").arg(slot));
      ASSERT_NE(button, nullptr);
      EXPECT_EQ(!button->isHidden(), slot == 2);
      if (slot == 2) {
        EXPECT_EQ(button->accessibleName(), QStringLiteral("Flip Normals"));
        EXPECT_TRUE(button->isEnabled());
      }
    }
    const auto *flip = popup->findChild<QAction *>(QStringLiteral("modeling.invert_mesh"));
    ASSERT_NE(flip, nullptr);
    EXPECT_TRUE(flip->isEnabled());
    for (const auto id : {quader_command::kEditCreateInstance,
                          quader_command::kModelingSnapMeshesToFloor,
                          quader_command::kModelingSetOriginToCenter,
                          quader_command::kModelingSetOriginToPivot,
                          quader_command::kModelingBakeLocation,
                          quader_command::kModelingBakeTransforms,
                          quader_command::kModelingMirrorSelectionX,
                          quader_command::kModelingFlipHorizontal}) {
      EXPECT_EQ(popup->findChild<QAction *>(QString::fromUtf8(id.string().data())), nullptr)
          << id.string();
    }
    for (const auto *name : {"viewport_marking_submenu_mirror",
                              "viewport_marking_submenu_flip",
                              "viewport_marking_submenu_bake"}) {
      EXPECT_EQ(popup->findChild<QMenu *>(QString::fromUtf8(name)), nullptr);
    }
    const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
    if (!review_directory.isEmpty()) {
      ASSERT_TRUE(QDir().mkpath(review_directory));
      EXPECT_TRUE(popup->grab().save(QDir(review_directory).filePath(
          QStringLiteral("marking-mesh-public-100.png"))));
    }
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(popup, &cancel);
  });
  QtViewportMarkingMenuPresenter presenter;
  const QuaderInputChord chord{
      .device = QuaderInputDevice::Keyboard, .key = QuaderKeyCode::Q};
  EXPECT_FALSE(presenter.show(owner, owner.mapToGlobal(owner.rect().center()),
      editor.viewports().focused_pane(), chord, editor, input_router, localization,
      QtUiScaleMetrics{}));
  watchdog.stop();
  EXPECT_TRUE(inspected);
}

TEST_F(PublicReleaseUiTests, ObjectAndEditModesExposeOnlyTheirSelectionDomains) {
  SceneSession scene;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(scene));
  const auto first = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "First public mesh");
  const auto second = editor.modeling().create_box(
      {.min = {3, -1, -1}, .max = {5, 1, 1}}, "Second public mesh");
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(editor.selection().select_objects({first.value(), second.value()}).ok());
  apply_qt_obsidian_application_theme(QApplication::style()->standardPalette());
  QtSelectionToolbar toolbar;
  LocalizationService localization;
  ASSERT_TRUE(localization.load(resource_root() / "locales", "en-US"));
  toolbar.set_localization(localization);
  // The public test target has no application QRC; use its shipping artwork.
  const QDir icons(QString::fromStdWString((resource_root() / "icons").wstring()));
  for (const auto *kind : {"Vertex", "Edge", "Face", "Mesh", "Group"}) {
    const QString suffix = QString::fromLatin1(kind);
    auto *action = toolbar.findChild<QAction *>(
        QStringLiteral("selectionMode%1").arg(suffix));
    ASSERT_NE(action, nullptr);
    const QIcon icon = qt_multiresolution_icon(icons.filePath(
        QStringLiteral("selection_%1.png").arg(suffix.toLower())));
    ASSERT_FALSE(icon.isNull());
    action->setIcon(icon);
  }
  toolbar.synchronize(editor, QuaderSelectionMode::Object);
  int selection_requests = 0;
  toolbar.set_mode_requested_callback([&](QuaderSelectionMode mode) {
    ++selection_requests;
    EXPECT_TRUE(editor.selection().set_mode(mode).ok());
    toolbar.synchronize(editor, editor.selection().summary().mode);
  });
  auto *combo = toolbar.findChild<QComboBox *>(QStringLiteral("editorModeCombo"));
  ASSERT_NE(combo, nullptr);
  EXPECT_EQ(combo->currentText(), QStringLiteral("Object Mode"));
  toolbar.setAttribute(Qt::WA_DontShowOnScreen, true);
  toolbar.show();
  const auto retained_actions = toolbar.actions();
  const auto retained_buttons = toolbar.findChildren<QToolButton *>();
  const auto assert_domain = [&](bool edit) {
    toolbar.adjustSize();
    QCoreApplication::processEvents();
    EXPECT_TRUE(combo->isVisible());
    EXPECT_EQ(toolbar.actions(), retained_actions);
    EXPECT_EQ(toolbar.findChildren<QToolButton *>(), retained_buttons);
    int visible_separators = 0;
    for (const auto *action : toolbar.actions()) {
      if (action->isSeparator() && action->isVisible()) {
        ++visible_separators;
      }
    }
    EXPECT_EQ(visible_separators, 1);
    for (const auto *label : toolbar.findChildren<QLabel *>()) {
      EXPECT_FALSE(label->isVisible());
    }
    for (const auto *name : {"selectionModeVertex", "selectionModeEdge",
                             "selectionModeFace", "selectionModeMesh",
                             "selectionModeGroup"}) {
      auto *action = toolbar.findChild<QAction *>(QString::fromLatin1(name));
      ASSERT_NE(action, nullptr);
      const bool component = action->objectName() != "selectionModeMesh" &&
          action->objectName() != "selectionModeGroup";
      if (component == edit) {
        EXPECT_TRUE(action->isEnabled());
      }
      EXPECT_EQ(action->isVisible(), component == edit);
      EXPECT_FALSE(action->text().isEmpty());
      EXPECT_TRUE(action->toolTip().startsWith(action->text()));
      auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(action));
      ASSERT_NE(button, nullptr);
      EXPECT_EQ(button->defaultAction(), action);
      EXPECT_EQ(button->isVisible(), component == edit);
      EXPECT_EQ(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
      EXPECT_FALSE(button->icon().isNull());
      const auto *accessible = QAccessible::queryAccessibleInterface(button);
      ASSERT_NE(accessible, nullptr);
      EXPECT_EQ(accessible->text(QAccessible::Name), action->text());
    }
  };
  const auto capture_toolbar = [&](const QString &filename) {
    const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
    if (!directory.isEmpty()) {
      ASSERT_TRUE(QDir().mkpath(directory));
      EXPECT_TRUE(toolbar.grab().save(QDir(directory).filePath(filename)));
    }
  };
  assert_domain(false);
  capture_toolbar(QStringLiteral("selection-toolbar-public-object-100.png"));
  auto *mesh = toolbar.findChild<QAction *>(QStringLiteral("selectionModeMesh"));
  auto *group = toolbar.findChild<QAction *>(QStringLiteral("selectionModeGroup"));
  ASSERT_NE(mesh, nullptr);
  ASSERT_NE(group, nullptr);
  EXPECT_TRUE(mesh->isChecked());
  for (const auto &[action, expected_mode] : {
           std::pair{group, QuaderSelectionMode::Group},
           std::pair{mesh, QuaderSelectionMode::Object}}) {
    auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(action));
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(button->isVisible());
    button->setFocus(Qt::TabFocusReason);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(button, &press);
    QCoreApplication::sendEvent(button, &release);
    EXPECT_EQ(editor.selection().summary().mode, expected_mode);
    EXPECT_TRUE(action->isChecked());
    EXPECT_FALSE((action == group ? mesh : group)->isChecked());
    assert_domain(false);
  }
  EXPECT_EQ(selection_requests, 2);
  EXPECT_FALSE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditorModeToggleEdit).ok());
  toolbar.synchronize(editor, QuaderSelectionMode::Vertex);
  EXPECT_EQ(combo->currentText(), QStringLiteral("Edit Mode"));
  EXPECT_EQ(editor.mode().state().participants.size(), 2u);
  assert_domain(true);
  capture_toolbar(QStringLiteral("selection-toolbar-public-edit-100.png"));
  for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge,
                          QuaderSelectionMode::Face}) {
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    toolbar.synchronize(editor, mode);
    EXPECT_EQ(editor.mode().state().participants.size(), 2u);
    assert_domain(true);
  }
  EXPECT_FALSE(editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  EXPECT_FALSE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditorModeToggleEdit).ok());
  toolbar.synchronize(editor, QuaderSelectionMode::Object);
  assert_domain(false);
  EXPECT_TRUE(mesh->isChecked());
  EXPECT_FALSE(group->isChecked());
  EXPECT_EQ(combo->currentIndex(), 0);
  EXPECT_EQ(editor.selection().summary().selected_object_count, 2u);
}

TEST_F(PublicReleaseUiTests, AssetBrowserOpensAtItsFinalDockGeometry) {
  QTemporaryDir project_root;
  ASSERT_TRUE(project_root.isValid());
  RenderSystem renderer;
  ProjectSession project;
  project.set_resource_root(resource_root() / "modeling_assets");
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(
      std::filesystem::path(project_root.path().toStdWString())).ok());
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  UISystem ui;
  ASSERT_TRUE(ui.initialize(renderer, resource_root()));
  auto *window = ui.main_window();
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(), kInvalidAssetId);
  auto *stack = qobject_cast<QStackedWidget *>(window->centralWidget());
  auto *workspace = window->findChild<QWidget *>(QStringLiteral("sceneWorkspace"));
  auto *dock = window->findChild<QDockWidget *>(QStringLiteral("dock_asset_browser"));
  ASSERT_NE(stack, nullptr);
  ASSERT_NE(workspace, nullptr);
  ASSERT_NE(dock, nullptr);
  // Use the real Qt dock layout without initializing a rendering device.
  stack->setCurrentWidget(workspace);
  window->resize(1600, 1000);
  window->setAnimated(false);
  ASSERT_TRUE(editor.ui().show_panel("outliner").ok());
  ASSERT_TRUE(editor.ui().show_panel("inspector").ok());
  window->show();
  QCoreApplication::processEvents();
  window->setAnimated(true);
  for (int transition = 0; transition < 3; ++transition) {
    ASSERT_TRUE(editor.ui().hide_panel("asset_browser").ok());
    QCoreApplication::processEvents();
    ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
    const QRect opening_geometry = dock->geometry();
    EXPECT_EQ(window->dockWidgetArea(dock), Qt::BottomDockWidgetArea);
    EXPECT_LE(opening_geometry.left(), window->contentsRect().left() + 2);
    EXPECT_GE(opening_geometry.right(), window->contentsRect().right() - 2);
    EXPECT_TRUE(window->isAnimated());
    for (auto *animation : window->findChildren<QPropertyAnimation *>()) {
      EXPECT_FALSE(animation->state() == QAbstractAnimation::Running &&
                   animation->propertyName() == QByteArrayLiteral("geometry"));
    }
    QCoreApplication::processEvents();
    EXPECT_EQ(dock->geometry(), opening_geometry);
  }
  const QString review = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review));
    EXPECT_TRUE(window->grab().save(QDir(review).filePath(
        QStringLiteral("asset-browser-public-open.png"))));
  }
  ASSERT_TRUE(editor.ui().hide_panel("asset_browser").ok());
  window->addDockWidget(Qt::RightDockWidgetArea, dock);
  ASSERT_TRUE(editor.ui().focus_panel("asset_browser").ok());
  EXPECT_EQ(window->dockWidgetArea(dock), Qt::RightDockWidgetArea);
  ui.shutdown();
}

TEST_F(PublicReleaseUiTests, StaleWindowStateCannotRestoreExcludedPanelsAndLaunchers) {
  const auto paths = UISettings::resolve_paths(resource_root());
  std::filesystem::create_directories(paths.directory);
  auto settings = UISettings::defaults();
  settings.set_panel_visible(UIPanelId::FaceTexture, true);
  settings.set_panel_visible(UIPanelId::Layers, true);
  ASSERT_TRUE(settings.save(paths.settings_file));
  {
    QMainWindow old_window;
    QDockWidget old_face_texture(&old_window);
    old_face_texture.setObjectName(QStringLiteral("dock_face_texture"));
    old_window.addDockWidget(Qt::RightDockWidgetArea, &old_face_texture);
    QDockWidget old_layers(&old_window);
    old_layers.setObjectName(QStringLiteral("dock_layers"));
    old_window.addDockWidget(Qt::RightDockWidgetArea, &old_layers);
    QToolBar old_texture_toolbar(&old_window);
    old_texture_toolbar.setObjectName(QStringLiteral("textureToolbar"));
    old_window.addToolBar(&old_texture_toolbar);
    const QByteArray old_state = old_window.saveState(1);
    std::ofstream output(paths.qt_window_state_file, std::ios::binary);
    ASSERT_TRUE(output.is_open());
    output.write(old_state.constData(), old_state.size());
    ASSERT_TRUE(output.good());
  }

  RenderSystem render_system;
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root()));
  ASSERT_NE(ui.main_window(), nullptr);
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  ui.install_runtime_hooks(editor, session.scene(), session.assets(),
                           session.modeling(), session.selection(),
                           session.native_tools(), session.point_placement(),
                           kInvalidAssetId);
  const bool outliner_visible = editor.ui().panel_visible("outliner");
  for (const char *panel : {"face_texture", "Face Texture", "layers", "Layers"}) {
    const auto shown = editor.ui().show_panel(panel);
    EXPECT_FALSE(shown.ok());
    EXPECT_FALSE(shown.changed());
    const auto focused = editor.ui().focus_panel(panel);
    EXPECT_FALSE(focused.ok());
    EXPECT_FALSE(focused.changed());
    EXPECT_FALSE(editor.ui().panel_visible(panel));
  }
  ui.refresh({}, {});
  EXPECT_EQ(editor.ui().panel_visible("outliner"), outliner_visible);
  EXPECT_NE(ui.main_window()->findChild<QDockWidget *>(
                QStringLiteral("dock_outliner")), nullptr);
  EXPECT_NE(ui.main_window()->findChild<QDockWidget *>(
                QStringLiteral("dock_inspector")), nullptr);
  EXPECT_NE(ui.main_window()->findChild<QDockWidget *>(
                QStringLiteral("dock_asset_browser")), nullptr);
  EXPECT_NE(ui.main_window()->findChild<QWidget *>(
                QStringLiteral("assetBrowser")), nullptr);
  for (const char *name : {"dock_layers", "panel_layers",
                           "dock_face_texture", "face_texture.panel",
                           "textureToolbar", "materialBrowser.window",
                           "materialBrowser.embedded",
                           "materialFindReplace.dialog",
                           "standardMaterialEditor.window", "textureInfo.window"}) {
    EXPECT_EQ(ui.main_window()->findChild<QWidget *>(QString::fromUtf8(name)),
              nullptr) << name;
  }

  auto *tools = dynamic_cast<QtToolToolbar *>(
      ui.main_window()->findChild<QToolBar *>(QStringLiteral("toolToolbar")));
  auto *snapping = dynamic_cast<QtSnappingToolbar *>(
      ui.main_window()->findChild<QToolBar *>(QStringLiteral("snappingToolbar")));
  auto *display = dynamic_cast<QtViewportDisplayToolbar *>(
      ui.main_window()->findChild<QToolBar *>(QStringLiteral("viewportDisplaySources")));
  ASSERT_NE(tools, nullptr);
  ASSERT_NE(snapping, nullptr);
  ASSERT_NE(display, nullptr);
  EXPECT_EQ(ui.main_window()->toolBarArea(display), Qt::NoToolBarArea);
  EXPECT_TRUE(display->isHidden());
  EXPECT_FALSE(display->toggleViewAction()->isVisible());
  EXPECT_EQ(ui.main_window()->findChild<QToolBar *>(
                QStringLiteral("viewportDisplayToolbar")), nullptr);
  const auto tool_actions = tools->actions();
  ASSERT_EQ(tool_actions.size(), 3);
  EXPECT_EQ(tool_actions[0]->objectName(), QStringLiteral("tool_create_primitive"));
  EXPECT_EQ(tool_actions[1]->objectName(), QStringLiteral("tool_insert_edge_loop"));
  EXPECT_EQ(tool_actions[2]->objectName(), QStringLiteral("tool_knife"));
  for (QAction *action : tool_actions) {
    EXPECT_FALSE(action->isSeparator());
  }
  EXPECT_EQ(ui.main_window()->findChild<QWidget *>(QStringLiteral("viewportControlStrip")), nullptr);
  const auto snapping_actions = snapping->viewport_actions();
  const auto display_actions = display->viewport_actions();
  ASSERT_NE(display_actions[0], nullptr);
  EXPECT_EQ(display_actions[0]->objectName(), QStringLiteral("viewportWireframeOnShaded"));
  EXPECT_FALSE(display_actions[0]->isChecked());
  EXPECT_EQ(display_actions[3], nullptr);
  EXPECT_FALSE(release_has_feature(ReleaseFeature::CameraSettings));
  for (const char *pane : {"perspective", "top", "front", "side"}) {
    SCOPED_TRACE(pane);
    EXPECT_EQ(ui.main_window()->findChild<QWidget *>(
                  QStringLiteral("viewportNavigationGizmo_") +
                  QString::fromUtf8(pane)), nullptr);
    auto *strip = dynamic_cast<QtViewportControlStrip *>(ui.main_window()->findChild<QWidget *>(
        QStringLiteral("viewportToolbar_") + QString::fromUtf8(pane)));
    ASSERT_NE(strip, nullptr);
    auto *layout = strip->findChild<QToolButton *>(
        QStringLiteral("viewportLayout_") + QString::fromUtf8(pane));
    auto *camera_settings = strip->findChild<QToolButton *>(
        QStringLiteral("viewportCameraSettings_") + QString::fromUtf8(pane));
    ASSERT_NE(layout, nullptr);
    ASSERT_NE(camera_settings, nullptr);
    EXPECT_TRUE(layout->isEnabled());
    EXPECT_TRUE(camera_settings->isEnabled());
    ASSERT_NE(layout->menu(), nullptr);
    EXPECT_EQ(layout->menu()->actions().size(), 8);
    const auto expect_clone = [&](QAction *source) {
      EXPECT_NE(source, nullptr);
      if (source == nullptr) return;
      auto *button = strip->findChild<QToolButton *>(QStringLiteral("viewportStrip_") + source->objectName());
      ASSERT_NE(button, nullptr);
      ASSERT_NE(button->defaultAction(), nullptr);
      EXPECT_NE(button->defaultAction(), source);
      EXPECT_EQ(button->defaultAction()->objectName(), source->objectName());
      EXPECT_TRUE(button->defaultAction()->shortcuts().empty());
    };
    for (const auto tool : {ViewportToolId::Select, ViewportToolId::Move,
                            ViewportToolId::Rotate, ViewportToolId::Scale}) {
      QAction *action = tools->tool_action(tool);
      expect_clone(action);
      EXPECT_FALSE(tools->actions().contains(action));
      EXPECT_EQ(tools->widgetForAction(action), nullptr);
      EXPECT_TRUE(action->isVisible());
    }
    QAction *lasso_source = tools->tool_action(ViewportToolId::LassoSelect);
    ASSERT_NE(lasso_source, nullptr);
    EXPECT_FALSE(tools->actions().contains(lasso_source));
    EXPECT_EQ(tools->widgetForAction(lasso_source), nullptr);
    EXPECT_TRUE(lasso_source->isVisible());
    QAction *paint_source = tools->tool_action(ViewportToolId::PaintSelect);
    ASSERT_NE(paint_source, nullptr);
    EXPECT_FALSE(tools->actions().contains(paint_source));
    EXPECT_EQ(tools->widgetForAction(paint_source), nullptr);
    EXPECT_TRUE(paint_source->isVisible());
    EXPECT_FALSE(paint_source->icon().isNull());
    auto *selection = strip->findChild<QToolButton *>(QStringLiteral("viewportStrip_tool_select"));
    ASSERT_NE(selection, nullptr);
    EXPECT_EQ(selection->popupMode(), QToolButton::DelayedPopup);
    ASSERT_NE(selection->defaultAction()->menu(), nullptr);
    EXPECT_EQ(selection->defaultAction()->menu()->objectName(),
              QStringLiteral("viewportSelectionToolMenu_") + QString::fromUtf8(pane));
    ASSERT_EQ(selection->defaultAction()->menu()->actions().size(), 3);
    EXPECT_TRUE(selection->defaultAction()->menu()->actions()[0]->text().startsWith(QStringLiteral("Box Select")));
    EXPECT_TRUE(selection->defaultAction()->menu()->actions()[1]->text().startsWith(QStringLiteral("Lasso")));
    EXPECT_EQ(selection->defaultAction()->menu()->actions()[0]->objectName(), QStringLiteral("selectionBox"));
    EXPECT_EQ(selection->defaultAction()->menu()->actions()[1]->objectName(), QStringLiteral("selectionLasso"));
    EXPECT_EQ(selection->defaultAction()->menu()->actions()[2]->objectName(), QStringLiteral("selectionPaint"));
    EXPECT_EQ(selection->defaultAction()->menu()->actions()[2]->text(), QStringLiteral("Paint Select\tCtrl+Q"));
    for (QAction *choice : selection->defaultAction()->menu()->actions()) {
      EXPECT_TRUE(choice->shortcuts().empty());
    }
    EXPECT_EQ(strip->findChild<QToolButton *>(
                  QStringLiteral("viewportStrip_") + lasso_source->objectName()), nullptr);
    auto *local = strip->findChild<QToolButton *>(QStringLiteral("viewportStrip_toggle_transform_space"));
    auto *backface = strip->findChild<QToolButton *>(QStringLiteral("viewportStrip_backfaceSelection"));
    ASSERT_NE(local, nullptr);
    ASSERT_NE(backface, nullptr);
    EXPECT_EQ(strip->layout()->indexOf(backface), strip->layout()->indexOf(local) + 1);
    for (QAction *action : snapping_actions) expect_clone(action);
    for (QAction *action : display_actions) {
      if (action != nullptr) expect_clone(action);
    }
    EXPECT_EQ(strip->findChild<QToolButton *>(QStringLiteral("viewportStrip_viewportMeshGrid")), nullptr);
    EXPECT_EQ(strip->findChild<QAction *>(QStringLiteral("viewportMeshGrid")), nullptr);
    auto *camera = strip->findChild<QPushButton *>(QStringLiteral("viewportCamera_") + QString::fromUtf8(pane));
    auto *shading = strip->findChild<QPushButton *>(QStringLiteral("viewportShading_") + QString::fromUtf8(pane));
    ASSERT_NE(camera, nullptr);
    ASSERT_NE(shading, nullptr);
    ASSERT_NE(camera->menu(), nullptr);
    ASSERT_NE(shading->menu(), nullptr);
    EXPECT_NE(camera->menu(), shading->menu());
    for (QAction *action : shading->menu()->actions()) {
      EXPECT_FALSE(action->text().startsWith(QStringLiteral("Camera")));
    }
  }

  int camera_launchers = 0;
  for (QPushButton *button : ui.main_window()->findChildren<QPushButton *>()) {
    if (button->objectName().startsWith(QStringLiteral("viewportEnvironment_"))) {
      ++camera_launchers;

    }
    if (button->objectName().startsWith(QStringLiteral("viewportShading_"))) {
      ASSERT_NE(button->menu(), nullptr);
      for (const QAction *action : button->menu()->actions()) {
        EXPECT_FALSE(action->text().startsWith(QStringLiteral("Rendered")));
        if (action->menu() != nullptr) {
          EXPECT_NE(action->menu()->title(), QStringLiteral("Debug View"));
        }
      }
    }
  }
  EXPECT_EQ(camera_launchers, 0);
  EXPECT_EQ(ui.main_window()->findChild<QWidget *>(
                QStringLiteral("environmentSettings")), nullptr);
  ui.shutdown();
}

TEST_F(PublicReleaseUiTests, CameraSettingsHaveNoControlsAndNativeClippingRemainsAvailable) {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  LocalizationService localization;
  ASSERT_TRUE(localization.load(resource_root() / "locales", "en-US"));
  QtEnvironmentSettingsPopup popup;
  popup.refresh(editor, session.scene(), &localization);
  auto *tree = popup.findChild<QTreeWidget *>(
      QStringLiteral("environmentSettings.tree"));
  ASSERT_NE(tree, nullptr);
  EXPECT_EQ(tree->topLevelItemCount(), 0);
  EXPECT_TRUE(popup.findChildren<QDoubleSpinBox *>().empty());
  const auto before = editor.query().scene_settings();
  auto settings = before;
  settings.camera.near_plane = 0.125f;
  settings.camera.far_plane = 1234.0f;
  ASSERT_TRUE(editor.scene().set_scene_settings(settings).ok());
  const auto after = editor.query().scene_settings();
  EXPECT_FLOAT_EQ(after.camera.near_plane, 0.125f);
  EXPECT_FLOAT_EQ(after.camera.far_plane, 1234.0f);
  EXPECT_FLOAT_EQ(after.camera.aperture, before.camera.aperture);
  EXPECT_FLOAT_EQ(after.camera.shutter_speed, before.camera.shutter_speed);
  EXPECT_FLOAT_EQ(after.camera.sensitivity, before.camera.sensitivity);
  EXPECT_EQ(after.environment.environment_id, before.environment.environment_id);
  EXPECT_FLOAT_EQ(after.environment.intensity, before.environment.intensity);
  EXPECT_EQ(after.fog.enabled, before.fog.enabled);
  EXPECT_EQ(after.post_process.enabled, before.post_process.enabled);

  popup.refresh(editor, session.scene(), &localization);
  const auto presentation_updates = popup.presentation_update_count();
  const auto settings_revision = session.scene().scene_settings_revision();
  ASSERT_TRUE(editor.scene().create_empty().ok());
  for (int frame = 0; frame < 8; ++frame) {
    popup.refresh(editor, session.scene(), &localization);
  }
  EXPECT_EQ(session.scene().scene_settings_revision(), settings_revision);
  EXPECT_EQ(popup.presentation_update_count(), presentation_updates);
  EXPECT_TRUE(popup.findChildren<QDoubleSpinBox *>().empty());
}

TEST_F(PublicReleaseUiTests,
       DefaultImagePreferenceWorksWithoutUnlockingAuthoredMaterialsOrChangingSaves) {
  const auto paths = UISettings::resolve_paths(resource_root());
  std::filesystem::create_directories(paths.directory);
  const auto source = paths.directory / "chosen-image.png";
  QImage image(4, 4, QImage::Format_ARGB32);
  image.fill(qRgb(40, 100, 180));
  ASSERT_TRUE(image.save(QString::fromStdWString(source.wstring()), "PNG"));

  RenderSystem render_system;
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root()));
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  ui.install_runtime_hooks(editor, session.scene(), session.assets(),
                           session.modeling(), session.selection(),
                           session.native_tools(), session.point_placement(),
                           kInvalidAssetId);
  EXPECT_FALSE(release_has_feature(ReleaseFeature::Materials));
  auto *browse = ui.main_window()->findChild<QPushButton *>(
      QStringLiteral("settingsDefaultMaterialBrowse"));
  ASSERT_NE(browse, nullptr);
  EXPECT_TRUE(browse->isEnabled());
  EXPECT_NE(ui.main_window()->findChild<QLabel *>(
                QStringLiteral("settingsDefaultMaterialPreview")), nullptr);
  EXPECT_EQ(ui.main_window()->findChild<QWidget *>(
                QStringLiteral("standardMaterialEditor.window")), nullptr);

  const auto mesh = editor.modeling().create_box(
      {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Existing mesh");
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor.modeling().update_transform_translate({0.25f, 0.f, 0.f}).ok());
  const auto before = capture_command_observable_state(editor);
  const auto scene_bytes = QdrDocumentWriter::serialize_project_document_bytes(session.document());
  int publications = 0;
  std::filesystem::path published;
  ui.set_default_material_texture_changed_callback(
      [&](const std::filesystem::path &path) { ++publications; published = path; });
  const auto chosen = editor.preferences().set_default_material_texture(source);
  ASSERT_TRUE(chosen.ok()) << chosen.error_message();
  EXPECT_TRUE(chosen.changed());
  EXPECT_EQ(publications, 1);
  const auto managed = editor.preferences().default_material_texture();
  EXPECT_EQ(published, managed);
  EXPECT_NE(managed, source);
  EXPECT_TRUE(std::filesystem::is_regular_file(managed));
  EXPECT_EQ(UISettings::load(paths.settings_file).default_material_texture(), managed);
  constexpr auto kDocumentDomains = kAllCommandObservableDomains &
      ~command_observable_domain_mask(CommandObservableDomain::Preferences);
  const auto difference = diff_command_observable_state(
      before, capture_command_observable_state(editor), kDocumentDomains);
  EXPECT_TRUE(difference.empty()) << difference.summary;
  const auto after_bytes = QdrDocumentWriter::serialize_project_document_bytes(session.document());
  EXPECT_EQ(after_bytes, scene_bytes);
  const auto reopened = QdrDocumentReader::deserialize_project_document(after_bytes, kPublicRelease01);
  EXPECT_TRUE(reopened.ok) << reopened.message;

  const auto repeated = editor.preferences().set_default_material_texture(source);
  EXPECT_TRUE(repeated.ok());
  EXPECT_FALSE(repeated.changed());
  EXPECT_EQ(publications, 1);
  const auto invalid = editor.preferences().set_default_material_texture(paths.directory / "missing.png");
  EXPECT_FALSE(invalid.ok());
  EXPECT_FALSE(invalid.changed());
  EXPECT_EQ(editor.preferences().default_material_texture(), managed);
  EXPECT_EQ(publications, 1);
  const auto reset = editor.preferences().set_default_material_texture({});
  EXPECT_TRUE(reset.ok());
  EXPECT_TRUE(reset.changed());
  EXPECT_EQ(publications, 2);
  EXPECT_TRUE(published.empty());
  EXPECT_TRUE(editor.preferences().default_material_texture().empty());
  ui.shutdown();
}

TEST_F(PublicReleaseUiTests, MeshInspectorRetainsDoubleSidedWithoutCastShadows) {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  const auto box = editor.modeling().create_box(
      {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Inspector Box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization;
  ASSERT_TRUE(localization.load(resource_root() / "locales", "en-US"));
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(), .assets = session.assets(),
                .modeling = session.modeling(), .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
  InspectorPanel panel;
  panel.refresh(context);
  EXPECT_EQ(panel.findChild<QCheckBox *>(
                QStringLiteral("inspector.lighting.cast_shadows")), nullptr);
  auto *double_sided = panel.findChild<QCheckBox *>(
      QStringLiteral("inspector.lighting.double_sided"));
  ASSERT_NE(double_sided, nullptr);
  const auto *mesh = session.scene().mesh(box.value().entity);
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(panel.findChild<QComboBox *>(
                QStringLiteral("inspector.physics.collision_type")), nullptr);
  EXPECT_EQ(panel.findChild<QComboBox *>(
                QStringLiteral("inspector.physics.body_type")), nullptr);
  EXPECT_NE(panel.findChild<QCheckBox *>(
                QStringLiteral("inspector.physics.generate_collisions")), nullptr);
  EXPECT_NE(panel.findChild<QCheckBox *>(
                QStringLiteral("inspector.physics.display_collision")), nullptr);
  EXPECT_EQ(mesh->collision_type, MeshCollisionType::Mesh);
  EXPECT_EQ(mesh->body_type, MeshPhysicsBodyType::StaticBody);
  const bool before_shadows = mesh->cast_shadows;
  const bool next_double_sided = !mesh->double_sided;
  double_sided->setChecked(next_double_sided);
  mesh = session.scene().mesh(box.value().entity);
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->double_sided, next_double_sided);
  EXPECT_EQ(mesh->cast_shadows, before_shadows);
}

TEST_F(PublicReleaseUiTests, ObjectMenuKeepsPublicGroupsAndNestedCleanUp) {
  apply_qt_obsidian_application_theme(QApplication::style()->standardPalette());
  QtUiScaleApplicationController scale_controller(*qApp);
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  LocalizationService localization;
  ASSERT_TRUE(localization.load(resource_root() / "locales", "en-US"));
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  QMenuBar menu_bar;
  QtMainMenu menu;
  menu.initialize(menu_bar, QtMainMenuContext{
      .editor = editor, .modeling = session.modeling(),
      .viewport_settings = viewport_settings, .localization = localization,
  });
  auto *object = menu_bar.findChild<QMenu *>(QStringLiteral("menu_object"));
  ASSERT_NE(object, nullptr);
  const auto actions = object->actions();
  ASSERT_FALSE(actions.empty());
  EXPECT_FALSE(actions.front()->isSeparator());
  EXPECT_FALSE(actions.back()->isSeparator());
  QStringList rows;
  for (auto *action : actions) {
    rows.append(action->isSeparator() ? QStringLiteral("separator")
                : action->menu() != nullptr ? action->menu()->objectName()
                                     : action->objectName());
  }
  EXPECT_EQ(rows, (QStringList{
      QStringLiteral("command_edit_group"),
      QStringLiteral("command_edit_ungroup"),
      QStringLiteral("command_edit_parent_to_active"),
      QStringLiteral("command_edit_unparent"),
      QStringLiteral("separator"),
      QStringLiteral("command_edit_duplicate"),
      QStringLiteral("separator"),
      QStringLiteral("menu_object_cleanup"),
  }));
  auto *cleanup = object->findChild<QMenu *>(QStringLiteral("menu_object_cleanup"));
  ASSERT_NE(cleanup, nullptr);
  EXPECT_EQ(cleanup->title(), QStringLiteral("Clean Up"));
  ASSERT_EQ(cleanup->actions().size(), 1);
  EXPECT_EQ(cleanup->actions().front()->objectName(),
            QStringLiteral("command_modeling_remove_doubles"));
  EXPECT_EQ(cleanup->actions().front()->text(), QStringLiteral("Remove Doubles"));
  EXPECT_EQ(cleanup->actions().front()->menu(), nullptr);
  auto *mesh = menu_bar.findChild<QMenu *>(QStringLiteral("menu_mesh"));
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->findChild<QAction *>(
                QStringLiteral("command_modeling_remove_doubles")), nullptr);
  for (const char *name : {"menu_object_align", "menu_object_distribute_centers",
                           "menu_object_distribute_gaps", "menu_object_instances",
                           "menu_object_materials"}) {
    EXPECT_EQ(object->findChild<QMenu *>(QString::fromLatin1(name)), nullptr);
  }
  EXPECT_EQ(object->findChild<QAction *>(
                QStringLiteral("command_tools_activate_align")), nullptr);
  EXPECT_FALSE(editor.commands().descriptor(
      quader_command::kToolsActivateAlign).has_value());
  EXPECT_TRUE(editor.commands().descriptor(quader_command::kDeleteSelection)
                  .has_value());

  auto *view = menu_bar.findChild<QMenu *>(QStringLiteral("menu_view"));
  auto *window = menu_bar.findChild<QMenu *>(QStringLiteral("menu_window"));
  auto *display = menu_bar.findChild<QMenu *>(QStringLiteral("menu_view_display"));
  ASSERT_NE(view, nullptr);
  ASSERT_NE(window, nullptr);
  EXPECT_EQ(menu_bar.findChild<QMenu *>(QStringLiteral("menu_window_layout")),
            nullptr);
  ASSERT_NE(display, nullptr);
  const auto view_actions = view->actions();
  ASSERT_GE(view_actions.size(), 2);
  EXPECT_EQ(view_actions.front()->objectName(),
            QStringLiteral("command_viewport_toggle_local_visibility"));
  EXPECT_TRUE(view_actions[1]->isSeparator());
  EXPECT_FALSE(display->actions().contains(view_actions.front()));
  for (const char *name : {"command_scene_hide_selection",
                           "command_scene_unhide_hidden_objects"}) {
    auto *action = menu_bar.findChild<QAction *>(QString::fromLatin1(name));
    ASSERT_NE(action, nullptr);
    EXPECT_TRUE(view_actions.contains(action));
    EXPECT_FALSE(actions.contains(action));
  }
  for (const char *name : {"command_viewport_toggle_transform_space",
                           "command_viewport_spawn_window"}) {
    EXPECT_EQ(menu_bar.findChild<QAction *>(QString::fromLatin1(name)), nullptr);
  }
  EXPECT_TRUE(editor.commands()
                  .descriptor(quader_command::kViewportToggleTransformSpace)
                  .has_value());
  EXPECT_TRUE(editor.commands()
                  .descriptor(quader_command::kViewportSpawnWindow)
                  .has_value());

  const auto review_menu = [&](QMenu &reviewed, const QString &filename,
                               int &row_height) {
    reviewed.setAttribute(Qt::WA_DontShowOnScreen, true);
    reviewed.popup(QPoint(20, 20));
    QCoreApplication::processEvents();
    ASSERT_TRUE(reviewed.isVisible());
    reviewed.setActiveAction(reviewed.actions().front());
    row_height = reviewed.actionGeometry(reviewed.actions().front()).height();
    EXPECT_GE(row_height, scale_controller.metrics().px(28));
    const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
    if (!directory.isEmpty()) {
      ASSERT_TRUE(QDir().mkpath(directory));
      QImage image(reviewed.size(), QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      reviewed.render(&image);
      EXPECT_TRUE(image.save(QDir(directory).filePath(filename)));
    }
    reviewed.close();
    QCoreApplication::processEvents();
  };
  const auto review_roots = [&](const QString &suffix) {
    int row_height = 0;
    review_menu(*view, QStringLiteral("view-menu-public-") + suffix + ".png",
                row_height);
    review_menu(*window, QStringLiteral("window-menu-public-") + suffix + ".png",
                row_height);
    review_menu(*cleanup, QStringLiteral("object-cleanup-menu-public-") + suffix + ".png",
                row_height);
  };
  int row_100 = 0;
  review_menu(*object, QStringLiteral("object-menu-public-100.png"), row_100);
  review_roots(QStringLiteral("100"));
  ASSERT_TRUE(scale_controller.apply_factor(2.0F));
  int row_200 = 0;
  review_menu(*object, QStringLiteral("object-menu-public-200.png"), row_200);
  EXPECT_GT(row_200, row_100);
  review_roots(QStringLiteral("200"));
  ASSERT_TRUE(scale_controller.apply_factor(1.0F));
  int row_down = 0;
  review_menu(*object, QStringLiteral("object-menu-public-scale-down.png"), row_down);
  EXPECT_EQ(row_down, row_100);
  review_roots(QStringLiteral("scale-down"));
}

TEST_F(PublicReleaseUiTests, AdvancedSelectionDisappearsOnlyFromDiscoverySurfaces) {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  LocalizationService localization;
  ASSERT_TRUE(localization.load(resource_root() / "locales", "en-US"));
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  QMainWindow window;
  QtMainMenu menu;
  menu.initialize(*window.menuBar(), QtMainMenuContext{
      .editor = editor, .modeling = session.modeling(),
      .viewport_settings = viewport_settings, .localization = localization,
  });
  QtSettingsDialog settings;
  settings.set_context(ui_settings, viewport_settings, localization,
                        editor.commands(), {});
  auto *table = settings.findChild<QTableView *>(
      QStringLiteral("settingsKeybindingsTable"));
  ASSERT_NE(table, nullptr);
  ASSERT_NE(table->model(), nullptr);
  ASSERT_GT(table->model()->rowCount(), 0);
  QtCommandPalette palette(editor.commands(), localization);
  palette.setAttribute(Qt::WA_DontShowOnScreen, true);
  palette.open_palette();
  auto *search = palette.findChild<QLineEdit *>(
      QStringLiteral("commandPalette.search"));
  auto *results = palette.findChild<QTreeWidget *>(
      QStringLiteral("commandPalette.results"));
  ASSERT_NE(search, nullptr);
  ASSERT_NE(results, nullptr);
  for (const QuaderCommandId id : {
           quader_command::kSelectEdgeDirection,
           quader_command::kSelectCoplanarFaces,
           quader_command::kSelectFaceNormal,
           quader_command::kSelectMeshNGons,
           quader_command::kSelectSimilarMeshes}) {
    const QString target = QString::fromUtf8(id.string().data(),
                                            static_cast<qsizetype>(id.string().size()));
    EXPECT_TRUE(editor.commands().descriptor(id).has_value()) << id.string();
    for (const QAction *action : window.menuBar()->findChildren<QAction *>()) {
      EXPECT_NE(action->property("quaderCommandId").toString(), target);
    }
    for (int row = 0; row < table->model()->rowCount(); ++row) {
      EXPECT_NE(table->model()->index(row, 0).data(Qt::UserRole).toString(), target);
    }
    search->setText(target);
    for (int row = 0; row < results->topLevelItemCount(); ++row) {
      EXPECT_NE(results->topLevelItem(row)->data(0, Qt::UserRole).toString(), target);
    }
  }
  for (const QuaderCommandId id : {quader_command::kToolsActivateCreateTorus,
                                  quader_command::kToolsActivateCreateCone}) {
    const QString target = QString::fromUtf8(id.string().data(),
                                            static_cast<qsizetype>(id.string().size()));
    for (const QAction *action : window.menuBar()->findChildren<QAction *>()) {
      EXPECT_NE(action->property("quaderCommandId").toString(), target);
    }
  }
  EXPECT_EQ(window.menuBar()->findChild<QAction *>(QStringLiteral("panel_layers")),
            nullptr);
  for (const QString component : {QStringLiteral("edge"), QStringLiteral("face")}) {
    auto *component_menu = window.menuBar()->findChild<QMenu *>(
        QStringLiteral("menu_") + component);
    ASSERT_NE(component_menu, nullptr);
    EXPECT_EQ(component_menu->findChild<QAction *>(
                  QStringLiteral("command_modeling_bridge_") + component + 's'),
              nullptr);
    EXPECT_NE(component_menu->findChild<QAction *>(
                  QStringLiteral("command_modeling_interpolated_bridge_") + component + 's'),
              nullptr);
  }
  palette.reject();
}

TEST_F(PublicReleaseUiTests, ContextHintsRespectPublicAvailabilityAndCurrentBindings) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto idle = editor.tools().active_input_hints();
  ASSERT_EQ(idle.size(), 3u);
  EXPECT_EQ(idle[0].label_fallback, "Orbit");
  EXPECT_EQ(idle[1].label_fallback, "Pan");
  EXPECT_EQ(idle[2].label_fallback, "Fly Navigation");
  const auto contains = [](const auto &hints, QuaderCommandId command) {
    for (const auto &hint : hints) if (hint.command.string() == command.string()) return true;
    return false;
  };
  for (const auto command : {quader_command::kToolsActivateCreateBox,
                             quader_command::kToolsActivateCreateCylinder,
                             quader_command::kToolsActivateCreateSphere}) {
    ASSERT_TRUE(editor.commands().execute(command).ok());
    const auto pending = editor.tools().active_input_hints();
    EXPECT_FALSE(contains(pending, quader_command::kToolsApplyPrimitiveCreation));
    EXPECT_TRUE(contains(pending, quader_command::kToolsCancelPrimitiveCreation));
  }
  for (const auto blocked : {quader_command::kToolsActivateCreateCone,
                             quader_command::kToolsActivateCreateTorus}) {
    EXPECT_FALSE(editor.commands().execute(blocked).ok());
    EXPECT_TRUE(contains(editor.tools().active_input_hints(), quader_command::kToolsTogglePrimitiveHemisphere));
  }
  LocalizationService localization;
  ASSERT_TRUE(localization.load(resource_root() / "locales", "en-US"));
  QtOperationInputHints widget;
  ASSERT_TRUE(editor.commands().set_shortcut(quader_command::kToolsFlipPrimitiveFaces, "Ctrl+K D").ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  bool rebound_found = false;
  for (const auto &hint : widget.hints()) rebound_found |= hint.badge == QStringLiteral("Ctrl+K D");
  EXPECT_TRUE(rebound_found);
  ASSERT_TRUE(editor.commands().remove_shortcut(quader_command::kToolsFlipPrimitiveFaces).ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  for (const auto &hint : widget.hints()) EXPECT_NE(hint.badge, QStringLiteral("Ctrl+K D"));

  QuaderOperationInputContext context;
  context.material_transfer = QuaderInputMaterialTransfer::ContinueTexture;
  EXPECT_TRUE(editor.tools().active_input_hints(context).empty());
  context = {};
  context.point_light_range_hovered = true;
  EXPECT_TRUE(editor.tools().active_input_hints(context).empty());
  context = {};
  context.selection_gesture = QuaderInputSelectionGesture::EnterInstance;
  EXPECT_TRUE(editor.tools().active_input_hints(context).empty());

  SceneSession session;
  QuaderEditorApi phase_editor{QuaderApiSessionInternalAccess::from_scene_session(session)};
  ASSERT_TRUE(phase_editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ModelingBoxPreview preview;
  preview.ok = true;
  preview.valid = true;
  preview.drag_state = ModelingBoxDragState::Height;
  preview.corners = {QVec3{-1, 0, -1}, QVec3{-1, 0, 1}, QVec3{1, 0, 1}, QVec3{1, 0, -1},
                     QVec3{-1, 2, -1}, QVec3{-1, 2, 1}, QVec3{1, 2, 1}, QVec3{1, 2, -1}};
  preview.corner_count = preview.corners.size();
  ASSERT_TRUE(session.primitive_creation().accept_preview(preview, "Hint Box").ok);
  EXPECT_TRUE(contains(phase_editor.tools().active_input_hints(), quader_command::kToolsApplyPrimitiveCreation));
  context = {};
  context.pointer_dragging = true;
  EXPECT_FALSE(contains(phase_editor.tools().active_input_hints(context), quader_command::kToolsApplyPrimitiveCreation));
}

} // namespace
} // namespace quader::test
