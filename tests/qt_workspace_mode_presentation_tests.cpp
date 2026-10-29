////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/qt_commands_library_panel.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_selection_toolbar.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "ui/ui_panel_manager.hpp"
#include "ui/ui_settings.hpp"

#include <gtest/gtest.h>
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QTemporaryDir>

#include <array>
#include <filesystem>
#include <memory>

namespace quader {
namespace {

class WorkspaceModePresentationTests : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
  }

  int argc_ = 1;
  char application_name_[32] = "workspace_mode_presentation";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
};

QAction *command_action(QMenuBar &bar, QuaderCommandId id) {
  for (auto *action : bar.findChildren<QAction *>()) {
    if (action->property("quaderCommandId").toString() ==
        QString::fromUtf8(id.string().data(), static_cast<qsizetype>(id.string().size()))) {
      return action;
    }
  }
  return nullptr;
}

TEST_F(WorkspaceModePresentationTests, DefaultHidesGeometryAndRetainsSceneControls) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  QtSelectionToolbar selection;
  QtToolToolbar tools;
  LocalizationService localization;
  auto viewport_settings = ViewportSettings::defaults();
  QMenuBar bar;
  QtMainMenu menu;
  menu.initialize(bar, {.editor = editor, .modeling = session.modeling(),
                       .viewport_settings = viewport_settings, .localization = localization});
  selection.synchronize(editor, QuaderSelectionMode::Object);
  tools.synchronize(editor, editor.selection().summary());
  const auto retained_actions = tools.actions();
  auto *mesh_menu = bar.findChild<QMenu *>(QStringLiteral("menu_mesh"));
  ASSERT_NE(mesh_menu, nullptr);
  const auto *const retained_menu = mesh_menu;
  auto *create = tools.tool_action(ViewportToolId::CreatePrimitive);
  auto *light = command_action(bar, quader_command::kSceneCreatePointLight);
  ASSERT_NE(create, nullptr);
  ASSERT_NE(light, nullptr);
  EXPECT_TRUE(create->isVisible());

  ASSERT_TRUE(editor.mode().set_workspace_mode(QuaderWorkspaceMode::Default).ok());
  selection.synchronize(editor, QuaderSelectionMode::Object);
  tools.synchronize(editor, editor.selection().summary());
  menu.refresh();
  for (const auto *name : {"selectionModeVertex", "selectionModeEdge", "selectionModeFace"}) {
    auto *action = selection.findChild<QAction *>(QString::fromLatin1(name));
    ASSERT_NE(action, nullptr);
    EXPECT_FALSE(action->isVisible());
  }
  EXPECT_TRUE(selection.findChild<QAction *>(QStringLiteral("selectionModeMesh"))->isVisible());
  EXPECT_TRUE(selection.findChild<QAction *>(QStringLiteral("selectionModeGroup"))->isVisible());
  for (const auto tool : {ViewportToolId::CreatePrimitive, ViewportToolId::Cut,
                          ViewportToolId::Curve, ViewportToolId::Extrude, ViewportToolId::UvPeel}) {
    ASSERT_NE(tools.tool_action(tool), nullptr);
    EXPECT_FALSE(tools.tool_action(tool)->isVisible());
  }
  for (const auto tool : {ViewportToolId::Select, ViewportToolId::Move,
                          ViewportToolId::Rotate, ViewportToolId::Scale}) {
    ASSERT_NE(tools.tool_action(tool), nullptr);
    EXPECT_TRUE(tools.tool_action(tool)->isVisible());
  }
  for (const auto *name : {"menu_mesh", "menu_vertex", "menu_edge", "menu_face", "menu_uv"}) {
    auto *geometry = bar.findChild<QMenu *>(QString::fromLatin1(name));
    ASSERT_NE(geometry, nullptr);
    EXPECT_FALSE(geometry->menuAction()->isVisible());
  }
  EXPECT_FALSE(create->isVisible());
  EXPECT_TRUE(light->isVisible());
  EXPECT_TRUE(editor.commands().workspace_allows(quader_command::kSceneCreatePointLight.string()));
  EXPECT_FALSE(editor.commands().workspace_allows(quader_command::kToolsActivateCreatePrimitive.string()));
  EXPECT_FALSE(editor.commands().execute(quader_command::kToolsActivateCreatePrimitive).ok());
  auto *face_panel = bar.findChild<QAction *>(QStringLiteral("panel_face_texture"));
  auto *library_panel = bar.findChild<QAction *>(QStringLiteral("panel_commands_library"));
  ASSERT_NE(face_panel, nullptr);
  ASSERT_NE(library_panel, nullptr);
  EXPECT_FALSE(face_panel->isVisible());
  EXPECT_FALSE(library_panel->isVisible());

  ASSERT_TRUE(editor.mode().set_workspace_mode(QuaderWorkspaceMode::Modeling).ok());
  selection.synchronize(editor, QuaderSelectionMode::Object);
  tools.synchronize(editor, editor.selection().summary());
  menu.refresh();
  EXPECT_EQ(tools.actions(), retained_actions);
  EXPECT_EQ(bar.findChild<QMenu *>(QStringLiteral("menu_mesh")), retained_menu);
  EXPECT_TRUE(create->isVisible());
  EXPECT_TRUE(mesh_menu->menuAction()->isVisible());
  EXPECT_TRUE(selection.findChild<QAction *>(QStringLiteral("selectionModeVertex"))->isVisible());
  EXPECT_TRUE(face_panel->isVisible());
  EXPECT_TRUE(library_panel->isVisible());
}

