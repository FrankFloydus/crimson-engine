////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>

class QLabel;
class QWidget;

namespace quader {

class ViewportCameraController;
struct ModelingBoxPreview;

/**
 * Presents compact world-space edge, footprint, and selection dimensions in
 * map units. Borrows existing semantic state; never queries scene meshes.
 * Owns retirement of accepted native glyph pixels before ancestor geometry
 * changes, including their historical positions in the shared backing store.
 */
class QtViewportEdgeLengthHud final : public QObject,
                                    public QAbstractNativeEventFilter {
public:
  QtViewportEdgeLengthHud(QWidget &parent, ViewportPaneId pane_id);
  ~QtViewportEdgeLengthHud() override;

  QtViewportEdgeLengthHud(const QtViewportEdgeLengthHud &) = delete;
  QtViewportEdgeLengthHud &operator=(const QtViewportEdgeLengthHud &) = delete;

  void apply_ui_scale(const QtUiScaleMetrics &metrics);

  /// The signature is the caller's existing dynamic-overlay frame signature.
  /// Bounds are pane-local Qt logical pixels; camera coordinates are render pixels.
  void present(const ViewportOverlayFrame &frame, std::uint64_t frame_signature,
               const ViewportCameraController &camera,
               const QRect &logical_viewport_bounds, bool edge_enabled,
               double font_size_points,
               const ModelingBoxPreview *footprint_preview = nullptr,
               bool footprint_enabled = false,
               const ViewportSelectionDimensionBounds *selection_bounds = nullptr,
               std::uint64_t selection_signature = 0u,
               bool selection_enabled = false);
  void hide();
  void raise();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  bool nativeEventFilter(const QByteArray &event_type, void *message,
                         qintptr *result) override;

private:
  /// One full semantic span and the optional interior point it faces away from.
  struct Dimension final {
    QVec3 start;
    QVec3 end;
    std::optional<QVec3> outside_reference;
  };

  void retain_native_footprint(const QLabel &label);
  bool clean_native_footprint();

  std::array<QLabel *, 6> labels_ = {};
  QtUiScaleMetrics ui_scale_metrics_;
  std::array<std::optional<Dimension>, 6> dimensions_ = {};
  std::array<QString, 6> dimension_texts_ = {};
  std::optional<ViewportSelectionDimensionBounds> selection_bounds_;
  std::array<double, 3> selection_lengths_ = {};
  QRect logical_viewport_bounds_;
  QPointer<QWidget> native_footprint_owner_;
  QRect native_footprint_;
  std::uint64_t frame_signature_ = 0;
  std::uint64_t camera_signature_ = 0;
  std::uint64_t selection_signature_ = 0;
  std::uint64_t selection_camera_signature_ = 0;
  double font_size_points_ = 0.0;
  bool frame_signature_valid_ = false;
  bool selection_signature_valid_ = false;
  bool selection_projection_valid_ = false;
  bool font_size_valid_ = false;
  bool view_valid_ = false;
  bool native_cleanup_pending_ = false;
  bool native_cleanup_active_ = false;
};

} // namespace quader
