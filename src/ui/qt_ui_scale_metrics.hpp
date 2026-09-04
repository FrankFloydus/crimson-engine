////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QFont>
#include <QMargins>
#include <QProxyStyle>
#include <QSize>
#include <QtTypes>

#include <cstdint>
#include <functional>
#include <memory>

class QApplication;
class QObject;
class QStyle;

namespace quader {

/**
 * Immutable UI-scale snapshot. Every helper accepts an unscaled design value,
 * so applying a newer snapshot never compounds an already-scaled result.
 */
class QtUiScaleMetrics final {
public:
  explicit QtUiScaleMetrics(float factor = 1.0F,
                            std::uint64_t revision = 0U);

  [[nodiscard]] float factor() const noexcept { return factor_; }
  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

  [[nodiscard]] int px(int design_pixels) const noexcept;
  [[nodiscard]] float px(float design_pixels) const noexcept;
  [[nodiscard]] double px(double design_pixels) const noexcept;
  [[nodiscard]] QSize size(const QSize &design_size) const noexcept;
  [[nodiscard]] QMargins margins(const QMargins &design_margins) const noexcept;
  [[nodiscard]] QFont font(const QFont &design_font) const;

  /** Returns the backing-pixel extent for a logical design extent. */
  [[nodiscard]] int device_pixels(int design_pixels,
                                  qreal device_pixel_ratio) const noexcept;

private:
  float factor_ = 1.0F;
  std::uint64_t revision_ = 0U;
};

/** Fusion proxy that scales stock control geometry while keeping hairlines. */
class QtUiScaleProxyStyle final : public QProxyStyle {
public:
  explicit QtUiScaleProxyStyle(
      const QtUiScaleMetrics &metrics = QtUiScaleMetrics{});

  void set_metrics(const QtUiScaleMetrics &metrics) noexcept;
  [[nodiscard]] const QtUiScaleMetrics &metrics() const noexcept {
    return metrics_;
  }

  [[nodiscard]] int
  pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
              const QWidget *widget = nullptr) const override;

private:
  QtUiScaleMetrics metrics_;
  // Kept outside QProxyStyle's base-style chain so Fusion's internal
  // proxy()->pixelMetric() delegation cannot re-enter scaled metrics.
  QStyle *design_metrics_style_ = nullptr;
};

/**
 * Owns the one application-wide scale revision. Applying a factor updates the
 * proxy style and derives the application font from one immutable baseline,
 * then notifies retained widgets exactly once for that revision.
 */
class QtUiScaleApplicationController final {
public:
  using ListenerId = std::uint64_t;
  using ChangeListener = std::function<void(const QtUiScaleMetrics &)>;

  explicit QtUiScaleApplicationController(QApplication &application,
                                          float initial_factor = 1.0F);
  ~QtUiScaleApplicationController();

  QtUiScaleApplicationController(const QtUiScaleApplicationController &) =
      delete;
  QtUiScaleApplicationController &
  operator=(const QtUiScaleApplicationController &) = delete;

  [[nodiscard]] const QtUiScaleMetrics &metrics() const noexcept;
  [[nodiscard]] const QFont &baseline_font() const noexcept;
  [[nodiscard]] QtUiScaleProxyStyle &style() const noexcept;

  /** Returns false when the normalized factor is unchanged. */
  bool apply_factor(float factor);

  /**
   * Registers a direct UI-thread listener. Receiver lifetime is observed and
   * the current snapshot is delivered immediately by default.
   */
  ListenerId add_change_listener(QObject &receiver, ChangeListener listener,
                                 bool notify_immediately = true);
  void remove_change_listener(ListenerId id);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
