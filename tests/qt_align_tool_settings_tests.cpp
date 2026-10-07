////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_command_ids.hpp"
#include "core/localization_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/transform/align_tool_service.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>

#include <array>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace quader {
namespace {

constexpr float kAlignSettingsEpsilon = 0.0001F;

struct AlignSettingsHarness final {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  std::array<QuaderModelingObjectRef, 3> objects;

  void seed() {
    constexpr std::array<QVec3, 3> kPositions{
        {{25.0F, 3.0F, 1.0F}, {27.0F, 9.0F, 7.0F}, {55.0F, 15.0F, 11.0F}}};
    for (std::size_t index = 0; index < objects.size(); ++index) {
      const auto created = editor.modeling().create_box(
          {.min = {-0.5F, -0.5F, -0.5F}, .max = {0.5F, 0.5F, 0.5F}},
          "Align Settings Box " + std::to_string(index));
      ASSERT_TRUE(created.ok()) << created.error_message();
      objects[index] = created.value();
      ASSERT_TRUE(editor.scene()
                      .set_transform(objects[index].entity,
                                     {.position = kPositions[index]})
                      .ok());
    }
    ASSERT_TRUE(editor.selection()
                    .select_objects({objects[0].entity, objects[1].entity,
                                     objects[2].entity},
                                    objects[1].entity)
                    .ok());
    session.history().clear();
    session.mark_clean();
  }

  std::array<QuaderTransform, 3> transforms() const {
    std::array<QuaderTransform, 3> result;
    for (std::size_t index = 0; index < objects.size(); ++index) {
      const auto entity = editor.query().entity(objects[index].entity);
      EXPECT_TRUE(entity.has_value());
      if (entity) {
        result[index] = entity->transform;
      }
    }
    return result;
  }
};

void expect_align_transforms(
    const AlignSettingsHarness &harness,
    const std::array<QuaderTransform, 3> &expected) {
  const auto actual = harness.transforms();
  for (std::size_t index = 0; index < actual.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_FLOAT_EQ(actual[index].position.x, expected[index].position.x);
    EXPECT_FLOAT_EQ(actual[index].position.y, expected[index].position.y);
    EXPECT_FLOAT_EQ(actual[index].position.z, expected[index].position.z);
    EXPECT_FLOAT_EQ(actual[index].rotation_euler.x, expected[index].rotation_euler.x);
    EXPECT_FLOAT_EQ(actual[index].rotation_euler.y, expected[index].rotation_euler.y);
    EXPECT_FLOAT_EQ(actual[index].rotation_euler.z, expected[index].rotation_euler.z);
    EXPECT_FLOAT_EQ(actual[index].scale.x, expected[index].scale.x);
    EXPECT_FLOAT_EQ(actual[index].scale.y, expected[index].scale.y);
    EXPECT_FLOAT_EQ(actual[index].scale.z, expected[index].scale.z);
  }
}

const QuaderToolSettingControl *align_setting(
    const QuaderToolSettingsTarget &target, std::string_view id) {
  const auto found = std::ranges::find_if(
      target.controls, [id](const auto &control) { return control.id == id; });
  return found == target.controls.end() ? nullptr : &*found;
}

class QtAlignToolSettingsTests : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_.data());
    QApplication::setStyle(QStringLiteral("Fusion"));
    harness_ = std::make_unique<AlignSettingsHarness>();
    ASSERT_NO_FATAL_FAILURE(harness_->seed());
    panel_ = std::make_unique<QtToolSettingsPanel>();
    panel_->setAttribute(Qt::WA_DontShowOnScreen, true);
  }

  void activate() {
    const auto activated = harness_->editor.tools().activate(ViewportToolId::Align);
    ASSERT_TRUE(activated.ok()) << activated.error_message();
    panel_->bind(harness_->editor);
    panel_->show();
    refresh();
    ASSERT_EQ(panel_->target_id(), "align");
  }

  void refresh() {
    QCoreApplication::processEvents();
    panel_->refresh();
    QCoreApplication::processEvents();
  }

  QPushButton *action_button(std::string_view axis, std::string_view option) const {
    return panel_->findChild<QPushButton *>(
        QStringLiteral("tool_settings.control.align.%1.option.%2")
            .arg(QString::fromUtf8(axis.data(), static_cast<qsizetype>(axis.size())),
                 QString::fromUtf8(option.data(),
                                   static_cast<qsizetype>(option.size()))));
  }

  QCheckBox *toggle(std::string_view id) const {
    return panel_->findChild<QCheckBox *>(
        QStringLiteral("tool_settings.control.align.%1")
            .arg(QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()))));
  }

  void expect_plain_actions(std::string_view axis,
                        std::initializer_list<std::string_view> options) const {
    for (const auto option : options) {
      SCOPED_TRACE(option);
      const auto *button = action_button(axis, option);
      ASSERT_NE(button, nullptr);
      EXPECT_FALSE(button->isCheckable());
      EXPECT_FALSE(button->isChecked());
    }
  }

  int argc_ = 1;
  char executable_[28] = "quader-align-settings-tests";
  std::array<char *, 2> argv_{{executable_, nullptr}};
  std::unique_ptr<QApplication> application_;
  std::unique_ptr<AlignSettingsHarness> harness_;
  std::unique_ptr<QtToolSettingsPanel> panel_;
};

