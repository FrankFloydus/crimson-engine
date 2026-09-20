////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "core/localization_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/input/quader_input_binding.hpp"
#include "editor/metadata/operation_input_hint_catalog.hpp"
#include "editor/session/project_session.hpp"
#include "support/command_behavior_fixture.hpp"
#include "ui/qt_operation_input_hints.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QStatusBar>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string_view>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

bool contains_command(const std::vector<QuaderOperationInputHint> &hints,
                      QuaderCommandId command) {
  return std::ranges::any_of(hints, [&](const auto &hint) {
    return hint.command.string() == command.string();
  });
}

bool contains_gesture(const std::vector<QuaderOperationInputHint> &hints,
                      QuaderOperationInputGesture gesture) {
  return std::ranges::any_of(hints, [&](const auto &hint) { return hint.gesture == gesture; });
}

bool contains_badge(const QtOperationInputHints &widget, const QString &badge) {
  return std::ranges::any_of(widget.hints(), [&](const auto &hint) { return hint.badge == badge; });
}

LocalizationService localization_for(std::string_view locale = "en-US") {
  LocalizationService localization;
  EXPECT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", std::string(locale)));
  return localization;
}

TEST(OperationInputHintTests, MetadataSelectsOnlyApplicableKeyboardCommands) {
  using Shape = QuaderPrimitiveCreationShape;
  using namespace quader_command;
  for (const auto shape : {Shape::Box, Shape::Cylinder, Shape::Cone, Shape::Sphere, Shape::Torus}) {
    const auto pending = primitive_creation_input_hints(shape, false);
    EXPECT_FALSE(contains_command(pending, kToolsApplyPrimitiveCreation));
    EXPECT_TRUE(contains_command(pending, kToolsCancelPrimitiveCreation));
    EXPECT_TRUE(contains_command(pending, kToolsFlipPrimitiveFaces));
    EXPECT_EQ(contains_command(pending, kToolsTogglePrimitiveCaps),
              shape == Shape::Cylinder || shape == Shape::Cone);
    EXPECT_EQ(contains_command(pending, kToolsTogglePrimitiveHemisphere), shape == Shape::Sphere);
    EXPECT_TRUE(contains_gesture(pending, QuaderOperationInputGesture::PrimaryDrag));
    EXPECT_FALSE(contains_gesture(pending, QuaderOperationInputGesture::Wheel));
    const auto live = primitive_creation_input_hints(shape, true);
    EXPECT_TRUE(contains_command(live, kToolsApplyPrimitiveCreation));
    EXPECT_FALSE(contains_gesture(live, QuaderOperationInputGesture::PrimaryClick));
    EXPECT_FALSE(contains_gesture(live, QuaderOperationInputGesture::Wheel));
    for (const char *locale : {"en-US", "it-IT"}) {
      const auto localization = localization_for(locale);
      for (const auto &hint : live) {
        EXPECT_NE(localization.text(hint.label_key, "missing"), "missing") << hint.label_key;
      }
    }
  }
}

TEST(OperationInputHintTests, LassoHintsDescribeSelectionModifiersAndTrackToolChanges) {
  auto standalone = create_standalone_editor();
  auto &tools = standalone.editor().tools();
  ASSERT_TRUE(tools.activate(ViewportToolId::LassoSelect).ok());
  const auto hints = tools.active_input_hints();
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::PrimaryDrag));
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::ShiftPrimaryDrag));
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::ControlPrimaryDrag));
  for (const char *locale : {"en-US", "it-IT"}) {
    const auto localization = localization_for(locale);
    for (const auto &hint : hints) {
      EXPECT_NE(localization.text(hint.label_key, "missing"), "missing");
    }
  }
  const auto revision = tools.active_input_hints_revision();
  EXPECT_EQ(tools.active_input_hints_revision(), revision);
  ASSERT_TRUE(tools.activate(ViewportToolId::Select).ok());
  EXPECT_GT(tools.active_input_hints_revision(), revision);
  EXPECT_TRUE(contains_gesture(tools.active_input_hints(),
                               QuaderOperationInputGesture::PrimaryClick));
  EXPECT_TRUE(contains_gesture(tools.active_input_hints(),
                               QuaderOperationInputGesture::ShiftPrimaryClick));
  EXPECT_TRUE(contains_gesture(tools.active_input_hints(),
                               QuaderOperationInputGesture::ControlPrimaryClick));
}

