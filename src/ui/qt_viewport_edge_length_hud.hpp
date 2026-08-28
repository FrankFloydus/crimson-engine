////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <QObject>
#include <QRect>
#include <QString>

#include <cstdint>
#include <optional>

class QLabel;
class QWidget;

namespace quader {

class ViewportCameraController;

/**
 * Presents the world-space length of the current component hover edge in map
 * units. Borrows the small semantic overlay stream; never queries scene meshes.
 */
class QtViewportEdgeLengthHud final : public QObject {
public:
  QtViewportEdgeLengthHud(QWidget &parent, ViewportPaneId pane_id);

  QtViewportEdgeLengthHud(const QtViewportEdgeLengthHud &) = delete;
  QtViewportEdgeLengthHud &operator=(const QtViewportEdgeLengthHud &) = delete;

  /// The signature is the caller's existing dynamic-overlay frame signature.
  /// Bounds are pane-local Qt logical pixels; camera coordinates are render pixels.
  void present(const ViewportOverlayFrame &frame, std::uint64_t frame_signature,
               const ViewportCameraController &camera,
               const QRect &logical_viewport_bounds, bool enabled);
  void hide();
  void raise();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  QLabel *label_ = nullptr;
  std::optional<ViewportOverlayLine> hovered_edge_;
  QString length_text_;
  QRect logical_viewport_bounds_;
  std::uint64_t frame_signature_ = 0;
  std::uint64_t camera_signature_ = 0;
  bool frame_signature_valid_ = false;
  bool view_valid_ = false;
};

} // namespace quader
