////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_command_ids.hpp"
#include "viewport/viewport_runtime_binding_token.hpp"
#include "editor/modeling/modeling_types.hpp"

#include <array>
#include <memory>
#include <string_view>

class QPoint;
class QWidget;

namespace quader {

class LocalizationService;
class ModelingSessionService;
class QuaderEditorApi;
class QtRetainedMessageBox;
class ViewportInputRouter;

/**
 * Selects the stock Qt surface used to present the router-owned viewport X
 * shortcut without duplicating input or modeling command policy in Qt.
 */
enum class QtViewportDeletePopupKind {
  DirectDeleteCommand,
  ObjectConfirmation,
  ComponentMenu,
};

/**
 * Describes one legacy component-delete choice. The descriptor carries only
 * localized presentation metadata and an existing public command route; it
 * never owns selection or modeling state.
 */
struct QtViewportDeletePopupAction {
  std::string_view id;
  std::string_view label_key;
  std::string_view fallback_label;
  QuaderCommandId command_id;
  bool enabled = false;
};

[[nodiscard]] QtViewportDeletePopupKind qt_viewport_delete_popup_kind(
    const ModelingSelectionSummary &summary, bool has_selected_object_roots);

[[nodiscard]] std::array<QtViewportDeletePopupAction, 5>
qt_viewport_delete_popup_actions(const ModelingSelectionSummary &summary);

/**
 * Presents the router-owned DeletePopup shortcut through stock Qt Widgets.
 * Modeling mutations remain routed through QuaderEditorApi commands.
 */
class QtViewportDeletePopupPresenter final {
public:
  explicit QtViewportDeletePopupPresenter(QWidget &owner);
  ~QtViewportDeletePopupPresenter();

  /// Resolves the hidden object-confirmation window and its retained controls
  /// after the owning viewport window has completed startup presentation.
  void prewarm();

  [[nodiscard]] bool
  show(QWidget &focus_parent, const QPoint &global_point,
       QuaderEditorApi &editor, ModelingSessionService &modeling,
       ViewportInputRouter &input_router,
       LocalizationService &localization,
       ViewportRuntimeBindingToken runtime_binding = {});

private:
  std::unique_ptr<QtRetainedMessageBox> object_confirmation_;
};

} // namespace quader