TEST(OperationInputHintTests, OwnerRevisionTracksKindSelectionFootprintAndCancel) {
  const auto *contract = test::find_command_behavior_contract(
      quader_command::kToolsTogglePrimitiveCaps.string());
  ASSERT_NE(contract, nullptr);
  auto fixture = test::CommandBehaviorFixture::create(*contract);
  auto &editor = fixture->editor();
  auto &tools = editor.tools();
  const auto initial = tools.active_input_hints_revision();
  EXPECT_EQ(tools.active_input_hints_revision(), initial);
  EXPECT_FALSE(contains_gesture(tools.active_input_hints(), QuaderOperationInputGesture::Wheel));
  ASSERT_TRUE(tools.set_setting("create_primitive", "primitive.type", "sphere").ok());
  EXPECT_GT(tools.active_input_hints_revision(), initial);
  EXPECT_TRUE(contains_command(tools.active_input_hints(), quader_command::kToolsTogglePrimitiveHemisphere));
  const auto changed = tools.active_input_hints_revision();
  fixture->begin_primitive_footprint();
  EXPECT_GT(tools.active_input_hints_revision(), changed);
  EXPECT_FALSE(contains_gesture(tools.active_input_hints(), QuaderOperationInputGesture::Wheel));
  EXPECT_TRUE(contains_command(tools.active_input_hints(), quader_command::kToolsFlipPrimitiveFaces));
  ASSERT_TRUE(tools.cancel_active_tool_preview().ok());
  EXPECT_FALSE(contains_command(tools.active_input_hints(),
                                quader_command::kToolsFlipPrimitiveFaces));
  EXPECT_TRUE(contains_gesture(tools.active_input_hints(),
                               QuaderOperationInputGesture::PrimaryClick));

  auto selected_fixture = test::CommandBehaviorFixture::create(*contract);
  auto &selected_editor = selected_fixture->editor();
  const auto selected_revision = selected_editor.tools().active_input_hints_revision();
  ASSERT_TRUE(selected_editor.selection().clear().ok());
  EXPECT_GT(selected_editor.tools().active_input_hints_revision(), selected_revision);
  EXPECT_FALSE(contains_gesture(selected_editor.tools().active_input_hints(),
                                QuaderOperationInputGesture::PrimaryClick));
}

TEST(OperationInputHintTests, UvPeelRoutesCompletionBeforeAnyGuideIsSelected) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.commands().execute(quader_command::kModelingUvPeel).ok());
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Edge);

  const auto hints = editor.tools().active_input_hints();
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::PrimaryClick));
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::ShiftPrimaryClick));
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::ControlPrimaryClick));
  EXPECT_TRUE(contains_command(hints, QuaderCommandId{"viewport.modal_tool.finalize"}));
  EXPECT_TRUE(contains_command(hints, QuaderCommandId{"viewport.cancel_active_tool"}));

  ViewportToolState state;
  state.active_tool = ViewportToolId::UvPeel;
  state.selection_mode = ViewportSelectionMode::Edge;
  const auto space = quader_input_parse_chord("Space");
  const auto escape = quader_input_parse_chord("Escape");
  ASSERT_TRUE(space.has_value());
  ASSERT_TRUE(escape.has_value());
  const auto finalize = editor.commands().dispatch_viewport_input_binding(
      *space, state, true, false);
  ASSERT_EQ(finalize.kind, QuaderViewportKeybindingDispatch::Kind::ViewportInputAction);
  EXPECT_EQ(finalize.action, ViewportInputActionKind::ModalToolFinalize);
  const auto cancel = editor.commands().dispatch_viewport_input_binding(
      *escape, state, true, false);
  ASSERT_EQ(cancel.kind, QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
  EXPECT_EQ(cancel.shortcut.kind, ViewportShortcutKind::CancelActiveTool);
  EXPECT_EQ(editor.commands().dispatch_viewport_input_binding(
                *space, state, true, false, {.navigation_active = true}).kind,
            QuaderViewportKeybindingDispatch::Kind::NoMatch);
  EXPECT_EQ(editor.commands().dispatch_viewport_input_binding(
                *space, state, true, true).kind,
            QuaderViewportKeybindingDispatch::Kind::NoMatch);

  const auto settings_cancel =
      editor.commands().dispatch_native_tool_settings_input_binding(*escape, state);
  ASSERT_EQ(settings_cancel.kind, QuaderNativeToolSettingsDispatch::Kind::Intent);
  EXPECT_EQ(settings_cancel.intent, QuaderNativeToolSettingsIntent::Cancel);
  ASSERT_TRUE(editor.commands().execute_native_tool_settings_intent(settings_cancel.intent).ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Edge);
  EXPECT_FALSE(contains_command(editor.tools().active_input_hints(),
                                QuaderCommandId{"viewport.cancel_active_tool"}));
}

