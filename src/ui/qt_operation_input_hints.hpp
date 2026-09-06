////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/retained_ui_presenter.hpp"

#include <QRect>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <vector>

namespace quader {

class LocalizationService;
class QuaderCommandsApi;
class QuaderToolsApi;

/// One retained, localized badge/action pair, independent of the originating tool.
struct QtOperationInputHint {
  QString badge;
  QString label;
  friend bool operator==(const QtOperationInputHint &, const QtOperationInputHint &) = default;
};

/// A compact, single-line status-bar hint row. It never handles input or enumerates catalogs.
class QtOperationInputHints final : public QWidget {
public:
  explicit QtOperationInputHints(QWidget *parent = nullptr);
  void refresh(const QuaderToolsApi &tools, const QuaderCommandsApi &commands,
               const LocalizationService &localization);
  /// Drops presentation without consulting a possibly expired runtime owner.
  void invalidate();
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  [[nodiscard]] const std::vector<QtOperationInputHint> &hints() const { return hints_; }
  [[nodiscard]] std::uint64_t projection_revision() const { return projection_revision_; }
  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  /// Identifies the owner, binding, and locale publications already presented.
  struct Stamp {
    const QuaderToolsApi *tools = nullptr;
    const QuaderCommandsApi *commands = nullptr;
    const LocalizationService *localization = nullptr;
    std::uint64_t hints_revision = 0;
    std::uint64_t bindings_revision = 0;
    std::uint64_t localization_revision = 0;
    friend bool operator==(const Stamp &, const Stamp &) = default;
  };
  /// Retains logical-pixel badge and label rectangles within the status row.
  struct ItemGeometry { QRect badge; QRect label; };
  [[nodiscard]] int layout_items(int width, std::vector<ItemGeometry> *items) const;
  void refresh_geometry();

  RetainedUiPresenter<Stamp> presenter_;
  QtUiScaleMetrics metrics_;
  std::vector<QtOperationInputHint> hints_;
  std::vector<ItemGeometry> geometry_;
  std::uint64_t projection_revision_ = 0;
};

} // namespace quader
