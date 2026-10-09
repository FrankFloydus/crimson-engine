////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_vulkan_composition_widget.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "assets/asset_registry.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/input/viewport_keybinding_resolver.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/viewport/primitive_creation_api_adapter.hpp"
#include "editor/viewport/viewport_scene_projection.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "editor/viewport/viewport_modeling_tool_commit_adapter.hpp"
#include "editor/tools/viewport_native_tool_interaction.hpp"
#include "math/glm_bridge.hpp"
#include "platform/native_window_input.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/render_layers.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_info_stats.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_face_texture_panel.hpp"
#include "ui/qt_environment_settings_popup.hpp"
#include "core/release_features.hpp"
#include "ui/qt_numeric_format.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"
#include "ui/qt_viewport_control_strip.hpp"
#include "ui/qt_viewport_edge_length_hud.hpp"
#include "ui/qt_viewport_fly_navigation_hud.hpp"
#include "ui/qt_viewport_pane_activation.hpp"
#include "ui/qt_viewport_render_submission_policy.hpp"
#include "ui/viewport_context_menu_policy.hpp"
#include "viewport/viewport_host_interaction_policy.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_input_types.hpp"
#include "viewport/viewport_layout.hpp"
#include "viewport/viewport_navigation_defaults.hpp"
#include "viewport/viewport_pixel_scale.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QByteArray>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLocale>
#include <QPalette>
#include <QPainter>
#include <QRegion>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSlider>
#include <QSizePolicy>
#include <QStylePainter>
#include <QStyleOptionButton>
#include <QStyleOptionToolButton>
#include <QToolButton>
#include <QFontMetrics>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {

/// Retained native popup editing only the linked pane's current fly speed.
class QtViewportCameraSettingsPopup final : public QFrame {
public:
  explicit QtViewportCameraSettingsPopup(QWidget *parent) : QFrame(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setFrameShape(QFrame::StyledPanel);
    setBackgroundRole(QPalette::AlternateBase);
    setAutoFillBackground(true);
    layout_ = new QVBoxLayout(this);
    auto *row = new QHBoxLayout;
    label_ = new QLabel(this);
    value_ = new QLabel(this);
    row->addWidget(label_);
    row->addStretch();
    row->addWidget(value_);
    layout_->addLayout(row);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(static_cast<int>(kMinimumViewportFlyNavigationSpeed),
                      static_cast<int>(kMaximumViewportFlyNavigationSpeed));
    slider_->setSingleStep(1);
    slider_->setPageStep(10);
    slider_->setTracking(true);
    label_->setBuddy(slider_);
    layout_->addWidget(slider_);
    setFocusProxy(slider_);
  }

  QSlider &slider() const { return *slider_; }
  QLabel &value_label() const { return *value_; }

  void set_label(const QString &label) {
    label_->setText(label);
    slider_->setAccessibleName(label);
    speed_valid_ = false;
  }

  void set_speed(double speed) {
    const int slider_value = static_cast<int>(std::lround(speed));
    if (speed_valid_ && speed_ == speed && slider_->value() == slider_value) return;
    speed_ = speed;
    speed_valid_ = true;
    const QSignalBlocker blocker(slider_);
    slider_->setValue(slider_value);
    value_->setText(qt_format_number(speed, locale()));
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    setFont(QApplication::font());
    layout_->setContentsMargins(metrics.margins(QMargins(12, 10, 12, 10)));
    layout_->setSpacing(metrics.px(6));
    slider_->setMinimumWidth(metrics.px(220));
    layout_->invalidate();
    adjustSize();
  }

  void show_below(QWidget &anchor) {
    adjustSize();
    QPoint position = anchor.mapToGlobal(QPoint(0, anchor.height()));
    if (layoutDirection() == Qt::RightToLeft) {
      position.rx() += anchor.width() - width();
    }
    if (QScreen *screen = anchor.screen()) {
      const QRect bounds = screen->availableGeometry();
      position.setX(std::clamp(position.x(), bounds.left(),
          std::max(bounds.left(), bounds.right() - width() + 1)));
      position.setY(std::clamp(position.y(), bounds.top(),
          std::max(bounds.top(), bounds.bottom() - height() + 1)));
    }
    move(position);
    show();
    slider_->setFocus(Qt::PopupFocusReason);
  }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event->key() == Qt::Key_Escape) {
      close();
      event->accept();
      return;
    }
    QFrame::keyPressEvent(event);
  }

private:
  QVBoxLayout *layout_ = nullptr;
  QLabel *label_ = nullptr;
  QLabel *value_ = nullptr;
  QSlider *slider_ = nullptr;
  double speed_ = 0.0;
  bool speed_valid_ = false;
};

namespace {

using ViewportProbeClock = std::chrono::steady_clock;

/// Native toolbar interaction with palette-aware neutral viewport chrome.
class QtViewportChromeButton final : public QToolButton {
public:
  enum class Glyph { Layout, Camera };
  QtViewportChromeButton(Glyph glyph, QWidget *parent)
      : QToolButton(parent), glyph_(glyph) {
    auto *action = new QAction(this);
    setDefaultAction(action);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    configure_native_toolbar_button_face(*this, *action, *parent->style());
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    apply_native_toolbar_button_face_ui_scale(*this, metrics);
    setFixedSize(metrics.size(QSize(28, 28)));
    glyph_size_ = metrics.px(16);
    update();
  }

  QSize sizeHint() const override { return minimumSize(); }
  QSize minimumSizeHint() const override { return minimumSize(); }

protected:
  void paintEvent(QPaintEvent *) override {
    QStylePainter painter(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.text.clear();
    option.features &= ~(QStyleOptionToolButton::HasMenu |
                         QStyleOptionToolButton::MenuButtonPopup);
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate((width() - glyph_size_) / 2.0,
                      (height() - glyph_size_) / 2.0);
    painter.scale(glyph_size_ / 16.0, glyph_size_ / 16.0);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(option.palette.color(
        qt_toolbar_button_color_group(option.state), QPalette::ButtonText),
        1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (glyph_ == Glyph::Layout) {
      painter.drawRect(QRectF(2.0, 2.0, 12.0, 12.0));
    } else {
      painter.drawRoundedRect(QRectF(1.5, 5.0, 13.0, 9.0), 1.2, 1.2);
      painter.drawPolyline(QPolygonF{{4.0, 5.0}, {5.0, 2.5},
                                     {10.5, 2.5}, {11.5, 5.0}});
      painter.drawEllipse(QPointF(8.0, 9.5), 2.7, 2.7);
    }
  }

private:
  Glyph glyph_;
  int glyph_size_ = 16;
};

QuaderCommandId layout_command_id(ViewportLayoutId layout) {
  switch (layout) {
  case ViewportLayoutId::Single: return quader_command::kViewportLayoutSingle;
  case ViewportLayoutId::TwoHorizontal: return quader_command::kViewportLayoutTwoHorizontal;
  case ViewportLayoutId::TwoVertical: return quader_command::kViewportLayoutTwoVertical;
  case ViewportLayoutId::ThreePrimaryRight: return quader_command::kViewportLayoutThreePrimaryRight;
  case ViewportLayoutId::ThreePrimaryLeft: return quader_command::kViewportLayoutThreePrimaryLeft;
  case ViewportLayoutId::ThreePrimaryBottom: return quader_command::kViewportLayoutThreePrimaryBottom;
  case ViewportLayoutId::ThreePrimaryTop: return quader_command::kViewportLayoutThreePrimaryTop;
  case ViewportLayoutId::FourQuad: return quader_command::kViewportLayoutFourQuad;
  }
  return quader_command::kViewportLayoutSingle;
}

/// Uses native menu-button chrome while opening a retained settings window.
class QtEnvironmentLauncherButton final : public QPushButton {
public:
  using QPushButton::QPushButton;
  QSize sizeHint() const override {
    QSize result = QPushButton::sizeHint();
    result.rwidth() += style()->pixelMetric(QStyle::PM_MenuButtonIndicator, nullptr, this);
    return result;
  }
protected:
  void paintEvent(QPaintEvent *) override {
    QStylePainter painter(this);
    QStyleOptionButton option;
    initStyleOption(&option);
    option.features |= QStyleOptionButton::HasMenu;
    painter.drawControl(QStyle::CE_PushButton, option);
  }
};

/// Keeps the strip's closed selectors compact while menus retain native metrics.
class QtViewportCompactMenuButton final : public QPushButton {
public:
  using QPushButton::QPushButton;

  void set_metrics(const QtUiScaleMetrics &metrics) {
    metrics_ = metrics;
    setFixedHeight(fontMetrics().height() + metrics_.px(4));
    updateGeometry();
  }

  QSize sizeHint() const override {
    return QSize(fontMetrics().horizontalAdvance(text()) + metrics_.px(24),
                 fontMetrics().height() + metrics_.px(4));
  }

  QSize minimumSizeHint() const override { return sizeHint(); }

private:
  QtUiScaleMetrics metrics_;
};

/** Paints only a masked native border above the viewport's child window. */
class QtViewportFocusFrame final : public QWidget {
public:
  QtViewportFocusFrame(QWidget *parent, bool native_widget) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow, native_widget);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.fillRect(rect(), qt_current_item_outline_color());
  }

  bool nativeEvent(const QByteArray &event_type, void *message,
                   qintptr *result) override {
    if (result != nullptr &&
        event_type == QByteArrayLiteral("windows_generic_MSG")) {
      if (const auto transparent =
              platform_transparent_overlay_input_result(message)) {
        *result = static_cast<qintptr>(*transparent);
        return true;
      }
    }
    return QWidget::nativeEvent(event_type, message, result);
  }
};

/// Passes native hit testing through feedback to the sibling viewport window.
class QtViewportPrimitiveFeedbackLabel final : public QLabel {
public:
  explicit QtViewportPrimitiveFeedbackLabel(QWidget *parent) : QLabel(parent) {}

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

/// Accumulates opt-in viewport cadence and interaction timing diagnostics.
struct ViewportCadenceProbe {
  ViewportProbeClock::time_point window_start = ViewportProbeClock::now();
  std::uint64_t frames = 0;
  std::uint64_t motion_frames = 0;
  std::uint64_t mouse_events = 0;
  std::uint64_t submitted = 0;
  double input_ms = 0.0;
  double preview_sync_ms = 0.0;
  double prepare_ms = 0.0;
  double render_ms = 0.0;
};

ViewportCadenceProbe g_viewport_probe;

ModelingCommandResult set_primitive_segments(QuaderEditorApi &editor, int axis, int count) {
  const auto primitive = editor.tools().live_primitive_creation();
  if (!primitive || axis < 0) {
    return ModelingCommandResult{.ok = false, .changed = false,
                                 .message = "No editable primitive control."};
  }
  std::string_view setting;
  switch (primitive->shape) {
  case QuaderPrimitiveCreationShape::Box: {
    constexpr std::array<std::string_view, 3> kSettings{
        "primitive.box.subdivisions_x", "primitive.box.subdivisions_y",
        "primitive.box.subdivisions_z"};
    if (axis < static_cast<int>(kSettings.size())) setting = kSettings[axis];
    break;
  }
  case QuaderPrimitiveCreationShape::Cylinder:
    if (axis == 0) setting = "primitive.cylinder.side_count";
    break;
  case QuaderPrimitiveCreationShape::Cone:
    if (axis == 0) setting = "primitive.cone.side_count";
    break;
  case QuaderPrimitiveCreationShape::Sphere:
    if (primitive->sphere_type == QuaderSphereType::Quad) {
      if (axis == 0) setting = "primitive.sphere.subdivision";
    } else {
      if (axis == 0) setting = "primitive.sphere.radial_segments";
      else if (axis == 1) setting = "primitive.sphere.rings";
    }
    break;
  case QuaderPrimitiveCreationShape::Torus:
    if (axis == 0) setting = "primitive.torus.major_segments";
    else if (axis == 1) setting = "primitive.torus.minor_segments";
    break;
  }
  if (setting.empty()) {
    return ModelingCommandResult{.ok = false, .changed = false,
                                 .message = "Invalid primitive segment control."};
  }
  const auto result = editor.tools().set_setting(
      "create_primitive", setting, static_cast<double>(count));
  return {.ok = result.ok(), .changed = result.changed(),
          .message = std::string(result.error_message())};
}

double elapsed_milliseconds(ViewportProbeClock::time_point start,
                            ViewportProbeClock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

bool viewport_frame_diagnostics_enabled() {
  static const bool kEnabled =
      std::getenv("QUADER_FRAME_DIAGNOSTICS") != nullptr;
  return kEnabled;
}

constexpr double kKnifeEdgeStartMinimumRadiusLogicalPixels = 8.0;

bool same_pane_placements(const std::vector<ViewportPanePlacement> &left,
                          const std::vector<ViewportPanePlacement> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    const ViewportPanePlacement &a = left[index];
    const ViewportPanePlacement &b = right[index];
    if (a.pane_id != b.pane_id || a.normalized_rect.x != b.normalized_rect.x ||
        a.normalized_rect.y != b.normalized_rect.y ||
        a.normalized_rect.width != b.normalized_rect.width ||
        a.normalized_rect.height != b.normalized_rect.height) {
      return false;
    }
  }
  return true;
}

constexpr std::array<ViewportView, 7> kViewportViews{
    ViewportView::Perspective, ViewportView::Top,  ViewportView::Bottom,
    ViewportView::Front,       ViewportView::Back, ViewportView::Side,
    ViewportView::Left,
};

constexpr std::array<ViewportShadingMode, 4> kShadingModes{
    ViewportShadingMode::Wireframe,
    ViewportShadingMode::Solid,
    ViewportShadingMode::Shaded,
    ViewportShadingMode::Rendered,
};

std::optional<QuaderCommandId>
shading_command_id(ViewportShadingMode shading) {
  switch (shading) {
  case ViewportShadingMode::Wireframe:
    return quader_command::kViewportShadingWireframe;
  case ViewportShadingMode::Solid:
    return quader_command::kViewportShadingSolid;
  case ViewportShadingMode::Shaded:
    return quader_command::kViewportShadingShaded;
  case ViewportShadingMode::Rendered:
    return quader_command::kViewportShadingRendered;
  case ViewportShadingMode::DiffuseOnly:
  case ViewportShadingMode::SpecularOnly:
  case ViewportShadingMode::Albedo:
  case ViewportShadingMode::Roughness:
  case ViewportShadingMode::Metalness:
  case ViewportShadingMode::Normal:
  case ViewportShadingMode::Uv:
    return std::nullopt;
  }
  return std::nullopt;
}

constexpr std::array<ViewportShadingMode, 7> kDebugViewModes{
    ViewportShadingMode::DiffuseOnly, ViewportShadingMode::SpecularOnly,
    ViewportShadingMode::Albedo,      ViewportShadingMode::Roughness,
    ViewportShadingMode::Metalness,   ViewportShadingMode::Normal,
    ViewportShadingMode::Uv,
};

QString localized_view_label(const LocalizationService *localization,
                             ViewportView view) {
  std::string key = "viewport.view.";
  key.append(viewport_view_stable_id(view));
  const std::string fallback(viewport_view_label(view));
  return QString::fromStdString(
      localization != nullptr ? localization->text(key, fallback) : fallback);
}

std::string_view shading_stable_id(ViewportShadingMode shading) {
  switch (shading) {
  case ViewportShadingMode::Wireframe:
    return "wireframe";
  case ViewportShadingMode::Solid:
    return "solid";
  case ViewportShadingMode::Shaded:
    return "shaded";
  case ViewportShadingMode::Rendered:
    return "rendered";
  case ViewportShadingMode::DiffuseOnly:
    return "diffuse_only";
  case ViewportShadingMode::SpecularOnly:
    return "specular_only";
  case ViewportShadingMode::Albedo:
    return "albedo";
  case ViewportShadingMode::Roughness:
    return "roughness";
  case ViewportShadingMode::Metalness:
    return "metalness";
  case ViewportShadingMode::Normal:
    return "normal";
  case ViewportShadingMode::Uv:
    return "uv";
  }
  return "shaded";
}

std::string_view shading_label(ViewportShadingMode shading) {
  switch (shading) {
  case ViewportShadingMode::Wireframe:
    return "Wireframe";
  case ViewportShadingMode::Solid:
    return "Solid";
  case ViewportShadingMode::Shaded:
    return "Shaded";
  case ViewportShadingMode::Rendered:
    return "Rendered";
  case ViewportShadingMode::DiffuseOnly:
    return "Diffuse Only";
  case ViewportShadingMode::SpecularOnly:
    return "Specular Only";
  case ViewportShadingMode::Albedo:
    return "Albedo";
  case ViewportShadingMode::Roughness:
    return "Roughness";
  case ViewportShadingMode::Metalness:
    return "Metalness";
  case ViewportShadingMode::Normal:
    return "Normal";
  case ViewportShadingMode::Uv:
    return "UV";
  }
  return "Shaded";
}

QString localized_shading_label(const LocalizationService *localization,
                                ViewportShadingMode shading) {
  std::string key = viewport_shading_is_debug_view(shading)
                        ? "viewport.debug_view."
                        : "viewport.shading.";
  key.append(shading_stable_id(shading));
  const std::string fallback(shading_label(shading));
  return QString::fromStdString(
      localization != nullptr ? localization->text(key, fallback) : fallback);
}

QString localized_display_menu_label(const LocalizationService *localization,
                                     std::string_view key,
                                     std::string_view fallback) {
  return QString::fromStdString(
      localization != nullptr
          ? localization->text(std::string(key), std::string(fallback))
          : std::string(fallback));
}

ViewportMaterialDebugMode
viewport_material_debug_mode(ViewportShadingMode shading) {
  switch (shading) {
  case ViewportShadingMode::DiffuseOnly:
    return ViewportMaterialDebugMode::DiffuseOnly;
  case ViewportShadingMode::SpecularOnly:
    return ViewportMaterialDebugMode::SpecularOnly;
  case ViewportShadingMode::Albedo:
    return ViewportMaterialDebugMode::Albedo;
  case ViewportShadingMode::Roughness:
    return ViewportMaterialDebugMode::Roughness;
  case ViewportShadingMode::Metalness:
    return ViewportMaterialDebugMode::Metalness;
  case ViewportShadingMode::Normal:
    return ViewportMaterialDebugMode::Normal;
  case ViewportShadingMode::Uv:
    return ViewportMaterialDebugMode::Uv;
  case ViewportShadingMode::Wireframe:
  case ViewportShadingMode::Solid:
  case ViewportShadingMode::Shaded:
  case ViewportShadingMode::Rendered:
    return ViewportMaterialDebugMode::None;
  }
  return ViewportMaterialDebugMode::None;
}

QString localized_viewport_info_label(const LocalizationService *localization,
                                      std::string_view key,
                                      std::string_view fallback) {
  return QString::fromStdString(
      localization != nullptr
          ? localization->text(std::string(key), std::string(fallback))
          : std::string(fallback));
}

QString format_viewport_info_milliseconds(double value) {
  if (!(value >= 0.0) || !std::isfinite(value)) {
    return QStringLiteral("--");
  }
  return QStringLiteral("%1 ms").arg(value, 0, 'f', 2);
}

QString format_viewport_info_frames_per_second(double value) {
  if (!(value >= 0.0) || !std::isfinite(value)) {
    return QStringLiteral("--");
  }
  return QString::number(value, 'f', 1);
}

bool same_viewport_info_camera(const ViewportOverlayCamera &left,
                               const ViewportOverlayCamera &right) {
  return left.eye.x == right.eye.x && left.eye.y == right.eye.y &&
         left.eye.z == right.eye.z && left.forward.x == right.forward.x &&
         left.forward.y == right.forward.y &&
         left.forward.z == right.forward.z && left.right.x == right.right.x &&
         left.right.y == right.right.y && left.right.z == right.right.z &&
         left.up.x == right.up.x && left.up.y == right.up.y &&
         left.up.z == right.up.z &&
         left.viewport_size.x == right.viewport_size.x &&
         left.viewport_size.y == right.viewport_size.y &&
         left.projection == right.projection &&
         left.orthographic_size == right.orthographic_size &&
         left.fov_degrees == right.fov_degrees &&
         left.near_plane == right.near_plane &&
         left.far_plane == right.far_plane;
}

void log_command_failure(std::string_view source,
                         const QuaderApiVoidResult &result) {
  if (!result.ok) {
    LOG_ERROR("{} failed: {}", source,
              result.message.empty() ? "Command failed." : result.message);
  }
}

bool has_input_activity(const QtViewportInputFrame &frame) {
  return frame.pointer_activity() ||
         !frame.input.pressed_input_chords.empty() ||
         !frame.input.repeated_input_edges.empty() ||
         !frame.input.released_input_chords.empty() ||
         !frame.input.wheel_input_edges.empty();
}

ViewportOverlaySelectionContext
overlay_selection_context(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return ViewportOverlaySelectionContext::Vertex;
  case ModelingSelectionMode::Edge:
    return ViewportOverlaySelectionContext::Edge;
  case ModelingSelectionMode::Face:
    return ViewportOverlaySelectionContext::Face;
  case ModelingSelectionMode::Group:
    return ViewportOverlaySelectionContext::Group;
  case ModelingSelectionMode::Object:
    return ViewportOverlaySelectionContext::Object;
  }
  return ViewportOverlaySelectionContext::Any;
}

void notify_command_failure(QuaderEditorApi &editor,
                            const QuaderApiVoidResult &result) {
  if (!result.ok()) {
    (void)editor.ui().notify(
        {.level = QuaderNotificationLevel::Error,
         .message = result.message.empty() ? "Command failed." : result.message});
  }
}

class ModelingSelectionOverlayFrameProvider final
    : public ViewportOverlaySelectionFrameProvider {
public:
  ModelingSelectionOverlayFrameProvider(
      ModelingSessionService &modeling,
      ModelingSelectionOverlayOptions options)
      : modeling_(modeling), options_(options) {}

  [[nodiscard]] ViewportOverlayFrame
  build_selection_overlay_frame() const override {
    return modeling_.selection_overlay_render_frame(options_);
  }

private:
  ModelingSessionService &modeling_;
  ModelingSelectionOverlayOptions options_;
};

} // namespace

bool qt_event_target_belongs_to_tool_settings(const QObject *target) {
  return qt_object_belongs_to_tool_settings_panel(target) ||
         qt_object_belongs_to_face_texture_panel(target);
}

double viewport_monotonic_time_seconds() {
  return std::chrono::duration<double>(
             ViewportProbeClock::now().time_since_epoch())
      .count();
}

bool qt_event_target_belongs_to_tool_settings(
    const QObject *target, const QPoint &global_position) {
  return qt_event_target_belongs_to_tool_settings(target) ||
         qt_object_belongs_to_tool_settings_panel(target, global_position) ||
         qt_object_belongs_to_face_texture_panel(target, global_position);
}

QtViewportPanel::QtViewportPanel(QWidget *parent, bool composed)
    : QWidget(parent), composed_(composed), delete_popup_presenter_(*this) {
  // Native panes retain their clipping hierarchy without promoting workspace
  // and central-stack widgets into additional windows resized with every dock.
  setAttribute(Qt::WA_DontCreateNativeAncestors);
  setObjectName(QStringLiteral("viewportWorkspace"));
  setMinimumSize(300, 200);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAutoFillBackground(true);

  if (release_has_feature(ReleaseFeature::Environment) ||
      release_has_feature(ReleaseFeature::CameraSettings)) {
    environment_popup_ = new QtEnvironmentSettingsPopup(this);
  }
  panes_[viewport_pane_index(ViewportPaneId::Perspective)].id =
      ViewportPaneId::Perspective;
  panes_[viewport_pane_index(ViewportPaneId::Top)].id = ViewportPaneId::Top;
  panes_[viewport_pane_index(ViewportPaneId::Front)].id = ViewportPaneId::Front;
  panes_[viewport_pane_index(ViewportPaneId::Side)].id = ViewportPaneId::Side;
  for (Pane &pane_state : panes_) {
    create_pane(pane_state);
  }
  focused_pane_frame_ = new QtViewportFocusFrame(this, !composed_);
  focused_pane_frame_->setObjectName(
      QStringLiteral("viewportFocusedPaneFrame"));
  focused_pane_frame_->hide();
  connect(qApp, &QApplication::focusChanged, this,
          [this](QWidget *, QWidget *current) {
            synchronize_control_focus(current);
            update_focused_pane_indicator();
          });
  connect(qApp, &QGuiApplication::focusWindowChanged, this,
          [this](QWindow *current) {
            synchronize_control_focus(current);
            update_focused_pane_indicator();
          });
  for (std::size_t index = 0; index < split_handles_.size(); ++index) {
    SplitHandleHost &handle = split_handles_[index];
    handle.widget = new QWidget(this);
    handle.widget->setObjectName(
        QStringLiteral("viewportSplitHandle_%1").arg(index));
    handle.widget->setMouseTracking(true);
    handle.widget->setAttribute(Qt::WA_NativeWindow, !composed_);
    handle.widget->installEventFilter(this);
    handle.widget->hide();
  }
  refresh_native_geometry_ancestors();
}

QtViewportPanel::~QtViewportPanel() {
  for (const QPointer<QWidget> &ancestor : native_geometry_ancestors_) {
    if (ancestor != nullptr) ancestor->removeEventFilter(this);
  }
  native_geometry_ancestors_.clear();
  set_settings_panel(nullptr);
  shutdown();
  if (composed_) {
    for (Pane &pane_state : panes_) {
      auto *host = static_cast<QtVulkanCompositionWidget *>(
          pane_state.surface_container);
      host->set_prepare_callback({});
      host->set_render_callback({});
      host->set_release_callback({});
    }
  }
}

bool QtViewportPanel::initialize(RenderSystem &render_system) {
  if (initialized_) {
    return true;
  }
  render_system_ = &render_system;
  native_surface_startup_ = {};
  bool success = true;
  for (Pane &pane_state : panes_) {
    pane_state.surface->set_surface_changed_callback([this]() {
      ++composed_geometry_revision_;
      if (presentation_requested_) {
        presentation_requested_();
      }
    });
    if (composed_) {
      auto *host = static_cast<QtVulkanCompositionWidget *>(pane_state.surface_container);
      host->set_render_system(&render_system);
    }
    pane_state.surface_container->hide();
    pane_state.surface_presented = false;
    success =
        pane_state.surface->initialize(
            render_system,
            viewport_grid_layer_mask(viewport_pane_index(pane_state.id))) &&
        success;
  }
  initialized_ = success;
  refresh_workspace_layout();
  return success;
}

void QtViewportPanel::shutdown() {
  presentation_requested_ = {};
  composed_frame_prepare_ = {};
  for (Pane &pane_state : panes_) {
    pane_state.surface->set_surface_changed_callback({});
    if (composed_) {
      auto *host = static_cast<QtVulkanCompositionWidget *>(pane_state.surface_container);
      host->set_render_system(nullptr);
    }
  }
  if (environment_popup_ != nullptr) {
    environment_popup_->abandon_runtime();
  }
  abandon_settings_overlay();
  hide_primitive_feedback();
  set_modal_tool_owns_viewport_snapshot(false);
  for (Pane &pane_state : panes_) {
    pane_state.surface->set_interaction_cursor(ViewportPointerCursor::Default);
  }
  if (interaction_native_tools_ != nullptr) {
    interaction_controller_.reset(interaction_editor_,
                                  *interaction_native_tools_);
  } else {
    interaction_controller_.reset(interaction_editor_);
  }
  interaction_editor_ = nullptr;
  interaction_native_tools_ = nullptr;
  for (Pane &pane_state : panes_) {
    pane_state.camera_settings_popup->close();
    pane_state.layout_menu->close();
  }
  active_tool_revision_valid_ = false;
  cancel_navigation();
  if (render_system_ == nullptr) {
    return;
  }
  for (Pane &pane_state : panes_) {
    pane_state.edge_length_hud->hide();
    pane_state.surface_container->hide();
    pane_state.surface_presented = false;
    pane_state.frame->hide();
    pane_state.frame_presented = false;
    pane_state.overlay_stack_valid = false;
    if (pane_state.surface != nullptr) {
      pane_state.surface->set_pointer_capture(false);
      pane_state.surface->shutdown();
    }
  }
  hide_fly_navigation_huds();
  focused_pane_frame_->hide();
  for (SplitHandleHost &handle : split_handles_) {
    if (handle.visible) {
      handle.widget->hide();
      handle.visible = false;
    }
  }
  last_fly_navigation_active_ = false;
  edge_length_hud_enabled_ = false;
  modal_tool_dragging_ = false;
  linear_array_wheel_remainder_ = 0.0;
  radial_array_wheel_remainder_ = 0.0;
  curve_pick_cache_.reset();
  native_surface_startup_ = {};
  startup_presentation_blocked_ = true;
  initialized_ = false;
  render_system_ = nullptr;
  if (workspace_ != nullptr) {
    workspace_->clear_pane_visibility();
  }
}