TEST(OperationInputHintTests, ExtrusionDescribesAdjustmentAndExplicitSnapModifiers) {
  const auto hints = viewport_tool_input_hints("extrude");
  EXPECT_TRUE(contains_command(hints, quader_command::kToolsActivateExtrude));
  EXPECT_TRUE(contains_command(hints, quader_command::kToolsActivateExtrudeIndividual));
  EXPECT_TRUE(contains_command(hints, quader_command::kToolsActivateExtrudeAlongNormals));
  EXPECT_TRUE(contains_command(hints, QuaderCommandId{"viewport.transform.vertex_snap"}));
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::PrimaryDrag));
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::ControlPrimaryDrag));
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::ShiftPrimaryDrag));
}

TEST(OperationInputHintTests, AuditedViewportToolsPublishLocalizedActions) {
  const std::array<std::string_view, 14> tool_ids{
      "select",          "lasso_select", "move",   "rotate", "scale",
      "extrude",         "insert_edge_loop", "knife", "cut", "poly",
      "mirror",          "pivot",        "linear_array", "uv_peel"};
  for (const std::string_view tool_id : tool_ids) {
    SCOPED_TRACE(tool_id);
    const auto hints = viewport_tool_input_hints(tool_id);
    EXPECT_FALSE(hints.empty());
    for (const char *locale : {"en-US", "it-IT"}) {
      const auto localization = localization_for(locale);
      for (const auto &hint : hints) {
        EXPECT_NE(localization.text(hint.label_key, "missing"), "missing")
            << hint.label_key;
      }
    }
  }
  for (const std::string_view tool_id : {"path_array", "curve"}) {
    EXPECT_FALSE(viewport_tool_input_hints(tool_id).empty());
  }
  EXPECT_EQ(viewport_navigation_input_hints().size(), 5u);
}

/// Supplies Qt services without activating or showing a native window.
class QtOperationInputHintTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
  }
  int argc_ = 1;
  char name_[24] = "operation-input-hints";
  char *argv_[2] = {name_, nullptr};
  std::unique_ptr<QApplication> application_;
};

TEST_F(QtOperationInputHintTests, RebindingUnassignmentAndLocaleRefreshWithoutIdleProjection) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  auto localization = localization_for();
  QWidget parent;
  QtOperationInputHints widget(&parent);
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Shift+F")));
  EXPECT_TRUE(contains_badge(widget, QString::fromStdString(
      editor.commands().shortcut_for(quader_command::kToolsCancelPrimitiveCreation))));
  const auto revision = widget.projection_revision();
  const auto *items = widget.hints().data();
  for (int i = 0; i < 20; ++i) {
    widget.refresh(editor.tools(), editor.commands(), localization);
  }
  EXPECT_EQ(widget.projection_revision(), revision);
  EXPECT_EQ(widget.hints().data(), items);
  EXPECT_FALSE(parent.isVisible());

  ASSERT_TRUE(editor.commands().set_shortcut(quader_command::kToolsFlipPrimitiveFaces, "Ctrl+K D").ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Shift+F")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Ctrl+K D")));
  ASSERT_TRUE(editor.commands().remove_shortcut(quader_command::kToolsFlipPrimitiveFaces).ok());
  ASSERT_TRUE(editor.commands().descriptor(quader_command::kToolsFlipPrimitiveFaces));
  EXPECT_TRUE(editor.commands().descriptor(quader_command::kToolsFlipPrimitiveFaces)->shortcut.empty());
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Shift+F")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Ctrl+K D")));
  ASSERT_TRUE(editor.commands().reset_shortcut(quader_command::kToolsFlipPrimitiveFaces).ok());
  ASSERT_TRUE(localization.select_locale("it-IT"));
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Shift+F")));
  EXPECT_TRUE(std::ranges::any_of(widget.hints(), [](const auto &hint) {
    return hint.badge == QStringLiteral("Shift+F") &&
           hint.label == QStringLiteral("Inverti");
  }));
}