TEST_F(QtAlignToolSettingsTests,
       PlainActionRowsApplyImmediatelyAndKeepRetainedWidgets) {
  const auto baseline = harness_->transforms();
  ASSERT_NO_FATAL_FAILURE(activate());
  const auto target = harness_->editor.tools().active_settings();
  EXPECT_FALSE(target.preview_active);
  EXPECT_FALSE(target.apply_visible);
  EXPECT_FALSE(target.can_apply);
  EXPECT_FALSE(target.can_cancel);
  ASSERT_EQ(target.controls.size(), 5U);
  for (const auto axis : {"x", "y", "z", "distribute"}) {
    const auto *control = align_setting(target, std::string("align.") + axis);
    ASSERT_NE(control, nullptr);
    EXPECT_EQ(control->kind, QuaderToolSettingControlKind::Action);
    EXPECT_EQ(control->choices.size(), 3U);
  }
  expect_plain_actions("x", {"min", "center", "max"});
  expect_plain_actions("y", {"min", "center", "max"});
  expect_plain_actions("z", {"min", "center", "max"});
  expect_plain_actions("distribute", {"x", "y", "z"});
  EXPECT_EQ(toggle("center_all"), nullptr);
  auto *apply = panel_->findChild<QPushButton *>(
      QStringLiteral("tool_settings.apply"));
  auto *cancel = panel_->findChild<QPushButton *>(
      QStringLiteral("tool_settings.cancel"));
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(cancel, nullptr);
  EXPECT_TRUE(apply->isHidden());
  EXPECT_TRUE(cancel->isHidden());
  QPointer<QPushButton> minimum = action_button("x", "min");
  QPointer<QPushButton> maximum_y = action_button("y", "max");
  QPointer<QCheckBox> snapping = toggle("use_snapping");
  ASSERT_NE(minimum, nullptr);
  ASSERT_NE(maximum_y, nullptr);
  ASSERT_NE(snapping, nullptr);
  EXPECT_FALSE(snapping->isChecked());
  EXPECT_EQ(harness_->session.history().undo_count(), 0U);
  EXPECT_FALSE(harness_->session.dirty());

  minimum->click();
  refresh();
  auto expected = baseline;
  for (auto &transform : expected) {
    transform.position.x = 25.0F;
  }
  expect_align_transforms(*harness_, expected);
  EXPECT_EQ(harness_->session.history().undo_count(), 1U);
  EXPECT_TRUE(harness_->session.dirty());
  EXPECT_TRUE(harness_->session.align_tool().active());
  EXPECT_FALSE(harness_->editor.tools().active_settings().preview_active);
  maximum_y->click();
  refresh();
  for (auto &transform : expected) {
    transform.position.y = 15.0F;
  }
  expect_align_transforms(*harness_, expected);
  EXPECT_EQ(harness_->session.history().undo_count(), 2U);
  EXPECT_TRUE(harness_->session.align_tool().active());
  EXPECT_EQ(minimum, action_button("x", "min"));
  EXPECT_EQ(maximum_y, action_button("y", "max"));
  EXPECT_EQ(snapping, toggle("use_snapping"));
  expect_plain_actions("x", {"min", "center", "max"});
  expect_plain_actions("y", {"min", "center", "max"});

  const auto settings_revision = harness_->editor.tools().active_settings_revision();
  const auto scene_revision = harness_->editor.revisions().scene_revision;
  auto *label = panel_->findChild<QLabel *>(
      QStringLiteral("tool_settings.control.align.x.label"));
  ASSERT_NE(label, nullptr);
  label->setText(QStringLiteral("Retained action label"));
  int presentations = 0;
  panel_->presentation_changed = [&] { ++presentations; };
  for (int iteration = 0; iteration < 16; ++iteration) {
    panel_->refresh();
  }
  EXPECT_EQ(label->text(), QStringLiteral("Retained action label"));
  EXPECT_EQ(presentations, 0);
  EXPECT_EQ(harness_->editor.tools().active_settings_revision(), settings_revision);
  EXPECT_EQ(harness_->editor.revisions().scene_revision, scene_revision);
}