void QtViewportPanel::reset_interactions(QuaderEditorApi *editor,
    NativeViewportToolCoordinator *native_tools) {
  pending_marking_menu_gesture_.reset();
  operation_input_context_ = {.viewport_available = false};
  hide_primitive_feedback();
  for (Pane &pane_state : panes_) {
    pane_state.surface->set_interaction_cursor(ViewportPointerCursor::Default);
  }
  if (native_tools != nullptr) {
    interaction_controller_.reset(editor, *native_tools);
  } else {
    interaction_controller_.reset(editor);
  }
  if (interaction_editor_ == editor) {
    interaction_editor_ = nullptr;
    interaction_native_tools_ = nullptr;
    active_tool_revision_valid_ = false;
  }
}

void QtViewportPanel::abandon_interactions() {
  pending_marking_menu_gesture_.reset();
  operation_input_context_ = {.viewport_available = false};
  if (environment_popup_ != nullptr) {
    environment_popup_->abandon_runtime();
  }
  abandon_settings_overlay();
  hide_primitive_feedback();
  ++runtime_binding_generation_;
  interaction_editor_ = nullptr;
  interaction_native_tools_ = nullptr;
  // The previous scene may already have been destroyed by a synchronous
  // project rebind, so this owner-free path must not dereference workspace_.
  // A normal layout/workspace transition cancels the live camera first.
  navigation_camera_owner_.reset();
  input_router_.reset();
  workspace_ = nullptr;
  active_tool_revision_valid_ = false;
  set_modal_tool_owns_viewport_snapshot(false);
  for (Pane &pane_state : panes_) {
    for (QMenu *menu : {pane_state.camera_menu, pane_state.display_menu,
                        pane_state.debug_view_menu, pane_state.layout_menu,
                        pane_state.selection_action != nullptr
                            ? pane_state.selection_action->menu() : nullptr}) {
      if (menu != nullptr) menu->close();
    }
    pane_state.camera_settings_popup->close();
    if (pane_state.surface != nullptr) {
      pane_state.surface->set_interaction_cursor(
          ViewportPointerCursor::Default);
      pane_state.surface->abandon_input();
    }
  }
  for (SplitHandleHost &handle : split_handles_) {
    if (handle.widget != nullptr && QWidget::mouseGrabber() == handle.widget) {
      handle.widget->releaseMouse();
    }
  }
  split_handle_dragging_ = false;
  interaction_controller_.reset();
  interaction_controller_.clear_modal_tool_state();
  active_box_preview_ = {};
  modal_tool_dragging_ = false;
  suppressed_keybinding_chords_.clear();
  repeated_keybinding_commands_.clear();
  command_pointer_release_suppression_ = {};
  linear_array_wheel_remainder_ = 0.0;
  radial_array_wheel_remainder_ = 0.0;
}

void QtViewportPanel::attach_workspace(ViewportWorkspaceService &workspace) {
  interaction_controller_.cancel_pending_material_interaction();
  interaction_controller_.cancel_pending_selection_click();
  if (workspace_ != &workspace) {
    pending_marking_menu_gesture_.reset();
    for (Pane &pane_state : panes_) {
      pane_state.camera_settings_popup->close();
      pane_state.layout_menu->close();
    }
    abandon_settings_overlay();
    hide_primitive_feedback();
    cancel_navigation();
    if (interaction_native_tools_ != nullptr) {
      interaction_controller_.reset(interaction_editor_,
                                    *interaction_native_tools_);
    } else {
      interaction_controller_.reset(interaction_editor_);
    }
    hover_controller_.invalidate();
    curve_pick_cache_.reset();
    point_light_pick_cache_valid_ = false;
    viewport_info_scene_snapshot_valid_ = false;
    viewport_info_scene_stats_valid_ = false;
    displayed_placements_.clear();
    interaction_controller_.clear_modal_drag_state();
    if (interaction_native_tools_ != nullptr) {
      interaction_native_tools_->clear_pointer_drag_state();
    }
    modal_tool_dragging_ = false;
    edge_length_hud_enabled_ = false;
    set_modal_tool_owns_viewport_snapshot(false);
  }
  workspace_ = &workspace;
  for (Pane &pane_state : panes_) {
    pane_state.controls_valid = false;
    pane_state.control_selection_valid = false;
    pane_state.idle_gizmo.reset();
  }
  refresh_workspace_layout();
}

void QtViewportPanel::set_control_actions(const std::vector<QAction *> &actions,
                                          QAction *lasso_action,
                                          QAction *paint_action) {
  for (Pane &pane_state : panes_) {
    if (!pane_state.control_actions.empty()) {
      continue;
    }
    for (QAction *source : actions) {
      if (source == nullptr) {
        pane_state.toolbar->add_separator();
        continue;
      }
      auto *action = new QAction(pane_state.toolbar);
      action->setObjectName(source->objectName());
      action->setCheckable(source->isCheckable());
      const auto synchronize_metadata = [this, &pane_state, action, source]() {
        if (action->text() != source->text()) action->setText(source->text());
        if (action->toolTip() != source->toolTip()) action->setToolTip(source->toolTip());
        if (action->icon().cacheKey() != source->icon().cacheKey()) action->setIcon(source->icon());
        if (action->isEnabled() != source->isEnabled()) action->setEnabled(source->isEnabled());
        if (action->isVisible() != source->isVisible()) action->setVisible(source->isVisible());
        pane_state.controls_valid = false;
        sync_pane_actions(pane_state);
      };
      connect(source, &QAction::changed, action, synchronize_metadata);
      connect(action, &QAction::triggered, this,
              [this, &pane_state, source](bool) {
                if (source->objectName() == QStringLiteral("tool_select") &&
                    workspace_ != nullptr) {
                  const auto preferred = workspace_->preferred_selection_tool(pane_state.id);
                  const std::size_t index = preferred == ViewportToolId::PaintSelect ? 2u :
                      preferred == ViewportToolId::LassoSelect ? 1u : 0u;
                  if (const auto &choice = pane_state.selection_choices[index];
                      choice.source != nullptr) {
                    dispatch_control_action(pane_state, *choice.source);
                    return;
                  }
                }
                dispatch_control_action(pane_state, *source);
              });
      pane_state.control_actions.push_back({source, action});
      if (source->objectName() == QStringLiteral("tool_select")) {
        pane_state.selection_action = action;
        auto *menu = new QMenu(pane_state.toolbar);
        menu->setObjectName(QStringLiteral("viewportSelectionToolMenu_%1").arg(
            QString::fromUtf8(viewport_pane_stable_id(pane_state.id).data())));
        auto *group = new QActionGroup(menu);
        group->setExclusive(true);
        const std::array<QAction *, 3> sources{source, lasso_action, paint_action};
        for (std::size_t index = 0; index < sources.size(); ++index) {
          QAction *choice_source = sources[index];
          if (choice_source == nullptr) continue;
          auto *choice = new QAction(menu);
          choice->setObjectName(index == 0u ? QStringLiteral("selectionBox") :
                               index == 1u ? QStringLiteral("selectionLasso") :
                                             QStringLiteral("selectionPaint"));
          choice->setCheckable(true);
          group->addAction(choice);
          menu->addAction(choice);
          pane_state.selection_choices[index] = {choice_source, choice};
          connect(choice_source, &QAction::changed, choice, [this, &pane_state]() {
            refresh_selection_tool_actions(pane_state);
          });
          connect(choice, &QAction::triggered, this,
                  [this, &pane_state, choice_source]() {
                    dispatch_control_action(pane_state, *choice_source);
                  });
        }
        connect(menu, &QMenu::aboutToShow, this, [this, &pane_state]() {
          request_pane_focus(pane_state.id, ViewportPaneFocusTrigger::FocusIn);
          pane_state.controls_valid = false;
          sync_pane_actions(pane_state);
        });
        action->setMenu(menu);
        refresh_selection_tool_actions(pane_state);
      }
      synchronize_metadata();
      pane_state.toolbar->add_action(*action);
    }
    pane_state.controls_valid = false;
    sync_pane_actions(pane_state);
  }
  update_pane_geometry();
}

void QtViewportPanel::dispatch_control_action(Pane &pane_state, QAction &action) {
  const auto generation = runtime_binding_generation_;
  request_pane_focus(pane_state.id, ViewportPaneFocusTrigger::FocusIn);
  if (generation != runtime_binding_generation_) return;
  if (workspace_ != nullptr && workspace_->focused_pane() == pane_state.id &&
      control_action_requested_) {
    control_action_requested_(pane_state.id, action);
  }
  if (generation != runtime_binding_generation_) return;
  pane_state.controls_valid = false;
  sync_pane_actions(pane_state);
}

void QtViewportPanel::refresh_selection_tool_actions(Pane &pane_state) {
  for (std::size_t index = 0; index < pane_state.selection_choices.size(); ++index) {
    const auto &entry = pane_state.selection_choices[index];
    if (entry.source == nullptr || entry.action == nullptr) continue;
    const QString label = index == 0u
        ? localized_display_menu_label(localization_, "viewport.selection.box", "Box Select")
        : entry.source->text();
    QString tooltip = entry.source->toolTip();
    if (tooltip.startsWith(entry.source->text())) {
      tooltip.replace(0, entry.source->text().size(), label);
    }
    QString menu_text = label;
    if (interaction_editor_ != nullptr) {
      const auto command = index == 0u ? quader_command::kToolsActivateSelect :
                           index == 1u ? quader_command::kToolsActivateLassoSelect :
                                         quader_command::kToolsActivatePaintSelect;
      const std::string shortcut = interaction_editor_->commands().shortcut_for(command);
      if (!shortcut.empty()) menu_text += QLatin1Char('\t') + QString::fromStdString(shortcut);
    }
    if (entry.action->text() != menu_text) entry.action->setText(menu_text);
    if (entry.action->toolTip() != tooltip) entry.action->setToolTip(tooltip);
    if (entry.action->icon().cacheKey() != entry.source->icon().cacheKey())
      entry.action->setIcon(entry.source->icon());
    if (entry.action->isEnabled() != entry.source->isEnabled())
      entry.action->setEnabled(entry.source->isEnabled());
    if (entry.action->isVisible() != entry.source->isVisible())
      entry.action->setVisible(entry.source->isVisible());
  }
  pane_state.controls_valid = false;
  sync_pane_actions(pane_state);
}

void QtViewportPanel::set_control_action_requested_callback(
    std::function<void(ViewportPaneId, QAction &)> callback) {
  control_action_requested_ = std::move(callback);
}

void QtViewportPanel::set_presentation_requested_callback(
    std::function<void()> callback) {
  presentation_requested_ = std::move(callback);
}

void QtViewportPanel::set_composed_frame_prepare_callback(
    std::function<void()> callback) {
  composed_frame_prepare_ = std::move(callback);
  composed_prepared_geometry_revision_ = 0u;
}

bool QtViewportPanel::prepare_composed_frame() {
  if (!composed_ || composed_preparing_ || workspace_ == nullptr ||
      startup_presentation_blocked_ || !composed_frame_prepare_) {
    return false;
  }
  if (composed_prepared_geometry_revision_ == composed_geometry_revision_) {
    return true;
  }
  const QScopedValueRollback preparing_guard(composed_preparing_, true);
  composed_frame_prepare_();
  composed_prepared_geometry_revision_ = composed_geometry_revision_;
  return true;
}

void QtViewportPanel::refresh_control_actions(QuaderEditorApi &editor) {
  if (interaction_editor_ != &editor) {
    refresh_keybinding_presentation(editor);
    for (Pane &pane_state : panes_) pane_state.controls_valid = false;
  }
  for (Pane &pane_state : panes_) {
    sync_pane_actions(pane_state);
    sync_pane_control_selection(pane_state);
  }
}

void QtViewportPanel::sync_pane_actions(Pane &pane_state) {
  if (workspace_ == nullptr) return;
  const auto revision = workspace_->control_revision(pane_state.id);
  if (pane_state.controls_valid && pane_state.controls_revision == revision) return;
  const ViewportSettings &settings = workspace_->effective_settings(pane_state.id);
  const ViewportToolId tool = workspace_->selected_tool(pane_state.id);
  for (const auto &entry : pane_state.control_actions) {
    const QString name = entry.action->objectName();
    std::optional<bool> checked;
    if (name == QStringLiteral("tool_select")) checked =
        tool == ViewportToolId::Select || tool == ViewportToolId::LassoSelect ||
        tool == ViewportToolId::PaintSelect;
    else if (name == QStringLiteral("tool_move")) checked = tool == ViewportToolId::Move;
    else if (name == QStringLiteral("tool_rotate")) checked = tool == ViewportToolId::Rotate;
    else if (name == QStringLiteral("tool_scale")) checked = tool == ViewportToolId::Scale;
    else if (name == QStringLiteral("toggle_transform_space"))
      checked = workspace_->transform_space(pane_state.id) == ViewportTransformSpace::Local;
    else if (name == QStringLiteral("backfaceSelection"))
      checked = settings.bool_value(ViewportBoolSettingId::BackfaceSelection);
    else if (name == QStringLiteral("snappingEnabled")) checked = settings.bool_value(ViewportBoolSettingId::SnappingEnabled);
    else if (name == QStringLiteral("gridSnapping")) checked = settings.bool_value(ViewportBoolSettingId::GridSnap);
    else if (name == QStringLiteral("vertexSnapping")) checked = settings.bool_value(ViewportBoolSettingId::VertexSnap);
    else if (name == QStringLiteral("rotationSnapping")) checked = settings.bool_value(ViewportBoolSettingId::RotationSnap);
    else if (name == QStringLiteral("viewportWireframeOnShaded")) checked = settings.bool_value(ViewportBoolSettingId::WireframeOnShaded);
    else if (name == QStringLiteral("viewportGrid")) checked = settings.bool_value(ViewportBoolSettingId::ShowGrid);
    else if (name == QStringLiteral("viewportGizmos")) checked = settings.bool_value(ViewportBoolSettingId::ShowGizmos);
    else if (name == QStringLiteral("viewportMeshGrid")) checked = settings.bool_value(ViewportBoolSettingId::ShowMeshGrid);
    else if (name == QStringLiteral("viewportOverlays")) checked = settings.bool_value(ViewportBoolSettingId::ShowOverlays);
    if (checked.has_value() && entry.action->isChecked() != *checked) {
      entry.action->setChecked(*checked);
    }
  }
  const auto preferred_tool = workspace_->preferred_selection_tool(pane_state.id);
  const std::size_t preferred_selection = preferred_tool == ViewportToolId::PaintSelect ? 2u :
      preferred_tool == ViewportToolId::LassoSelect ? 1u : 0u;
  for (std::size_t index = 0; index < pane_state.selection_choices.size(); ++index) {
    const auto &entry = pane_state.selection_choices[index];
    if (entry.action == nullptr) continue;
    if (entry.action->isChecked() != (index == preferred_selection))
      entry.action->setChecked(index == preferred_selection);
    if (index == preferred_selection && pane_state.selection_action != nullptr) {
      auto &launcher = *pane_state.selection_action;
      const QString text = entry.action->text().section(QLatin1Char('\t'), 0, 0);
      if (launcher.text() != text) launcher.setText(text);
      if (launcher.toolTip() != entry.action->toolTip()) launcher.setToolTip(entry.action->toolTip());
      if (launcher.icon().cacheKey() != entry.action->icon().cacheKey()) launcher.setIcon(entry.action->icon());
      if (launcher.isEnabled() != entry.action->isEnabled()) launcher.setEnabled(entry.action->isEnabled());
    }
  }
  pane_state.controls_revision = revision;
  pane_state.controls_valid = true;
}

void QtViewportPanel::set_settings_panel(QtToolSettingsPanel *panel) {
  if (settings_panel_ == panel) {
    return;
  }
  if (settings_panel_ != nullptr) {
    settings_panel_->presentation_changed = {};
    settings_panel_->hide();
  }
  settings_panel_ = panel;
  abandon_settings_overlay();
  if (panel == nullptr) {
    return;
  }
  panel->setParent(this);
  panel->setAttribute(Qt::WA_NativeWindow, !composed_);
  panel->hide();
  panel->presentation_changed = [this]() {
    settings_overlay_geometry_dirty_ = true;
    update_settings_overlay_geometry();
  };
}

void QtViewportPanel::abandon_settings_overlay() {
  if (settings_panel_ != nullptr && !settings_panel_->isHidden()) {
    settings_panel_->hide();
  }
  settings_target_id_.clear();
  settings_origin_serial_ = 0;
  settings_origin_pane_.reset();
  settings_host_pane_.reset();
  settings_overlay_geometry_dirty_ = true;
}

void QtViewportPanel::refresh_settings_overlay(QuaderEditorApi &editor) {
  if (settings_panel_ == nullptr || workspace_ == nullptr ||
      !settings_panel_->has_settings()) {
    if (settings_origin_pane_.has_value()) {
      abandon_settings_overlay();
    }
    return;
  }
  const auto &origin = editor.tools().active_settings_presentation_origin();
  const std::string_view target_id = settings_panel_->target_id();
  if (!settings_origin_pane_.has_value() ||
      settings_target_id_ != target_id ||
      settings_origin_serial_ != origin.serial) {
    settings_target_id_ = target_id;
    settings_origin_serial_ = origin.serial;
    settings_origin_pane_ = origin.pane;
    settings_overlay_geometry_dirty_ = true;
  }
  update_settings_overlay_geometry();
}

void QtViewportPanel::update_settings_overlay_geometry() {
  if (settings_panel_ == nullptr) {
    return;
  }
  if (workspace_ == nullptr || startup_presentation_blocked_ ||
      !settings_origin_pane_.has_value() || !settings_panel_->has_settings() ||
      settings_panel_->target_id() != settings_target_id_) {
    if (!settings_panel_->isHidden()) {
      settings_panel_->hide();
    }
    settings_overlay_geometry_dirty_ = true;
    return;
  }

  ViewportPaneId host_id = *settings_origin_pane_;
  if (!pane(host_id).frame_presented) {
    host_id = workspace_->focused_pane();
  }
  if (!pane(host_id).frame_presented) {
    const auto visible = std::find_if(panes_.begin(), panes_.end(),
        [](const Pane &candidate) { return candidate.frame_presented; });
    if (visible == panes_.end()) {
      if (!settings_panel_->isHidden()) {
        settings_panel_->hide();
      }
      settings_overlay_geometry_dirty_ = true;
      return;
    }
    host_id = visible->id;
  }
  if (settings_host_pane_ != host_id) {
    settings_host_pane_ = host_id;
    settings_overlay_geometry_dirty_ = true;
  }
  if (!settings_overlay_geometry_dirty_ && !settings_panel_->isHidden()) {
    return;
  }

  ++settings_overlay_update_count_;
  Pane &host = pane(host_id);
  if (settings_panel_->parentWidget() != host.frame) {
    settings_panel_->setParent(host.frame);
  }
  const QRect bounds = pane_content_rect(host);
  const int inset = std::min({ui_scale_metrics_.px(6),
                              std::max(0, (bounds.width() - 1) / 2),
                              std::max(0, (bounds.height() - 1) / 2)});
  const QSize preferred = settings_panel_->sizeHint();
  const int width = std::clamp(preferred.width(), 1,
                               std::max(1, bounds.width() - 2 * inset));
  const int height = std::clamp(preferred.height(), 1,
                                std::max(1, bounds.height() - 2 * inset));
  const QRect geometry{bounds.left() + inset,
                       bounds.bottom() + 1 - inset - height, width, height};
  if (settings_panel_->geometry() != geometry) {
    settings_panel_->setGeometry(geometry);
  }
  if (settings_panel_->isHidden()) {
    settings_panel_->show();
  }
  settings_panel_->raise();
  settings_overlay_geometry_dirty_ = false;
}

void QtViewportPanel::set_viewport_settings(const ViewportSettings &settings) {
  viewport_settings_ = &settings;
}

void QtViewportPanel::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  if (ui_scale_metrics_.revision() == metrics.revision() &&
      ui_scale_metrics_.factor() == metrics.factor()) {
    return;
  }
  ui_scale_metrics_ = metrics;
  if (environment_popup_ != nullptr) {
    environment_popup_->apply_ui_scale(metrics);
  }
  hide_primitive_feedback();
  context_menu_presenter_.apply_ui_scale(metrics);
  for (Pane &pane_state : panes_) {
    pane_state.fly_navigation_hud->apply_ui_scale(metrics);
    pane_state.edge_length_hud->apply_ui_scale(metrics);
    scale_pane_controls(pane_state);
    pane_state.toolbar->apply_ui_scale(metrics);
    pane_state.overlay_stack_valid = false;
    update_pane_overlay_geometry(pane_state);
  }
  update_pane_geometry();
}

float QtViewportPanel::viewport_raster_scale(ViewportPaneId pane_id) const {
  const QtViewportSurface *pane_surface = surface(pane_id);
  const qreal device_pixel_ratio =
      pane_surface != nullptr ? pane_surface->input_device_pixel_ratio() : 1.0;
  const qreal safe_device_pixel_ratio =
      std::isfinite(device_pixel_ratio) && device_pixel_ratio > 0.0
          ? device_pixel_ratio
          : 1.0;
  return ui_scale_metrics_.factor() *
         static_cast<float>(safe_device_pixel_ratio);
}

void QtViewportPanel::set_render_performance_stats(
    RenderFramePerformanceStats stats) {
  render_performance_ = stats;
}

void QtViewportPanel::set_localization(LocalizationService &localization) {
  hide_primitive_feedback();
  localization_ = &localization;
  viewport_info_last_text_refresh_ = {};
  for (Pane &pane_state : panes_) {
    refresh_pane_controls(pane_state);
  }
}

void QtViewportPanel::prewarm_retained_dialogs() {
  delete_popup_presenter_.prewarm();
}

void QtViewportPanel::set_startup_presentation_blocked(bool blocked) {
  if (startup_presentation_blocked_ == blocked) {
    return;
  }
  startup_presentation_blocked_ = blocked;
  native_surface_startup_ = {};
  if (blocked) {
    hide_primitive_feedback();
    for (Pane &pane_state : panes_) {
      pane_state.surface_container->hide();
      pane_state.surface_presented = false;
      pane_state.frame->hide();
      pane_state.frame_presented = false;
      pane_state.overlay_stack_valid = false;
    }
    focused_pane_frame_->hide();
    for (SplitHandleHost &handle : split_handles_) {
      if (handle.visible) {
        handle.widget->hide();
        handle.visible = false;
      }
    }
    update_settings_overlay_geometry();
    return;
  }
  displayed_placements_.clear();
  refresh_workspace_layout();
}

void QtViewportPanel::refresh_workspace_layout() {
  if (workspace_ == nullptr) {
    return;
  }
  const std::vector<ViewportPanePlacement> placements =
      workspace_->active_pane_placements();
  if (navigation_camera_owner_.has_value() &&
      std::none_of(placements.begin(), placements.end(),
                   [&](const ViewportPanePlacement &placement) {
                     return placement.pane_id == *navigation_camera_owner_;
                   })) {
    cancel_navigation();
  }
  const bool geometry_changed =
      !same_pane_placements(displayed_placements_, placements);
  displayed_layout_ = workspace_->layout();
  if (geometry_changed) {
    qt_invalidate_startup_presentation_geometry(native_surface_startup_);
    displayed_placements_ = placements;
    update_pane_geometry();
  }
  for (Pane &pane_state : panes_) {
    sync_pane_control_selection(pane_state);
    sync_pane_actions(pane_state);
  }
  update_focused_pane_indicator();
  update_settings_overlay_geometry();
}

void QtViewportPanel::process_input(
    QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
    ModelingSessionService &modeling, AssetId default_material,
    NativeViewportToolCoordinator &native_tools,
    ViewportPointPlacementService *point_placement, double delta_seconds) {
  const bool diagnostics_enabled = viewport_frame_diagnostics_enabled();
  const ViewportProbeClock::time_point input_start =
      diagnostics_enabled ? ViewportProbeClock::now()
                          : ViewportProbeClock::time_point{};
  primitive_feedback_input_eligible_ = false;
  pointer_cursor_pane_.reset();
  gizmo_input_pane_.reset();
  operation_input_context_ = {.viewport_available = false};
  process_viewport_input(editor, document, assets, modeling, default_material,
                         native_tools, point_placement, delta_seconds);
  refresh_control_hint_context();
  if (primitive_feedback_input_eligible_ && workspace_ != nullptr) {
    Pane &pane_state = pane(interaction_pane_id_);
    present_primitive_feedback(pane_state,
        interaction_controller_.primitive_creation_controls(
            editor.tools().live_primitive_creation(),
            workspace_->camera(interaction_pane_id_),
            viewport_raster_scale(interaction_pane_id_)));
  } else {
    hide_primitive_feedback();
  }
  update_pointer_cursor_presentation();
  if (diagnostics_enabled) {
    g_viewport_probe.input_ms +=
        elapsed_milliseconds(input_start, ViewportProbeClock::now());
  }
}

void QtViewportPanel::set_primitive_creation_material_provider(
    std::function<QuaderMaterialReference()> provider) {
  primitive_creation_material_provider_ = std::move(provider);
}

void QtViewportPanel::hide_primitive_feedback() {
  for (Pane &pane_state : panes_) {
    if (pane_state.primitive_feedback != nullptr &&
        !pane_state.primitive_feedback->isHidden()) {
      pane_state.primitive_feedback->hide();
    }
    pane_state.primitive_feedback_valid = false;
  }
}

void QtViewportPanel::present_primitive_feedback(
    Pane &pane_state, const PrimitiveCreationControlsFrame &controls) {
  if (!controls.visible || controls.label_key.empty() ||
      !std::isfinite(controls.label_value) ||
      !std::isfinite(controls.label_position.x) ||
      !std::isfinite(controls.label_position.y)) {
    hide_primitive_feedback();
    return;
  }
  const auto locale_revision = localization_ != nullptr ? localization_->revision() : 0u;
  const QRect bounds = pane_state.surface_container->geometry();
  const qreal raw_dpr = pane_state.surface->input_device_pixel_ratio();
  const qreal dpr = std::isfinite(raw_dpr) && raw_dpr > 0.0 ? raw_dpr : 1.0;
  if (pane_state.primitive_feedback_valid &&
      pane_state.primitive_feedback_revision == controls.revision &&
      pane_state.primitive_feedback_locale_revision == locale_revision &&
      pane_state.primitive_feedback_bounds == bounds &&
      pane_state.primitive_feedback_dpr == dpr) {
    return;
  }
  for (Pane &other : panes_) {
    if (&other != &pane_state) {
      if (!other.primitive_feedback->isHidden()) other.primitive_feedback->hide();
      other.primitive_feedback_valid = false;
    }
  }
  pane_state.primitive_feedback_valid = true;
  pane_state.primitive_feedback_revision = controls.revision;
  pane_state.primitive_feedback_locale_revision = locale_revision;
  pane_state.primitive_feedback_bounds = bounds;
  pane_state.primitive_feedback_dpr = dpr;
  ++primitive_feedback_update_count_;

  constexpr std::array<std::pair<std::string_view, std::string_view>, 13> kLabels{{
      {"primitive.controls.width", "Width"},
      {"primitive.controls.height", "Height"},
      {"primitive.controls.depth", "Depth"},
      {"primitive.controls.radius", "Radius"},
      {"primitive.controls.sides", "Sides"},
      {"primitive.controls.radial_segments", "Radial segments"},
      {"primitive.controls.rings", "Rings"},
      {"primitive.controls.subdivision", "Subdivision"},
      {"primitive.controls.outer_radius", "Outer radius"},
      {"primitive.controls.inner_radius", "Inner radius"},
      {"primitive.controls.tube_height", "Tube height"},
      {"primitive.controls.major_segments", "Around Hole"},
      {"primitive.controls.minor_segments", "Around Tube"},
  }};
  const auto found = std::find_if(kLabels.begin(), kLabels.end(), [&](const auto &label) {
    return label.first == controls.label_key;
  });
  const std::string_view fallback = found != kLabels.end() ? found->second : controls.label_key;
  const std::string label = localization_ != nullptr
      ? localization_->text(controls.label_key, fallback) : std::string(fallback);
  const QLocale locale = localization_ != nullptr
      ? QLocale(QString::fromStdString(localization_->current_locale())) : QLocale::c();
  const QString value = controls.label_is_count
      ? locale.toString(static_cast<qlonglong>(std::llround(controls.label_value)))
      : qt_format_number(controls.label_value *
                             viewport_host_defaults().map_units_per_world_unit,
                         locale);
  const QString text = QStringLiteral("%1: %2").arg(QString::fromStdString(label), value);
  QLabel &widget = *pane_state.primitive_feedback;
  const int margin = ui_scale_metrics_.px(4);
  const bool text_changed = widget.text() != text;
  const bool margin_changed = widget.margin() != margin;
  if (text_changed) widget.setText(text);
  if (margin_changed) widget.setMargin(margin);
  if (text_changed || margin_changed) widget.adjustSize();
  // The shared label position is already offset from the control in physical
  // pixels. Convert only DPR; interface scale is already in that position.
  const QPoint position{
      std::clamp(bounds.x() + static_cast<int>(std::lround(controls.label_position.x / dpr)),
                 bounds.x(), std::max(bounds.x(), bounds.right() + 1 - widget.width())),
      std::clamp(bounds.y() + static_cast<int>(std::lround(controls.label_position.y / dpr)),
                 bounds.y(), std::max(bounds.y(), bounds.bottom() + 1 - widget.height()))};
  if (widget.pos() != position) widget.move(position);
  if (widget.isHidden()) {
    widget.show();
    widget.raise();
  }
}

