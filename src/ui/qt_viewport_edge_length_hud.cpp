////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_edge_length_hud.hpp"

#include "platform/native_window_input.hpp"
#include "render/render_signature_builder.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_layout.hpp"

#include <QByteArray>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QPalette>
#include <QSizePolicy>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace quader {
namespace {

/// Keeps the HUD's native child window out of viewport pointer hit testing.
class QtViewportEdgeLengthLabel final : public QLabel {
public:
  explicit QtViewportEdgeLengthLabel(QWidget *parent) : QLabel(parent) {}

protected:
  bool nativeEvent(const QByteArray &event_type, void *message,
                   qintptr *result) override {
    if (result != nullptr &&
        event_type == QByteArrayLiteral("windows_generic_MSG")) {
      if (const std::optional<std::intptr_t> transparent_result =
              platform_transparent_overlay_input_result(message)) {
        *result = static_cast<qintptr>(*transparent_result);
        return true;
      }
    }
    return QLabel::nativeEvent(event_type, message, result);
  }
};

bool same_edge_geometry(const std::optional<ViewportOverlayLine> &current,
                        const ViewportOverlayLine *requested) {
  if (!current.has_value() || requested == nullptr) {
    return current.has_value() == (requested != nullptr);
  }
  return current->start.x == requested->start.x &&
         current->start.y == requested->start.y &&
         current->start.z == requested->start.z &&
         current->end.x == requested->end.x &&
         current->end.y == requested->end.y &&
         current->end.z == requested->end.z;
}

QString edge_length_text(const ViewportOverlayLine &edge) {
  const double x = static_cast<double>(edge.end.x) - edge.start.x;
  const double y = static_cast<double>(edge.end.y) - edge.start.y;
  const double z = static_cast<double>(edge.end.z) - edge.start.z;
  const double map_length =
      std::hypot(x, y, z) * viewport_host_defaults().map_units_per_world_unit;
  if (!std::isfinite(map_length) || map_length <= 0.0) {
    return {};
  }
  // Ordinary dimensions get two decimals; short edges retain useful precision
  // and extreme values stay compact. A numeric-only HUD needs no label string.
  if (map_length < 0.0001 || map_length >= 1.0e9) {
    return QString::number(map_length, 'g', 6);
  }
  const int decimals = map_length < 1.0
                           ? std::clamp(
                                 2 - static_cast<int>(std::floor(
                                         std::log10(map_length))),
                                 2, 6)
                           : 2;
  QString text = QString::number(map_length, 'f', decimals);
  while (text.endsWith(QLatin1Char('0'))) {
    text.chop(1);
  }
  if (text.endsWith(QLatin1Char('.'))) {
    text.chop(1);
  }
  return text;
}

} // namespace

QtViewportEdgeLengthHud::QtViewportEdgeLengthHud(
    QWidget &parent, ViewportPaneId pane_id)
    : QObject(&parent), label_(new QtViewportEdgeLengthLabel(&parent)) {
  const std::string_view pane_name = viewport_pane_stable_id(pane_id);
  label_->setObjectName(
      QStringLiteral("viewportEdgeLength_%1")
          .arg(QString::fromUtf8(pane_name.data(),
                                 static_cast<qsizetype>(pane_name.size()))));
  label_->setTextFormat(Qt::PlainText);
  label_->setAlignment(Qt::AlignCenter);
  label_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  label_->setFrameStyle(QFrame::Panel | QFrame::Plain);
  label_->setMargin(2);
  label_->setBackgroundRole(QPalette::ToolTipBase);
  label_->setForegroundRole(QPalette::ToolTipText);
  label_->setAutoFillBackground(true);
  label_->setFocusPolicy(Qt::NoFocus);
  label_->setAttribute(Qt::WA_NativeWindow);
  // This Qt attribute handles widget routing; the label's native hit-test
  // override also passes input to the sibling viewport QWindow on Windows.
  label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  label_->installEventFilter(this);
  label_->hide();
}