TEST_F(QtOperationInputHintTests, UvPeelRetainsSelectionHintsAndTracksCompletionBindings) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::UvPeel).ok());
  auto localization = localization_for();
  QWidget parent;
  QtOperationInputHints widget(&parent);
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("LMB")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Shift + LMB")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Ctrl + LMB")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("MMB")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Space")));
  const auto cancel = QuaderCommandId{"viewport.cancel_active_tool"};
  const auto cancel_badge = QString::fromStdString(editor.commands().shortcut_for(cancel));
  ASSERT_FALSE(cancel_badge.isEmpty());
  EXPECT_TRUE(contains_badge(widget, cancel_badge));
  const auto revision = widget.projection_revision();
  const auto *items = widget.hints().data();
  for (int frame = 0; frame < 20; ++frame) {
    widget.refresh(editor.tools(), editor.commands(), localization);
  }
  EXPECT_EQ(widget.projection_revision(), revision);
  EXPECT_EQ(widget.hints().data(), items);

  ASSERT_TRUE(editor.commands().set_shortcut(cancel, "Ctrl+Alt+F20").ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Ctrl+Alt+F20")));
  EXPECT_FALSE(contains_badge(widget, cancel_badge));
  ASSERT_TRUE(editor.commands().remove_shortcut(cancel).ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Ctrl+Alt+F20")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("LMB")));
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Space")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("LMB")));
}

TEST_F(QtOperationInputHintTests,
       RadialHintsTrackValidCenterBindingsAndLocaleWithoutIdleProjection) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto source = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Radial Hint Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::RadialArray).ok());
  auto localization = localization_for();
  QWidget parent;
  QtOperationInputHints widget(&parent);
  const QuaderCommandId finalize{"viewport.radial_array.finalize"};
  const QuaderCommandId increase{"viewport.radial_array.increase_copies"};
  const QuaderCommandId decrease{"viewport.radial_array.decrease_copies"};
  const QuaderCommandId increase_repeats{"viewport.radial_array.increase_repeats"};
  const QuaderCommandId decrease_repeats{"viewport.radial_array.decrease_repeats"};
  const QuaderCommandId toggle{"viewport.radial_array.toggle_instances"};
  EXPECT_FALSE(contains_command(editor.tools().active_input_hints(), finalize));
  EXPECT_TRUE(contains_gesture(editor.tools().active_input_hints(),
                               QuaderOperationInputGesture::PrimaryClick));
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("LMB")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("MMB")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Shift + MMB")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Alt + MMB")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("RMB")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Shift+WheelUp")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Shift+WheelDown")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Ctrl+WheelUp")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Ctrl+WheelDown")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("I")));
  EXPECT_TRUE(contains_badge(widget, QString::fromStdString(
      editor.commands().shortcut_for("viewport.cancel_active_tool"))));
  const auto pending_revision = editor.tools().active_input_hints_revision();
  ASSERT_TRUE(session.radial_array()
                  .update_center_candidate({}, {0.0f, 1.0f, 0.0f}, 1.0)
                  .ok);
  EXPECT_GT(editor.tools().active_input_hints_revision(), pending_revision);
  EXPECT_TRUE(contains_command(editor.tools().active_input_hints(), finalize));
  EXPECT_TRUE(contains_gesture(editor.tools().active_input_hints(),
                               QuaderOperationInputGesture::PrimaryClick));
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QString::fromStdString(
      editor.commands().shortcut_for(finalize))));
  ASSERT_EQ(widget.hints().size(), 13u);
  const auto full_projection_revision = widget.projection_revision();
  for (const float scale : {1.0f, 1.5f, 2.0f}) {
    widget.apply_ui_scale(QtUiScaleMetrics(scale));
    const int row_height = widget.sizeHint().height();
    for (const int width : {240, 480, 1200}) {
      widget.resize(width, row_height);
      EXPECT_FALSE(widget.hasHeightForWidth());
      EXPECT_EQ(widget.sizeHint().height(), row_height);
      EXPECT_EQ(widget.minimumHeight(), row_height);
      EXPECT_EQ(widget.maximumHeight(), row_height);
    }
  }
  widget.apply_ui_scale(QtUiScaleMetrics(1.0f));
  EXPECT_EQ(widget.projection_revision(), full_projection_revision);
  const auto placing_revision = editor.tools().active_input_hints_revision();
  ASSERT_TRUE(session.radial_array().lock_center().ok);
  EXPECT_GT(editor.tools().active_input_hints_revision(), placing_revision);
  EXPECT_TRUE(session.radial_array().active());
  EXPECT_TRUE(contains_command(editor.tools().active_input_hints(), finalize));
  EXPECT_FALSE(contains_gesture(editor.tools().active_input_hints(),
                                QuaderOperationInputGesture::PrimaryClick));
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("LMB")));
  EXPECT_TRUE(contains_badge(widget, QString::fromStdString(
      editor.commands().shortcut_for(finalize))));
  const auto projection_revision = widget.projection_revision();
  const auto *items = widget.hints().data();
  for (int frame = 0; frame < 20; ++frame) {
    widget.refresh(editor.tools(), editor.commands(), localization);
  }
  EXPECT_EQ(widget.projection_revision(), projection_revision);
  EXPECT_EQ(widget.hints().data(), items);

  const auto rebound_increase =
      editor.commands().set_shortcut(increase, "Ctrl+Alt+WheelUp");
  ASSERT_TRUE(rebound_increase.ok()) << rebound_increase.error_message();
  const auto rebound_toggle =
      editor.commands().set_shortcut(toggle, "Ctrl+K I");
  ASSERT_TRUE(rebound_toggle.ok()) << rebound_toggle.error_message();
  const auto rebound_repeats =
      editor.commands().set_shortcut(increase_repeats, "Shift+Alt+WheelUp");
  ASSERT_TRUE(rebound_repeats.ok()) << rebound_repeats.error_message();
  const auto removed_decrease = editor.commands().remove_shortcut(decrease);
  ASSERT_TRUE(removed_decrease.ok()) << removed_decrease.error_message();
  const auto removed_repeats = editor.commands().remove_shortcut(decrease_repeats);
  ASSERT_TRUE(removed_repeats.ok()) << removed_repeats.error_message();
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Ctrl+Alt+WheelUp")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Ctrl+K I")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Shift+Alt+WheelUp")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Shift+WheelUp")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Shift+WheelDown")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Ctrl+WheelUp")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Ctrl+WheelDown")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("I")));
  for (const char *locale : {"it-IT", "en-US"}) {
    ASSERT_TRUE(localization.select_locale(locale));
    widget.refresh(editor.tools(), editor.commands(), localization);
    for (const auto &hint : editor.tools().active_input_hints()) {
      EXPECT_NE(localization.text(hint.label_key, "missing"), "missing");
    }
    EXPECT_TRUE(std::ranges::any_of(widget.hints(), [&](const auto &hint) {
      return hint.badge == QStringLiteral("Ctrl+K I") &&
             hint.label == QString::fromStdString(localization.text(
                 "operation.hints.radial_array.toggle_instances", "missing"));
    }));
    EXPECT_TRUE(std::ranges::any_of(widget.hints(), [&](const auto &hint) {
      return hint.badge == QStringLiteral("Shift+Alt+WheelUp") &&
             hint.label == QString::fromStdString(localization.text(
                 "operation.hints.radial_array.increase_repeats", "missing"));
    }));
  }
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_FALSE(widget.hints().empty());
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("LMB")));
  EXPECT_FALSE(widget.isHidden());
}