void QtViewportPanel::prepare_viewports(
    QuaderEditorApi &editor, SceneRenderBridge &render_bridge,
    RenderOverlaySnapshotCache &snapshot_cache,
    SceneDocument &document, AssetRegistry &assets,
    ModelingSessionService &modeling,
    NativeViewportToolCoordinator &native_tools) {
  QDR_PROFILE_FUNCTION();
  const QScopedValueRollback preparing_guard(composed_preparing_, true);
  if (environment_popup_ != nullptr) {
    environment_popup_->refresh(editor, document, localization_);
  }
  const bool diagnostics_enabled = viewport_frame_diagnostics_enabled();
  const ViewportProbeClock::time_point prepare_start =
      diagnostics_enabled ? ViewportProbeClock::now()
                          : ViewportProbeClock::time_point{};
  if (workspace_ == nullptr) {
    return;
  }
  if (displayed_layout_ != workspace_->layout()) {
    refresh_workspace_layout();
  }
  update_focused_pane_indicator();
  const std::vector<ViewportPanePlacement> active_placements =
      workspace_->active_pane_placements();

  std::uint8_t active_overlay_viewport_mask = 0u;
  for (const ViewportPanePlacement &placement : active_placements) {
    if (pane(placement.pane_id).frame->isVisible()) {
      active_overlay_viewport_mask = static_cast<std::uint8_t>(
          active_overlay_viewport_mask |
          (1u << viewport_pane_index(placement.pane_id)));
    }
  }
  render_bridge.set_active_overlay_viewports(active_overlay_viewport_mask);

  const ViewportProbeClock::time_point preview_start =
      diagnostics_enabled ? ViewportProbeClock::now()
                          : ViewportProbeClock::time_point{};
  (void)QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(),
                                                         render_bridge);
  const bool viewport_interaction_active =
      input_router_.navigating() || input_router_.fly_navigating() ||
      modal_tool_dragging_ || modeling.transform_drag_active() ||
      modeling.modal_tool_owns_viewport();
  if (!viewport_interaction_active) {
    render_bridge.perform_idle_resource_maintenance();
  }
  if (diagnostics_enabled) {
    g_viewport_probe.preview_sync_ms +=
        elapsed_milliseconds(preview_start, ViewportProbeClock::now());
  }

  // Object-mode face fills remain in the persistent scene cache while the
  // render-owned submission coordinator lazily publishes authored component
  // selection wires from the semantic provider below.
  const ViewportSettings &settings =
      viewport_settings_ != nullptr ? *viewport_settings_ : fallback_settings_;
  const float grid_world_size =
      viewport_grid_world_size_for_preset(workspace_->grid_preset());
  const bool any_overlays_visible = std::any_of(
      active_placements.begin(), active_placements.end(), [&](const auto &placement) {
        return workspace_->effective_settings(placement.pane_id).bool_value(
            ViewportBoolSettingId::ShowOverlays);
      });
  const bool hover_dimensions_visible =
      settings.bool_value(ViewportBoolSettingId::ShowHoverDimensions);
  const bool selection_dimensions_visible =
      settings.bool_value(ViewportBoolSettingId::ShowDimensions);
  const double dimension_font_size_points = static_cast<double>(
      settings.float_value(ViewportFloatSettingId::DimensionFontSize));
  const bool viewport_info_visible =
      settings.bool_value(ViewportBoolSettingId::ShowInfo);
  const bool instance_edit_context_active =
      editor.modeling().instance_edit_state().active;
  const ViewportPaneId focused_pane = workspace_->focused_pane();
  const std::optional<bool> array_dimensions =
      editor.tools().active_array_bounding_box_dimensions();
  const bool pane_dimensions_visible =
      array_dimensions.value_or(selection_dimensions_visible);
  for (Pane &pane_state : panes_) {
    const bool overlays_visible = workspace_->effective_settings(pane_state.id)
        .bool_value(ViewportBoolSettingId::ShowOverlays);
    if ((!viewport_info_visible || pane_state.id != focused_pane) &&
        !pane_state.info_overlay->isHidden()) {
      pane_state.info_overlay->hide();
    }
    const bool footprint_dimensions_visible =
        overlays_visible && pane_state.frame->isVisible() &&
        active_box_preview_.ok &&
        active_box_preview_.drag_state == ModelingBoxDragState::Footprint &&
        active_box_preview_.footprint_count >= 2U;
    if ((!edge_length_hud_enabled_ || !hover_dimensions_visible ||
         !overlays_visible ||
         pane_state.id != interaction_pane_id_ ||
         !pane_state.frame->isVisible()) &&
        !footprint_dimensions_visible &&
        !(pane_dimensions_visible && overlays_visible &&
          pane_state.frame->isVisible())) {
      pane_state.edge_length_hud->hide();
    }
  }
  const NativeViewportToolPresentationSnapshot native_presentation =
      native_tools.presentation_snapshot();
  const PivotHandleState &pivot_handle = native_presentation.pivot_handle;
  const LinearArrayHandleState &linear_array_handle =
      native_presentation.linear_array_handle;
  const RadialArrayCenterState &radial_array_center =
      native_presentation.radial_array_center;
  const auto &extrusion_handle = native_presentation.extrusion_handle;
  const bool pivot_handle_editing =
      pivot_handle.visible && pivot_handle.editing;
  ViewportSettings mesh_grid_settings = settings;
  mesh_grid_settings.set_bool_value(ViewportBoolSettingId::ShowMeshGrid, std::any_of(
      active_placements.begin(), active_placements.end(), [&](const auto &placement) {
        return workspace_->effective_settings(placement.pane_id).bool_value(
            ViewportBoolSettingId::ShowMeshGrid);
      }));
  render_bridge.update_mesh_surface_grid(grid_world_size, mesh_grid_settings);

  const ModelingSelectionMode cached_overlay_mode = modeling.selection_mode();
  const auto live_primitive_creation = editor.tools().live_primitive_creation();
  const ModelingSelectionOverlayOptions selection_overlay_options{
      .suppress_persistent_object_selection =
          cached_overlay_mode == ModelingSelectionMode::Object,
  };
  const ViewportLocalVisibilitySnapshot local_visibility =
      workspace_->local_visibility_snapshot();
  const ViewportOverlaySelectionSourceStamp selection_source{
      .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
      .content_revision = modeling.content_revision(),
      .selection_revision = modeling.selection_revision(),
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .local_visibility_revision = local_visibility.revision(),
      .overlay_signature =
          modeling.selection_overlay_signature(selection_overlay_options),
      .selection_context = overlay_selection_context(cached_overlay_mode),
  };
  const ModelingSelectionOverlayFrameProvider selection_frame_provider{
      modeling, selection_overlay_options};
  ViewportPersistentOverlayRasterGroupSet persistent_raster_groups;
  for (const ViewportPanePlacement &placement : active_placements) {
    const std::uint8_t pane_layer_mask =
        viewport_grid_layer_mask(viewport_pane_index(placement.pane_id));
    const bool accepted = persistent_raster_groups.add(
        pane_layer_mask,
        ViewportOverlayStyleResolver::persistent_style_snapshot(
            settings, viewport_raster_scale(placement.pane_id)));
    Q_ASSERT(accepted);
    (void)accepted;
  }
  // The shared snapshot keeps authored/color identity independent of a pane.
  // Physical widths are bound by the per-pane raster groups below.
  const ViewportPersistentOverlayStyleSnapshot persistent_overlay_style =
      ViewportOverlayStyleResolver::persistent_style_snapshot(
          settings, 1.0f);
  std::uint8_t wireframe_layer_mask = 0u;
  std::uint8_t wireframe_on_shaded_layer_mask = 0u;
  for (const ViewportPanePlacement &placement : active_placements) {
    const ViewportShadingMode shading = workspace_->shading_mode(placement.pane_id);
    if (shading == ViewportShadingMode::Wireframe) {
      wireframe_layer_mask = static_cast<std::uint8_t>(
          wireframe_layer_mask |
          viewport_grid_layer_mask(viewport_pane_index(placement.pane_id)));
    } else if ((shading == ViewportShadingMode::Solid ||
                shading == ViewportShadingMode::Shaded ||
                shading == ViewportShadingMode::Rendered) &&
               workspace_->effective_settings(placement.pane_id).bool_value(
                   ViewportBoolSettingId::WireframeOnShaded)) {
      wireframe_on_shaded_layer_mask = static_cast<std::uint8_t>(
          wireframe_on_shaded_layer_mask |
          viewport_grid_layer_mask(viewport_pane_index(placement.pane_id)));
    }
  }
  const bool selected_mesh_outline_only = !live_primitive_creation &&
      (cached_overlay_mode == ModelingSelectionMode::Object ||
       cached_overlay_mode == ModelingSelectionMode::Group);
  (void)QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays(
      editor.render(), render_bridge, snapshot_cache,
      QuaderRenderSceneOverlaySyncRequest{
          .selected_object_overlay_visible =
              any_overlays_visible && selected_mesh_outline_only,
          .scene_wireframe_layer_mask = wireframe_layer_mask,
          .wireframe_on_shaded_layer_mask = wireframe_on_shaded_layer_mask,
          .selected_scene_wire_visible = false,
          .selected_mesh_outline_only = selected_mesh_outline_only,
          .style = persistent_overlay_style,
          .raster_groups = persistent_raster_groups,
          .collision_visible =
              any_overlays_visible &&
              settings.bool_value(ViewportBoolSettingId::ShowCollisions),
          .collision_color = settings.color(ViewportColorSettingId::CollisionFill),
      });

  std::array<bool, kViewportPaneCount> scene_overlay_requests = {};
  for (const ViewportPanePlacement &placement : active_placements) {
    const Pane &pane_state = pane(placement.pane_id);
    if (!pane_state.frame->isVisible()) {
      continue;
    }
    const ViewportShadingMode shading =
        workspace_->shading_mode(placement.pane_id);
    const ViewportSettings &pane_settings = workspace_->effective_settings(placement.pane_id);
    const bool overlays_visible = pane_settings.bool_value(ViewportBoolSettingId::ShowOverlays);
    const bool gizmos_visible = pane_settings.bool_value(ViewportBoolSettingId::ShowGizmos);
    const bool point_light_gizmos_visible =
        gizmos_visible && viewport_shading_shows_light_gizmos(shading);
    scene_overlay_requests[viewport_pane_index(placement.pane_id)] =
        overlays_visible ||
        point_light_gizmos_visible || assets.curve_count() != 0u;
  }
  const RenderOverlaySelectionSnapshot *scene_overlay_snapshot = nullptr;
  if (viewport_scene_overlay_snapshot_extraction_count(
          scene_overlay_requests) != 0u) {
    scene_overlay_snapshot =
        &QuaderRenderApiRuntimeAccess::build_scene_overlay_snapshot(
            editor.render(), snapshot_cache);
  }
  // This O(1) shared snapshot pins the immutable modeling publication across
  // every pane submission in this presentation pass.
  const ModelingVolatileOverlaySnapshot volatile_modeling_overlay =
      any_overlays_visible ? modeling.volatile_overlay_snapshot()
                       : ModelingVolatileOverlaySnapshot{};
  const quader_editor_core::TransformGizmoFrame *interaction_gizmo =
      interaction_controller_.transform_gizmo_frame();
  const ViewportInteractionSelectionState gizmo_selection{
      .summary = editor.selection().summary(),
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .pivot_revision = native_presentation.selection_pivot_revision,
  };
  const std::vector<quader_editor_core::SelectionOverlayPoint> &
      transform_snap_points =
          interaction_controller_.transform_gizmo_snap_overlay_points();
  const ViewportSelectionMarquee &selection_marquee =
      interaction_controller_.selection_marquee();
  const ViewportToolOverlaySourceRevisions dynamic_tool_revisions{
      .active_tool = editor.tools().active_tool_revision(),
      .modeling_content = modeling.content_revision(),
      .modeling_selection = modeling.selection_revision(),
      .modeling_transform_preview =
          modeling.transient_transform_preview_revision(),
      .scene_transform = document.transform_revision(),
      .scene_selection = document.selection_revision(),
      .transform_space = editor.viewports().transform_space_revision(),
      .selection_pivot = native_presentation.selection_pivot_revision,
      .curve_preview = native_presentation.curve_preview != nullptr
                           ? native_presentation.curve_preview->revision
                           : 0u,
  };
  const ViewportOverlayFrame empty_edge_hud_frame;

  Pane &primitive_input_pane = pane(interaction_pane_id_);
  primitive_input_pane.surface->synchronize_render_size();
  workspace_->set_pane_render_size(interaction_pane_id_,
      primitive_input_pane.surface->render_size_pixels());
  workspace_->camera(interaction_pane_id_).set_clip_planes(
      document.scene_settings().camera.near_plane,
      document.scene_settings().camera.far_plane);
  const PrimitiveCreationControlsFrame &shared_primitive_controls =
      interaction_controller_.primitive_creation_controls(
          live_primitive_creation, workspace_->camera(interaction_pane_id_),
          viewport_raster_scale(interaction_pane_id_));

  workspace_->clear_pane_visibility();
  for (const ViewportPanePlacement &placement : active_placements) {
    Pane &pane_state = pane(placement.pane_id);
    if (!pane_state.frame->isVisible()) {
      continue;
    }
    workspace_->set_pane_visible(pane_state.id, true);
    // The QWidget container owns native-child geometry, but Qt can defer the
    // corresponding QWindow resize event. Synchronize Filament's viewport
    // before camera projection and grid generation consume its dimensions.
    pane_state.surface->synchronize_render_size();
    const QVec2 render_size = pane_state.surface->render_size_pixels();
    workspace_->set_pane_render_size(pane_state.id, render_size);
    ViewportWorkspacePaneState &pane_workspace =
        workspace_->pane(pane_state.id);
    const ViewportSettings &pane_settings = workspace_->effective_settings(pane_state.id);
    const bool grid_visible = pane_settings.bool_value(ViewportBoolSettingId::ShowGrid);
    const bool overlays_visible = pane_settings.bool_value(ViewportBoolSettingId::ShowOverlays);
    const bool gizmos_visible = pane_settings.bool_value(ViewportBoolSettingId::ShowGizmos);
    const ViewportShadingMode shading =
        workspace_->shading_mode(pane_state.id);
    ViewportRenderer &renderer = pane_state.surface->renderer();
    renderer.set_instance_edit_context_active(
        instance_edit_context_active, pane_settings);
    renderer.set_material_presentation(
        viewport_material_debug_mode(shading),
        shading == ViewportShadingMode::Solid,
        pane_settings.bool_value(ViewportBoolSettingId::ShowMeshGrid));

    const SceneSettings &scene_settings = document.scene_settings();
    pane_workspace.camera.set_clip_planes(scene_settings.camera.near_plane,
                                          scene_settings.camera.far_plane);
    renderer.apply_scene_settings(
        scene_settings, viewport_shading_uses_scene_lighting(shading), pane_settings);
    renderer.update_camera(pane_workspace.camera);
    if (pane_state.id == focused_pane) {
      const bool selection_depth_eligible = !viewport_interaction_active &&
          (cached_overlay_mode == ModelingSelectionMode::Vertex || cached_overlay_mode == ModelingSelectionMode::Face) &&
          !pane_settings.bool_value(ViewportBoolSettingId::BackfaceSelection);
      QuaderRenderApiRuntimeAccess::prepare_selection_depth(editor.render(), renderer,
          pane_workspace.camera, selection_depth_eligible);
    }
    renderer.update_ground_grid(pane_workspace.camera, grid_world_size,
                                pane_settings, grid_visible);
    renderer.set_scene_layer_mask(
        viewport_shading_uses_rendered_materials(shading)
            ? kViewportRenderedSceneLayerMask
            : kViewportShadedSceneLayerMask);
    renderer.set_shadowing_enabled(
        viewport_shading_uses_scene_lighting(shading));
    renderer.set_scene_visible(shading != ViewportShadingMode::Wireframe);
    const bool tool_overlay_visible =
        active_box_preview_.ok &&
        active_box_preview_.footprint_count >= 2U &&
        active_box_preview_.drag_state == ModelingBoxDragState::Footprint;
    const bool radial_array_center_visible =
        should_show_point_placement_overlay(
            radial_array_center.visible, radial_array_center.locked,
            true);
    const bool primitive_creation_visible = gizmos_visible && live_primitive_creation.has_value();
    renderer.set_overlay_visible(should_enable_viewport_overlay_pass({
        .overlays_visible = overlays_visible,
        .gizmos_visible = gizmos_visible,
        .wireframe_pane = shading == ViewportShadingMode::Wireframe,
        .pivot_handle_visible = pivot_handle.visible,
        .native_tool_overlay_visible =
            linear_array_handle.visible || radial_array_center_visible ||
            extrusion_handle.active ||
            (native_presentation.curve_preview != nullptr &&
             native_presentation.curve_preview->active) ||
            assets.curve_count() != 0u,
        .tool_overlay_visible = tool_overlay_visible || primitive_creation_visible,
    }));

    const ViewportOverlayCamera overlay_camera =
        ViewportCameraAdapter::overlay_camera(pane_workspace.camera,
                                              pane_workspace.render_size);
    if (viewport_info_visible && pane_state.id == focused_pane) {
      draw_viewport_info_overlay(pane_state, document, assets, overlay_camera);
    }
    const bool point_light_gizmos_visible =
        gizmos_visible && viewport_shading_shows_light_gizmos(shading);
    const float pane_viewport_pixel_scale =
        viewport_raster_scale(pane_state.id);
    const ViewportToolId pane_tool = workspace_->selected_tool(pane_state.id);
    const bool uses_interaction_gizmo = pane_state.id == interaction_pane_id_ &&
        (gizmo_input_pane_ == pane_state.id ||
         interaction_controller_.transform_gizmo_pointer_active()) &&
        editor.tools().pane_owns_executing_tool(pane_state.id) &&
        input_router_.state().active_tool == pane_tool && interaction_gizmo != nullptr;
    const auto *transform_gizmo = uses_interaction_gizmo ? interaction_gizmo :
        pane_state.idle_gizmo.prepare(editor, {
            .camera = pane_workspace.camera,
            .settings = pane_settings,
            .selection_state = gizmo_selection,
            .tool = pane_tool,
            .transform_space = editor.viewports().transform_space(pane_state.id),
            .grid_preset = workspace_->grid_preset(),
            .raster_scale = pane_viewport_pixel_scale,
            .transform_preview_revision = modeling.transient_transform_preview_revision(),
            .pane_settings_revision = workspace_->control_revision(pane_state.id),
            .source_identity = reinterpret_cast<std::uintptr_t>(&document),
            .curve_tool = &native_tools.curve_interaction(),
            .curve_control_route_available = native_presentation.curve_controls_editable,
            .shared_interaction_frame = interaction_gizmo,
        });
    ViewportToolOverlaySourceRevisions pane_tool_revisions = dynamic_tool_revisions;
    pane_tool_revisions.active_tool = workspace_->control_revision(pane_state.id);
    pane_tool_revisions.transform_space = workspace_->transform_space_revision(pane_state.id);
    const PrimitiveCreationControlsFrame *primitive_creation_controls = nullptr;
    if (gizmos_visible) {
      primitive_creation_controls = pane_state.id == interaction_pane_id_
          ? &shared_primitive_controls
          : &pane_state.primitive_presentation.project_presentation(
              ViewportPrimitiveCreationApiAdapter::from_api(live_primitive_creation),
              pane_workspace.camera, pane_viewport_pixel_scale,
              shared_primitive_controls);
    }
    if (primitive_feedback_input_eligible_ && primitive_creation_controls != nullptr) {
      present_primitive_feedback(pane_state, *primitive_creation_controls);
    }
    const ViewportToolOverlaySource dynamic_tool_frame_provider{
        ViewportToolOverlaySourceInputs{
            .source_identity = reinterpret_cast<std::uintptr_t>(
                &interaction_controller_),
            .raster_scale = pane_viewport_pixel_scale,
            .overlay_camera = &overlay_camera,
            .viewport_camera = &pane_workspace.camera,
            .box_preview = &active_box_preview_,
            .primitive_creation_controls = primitive_creation_controls,
            .transform_gizmo = transform_gizmo,
            .transform_snap_points = uses_interaction_gizmo ? &transform_snap_points : nullptr,
            .selection_marquee = &selection_marquee,
            .pivot_handle = &pivot_handle,
            .linear_array_handle = &linear_array_handle,
            .extrusion_handle = &extrusion_handle,
            .radial_array_center = &radial_array_center,
            .revisions = pane_tool_revisions,
            .interaction_pane = pane_state.id == interaction_pane_id_,
            .tool_overlay_visible = tool_overlay_visible,
            .gizmos_visible = gizmos_visible,
            .pivot_handle_editing = pivot_handle_editing,
            .overlays_visible = overlays_visible,
            .radial_array_center_visible = radial_array_center_visible,
        }};

    const std::size_t viewport_index = viewport_pane_index(pane_state.id);
    const std::uint8_t layer_mask =
        viewport_grid_layer_mask(viewport_index);
    const bool scene_overlay_requested =
        scene_overlay_requests[viewport_index];
    const bool retained_mesh_outlines =
        render_bridge.tool_preview_suppression().empty();
    const bool gpu_mesh_outlines = renderer.update_selected_mesh_outlines(
        selected_mesh_outline_only ? snapshot_cache.selected_object_snapshot() : nullptr,
        selected_mesh_outline_only ? scene_overlay_snapshot : nullptr,
        ViewportOverlayStyleResolver::persistent_style_snapshot(
            pane_settings, pane_viewport_pixel_scale),
        selected_mesh_outline_only && overlays_visible && retained_mesh_outlines) &&
        retained_mesh_outlines;
    ViewportOverlaySubmissionResult overlay_submission =
        render_bridge.submit_viewport_overlay(
            ViewportOverlaySubmissionRequest{
                .viewport_index = viewport_index,
                .layer_mask = layer_mask,
                .raster_scale = pane_viewport_pixel_scale,
                .construction_object_id = live_primitive_creation
                    ? live_primitive_creation->object_id : -1,
                .camera = overlay_camera,
                .selection_source = selection_source,
                .selection_frame_provider = &selection_frame_provider,
                .scene_overlay_snapshot = scene_overlay_snapshot,
                .selected_mesh_outline_snapshot =
                    selected_mesh_outline_only
                        ? snapshot_cache.selected_object_snapshot() : nullptr,
                .gpu_mesh_outlines = gpu_mesh_outlines,
                .volatile_modeling_overlay_frame =
                    volatile_modeling_overlay.frame.get(),
                .volatile_modeling_source = {
                    .source_identity =
                        volatile_modeling_overlay.source.source_identity,
                    .publication_revision =
                        volatile_modeling_overlay.source.publication_revision,
                },
                .curve_preview = native_presentation.curve_preview,
                .dynamic_tool_source =
                    dynamic_tool_frame_provider.source_stamp(),
                .dynamic_tool_frame_provider =
                    &dynamic_tool_frame_provider,
                .scene_selection_mode =
                    ViewportToolOverlayProjection::selection_mode(
                        cached_overlay_mode),
                .style_selection_context =
                    overlay_selection_context(cached_overlay_mode),
                .overlays_visible = overlays_visible,
                .scene_overlay_requested = scene_overlay_requested,
                .wireframe_pane =
                    shading == ViewportShadingMode::Wireframe,
                .point_light_gizmos_visible = point_light_gizmos_visible,
                .selection_dimensions_visible = selection_dimensions_visible,
                .array_dimensions_mode = !array_dimensions
                    ? ViewportArrayDimensionsMode::Inactive
                    : *array_dimensions ? ViewportArrayDimensionsMode::Visible
                                        : ViewportArrayDimensionsMode::Hidden,
            },
            pane_settings);
    pane_state.edge_length_hud->present(
        overlay_submission.edge_hud_frame != nullptr
            ? *overlay_submission.edge_hud_frame
            : empty_edge_hud_frame,
        overlay_submission.dynamic_source_signature,
        pane_workspace.camera, pane_state.surface_container->geometry(),
        edge_length_hud_enabled_ && hover_dimensions_visible &&
            overlays_visible &&
            cached_overlay_mode == ModelingSelectionMode::Edge &&
            pane_state.id == interaction_pane_id_ &&
            !viewport_interaction_active,
        dimension_font_size_points,
        &active_box_preview_, overlays_visible && tool_overlay_visible,
        overlay_submission.selection_dimension_bounds
            ? &*overlay_submission.selection_dimension_bounds : nullptr,
        overlay_submission.selection_dimension_signature,
        pane_dimensions_visible && overlays_visible);
    sync_pane_control_selection(pane_state);
  }
  composed_prepared_geometry_revision_ = composed_geometry_revision_;
  if (diagnostics_enabled) {
    g_viewport_probe.prepare_ms +=
        elapsed_milliseconds(prepare_start, ViewportProbeClock::now());
    ++g_viewport_probe.frames;
  }
}

