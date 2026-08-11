////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_unsaved_confirmation_dialog.hpp"

namespace quader {

QtUnsavedConfirmationDialog::QtUnsavedConfirmationDialog(QWidget *parent)
    : QtRetainedMessageBox(parent) {
  (void)set_presentation(QtRetainedMessageBoxPresentation{
      .icon = QMessageBox::Warning,
      .buttons =
          {
              {
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name = QStringLiteral("unsavedConfirmation.save"),
                  .label = QStringLiteral("Save"),
                  .role = QMessageBox::AcceptRole,
              },
              {
                  .button = QtRetainedMessageBoxButton::Secondary,
                  .object_name =
                      QStringLiteral("unsavedConfirmation.discard"),
                  .label = QStringLiteral("Discard"),
                  .role = QMessageBox::DestructiveRole,
              },
              {
                  .button = QtRetainedMessageBoxButton::Tertiary,
                  .object_name = QStringLiteral("unsavedConfirmation.cancel"),
                  .label = QStringLiteral("Cancel"),
                  .role = QMessageBox::RejectRole,
              },
          },
      .default_button = QtRetainedMessageBoxButton::Primary,
      .escape_button = QtRetainedMessageBoxButton::Tertiary,
      .close_button = QtRetainedMessageBoxButton::Tertiary,
  });
}

QtUnsavedConfirmationDecision QtUnsavedConfirmationDialog::present(
    const QtUnsavedConfirmationPresentation &presentation) {
  const QtRetainedMessageBoxButton selected =
      QtRetainedMessageBox::present(QtRetainedMessageBoxPresentation{
          .icon = QMessageBox::Warning,
          .object_name = presentation.object_name,
          .title = presentation.title,
          .text = presentation.message,
          .buttons =
              {
                  {
                      .button = QtRetainedMessageBoxButton::Primary,
                      .object_name =
                          QStringLiteral("unsavedConfirmation.save"),
                      .label = presentation.save_label,
                      .role = QMessageBox::AcceptRole,
                  },
                  {
                      .button = QtRetainedMessageBoxButton::Secondary,
                      .object_name =
                          QStringLiteral("unsavedConfirmation.discard"),
                      .label = presentation.discard_label,
                      .role = QMessageBox::DestructiveRole,
                  },
                  {
                      .button = QtRetainedMessageBoxButton::Tertiary,
                      .object_name =
                          QStringLiteral("unsavedConfirmation.cancel"),
                      .label = presentation.cancel_label,
                      .role = QMessageBox::RejectRole,
                  },
              },
          .default_button = QtRetainedMessageBoxButton::Primary,
          .escape_button = QtRetainedMessageBoxButton::Tertiary,
          .close_button = QtRetainedMessageBoxButton::Tertiary,
      });
  if (selected == QtRetainedMessageBoxButton::Primary) {
    return QtUnsavedConfirmationDecision::Save;
  }
  if (selected == QtRetainedMessageBoxButton::Secondary) {
    return QtUnsavedConfirmationDecision::Discard;
  }
  return QtUnsavedConfirmationDecision::Cancel;
}

} // namespace quader
