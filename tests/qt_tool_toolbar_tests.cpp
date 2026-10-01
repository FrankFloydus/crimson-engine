////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "ui/qt_tool_toolbar.hpp"

#include <QAction>
#include <QApplication>

#include <array>
#include <string>

namespace {

constexpr std::array<quader::ViewportToolId, 13> kModelingTools{
    quader::ViewportToolId::CreatePrimitive,
    quader::ViewportToolId::Pivot,
    quader::ViewportToolId::Extrude,
    quader::ViewportToolId::InsertEdgeLoop,
    quader::ViewportToolId::Knife,
    quader::ViewportToolId::Cut,
    quader::ViewportToolId::Mirror,
    quader::ViewportToolId::Poly,
    quader::ViewportToolId::LinearArray,
    quader::ViewportToolId::RadialArray,
    quader::ViewportToolId::Curve,
    quader::ViewportToolId::PathArray,
    quader::ViewportToolId::UvPeel};

constexpr std::array<qsizetype, 3> kSeparatorPositions{8, 11, 14};

} // namespace

TEST(QtToolToolbarTests, RetainsAllModelingLaunchersAcrossSelectionModes) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_tool_toolbar_membership_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::QtToolToolbar toolbar;
  EXPECT_EQ(toolbar.objectName(), QStringLiteral("toolToolbar"));
  ASSERT_EQ(toolbar.actions().size(),
            kModelingTools.size() + kSeparatorPositions.size());
  for (const auto mode : {quader::QuaderSelectionMode::Vertex,
                          quader::QuaderSelectionMode::Edge,
                          quader::QuaderSelectionMode::Face,
                          quader::QuaderSelectionMode::Object,
                          quader::QuaderSelectionMode::Group}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    toolbar.synchronize(editor, editor.selection().summary());
    const auto actions = toolbar.actions();
    std::size_t tool_index = 0;
    std::size_t separator_index = 0;
    for (qsizetype index = 0; index < actions.size(); ++index) {
      if (separator_index < kSeparatorPositions.size() &&
          index == kSeparatorPositions[separator_index]) {
        EXPECT_TRUE(actions[index]->isSeparator());
        ++separator_index;
        continue;
      }
      ASSERT_LT(tool_index, kModelingTools.size());
      QAction *action = toolbar.tool_action(kModelingTools[tool_index++]);
      ASSERT_NE(action, nullptr);
      EXPECT_EQ(actions[index], action);
      EXPECT_FALSE(action->isSeparator());
      EXPECT_TRUE(action->isVisible());
      EXPECT_TRUE(action->isCheckable());
      EXPECT_TRUE(action->shortcuts().isEmpty());
      EXPECT_NE(toolbar.widgetForAction(action), nullptr);
    }
    EXPECT_EQ(tool_index, kModelingTools.size());
    EXPECT_EQ(separator_index, kSeparatorPositions.size());
  }
  for (const auto tool : {quader::ViewportToolId::Select,
                          quader::ViewportToolId::LassoSelect,
                          quader::ViewportToolId::Move,
                          quader::ViewportToolId::Rotate,
                          quader::ViewportToolId::Scale}) {
    QAction *source = toolbar.tool_action(tool);
    ASSERT_NE(source, nullptr);
    EXPECT_FALSE(toolbar.actions().contains(source));
    EXPECT_EQ(toolbar.widgetForAction(source), nullptr);
  }
  EXPECT_EQ(toolbar.findChild<QAction *>(QStringLiteral("command_edit.delete")),
            nullptr);
}

TEST(QtToolToolbarTests, SelectionChangesRefreshCanonicalToolRequirements) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_tool_toolbar_requirements_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::QtToolToolbar toolbar;
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  toolbar.synchronize(editor, editor.selection().summary());
  QAction *cut = toolbar.tool_action(quader::ViewportToolId::Cut);
  QAction *mirror = toolbar.tool_action(quader::ViewportToolId::Mirror);
  ASSERT_NE(cut, nullptr);
  ASSERT_NE(mirror, nullptr);
  EXPECT_FALSE(cut->isEnabled());
  EXPECT_FALSE(mirror->isEnabled());

  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Toolbar Requirements");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_TRUE(cut->isEnabled());
  EXPECT_TRUE(mirror->isEnabled());
  for (const auto tool : kModelingTools) {
    if (tool == quader::ViewportToolId::CreatePrimitive) {
      continue;
    }
    const auto command_id = editor.commands().command_id_for_tool_activation(
        quader::viewport_tool_catalog_id(tool));
    const auto descriptor = editor.commands().descriptor(command_id);
    ASSERT_TRUE(descriptor.has_value()) << command_id;
    EXPECT_EQ(toolbar.tool_action(tool)->isEnabled(), descriptor->enabled)
        << command_id;
  }
  ASSERT_TRUE(editor.selection().clear().ok());
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_FALSE(cut->isEnabled());
  EXPECT_FALSE(mirror->isEnabled());
}

TEST(QtToolToolbarTests, DispatchesActivationAndRetainsUnchangedPresentation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_tool_toolbar_dispatch_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::QtToolToolbar toolbar;
  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Toolbar Activation");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  int requests = 0;
  toolbar.set_tool_requested_callback([&](std::string_view tool_id) {
    ++requests;
    EXPECT_EQ(tool_id, "cut");
    const auto command_id = editor.commands().command_id_for_tool_activation(tool_id);
    EXPECT_EQ(command_id, quader::quader_command::kToolsActivateCut.string());
    const auto result = editor.commands().execute(command_id);
    EXPECT_TRUE(result.ok()) << result.error_message();
  });
  toolbar.synchronize(editor, editor.selection().summary());
  QAction *cut = toolbar.tool_action(quader::ViewportToolId::Cut);
  ASSERT_NE(cut, nullptr);
  ASSERT_TRUE(cut->isEnabled());
  cut->trigger();
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(editor.viewports().selected_tool(editor.viewports().focused_pane()),
            quader::ViewportToolId::Cut);
  EXPECT_TRUE(cut->isChecked());

  int presentation_changes = 0;
  for (QAction *action : toolbar.findChildren<QAction *>()) {
    QObject::connect(action, &QAction::changed, &toolbar,
                     [&]() { ++presentation_changes; });
  }
  const auto selection = editor.selection().summary();
  const auto before = session.modeling().modeling_sdk_projection_stats_for_tests();
  for (int frame = 0; frame < 10; ++frame) {
    toolbar.synchronize(editor, selection);
  }
  const auto after = session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(presentation_changes, 0);
  EXPECT_EQ(after.selection_summary_build_count, before.selection_summary_build_count);
  EXPECT_EQ(after.selection_topology_build_count, before.selection_topology_build_count);
  EXPECT_EQ(after.mesh_compile_count, before.mesh_compile_count);

  ASSERT_TRUE(editor.commands().execute(
      quader::quader_command::kToolsActivateSelect).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_FALSE(cut->isEnabled());
  cut->trigger();
  EXPECT_EQ(requests, 1);
}
