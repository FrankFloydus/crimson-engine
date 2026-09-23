////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRect>

#include <cstdint>
#include <optional>

class QWidget;

namespace quader {

/// One accepted motion sample and any cursor recenter it requires.
struct QtHorizontalPointerMotion {
  double delta_x = 0.0;
  std::optional<QPoint> warp_target;
};

/// Tracks physical travel independently of cursor warps and bounded coordinates.
class QtHorizontalPointerWrap final {
public:
  void start(QPointF global_position, QRect bounds);
  [[nodiscard]] QtHorizontalPointerMotion move(QPointF global_position);
  void clear();

private:
  QPointF last_position_;
  QRect bounds_;
  std::optional<QPoint> pending_warp_;
  bool awaiting_warp_motion_ = false;
};

/// Owns a widget mouse grab and hidden, unbounded horizontal cursor motion.
/// The gesture owner remains responsible for deciding commit versus cancel.
class QtHorizontalPointerCapture final : public QObject {
public:
  QtHorizontalPointerCapture() = default;
  ~QtHorizontalPointerCapture() override;

  [[nodiscard]] bool start(QWidget &widget, QPointF global_position,
                           QPointF restore_position);
  [[nodiscard]] double delta(QPointF global_position);
  void stop();
  [[nodiscard]] bool is_active() const {
    return active_ && !widget_.isNull();
  }

private:
  QPointer<QWidget> widget_;
  QtHorizontalPointerWrap motion_;
  QPoint restore_position_;
  bool active_ = false;
  bool native_capture_ = false;
  std::uint64_t generation_ = 0;
};

} // namespace quader
