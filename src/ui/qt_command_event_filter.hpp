////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_input_types.hpp"

#include <QObject>

#include <cstdint>
#include <functional>
#include <unordered_set>

class QEvent;
class QWidget;

namespace quader {

class QuaderEditorApi;
struct QuaderViewportKeybindingContext;

using QtViewportToolStateProvider = std::function<ViewportToolState()>;
using QtApplicationCommandInputOwnerProvider = std::function<bool()>;
using QtCommandInputContextProvider =
    std::function<QuaderViewportKeybindingContext()>;

[[nodiscard]] ViewportToolState
qt_command_dispatch_tool_state(const QtViewportToolStateProvider &provider,
                               int grid_preset);

[[nodiscard]] bool qt_application_command_input_owner_active(
    const QtApplicationCommandInputOwnerProvider &provider);

/**
 * Returns whether a retained auxiliary window currently owns keyboard input.
 * The focus-chain check covers platforms and headless tests where activation
 * notification can lag behind QWidget focus delivery.
 */
[[nodiscard]] bool qt_window_owns_command_input(const QWidget *window);

/** Reports hover or focus ownership of the retained scene hierarchy panel. */
[[nodiscard]] bool qt_outliner_owns_command_input(const QWidget *outliner);

/**
 * Routes application-level Qt key presses through Quader's canonical command
 * and keybinding API. Registered editor commands are available from every
 * editor panel; viewport surfaces retain their semantic pointer/router input.
 * Exact global commands execute at the event boundary so their effects precede
 * later window events. Focused Qt controls retain explicit editing/navigation keys,
 * except that document saves settle a focused main-editor draft first.
 */
class QtCommandEventFilter final : public QObject {
public:
  explicit QtCommandEventFilter(QuaderEditorApi &editor,
                                QtViewportToolStateProvider tool_state_provider,
                                QObject *parent = nullptr);

  QtCommandEventFilter(const QtCommandEventFilter &) = delete;
  QtCommandEventFilter &operator=(const QtCommandEventFilter &) = delete;

  void rebind_editor(QuaderEditorApi &editor) noexcept;
  void set_command_input_owner_provider(
      QtApplicationCommandInputOwnerProvider provider);
  void set_outliner_input_owner_provider(
      QtApplicationCommandInputOwnerProvider provider);
  void set_input_context_provider(QtCommandInputContextProvider provider);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  [[nodiscard]] static bool modal_or_popup_owns_input();
  [[nodiscard]] static bool viewport_surface_has_focus(QObject *watched);
  [[nodiscard]] static bool text_input_has_focus();
  [[nodiscard]] static bool is_text_input_widget(const QWidget *widget);
  void reset_pending_chord();

  QuaderEditorApi *editor_ = nullptr;
  QtViewportToolStateProvider tool_state_provider_;
  QtApplicationCommandInputOwnerProvider command_input_owner_provider_;
  QtApplicationCommandInputOwnerProvider outliner_input_owner_provider_;
  QtCommandInputContextProvider input_context_provider_;
  bool text_input_sequence_pending_ = false;
  std::uint64_t binding_generation_ = 0;
  // A consumed settings press retains its release across focus/runtime changes.
  std::unordered_set<int> consumed_settings_keys_;
};

} // namespace quader
