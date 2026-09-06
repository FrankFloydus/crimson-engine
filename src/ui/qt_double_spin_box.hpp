////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_numeric_format.hpp"

#include <QDoubleSpinBox>
#include <QLineEdit>

namespace quader {

/// Shares compact numeric presentation while retaining each owner's precision.
class QtDoubleSpinBox : public QDoubleSpinBox {
public:
  explicit QtDoubleSpinBox(QWidget *parent = nullptr) : QDoubleSpinBox(parent) {
    setDecimals(4);
    QObject::connect(lineEdit(), &QLineEdit::textEdited, this,
                     [this]() {
                       text_edited_ = true;
                       edit_value_ = value();
                     });
    QObject::connect(this, &QDoubleSpinBox::valueChanged, this,
                     [this]() { text_edited_ = false; });
    QObject::connect(this, &QDoubleSpinBox::editingFinished, this,
                     [this]() { text_edited_ = false; });
  }

protected:
  /// A replacement presentation abandons drafts even when signals are blocked.
  void discard_text_edit() { text_edited_ = false; }

  QString textFromValue(double value) const override {
    QLocale number_locale = locale();
    if (!isGroupSeparatorShown()) {
      number_locale.setNumberOptions(number_locale.numberOptions() |
                                     QLocale::OmitGroupSeparator);
    } else {
      number_locale.setNumberOptions(number_locale.numberOptions() &
                                     ~QLocale::OmitGroupSeparator);
    }
    return qt_format_number(value, number_locale, decimals());
  }

  double valueFromText(const QString &text) const override {
    // Qt may interpret the displayed text on Return or focus loss. Presentation
    // rounding must not commit a new value when the user has not edited it.
    const QString canonical = textFromValue(value());
    // QLineEdit's modified flag is cleared by Qt before interpreting Return.
    if ((!text_edited_ || edit_value_ != value()) &&
        (text == canonical || text == prefix() + canonical + suffix())) {
      return value();
    }
    return QDoubleSpinBox::valueFromText(text);
  }

private:
  bool text_edited_ = false;
  double edit_value_ = 0.0;
};

} // namespace quader
