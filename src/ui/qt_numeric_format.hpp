////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QLocale>
#include <QString>

#include <algorithm>
#include <cmath>

namespace quader {

/// Formats numeric controls without padding or changing their underlying value.
[[nodiscard]] inline QString qt_format_number(
    double value, const QLocale &locale, int decimals = 4) {
  decimals = std::clamp(decimals, 0, 4);
  if (std::abs(value) < 0.5 * std::pow(10.0, -decimals)) {
    value = 0.0;
  }
  QString text = locale.toString(value, 'f', decimals);
  const QString separator = locale.decimalPoint();
  if (text.contains(separator)) {
    const QString zero = locale.zeroDigit();
    while (text.endsWith(zero)) {
      text.chop(zero.size());
    }
    if (text.endsWith(separator)) {
      text.chop(separator.size());
    }
  }
  return text;
}

} // namespace quader