TEST_F(QtAlignToolSettingsTests,
       UndoThenAnotherAxisUsesCurrentTransformsWithoutReplayingOldAction) {
  const auto baseline = harness_->transforms();
  ASSERT_NO_FATAL_FAILURE(activate());
  QPointer<QPushButton> minimum_x = action_button("x", "min");
  QPointer<QPushButton> maximum_y = action_button("y", "max");
  ASSERT_NE(minimum_x, nullptr);
  ASSERT_NE(maximum_y, nullptr);
  minimum_x->click();
  refresh();
  ASSERT_EQ(harness_->session.history().undo_count(), 1U);
  ASSERT_TRUE(harness_->editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(harness_->editor.commands()
                  .set_shortcut(quader_command::kEditUndo, "Ctrl+Z")
                  .ok());
  {
    QtCommandEventFilter command_filter(harness_->editor, [] {
      return ViewportToolState{.active_tool = ViewportToolId::Align,
                               .selection_mode = ViewportSelectionMode::Object};
    });
    application_->installEventFilter(&command_filter);
    panel_->activateWindow();
    QCoreApplication::processEvents();
    QApplication::setActiveWindow(panel_.get());
    minimum_x->setFocus(Qt::OtherFocusReason);
    ASSERT_EQ(QApplication::focusWidget(), minimum_x.data());
    QKeyEvent shortcut_override(QEvent::ShortcutOverride, Qt::Key_Z,
                                Qt::ControlModifier);
    shortcut_override.ignore();
    QCoreApplication::sendEvent(minimum_x.data(), &shortcut_override);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier,
                    QStringLiteral("z"));
    press.ignore();
    QCoreApplication::sendEvent(minimum_x.data(), &press);
    EXPECT_TRUE(press.isAccepted());
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Z, Qt::ControlModifier);
    QCoreApplication::sendEvent(minimum_x.data(), &release);
    application_->removeEventFilter(&command_filter);
  }
  refresh();
  expect_align_transforms(*harness_, baseline);
  EXPECT_TRUE(harness_->session.align_tool().active());
  EXPECT_TRUE(harness_->editor.history().can_redo());
  EXPECT_EQ(maximum_y, action_button("y", "max"));
  maximum_y->click();
  refresh();
  auto expected = baseline;
  for (auto &transform : expected) {
    transform.position.y = 15.0F;
  }
  expect_align_transforms(*harness_, expected);
  EXPECT_EQ(harness_->session.history().undo_count(), 1U);
  EXPECT_FALSE(harness_->editor.history().can_redo());
  EXPECT_TRUE(harness_->session.align_tool().active());
  ASSERT_TRUE(harness_->editor.commands().execute(quader_command::kEditUndo).ok());
  refresh();
  expect_align_transforms(*harness_, baseline);
  auto *distribute_x = action_button("distribute", "x");
  ASSERT_NE(distribute_x, nullptr);
  distribute_x->click();
  refresh();
  expected = baseline;
  expected[1].position.x = 40.0F;
  expect_align_transforms(*harness_, expected);
  EXPECT_EQ(harness_->session.history().undo_count(), 1U);
  EXPECT_TRUE(harness_->session.align_tool().active());
  EXPECT_FALSE(distribute_x->isCheckable());
  ASSERT_TRUE(harness_->editor.commands().execute(quader_command::kEditUndo).ok());
  expect_align_transforms(*harness_, baseline);
  ASSERT_TRUE(harness_->editor.commands().execute(quader_command::kEditRedo).ok());
  expect_align_transforms(*harness_, expected);
}

