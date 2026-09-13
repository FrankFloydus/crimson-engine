////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_material_presentation.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QByteArray>
#include <QPoint>
#include <QWidget>

#include <optional>

class QScrollArea;
class QSplitter;

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class QtFaceTextureCanvas;
class QtFaceTextureControls;
class QtMaterialBrowserWindow;

/** Dock content composed from the face canvas, controls and shared browser. */
class QtFaceTexturePanel final : public QWidget {
public:
  explicit QtFaceTexturePanel(QWidget *parent = nullptr);
  ~QtFaceTexturePanel() override;

  void bind(QuaderEditorApi &editor);
  void unbind();
  /** Abandons gestures and numeric drafts without calling outgoing owners. */
  void abandon();
  void refresh();
  void set_localization(const LocalizationService &localization);
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void set_material_presentation(
      std::optional<QtMaterialPresentationItem> material);

  [[nodiscard]] QtMaterialBrowserWindow &browser() const;
  [[nodiscard]] QtFaceTextureCanvas &canvas() const;
  [[nodiscard]] QuaderMaterialReference canvas_material_reference() const;
  [[nodiscard]] bool numeric_interaction_active() const;
  [[nodiscard]] bool interaction_active() const;
  [[nodiscard]] QByteArray save_splitter_state() const;
  bool restore_splitter_state(const QByteArray &state);
  [[nodiscard]] QSize sizeHint() const override;

private:
  QSplitter *splitter_ = nullptr;
  QWidget *upper_ = nullptr;
  QtFaceTextureCanvas *canvas_ = nullptr;
  QScrollArea *controls_scroll_ = nullptr;
  QtFaceTextureControls *controls_ = nullptr;
  QtMaterialBrowserWindow *browser_ = nullptr;
  QtUiScaleMetrics metrics_;
};

/** Includes a floated dock's native window in Face Texture input ownership. */
[[nodiscard]] bool
qt_object_belongs_to_face_texture_panel(const QObject *target);
/** Resolves the visible panel region inside a shared docked native window. */
[[nodiscard]] bool qt_object_belongs_to_face_texture_panel(
    const QObject *target, const QPoint &global_position);

} // namespace quader
