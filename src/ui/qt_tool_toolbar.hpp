////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_primitive_creation_types.hpp"
#include "api/quader_viewport_input_types.hpp"

#include <QToolBar>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

class QAction;
class QEvent;
class QWidget;

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class QtUiScaleMetrics;

/**
 * Presents mode-independent modeling tools as one movable native Qt toolbar.
 * Tool availability, activation, and active state remain public API concerns.
 */
class QtToolToolbar final : public QToolBar {
public:
  explicit QtToolToolbar(QWidget *parent = nullptr);

  /// Returns the retained source, including tools presented only in pane strips.
  [[nodiscard]] QAction *tool_action(ViewportToolId tool) const;
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void set_localization(const LocalizationService &localization);
  void synchronize(const QuaderEditorApi &editor,
                   const QuaderSelectionSummary &selection,
                   bool force = false);
  void set_tool_requested_callback(
      std::function<void(std::string_view)> callback);
  void set_command_requested_callback(
      std::function<void(QuaderCommandId)> callback);

protected:
  bool event(QEvent *event) override;

private:
  void refresh_curve_tool_icon();
  void refresh_stairs_tool_icon();

  /// Binds one canonical viewport tool identity to its retained Qt action.
  struct ToolAction {
    QAction *action = nullptr;
    ViewportToolId tool = ViewportToolId::Select;
    std::string_view tool_id;
  };

  /// Retains one concrete primitive command without owning creation settings.
  struct PrimitiveAction {
    QAction *action = nullptr;
    QuaderPrimitiveCreationShape shape = QuaderPrimitiveCreationShape::Box;
    QuaderCommandId command_id;
    QString label;
  };

  std::array<ToolAction, 21> tool_actions_{};
  std::array<PrimitiveAction, 5> primitive_actions_{};
  QAction *primitive_action_ = nullptr;
  QAction *curve_action_ = nullptr;
  QAction *stairs_action_ = nullptr;
  const QuaderEditorApi *synchronized_editor_ = nullptr;
  bool synchronization_valid_ = false;
  ViewportToolId synchronized_selected_tool_ = ViewportToolId::Select;
  ViewportPaneId synchronized_pane_ = ViewportPaneId::Perspective;
  std::uint64_t synchronized_control_revision_ = 0;
  std::uint64_t synchronized_runtime_tool_revision_ = 0;
  QuaderSelectionMode synchronized_selection_mode_ =
      QuaderSelectionMode::Object;
  std::uint64_t synchronized_selection_revision_ = 0;
  std::uint64_t synchronized_keybindings_revision_ = 0;
  std::uint64_t synchronized_editor_mode_revision_ = 0;
  std::array<std::uint64_t, 8> synchronized_availability_revisions_{};
  QuaderPrimitiveCreationShape synchronized_primitive_shape_ =
      QuaderPrimitiveCreationShape::Box;
  std::function<void(std::string_view)> tool_requested_;
  std::function<void(QuaderCommandId)> command_requested_;
};

} // namespace quader