void QtViewportEdgeLengthHud::present(
    const ViewportOverlayFrame &frame, std::uint64_t frame_signature,
    const ViewportCameraController &camera,
    const QRect &logical_viewport_bounds, bool enabled) {
  if (!enabled) {
    hide();
    return;
  }

  // A stationary hover must not rescan overlays or reformat its number. Reuse
  // the caller's signature rather than hashing this stream a second time.
  if (!frame_signature_valid_ || frame_signature_ != frame_signature) {
    const auto found = std::find_if(
        frame.lines.begin(), frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.layer == ViewportOverlayLayer::HoverEdge &&
                 line.source == ViewportOverlaySource::ComponentHover;
        });
    const ViewportOverlayLine *edge =
        found != frame.lines.end() ? &*found : nullptr;
    if (!same_edge_geometry(hovered_edge_, edge)) {
      hovered_edge_ = edge != nullptr
                          ? std::optional<ViewportOverlayLine>{*edge}
                          : std::nullopt;
      length_text_ = edge != nullptr ? edge_length_text(*edge) : QString{};
      view_valid_ = false;
    }
    frame_signature_ = frame_signature;
    frame_signature_valid_ = true;
  }
  if (!hovered_edge_.has_value() || length_text_.isEmpty()) {
    hide();
    return;
  }

  const std::uint64_t camera_signature =
      RenderSignatureBuilder::overlay_camera_signature(
          ViewportCameraAdapter::overlay_camera(camera,
                                                 camera.viewport_size()));
  if (view_valid_ && camera_signature_ == camera_signature &&
      logical_viewport_bounds_ == logical_viewport_bounds) {
    return;
  }
  camera_signature_ = camera_signature;
  logical_viewport_bounds_ = logical_viewport_bounds;
  view_valid_ = true;

  const std::optional<ViewportScreenSegment> visible_segment =
      camera.project_visible_world_segment(hovered_edge_->start,
                                            hovered_edge_->end);
  const QVec2 render_size = camera.viewport_size();
  if (!visible_segment.has_value() || render_size.x <= 0.0f ||
      render_size.y <= 0.0f ||
      logical_viewport_bounds.isEmpty()) {
    if (!label_->isHidden()) {
      label_->hide();
    }
    return;
  }
  // Keep the label on the part of the edge the user can see, even when its
  // geometric midpoint is offscreen. The number still measures the full edge.
  const QVec2 midpoint =
      visible_segment->start * 0.5f + visible_segment->end * 0.5f;

  if (label_->text() != length_text_) {
    label_->setText(length_text_);
  }
  const QSize size = label_->sizeHint();
  if (size.width() > logical_viewport_bounds.width() ||
      size.height() > logical_viewport_bounds.height()) {
    if (!label_->isHidden()) {
      label_->hide();
    }
    return;
  }
  // Each native surface renders physical pixels, while child QWidget geometry
  // is logical. Independent axis ratios also cover rounded fractional DPI.
  const QPoint center{
      logical_viewport_bounds.x() +
          qRound(midpoint.x * logical_viewport_bounds.width() /
                 render_size.x),
      logical_viewport_bounds.y() +
          qRound(midpoint.y * logical_viewport_bounds.height() /
                 render_size.y)};
  const QRect geometry{
      std::clamp(center.x() - (size.width() - 1) / 2, logical_viewport_bounds.x(),
                 logical_viewport_bounds.right() - size.width() + 1),
      std::clamp(center.y() - (size.height() - 1) / 2, logical_viewport_bounds.y(),
                 logical_viewport_bounds.bottom() - size.height() + 1),
      size.width(), size.height()};
  if (label_->geometry() != geometry) {
    label_->setGeometry(geometry);
  }
  if (label_->isHidden()) {
    label_->show();
    label_->raise();
  }
}

void QtViewportEdgeLengthHud::hide() {
  view_valid_ = false;
  if (!label_->isHidden()) {
    label_->hide();
  }
}

void QtViewportEdgeLengthHud::raise() {
  if (!label_->isHidden()) {
    label_->raise();
  }
}

bool QtViewportEdgeLengthHud::eventFilter(QObject *watched, QEvent *event) {
  if (watched == label_ && event != nullptr &&
      (event->type() == QEvent::FontChange ||
       event->type() == QEvent::StyleChange ||
       event->type() == QEvent::PaletteChange)) {
    view_valid_ = false;
  }
  return QObject::eventFilter(watched, event);
}

} // namespace quader