std::size_t
QtViewportPanel::render_viewports(bool account_presentation_frame) {
  QDR_PROFILE_FUNCTION();
  const bool diagnostics_enabled = viewport_frame_diagnostics_enabled();
  const ViewportProbeClock::time_point render_start =
      diagnostics_enabled ? ViewportProbeClock::now()
                          : ViewportProbeClock::time_point{};
  if (!initialized_ || startup_presentation_blocked_ || workspace_ == nullptr ||
      render_system_ == nullptr) {
    return 0u;
  }
  if (composed_) {
    for (const ViewportPanePlacement &placement :
         workspace_->active_pane_placements()) {
      Pane &pane_state = pane(placement.pane_id);
      if (!workspace_->pane_visible(pane_state.id)) {
        continue;
      }
      update_pane_overlay_geometry(pane_state);
      if (!pane_state.surface_presented) {
        pane_state.surface_presented = true;
        pane_state.surface_container->show();
        pane_state.overlay_stack_valid = false;
      }
      (void)pane_state.surface->render_frame();
    }
    // The common Qt flush accounts submissions after the widget paints.
    return 0u;
  }
  const qreal pixel_scale = devicePixelRatioF();
  const int host_width_pixels =
      static_cast<int>(std::lround(width() * pixel_scale));
  const int host_height_pixels =
      static_cast<int>(std::lround(height() * pixel_scale));
  const QWindow *host_window =
      window() != nullptr ? window()->windowHandle() : nullptr;
  const std::uint64_t geometry_signature =
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(
           host_width_pixels))
       << 32u) |
      static_cast<std::uint32_t>(host_height_pixels);
  const QtStartupPresentationAction startup_action =
      qt_startup_presentation_action(
          native_surface_startup_,
          host_window != nullptr && host_window->isExposed(),
          geometry_signature);
  if (!qt_startup_native_child_should_be_visible(startup_action)) {
    for (Pane &pane_state : panes_) {
      if (pane_state.surface_presented) {
        pane_state.surface_container->hide();
        pane_state.surface_presented = false;
        pane_state.overlay_stack_valid = false;
      }
    }
    return 0u;
  }
  if (startup_action == QtStartupPresentationAction::GeometryStabilized) {
    for (Pane &pane_state : panes_) {
      if (!pane_state.surface_presented) {
        pane_state.surface_container->show();
        // Reassert both native hosts before yielding the exposure turn.
        pane_state.surface_container->setGeometry(pane_content_rect(pane_state));
        pane_state.surface_backing_container->setGeometry(
            pane_state.surface_backing_container->geometry());
        pane_state.surface_presented = true;
        pane_state.overlay_stack_valid = false;
      }
      update_pane_overlay_geometry(pane_state);
    }
    update_focused_pane_indicator();
    return 0u;
  }
  if (account_presentation_frame) {
    render_system_->begin_presentation_frame();
  }
  const std::vector<ViewportPanePlacement> active_placements =
      workspace_->active_pane_placements();
  const std::size_t submitted = qt_submit_viewport_render_frames(
      active_placements, input_router_.navigating(), interaction_pane_id_,
      [this](const ViewportPanePlacement &placement) {
        Pane &pane_state = pane(placement.pane_id);
        return workspace_->pane_visible(pane_state.id) &&
               pane_state.surface->render_frame();
      });
  if (account_presentation_frame) {
    render_system_->complete_presentation_frame(submitted);
  }
  if (diagnostics_enabled) {
    const ViewportProbeClock::time_point now = ViewportProbeClock::now();
    g_viewport_probe.render_ms += elapsed_milliseconds(render_start, now);
    g_viewport_probe.submitted += submitted;
    const double window_seconds =
        std::chrono::duration<double>(now - g_viewport_probe.window_start)
            .count();
    if (window_seconds >= 2.0 && g_viewport_probe.frames > 0u) {
      const double frames = static_cast<double>(g_viewport_probe.frames);
      LOG_INFO("Qt viewport cadence fps={:.1f} mouse_events={} "
               "motion_frames={} submitted_per_frame={:.2f} "
               "avg_input_ms={:.3f} avg_preview_sync_ms={:.3f} "
               "avg_prepare_ms={:.3f} avg_render_ms={:.3f}",
               frames / window_seconds, g_viewport_probe.mouse_events,
               g_viewport_probe.motion_frames,
               static_cast<double>(g_viewport_probe.submitted) / frames,
               g_viewport_probe.input_ms / frames,
               g_viewport_probe.preview_sync_ms / frames,
               g_viewport_probe.prepare_ms / frames,
               g_viewport_probe.render_ms / frames);
      g_viewport_probe = ViewportCadenceProbe{};
    }
  }
  return submitted;
}

QuaderApiVoidResult QtViewportPanel::frame_all(const SceneDocument &document,
                                               const AssetRegistry &assets,
                                               ViewportPaneId pane_id) {
  QDR_PROFILE_SCOPE("Viewport frame all");
  if (workspace_ == nullptr) {
    return {.ok = false,
            .changed = false,
            .message = "Viewport workspace is not attached."};
  }
  const ViewportLocalVisibilitySnapshot local_visibility =
      workspace_->local_visibility_snapshot();
  (void)workspace_->camera(pane_id).focus_optional_bounds(
      ViewportSceneProjection::world_bounds(document, assets,
                                            local_visibility),
      ViewportEmptyFocusBehavior::FocusOrigin);
  return {.ok = true,
          .changed = true,
          .message = "Focus All",
          .mutation_kind = QuaderMutationKind::Runtime};
}

QuaderApiVoidResult
QtViewportPanel::frame_selection(const ModelingSessionService &modeling,
                                 ViewportPaneId pane_id) {
  if (workspace_ == nullptr) {
    return {.ok = false,
            .changed = false,
            .message = "Viewport workspace is not attached."};
  }
  const std::optional<ViewportFocusBounds> bounds =
      ViewportSceneProjection::selection_bounds(modeling);
  if (!bounds.has_value()) {
    return {
        .ok = false, .changed = false, .message = "Select something to focus."};
  }
  // Blender's Frame Selected uses a deliberate 1.4 framing margin. Keep the
  // selected geometry comfortably readable while the camera owner performs
  // the tighter view-oriented bounds fit.
  (void)workspace_->camera(pane_id).focus_optional_bounds(
      bounds, ViewportEmptyFocusBehavior::KeepCamera, 1.4f);
  return {.ok = true,
          .changed = true,
          .message = "Focus Selection",
          .mutation_kind = QuaderMutationKind::Runtime};
}

bool QtViewportPanel::needs_continuous_rendering() const {
  // The Filament child swapchains are interactive editor surfaces, not static
  // document previews. Keep the app on its active timer while the workspace is
  // visible so the first pointer event and subsequent navigation are never
  // quantized by the 100 ms idle timer.
  return initialized_ && isVisible() &&
         (window() == nullptr || !window()->isMinimized());
}

bool QtViewportPanel::focus_active_pane() {
  if (workspace_ == nullptr) {
    return false;
  }
  request_pane_focus(workspace_->focused_pane(),
                     ViewportPaneFocusTrigger::Programmatic);
  return true;
}

QuaderViewportKeybindingContext QtViewportPanel::command_input_context() {
  const bool active_tool_owns_viewport =
      workspace_ != nullptr && interaction_editor_ != nullptr &&
      interaction_editor_->tools().pane_owns_executing_tool(workspace_->focused_pane()) &&
      interaction_controller_.active_tool_owns_viewport(
          workspace_->selected_tool(workspace_->focused_pane()));
  return {
      .orthographic_view =
          workspace_ != nullptr &&
          workspace_->camera(workspace_->focused_pane()).state().projection ==
              ViewportProjection::Orthographic,
      .navigation_active = input_router_.navigating(),
      .fly_navigation_active = input_router_.fly_navigating(),
      .orbit_navigation_active =
          input_router_.orbit_navigating(),
      .pan_navigation_active = input_router_.pan_navigating(),
      .modal_tool_active = modal_tool_owns_viewport_snapshot_,
      .active_tool_owns_viewport = active_tool_owns_viewport,
      .active_transform_drag = interaction_controller_.dragging(),
  };
}

QtViewportSurface *QtViewportPanel::surface(ViewportPaneId pane_id) {
  return pane(pane_id).surface;
}

const QtViewportSurface *
QtViewportPanel::surface(ViewportPaneId pane_id) const {
  return pane(pane_id).surface;
}

void QtViewportPanel::refresh_native_geometry_ancestors() {
  for (const QPointer<QWidget> &ancestor : native_geometry_ancestors_) {
    if (ancestor != nullptr) ancestor->removeEventFilter(this);
  }
  native_geometry_ancestors_.clear();
  for (QWidget *ancestor = parentWidget(); ancestor != nullptr;
       ancestor = ancestor->parentWidget()) {
    native_geometry_ancestors_.emplace_back(ancestor);
    ancestor->installEventFilter(this);
    if (ancestor->isWindow()) break;
  }
}

void QtViewportPanel::synchronize_native_ancestor_position() {
  if (composed_) {
    return;
  }
  QWindow *native_window = windowHandle();
  QWidget *native_parent = nativeParentWidget();
  if (native_window == nullptr || native_window->handle() == nullptr ||
      native_parent == nullptr || native_parent->windowHandle() == nullptr) {
    return;
  }
  // Alien ancestor moves do not change QWidget-local pane geometry. Place the
  // retained native window in its real parent's coordinates without resizing it.
  const QPoint native_position = mapTo(native_parent, QPoint{});
  if (native_window->position() != native_position) {
    native_window->setPosition(native_position);
  }
}

bool QtViewportPanel::event(QEvent *event) {
  const bool handled = QWidget::event(event);
  if (event != nullptr && event->type() == QEvent::ParentChange) {
    refresh_native_geometry_ancestors();
    synchronize_native_ancestor_position();
  }
  return handled;
}

bool QtViewportPanel::eventFilter(QObject *watched, QEvent *event) {
  if (event != nullptr &&
      (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
       event->type() == QEvent::ParentChange) &&
      std::any_of(native_geometry_ancestors_.begin(),
                  native_geometry_ancestors_.end(),
                  [watched](const QPointer<QWidget> &ancestor) {
                    return ancestor.data() == watched;
                  })) {
    if (event->type() == QEvent::ParentChange) {
      refresh_native_geometry_ancestors();
    }
    synchronize_native_ancestor_position();
  }
  if (event != nullptr) {
    for (const Pane &candidate : panes_) {
      if ((watched == candidate.surface ||
           (composed_ && watched == candidate.surface_container)) &&
          candidate.id == interaction_pane_id_ &&
          (event->type() == QEvent::FocusOut ||
           event->type() == QEvent::Hide ||
           (event->type() == QEvent::Leave &&
            !interaction_controller_.primitive_creation_pointer_active()))) {
        primitive_feedback_input_eligible_ = false;
        hide_primitive_feedback();
        break;
      }
    }
  }
  if (event == nullptr ||
      (event->type() != QEvent::MouseButtonPress &&
       event->type() != QEvent::MouseButtonDblClick &&
       event->type() != QEvent::MouseButtonRelease &&
       event->type() != QEvent::MouseMove)) {
    return QWidget::eventFilter(watched, event);
  }
  auto found = std::find_if(
      split_handles_.begin(), split_handles_.end(),
      [&](const SplitHandleHost &host) { return host.widget == watched; });
  if (found == split_handles_.end()) {
    return QWidget::eventFilter(watched, event);
  }

  auto update_from_mouse = [&](const QMouseEvent &mouse) {
    if (workspace_ == nullptr) {
      return;
    }
    const QPoint local = mapFromGlobal(mouse.globalPosition().toPoint());
    const float normalized =
        found->descriptor.axis == ViewportSplitHandleAxis::Vertical
            ? static_cast<float>(local.x()) /
                  static_cast<float>(std::max(1, width()))
            : static_cast<float>(local.y()) /
                  static_cast<float>(std::max(1, height()));
    workspace_->set_split_handle_position(active_split_handle_, normalized);
    update_pane_geometry();
  };

  switch (event->type()) {
  case QEvent::MouseButtonPress: {
    auto &mouse = *static_cast<QMouseEvent *>(event);
    if (mouse.button() != Qt::LeftButton || !found->visible) {
      break;
    }
    active_split_handle_ = found->descriptor.id;
    split_handle_dragging_ = true;
    found->widget->grabMouse();
    update_from_mouse(mouse);
    mouse.accept();
    return true;
  }
  case QEvent::MouseMove: {
    if (!split_handle_dragging_) {
      break;
    }
    auto &mouse = *static_cast<QMouseEvent *>(event);
    update_from_mouse(mouse);
    mouse.accept();
    return true;
  }
  case QEvent::MouseButtonRelease: {
    auto &mouse = *static_cast<QMouseEvent *>(event);
    if (!split_handle_dragging_ || mouse.button() != Qt::LeftButton) {
      break;
    }
    update_from_mouse(mouse);
    split_handle_dragging_ = false;
    if (QWidget::mouseGrabber() == found->widget) {
      found->widget->releaseMouse();
    }
    mouse.accept();
    return true;
  }
  default:
    break;
  }
  return QWidget::eventFilter(watched, event);
}

void QtViewportPanel::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  qt_invalidate_startup_presentation_geometry(native_surface_startup_);
  update_pane_geometry();
}

void QtViewportPanel::create_pane(Pane &pane_state) {
  pane_state.frame = new QFrame(this);
  pane_state.frame->setObjectName(
      QStringLiteral("viewportPane_%1")
          .arg(QString::fromUtf8(
              viewport_pane_stable_id(pane_state.id).data())));
  pane_state.frame->setFrameShape(QFrame::StyledPanel);
  pane_state.frame->setSizePolicy(QSizePolicy::Expanding,
                                  QSizePolicy::Expanding);
  pane_state.frame->hide();

  pane_state.surface = new QtViewportSurface(pane_state.id, composed_);
  pane_state.surface->installEventFilter(this);
  if (composed_) {
    pane_state.surface->QObject::setParent(pane_state.frame);
    auto *host = new QtVulkanCompositionWidget(pane_state.frame);
    pane_state.surface_container = host;
    const QPointer<QtViewportSurface> surface = pane_state.surface;
    host->set_prepare_callback([this]() { return prepare_composed_frame(); });
    host->set_render_callback([surface](const RenderVulkanCompositionTarget &target) {
      return surface != nullptr && surface->render_composed_frame(target);
    });
    host->set_release_callback([surface]() {
      if (surface != nullptr) {
        surface->release_composed_surface();
      }
    });
    host->setMouseTracking(true);
  } else {
    pane_state.surface_container = new QWidget(pane_state.frame);
    pane_state.surface_container->setAttribute(Qt::WA_NativeWindow);
  }
  pane_state.surface_container->setObjectName(
      QStringLiteral("viewportSurfaceContainer_%1")
          .arg(QString::fromUtf8(
              viewport_pane_stable_id(pane_state.id).data())));
  pane_state.surface_container->setFocusPolicy(Qt::StrongFocus);
  pane_state.surface_container->setSizePolicy(QSizePolicy::Expanding,
                                              QSizePolicy::Expanding);
  pane_state.surface_container->setGeometry(pane_state.frame->rect());
  pane_state.surface_container->hide();
  if (!composed_) {
    pane_state.surface_backing_container = QWidget::createWindowContainer(
        pane_state.surface, pane_state.surface_container);
    pane_state.surface_backing_container->setAttribute(Qt::WA_NativeWindow);
    pane_state.surface_backing_container->setFocusPolicy(Qt::StrongFocus);
    pane_state.surface_backing_container->installEventFilter(pane_state.surface);
    pane_state.surface_container->setFocusProxy(pane_state.surface_backing_container);
  }
  pane_state.surface->set_visible_host(*pane_state.surface_container);
  pane_state.surface->set_input_context_provider(
      [this]() -> std::optional<QtViewportInputContext> {
    if (interaction_editor_ == nullptr) return std::nullopt;
    return QtViewportInputContext{
        .binding_generation = runtime_binding_generation_,
        .edit_generation = interaction_editor_->mode().generation()};
  });
  if (composed_) {
    pane_state.surface_container->installEventFilter(this);
  }
  pane_state.surface->set_focus_request_callback(
      [this](ViewportPaneId pane_id, ViewportPaneFocusTrigger trigger) {
        request_pane_focus(pane_id, trigger);
      });

  pane_state.toolbar = new QtViewportControlStrip(pane_state.frame);
  pane_state.toolbar->setObjectName(
      QStringLiteral("viewportToolbar_%1")
          .arg(QString::fromUtf8(
              viewport_pane_stable_id(pane_state.id).data())));
  pane_state.toolbar->setAttribute(Qt::WA_NativeWindow, !composed_);

  if (environment_popup_ != nullptr) {
    pane_state.environment_button =
        new QtEnvironmentLauncherButton(pane_state.toolbar);
    pane_state.environment_button->setObjectName(
        QStringLiteral("viewportEnvironment_%1")
            .arg(QString::fromUtf8(viewport_pane_stable_id(pane_state.id).data())));
    connect(pane_state.environment_button, &QPushButton::clicked, this,
            [this, &pane_state]() {
              request_pane_focus(pane_state.id, ViewportPaneFocusTrigger::FocusIn);
              environment_popup_->show_below(*pane_state.environment_button);
            });
    pane_state.environment_overflow_action = new QAction(pane_state.toolbar);
    connect(pane_state.environment_overflow_action, &QAction::triggered, this,
            [this, &pane_state]() {
              request_pane_focus(pane_state.id, ViewportPaneFocusTrigger::FocusIn);
              environment_popup_->show_below(*pane_state.toolbar);
            });
  }
  pane_state.display_button = new QtViewportCompactMenuButton(pane_state.toolbar);
  pane_state.display_button->setObjectName(
      QStringLiteral("viewportShading_%1")
          .arg(QString::fromUtf8(
              viewport_pane_stable_id(pane_state.id).data())));
  pane_state.display_menu = new QMenu(pane_state.display_button);
  pane_state.display_button->setMenu(pane_state.display_menu);
  connect(pane_state.display_menu, &QMenu::aboutToShow, this,
          [this, &pane_state]() {
            request_pane_focus(pane_state.id, ViewportPaneFocusTrigger::FocusIn);
          });

  auto *display_group = new QActionGroup(pane_state.display_menu);
  display_group->setExclusive(true);
  for (std::size_t index = 0; index < kShadingModes.size(); ++index) {
    if (kShadingModes[index] == ViewportShadingMode::Rendered &&
        !release_has_feature(ReleaseFeature::RenderedViewport)) {
      continue;
    }
    QAction *action = pane_state.display_menu->addAction(QString());
    action->setCheckable(true);
    display_group->addAction(action);
    pane_state.shading_actions[index] = action;
    const ViewportShadingMode shading = kShadingModes[index];
    connect(action, &QAction::triggered, this,
            [this, &pane_state, shading]() {
              if (workspace_ != nullptr) {
                (void)workspace_->focus_pane(pane_state.id);
                update_focused_pane_indicator();
              }
              const std::optional<QuaderCommandId> command_id =
                  shading_command_id(shading);
              if (interaction_editor_ != nullptr && command_id.has_value()) {
                (void)interaction_editor_->commands().execute(*command_id);
                sync_pane_control_selection(pane_state);
              } else {
                set_pane_shading(pane_state, shading);
              }
            });
  }

  pane_state.camera_button = new QtViewportCompactMenuButton(pane_state.toolbar);
  pane_state.camera_button->setObjectName(
      QStringLiteral("viewportCamera_%1")
          .arg(QString::fromUtf8(viewport_pane_stable_id(pane_state.id).data())));
  pane_state.camera_menu = new QMenu(pane_state.camera_button);
  pane_state.camera_button->setMenu(pane_state.camera_menu);
  connect(pane_state.camera_menu, &QMenu::aboutToShow, this,
          [this, &pane_state]() {
            request_pane_focus(pane_state.id, ViewportPaneFocusTrigger::FocusIn);
          });
  auto *camera_group = new QActionGroup(pane_state.camera_menu);
  camera_group->setExclusive(true);
  for (std::size_t index = 0; index < kViewportViews.size(); ++index) {
    QAction *action = pane_state.camera_menu->addAction(QString());
    action->setCheckable(true);
    camera_group->addAction(action);
    pane_state.camera_actions[index] = action;
    const ViewportView view = kViewportViews[index];
    connect(action, &QAction::triggered, this,
            [this, &pane_state, view]() {
              if (workspace_ != nullptr) {
                (void)workspace_->focus_pane(pane_state.id);
                update_focused_pane_indicator();
              }
              set_pane_view(pane_state, view);
            });
  }

  if (release_has_feature(ReleaseFeature::RenderedViewport)) {
    pane_state.display_menu->addSeparator();
    pane_state.debug_view_menu = new QMenu(pane_state.display_menu);
    pane_state.display_menu->addMenu(pane_state.debug_view_menu);
    for (std::size_t index = 0; index < kDebugViewModes.size(); ++index) {
      QAction *action = pane_state.debug_view_menu->addAction(QString());
      action->setCheckable(true);
      display_group->addAction(action);
      pane_state.debug_view_actions[index] = action;
      const ViewportShadingMode shading = kDebugViewModes[index];
      connect(action, &QAction::triggered, this,
              [this, &pane_state, shading]() {
                if (workspace_ != nullptr) {
                  (void)workspace_->focus_pane(pane_state.id);
                  update_focused_pane_indicator();
                }
                set_pane_shading(pane_state, shading);
              });
    }
  }

  pane_state.layout_button = new QtViewportChromeButton(
      QtViewportChromeButton::Glyph::Layout, pane_state.toolbar);
  pane_state.layout_button->setObjectName(QStringLiteral("viewportLayout_%1")
      .arg(QString::fromUtf8(viewport_pane_stable_id(pane_state.id).data())));
  pane_state.layout_menu = new QMenu(pane_state.layout_button);
  pane_state.layout_menu->setObjectName(QStringLiteral("viewportLayoutMenu_%1")
      .arg(QString::fromUtf8(viewport_pane_stable_id(pane_state.id).data())));
  pane_state.layout_button->setMenu(pane_state.layout_menu);
  pane_state.layout_button->setPopupMode(QToolButton::InstantPopup);
  connect(pane_state.layout_menu, &QMenu::aboutToShow, this,
      [this, &pane_state]() {
        request_pane_focus(pane_state.id, ViewportPaneFocusTrigger::FocusIn);
        pane_state.layout_menu_generation = runtime_binding_generation_;
        pane_state.control_selection_valid = false;
        sync_pane_control_selection(pane_state);
        const auto &layouts = viewport_layout_options();
        for (std::size_t index = 0; index < layouts.size(); ++index) {
          QAction *action = pane_state.layout_actions[index];
          const auto descriptor = interaction_editor_ != nullptr
              ? interaction_editor_->commands().descriptor(layout_command_id(layouts[index].id))
              : std::optional<QuaderCommandDescriptor>{};
          QString text = action->text().section(QLatin1Char('\t'), 0, 0);
          if (descriptor.has_value() && !descriptor->shortcut.empty()) {
            text += QLatin1Char('\t');
            text += QString::fromStdString(descriptor->shortcut);
          }
          if (action->text() != text) action->setText(text);
          const bool enabled = descriptor.has_value() && descriptor->enabled;
          if (action->isEnabled() != enabled) action->setEnabled(enabled);
        }
      });
  auto *layout_group = new QActionGroup(pane_state.layout_menu);
  layout_group->setExclusive(true);
  const auto &layouts = viewport_layout_options();
  for (std::size_t index = 0; index < layouts.size(); ++index) {
    const auto layout = layouts[index].id;
    QAction *action = pane_state.layout_menu->addAction(QString());
    action->setObjectName(QStringLiteral("layout_%1")
        .arg(QString::fromUtf8(viewport_layout_stable_id(layout).data())));
    action->setCheckable(true);
    layout_group->addAction(action);
    pane_state.layout_actions[index] = action;
    connect(action, &QAction::triggered, this, [this, &pane_state, layout]() {
      const auto generation = pane_state.layout_menu_generation;
      if (generation != runtime_binding_generation_ || interaction_editor_ == nullptr ||
          workspace_ == nullptr) return;
      request_pane_focus(pane_state.id, ViewportPaneFocusTrigger::FocusIn);
      if (workspace_->focused_pane() != pane_state.id) return;
      (void)interaction_editor_->commands().execute(layout_command_id(layout));
      if (generation != runtime_binding_generation_) return;
      pane_state.control_selection_valid = false;
      refresh_workspace_layout();
    });
  }

  pane_state.camera_settings_button = new QtViewportChromeButton(
      QtViewportChromeButton::Glyph::Camera, pane_state.toolbar);
  pane_state.camera_settings_button->setObjectName(
      QStringLiteral("viewportCameraSettings_%1")
          .arg(QString::fromUtf8(viewport_pane_stable_id(pane_state.id).data())));
  pane_state.camera_settings_popup =
      new QtViewportCameraSettingsPopup(pane_state.camera_settings_button);
  pane_state.camera_settings_popup->setObjectName(
      QStringLiteral("viewportCameraSettingsPopup_%1")
          .arg(QString::fromUtf8(viewport_pane_stable_id(pane_state.id).data())));
  pane_state.camera_settings_popup->slider().setObjectName(
      QStringLiteral("viewportFlySpeed_%1")
          .arg(QString::fromUtf8(viewport_pane_stable_id(pane_state.id).data())));
  pane_state.camera_settings_popup->value_label().setObjectName(
      QStringLiteral("viewportFlySpeedValue_%1")
          .arg(QString::fromUtf8(viewport_pane_stable_id(pane_state.id).data())));
  connect(pane_state.camera_settings_button, &QToolButton::clicked, this,
      [this, &pane_state]() {
        if (interaction_editor_ == nullptr || workspace_ == nullptr) return;
        request_pane_focus(pane_state.id, ViewportPaneFocusTrigger::FocusIn);
        if (workspace_->focused_pane() != pane_state.id) return;
        pane_state.camera_settings_generation = runtime_binding_generation_;
        pane_state.camera_settings_popup->set_speed(
            interaction_editor_->viewports().fly_navigation_speed(pane_state.id));
        pane_state.camera_settings_popup->show_below(*pane_state.camera_settings_button);
      });
  connect(&pane_state.camera_settings_popup->slider(), &QSlider::valueChanged, this,
      [this, &pane_state](int value) {
        const auto generation = pane_state.camera_settings_generation;
        if (generation != runtime_binding_generation_ || interaction_editor_ == nullptr ||
            workspace_ == nullptr || !pane_state.camera_settings_popup->isVisible()) return;
        (void)interaction_editor_->viewports().set_fly_navigation_speed(pane_state.id, value);
        if (generation != runtime_binding_generation_) return;
        pane_state.camera_settings_popup->set_speed(
            interaction_editor_->viewports().fly_navigation_speed(pane_state.id));
      });

  pane_state.toolbar->set_trailing_controls(
      pane_state.camera_button, pane_state.display_button,
      pane_state.environment_button, pane_state.environment_overflow_action,
      pane_state.layout_button, pane_state.camera_settings_button);
  scale_pane_controls(pane_state);

  pane_state.info_overlay = new QLabel(pane_state.frame);
  pane_state.info_overlay->setObjectName(
      QStringLiteral("viewportInfo_%1")
          .arg(QString::fromUtf8(
              viewport_pane_stable_id(pane_state.id).data())));
  pane_state.info_overlay->setTextFormat(Qt::PlainText);
  pane_state.info_overlay->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  pane_state.info_overlay->setSizePolicy(QSizePolicy::Fixed,
                                         QSizePolicy::Fixed);
  pane_state.info_overlay->setAttribute(Qt::WA_NativeWindow, !composed_);
  pane_state.info_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
  pane_state.info_overlay->hide();

  pane_state.primitive_feedback = new QtViewportPrimitiveFeedbackLabel(pane_state.frame);
  pane_state.primitive_feedback->setObjectName(
      QStringLiteral("viewportPrimitiveFeedback_%1")
          .arg(QString::fromUtf8(viewport_pane_stable_id(pane_state.id).data())));
  pane_state.primitive_feedback->setTextFormat(Qt::PlainText);
  pane_state.primitive_feedback->setAttribute(Qt::WA_NativeWindow, !composed_);
  pane_state.primitive_feedback->setAttribute(Qt::WA_TransparentForMouseEvents);
  pane_state.primitive_feedback->setFocusPolicy(Qt::NoFocus);
  pane_state.primitive_feedback->setBackgroundRole(QPalette::ToolTipBase);
  pane_state.primitive_feedback->setForegroundRole(QPalette::ToolTipText);
  pane_state.primitive_feedback->setAutoFillBackground(true);
  pane_state.primitive_feedback->hide();

  pane_state.fly_navigation_hud =
      new QtViewportFlyNavigationHud(*pane_state.frame, pane_state.id, !composed_);
  pane_state.edge_length_hud =
      new QtViewportEdgeLengthHud(*pane_state.frame, pane_state.id, !composed_);

  update_pane_overlay_geometry(pane_state);

  refresh_pane_controls(pane_state);
}

void QtViewportPanel::refresh_keybinding_presentation(
    QuaderEditorApi &editor) {
  interaction_controller_.cancel_pending_selection_click();
  if (interaction_editor_ != &editor) {
    active_tool_revision_valid_ = false;
    interaction_native_tools_ = nullptr;
  }
  interaction_editor_ = &editor;
  for (Pane &pane_state : panes_) {
    refresh_pane_controls(pane_state);
  }
}