TEST_F(QtAlignToolSettingsTests,
       SnappingPersistsForFutureClicksWithoutMutatingAndReportsFallbackActions) {
  constexpr std::array<float, 3> kPositions{0.0F, 1.0F, 7.1F};
  for (std::size_t index = 0; index < kPositions.size(); ++index) {
    ASSERT_TRUE(harness_->editor.scene()
                    .set_transform(harness_->objects[index].entity,
                                   {.position = {kPositions[index], 0.0F, 0.0F}})
                    .ok());
  }
  harness_->session.history().clear();
  harness_->session.mark_clean();
  const auto baseline = harness_->transforms();
  std::vector<QuaderNotification> notifications;
  harness_->editor.ui().set_runtime_hooks({.notify = [&](QuaderNotification value) {
    notifications.push_back(std::move(value));
    return QuaderApiVoidResult{.ok = true};
  }});
  ASSERT_NO_FATAL_FAILURE(activate());
  auto *snapping = toggle("use_snapping");
  ASSERT_NE(snapping, nullptr);
  EXPECT_FALSE(snapping->isChecked());
  snapping->click();
  refresh();
  EXPECT_TRUE(snapping->isChecked());
  expect_align_transforms(*harness_, baseline);
  EXPECT_EQ(harness_->session.history().undo_count(), 0U);
  EXPECT_FALSE(harness_->session.dirty());
  ASSERT_TRUE(harness_->editor.tools().activate(ViewportToolId::Select).ok());
  expect_align_transforms(*harness_, baseline);
  ASSERT_TRUE(harness_->editor.tools().activate(ViewportToolId::Align).ok());
  refresh();
  snapping = toggle("use_snapping");
  ASSERT_NE(snapping, nullptr);
  EXPECT_TRUE(snapping->isChecked());
  expect_align_transforms(*harness_, baseline);
  EXPECT_EQ(harness_->session.history().undo_count(), 0U);
  ASSERT_TRUE(harness_->editor.viewports().set_grid_preset(10).ok());
  auto *distribute_x = action_button("distribute", "x");
  ASSERT_NE(distribute_x, nullptr);
  distribute_x->click();
  refresh();
  const auto distributed = harness_->transforms();
  EXPECT_NEAR(distributed[1].position.x, 3.55F, kAlignSettingsEpsilon);
  EXPECT_NEAR(distributed[1].position.x - distributed[0].position.x - 1.0F,
              distributed[2].position.x - distributed[1].position.x - 1.0F,
              kAlignSettingsEpsilon);
  EXPECT_EQ(harness_->session.history().undo_count(), 1U);
  EXPECT_TRUE(harness_->session.align_tool().active());
  EXPECT_TRUE(harness_->session.align_tool().grid_alignment_unavailable());
  const std::string warning(harness_->session.align_tool().warning_message());
  ASSERT_FALSE(warning.empty());
  ASSERT_FALSE(notifications.empty());
  EXPECT_EQ(notifications.back().level, QuaderNotificationLevel::Warning);
  EXPECT_EQ(notifications.back().message, warning);
  snapping->click();
  refresh();
  EXPECT_FALSE(snapping->isChecked());
  expect_align_transforms(*harness_, distributed);
  EXPECT_EQ(harness_->session.history().undo_count(), 1U);
  auto *minimum_x = action_button("x", "min");
  ASSERT_NE(minimum_x, nullptr);
  minimum_x->click();
  refresh();
  auto expected = distributed;
  for (auto &transform : expected) {
    transform.position.x = 0.0F;
  }
  expect_align_transforms(*harness_, expected);
  EXPECT_EQ(harness_->session.history().undo_count(), 2U);
  EXPECT_TRUE(harness_->session.align_tool().active());
  EXPECT_FALSE(harness_->session.align_tool().grid_alignment_unavailable());
  EXPECT_EQ(notifications.back().level, QuaderNotificationLevel::Info);
  EXPECT_TRUE(notifications.back().message.empty());
}

