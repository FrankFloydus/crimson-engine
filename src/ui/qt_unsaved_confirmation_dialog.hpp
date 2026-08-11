////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_retained_message_box.hpp"

#include <QString>

#include <cstdint>

class QWidget;

namespace quader {

/// Localized content refreshed before each retained confirmation presentation.
struct QtUnsavedConfirmationPresentation final {
  QString object_name;
  QString title;
  QString message;
  QString save_label;
  QString discard_label;
  QString cancel_label;
};

/// Semantic result returned by the reusable dirty-document confirmation.
enum class QtUnsavedConfirmationDecision : std::uint8_t {
  Save,
  Discard,
  Cancel,
};

/**
 * Retains the native QMessageBox, its warning icon, and its buttons across
 * dirty-document prompts. Presentation text remains invocation-owned so
 * locale and document changes never leak between prompts.
 */
class QtUnsavedConfirmationDialog final : public QtRetainedMessageBox {
public:
  explicit QtUnsavedConfirmationDialog(QWidget *parent = nullptr);

  [[nodiscard]] QtUnsavedConfirmationDecision
  present(const QtUnsavedConfirmationPresentation &presentation);

};

} // namespace quader
