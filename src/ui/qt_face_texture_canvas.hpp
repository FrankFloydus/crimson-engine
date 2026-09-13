////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_modeling_api.hpp"
#include "ui/qt_material_presentation.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QWidget>

#include <cstdint>
#include <optional>

namespace quader {

class LocalizationService;
class QuaderEditorApi;

/** Retained planar face view. All document edits are owner preview gestures. */
class QtFaceTextureCanvas final : public QWidget {
public:
  explicit QtFaceTextureCanvas(QWidget *parent = nullptr);
  ~QtFaceTextureCanvas() override;

  void bind(QuaderEditorApi &editor);
  void unbind();
  /** Drops capture and presentation without accessing an outgoing runtime. */
  void abandon();
  void refresh();
  void set_localization(const LocalizationService &localization);
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void set_material_presentation(
      std::optional<QtMaterialPresentationItem> material);

  [[nodiscard]] bool interaction_active() const noexcept;
  [[nodiscard]] QuaderMaterialReference material_reference() const;
  [[nodiscard]] const QuaderFaceTextureCanvas &presentation() const noexcept;
  [[nodiscard]] ::QTransform plane_to_view() const;
  [[nodiscard]] QSize sizeHint() const override;

protected:
  bool event(QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;
  void leaveEvent(QEvent *event) override;

private:
  enum class Handle { None, Height, Width, Rotation };

  void fit_face();
  void refresh_image();
  void release_capture();
  void cancel_gesture();
  void update_gesture(QPointF position, Qt::KeyboardModifiers modifiers);
  [[nodiscard]] QPointF plane_position(QPointF position) const;
  [[nodiscard]] QPointF handle_position(Handle handle) const;
  [[nodiscard]] Handle hit_handle(QPointF position) const;
  void update_hover(QPointF position);
  void present_failure(const QuaderApiVoidResult &result);

  QuaderEditorApi *editor_ = nullptr;
  const LocalizationService *localization_ = nullptr;
  std::uint64_t binding_generation_ = 0;
  std::optional<QuaderFaceTextureRevision> revision_;
  QuaderFaceTextureCanvas presentation_;
  QPolygonF outline_;
  QRectF bounds_;
  QPointF center_;
  QPointF pan_start_;
  QPointF pointer_start_;
  QPointF last_pointer_;
  QPointF gesture_pointer_offset_;
  double zoom_ = 1.0;
  Handle hovered_ = Handle::None;
  Handle dragged_ = Handle::None;
  bool gesture_active_ = false;
  bool panning_ = false;
  bool releasing_capture_ = false;
  bool fit_pending_ = false;
  bool view_manipulated_ = false;
  bool failure_announced_ = false;
  std::optional<QtMaterialPresentationItem> material_;
  QImage image_;
  int decoded_extent_ = 0;
  QtUiScaleMetrics metrics_;
};

} // namespace quader