TEST_F(QtAlignToolSettingsTests,
       UnsupportedSelectionModesClearRetainedControlsWithoutRevertingActions) {
  ASSERT_NO_FATAL_FAILURE(activate());
  auto *snapping = toggle("use_snapping");
  ASSERT_NE(snapping, nullptr);
  snapping->click();
  auto *minimum = action_button("x", "min");
  ASSERT_NE(minimum, nullptr);
  minimum->click();
  refresh();
  const auto committed = harness_->transforms();
  ASSERT_EQ(harness_->session.history().undo_count(), 1U);

  for (const auto mode : {QuaderSelectionMode::Face, QuaderSelectionMode::Edge,
                          QuaderSelectionMode::Vertex}) {
    SCOPED_TRACE(static_cast<int>(mode));
    QPointer<QPushButton> outgoing = action_button("x", "min");
    ASSERT_NE(outgoing, nullptr);
    ASSERT_TRUE(harness_->editor.mode().set(quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(harness_->editor.selection().set_mode(mode).ok());
    const auto history_count = harness_->session.history().undo_count();
    const auto acquired = harness_->editor.tools().acquire_pane_tool(
        harness_->editor.viewports().focused_pane());
    EXPECT_TRUE(acquired.ok()) << acquired.error_message();
    EXPECT_FALSE(acquired.changed());
    EXPECT_EQ(harness_->editor.viewports().selected_tool(), ViewportToolId::Select);
    EXPECT_FALSE(harness_->session.align_tool().active());
    EXPECT_TRUE(harness_->editor.tools().active_settings().tool_id.empty());
    EXPECT_FALSE(harness_->editor.tools()
                     .execute_action("align", "align.y.max")
                     .ok());
    refresh();
    EXPECT_TRUE(panel_->target_id().empty());
    EXPECT_TRUE(outgoing.isNull());
    EXPECT_EQ(action_button("x", "min"), nullptr);
    EXPECT_EQ(toggle("use_snapping"), nullptr);
    expect_align_transforms(*harness_, committed);
    EXPECT_EQ(harness_->session.history().undo_count(), history_count);
    EXPECT_TRUE(harness_->session.align_tool().settings().use_snapping);

    const auto availability_queries =
        harness_->session.align_tool().work_counters().availability_query_count;
    const auto context_validations =
        harness_->session.align_tool().work_counters().context_validation_count;
    for (int frame = 0; frame < 8; ++frame) {
      refresh();
      EXPECT_TRUE(panel_->target_id().empty());
      EXPECT_FALSE(harness_->session.align_tool().active());
    }
    EXPECT_EQ(harness_->session.align_tool().work_counters().availability_query_count,
              availability_queries);
    EXPECT_EQ(harness_->session.align_tool().work_counters().context_validation_count,
              context_validations);
    ASSERT_TRUE(harness_->editor.mode().set(quader::QuaderEditorMode::Object).ok());
    refresh();
    EXPECT_TRUE(panel_->target_id().empty());
    EXPECT_FALSE(harness_->session.align_tool().active());
    const auto reacquired = harness_->editor.tools().acquire_pane_tool(
        harness_->editor.viewports().focused_pane());
    EXPECT_TRUE(reacquired.ok()) << reacquired.error_message();
    EXPECT_FALSE(reacquired.changed());
    EXPECT_FALSE(harness_->session.align_tool().active());
    ASSERT_TRUE(harness_->editor.tools().activate(ViewportToolId::Align).ok());
    refresh();
    EXPECT_EQ(panel_->target_id(), "align");
    ASSERT_NE(toggle("use_snapping"), nullptr);
    EXPECT_TRUE(toggle("use_snapping")->isChecked());
    expect_align_transforms(*harness_, committed);
  }
}

TEST_F(QtAlignToolSettingsTests,
       InvalidSelectionAndLockedTargetsClearThePanelUntilExplicitActivation) {
  ASSERT_NO_FATAL_FAILURE(activate());
  auto *minimum = action_button("x", "min");
  ASSERT_NE(minimum, nullptr);
  minimum->click();
  refresh();
  const auto committed = harness_->transforms();
  QPointer<QPushButton> outgoing = action_button("x", "min");
  ASSERT_TRUE(harness_->editor.selection()
                  .select_object(harness_->objects[0])
                  .ok());
  const auto selection_history = harness_->session.history().undo_count();
  refresh();
  EXPECT_TRUE(panel_->target_id().empty());
  EXPECT_TRUE(outgoing.isNull());
  EXPECT_FALSE(harness_->session.align_tool().active());
  expect_align_transforms(*harness_, committed);
  EXPECT_EQ(harness_->session.history().undo_count(), selection_history);
  ASSERT_TRUE(harness_->editor.selection()
                  .select_objects({harness_->objects[0].entity,
                                   harness_->objects[1].entity,
                                   harness_->objects[2].entity},
                                  harness_->objects[1].entity)
                  .ok());
  refresh();
  EXPECT_TRUE(panel_->target_id().empty());
  EXPECT_FALSE(harness_->session.align_tool().active());
  ASSERT_TRUE(harness_->editor.tools().activate(ViewportToolId::Align).ok());
  refresh();
  ASSERT_EQ(panel_->target_id(), "align");
  outgoing = action_button("y", "max");
  ASSERT_NE(outgoing, nullptr);
  ASSERT_TRUE(harness_->session.scene().set_locked(harness_->objects[0].entity, true));
  const auto locked_history = harness_->session.history().undo_count();
  refresh();
  EXPECT_TRUE(panel_->target_id().empty());
  EXPECT_TRUE(outgoing.isNull());
  EXPECT_FALSE(harness_->session.align_tool().active());
  expect_align_transforms(*harness_, committed);
  EXPECT_EQ(harness_->session.history().undo_count(), locked_history);
  ASSERT_TRUE(harness_->session.scene().set_locked(harness_->objects[0].entity, false));
  refresh();
  EXPECT_TRUE(panel_->target_id().empty());
  EXPECT_FALSE(harness_->session.align_tool().active());
}

TEST_F(QtAlignToolSettingsTests,
       ActivatingAnotherToolReplacesTheVisiblePanelWithoutUndoingActions) {
  ASSERT_NO_FATAL_FAILURE(activate());
  auto *minimum = action_button("x", "min");
  ASSERT_NE(minimum, nullptr);
  minimum->click();
  refresh();
  const auto committed = harness_->transforms();
  QPointer<QPushButton> outgoing = action_button("x", "min");
  ASSERT_TRUE(harness_->editor.commands()
                  .execute(quader_command::kToolsActivateMove)
                  .ok());
  refresh();
  EXPECT_FALSE(harness_->session.align_tool().active());
  EXPECT_EQ(harness_->editor.tools().active_tool(), "move");
  const auto incoming = harness_->editor.tools().active_settings();
  EXPECT_EQ(incoming.tool_id, "move");
  EXPECT_TRUE(incoming.controls.empty());
  EXPECT_TRUE(panel_->target_id().empty());
  EXPECT_FALSE(panel_->has_settings());
  EXPECT_TRUE(outgoing.isNull());
  EXPECT_EQ(action_button("x", "min"), nullptr);
  EXPECT_EQ(toggle("use_snapping"), nullptr);
  expect_align_transforms(*harness_, committed);
  EXPECT_EQ(harness_->session.history().undo_count(), 1U);
}

TEST_F(QtAlignToolSettingsTests,
       NativeControlsRemainReadableAcrossScalingNarrowWidthAndScaleDown) {
  apply_qt_obsidian_application_theme(QApplication::palette());
  QtUiScaleApplicationController scale_controller(*application_);
  LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "en-US"));
  ASSERT_NO_FATAL_FAILURE(activate());
  panel_->set_localization(localization);
  const auto directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(directory));
  }

  struct CaptureLayout final {
    float scale;
    int width;
    const char *name;
  };
  constexpr std::array<CaptureLayout, 4> kLayouts{{
      {1.0F, 360, "100"}, {2.0F, 360, "200"},
      {1.0F, 260, "100_narrow"}, {1.0F, 360, "scale_down"}}};
  for (const auto locale : {"en-US", "it-IT"}) {
    ASSERT_TRUE(localization.select_locale(locale));
    panel_->set_localization(localization);
    refresh();
    QPointer<QPushButton> retained_minimum = action_button("x", "min");
    QPointer<QCheckBox> retained_snapping = toggle("use_snapping");
    ASSERT_NE(retained_minimum, nullptr);
    ASSERT_NE(retained_snapping, nullptr);
    auto *center_x = action_button("x", "center");
    ASSERT_NE(center_x, nullptr);
    EXPECT_EQ(center_x->text(), std::string_view(locale) == "it-IT"
                                   ? QStringLiteral("Centro")
                                   : QStringLiteral("Center"));
    std::optional<QSize> first_size_hint;
    for (const auto &layout : kLayouts) {
      SCOPED_TRACE(locale);
      SCOPED_TRACE(layout.name);
      scale_controller.apply_factor(layout.scale);
      const auto &metrics = scale_controller.metrics();
      panel_->apply_ui_scale(metrics);
      refresh();
      panel_->resize(std::max(metrics.px(layout.width),
                              panel_->minimumSizeHint().width()),
                     panel_->sizeHint().height());
      refresh();
      auto *header = panel_->findChild<QToolButton *>(
          QStringLiteral("tool_settings.target_title"));
      ASSERT_NE(header, nullptr);
      EXPECT_EQ(header->width(), panel_->width());
      EXPECT_GE(header->height(), metrics.px(26));
      for (const auto axis : {"x", "y", "z", "distribute"}) {
        const auto options = std::string_view(axis) == "distribute"
                                 ? std::array<std::string_view, 3>{"x", "y", "z"}
                                 : std::array<std::string_view, 3>{"min", "center", "max"};
        for (const auto option : options) {
          auto *button = action_button(axis, option);
          ASSERT_NE(button, nullptr);
          EXPECT_TRUE(button->isVisible());
          EXPECT_FALSE(button->isCheckable());
          EXPECT_FALSE(button->isChecked());
          EXPECT_TRUE(button->parentWidget()->rect().contains(button->geometry()));
          EXPECT_TRUE(panel_->rect().contains(
              QRect(button->mapTo(panel_.get(), QPoint{}), button->size())));
          EXPECT_GE(button->width(),
                    button->fontMetrics().horizontalAdvance(button->text()) +
                        metrics.px(8));
          EXPECT_GE(button->height(), button->minimumSizeHint().height());
        }
      }
      EXPECT_EQ(retained_minimum, action_button("x", "min"));
      EXPECT_EQ(retained_snapping, toggle("use_snapping"));
      if (!first_size_hint) {
        first_size_hint = panel_->sizeHint();
      }
      if (std::string_view(layout.name) == "scale_down") {
        EXPECT_EQ(panel_->sizeHint(), *first_size_hint);
      }
      if (!directory.isEmpty()) {
        QImage image(panel_->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        panel_->render(&image);
        ASSERT_TRUE(image.save(QDir(directory).filePath(
            QStringLiteral("align_tool_%1_%2.png")
                .arg(QString::fromUtf8(locale), QString::fromUtf8(layout.name)))));
      }
    }
  }
}

} // namespace
} // namespace quader