void QtViewportPanel::refresh_pane_controls(Pane &pane_state) {
  refresh_selection_tool_actions(pane_state);
  for (const auto &entry : pane_state.control_actions) {
    if (entry.action->objectName() != QStringLiteral("backfaceSelection")) continue;
    const QString label = localized_display_menu_label(localization_,
        "ui.menu.select.toggle_backface_selection", "Toggle Backface Selection");
    QString tooltip = label;
    if (interaction_editor_ != nullptr) {
      const std::string shortcut = interaction_editor_->commands().shortcut_for(
          quader_command::kSelectToggleBackfaceSelection);
      if (!shortcut.empty()) tooltip += QStringLiteral(" (%1)").arg(QString::fromStdString(shortcut));
    }
    if (entry.action->text() != label) entry.action->setText(label);
    if (entry.action->toolTip() != tooltip) entry.action->setToolTip(tooltip);
  }
  if (pane_state.environment_button != nullptr) {
    const bool environment_available =
        release_has_feature(ReleaseFeature::Environment);
    const char *environment_key = environment_available
        ? "ui.inspector.environment" : "ui.inspector.camera";
    const char *environment_fallback =
        environment_available ? "Environment" : "Camera";
    const QString environment_label = localization_ != nullptr
        ? QString::fromStdString(
              localization_->text(environment_key, environment_fallback))
        : QString::fromUtf8(environment_fallback);
    if (pane_state.environment_button->text() != environment_label) {
      pane_state.environment_button->setText(environment_label);
      pane_state.environment_button->setAccessibleName(environment_label);
      pane_state.environment_button->setToolTip(environment_label);
      pane_state.environment_overflow_action->setText(environment_label);
    }
  }
  for (std::size_t index = 0; index < kShadingModes.size(); ++index) {
    if (pane_state.shading_actions[index] == nullptr) {
      continue;
    }
    const ViewportShadingMode shading = kShadingModes[index];
    QString text = localized_shading_label(localization_, shading);
    if (interaction_editor_ != nullptr) {
      const std::optional<QuaderCommandId> command_id =
          shading_command_id(shading);
      if (command_id.has_value()) {
        const std::string shortcut =
            interaction_editor_->commands().shortcut_for(*command_id);
        if (!shortcut.empty()) {
          text.append(QLatin1Char('\t'));
          text.append(QString::fromStdString(shortcut));
        }
      }
    }
    pane_state.shading_actions[index]->setText(text);
  }
  pane_state.camera_menu->setTitle(localized_display_menu_label(
      localization_, "viewport.display.camera", "Camera"));
  pane_state.camera_button->setAccessibleName(pane_state.camera_menu->title());
  pane_state.camera_button->setToolTip(pane_state.camera_menu->title());
  const auto &layouts = viewport_layout_options();
  for (std::size_t index = 0; index < layouts.size(); ++index) {
    const auto &layout = layouts[index];
    const std::string key = "viewport.layout." +
        std::string(viewport_layout_stable_id(layout.id)) + ".label";
    const QString label = localization_ != nullptr
        ? QString::fromStdString(localization_->text(key, layout.label))
        : QString::fromUtf8(layout.label.data(), static_cast<qsizetype>(layout.label.size()));
    pane_state.layout_actions[index]->setText(label);
  }
  const QString layout_label = localized_display_menu_label(localization_,
      "viewport.display.layout", "Viewport Layout");
  pane_state.layout_button->defaultAction()->setText(layout_label);
  pane_state.layout_button->defaultAction()->setToolTip(layout_label);
  pane_state.layout_menu->setTitle(layout_label);
  const QString settings_label = localized_display_menu_label(localization_,
      "viewport.display.camera_settings", "Camera Settings");
  pane_state.camera_settings_button->defaultAction()->setText(settings_label);
  pane_state.camera_settings_button->defaultAction()->setToolTip(settings_label);
  pane_state.camera_settings_popup->setWindowTitle(settings_label);
  pane_state.camera_settings_popup->set_label(localized_display_menu_label(localization_,
      "viewport.display.fly_speed", "Fly Speed"));
  for (std::size_t index = 0; index < kViewportViews.size(); ++index) {
    pane_state.camera_actions[index]->setText(
        localized_view_label(localization_, kViewportViews[index]));
  }
  if (pane_state.debug_view_menu != nullptr) {
    pane_state.debug_view_menu->setTitle(localized_display_menu_label(
        localization_, "viewport.display.debug_view", "Debug View"));
  }
  for (std::size_t index = 0; index < kDebugViewModes.size(); ++index) {
    if (pane_state.debug_view_actions[index] == nullptr) {
      continue;
    }
    pane_state.debug_view_actions[index]->setText(
        localized_shading_label(localization_, kDebugViewModes[index]));
  }
  pane_state.control_selection_valid = false;
  sync_pane_control_selection(pane_state);
  scale_pane_controls(pane_state);
  update_pane_overlay_geometry(pane_state);
}

void QtViewportPanel::scale_pane_controls(Pane &pane_state) {
  for (QToolButton *button : {pane_state.layout_button, pane_state.camera_settings_button}) {
    static_cast<QtViewportChromeButton *>(button)->apply_ui_scale(ui_scale_metrics_);
  }
  pane_state.layout_menu->setFont(QApplication::font());
  pane_state.camera_settings_popup->apply_ui_scale(ui_scale_metrics_);
  QFont baseline = QApplication::font();
  const auto *application_style =
      dynamic_cast<const QtUiScaleProxyStyle *>(QApplication::style());
  const double application_scale = application_style != nullptr
      ? application_style->metrics().factor() : 1.0;
  if (baseline.pointSizeF() > 0.0) {
    baseline.setPointSizeF(std::max(1.0, baseline.pointSizeF() / application_scale - 2.0));
  } else if (baseline.pixelSize() > 0) {
    // Pixel-sized platform fonts need the same point-to-pixel conversion.
    const double points = baseline.pixelSize() * 72.0 / logicalDpiY() / application_scale;
    baseline.setPointSizeF(std::max(1.0, points - 2.0));
  }
  const QFont compact_font = ui_scale_metrics_.font(baseline);
  for (QPushButton *button : {pane_state.camera_button, pane_state.display_button}) {
    button->setFont(compact_font);
    static_cast<QtViewportCompactMenuButton *>(button)->set_metrics(ui_scale_metrics_);
    button->menu()->setFont(QApplication::font());
  }
  if (pane_state.environment_button != nullptr) {
    pane_state.environment_button->setFont(compact_font);
    pane_state.environment_button->setFixedHeight(
        pane_state.environment_button->fontMetrics().height() + ui_scale_metrics_.px(4));
  }
  if (pane_state.debug_view_menu != nullptr) {
    pane_state.debug_view_menu->setFont(QApplication::font());
  }
}

void QtViewportPanel::sync_pane_control_selection(Pane &pane_state) {
  if (workspace_ == nullptr) {
    return;
  }
  const ViewportView view = workspace_->view(pane_state.id);
  const ViewportShadingMode shading =
      workspace_->shading_mode(pane_state.id);
  const ViewportLayoutId layout = workspace_->layout();
  if (interaction_editor_ != nullptr && pane_state.camera_settings_popup->isVisible()) {
    pane_state.camera_settings_popup->set_speed(
        interaction_editor_->viewports().fly_navigation_speed(pane_state.id));
  }
  if (pane_state.control_selection_valid &&
      pane_state.presented_view == view &&
      pane_state.presented_shading == shading && pane_state.presented_layout == layout) {
    return;
  }
  const auto &layouts = viewport_layout_options();
  for (std::size_t index = 0; index < layouts.size(); ++index) {
    const bool selected = layouts[index].id == layout;
    if (pane_state.layout_actions[index]->isChecked() != selected) {
      pane_state.layout_actions[index]->setChecked(selected);
    }
  }
  for (std::size_t index = 0; index < kViewportViews.size(); ++index) {
    const bool selected = kViewportViews[index] == view;
    if (pane_state.camera_actions[index]->isChecked() != selected) {
      pane_state.camera_actions[index]->setChecked(selected);
    }
  }
  for (std::size_t index = 0; index < kShadingModes.size(); ++index) {
    if (pane_state.shading_actions[index] == nullptr) {
      continue;
    }
    const bool selected = kShadingModes[index] == shading;
    if (pane_state.shading_actions[index]->isChecked() != selected) {
      pane_state.shading_actions[index]->setChecked(selected);
    }
  }
  for (std::size_t index = 0; index < kDebugViewModes.size(); ++index) {
    if (pane_state.debug_view_actions[index] == nullptr) {
      continue;
    }
    const bool selected = kDebugViewModes[index] == shading;
    if (pane_state.debug_view_actions[index]->isChecked() != selected) {
      pane_state.debug_view_actions[index]->setChecked(selected);
    }
  }
  const QString display_label = localized_shading_label(localization_, shading);
  const QString camera_label = localized_view_label(localization_, view);
  const bool label_changed = pane_state.display_button->text() != display_label ||
                             pane_state.camera_button->text() != camera_label;
  if (pane_state.display_button->text() != display_label) {
    pane_state.display_button->setText(display_label);
  }
  if (pane_state.camera_button->text() != camera_label) {
    pane_state.camera_button->setText(camera_label);
  }
  pane_state.presented_view = view;
  pane_state.presented_shading = shading;
  pane_state.presented_layout = layout;
  pane_state.control_selection_valid = true;
  if (label_changed) {
    pane_state.toolbar->updateGeometry();
    update_pane_geometry();
  }
}

QRect QtViewportPanel::pane_content_rect(const Pane &pane_state) const {
  const QRect bounds = pane_state.frame->rect();
  const int header_height = ui_scale_metrics_.px(36);
  return QRect(0, header_height, bounds.width(),
               std::max(1, bounds.height() - header_height));
}

void QtViewportPanel::update_pane_geometry() {
  if (workspace_ == nullptr) {
    return;
  }
  std::array<bool, kViewportPaneCount> active{};
  std::array<const ViewportPanePlacement *, kViewportPaneCount> placements{};
  std::vector<ViewportPanePlacement> active_placements =
      workspace_->active_pane_placements();
  std::array<QSize, kViewportPaneCount> pane_minima{};
  QSize required_size = ui_scale_metrics_.size(QSize(300, 200));
  for (const ViewportPanePlacement &placement : active_placements) {
    const std::size_t index = viewport_pane_index(placement.pane_id);
    const QSize strip_minimum = panes_[index].toolbar->minimumSizeHint();
    pane_minima[index] =
        QSize(strip_minimum.width(),
              strip_minimum.height() + ui_scale_metrics_.px(32));
    required_size = required_size.expandedTo(pane_minima[index]);
  }
  struct SplitMinimum {
    ViewportSplitHandle handle;
    int before = 0;
    int after = 0;
  };
  std::vector<SplitMinimum> split_minima;
  for (const ViewportSplitHandle &handle : workspace_->active_split_handles()) {
    SplitMinimum minimum{handle};
    const bool vertical = handle.axis == ViewportSplitHandleAxis::Vertical;
    for (const ViewportPanePlacement &placement : active_placements) {
      const ViewportNormalizedRect &rect = placement.normalized_rect;
      const float cross_start = vertical ? rect.y : rect.x;
      const float cross_end = cross_start + (vertical ? rect.height : rect.width);
      if (cross_end <= handle.start || cross_start >= handle.end) {
        continue;
      }
      const float start = vertical ? rect.x : rect.y;
      const float end = start + (vertical ? rect.width : rect.height);
      const QSize pane_minimum = pane_minima[viewport_pane_index(placement.pane_id)];
      const int extent = vertical ? pane_minimum.width() : pane_minimum.height();
      if (std::abs(end - handle.position) < 0.0001f) {
        minimum.before = std::max(minimum.before, extent);
      }
      if (std::abs(start - handle.position) < 0.0001f) {
        minimum.after = std::max(minimum.after, extent);
      }
    }
    // Sum the two native minima, never divide by the current split ratio.
    // The owner limits ratios to 15%-85%, which also has to fit either side.
    const int minimum_extent = std::max({
        minimum.before + minimum.after,
        static_cast<int>(std::ceil(minimum.before / ViewportLayoutState::kMaxSplit)),
        static_cast<int>(std::ceil(minimum.after / (1.0f - ViewportLayoutState::kMinSplit)))});
    int &required_extent = vertical ? required_size.rwidth() : required_size.rheight();
    required_extent = std::max(required_extent, minimum_extent);
    split_minima.push_back(minimum);
  }
  if (minimumSize() != required_size) {
    setMinimumSize(required_size);
  }
  const int full_width = std::max(1, width());
  const int full_height = std::max(1, height());
  for (const SplitMinimum &minimum : split_minima) {
    const int extent = minimum.handle.axis == ViewportSplitHandleAxis::Vertical
                           ? full_width : full_height;
    const float lower = static_cast<float>(minimum.before) / extent;
    const float upper = 1.0f - static_cast<float>(minimum.after) / extent;
    const float position = std::clamp(minimum.handle.position, lower, std::max(lower, upper));
    if (position != minimum.handle.position) {
      workspace_->set_split_handle_position(minimum.handle.id, position);
    }
  }
  active_placements = workspace_->active_pane_placements();
  displayed_placements_ = active_placements;
  for (const ViewportPanePlacement &placement : active_placements) {
    const std::size_t index = viewport_pane_index(placement.pane_id);
    active[index] = true;
    placements[index] = &placement;
  }
  qt_apply_viewport_pane_activation(
      active,
      [this](std::size_t index, bool visible) {
        const bool should_present =
            visible && !startup_presentation_blocked_;
        if (panes_[index].frame_presented == should_present) {
          return;
        }
        if (!should_present) {
          panes_[index].camera_settings_popup->close();
          panes_[index].layout_menu->close();
        }
        panes_[index].frame->setVisible(should_present);
        panes_[index].frame_presented = should_present;
        panes_[index].overlay_stack_valid = false;
      },
      [this, &placements, full_width, full_height](std::size_t index) {
        Pane &pane_state = panes_[index];
        const ViewportNormalizedRect &rect =
            placements[index]->normalized_rect;
        const int x = static_cast<int>(std::lround(rect.x * full_width));
        const int y = static_cast<int>(std::lround(rect.y * full_height));
        const int right = static_cast<int>(
            std::lround((rect.x + rect.width) * full_width));
        const int bottom = static_cast<int>(
            std::lround((rect.y + rect.height) * full_height));
        const QRect geometry{x, y, std::max(1, right - x),
                             std::max(1, bottom - y)};
        if (pane_state.frame->geometry() != geometry) {
          pane_state.frame->setGeometry(geometry);
        }
        update_pane_overlay_geometry(pane_state);
      });
  update_split_handles();
  update_focused_pane_indicator();
  settings_overlay_geometry_dirty_ = true;
  update_settings_overlay_geometry();
}

void QtViewportPanel::update_pane_overlay_geometry(Pane &pane_state) {
  if (pane_state.frame == nullptr || pane_state.surface_container == nullptr ||
      pane_state.toolbar == nullptr || pane_state.display_button == nullptr ||
      pane_state.info_overlay == nullptr ||
      pane_state.edge_length_hud == nullptr ||
      pane_state.fly_navigation_hud == nullptr) {
    return;
  }
  const QRect pane_bounds = pane_content_rect(pane_state);
  if (pane_state.surface_container->geometry() != pane_bounds) {
    pane_state.surface_container->setGeometry(pane_bounds);
  }
  // Reserve vertical pixels before a bottom dock is revealed. Changing the
  // visible clip then preserves the native surface extent and its last image.
  const auto placement = std::find_if(
      displayed_placements_.begin(), displayed_placements_.end(),
      [&pane_state](const ViewportPanePlacement &candidate) {
        return candidate.pane_id == pane_state.id;
      });
  const QWidget *host_window = window();
  const int host_height = host_window != nullptr
                              ? host_window->contentsRect().height()
                              : height();
  const float pane_height_fraction = placement != displayed_placements_.end()
                                         ? placement->normalized_rect.height
                                         : 1.0f;
  const bool retain_hidden_pixels =
      render_system_ != nullptr && render_system_->supports_hidden_native_pixels();
  const int backing_height = retain_hidden_pixels
      ? std::max(pane_bounds.height(),
                 static_cast<int>(std::ceil(pane_height_fraction * host_height)) -
                     pane_bounds.top())
      : pane_bounds.height();
  const QRect backing_bounds{
      0, 0,
      pane_bounds.width(), backing_height};
  if (pane_state.surface_backing_container != nullptr &&
      pane_state.surface_backing_container->geometry() != backing_bounds) {
    pane_state.surface_backing_container->setGeometry(backing_bounds);
    pane_state.surface->synchronize_render_size();
  }
  const int overlay_inset = ui_scale_metrics_.px(6);
  const QRect toolbar_geometry{0, 0, pane_state.frame->width(), pane_bounds.top()};
  if (pane_state.toolbar->geometry() != toolbar_geometry) {
    pane_state.toolbar->setGeometry(toolbar_geometry);
  }

  pane_state.fly_navigation_hud->set_bounds(pane_bounds);

  const QSize info_size = pane_state.info_overlay->sizeHint().boundedTo(
      QSize(std::max(1, pane_bounds.width() - 2 * overlay_inset),
            std::max(1, pane_bounds.height() - 2 * overlay_inset)));
  const QRect info_geometry{
      std::max(overlay_inset,
               pane_state.frame->width() - info_size.width() - overlay_inset),
      std::max(pane_bounds.top() + overlay_inset,
               pane_state.frame->height() - info_size.height() -
                   overlay_inset),
      info_size.width(), info_size.height()};
  if (pane_state.info_overlay->geometry() != info_geometry) {
    pane_state.info_overlay->setGeometry(info_geometry);
  }

  if (!pane_state.overlay_stack_valid) {
    pane_state.edge_length_hud->raise();
    pane_state.toolbar->raise();
    pane_state.info_overlay->raise();
    if (!pane_state.primitive_feedback->isHidden()) {
      pane_state.primitive_feedback->raise();
    }
    if (settings_panel_ != nullptr &&
        settings_panel_->parentWidget() == pane_state.frame &&
        !settings_panel_->isHidden()) {
      settings_panel_->raise();
    }
    pane_state.overlay_stack_valid = true;
  }
}

void QtViewportPanel::update_focused_pane_indicator() {
  if (workspace_ == nullptr || focused_pane_frame_ == nullptr) {
    return;
  }
  const auto hide_indicator = [this]() {
    if (!focused_pane_frame_->isHidden()) {
      focused_pane_frame_->hide();
    }
  };
  if (startup_presentation_blocked_ || external_widget_has_keyboard_focus()) {
    hide_indicator();
    return;
  }
  const ViewportPaneId focused_pane = workspace_->focused_pane();
  if (!pane_surface_has_keyboard_focus(pane(focused_pane)) &&
      !pane_controls_have_presentation_focus(pane(focused_pane))) {
    hide_indicator();
    return;
  }
  const auto focused = std::find_if(
      displayed_placements_.begin(), displayed_placements_.end(),
      [focused_pane](const ViewportPanePlacement &placement) {
        return placement.pane_id == focused_pane;
      });
  if (focused == displayed_placements_.end()) {
    hide_indicator();
    return;
  }

  const Pane &focused_state = pane(focused_pane);
  const QRect bounds = pane_content_rect(focused_state).translated(
      focused_state.frame->pos());
  const bool geometry_changed = focused_pane_frame_->geometry() != bounds;
  const int border_width = std::max(1, ui_scale_metrics_.px(2));
  const bool width_changed = focused_pane_border_width_ != border_width;
  const bool was_hidden = focused_pane_frame_->isHidden();
  if (geometry_changed) {
    focused_pane_frame_->setGeometry(bounds);
  }
  if (geometry_changed || width_changed) {
    const QRect outer = focused_pane_frame_->rect();
    focused_pane_frame_->setMask(QRegion(outer).subtracted(QRegion(
        outer.adjusted(border_width, border_width, -border_width, -border_width))));
    focused_pane_border_width_ = border_width;
  }
  if (was_hidden) {
    focused_pane_frame_->show();
  }
  if (geometry_changed || was_hidden) {
    focused_pane_frame_->raise();
  }
}

void QtViewportPanel::request_pane_focus(
    ViewportPaneId pane_id, ViewportPaneFocusTrigger trigger) {
  if (workspace_ == nullptr) {
    return;
  }
  const ViewportPaneFocusDecision decision = viewport_pane_focus_decision({
      .trigger = trigger,
      .interaction_locked = pane_interaction_locked(),
      .requested_pane_is_interaction_owner =
          pane_id == interaction_pane_id_,
      .viewport_keyboard_focus = viewport_surface_has_keyboard_focus(),
  });
  if (!decision.focus_workspace_pane) {
    return;
  }

  if (workspace_->focused_pane() != pane_id) {
    (void)workspace_->focus_pane(pane_id);
    update_focused_pane_indicator();
    update_settings_overlay_geometry();
  }
  if (!decision.focus_native_surface) {
    return;
  }

  Pane &pane_state = pane(pane_id);
  if (pane_surface_has_keyboard_focus(pane_state)) {
    return;
  }
  const Qt::FocusReason reason =
      trigger == ViewportPaneFocusTrigger::Programmatic
          ? Qt::OtherFocusReason
          : Qt::MouseFocusReason;
  pane_state.surface_container->setFocus(reason);
  if (!composed_) {
    pane_state.surface->requestActivate();
  }
  update_focused_pane_indicator();
}

bool QtViewportPanel::viewport_surface_has_keyboard_focus() const {
  for (const Pane &pane_state : panes_) {
    if (pane_surface_has_keyboard_focus(pane_state)) {
      return true;
    }
  }
  return false;
}

bool QtViewportPanel::pane_surface_has_keyboard_focus(
    const Pane &pane_state) const {
  const QObject *focus_object = QGuiApplication::focusObject();
  const QWindow *focus_window = QGuiApplication::focusWindow();
  const QWidget *focus_widget = QApplication::focusWidget();
  return focus_object == pane_state.surface ||
         focus_window == pane_state.surface ||
         focus_widget == pane_state.surface_container ||
         (focus_widget != nullptr &&
          pane_state.surface_container != nullptr &&
          pane_state.surface_container->isAncestorOf(focus_widget));
}

bool QtViewportPanel::pane_controls_have_presentation_focus(
    const Pane &pane_state) const {
  if (QApplication::activeModalWidget() != nullptr ||
      external_widget_has_keyboard_focus()) return false;
  if (const QWidget *popup = QApplication::activePopupWidget()) {
    return qt_viewport_control_strip_ancestor(popup) == pane_state.toolbar;
  }
  return qt_viewport_control_strip_ancestor(QApplication::focusWidget()) == pane_state.toolbar ||
         qt_viewport_control_strip_ancestor(QGuiApplication::focusObject()) == pane_state.toolbar ||
         (pane_state.toolbar->windowHandle() != nullptr &&
          QGuiApplication::focusWindow() == pane_state.toolbar->windowHandle());
}

bool QtViewportPanel::external_widget_has_keyboard_focus() const {
  const QWidget *focus_widget = QApplication::focusWidget();
  if (focus_widget == nullptr) return false;
  const auto *strip = qt_viewport_control_strip_ancestor(focus_widget);
  for (const Pane &pane_state : panes_) {
    if (strip == pane_state.toolbar ||
        focus_widget == pane_state.surface_container ||
        (pane_state.surface_container != nullptr &&
         pane_state.surface_container->isAncestorOf(focus_widget))) {
      return false;
    }
  }
  // Native QWindow focus can lag the QWidget signal. A concrete external
  // editor wins, while transitions between this viewport's surfaces and
  // controls may still use the incoming native window to identify the pane.
  return true;
}

void QtViewportPanel::synchronize_control_focus(const QObject *object) {
  if (workspace_ == nullptr || object == nullptr) return;
  const auto *strip = qt_viewport_control_strip_ancestor(object);
  for (const Pane &pane_state : panes_) {
    if (strip == pane_state.toolbar || object == pane_state.toolbar->windowHandle()) {
      // Chrome remains part of this viewport, without moving keyboard focus
      // away from the button or menu the user is currently operating.
      request_pane_focus(pane_state.id, ViewportPaneFocusTrigger::FocusIn);
      return;
    }
  }
}

void QtViewportPanel::refresh_control_hint_context() {
  if (workspace_ == nullptr || pane_interaction_locked()) return;
  const auto focused = workspace_->focused_pane();
  if (!pane_controls_have_presentation_focus(pane(focused))) return;
  const auto &camera = workspace_->camera(focused);
  const auto &settings = workspace_->effective_settings(focused);
  const auto modifiers = QGuiApplication::keyboardModifiers();
  // This is presentation only: chrome focus never fabricates surface input,
  // acquires a tool, or carries stale hover/gesture hints into a popup.
  operation_input_context_ = {
      .viewport_available = true,
      .orthographic = camera.state().projection == ViewportProjection::Orthographic,
      .orbit_available = !camera.orthographic_view_locked(),
      .shift = modifiers.testFlag(Qt::ShiftModifier),
      .control = modifiers.testFlag(Qt::ControlModifier),
      .alt = modifiers.testFlag(Qt::AltModifier),
      .grid_snap_available = viewport_snapping_active(settings),
      .grid_snap_enabled = settings.bool_value(ViewportBoolSettingId::GridSnap),
      .angle_snap_available = viewport_snapping_active(settings),
      .angle_snap_enabled = settings.bool_value(ViewportBoolSettingId::RotationSnap),
  };
}

bool QtViewportPanel::pane_interaction_locked() const {
  if (input_router_.navigating() ||
      std::any_of(command_pointer_release_suppression_.begin(),
                  command_pointer_release_suppression_.end(),
                  [](const auto &suppression) { return suppression.active(); }) ||
      interaction_controller_.primitive_creation_pointer_active() ||
      interaction_controller_.transform_gizmo_pointer_active() ||
      interaction_controller_.dragging() ||
      interaction_controller_.selection_marquee().active ||
      interaction_controller_.point_light_range_pointer_active() ||
      (interaction_native_tools_ != nullptr &&
       interaction_native_tools_->pointer_dragging()) ||
      interaction_controller_.material_transfer_pointer_active() ||
      interaction_controller_.context_menu_pointer_active() ||
      modal_tool_dragging_) {
    return true;
  }
  return false;
}

void QtViewportPanel::synchronize_pointer_capture(
    std::optional<ViewportPaneId> owner) {
  if (composed_) {
    // Release the previous logical widget before registering the new raw
    // mouse owner on their shared native ancestor.
    for (Pane &candidate : panes_) {
      if (!owner.has_value() || candidate.id != *owner) {
        candidate.surface->set_pointer_capture(false);
      }
    }
  }
  for (Pane &candidate : panes_) {
    candidate.surface->set_pointer_capture(owner.has_value() &&
                                           candidate.id == *owner);
  }
}

void QtViewportPanel::cancel_navigation() {
  if (input_router_.navigating() && workspace_ != nullptr) {
    const ViewportPaneId owner =
        navigation_camera_owner_.value_or(interaction_pane_id_);
    workspace_->camera(owner).end_drag();
    Pane &owner_pane = pane(owner);
    if (owner_pane.surface != nullptr) {
      owner_pane.surface->set_pointer_capture(false);
      owner_pane.surface->abandon_input();
    }
  }
  input_router_.reset();
  navigation_camera_owner_.reset();
  hide_fly_navigation_huds();
  last_fly_navigation_active_ = false;
}

void QtViewportPanel::set_modal_tool_owns_viewport_snapshot(
    bool owns_viewport) {
  modal_tool_owns_viewport_snapshot_ = owns_viewport;
}

void QtViewportPanel::update_fly_navigation_hud(
    Pane &pane_state, bool active, bool speed_is_default, float speed) {
  pane_state.fly_navigation_hud->present(active, speed_is_default, speed,
                                         localization_);
}

void QtViewportPanel::hide_fly_navigation_huds() {
  for (Pane &pane_state : panes_) {
    if (pane_state.fly_navigation_hud != nullptr) {
      pane_state.fly_navigation_hud->hide();
    }
  }
}

void QtViewportPanel::update_pointer_cursor_presentation() {
  ViewportPointerCursor cursor = ViewportPointerCursor::Default;
  if (workspace_ != nullptr && !input_router_.navigating()) {
    cursor = interaction_controller_.active_tool_pointer_cursor(
        input_router_.state().active_tool);
  }
  for (Pane &pane_state : panes_) {
    pane_state.surface->set_interaction_cursor(
        pointer_cursor_pane_ == pane_state.id ? cursor
                                             : ViewportPointerCursor::Default);
  }
}