TEST_F(QtOperationInputHintTests,
       SelectionAndNavigationUseConventionalMouseButtonNames) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  auto localization = localization_for();
  QWidget parent;
  QtOperationInputHints widget(&parent);
  widget.refresh(editor.tools(), editor.commands(), localization);

  for (const QString &badge : {QStringLiteral("LMB"),
                               QStringLiteral("Shift + LMB"),
                               QStringLiteral("Ctrl + LMB"),
                               QStringLiteral("MMB"),
                               QStringLiteral("Shift + MMB"),
                               QStringLiteral("Alt + MMB"),
                               QStringLiteral("RMB")}) {
    EXPECT_TRUE(contains_badge(widget, badge)) << badge.toStdString();
  }
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Shift + RMB")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Ctrl + RMB")));
  for (const auto &hint : widget.hints()) {
    EXPECT_FALSE(hint.badge.contains(QStringLiteral("Mouse")));
  }
}

TEST_F(QtOperationInputHintTests, FitsOneStatusBarRowAtNarrowWidthsAndDifferentScales) {
  const auto *contract = test::find_command_behavior_contract(
      quader_command::kToolsTogglePrimitiveHemisphere.string());
  ASSERT_NE(contract, nullptr);
  auto fixture = test::CommandBehaviorFixture::create(*contract);
  auto &editor = fixture->editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  auto localization = localization_for();
  QWidget parent;
  QStatusBar status(&parent);
  auto *widget = new QtOperationInputHints(&status);
  auto *message = new QLabel(QStringLiteral("Existing command status"), &status);
  status.addPermanentWidget(widget, 1);
  status.addPermanentWidget(message, 1);
  message->hide();
  widget->refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(*widget, QStringLiteral("H")));
  EXPECT_FALSE(contains_badge(*widget, QStringLiteral("Ctrl + Click")));
  EXPECT_FALSE(contains_badge(*widget, QStringLiteral("Wheel +/-")));
  const auto finalize = quader_command::kToolsApplyPrimitiveCreation;
  const auto cancel = quader_command::kToolsCancelPrimitiveCreation;
  const QString finalize_badge = QString::fromStdString(editor.commands().shortcut_for(finalize));
  const QString cancel_badge = QString::fromStdString(editor.commands().shortcut_for(cancel));
  ASSERT_FALSE(finalize_badge.isEmpty());
  ASSERT_FALSE(cancel_badge.isEmpty());
  EXPECT_TRUE(contains_badge(*widget, finalize_badge));
  EXPECT_TRUE(contains_badge(*widget, cancel_badge));
  ASSERT_TRUE(editor.commands().set_shortcut(finalize, "Ctrl+Alt+F20").ok());
  ASSERT_TRUE(editor.commands().remove_shortcut(cancel).ok());
  widget->refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(*widget, QStringLiteral("Ctrl+Alt+F20")));
  EXPECT_FALSE(contains_badge(*widget, finalize_badge));
  EXPECT_FALSE(contains_badge(*widget, cancel_badge));
  EXPECT_FALSE(widget->isHidden());
  EXPECT_EQ(widget->parentWidget(), &status);
  const bool message_was_hidden = message->isHidden();
  status.showMessage(QStringLiteral("Temporary status"));
  EXPECT_FALSE(widget->isHidden());
  EXPECT_EQ(message->isHidden(), message_was_hidden);
  EXPECT_EQ(message->text(), QStringLiteral("Existing command status"));
  EXPECT_FALSE(parent.isVisible());
  EXPECT_FALSE(widget->hasHeightForWidth());
  EXPECT_GE(widget->sizeHint().height(), message->sizeHint().height() + 4);
  EXPECT_LE(widget->sizeHint().height(), message->sizeHint().height() + 6);
  const int original_height = widget->sizeHint().height();
  widget->resize(240, original_height);
  EXPECT_EQ(widget->sizeHint().height(), original_height);
  EXPECT_EQ(widget->minimumHeight(), original_height);
  EXPECT_EQ(widget->maximumHeight(), original_height);
  const auto projection = widget->projection_revision();
  widget->apply_ui_scale(QtUiScaleMetrics(2.0f));
  QFont scaled_font = widget->font();
  scaled_font.setPointSizeF(scaled_font.pointSizeF() * 2.0);
  widget->setFont(scaled_font);
  EXPECT_GT(widget->sizeHint().height(), original_height);
  EXPECT_FALSE(widget->hasHeightForWidth());
  EXPECT_EQ(widget->projection_revision(), projection);
  widget->apply_ui_scale(QtUiScaleMetrics(1.0f));
  widget->invalidate();
  EXPECT_TRUE(widget->isHidden());
  EXPECT_TRUE(widget->hints().empty());
  EXPECT_EQ(message->text(), QStringLiteral("Existing command status"));
}

TEST_F(QtOperationInputHintTests, SameFacadeSceneRebindRetiresPreviousHints) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto created = editor.project().create(std::filesystem::path(directory.path().toStdWString()));
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.tools().set_setting("create_primitive", "primitive.type", "sphere").ok());
  auto localization = localization_for();
  QWidget parent;
  QtOperationInputHints widget(&parent);
  auto *const tools = &editor.tools();
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("H")));
  const auto revision = widget.projection_revision();
  ASSERT_TRUE(editor.scene_documents().new_scene().ok());
  EXPECT_EQ(&editor.tools(), tools);
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_GT(widget.projection_revision(), revision);
  EXPECT_FALSE(widget.hints().empty());
  EXPECT_FALSE(widget.isHidden());
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("LMB")));
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Shift+F")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("H")));
}

} // namespace
} // namespace quader
