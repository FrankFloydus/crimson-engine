////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/render_view.hpp"
#include "render/render_host.hpp"
#include "viewport/material_preview_state.hpp"

#include <QPointF>
#include <QPointer>
#include <QWindow>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

class QExposeEvent;
class QMouseEvent;
class QResizeEvent;
class QWheelEvent;
class QWidget;

namespace quader {

/**
 * Hosts an isolated Standard Material preview in a Qt-native child surface.
 * It owns no project, scene-document, or viewport-workspace state.
 */
class QtMaterialPreviewSurface final : public QWindow {
public:
  explicit QtMaterialPreviewSurface(QWindow *parent = nullptr);
  ~QtMaterialPreviewSurface() override;

  QtMaterialPreviewSurface(const QtMaterialPreviewSurface &) = delete;
  QtMaterialPreviewSurface &
  operator=(const QtMaterialPreviewSurface &) = delete;

  [[nodiscard]] bool initialize(RenderHost &render_system);
  // Reuses Quader's QRhi composition while this QWindow remains the semantic
  // input owner, exactly like a composed modeling viewport. No native child is
  // created and no window container or swap chain belongs to the preview.
  QWidget *create_composed_widget(QWidget *parent,RenderHost &render_system);
  void shutdown();

  [[nodiscard]] bool render_frame();
  [[nodiscard]] bool ready_to_render() const;
  [[nodiscard]] bool needs_rendering() const;
  void synchronize_render_size();

  void set_material(StandardMaterialPreviewSnapshot snapshot);
  void set_record_callback(RenderMaterialPreviewRecorder);
  void mark_dirty();
  [[nodiscard]] const MaterialPreviewState &preview_state() const noexcept {
    return preview_state_;
  }
  bool set_preview_mesh(MaterialPreviewMeshKind mesh);
  bool set_preview_shading(MaterialPreviewShadingMode shading);
  bool set_preview_environment(std::string_view environment_id);
  bool set_show_environment_background(bool visible);
  bool reset_preview();

  void set_surface_changed_callback(std::function<void()> callback) {
    surface_changed_callback_ = std::move(callback);
  }

protected:
  bool eventFilter(QObject*,QEvent*) override;
  bool event(QEvent *event) override;
  void exposeEvent(QExposeEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

private:
  /// Selects the semantic preview action captured for one active MMB drag.
  enum class DragMode : std::uint8_t {
    None,
    Tumble,
    Pan,
    RotateEnvironment,
  };

  void apply_interaction(const MaterialPreviewInteractionFrame &input);
  void register_input_hints(QObject&);
  void publish_state_change();
  void mark_shared_commands_pending();
  void mark_render_pending();
  void recreate_swap_chain(bool force_retry = false);
  [[nodiscard]] void *native_window_handle() const;
  [[nodiscard]] bool input_hints_available() const noexcept;

  RenderHost *render_system_ = nullptr;
  QPointer<QWidget> composed_host_;
  std::unique_ptr<RenderSurface> swap_chain_;
  std::unique_ptr<RenderMaterialPreviewView> renderer_;
  std::optional<StandardMaterialPreviewSnapshot> material_snapshot_;
  MaterialPreviewState preview_state_;
  MaterialPreviewInputRouter input_router_;
  std::function<void()> surface_changed_callback_;
  RenderMaterialPreviewRecorder record_callback_;
  QPointF last_drag_position_;
  bool initialized_ = false;
  bool native_surface_available_ = false;
  bool swap_chain_creation_failed_ = false;
  bool render_pending_ = true;
  DragMode drag_mode_ = DragMode::None;
  std::uintptr_t swap_chain_native_id_ = 0u;
};

} // namespace quader