void QtViewportPanel::draw_viewport_info_overlay(
    Pane &pane_state, const SceneDocument &document,
    const AssetRegistry &assets, ViewportOverlayCamera camera) {
  const std::uint64_t visibility_revision = document.visibility_revision();
  const std::uint64_t transform_revision = document.transform_revision();
  const std::uint64_t mesh_binding_revision =
      document.mesh_binding_revision();
  const std::uint64_t render_component_revision =
      document.render_component_revision();
  const std::uint64_t layer_revision = document.layer_revision();
  const std::uint64_t asset_revision = assets.revision();
  const ViewportLocalVisibilitySnapshot local_visibility =
      workspace_ != nullptr ? workspace_->local_visibility_snapshot()
                            : ViewportLocalVisibilitySnapshot{};
  const std::uint64_t local_visibility_revision =
      local_visibility.revision();
  const bool structural_snapshot_cache_hit =
      viewport_info_scene_snapshot_valid_ &&
      viewport_info_cached_document_ == &document &&
      viewport_info_cached_assets_ == &assets &&
      viewport_info_cached_visibility_revision_ == visibility_revision &&
      viewport_info_cached_mesh_binding_revision_ == mesh_binding_revision &&
      viewport_info_cached_render_component_revision_ ==
          render_component_revision &&
      viewport_info_cached_layer_revision_ == layer_revision &&
      viewport_info_cached_asset_revision_ == asset_revision &&
      viewport_info_cached_local_visibility_revision_ ==
          local_visibility_revision;
  bool snapshot_cache_hit =
      structural_snapshot_cache_hit &&
      viewport_info_cached_transform_revision_ == transform_revision;
  if (structural_snapshot_cache_hit &&
      viewport_info_cached_transform_revision_ != transform_revision) {
    QDR_PROFILE_SCOPE("Viewport info refresh transform bounds");
    if (try_refresh_viewport_info_scene_snapshot_transforms(
            viewport_info_cached_scene_snapshot_, document)) {
      viewport_info_cached_transform_revision_ = transform_revision;
      viewport_info_scene_stats_valid_ = false;
      snapshot_cache_hit = true;
    }
  }
  if (!snapshot_cache_hit) {
    QDR_PROFILE_SCOPE("Viewport info rebuild scene snapshot");
    viewport_info_cached_scene_snapshot_ =
        build_viewport_info_scene_snapshot(document, assets,
                                           &local_visibility);
    viewport_info_cached_document_ = &document;
    viewport_info_cached_assets_ = &assets;
    viewport_info_cached_visibility_revision_ = visibility_revision;
    viewport_info_cached_transform_revision_ = transform_revision;
    viewport_info_cached_mesh_binding_revision_ = mesh_binding_revision;
    viewport_info_cached_render_component_revision_ =
        render_component_revision;
    viewport_info_cached_layer_revision_ = layer_revision;
    viewport_info_cached_asset_revision_ = asset_revision;
    viewport_info_cached_local_visibility_revision_ =
        local_visibility_revision;
    viewport_info_scene_snapshot_valid_ = true;
    viewport_info_scene_stats_valid_ = false;
  }

  const ViewportProbeClock::time_point now = ViewportProbeClock::now();
  constexpr auto kViewportInfoSceneStatsInterval =
      std::chrono::milliseconds(200);
  const bool pane_cache_hit =
      viewport_info_cached_pane_id_ == pane_state.id;
  const bool camera_cache_hit =
      pane_cache_hit &&
      same_viewport_info_camera(viewport_info_cached_camera_, camera);
  const bool recent_stats_sample =
      pane_cache_hit &&
      viewport_info_last_scene_stats_refresh_ !=
          ViewportProbeClock::time_point{} &&
      now - viewport_info_last_scene_stats_refresh_ <
          kViewportInfoSceneStatsInterval;
  const bool stats_cache_hit = viewport_info_scene_stats_valid_ &&
                               (camera_cache_hit || recent_stats_sample);
  ViewportInfoSceneStats scene_stats;
  bool stats_refreshed = false;
  if (stats_cache_hit) {
    scene_stats = viewport_info_cached_scene_stats_;
  } else {
    QDR_PROFILE_SCOPE("Viewport info cull scene snapshot");
    scene_stats = collect_viewport_info_scene_stats(
        viewport_info_cached_scene_snapshot_, camera);
    viewport_info_cached_pane_id_ = pane_state.id;
    viewport_info_cached_camera_ = camera;
    viewport_info_cached_scene_stats_ = scene_stats;
    viewport_info_scene_stats_valid_ = true;
    viewport_info_last_scene_stats_refresh_ = now;
    stats_refreshed = true;
  }

  constexpr auto kViewportInfoTextInterval = std::chrono::milliseconds(100);
  const bool overlay_was_hidden = pane_state.info_overlay->isHidden();
  if (!overlay_was_hidden && !stats_refreshed &&
      viewport_info_last_text_refresh_ != ViewportProbeClock::time_point{} &&
      now - viewport_info_last_text_refresh_ < kViewportInfoTextInterval) {
    return;
  }
  viewport_info_last_text_refresh_ = now;

  const QString unavailable = localized_viewport_info_label(
      localization_, "ui.viewport.info.unavailable", "--");
  const QString fps_value = render_performance_.frames_per_second_available
                                ? format_viewport_info_frames_per_second(
                                      render_performance_.frames_per_second)
                                : unavailable;
  const QString cpu_value = render_performance_.cpu_frame_ms_available
                                ? format_viewport_info_milliseconds(
                                      render_performance_.cpu_frame_ms)
                                : unavailable;
  const QString gpu_value = render_performance_.gpu_frame_ms_available
                                ? format_viewport_info_milliseconds(
                                      render_performance_.gpu_frame_ms)
                                : unavailable;
  const auto line = [this](std::string_view key, std::string_view fallback,
                           const QString &value) {
    return QStringLiteral("%1: %2").arg(
        localized_viewport_info_label(localization_, key, fallback), value);
  };
  const QString text =
      line("ui.viewport.info.fps", "FPS", fps_value) + QLatin1Char('\n') +
      line("ui.viewport.info.cpu", "CPU", cpu_value) + QLatin1Char('\n') +
      line("ui.viewport.info.gpu", "GPU", gpu_value) + QLatin1Char('\n') +
      line("ui.viewport.info.objects", "Objects",
           QString::number(scene_stats.objects)) +
      QLatin1Char('\n') +
      line("ui.viewport.info.vertices", "Vertices",
           QString::number(scene_stats.vertices)) +
      QLatin1Char('\n') +
      line("ui.viewport.info.edges", "Edges",
           QString::number(scene_stats.edges)) +
      QLatin1Char('\n') +
      line("ui.viewport.info.faces", "Faces",
           QString::number(scene_stats.faces)) +
      QLatin1Char('\n') +
      line("ui.viewport.info.triangles", "Triangles",
           QString::number(scene_stats.triangles)) +
      QLatin1Char('\n') +
      line("ui.viewport.info.primitives", "Primitives",
           QString::number(scene_stats.primitives)) +
      QLatin1Char('\n') +
      line("ui.viewport.info.draw_calls", "Draw Calls",
           QString::number(scene_stats.draw_calls));
  const bool text_changed = pane_state.info_overlay->text() != text;
  if (text_changed) {
    pane_state.info_overlay->setText(text);
  }
  if (overlay_was_hidden) {
    pane_state.info_overlay->show();
    pane_state.info_overlay->raise();
  }
  if (text_changed || overlay_was_hidden) {
    update_pane_overlay_geometry(pane_state);
  }
}

void QtViewportPanel::update_split_handles() {
  if (workspace_ == nullptr || startup_presentation_blocked_) {
    for (SplitHandleHost &host : split_handles_) {
      if (host.visible) {
        host.visible = false;
        host.widget->hide();
      }
    }
    return;
  }
  const std::vector<ViewportSplitHandle> handles =
      workspace_->active_split_handles();
  const int full_width = std::max(1, width());
  const int full_height = std::max(1, height());
  const int hit_thickness = ui_scale_metrics_.px(10);
  for (std::size_t index = 0; index < split_handles_.size(); ++index) {
    SplitHandleHost &host = split_handles_[index];
    if (index >= handles.size()) {
      if (host.visible) {
        host.visible = false;
        host.widget->hide();
      }
      continue;
    }
    const bool becoming_visible = !host.visible;
    host.descriptor = handles[index];
    host.visible = true;
    QRect geometry;
    Qt::CursorShape cursor_shape = Qt::SplitHCursor;
    if (host.descriptor.axis == ViewportSplitHandleAxis::Vertical) {
      const int x =
          static_cast<int>(std::lround(host.descriptor.position * full_width));
      const int start =
          static_cast<int>(std::lround(host.descriptor.start * full_height));
      const int end =
          static_cast<int>(std::lround(host.descriptor.end * full_height));
      geometry = {x - hit_thickness / 2, start, hit_thickness,
                  std::max(1, end - start)};
    } else {
      cursor_shape = Qt::SplitVCursor;
      const int y =
          static_cast<int>(std::lround(host.descriptor.position * full_height));
      const int start =
          static_cast<int>(std::lround(host.descriptor.start * full_width));
      const int end =
          static_cast<int>(std::lround(host.descriptor.end * full_width));
      geometry = {start, y - hit_thickness / 2,
                  std::max(1, end - start), hit_thickness};
    }
    if (host.widget->geometry() != geometry) {
      host.widget->setGeometry(geometry);
    }
    if (host.widget->cursor().shape() != cursor_shape) {
      host.widget->setCursor(cursor_shape);
    }
    if (becoming_visible) {
      host.widget->show();
      host.widget->raise();
    }
  }
}

std::optional<QPoint> QtViewportPanel::resolve_marking_menu_gesture(
    QuaderEditorApi &editor, Pane &pane_state, ViewportInputFrame &input,
    bool interaction_locked) {
  std::optional<QuaderInputChord> initiating_chord;
  std::erase_if(input.resolved_shortcuts, [&](const ViewportShortcut &shortcut) {
    if (shortcut.kind != ViewportShortcutKind::MarkingMenu ||
        !shortcut.initiating_chord.has_value() ||
        shortcut.initiating_chord->device != QuaderInputDevice::Keyboard) {
      return false;
    }
    initiating_chord = shortcut.initiating_chord;
    return true;
  });
  if (!initiating_chord.has_value() &&
      !pending_marking_menu_gesture_.has_value()) {
    return std::nullopt;
  }
  if (!input.viewport_focused || !input.viewport_hovered ||
      input.text_input_active || input.navigation_active || interaction_locked ||
      input.pointer_interaction_canceled ||
      viewport_navigation_press(input.navigation_bindings).action !=
          ViewportInputActionKind::None) {
    pending_marking_menu_gesture_.reset();
    return std::nullopt;
  }
  const auto matches_physical_input = [](const QuaderInputChord &a,
                                        const QuaderInputChord &b) {
    return a.device == b.device && a.key == b.key &&
           a.mouse_button == b.mouse_button;
  };
  if (initiating_chord.has_value() &&
      !pending_marking_menu_gesture_.has_value()) {
    const float scale = static_cast<float>(
        std::max<qreal>(pane_state.surface->input_device_pixel_ratio(), 1.0));
    const QPoint local_point(
        static_cast<int>(std::lround(input.mouse_local.x / scale)),
        static_cast<int>(std::lround(input.mouse_local.y / scale)));
    pending_marking_menu_gesture_ = PendingMarkingMenuGesture{
        .pane_id = pane_state.id,
        .initiating_chord = *initiating_chord,
        .global_origin = pane_state.surface->visible_local_to_global(local_point),
        .started = std::chrono::steady_clock::now(),
        .tool_revision = editor.tools().active_tool_revision(),
        .keybindings_revision = editor.commands().keybindings_revision(),
    };
  }
  const PendingMarkingMenuGesture &gesture = *pending_marking_menu_gesture_;
  const auto matches_gesture = [&](const QuaderInputChord &chord) {
    return matches_physical_input(chord, gesture.initiating_chord);
  };
  auto remaining_press = input.pressed_input_chords.begin();
  if (initiating_chord.has_value()) {
    const auto launch_press = std::find(input.pressed_input_chords.begin(),
                                       input.pressed_input_chords.end(),
                                       *initiating_chord);
    if (launch_press != input.pressed_input_chords.end()) {
      // Prefix presses already belong to the canonical sequence dispatcher.
      // Only input arriving after its completing chord abandons this launch.
      remaining_press = launch_press + 1;
    }
  }
  if (gesture.pane_id != pane_state.id ||
      gesture.tool_revision != editor.tools().active_tool_revision() ||
      gesture.keybindings_revision != editor.commands().keybindings_revision() ||
      !input.wheel_input_edges.empty() ||
      std::any_of(remaining_press, input.pressed_input_chords.end(),
                  [&](const QuaderInputChord &chord) {
                    return !matches_gesture(chord);
                  })) {
    pending_marking_menu_gesture_.reset();
    return std::nullopt;
  }
  if (std::any_of(input.released_input_chords.begin(),
                  input.released_input_chords.end(), matches_gesture)) {
    // A tap remains a normal tool activation, including draft settlement and
    // shared-pane publication. No popup is constructed before the hold resolves.
    input.resolved_shortcuts.push_back({
        .kind = ViewportShortcutKind::ActivateTool,
        .tool = next_viewport_selection_tool(
            editor.viewports().selected_tool(gesture.pane_id)),
    });
    pending_marking_menu_gesture_.reset();
    return std::nullopt;
  }
  if (!std::any_of(input.active_input_chords.begin(),
                   input.active_input_chords.end(), matches_gesture)) {
    pending_marking_menu_gesture_.reset();
    return std::nullopt;
  }
  constexpr auto kMarkingMenuHoldThreshold = std::chrono::milliseconds(200);
  if (std::chrono::steady_clock::now() - gesture.started <
          kMarkingMenuHoldThreshold ||
      localization_ == nullptr || input.active_tool_owns_viewport ||
      input.modal_tool_active || input.active_tool_blocks_focus_shortcuts) {
    return std::nullopt;
  }
  const QPoint global_origin = gesture.global_origin;
  input.resolved_shortcuts.push_back({
      .kind = ViewportShortcutKind::MarkingMenu,
      .initiating_chord = gesture.initiating_chord,
  });
  pending_marking_menu_gesture_.reset();
  return global_origin;
}