TEST_F(WorkspaceModePresentationTests, SuppressedDockPreservesRequestedVisibilityAndOwner) {
  QMainWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen);
  auto *dock = new QDockWidget(&window);
  auto *content = new QWidget(dock);
  dock->setWidget(content);
  window.addDockWidget(Qt::RightDockWidgetArea, dock);
  UIPanelManager panels;
  panels.add(UIPanelId::FaceTexture, *dock);
  panels.set_visible(UIPanelId::FaceTexture, true);
  QPointer<QWidget> retained = content;
  panels.set_suppressed(UIPanelId::FaceTexture, true);
  EXPECT_TRUE(dock->isHidden());
  EXPECT_TRUE(panels.is_visible(UIPanelId::FaceTexture));
  EXPECT_FALSE(dock->toggleViewAction()->isVisible());
  UISettings saved;
  panels.write_settings(saved);
  EXPECT_TRUE(saved.is_panel_visible(UIPanelId::FaceTexture));
  panels.set_suppressed(UIPanelId::FaceTexture, false);
  EXPECT_FALSE(dock->isHidden());
  EXPECT_EQ(dock->widget(), retained.data());
  panels.set_suppressed(UIPanelId::FaceTexture, true);
  panels.set_visible(UIPanelId::FaceTexture, false);
  panels.set_suppressed(UIPanelId::FaceTexture, false);
  EXPECT_TRUE(dock->isHidden());
  EXPECT_FALSE(panels.is_visible(UIPanelId::FaceTexture));
}

TEST_F(WorkspaceModePresentationTests, InspectorExtensionReparentsWithoutTakingDeletionOwnership) {
  InspectorPanel inspector;
  auto first = std::make_unique<QWidget>();
  auto second = std::make_unique<QWidget>();
  QPointer<QWidget> retained = first.get();
  inspector.set_component_extension(first.get());
  EXPECT_EQ(first->parentWidget(), &inspector);
  inspector.set_component_extension(second.get());
  EXPECT_NE(retained, nullptr);
  EXPECT_EQ(first->parentWidget(), nullptr);
  EXPECT_TRUE(first->isHidden());
  EXPECT_EQ(second->parentWidget(), &inspector);
  second.reset();
  inspector.set_component_extension(nullptr);
  EXPECT_NE(retained, nullptr);
}

TEST_F(WorkspaceModePresentationTests, ModelingToolbarPreferencesSurviveSuppressedWindowState) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto file = std::filesystem::path(temporary.path().toStdWString()) / "ui.ini";
  UISettings settings;
  EXPECT_FALSE(settings.modeling_tools_toolbar_visible().has_value());
  settings.set_modeling_toolbar_visibility(true, false);
  settings.set_panel_visible(UIPanelId::FaceTexture, true);
  ASSERT_TRUE(settings.save(file));
  const auto restored = UISettings::load(file);
  EXPECT_EQ(restored.modeling_tools_toolbar_visible(), true);
  EXPECT_EQ(restored.modeling_texture_toolbar_visible(), false);
  EXPECT_TRUE(restored.is_panel_visible(UIPanelId::FaceTexture));
}

} // namespace
} // namespace quader
