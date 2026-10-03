////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_marking_menu.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "ui/qt_marking_menu.hpp"
#include "ui/qt_modeling_operation_catalog.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_viewport_context_menu.hpp"
#include "viewport/viewport_input_router.hpp"

#include <QAction>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace quader {
namespace {

struct BoundMarkingCommand final {
  QAction *action = nullptr;
  std::string command_id;
};

// The palette and RMB retain compatibility IDs for the same merge operations.
// Deduplicate their presentation without changing the command a direction runs.
std::string_view context_command_identity(std::string_view command_id) {
  if (command_id == quader_command::kModelingMergeVerticesToActive.string()) {
    return quader_command::kModelingMergeVertices.string();
  }
  if (command_id == quader_command::kModelingMergeVerticesToCenter.string()) {
    return quader_command::kModelingMergeVerticesAtCenter.string();
  }
  return command_id;
}

bool same_context_revision(const QuaderRevisionStamp &left,
                           const QuaderRevisionStamp &right) {
  return left.project_revision == right.project_revision &&
         left.scene_revision == right.scene_revision &&
         left.scene_layer_revision == right.scene_layer_revision &&
         left.scene_settings_revision == right.scene_settings_revision &&
         left.asset_revision == right.asset_revision &&
         left.modeling_content_revision == right.modeling_content_revision &&
         left.modeling_selection_revision == right.modeling_selection_revision;
}

} // namespace

bool QtViewportMarkingMenuPresenter::show(
    QWidget &focus_parent, const QPoint &global_point,
    ViewportPaneId initiating_pane, const QuaderInputChord &initiating_chord,
    QuaderEditorApi &editor, ViewportInputRouter &input_router,
    LocalizationService &localization, const QtUiScaleMetrics &metrics,
    ViewportRuntimeBindingToken runtime_binding,
    std::optional<std::uint64_t> initiating_key_identity) const {
  QDR_PROFILE_SCOPE("Qt viewport marking menu");
  if (!runtime_binding.current() || initiating_chord.empty() ||
      editor.viewports().focused_pane() != initiating_pane) {
    return false;
  }

  const QPointer<QWidget> guarded_parent(&focus_parent);
  const auto presented_session = editor.scene_documents().active_scene_id();
  const QuaderRevisionStamp presented_context = editor.revisions();
  const auto presented_layout = editor.viewports().layout();
  const auto presented_tool_revision = editor.tools().active_tool_revision();
  const auto selection = editor.selection().summary();
  const auto presented_mode = selection.mode;

  // A nested popup can destroy its originating pane. Keep the popup stack-owned
  // without a QObject parent, then validate the pane and owner generation again.
  QtMarkingMenu menu;
  menu.setObjectName(QStringLiteral("viewport_marking_menu"));
  menu.apply_ui_scale(metrics);
  QObject::connect(&focus_parent, &QObject::destroyed, &menu, &QWidget::close);
  std::array<QAction *, 8> slot_actions{};
  std::vector<QAction *> overflow_actions;
  std::vector<BoundMarkingCommand> commands;
  std::unordered_set<std::string> presented_commands;
  for (const QtModelingOperationSpec &spec : qt_modeling_operation_specs()) {
    if (spec.mode != presented_mode || spec.marking_slot < 0 ||
        spec.marking_slot >= 8 || !release_presents_command(spec.command_id)) {
      continue;
    }
    const auto descriptor = editor.commands().descriptor(spec.command_id);
    if (!descriptor.has_value()) {
      continue;
    }
    auto *action = new QAction(
        qt_modeling_operation_label(localization, spec.command_id, spec.fallback),
        &menu);
    action->setObjectName(QString::fromUtf8(spec.command_id.string().data(),
                                          spec.command_id.string().size()));
    action->setEnabled(descriptor->enabled);
    commands.push_back({action, std::string(spec.command_id.string())});
    presented_commands.emplace(context_command_identity(spec.command_id.string()));
    slot_actions[static_cast<std::size_t>(spec.marking_slot)] = action;
  }
  std::optional<ViewportContextMenuActionSection> previous_section;
  for (const auto &entry :
       qt_viewport_context_commands(editor, selection, localization)) {
    if (!release_presents_command(QuaderCommandId{entry.command_id}) ||
        !presented_commands.emplace(context_command_identity(entry.command_id)).second) {
      continue;
    }
    if (previous_section.has_value() && *previous_section != entry.section) {
      auto *separator = new QAction(&menu);
      separator->setSeparator(true);
      overflow_actions.push_back(separator);
    }
    previous_section = entry.section;
    auto *action = new QAction(entry.label, &menu);
    if (entry.command_id == quader_command::kToolsActivateAlign.string()) {
      action->setIcon(qt_multiresolution_icon(
          QStringLiteral(":/quader/icons/tool_align.png")));
    }
    action->setObjectName(QString::fromStdString(entry.command_id));
    commands.push_back({action, entry.command_id});
    overflow_actions.push_back(action);
  }
  const QString title = QString::fromStdString(localization.text(
      "ui.viewport.marking_menu.title", "Modeling"));
  QAction *selected_action = menu.exec(global_point, initiating_chord, slot_actions,
                                       overflow_actions, title,
                                       initiating_key_identity);

  if (guarded_parent == nullptr || !runtime_binding.current() ||
      editor.scene_documents().active_scene_id() != presented_session ||
      !same_context_revision(presented_context, editor.revisions()) ||
      editor.viewports().layout() != presented_layout ||
      editor.viewports().focused_pane() != initiating_pane ||
      editor.tools().active_tool_revision() != presented_tool_revision ||
      editor.selection().summary().mode != presented_mode) {
    return false;
  }
  guarded_parent->setFocus(Qt::ShortcutFocusReason);
  const auto selected = std::find_if(
      commands.begin(), commands.end(),
      [selected_action](const BoundMarkingCommand &command) {
        return command.action == selected_action;
      });
  if (selected == commands.end()) {
    return false;
  }
  const auto descriptor = editor.commands().descriptor(selected->command_id);
  if (!descriptor.has_value() || !descriptor->enabled) {
    return false;
  }
  const auto result = editor.commands().execute(selected->command_id);
  if (!runtime_binding.current()) {
    return false;
  }
  if (!result.ok()) {
    LOG_ERROR("Marking menu command '{}' failed: {}", selected->command_id,
              result.error_message());
    return false;
  }
  input_router.set_selection_mode(app_selection_mode_to_viewport(
      to_modeling_selection_mode(editor.selection().summary().mode)));
  return result.changed();
}

} // namespace quader