void QtViewportPanel::process_viewport_input(
    QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
    ModelingSessionService &modeling, AssetId default_material,
    NativeViewportToolCoordinator &native_tools,
    ViewportPointPlacementService *point_placement,
    double frame_delta_seconds) {
  edge_length_hud_enabled_ = false;
  if (workspace_ == nullptr) {
    return;
  }
  const std::uint64_t input_binding_generation =
      runtime_binding_generation_;
  if (interaction_editor_ != &editor) {
    refresh_keybinding_presentation(editor);
  }
  interaction_native_tools_ = &native_tools;
  if (native_tools.refresh().changed) {
    hover_controller_.invalidate();
  }
  if (runtime_binding_generation_ != input_binding_generation) {
    return;
  }
  NativeViewportToolPresentationSnapshot native_presentation =
      native_tools.presentation_snapshot();
  sync_active_tool_from_editor(
      editor, modeling, native_tools,
      {.current_generation = &runtime_binding_generation_,
       .expected_generation = input_binding_generation});
  if (runtime_binding_generation_ != input_binding_generation) {
    return;
  }

  const float delta_seconds = static_cast<float>(
      std::clamp(frame_delta_seconds, 0.0, 0.1));

  const std::vector<ViewportPanePlacement> active_placements =
      workspace_->active_pane_placements();
  if (input_router_.navigating()) {
    if (!navigation_camera_owner_.has_value()) {
      navigation_camera_owner_ = interaction_pane_id_;
    }
    const bool owner_is_active =
        std::any_of(active_placements.begin(), active_placements.end(),
                    [&](const ViewportPanePlacement &placement) {
                      return placement.pane_id == *navigation_camera_owner_;
                    });
    if (!owner_is_active) {
      cancel_navigation();
    } else {
      interaction_pane_id_ = *navigation_camera_owner_;
    }
  }

  const ModelingToolStatus current_tool_status = modeling.tool_status();
  const bool modal_dragging = current_tool_status.insert_edge_loop_dragging ||
                              current_tool_status.cut_dragging ||
                              current_tool_status.mirror_dragging;
  modal_tool_dragging_ = modal_dragging;
  const bool interaction_locked =
      input_router_.navigating() ||
      std::any_of(command_pointer_release_suppression_.begin(),
                  command_pointer_release_suppression_.end(),
                  [](const auto &suppression) { return suppression.active(); }) ||
      interaction_controller_.primitive_creation_pointer_active() ||
      interaction_controller_.transform_gizmo_pointer_active() ||
      interaction_controller_.dragging() ||
      interaction_controller_.selection_marquee().active ||
      interaction_controller_.point_light_range_pointer_active() ||
      native_tools.pointer_dragging() ||
      interaction_controller_.material_transfer_pointer_active() ||
      interaction_controller_.context_menu_pointer_active() ||
      modal_dragging;
  std::array<QtViewportInputFrame, kViewportPaneCount> samples;
  for (Pane &pane_state : panes_) {
    const bool routes_locked_interaction =
        interaction_locked && pane_state.id == interaction_pane_id_;
    samples[viewport_pane_index(pane_state.id)] =
        pane_state.surface->take_input_frame(delta_seconds,
                                             routes_locked_interaction);
  }

  // Presentation follows held modifiers even if focus/hover routing below
  // does not produce a tool frame. An active click keeps its captured glyph.
  interaction_controller_.set_primitive_creation_decrement(
      samples[viewport_pane_index(interaction_pane_id_)].input.modifiers.primary);
  std::vector<ViewportPaneInputCandidate> pane_candidates;
  pane_candidates.reserve(active_placements.size());
  for (const ViewportPanePlacement &placement : active_placements) {
    const QtViewportInputFrame &candidate =
        samples[viewport_pane_index(placement.pane_id)];
    pane_candidates.push_back({
        .pane_id = placement.pane_id,
        .has_input_activity = has_input_activity(candidate),
        .hovered = candidate.input.viewport_hovered,
        .focused = candidate.input.viewport_focused,
    });
  }
  const std::optional<ViewportPaneId> input_pane = resolve_viewport_input_pane({
      .active_panes = pane_candidates,
      .interaction_locked = interaction_locked,
      .interaction_owner = interaction_pane_id_,
      .focused_pane = workspace_->focused_pane(),
  });
  if (!input_pane.has_value()) {
    repeated_keybinding_commands_.clear();
    pending_marking_menu_gesture_.reset();
    interaction_controller_.cancel_pending_material_interaction();
    interaction_controller_.cancel_pending_selection_click();
    interaction_controller_.cancel_context_menu_pointer();
    for (Pane &pane_state : panes_) {
      pane_state.surface->set_pointer_capture(false);
    }
    hide_fly_navigation_huds();
    last_fly_navigation_active_ = false;
    const ViewportTool *executing_tool =
        find_viewport_tool(editor.tools().active_tool());
    set_modal_tool_owns_viewport_snapshot(
        native_tools.light_creation_active() ||
        (executing_tool != nullptr &&
         interaction_controller_.active_tool_owns_viewport(
             executing_tool->viewport_tool())) ||
        modeling.modal_tool_owns_viewport());
    return;
  }

  Pane &pane_state = pane(*input_pane);
  pointer_cursor_pane_ = *input_pane;
  QtViewportInputFrame &sample = samples[viewport_pane_index(*input_pane)];
  if (viewport_frame_diagnostics_enabled()) {
    g_viewport_probe.mouse_events += sample.mouse_move_events;
  }
  const bool pointer_pressed = sample.left_pressed || sample.right_pressed ||
                               sample.input.middle_pressed;
  const bool wheel_activity = sample.input.mouse_wheel != 0.0f ||
                              !sample.input.wheel_input_edges.empty();
  if (!interaction_locked && sample.input.viewport_hovered) {
    request_pane_focus(*input_pane, ViewportPaneFocusTrigger::Hover);
  }
  if (pointer_pressed || wheel_activity) {
    request_pane_focus(
        *input_pane, pointer_pressed ? ViewportPaneFocusTrigger::PointerPress
                                     : ViewportPaneFocusTrigger::Wheel);
    sample.input.viewport_focused = true;
  }
  if (pointer_pressed) {
    interaction_pane_id_ = *input_pane;
  }
  if (!interaction_locked) {
    // Presentation follows the pane without activating its selected tool.
    input_router_.set_active_tool(workspace_->selected_tool(*input_pane));
  }
  bool pane_owns_tool = editor.tools().pane_owns_executing_tool(*input_pane);

  ViewportInputFrame &input = sample.input;
  if (viewport_frame_diagnostics_enabled() &&
      (input.mouse_delta.x != 0.0f || input.mouse_delta.y != 0.0f)) {
    ++g_viewport_probe.motion_frames;
  }
  input.navigation_active = input_router_.navigating();
  input.fly_navigation_active = input_router_.fly_navigating();
  input.orbit_navigation_active = input_router_.orbit_navigating();
  input.pan_navigation_active = input_router_.pan_navigating();
  ViewportModelingToolPortAdapter preview_port_adapter(modeling);
  const ViewportToolPorts preview_ports = preview_port_adapter.ports();
  const ViewportSettings &viewport_settings = workspace_->effective_settings(*input_pane);
  const bool resolved_scene_snap_enabled =
      viewport_vertex_snap_radius(viewport_settings) > 0.0f;
  const bool resolved_grid_snap_enabled = viewport_grid_snap_active(
      viewport_settings, input.modifiers.primary);
  input.modal_tool_active =
      pane_owns_tool && (native_tools.light_creation_active() ||
      interaction_controller_.has_active_modal_preview(
          *preview_ports.preview, resolved_grid_snap_enabled) ||
      modeling.modal_tool_owns_viewport());
  input.active_tool_owns_viewport =
      pane_owns_tool && interaction_controller_.active_tool_owns_viewport(
          input_router_.state().active_tool);
  input.active_tool_blocks_focus_shortcuts =
      pane_owns_tool && interaction_controller_.active_tool_blocks_viewport_focus_shortcuts(
          input_router_.state().active_tool);
  // A pending gizmo click owns pointer routing, but it is not yet an active
  // transform. Keeping those contexts separate preserves deferred depth picks.
  input.active_transform_drag = interaction_controller_.dragging();
  set_modal_tool_owns_viewport_snapshot(input.active_tool_owns_viewport ||
                                        input.modal_tool_active);
  input.operation_preview_active = modeling.operation_preview_active();
  const ViewportToolId keybinding_tool = input_router_.state().active_tool;
  if (!pane_owns_tool && keybinding_tool == ViewportToolId::InsertEdgeLoop) {
    input.operation_preview_active = false;
    input.active_transform_drag = false;
  }
  const bool passive_curve_controls_available =
      interaction_controller_.active_tool_allows_selection_fallback(
          keybinding_tool) &&
      !input.active_tool_owns_viewport && !input.modal_tool_active;
  input.curve_controls_editable =
      native_presentation.curve_controls_editable &&
      ((native_presentation.curve_preview != nullptr &&
        native_presentation.curve_preview->active) ||
       passive_curve_controls_available);
  input.grid_preset = workspace_->grid_preset();
  ViewportCameraController &camera = workspace_->camera(*input_pane);
  // Orbit/pan depth sampling happens before the router's normal camera sync.
  // Keep the press-time ray correct on the first frame after a pane resize.
  camera.set_viewport_size(input.viewport_size.x, input.viewport_size.y);
  input.orthographic_view =
      camera.state().projection == ViewportProjection::Orthographic;
  const bool keybinding_edge_can_change_tool =
      !input.pressed_input_chords.empty() ||
      !input.wheel_input_edges.empty();
  std::erase_if(repeated_keybinding_commands_, [&](const auto &binding) {
    return binding.pane != *input_pane;
  });
  ViewportKeybindingResolver::resolve(
      editor, input_router_.state(), input, suppressed_keybinding_chords_,
      linear_array_wheel_remainder_, radial_array_wheel_remainder_,
      {.current_generation = &runtime_binding_generation_,
       .expected_generation = input_binding_generation},
      &repeated_keybinding_commands_);
  if (runtime_binding_generation_ != input_binding_generation) {
    // A synchronous command (for example Close Scene) rebound every runtime
    // owner while this stack still holds references to the previous scene.
    // The rebind has already abandoned Qt-owned state, so no old owner may be
    // observed again in this input frame.
    suppressed_keybinding_chords_.clear();
    repeated_keybinding_commands_.clear();
    linear_array_wheel_remainder_ = 0.0;
    radial_array_wheel_remainder_ = 0.0;
    return;
  }
  if (sample.context.has_value() &&
      sample.context->edit_generation != editor.mode().generation()) {
    // A queued mode command ran after this packet was captured. Retire its
    // pointer work before any tool can reinterpret it in the new edit set.
    for (Pane &other_pane : panes_) {
      other_pane.surface->synchronize_input_context();
    }
    interaction_controller_.reset(nullptr);
    native_tools.clear_pointer_drag_state();
    hover_controller_.invalidate();
    active_box_preview_ = {};
    pending_marking_menu_gesture_.reset();
    return;
  }
  const std::optional<QPoint> marking_menu_global_origin =
      resolve_marking_menu_gesture(editor, pane_state, input, interaction_locked);
  const bool active_tool_changed =
      keybinding_edge_can_change_tool &&
      sync_active_tool_from_editor(
          editor, modeling, native_tools,
          {.current_generation = &runtime_binding_generation_,
           .expected_generation = input_binding_generation});
  if (runtime_binding_generation_ != input_binding_generation) {
    return;
  }
  // Commands own only their originating mouse gesture. Keep the physical
  // release reserved across frames/modifier changes without hiding keyboard
  // commands (such as Flip Faces) from an otherwise active tool drag.
  const auto consume_command_pointer = [&](std::size_t index,
                                           QuaderMouseButton button,
                                           bool &pressed, bool &down,
                                           bool &released) {
    auto &suppression = command_pointer_release_suppression_[index];
    bool consumed = suppression.consume(button, pressed, down, released);
    const auto *latest_press =
        viewport_pointer_latest_edge(input.pointer_pressed_edges, button);
    for (const auto &edge : input.command_pointer_edges) {
      if (!edge.pointer_edge || edge.pointer_edge->button != button) continue;
      // An earlier command in a coalesced frame must not reserve a newer,
      // unrelated press of the same button.
      if (latest_press && latest_press->sequence != 0 &&
          edge.pointer_edge->sequence != 0 &&
          edge.pointer_edge->sequence < latest_press->sequence) continue;
      consumed = true;
      suppression.begin(button, down);
    }
    if (!consumed) return;
    pressed = down = released = false;
    const auto matches = [button](const auto &edge) {
      return edge.button == button;
    };
    std::erase_if(input.pointer_pressed_edges, matches);
    std::erase_if(input.pointer_released_edges, matches);
    std::erase_if(input.pointer_drag_started_edges, matches);
    std::erase_if(input.pointer_motion_samples, matches);
  };
  consume_command_pointer(0, QuaderMouseButton::Left, sample.left_pressed,
                          sample.left_down, sample.left_released);
  consume_command_pointer(1, QuaderMouseButton::Middle, input.middle_pressed,
                          input.middle_down, input.middle_released);
  consume_command_pointer(2, QuaderMouseButton::Right, sample.right_pressed,
                          sample.right_down, sample.right_released);
  input.right_pressed = sample.right_pressed;
  input.right_down = sample.right_down;
  input.right_released = sample.right_released;
  const bool pane_edit_start = sample.left_pressed && !input.text_input_active &&
      !input.navigation_active &&
      viewport_navigation_press(input.navigation_bindings).action == ViewportInputActionKind::None;
  if (pane_edit_start && !interaction_locked) {
    const auto acquired = editor.tools().acquire_pane_tool(*input_pane);
    if (runtime_binding_generation_ != input_binding_generation) return;
    if (!acquired.ok()) {
      notify_command_failure(editor, acquired);
      return;
    }
    if (acquired.changed()) {
      interaction_controller_.clear_modal_tool_state();
      interaction_controller_.reset(&editor, native_tools);
      if (runtime_binding_generation_ != input_binding_generation) return;
      hover_controller_.invalidate();
      active_box_preview_ = {};
      native_presentation = native_tools.presentation_snapshot();
    }
    pane_owns_tool = editor.tools().pane_owns_executing_tool(*input_pane);
  }
  if (active_tool_changed || pane_edit_start) {
    pane_owns_tool = editor.tools().pane_owns_executing_tool(*input_pane);
    input.modal_tool_active =
        pane_owns_tool && (native_tools.light_creation_active() ||
        interaction_controller_.has_active_modal_preview(
            *preview_ports.preview, resolved_grid_snap_enabled) ||
        modeling.modal_tool_owns_viewport());
    input.active_tool_owns_viewport =
        pane_owns_tool && interaction_controller_.active_tool_owns_viewport(
            input_router_.state().active_tool);
    input.active_tool_blocks_focus_shortcuts =
        pane_owns_tool && interaction_controller_.active_tool_blocks_viewport_focus_shortcuts(
            input_router_.state().active_tool);
    set_modal_tool_owns_viewport_snapshot(input.active_tool_owns_viewport ||
                                          input.modal_tool_active);
  }

  // Commands and pending sequences resolve first. The active tool may then
  // claim individual ordered packets before the remaining ones reach zoom.
  if (pane_owns_tool && !input.navigation_bindings.wheel_events.empty()) {
    std::optional<QuaderApiVoidResult> wheel_failure;
    ViewportBoxToolContext wheel_box;
    wheel_box.live_primitive = [&]() {
      return ViewportPrimitiveCreationApiAdapter::from_api(
          editor.tools().live_primitive_creation());
    };
    wheel_box.set_segments = [&](int axis, int count) {
      const auto result = set_primitive_segments(editor, axis, count);
      if (!result.ok && !wheel_failure.has_value()) {
        wheel_failure = QuaderApiVoidResult{.ok = false,
                                            .message = result.message};
      }
      return result;
    };
    wheel_box.raster_scale = viewport_raster_scale(*input_pane);
    wheel_box.primitive_controls_visible = viewport_settings.bool_value(ViewportBoolSettingId::ShowGizmos);
    ViewportToolContext wheel_context{
        .state = input_router_.state(), .camera = camera, .ports = preview_ports,
        .box = &wheel_box,
        .runtime_binding = {.current_generation = &runtime_binding_generation_,
                            .expected_generation = input_binding_generation}};
    const bool command_pointer_owned =
        !input.command_pointer_edges.empty() ||
        std::any_of(command_pointer_release_suppression_.begin(),
                    command_pointer_release_suppression_.end(),
                    [](const auto &owner) { return owner.active(); });
    (void)interaction_controller_.handle_active_tool_event(
        input_router_.state(), wheel_context,
        {.kind = ViewportToolEventKind::Wheel,
         .tool = input_router_.state().active_tool,
         .pointer = {
             .left_clicked = sample.left_pressed, .left_down = sample.left_down,
             .left_released = sample.left_released,
             .gizmo_consumed = command_pointer_owned ||
                 interaction_controller_.transform_gizmo_pointer_active() ||
                 interaction_controller_.point_light_range_pointer_active() ||
                 native_tools.pointer_dragging() || pane_state.surface->pointer_captured(),
             .navigation_active = input.navigation_active || input.fly_navigation_active ||
                 input.right_down || input.middle_down || input.right_pressed ||
                 input.middle_pressed || input.right_released || input.middle_released ||
                 viewport_navigation_press(input.navigation_bindings).action !=
                     ViewportInputActionKind::None,
             .text_input_active = input.text_input_active,
             .viewport_focused = input.viewport_focused,
             .pointer_interaction_canceled = input.pointer_interaction_canceled,
             .modifiers = input.modifiers,
             .wheel_events = input.navigation_bindings.wheel_events}});
    if (runtime_binding_generation_ != input_binding_generation) return;
    if (wheel_failure.has_value()) {
      notify_command_failure(editor, *wheel_failure);
      if (runtime_binding_generation_ != input_binding_generation) return;
    }
    std::erase_if(input.navigation_bindings.wheel_events,
                  [](const auto &wheel) { return wheel.consumed; });
    input.mouse_wheel = 0.0f;
    input.navigation_bindings.zoom_wheel = false;
    input.navigation_bindings.zoom_to_cursor_wheel = false;
    for (const auto &wheel : input.navigation_bindings.wheel_events) {
      input.mouse_wheel += wheel.steps;
      input.navigation_bindings.zoom_wheel |= wheel.action == ViewportInputActionKind::Zoom;
      input.navigation_bindings.zoom_to_cursor_wheel |=
          wheel.action == ViewportInputActionKind::ZoomToCursor;
    }
  }

  // Navigation depth comes from surfaces in this view. The exact press wins,
  // with the visible center surface covering gestures beside the subject.
  // Plain wheel prefers the center, then the pointer when that center is empty.
  // Keep the snapshot lazy and share it with later interaction picking.
  const ViewportLocalVisibilitySnapshot local_visibility =
      workspace_->local_visibility_snapshot();
  std::optional<SceneSurfaceQuerySnapshot> scene_surfaces;
  const auto ensure_scene_surfaces = [&]()
      -> const SceneSurfaceQuerySnapshot * {
    if (point_placement == nullptr) {
      return nullptr;
    }
    if (!scene_surfaces.has_value()) {
      scene_surfaces = point_placement->surface_snapshot(document, assets);
    }
    return &(*scene_surfaces);
  };
  const auto surface_navigation_anchor =
      [&](QVec2 local_position) -> std::optional<QVec3> {
    const SceneSurfaceQuerySnapshot *surfaces = ensure_scene_surfaces();
    if (surfaces == nullptr) {
      return std::nullopt;
    }
    const ViewportRay ray = camera.ray_from_viewport_point(local_position);
    const ViewportCameraPose camera_pose = camera.pose();
    const QVec3 forward = normalize(camera_pose.target - camera_pose.eye);
    const float forward_per_ray_unit = dot(ray.direction, forward);
    if (!std::isfinite(forward_per_ray_unit) ||
        forward_per_ray_unit <= 0.000001f) {
      return std::nullopt;
    }
    const float origin_depth = dot(ray.origin - camera_pose.eye, forward);
    const std::optional<SceneSurfaceHit> hit = surfaces->nearest_hit({
        .world_origin = ray.origin,
        .world_direction = ray.direction,
        .min_distance = std::max(
            0.0f, (camera.state().near_plane - origin_depth) /
                      forward_per_ray_unit),
        .max_distance = (camera.state().far_plane - origin_depth) /
                        forward_per_ray_unit,
        .include_backfaces = false,
        .eligibility = SceneSurfaceEligibility::RenderVisible,
    });
    return hit.has_value() ? std::optional<QVec3>{hit->world_position}
                           : std::nullopt;
  };
  const auto visible_selection_anchor =
      [&](QVec2 local_position) -> std::optional<QVec3> {
    const std::vector<EntityId> selected_entities =
        document.selected_entities();
    if (selected_entities.empty() ||
        !std::all_of(selected_entities.begin(), selected_entities.end(),
                     [&](EntityId entity) {
                       return document.is_effectively_visible(entity) &&
                              local_visibility.allows(
                                  static_cast<
                                      ViewportLocalVisibilityEntityId>(entity));
                     })) {
      return std::nullopt;
    }
    const std::optional<QVec3> center = editor.query().selection_center();
    if (!center.has_value()) {
      return std::nullopt;
    }
    const ViewportScreenPoint projected = camera.project_world_point(*center);
    if (!projected.visible || !std::isfinite(projected.position.x) ||
        !std::isfinite(projected.position.y) || projected.position.x < 0.0f ||
        projected.position.y < 0.0f ||
        projected.position.x > input.viewport_size.x ||
        projected.position.y > input.viewport_size.y) {
      return std::nullopt;
    }
    constexpr float kSelectionAnchorLogicalRadius = 64.0f;
    const float device_scale =
        std::isfinite(input.device_pixel_ratio) &&
                input.device_pixel_ratio > 0.0f
            ? input.device_pixel_ratio
            : 1.0f;
    const float maximum_distance =
        kSelectionAnchorLogicalRadius * device_scale;
    const QVec2 pointer_offset = projected.position - local_position;
    if (pointer_offset.x * pointer_offset.x +
            pointer_offset.y * pointer_offset.y >
        maximum_distance * maximum_distance) {
      return std::nullopt;
    }
    return center;
  };
  std::optional<QVec3> center_surface_anchor;
  bool center_surface_sampled = false;
  const auto center_navigation_anchor = [&]() -> std::optional<QVec3> {
    if (!center_surface_sampled) {
      center_surface_anchor =
          surface_navigation_anchor(input.viewport_size * 0.5f);
      center_surface_sampled = true;
    }
    return center_surface_anchor;
  };
  const ViewportNavigationPress navigation_press =
      viewport_navigation_press(input.navigation_bindings);
  const QVec2 navigation_press_local =
      navigation_press.pointer_edge.has_value()
          ? navigation_press.pointer_edge->local
          : input.mouse_local;
  if (navigation_press.action == ViewportInputActionKind::Orbit ||
      navigation_press.action == ViewportInputActionKind::OrbitSnap) {
    input.navigation_anchor =
        surface_navigation_anchor(navigation_press_local);
    if (!input.navigation_anchor.has_value()) {
      input.navigation_anchor =
          visible_selection_anchor(navigation_press_local);
    }
    if (!input.navigation_anchor.has_value()) {
      input.navigation_anchor = center_navigation_anchor();
    }
  } else if (navigation_press.action == ViewportInputActionKind::Pan) {
    input.navigation_anchor = surface_navigation_anchor(navigation_press_local);
    if (!input.navigation_anchor.has_value()) {
      input.navigation_anchor =
          visible_selection_anchor(navigation_press_local);
    }
    if (!input.navigation_anchor.has_value()) {
      input.navigation_anchor = center_navigation_anchor();
    }
  }
  if (!input.fly_navigation_active &&
      navigation_press.action != ViewportInputActionKind::Fly) {
    const auto wheel_navigation_anchor =
        [&](ViewportInputActionKind action,
            QVec2 local_position) -> std::optional<QVec3> {
      if (action == ViewportInputActionKind::ZoomToCursor) {
        if (const std::optional<QVec3> anchor =
                surface_navigation_anchor(local_position)) {
          return anchor;
        }
      } else if (action == ViewportInputActionKind::Zoom) {
        if (const std::optional<QVec3> anchor = center_navigation_anchor()) {
          return anchor;
        }
        return surface_navigation_anchor(local_position);
      } else {
        return std::nullopt;
      }
      return center_navigation_anchor();
    };
    if (!input.navigation_bindings.wheel_events.empty()) {
      for (ViewportNavigationWheelEvent &event :
           input.navigation_bindings.wheel_events) {
        if (!event.consumed && std::abs(event.steps) > 0.000001f) {
          event.navigation_anchor =
              wheel_navigation_anchor(event.action, event.local);
        }
      }
    } else if (input.mouse_wheel != 0.0f &&
               (input.navigation_bindings.zoom_wheel ||
                input.navigation_bindings.zoom_to_cursor_wheel)) {
      input.navigation_anchor = wheel_navigation_anchor(
          input.navigation_bindings.zoom_to_cursor_wheel
              ? ViewportInputActionKind::ZoomToCursor
              : ViewportInputActionKind::Zoom,
          input.mouse_local);
    }
  }

  const bool navigation_was_active = input_router_.navigating();
  const ViewportInputResult result = input_router_.handle_frame(input, camera);
  if (!navigation_was_active && input_router_.navigating()) {
    navigation_camera_owner_ = *input_pane;
  } else if (navigation_was_active && !input_router_.navigating()) {
    navigation_camera_owner_.reset();
  }
  for (Pane &candidate : panes_) {
    if (candidate.id != *input_pane) {
      candidate.fly_navigation_hud->hide();
    }
  }
  update_fly_navigation_hud(pane_state, result.fly_hud_active,
                            camera.fly_speed_is_default(), camera.fly_speed());
  if (result.maximized_toggle_requested) {
    (void)workspace_->focus_pane(*input_pane);
    (void)workspace_->toggle_focused_pane_maximized();
    refresh_workspace_layout();
  }
  if (result.grid_preset_changed) {
    workspace_->set_grid_preset(result.requested_grid_preset);
    input_router_.sync_grid_preset_snapshot(workspace_->grid_preset());
  }
  if (result.view_changed) {
    (void)workspace_->set_view(*input_pane, result.requested_view);
  }
  if (result.focus_document_requested) {
    const ViewportLocalVisibilitySnapshot local_visibility =
        workspace_->local_visibility_snapshot();
    (void)camera.focus_optional_bounds(
        ViewportSceneProjection::world_bounds(document, assets,
                                              local_visibility),
        ViewportEmptyFocusBehavior::FocusOrigin);
  }

  const bool modal_tool_owns_viewport_for_menu =
      input.active_tool_owns_viewport || input.modal_tool_active;

  if (result.shortcut_handled &&
      result.shortcut.kind == ViewportShortcutKind::SetSelectionMode) {
    const QuaderApiVoidResult mode_change = editor.selection().set_mode(
        to_api_selection_mode(
            viewport_selection_mode_to_app(result.shortcut.selection_mode)));
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
    log_command_failure("Selection mode change", mode_change);
    notify_command_failure(editor, mode_change);
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
    input_router_.set_selection_mode(app_selection_mode_to_viewport(
        to_modeling_selection_mode(editor.selection().summary().mode)));
    interaction_controller_.clear_modal_drag_state();
    interaction_controller_.reset(&editor, native_tools);
    hover_controller_.invalidate();
  }

  if (result.tool_activation_requested) {
    const ViewportTool *active_tool = find_viewport_tool(
        input_router_.state().active_tool);
    const bool editor_tool_matches_requested =
        active_tool != nullptr &&
        editor.tools().active_tool() == active_tool->id() &&
        editor.tools().pane_owns_executing_tool(*input_pane);
    if (should_dispatch_viewport_tool_activation({
            .activation_requested = true,
            .tool_changed = result.tool_changed,
            .editor_tool_matches_requested = editor_tool_matches_requested,
            .repeat_activation_when_active =
                active_tool != nullptr &&
                active_tool->descriptor().repeat_activation_when_active,
        })) {
      hover_controller_.invalidate();
      const QuaderApiVoidResult activation =
          active_tool != nullptr
              ? editor.tools().activate(input_router_.state().active_tool)
              : QuaderApiVoidResult{.ok = false,
                                    .changed = false,
                                    .message = "No active viewport tool."};
      if (runtime_binding_generation_ != input_binding_generation) {
        return;
      }
      if (!activation.ok) {
        log_command_failure("Viewport tool activation", activation);
        notify_command_failure(editor, activation);
        if (runtime_binding_generation_ != input_binding_generation) {
          return;
        }
        input_router_.set_active_tool(workspace_->selected_tool(*input_pane));
        return;
      }
      interaction_controller_.clear_modal_tool_state();
      interaction_controller_.reset(&editor, native_tools);
    }
  }

  {
    const ViewportShortcutControllerResult modeling_shortcut =
        shortcut_controller_.handle_modeling_shortcut(result, editor);
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
    if (modeling_shortcut.invalidate_hover) {
      hover_controller_.invalidate();
    }
  }

  const bool show_delete_popup =
      result.shortcut_handled &&
      result.shortcut.kind == ViewportShortcutKind::DeletePopup &&
      localization_ != nullptr;
  const bool show_quick_menu =
      result.shortcut_handled &&
      result.shortcut.kind == ViewportShortcutKind::AddPopup &&
      localization_ != nullptr;
  const bool show_marking_menu =
      result.shortcut_handled &&
      result.shortcut.kind == ViewportShortcutKind::MarkingMenu &&
      result.shortcut.initiating_chord.has_value() && localization_ != nullptr &&
      input.viewport_hovered && !input.text_input_active && !interaction_locked &&
      !input.pointer_interaction_canceled && !result.navigation_active &&
      !input.active_tool_owns_viewport && !input.modal_tool_active;
  QPoint shortcut_popup_global_point;
  if (show_delete_popup || show_quick_menu || show_marking_menu) {
    const qreal scale =
        std::max<qreal>(pane_state.surface->input_device_pixel_ratio(), 1.0);
    const QPoint local_point(
        static_cast<int>(std::lround(input.mouse_local.x /
                                     static_cast<float>(scale))),
        static_cast<int>(std::lround(input.mouse_local.y /
                                     static_cast<float>(scale))));
    shortcut_popup_global_point = pane_state.surface->visible_local_to_global(local_point);
    if (show_marking_menu && marking_menu_global_origin.has_value()) {
      shortcut_popup_global_point = *marking_menu_global_origin;
    }
  }

  if (show_marking_menu) {
    const QuaderInputChord initiating_chord = *result.shortcut.initiating_chord;
    // A complete tap can arrive in one sampled frame. Its release already
    // canceled the gesture at the origin; do not enter an unclosable hold loop.
    const bool already_released = std::any_of(
        input.released_input_chords.begin(), input.released_input_chords.end(),
        [&initiating_chord](const QuaderInputChord &released) {
          return released.device == initiating_chord.device &&
                 released.key == initiating_chord.key &&
                 released.mouse_button == initiating_chord.mouse_button;
        });
    if (!already_released) {
      const auto initiating_key_identity =
          pane_state.surface->held_keyboard_key_identity(initiating_chord);
      (void)workspace_->focus_pane(*input_pane);
      update_focused_pane_indicator();
      const bool changed = marking_menu_presenter_.show(
          *pane_state.surface_container, shortcut_popup_global_point, *input_pane,
          initiating_chord, editor, input_router_, *localization_, ui_scale_metrics_,
          {.current_generation = &runtime_binding_generation_,
           .expected_generation = input_binding_generation},
          initiating_key_identity);
      if (runtime_binding_generation_ != input_binding_generation) {
        return;
      }
      // The popup consumed the release. Discard its held-key/pointer samples
      // before the surface regains ownership of the next editing gesture.
      pane_state.surface->abandon_input();
      suppressed_keybinding_chords_.clear();
      repeated_keybinding_commands_.clear();
      editor.commands().reset_pending_viewport_keybinding_chord();
      if (changed) {
        hover_controller_.invalidate();
        sync_pane_control_selection(pane_state);
      }
    }
    return;
  }

  const bool suppress_hover_work =
      input.navigation_active || result.navigation_active || result.camera_changed;
  const bool point_lights_pickable =
      !suppress_hover_work && viewport_settings.bool_value(
                                  ViewportBoolSettingId::ShowGizmos) &&
      viewport_shading_shows_light_gizmos(
          workspace_->shading_mode(*input_pane));
  const float viewport_pixel_scale = viewport_raster_scale(*input_pane);
  std::span<const ViewportSceneBillboardPickCandidate> billboards;
  if (point_lights_pickable) {
    const std::uint64_t scene_revision = document.revision();
    const std::uint64_t selection_revision = document.selection_revision();
    const std::uint64_t local_visibility_revision =
        local_visibility.revision();
    if (!point_light_pick_cache_valid_ ||
        point_light_pick_cached_document_ != &document ||
        point_light_pick_cached_scene_revision_ != scene_revision ||
        point_light_pick_cached_selection_revision_ != selection_revision ||
        point_light_pick_cached_local_visibility_revision_ !=
            local_visibility_revision ||
        point_light_pick_cached_viewport_pixel_scale_ !=
            viewport_pixel_scale) {
      QDR_PROFILE_SCOPE("Viewport rebuild point light pick candidates");
      point_light_pick_cached_candidates_ =
          ViewportSceneProjection::point_light_pick_candidates(
              document, local_visibility, viewport_pixel_scale);
      point_light_pick_cached_document_ = &document;
      point_light_pick_cached_scene_revision_ = scene_revision;
      point_light_pick_cached_selection_revision_ = selection_revision;
      point_light_pick_cached_local_visibility_revision_ =
          local_visibility_revision;
      point_light_pick_cached_viewport_pixel_scale_ = viewport_pixel_scale;
      point_light_pick_cache_valid_ = true;
      ++point_light_pick_generation_;
    }
    billboards = point_light_pick_cached_candidates_;
  }
  ViewportInteractionFrame interaction_frame{
      .input_router = input_router_,
      .camera = camera,
      .settings = viewport_settings,
      .input = input,
      .input_result = result,
      .left_click_local =
          sample.left_pressed ? sample.left_click_local : input.mouse_local,
      .left_click_count = std::max(1, sample.left_click_count),
      .left_pressed = sample.left_pressed,
      .left_down = sample.left_down,
      .left_released = sample.left_released,
      .right_click_local =
          sample.right_pressed ? sample.right_click_local : input.mouse_local,
      .right_click_count = std::max(1, sample.right_click_count),
      .right_pressed = sample.right_pressed,
      .right_down = sample.right_down,
      .right_released = sample.right_released,
      .pointer_drag_threshold_pixels =
          static_cast<float>(QApplication::startDragDistance()) *
          input.device_pixel_ratio,
      .viewport_pixel_scale = viewport_pixel_scale,
      .pointer_capture_recentered = pane_state.surface->pointer_captured(),
      .double_click_interval_seconds =
          static_cast<double>(QApplication::doubleClickInterval()) / 1000.0,
      .monotonic_time_seconds = viewport_monotonic_time_seconds(),
      .active_tool_allows_selection_fallback =
          interaction_controller_.active_tool_allows_selection_fallback(
              input_router_.state().active_tool),
      .preferred_selection_tool =
          workspace_->preferred_selection_tool(*input_pane),
      .scene_billboard_pick_candidates = billboards,
      .scene_billboard_pick_revision = point_light_pick_generation_,
  };
  const ViewportContextMenuPointerResult context_menu_pointer =
      interaction_controller_.handle_context_menu_pointer(
          interaction_frame, modal_tool_owns_viewport_for_menu);
  ViewportContextMenuOpenRequest pending_context_menu_request{
      .modal_tool_owns_viewport = modal_tool_owns_viewport_for_menu,
      .right_press_tracked = context_menu_pointer.completed,
      .right_released = context_menu_pointer.completed,
      .release_inside_viewport =
          context_menu_pointer.release_inside_viewport,
      .press_screen = context_menu_pointer.press_screen,
      .release_screen = context_menu_pointer.release_screen,
      .accumulated_drag_delta =
          context_menu_pointer.accumulated_drag_delta,
      .max_click_distance_squared =
          context_menu_pointer.drag_threshold_pixels *
          context_menu_pointer.drag_threshold_pixels,
  };
  const bool pending_context_menu_click =
      is_viewport_context_menu_click_gesture(pending_context_menu_request);
  const QVec2 pending_context_menu_local_mouse =
      context_menu_pointer.release_local;
  if (result.cancel_requested &&
      !editor.tools().pane_owns_executing_tool(*input_pane)) {
    const auto canceled = editor.viewports().record_selected_tool(
        *input_pane, ViewportToolId::Select);
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
    if (canceled.ok()) {
      input_router_.set_active_tool(ViewportToolId::Select);
    }
  }
  if (!editor.tools().pane_owns_executing_tool(*input_pane)) {
    // Another pane owns the sole modeling session. Camera motion and commands
    // above remain available; passive focus/hover must not update its preview.
    last_fly_navigation_active_ = result.fly_cursor_capture_active;
    synchronize_pointer_capture(result.fly_cursor_capture_active
                                    ? input_pane : std::nullopt);
    auto &context = operation_input_context_;
    context.viewport_available = !input.text_input_active;
    context.orthographic = input.orthographic_view;
    context.orbit_available = !camera.orthographic_view_locked();
    context.shift = input.modifiers.shift;
    context.control = input.modifiers.primary;
    context.alt = input.modifiers.alt;
    context.grid_snap_available = viewport_snapping_active(viewport_settings);
    context.grid_snap_enabled = viewport_settings.bool_value(ViewportBoolSettingId::GridSnap);
    context.angle_snap_available = context.grid_snap_available;
    context.angle_snap_enabled = viewport_settings.bool_value(ViewportBoolSettingId::RotationSnap);
    context.navigation = input_router_.fly_navigating() ? QuaderInputNavigation::Fly
        : input_router_.orbit_navigating() ? QuaderInputNavigation::Orbit
        : input_router_.pan_navigating() ? QuaderInputNavigation::Pan
        : QuaderInputNavigation::None;
    if (pending_context_menu_click && localization_ != nullptr) {
      const auto selection = editor.selection().summary();
      const auto actions = editor.modeling().available_action_set({
          .surface = QuaderActionSurface::ViewportContextMenu,
          .modal_tool_owns_viewport = false,
          .include_disabled = true,
          .include_hidden = false,
      });
      const qreal dpr = std::max<qreal>(pane_state.surface->input_device_pixel_ratio(), 1.0);
      const QPoint local(static_cast<int>(std::lround(pending_context_menu_local_mouse.x / dpr)),
                         static_cast<int>(std::lround(pending_context_menu_local_mouse.y / dpr)));
      context_menu_presenter_.show(
          *pane_state.surface_container, pane_state.surface->visible_local_to_global(local),
          editor, input_router_, *localization_, actions, selection,
          selection.has_selection, std::nullopt,
          {.current_generation = &runtime_binding_generation_,
           .expected_generation = input_binding_generation});
    } else if (show_delete_popup) {
      (void)delete_popup_presenter_.show(
          *pane_state.surface_container, shortcut_popup_global_point, editor,
          modeling, input_router_, *localization_,
          {.current_generation = &runtime_binding_generation_,
           .expected_generation = input_binding_generation});
    } else if (show_quick_menu) {
      (void)quick_menu_presenter_.show(
          *pane_state.surface_container, shortcut_popup_global_point,
          *input_pane, editor,
          input_router_, *localization_,
          {.current_generation = &runtime_binding_generation_,
           .expected_generation = input_binding_generation});
    }
    return;
  }
  if ((interaction_frame.active_tool_allows_selection_fallback ||
       native_presentation.curve_controls_editable ||
       native_presentation.path_array_source_picking) &&
      !native_presentation.curve_dragging &&
      !editor.modeling().can_exit_instance_edit()) {
    interaction_frame.scene_curve_pick_candidates =
        curve_pick_cache_.candidates(document, assets, local_visibility);
  }
  const bool scene_surface_pick_requested =
      point_placement != nullptr &&
      ((input.viewport_hovered && !suppress_hover_work &&
        (interaction_frame.active_tool_allows_selection_fallback ||
         !interaction_frame.scene_curve_pick_candidates.empty() ||
         native_presentation.curve_controls_editable ||
         native_presentation.path_array_source_picking)) ||
       sample.left_pressed || sample.left_down || sample.left_released ||
       pending_context_menu_click || !input.curve_conversion_edges.empty());
  if (scene_surface_pick_requested) {
    interaction_frame.scene_surfaces = ensure_scene_surfaces();
  }
  interaction_frame.scene_mesh_snap_query =
      [&editor](const ViewportRay &ray, double vertex_radius,
                std::optional<double> maximum_ray_distance)
          -> ViewportMeshSnapQueryResult {
    // Curve controls have their own selected-anchor exclusion. Scene-selected
    // meshes stay stationary during this drag and remain valid landmarks.
    const std::optional<QuaderMeshVertexSnapTarget> target =
        editor.query().mesh_vertex_snap_target(
            {.origin = ray.origin, .direction = ray.direction}, vertex_radius,
            false, maximum_ray_distance);
    ViewportMeshSnapQueryResult result;
    if (target.has_value()) {
      result.vertex_target = ViewportMeshVertexSnapTarget{
          .modeling_object_id = target->modeling_object_id,
          .vertex_id =
              static_cast<quader_poly::ElementId>(target->vertex_id),
          .world_position = target->world_position,
          .ray_distance = target->ray_distance,
          .pick_distance = target->pick_distance,
      };
    }
    return result;
  };

  auto modal_tool_owns_viewport = [&]() {
    return native_tools.light_creation_active() ||
           interaction_controller_.active_tool_owns_viewport(
               input_router_.state().active_tool) ||
           interaction_controller_.has_active_modal_preview(
               *preview_ports.preview, resolved_grid_snap_enabled) ||
           modeling.modal_tool_owns_viewport();
  };
  auto cancel_active_modal_tool = [&](bool exit_tool) {
    if (native_tools.light_creation_active() ||
        interaction_controller_.active_tool_uses_editor_native_lifecycle(
            input_router_.state().active_tool)) {
      const QuaderApiVoidResult canceled =
          editor.tools().cancel_active_tool_preview();
      if (runtime_binding_generation_ != input_binding_generation) {
        return false;
      }
      log_command_failure("Native viewport tool cancel", canceled);
      notify_command_failure(editor, canceled);
      if (runtime_binding_generation_ != input_binding_generation) {
        return false;
      }
      native_tools.clear_pointer_drag_state();
      sync_active_tool_from_editor(
          editor, modeling, native_tools,
          {.current_generation = &runtime_binding_generation_,
           .expected_generation = input_binding_generation});
      if (runtime_binding_generation_ != input_binding_generation) {
        return false;
      }
      interaction_controller_.reset(&editor, native_tools);
      hover_controller_.invalidate();
      return true;
    }
    std::optional<QuaderApiVoidResult> cancel_failure;
    ViewportModelingToolPortCallbacks callbacks;
    callbacks.invalidate_hover = [&]() { hover_controller_.invalidate(); };
    callbacks.handle_tool_command_result =
        [&](std::string_view label, const ModelingCommandResult &command,
            bool announce_failure) {
          const QuaderApiVoidResult result{.ok = command.ok,
                                           .message = command.message};
          log_command_failure(label, result);
          if (!command.ok && announce_failure) {
            cancel_failure = result;
          }
        };
    ViewportModelingToolPortAdapter cancel_ports(modeling,
                                                 std::move(callbacks));
    ViewportToolContext cancel_context{
        .state = input_router_.state(),
        .camera = camera,
        .ports = cancel_ports.ports(),
    };
    cancel_context.set_active_tool = [&](ViewportToolId tool) {
      input_router_.set_active_tool(tool);
    };
    (void)interaction_controller_.handle_active_tool_event(
        input_router_.state(), cancel_context,
        ViewportToolEvent{.kind = ViewportToolEventKind::Cancel,
                          .tool = input_router_.state().active_tool,
                          .exit_active_modal_tool = exit_tool});
    if (runtime_binding_generation_ != input_binding_generation) {
      return false;
    }
    editor.tools().synchronize_pane_tool_after_execution(*input_pane);
    if (cancel_failure.has_value()) {
      notify_command_failure(editor, *cancel_failure);
      if (runtime_binding_generation_ != input_binding_generation) {
        return false;
      }
    }
    interaction_controller_.reset(&editor, native_tools);
    hover_controller_.invalidate();
    return true;
  };

  bool cancel_consumed = false;
  if (result.cancel_requested) {
    const bool active_drag_canceled =
        interaction_controller_.cancel_active_drag(editor, native_tools);
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
    if (active_drag_canceled) {
      cancel_consumed = true;
    }
    if (!cancel_consumed && modal_tool_owns_viewport()) {
      if (!cancel_active_modal_tool(true)) {
        return;
      }
      cancel_consumed = true;
    }
  }

  if (suppress_hover_work || result.grid_preset_changed) {
    hover_controller_.invalidate();
  }

  const ViewportToolPointerLifecycleResult pointer_lifecycle =
      interaction_controller_.handle_tool_pointer_lifecycle(
          editor, document, assets, native_tools, point_placement, camera,
          ViewportToolPointerLifecycleFrame{
              .interaction = interaction_frame,
              .native_tool =
                  {
                      .input = input,
                      .input_result = result,
                      .suppress_hover_work = suppress_hover_work,
                      .left_pressed = sample.left_pressed,
                       .left_down = sample.left_down,
                       .left_released = sample.left_released,
                       .left_click_local = sample.left_click_local,
                       .pointer_drag_threshold_pixels =
                           interaction_frame.pointer_drag_threshold_pixels,
                       .scene_snap_enabled = resolved_scene_snap_enabled,
                      .grid_snap_enabled = resolved_grid_snap_enabled,
                      .linear_array_wheel_remainder =
                          &linear_array_wheel_remainder_,
                      .radial_array_wheel_remainder =
                          &radial_array_wheel_remainder_,
                  },
              .cancel_consumed = cancel_consumed,
              .runtime_binding =
                  {.current_generation = &runtime_binding_generation_,
                   .expected_generation = input_binding_generation},
          });
  if (runtime_binding_generation_ != input_binding_generation) {
    return;
  }
  const ViewportNativeToolInteractionResult &native_tool_result =
      pointer_lifecycle.native_tool;
  if (native_tool_result.command_attempted &&
      !native_tool_result.command_result.ok) {
    const QuaderApiVoidResult placement_failure{
        .ok = false,
        .changed = native_tool_result.command_result.changed,
        .message = native_tool_result.command_result.message,
    };
    log_command_failure("Light placement", placement_failure);
    notify_command_failure(editor, placement_failure);
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
  }
  if (native_tool_result.sync_active_tool) {
    editor.tools().synchronize_pane_tool_after_execution(*input_pane);
    sync_active_tool_from_editor(
        editor, modeling, native_tools,
        {.current_generation = &runtime_binding_generation_,
         .expected_generation = input_binding_generation});
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
  }
  if (native_tool_result.claim_interaction_pane) {
    interaction_pane_id_ = *input_pane;
  }
  if (native_tool_result.invalidate_hover) {
    hover_controller_.invalidate();
  }
  interaction_frame.selection_state =
      pointer_lifecycle.interaction_selection;
  const QuaderSelectionSummary &interaction_selection =
      pointer_lifecycle.interaction_selection.summary;
  const ViewportBeforeToolInteractionResult &before_tool =
      pointer_lifecycle.before_tool;
  if (input.viewport_hovered && !input.text_input_active && !suppress_hover_work) {
    gizmo_input_pane_ = *input_pane;
  }

  ModelingSelectionSummary hover_selection =
      to_modeling_selection_summary(
          before_tool.selection_after_pending_pick.has_value()
              ? before_tool.selection_after_pending_pick->summary
              : interaction_selection);
  auto make_hover_key = [&](bool gizmo_consumed) {
    const ViewportSettings &settings = viewport_settings;
    const double vertex_radius =
        settings.float_value(ViewportFloatSettingId::PickVertexRadius);
    const double edge_radius =
        settings.float_value(ViewportFloatSettingId::PickEdgeRadius);
    return hover_controller_.make_key(
        camera,
        ViewportHoverWorkRequest{
            .pane_id = *input_pane,
            .active_tool = input_router_.state().active_tool,
            .selection_mode = input_router_.state().selection_mode,
            .box_drag_state =
                interaction_controller_
                    .box_preview(*preview_ports.preview,
                                 resolved_grid_snap_enabled)
                    .drag_state,
            .grid_preset = input_router_.state().grid_preset,
            .grid_snap_enabled = resolved_grid_snap_enabled,
            .mouse_local = input.mouse_local,
            .viewport_size = input.viewport_size,
            .modifiers = input.modifiers,
            .gizmo_consumed = gizmo_consumed,
            .viewport_pixel_scale = viewport_pixel_scale,
            .vertex_radius = vertex_radius,
            .edge_radius = edge_radius,
            .vertex_world_radius = viewport_scaled_world_pick_radius(
                vertex_radius, viewport_pixel_scale, input.device_pixel_ratio),
            .edge_world_radius = viewport_scaled_world_pick_radius(
                edge_radius,
                viewport_pixel_scale, input.device_pixel_ratio),
            .select_backfaces = settings.bool_value(
                ViewportBoolSettingId::BackfaceSelection),
            .scene_revision = document.revision(),
            .asset_revision = assets.revision(),
            .local_visibility_revision = local_visibility.revision(),
            .content_revision = hover_selection.content_revision,
            .selection_revision = hover_selection.selection_revision,
        });
  };
  auto make_tool_event_at = [&](QVec2 mouse_local, bool dragging = false) {
    const ViewportSettings &settings = viewport_settings;
    const double vertex_radius =
        settings.float_value(ViewportFloatSettingId::PickVertexRadius);
    const double edge_radius =
        settings.float_value(ViewportFloatSettingId::PickEdgeRadius);
    double edge_screen_radius =
        viewport_component_pick_radius_pixels(edge_radius);
    if (input_router_.state().active_tool == ViewportToolId::Knife &&
        !modeling.tool_status().knife_has_active_stroke) {
      // Knife must begin on topology, so a small persisted general-picking
      // radius still needs a practical UI-space target for the first edge.
      edge_screen_radius = std::max(
          edge_screen_radius, kKnifeEdgeStartMinimumRadiusLogicalPixels);
    }
    const ViewportRay ray = camera.ray_from_viewport_point(mouse_local);
    return ModelingToolRayEvent{
        .screen_position = mouse_local,
        .ray = {.origin = ray.origin, .direction = ray.direction},
        .camera =
            ViewportCameraAdapter::modeling_camera(camera, input.viewport_size),
        .grid_size =
            std::max(static_cast<double>(viewport_grid_world_size_for_preset(
                         input_router_.state().grid_preset)),
                     0.0001),
        .vertex_radius = viewport_scaled_world_pick_radius(
            vertex_radius, viewport_pixel_scale, input.device_pixel_ratio),
        .edge_radius = viewport_scaled_world_pick_radius(
            edge_radius, viewport_pixel_scale, input.device_pixel_ratio),
        .vertex_screen_radius =
            viewport_component_pick_radius_pixels(vertex_radius) *
            static_cast<double>(viewport_pixel_scale),
        .edge_screen_radius = edge_screen_radius *
                              static_cast<double>(viewport_pixel_scale),
        .snap_enabled = resolved_grid_snap_enabled,
        .constrain_axis = input.modifiers.shift,
        .dragging = dragging,
    };
  };
  std::optional<std::string> tool_error_notification;
  ViewportModelingToolPortCallbacks tool_callbacks;
  tool_callbacks.claim_hover = [&](ModelingBoxDragState) {
    return hover_controller_.claim(make_hover_key(before_tool.gizmo_consumed));
  };
  tool_callbacks.invalidate_hover = [&]() {
    hover_controller_.invalidate();
    hover_selection =
        to_modeling_selection_summary(editor.selection().summary());
  };
  tool_callbacks.set_selection_mode = [&](ViewportSelectionMode mode) {
    input_router_.set_selection_mode(mode);
  };
  tool_callbacks.set_app_selection_mode = [&](ModelingSelectionMode mode) {
    const QuaderApiVoidResult changed =
        editor.selection().set_mode(to_api_selection_mode(mode));
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
    log_command_failure("Selection mode change", changed);
    if (!changed.ok()) {
      tool_error_notification = changed.message.empty() ? "Command failed."
                                                         : changed.message;
    }
    input_router_.set_selection_mode(app_selection_mode_to_viewport(
        to_modeling_selection_mode(editor.selection().summary().mode)));
  };
  tool_callbacks.handle_tool_command_result =
      [&](std::string_view label, const ModelingCommandResult &command,
          bool announce_failure) {
        if (!command.ok) {
          LOG_ERROR("{} failed: {}", label,
                    command.message.empty() ? "Command failed."
                                            : command.message);
          if (announce_failure && !tool_error_notification.has_value()) {
            tool_error_notification = command.message.empty()
                                          ? "Command failed."
                                          : command.message;
          }
        }
        // The claimed hover key also throttles deterministic failures. Retrying
        // an unchanged failed query every frame only floods the log and can
        // leave stale presentation oscillating. A changed result invalidates
        // naturally; a failed key becomes eligible again when pointer, camera,
        // tool, or document state changes.
        if (command.changed) {
          hover_controller_.invalidate();
        }
      };
  ViewportModelingToolPortAdapter tool_ports(modeling,
                                             std::move(tool_callbacks));
  ViewportModelingToolCommitAdapter tool_commits(editor.tools());
  const auto record_direct_tool_failure = [&](const QuaderApiVoidResult &result) {
    if (!result.ok() && !tool_error_notification.has_value()) {
      tool_error_notification = result.message.empty() ? "Command failed."
                                                       : result.message;
    }
  };
  ViewportBoxToolContext box_context{.default_material = default_material};
  box_context.make_event_at = [&](QVec2 mouse_local) {
    return ViewportNativeToolInteraction::modeling_box_event(
        camera, input.viewport_size, mouse_local,
        viewport_grid_world_size_for_preset(input_router_.state().grid_preset),
        resolved_grid_snap_enabled);
  };
  box_context.accept_preview = [&](std::string name, AssetId material,
                                   bool snap_enabled) {
    QuaderMaterialReference selected_material;
    if (primitive_creation_material_provider_) {
      selected_material = primitive_creation_material_provider_();
      if (runtime_binding_generation_ != input_binding_generation) {
        return ModelingCommandResult{.ok = false};
      }
    }
    const QuaderApiVoidResult result = primitive_creation_material_provider_
        ? editor.tools().accept_active_primitive(
              std::move(name), selected_material, snap_enabled)
        : editor.tools().accept_active_primitive(
              std::move(name), material, snap_enabled);
    record_direct_tool_failure(result);
    return ModelingCommandResult{
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  };
  box_context.apply_session = [&]() {
    const QuaderApiVoidResult result =
        editor.tools().commit_active_tool_preview();
    record_direct_tool_failure(result);
    return ModelingCommandResult{
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  };
  box_context.cancel_session = [&]() {
    const QuaderApiVoidResult result =
        editor.tools().cancel_active_tool_preview();
    record_direct_tool_failure(result);
    return ModelingCommandResult{
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  };
  box_context.session_active = [&]() {
    return editor.tools().has_active_primitive_creation();
  };
  box_context.next_mesh_name = [&]() {
    const std::string active_name =
        editor.tools().active_primitive_default_name();
    const std::string_view base_name =
        active_name.empty() ? std::string_view{"Mesh"} : active_name;
    return SceneEntityNaming::next_name(document, base_name);
  };
  const auto cube_result = [](const QuaderApiVoidResult &result) {
    return ModelingCommandResult{.ok = result.ok(), .changed = result.changed(),
                                  .message = std::string(result.error_message())};
  };
  box_context.live_primitive = [&]() {
    return ViewportPrimitiveCreationApiAdapter::from_api(
        editor.tools().live_primitive_creation());
  };
  box_context.begin_resize = [&](ViewportPrimitiveResizeControl control, QVec3 axis) {
    const auto result = editor.tools().begin_primitive_resize(
        ViewportPrimitiveCreationApiAdapter::to_api(control), axis);
    record_direct_tool_failure(result);
    return cube_result(result);
  };
  box_context.update_resize = [&](double displacement) {
    return cube_result(editor.tools().update_primitive_resize(displacement));
  };
  box_context.end_resize = [&]() {
    const auto result = editor.tools().end_primitive_resize();
    record_direct_tool_failure(result);
    return cube_result(result);
  };
  box_context.cancel_resize = [&]() {
    const auto result = editor.tools().cancel_primitive_resize();
    record_direct_tool_failure(result);
    return cube_result(result);
  };
  box_context.set_segments = [&](int axis, int count) {
    const auto result = set_primitive_segments(editor, axis, count);
    record_direct_tool_failure(
        {.ok = result.ok, .changed = result.changed, .message = result.message});
    return result;
  };
  box_context.raster_scale = viewport_pixel_scale;
  box_context.primitive_controls_visible = viewport_settings.bool_value(ViewportBoolSettingId::ShowGizmos);
  ViewportPointToolContext point_context;
  point_context.make_event_at = make_tool_event_at;
  point_context.hit_drawn_plane_captured_point =
      [&](const std::array<QVec3, 3> &points, int count) {
        return ViewportNativeToolInteraction::hit_drawn_plane_captured_point(
            points, count, sample.left_click_local, camera,
            viewport_pixel_scale);
      };
  point_context.hit_drawn_plane_line = [&](const std::array<QVec3, 3> &points,
                                           int count, bool has_line) {
    return ViewportNativeToolInteraction::hit_drawn_plane_line(
        points, count, has_line, sample.left_click_local, camera,
        viewport_pixel_scale);
  };
  ViewportToolContext tool_context{
      .state = input_router_.state(),
      .camera = camera,
      .ports = tool_ports.ports(&tool_commits),
      .document = &document,
      .settings = &viewport_settings,
      .box = &box_context,
      .point = &point_context,
      .runtime_binding = {.current_generation = &runtime_binding_generation_,
                          .expected_generation = input_binding_generation},
  };
  tool_context.set_active_tool = [&](ViewportToolId tool) {
    input_router_.set_active_tool(tool);
  };
  const bool gizmo_consumed =
      cancel_consumed ||
      native_tool_result.consumed ||
      before_tool.gizmo_consumed;
  const std::uint64_t tool_command_revision =
      editor.commands().last_status_revision();
  const ViewportToolResult active_tool_result =
      interaction_controller_.handle_active_tool_event(
          input_router_.state(), tool_context,
          ViewportToolEvent{
              .kind = ViewportToolEventKind::Frame,
              .tool = input_router_.state().active_tool,
              .pointer =
                  {
                      .hovered = input.viewport_hovered,
                      .left_clicked = sample.left_pressed,
                      .left_down = sample.left_down,
                      .left_released = sample.left_released,
                      .can_interact = input.viewport_hovered &&
                                      !gizmo_consumed &&
                                      !input.navigation_active &&
                                      !result.navigation_active,
                      .finalize_pressed = input.modal_tool_finalize_pressed,
                      .mode_cycle_pressed = input.modal_tool_mode_cycle_pressed,
                      .suppress_hover_work = suppress_hover_work,
                      .gizmo_consumed = gizmo_consumed,
                      .navigation_active = input.navigation_active ||
                                           result.navigation_active,
                      .text_input_active = input.text_input_active,
                      .viewport_focused = input.viewport_focused,
                      .pointer_interaction_canceled =
                          input.pointer_interaction_canceled,
                      .mouse_local = input.mouse_local,
                      .mouse_screen = input.mouse_screen,
                      .left_click_local = sample.left_pressed
                                              ? sample.left_click_local
                                              : input.mouse_local,
                      .viewport_size = input.viewport_size,
                      // Live modifiers drive hover UI/snapping. Press edges
                      // carry their own modifiers for frozen click intent.
                      .modifiers = input.modifiers,
                      .pointer_pressed_edges = input.pointer_pressed_edges,
                      .pointer_released_edges = input.pointer_released_edges,
                      .pointer_drag_threshold_pixels =
                          interaction_frame.pointer_drag_threshold_pixels,
                      .viewport_pixel_scale = viewport_pixel_scale,
                      .monotonic_time_seconds =
                          interaction_frame.monotonic_time_seconds,
                      .camera_changed = result.camera_changed,
                  },
          });
  if (runtime_binding_generation_ != input_binding_generation) {
    return;
  }
  if (active_tool_result.active_tool_changed ||
      active_tool_result.sync_active_tool_from_modeling) {
    editor.tools().synchronize_pane_tool_after_execution(*input_pane);
  }
  if (active_tool_result.settings_operation_started) {
    editor.tools().record_active_settings_origin(*input_pane);
    refresh_settings_overlay(editor);
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
  }
  if (tool_error_notification.has_value() &&
      editor.commands().last_status_revision() == tool_command_revision) {
    // Present only after the tool has returned: notification hooks can replace
    // its runtime owners. Hover and continuous preview failures stay silent.
    (void)editor.ui().notify(
        {.level = QuaderNotificationLevel::Error,
         .message = std::move(*tool_error_notification)});
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
  }
  active_box_preview_ = interaction_controller_.box_preview(
      *preview_ports.preview, resolved_grid_snap_enabled);
  interaction_controller_.handle_after_active_tool(
      editor, interaction_frame, active_tool_result, before_tool,
      native_tools, tool_context.ports.hover,
      {.current_generation = &runtime_binding_generation_,
       .expected_generation = input_binding_generation});
  if (runtime_binding_generation_ != input_binding_generation) {
    return;
  }

  bool show_context_menu = false;
  QPoint context_menu_global_point;
  QuaderSelectionSummary context_menu_selection;
  std::optional<QuaderHoverTarget> context_menu_material_face_target;
  const bool selection_fallback_active =
      interaction_controller_.active_tool_allows_selection_fallback(
          input_router_.state().active_tool);
  if (!active_tool_result.handled && selection_fallback_active &&
      pending_context_menu_click && !gizmo_consumed) {
    ViewportInputFrame context_input = input;
    context_input.mouse_local = pending_context_menu_local_mouse;
    const ViewportInteractionFrame context_frame{
        .input_router = input_router_,
        .camera = camera,
        .settings = viewport_settings,
        .input = context_input,
        .input_result = result,
        .right_click_local = pending_context_menu_local_mouse,
        .viewport_pixel_scale = viewport_pixel_scale,
        .active_tool_allows_selection_fallback = selection_fallback_active,
        .preferred_selection_tool =
            workspace_->preferred_selection_tool(*input_pane),
        .scene_billboard_pick_candidates = billboards,
        .scene_curve_pick_candidates =
            interaction_frame.scene_curve_pick_candidates,
        .scene_surfaces = interaction_frame.scene_surfaces,
    };
    context_menu_material_face_target =
        interaction_controller_.pick_material_face_target(editor,
                                                          context_frame);
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
    context_menu_selection = interaction_controller_.pick_context_menu_target(
        editor, context_frame);
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
    pending_context_menu_request.target_hit =
        context_menu_selection.has_selection ||
        context_menu_material_face_target.has_value();
    // The presenter resolves pointer-specific transfers and curve settings.
    // Generic operations remain in Q; empty-space RMB has no independent actions.
    pending_context_menu_request.has_target_independent_actions = false;
    pending_context_menu_request.has_actions =
        pending_context_menu_request.target_hit;
    show_context_menu =
        should_open_viewport_context_menu(pending_context_menu_request) &&
        localization_ != nullptr;
    if (show_context_menu) {
      const qreal scale =
          std::max<qreal>(pane_state.surface->input_device_pixel_ratio(), 1.0);
      const QPoint local_point(
          static_cast<int>(std::lround(pending_context_menu_local_mouse.x /
                                       static_cast<float>(scale))),
          static_cast<int>(std::lround(pending_context_menu_local_mouse.y /
                                       static_cast<float>(scale))));
      context_menu_global_point = pane_state.surface->visible_local_to_global(local_point);
    }
    hover_controller_.invalidate();
  }
  if (interaction_controller_.transform_gizmo_pointer_active() ||
      interaction_controller_.point_light_range_pointer_active() ||
      interaction_controller_.primitive_creation_pointer_active() ||
      before_tool.gizmo_consumed ||
      has_input_activity(sample)) {
    interaction_pane_id_ = *input_pane;
  }

  last_fly_navigation_active_ = result.fly_cursor_capture_active;
  synchronize_pointer_capture(result.fly_cursor_capture_active
                                  ? input_pane : std::nullopt);
  if (show_context_menu) {
    context_menu_presenter_.show(
        *pane_state.surface_container, context_menu_global_point, editor,
        input_router_, *localization_, {},
        context_menu_selection, pending_context_menu_request.target_hit,
        context_menu_material_face_target,
        {.current_generation = &runtime_binding_generation_,
         .expected_generation = input_binding_generation});
    if (runtime_binding_generation_ != input_binding_generation) {
      return;
    }
  }
  if (show_delete_popup &&
      delete_popup_presenter_.show(
          *pane_state.surface_container, shortcut_popup_global_point, editor,
          modeling, input_router_, *localization_,
          {.current_generation = &runtime_binding_generation_,
           .expected_generation = input_binding_generation})) {
    hover_controller_.invalidate();
  }
  if (show_quick_menu) {
    (void)workspace_->focus_pane(*input_pane);
    update_focused_pane_indicator();
    if (quick_menu_presenter_.show(
            *pane_state.surface_container, shortcut_popup_global_point,
            *input_pane, editor,
            input_router_, *localization_,
            {.current_generation = &runtime_binding_generation_,
             .expected_generation = input_binding_generation})) {
      hover_controller_.invalidate();
      sync_pane_control_selection(pane_state);
    }
  }
  if (runtime_binding_generation_ != input_binding_generation) {
    return;
  }
  const ModelingToolStatus final_tool_status = modeling.tool_status();
  modal_tool_dragging_ = final_tool_status.insert_edge_loop_dragging ||
                         final_tool_status.cut_dragging ||
                         final_tool_status.mirror_dragging;
  set_modal_tool_owns_viewport_snapshot(modal_tool_owns_viewport());
  edge_length_hud_enabled_ =
      input.viewport_hovered && !input.text_input_active &&
      !suppress_hover_work && !gizmo_consumed && !before_tool.gizmo_hovered &&
      !modal_tool_owns_viewport_snapshot_ && !modal_tool_dragging_ &&
      !interaction_controller_.transform_gizmo_pointer_active() &&
      !interaction_controller_.point_light_range_pointer_active() &&
      !native_tools.pointer_dragging() &&
      !show_context_menu && !show_delete_popup && !show_quick_menu;
  primitive_feedback_input_eligible_ =
      !input.text_input_active &&
      !input.pointer_interaction_canceled && !result.navigation_active &&
      (input.viewport_hovered || interaction_controller_.primitive_creation_pointer_active()) &&
      viewport_settings.bool_value(ViewportBoolSettingId::ShowGizmos) &&
      !show_context_menu && !show_delete_popup && !show_quick_menu;

  auto &hint_context = operation_input_context_;
  hint_context.viewport_available =
      !input.text_input_active && !input.pointer_interaction_canceled &&
      (input.viewport_hovered || input.viewport_focused || interaction_locked) &&
      !show_context_menu && !show_delete_popup && !show_quick_menu;
  if (!hint_context.viewport_available) return;
  interaction_controller_.populate_operation_input_context(hint_context, interaction_frame);
  if (hint_context.navigation == QuaderInputNavigation::None) {
    native_tools.populate_operation_input_context(hint_context);
    hint_context.pointer_dragging = hint_context.pointer_dragging ||
                                   native_tools.pointer_dragging() ||
                                   modal_tool_dragging_;
  }
}

bool QtViewportPanel::sync_active_tool_from_editor(
    QuaderEditorApi &editor, ModelingSessionService &modeling,
    NativeViewportToolCoordinator &native_tools,
    ViewportRuntimeBindingToken runtime_binding) {
  const ViewportSelectionMode selection_mode =
      app_selection_mode_to_viewport(modeling.selection_mode());
  const bool selection_mode_changed =
      input_router_.state().selection_mode != selection_mode;
  input_router_.set_selection_mode(selection_mode);
  input_router_.sync_grid_preset_snapshot(workspace_->grid_preset());

  const std::uint64_t revision = editor.tools().active_tool_revision();
  if (active_tool_revision_valid_ && active_tool_revision_ == revision &&
      !selection_mode_changed) {
    return false;
  }
  const ViewportTool *editor_tool =
      find_viewport_tool(editor.tools().active_tool());
  const ViewportToolId active_tool = editor_tool != nullptr
                                         ? editor_tool->viewport_tool()
                                         : ViewportToolId::Select;
  const bool changed = selection_mode_changed ||
                       input_router_.state().active_tool != active_tool;
  active_tool_revision_ = revision;
  active_tool_revision_valid_ = true;
  if (changed) {
    linear_array_wheel_remainder_ = 0.0;
    radial_array_wheel_remainder_ = 0.0;
    input_router_.set_active_tool(active_tool);
    interaction_controller_.clear_modal_tool_state();
    native_tools.clear_pointer_drag_state();
    const QuaderApiVoidResult cleared_hover = editor.selection().clear_hover();
    if (!runtime_binding.current()) {
      return changed;
    }
    if (!cleared_hover.ok()) {
      LOG_ERROR("Viewport tool or selection mode hover clear failed: {}",
                cleared_hover.error_message().empty()
                    ? "Hover clear failed."
                    : cleared_hover.error_message());
    }
    interaction_controller_.reset(&editor, native_tools);
    if (!runtime_binding.current()) {
      return changed;
    }
    hover_controller_.invalidate();
    active_box_preview_ = {};
  }
  return changed;
}

void QtViewportPanel::set_pane_view(Pane &pane_state, ViewportView view) {
  if (workspace_ != nullptr) {
    (void)workspace_->set_view(pane_state.id, view);
    sync_pane_control_selection(pane_state);
  }
}

void QtViewportPanel::set_pane_shading(Pane &pane_state,
                                       ViewportShadingMode shading) {
  if (!release_has_feature(ReleaseFeature::RenderedViewport) &&
      shading != ViewportShadingMode::Wireframe &&
      shading != ViewportShadingMode::Solid &&
      shading != ViewportShadingMode::Shaded) {
    return;
  }
  if (workspace_ != nullptr) {
    (void)workspace_->set_shading_mode(pane_state.id, shading);
    sync_pane_control_selection(pane_state);
  }
}

QtViewportPanel::Pane &QtViewportPanel::pane(ViewportPaneId pane_id) {
  return panes_[viewport_pane_index(pane_id)];
}

const QtViewportPanel::Pane &
QtViewportPanel::pane(ViewportPaneId pane_id) const {
  return panes_[viewport_pane_index(pane_id)];
}

} // namespace quader
