////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_system.hpp"

#include "editor/material/default_material_texture_store.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "assets/asset_registry.hpp"
#include "assets/default_material_catalog.hpp"
#include "core/logger.hpp"
#include "core/release_features.hpp"
#include "core/scene_environment_catalog.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/transform/mesh_arrangement_service.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "platform/platform_file_dialog.hpp"
#include "platform/platform_shell.hpp"
#include "platform/window_placement.hpp"
#include "quader_app_version.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_material_surface.hpp"
#include "ui/standard_material_preview_render_snapshot_adapter.hpp"
#include "ui/console_panel.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/layer_panel.hpp"
#include "ui/outliner_panel.hpp"
#include "ui/qt_face_texture_panel.hpp"
#include "ui/qt_face_texture_canvas.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_command_palette.hpp"
#include "ui/qt_contextual_input_hints.hpp"
#include "ui/qt_dock_title_presentation.hpp"
#include "ui/qt_main_window.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_operation_input_hints.hpp"
#include "ui/qt_material_browser_window.hpp"
#include "ui/qt_asset_browser.hpp"
#include "ui/qt_asset_thumbnail_cache.hpp"
#include "ui/qt_material_find_replace_dialog.hpp"
#include "ui/qt_material_preview_surface.hpp"
#include "ui/qt_project_home_page.hpp"
#include "ui/qt_retained_message_box.hpp"
#include "ui/qt_scene_workspace.hpp"
#include "ui/qt_selection_toolbar.hpp"
#include "ui/qt_settings_dialog.hpp"
#include "ui/qt_snapping_toolbar.hpp"
#include "ui/qt_status_message.hpp"
#include "ui/qt_texture_toolbar.hpp"
#include "ui/qt_texture_info_window.hpp"
#include "ui/qt_standard_material_editor_window.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_unsaved_confirmation_dialog.hpp"
#include "ui/qt_viewport_display_toolbar.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_vulkan_composition_widget.hpp"
#include "ui/ui_context.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QColor>
#include <QDialog>
#include <QDockWidget>
#include <QFrame>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QLocale>
#include <QMainWindow>
#include <QMenuBar>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QRect>
#include <QScopedValueRollback>
#include <QSize>
#include <QStatusBar>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <qpa/qplatformwindow.h>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr int kQtWindowStateVersion = 1;
constexpr int kDockContentMinimumWidth = 220;

static_assert(QT_VERSION == QT_VERSION_CHECK(6, 10, 3),
              "Review synchronous native geometry delivery before upgrading Qt.");

/** Covers the shell without suppressing the QRhi children below its paint. */
class QtLoadingCover final : public QWidget {
public:
  QtLoadingCover(QWidget &parent, bool composed, int logo_extent)
      : QWidget(&parent), logo_extent_(logo_extent), composed_(composed) {
    setObjectName(QStringLiteral("quaderLoadingCover"));
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_NativeWindow, !composed);
    setFocusPolicy(Qt::NoFocus);
    parent.installEventFilter(this);
    hide();
  }

  void set_logo_extent(int extent) {
    if (logo_extent_ != extent) {
      logo_extent_ = extent;
      update();
    }
  }

  void flush_presentation() {
    QWidget *root = window();
    QWindow *host = root->windowHandle();
    if (host != nullptr && host->handle() != nullptr &&
        root->geometry() != host->geometry()) {
      // Qt may expose the real maximized client before delivering its queued
      // geometry to QWidget. On the GUI thread this delivers only that report
      // through Qt's existing adapter, preserving native placement and normal
      // restore geometry. It does not pump input or resize the native window.
      QWindowSystemInterface::handleGeometryChange<
          QWindowSystemInterface::SynchronousDelivery>(
          host, host->handle()->geometry());
    }
    if (QLayout *layout = root->layout()) {
      (void)layout->activate();
    }
    if (geometry() != root->rect()) {
      setGeometry(root->rect());
    }
    const auto previous_paint = paint_count_;
    // Repaint the root: in the composed path a cover paint alone is not
    // evidence that Qt submitted the backing store to the native window.
    window()->repaint();
    // QtMainWindow permits two input-before-paint deferrals. Deliver only its
    // queued repaint so keyboard-triggered loading cannot skip this cover.
    for (unsigned int attempt = 0u; attempt != 3u && isVisible() &&
         paint_count_ == previous_paint; ++attempt) {
      QCoreApplication::sendPostedEvents(window(), QEvent::UpdateRequest);
    }
  }

  void begin_presentation() {
    presentation_begin_paint_count_ = paint_count_;
    first_presented_at_ = {};
    last_paint_has_logo_ = false;
  }

  void note_composited_submission() {
    if (first_presented_at_ == std::chrono::steady_clock::time_point{} &&
        last_paint_has_logo_ && paint_count_ != presentation_begin_paint_count_ &&
        logo_geometry_ready()) {
      first_presented_at_ = std::chrono::steady_clock::now();
    }
  }

  [[nodiscard]] bool logo_presented() const {
    return first_presented_at_ != std::chrono::steady_clock::time_point{};
  }

  [[nodiscard]] bool minimum_duration_elapsed() const {
    constexpr auto kMinimumLoadingDuration = std::chrono::seconds(2);
    return first_presented_at_ != std::chrono::steady_clock::time_point{} &&
           std::chrono::steady_clock::now() - first_presented_at_ >=
               kMinimumLoadingDuration;
  }

  void set_input_blocked(bool blocked) {
    if (blocked) {
      QApplication::instance()->installEventFilter(this);
    } else {
      QApplication::instance()->removeEventFilter(this);
    }
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
      setGeometry(parentWidget()->rect());
    }
    if (isVisible()) {
      switch (event->type()) {
      case QEvent::ShortcutOverride:
      case QEvent::KeyPress:
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonDblClick:
      case QEvent::Wheel:
      case QEvent::ContextMenu:
        for (QObject *target = watched; target != nullptr; target = target->parent()) {
          if (target == parentWidget() || target == window()->windowHandle()) {
            event->accept();
            return true;
          }
        }
        break;
      default:
        break;
      }
    }
    return QWidget::eventFilter(watched, event);
  }

  bool event(QEvent *event) override {
    switch (event->type()) {
    case QEvent::ShortcutOverride:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::ContextMenu:
      event->accept();
      return true;
    default:
      return QWidget::event(event);
    }
  }

  void paintEvent(QPaintEvent *) override {
    last_paint_has_logo_ = false;
    QPainter painter(this);
    // An opaque-widget hint would let Qt cull the underlying scene texture.
    painter.fillRect(rect(), palette().brush(QPalette::Window));
    // Spontaneous show/expose paints can precede Qt's retained resize report.
    // Keep the same backdrop until the logo's actual client center is valid.
    if (!logo_geometry_ready()) {
      return;
    }
    const QSize extent(logo_extent_, logo_extent_);
    const QRect logo_rect(QPoint((width() - extent.width()) / 2,
                                (height() - extent.height()) / 2), extent);
    QApplication::windowIcon().paint(&painter, logo_rect);
    ++paint_count_;
    last_paint_has_logo_ = true;
    if (!composed_ && first_presented_at_ == std::chrono::steady_clock::time_point{}) {
      first_presented_at_ = std::chrono::steady_clock::now();
    }
  }

private:
  [[nodiscard]] bool logo_geometry_ready() const {
    const QWidget *root = window();
    const QWindow *host = root->windowHandle();
    if (!isVisible() || host == nullptr || !host->isExposed() ||
        root->isMinimized() || root->geometry() != host->geometry() ||
        geometry() != root->rect()) {
      return false;
    }
    void *const native_handle = reinterpret_cast<void *>(
        static_cast<std::uintptr_t>(host->winId()));
    const bool requested_maximized =
        root->windowState().testFlag(Qt::WindowMaximized);
    if (requested_maximized != platform_window_is_maximized(native_handle)) {
      return false;
    }
    const PlatformWindowClientExtent native_client =
        platform_window_client_extent(native_handle);
    const qreal pixel_scale = root->devicePixelRatioF();
    return qt_startup_client_extents_match(
        static_cast<int>(std::lround(root->width() * pixel_scale)),
        static_cast<int>(std::lround(root->height() * pixel_scale)),
        native_client.width, native_client.height);
  }

  int logo_extent_ = 0;
  std::uint64_t paint_count_ = 0u;
  std::uint64_t presentation_begin_paint_count_ = 0u;
  std::chrono::steady_clock::time_point first_presented_at_{};
  bool composed_ = false;
  bool last_paint_has_logo_ = false;
};

/** Keeps the menu/toolbar boundary on the same quiet palette role as the status bar. */
class QtMainMenuBar final : public QMenuBar {
public:
  using QMenuBar::QMenuBar;

protected:
  void paintEvent(QPaintEvent *event) override {
    QMenuBar::paintEvent(event);
    QPainter painter(this);
    const qreal extent = 1.0 / std::max<qreal>(1.0, devicePixelRatioF());
    painter.fillRect(QRectF(0.0, height() - extent, width(), extent),
                     palette().brush(QPalette::Midlight));
  }
};

/** Keeps the native status bar layout with a quiet, palette-aware top edge. */
class QtMainStatusBar final : public QStatusBar {
public:
  using QStatusBar::QStatusBar;

protected:
  void paintEvent(QPaintEvent *event) override {
    QStatusBar::paintEvent(event);
    QPainter painter(this);
    painter.fillRect(QRectF(0, 0, width(), 1.0 / devicePixelRatioF()),
                     palette().brush(QPalette::Midlight));
  }
};

void apply_dock_content_minimum_width(
    QWidget &content, const QtUiScaleMetrics &metrics) {
  const int minimum_width = metrics.px(kDockContentMinimumWidth);
  if (content.minimumWidth() != minimum_width) {
    content.setMinimumWidth(minimum_width);
  }
}

constexpr std::array<QuaderStandardMaterialTextureSlot, 4>
    kMaterialTextureSlots{
        QuaderStandardMaterialTextureSlot::Albedo,
        QuaderStandardMaterialTextureSlot::Roughness,
        QuaderStandardMaterialTextureSlot::Metallic,
        QuaderStandardMaterialTextureSlot::Normal,
};

QuaderStandardMaterialTextureSlot domain_texture_slot(
    QtStandardMaterialTextureSlot slot) {
  return kMaterialTextureSlots[static_cast<std::size_t>(slot)];
}

QuaderStandardMaterialScalar domain_material_scalar(
    QtStandardMaterialScalar scalar) {
  switch (scalar) {
  case QtStandardMaterialScalar::Roughness:
    return QuaderStandardMaterialScalar::Roughness;
  case QtStandardMaterialScalar::Metallic:
    return QuaderStandardMaterialScalar::Metallic;
  case QtStandardMaterialScalar::NormalStrength:
    return QuaderStandardMaterialScalar::NormalStrength;
  }
  return QuaderStandardMaterialScalar::Roughness;
}

QuaderStandardMaterialTextureTransformComponent
domain_texture_transform_component(
    QtStandardMaterialTextureTransformComponent component) {
  using Source = QtStandardMaterialTextureTransformComponent;
  using Target = QuaderStandardMaterialTextureTransformComponent;
  switch (component) {
  case Source::CenterU:
    return Target::CenterU;
  case Source::CenterV:
    return Target::CenterV;
  case Source::ScaleU:
    return Target::ScaleU;
  case Source::ScaleV:
    return Target::ScaleV;
  case Source::OffsetU:
    return Target::OffsetU;
  case Source::OffsetV:
    return Target::OffsetV;
  case Source::RotationDegrees:
    return Target::RotationDegrees;
  }
  return Target::CenterU;
}

MaterialPreviewMeshKind preview_mesh_kind(
    QtStandardMaterialPreviewMesh mesh) {
  return static_cast<MaterialPreviewMeshKind>(mesh);
}

MaterialPreviewShadingMode preview_shading_mode(
    QtStandardMaterialPreviewShading shading) {
  return static_cast<MaterialPreviewShadingMode>(shading);
}

std::optional<ProjectRelativePath> project_relative_dialog_path(
    const std::filesystem::path &selected,
    const std::filesystem::path &project_root) {
  if (selected.empty() || project_root.empty()) {
    return std::nullopt;
  }
  const std::filesystem::path relative =
      selected.lexically_normal().lexically_relative(
          project_root.lexically_normal());
  ProjectIoResult<ProjectRelativePath> parsed =
      ProjectRelativePath::parse(relative);
  return parsed.ok() ? std::optional(parsed.take_value()) : std::nullopt;
}

QuaderApiVoidResult runtime_status(bool changed, std::string message = {}) {
  return {
      .ok = true,
      .changed = changed,
      .message = std::move(message),
      .mutation_kind =
          changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None,
  };
}

QuaderApiVoidResult runtime_failure(std::string message) {
  return {
      .ok = false,
      .changed = false,
      .message = std::move(message),
      .mutation_kind = QuaderMutationKind::None,
  };
}

std::optional<UIPanelId> panel_id_from_name(std::string_view name) {
  if (name == ui_panel_stable_id(UIPanelId::Viewport) ||
      name == ui_panel_key(UIPanelId::Viewport)) {
    return UIPanelId::Viewport;
  }
  for (UIPanelId id : kUiDockPanelIds) {
    if (name == ui_panel_stable_id(id) || name == ui_panel_key(id)) {
      return id;
    }
  }
  return std::nullopt;
}

std::string localized_release_message(
    const LocalizationService &localization, const std::string &message) {
  if (message == release_feature_unavailable_message()) {
    return localization.text("release.feature_unavailable", message);
  }
  constexpr std::string_view kPrefix = "This file requires Quader ";
  if (!message.starts_with(kPrefix) || !message.ends_with('.')) {
    return message;
  }
  const std::string_view label = std::string_view(message).substr(
      kPrefix.size(), message.size() - kPrefix.size() - 1u);
  bool valid_label = label == "Internal Full";
  if (const auto separator = label.find('.');
      !valid_label && separator != std::string_view::npos) {
    std::uint32_t major = 0u;
    std::uint32_t minor = 0u;
    const char *const begin = label.data();
    const char *const end = begin + label.size();
    const auto parsed_major = std::from_chars(begin, begin + separator, major);
    const auto parsed_minor = std::from_chars(begin + separator + 1u, end, minor);
    valid_label = parsed_major.ec == std::errc{} &&
                  parsed_major.ptr == begin + separator &&
                  parsed_minor.ec == std::errc{} && parsed_minor.ptr == end &&
                  product_release_label(product_release(major, minor)) == label;
  }
  if (!valid_label) {
    return message;
  }
  std::string localized = localization.text(
      "release.file_requires", "This file requires Quader {release}.");
  constexpr std::string_view kToken = "{release}";
  if (const auto position = localized.find(kToken); position != std::string::npos) {
    localized.replace(position, kToken.size(), label);
  }
  return localized;
}

std::string status_bar_message(
    const LocalizationService &localization,
    const QuaderNotification &notification) {
  if (notification.level == QuaderNotificationLevel::Info) {
    return {};
  }
  if (notification.level == QuaderNotificationLevel::Error &&
      notification.message.empty()) {
    return localization.text("ui.status.command_failed", "Command failed.");
  }
  if (notification.message == mesh_arrangement_grid_alignment_warning()) {
    return localization.text("tool_settings.align.snap_unavailable",
                             notification.message);
  }
  return localized_release_message(localization, notification.message);
}

std::string read_binary_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return {};
  }
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool write_binary_file(const std::filesystem::path &path,
                       const QByteArray &bytes) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }
  output.write(bytes.constData(), bytes.size());
  return output.good();
}

QString material_deletion_scene_label(
    const QuaderProjectSceneMaterialDependencies &scene,
    const LocalizationService &localization) {
  const auto localized = [&localization](std::string_view key,
                                          std::string_view fallback) {
    return QString::fromStdString(localization.text(key, fallback));
  };
  QString source;
  if (!scene.project_relative_path.empty()) {
    source = localized(scene.live
                           ? "ui.material_browser.delete.scene.open"
                           : "ui.material_browser.delete.scene.saved",
                       scene.live ? "open: %1" : "saved: %1")
                 .arg(QString::fromStdString(scene.project_relative_path));
  } else if (scene.session_id.has_value()) {
    source = localized("ui.material_browser.delete.scene.open_session",
                       "open session: %1")
                 .arg(static_cast<qulonglong>(scene.session_id->packed()));
  } else {
    source = localized("ui.material_browser.delete.scene.identity",
                       "scene: %1")
                 .arg(QString::fromStdString(std::string(scene.scene_id.str())));
  }
  return QStringLiteral("%1 (%2)")
      .arg(QString::fromStdString(scene.name), source);
}

} // namespace

UISystem::UISystem() = default;

UISystem::~UISystem() { shutdown(); }

void UISystem::abandon_scene_backed_panels() noexcept {
  face_texture_browser_selection_presenter_.invalidate();
  face_texture_browser_selection_pending_ = false;
  if (face_texture_panel_ != nullptr) {
    face_texture_panel_->abandon();
  }
  if (outliner_panel_ != nullptr) {
    outliner_panel_->abandon_runtime();
  }
  if (inspector_panel_ != nullptr) {
    inspector_panel_->abandon_runtime();
  }
  if (layer_panel_ != nullptr) {
    layer_panel_->abandon_runtime();
  }
}

void UISystem::bind_scene_backed_panels() {
  if (editor_ == nullptr || document_ == nullptr || assets_ == nullptr ||
      modeling_ == nullptr || selection_ == nullptr) {
    return;
  }
  UIInspectorPreviewContext preview{.document = *document_, .assets = *assets_};
  UIContext context{
      .intent = {.editor = *editor_},
      .scene =
          {
              .document = *document_,
              .assets = *assets_,
              .modeling = *modeling_,
              .selection = *selection_,
              .default_material = default_material_,
          },
      .inspector_preview = &preview,
      .preferences =
          {
              .ui = settings_,
              .viewport = viewport_settings_,
              .ui_settings_changed = &ui_settings_changed_,
          },
      .localization = localization_,
  };
  if (outliner_panel_ != nullptr) {
    outliner_panel_->refresh(context);
  }
  if (layer_panel_ != nullptr) {
    layer_panel_->refresh(context);
  }
  if (inspector_panel_ != nullptr) {
    refresh_inspector_material_presentation();
    inspector_panel_->refresh(context);
  }
}

void UISystem::clear_editor_runtime_hooks() {
  if (auto *application =
          qobject_cast<QApplication *>(QCoreApplication::instance());
      application != nullptr && main_window_ != nullptr) {
    qt_set_numeric_snap_increment_provider(*application, *main_window_, {});
  }
  if (editor_ == nullptr) {
    return;
  }
  editor_->viewports().set_runtime_hooks(QuaderViewportRuntimeHooks{});
#ifndef NDEBUG
  editor_->dev_tools().set_runtime_hooks(QuaderDevToolsRuntimeHooks{});
#endif
  editor_->ui().set_runtime_hooks(QuaderUIRuntimeHooks{});
  editor_->preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{});
}

bool UISystem::initialize(RenderHost &render_system,
                          const std::filesystem::path &executable_dir,
                          bool composed_viewports) {
  if (initialized_) {
    return true;
  }

  render_system_ = &render_system;
  render_resources_detached_ = false;
  settings_paths_ = UISettings::resolve_paths(executable_dir);
  std::error_code error;
  std::filesystem::create_directories(settings_paths_.directory, error);
  settings_ = UISettings::load(settings_paths_.settings_file);
  if (!settings_.default_material_texture().empty() &&
      !DefaultMaterialTextureStore::validate_image(
          settings_.default_material_texture()).ok()) {
    settings_.set_default_material_texture({});
  }
  render_system_->set_vsync_enabled(settings_.vsync_enabled());
  auto *application =
      qobject_cast<QApplication *>(QCoreApplication::instance());
  if (application == nullptr) {
    LOG_ERROR("Qt application is unavailable while initializing UI scale");
    return false;
  }
  // Viewport chrome requests native windows explicitly. Keep ordinary docks
  // and toolbars in Qt's backing store instead of promoting every sibling HWND.
  QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
  ui_scale_controller_ = std::make_unique<QtUiScaleApplicationController>(
      *application, settings_.interface_scale());
  localization_.load(executable_dir / "locales", settings_.locale());
  if (settings_.locale() != localization_.current_locale()) {
    settings_.set_locale(localization_.current_locale());
  }
  localized_locale_ = localization_.current_locale();
  localized_revision_ = localization_.revision();
  viewport_settings_ =
      ViewportSettings::load(settings_paths_.viewport_settings_file);

  auto main_window = std::make_unique<QtMainWindow>();
  main_window->set_close_request_handler(
      [this]() { return allow_main_window_close(); });
  main_window_ = std::move(main_window);
  main_window_->setObjectName(QStringLiteral("quaderMainWindow"));
  main_window_->setWindowTitle(QStringLiteral("Quader"));
  main_window_->setWindowIcon(QApplication::windowIcon());
  main_window_->setMinimumSize(kPlatformWindowMinimumWidth,
                               kPlatformWindowMinimumHeight);
  main_window_->setDockOptions(QMainWindow::AllowNestedDocks |
                               QMainWindow::AllowTabbedDocks |
                               QMainWindow::AnimatedDocks);
  main_window_->setMenuBar(new QtMainMenuBar(main_window_.get()));

  unsaved_confirmation_ =
      new QtUnsavedConfirmationDialog(main_window_.get());
  lifecycle_message_box_ = new QtRetainedMessageBox(main_window_.get());

  selection_toolbar_ = new QtSelectionToolbar(main_window_.get());
  selection_toolbar_->set_localization(localization_);
  selection_toolbar_->setEnabled(false);
  main_window_->addToolBar(Qt::TopToolBarArea, selection_toolbar_);

  snapping_toolbar_ = new QtSnappingToolbar(main_window_.get());
  snapping_toolbar_->set_localization(localization_);
  snapping_toolbar_->setEnabled(false);
  main_window_->addToolBar(Qt::TopToolBarArea, snapping_toolbar_);

  viewport_display_toolbar_ =
      new QtViewportDisplayToolbar(main_window_.get());
  viewport_display_toolbar_->setObjectName(
      QStringLiteral("viewportDisplaySources"));
  viewport_display_toolbar_->set_localization(localization_);
  viewport_display_toolbar_->setEnabled(false);
  // Retain display action sources for pane strips without a workspace toolbar.
  viewport_display_toolbar_->toggleViewAction()->setVisible(false);
  viewport_display_toolbar_->hide();

  if (release_has_feature(ReleaseFeature::UvEditing)) {
    texture_toolbar_ = new QtTextureToolbar(main_window_.get());
    texture_toolbar_->set_localization(localization_);
    texture_toolbar_->setEnabled(false);
    main_window_->addToolBar(Qt::TopToolBarArea, texture_toolbar_);
  }

  tool_toolbar_ = new QtToolToolbar(main_window_.get());
  tool_toolbar_->set_localization(localization_);
  tool_toolbar_->setEnabled(false);
  main_window_->addToolBar(Qt::LeftToolBarArea, tool_toolbar_);

  central_stack_ = new QStackedWidget(main_window_.get());
  central_stack_->setObjectName(QStringLiteral("mainCentralStack"));
  if (composed_viewports) {
    // Keep the real window's device alive through Home/Workspace switches.
    // This child is not a stack page and never takes focus or pointer input.
    composition_anchor_ = new QtVulkanCompositionWidget(central_stack_);
    composition_anchor_->setObjectName(QStringLiteral("viewportCompositionAnchor"));
    composition_anchor_->setFocusPolicy(Qt::NoFocus);
    composition_anchor_->setAttribute(Qt::WA_TransparentForMouseEvents);
    composition_anchor_->setFixedSize(1, 1);
    composition_anchor_->setFixedColorBufferSize(QSize(1, 1));
    composition_anchor_->set_render_system(render_system_);
    composition_anchor_->set_release_callback([this]() {
      if (render_system_ != nullptr && render_system_->initialized()) {
        // The shell owns the borrowed device. Ordinary shutdown detaches this
        // callback before destroying it; an unexpected device replacement
        // cannot safely resume the existing Filament resource graph.
        qFatal("Qt replaced the active Quader Vulkan composition device");
      }
    });
    composition_anchor_->set_vsync_enabled(settings_.vsync_enabled());
    QObject::connect(composition_anchor_, &QRhiWidget::frameSubmitted,
                     composition_anchor_, [this]() {
      if (loading_presentation_active_ && loading_cover_ != nullptr) {
        static_cast<QtLoadingCover *>(loading_cover_)->note_composited_submission();
      }
      if (render_system_ != nullptr && render_system_->initialized()) {
        render_system_->complete_composited_presentation_frame();
        // Follow Qt's actual composition cadence instead of letting the
        // rounded millisecond watchdog drift against synchronized presentation.
        // App coalesces this wakeup and queues the next frame after this flush.
        if (viewport_presentation_requested_ &&
            render_system_->presentation_is_synchronized() &&
            needs_continuous_rendering()) {
          viewport_presentation_requested_();
        }
      }
    });
    composition_anchor_->show();
    composition_anchor_->lower();
  }
  project_home_page_ = new QtProjectHomePage(central_stack_);
  scene_workspace_ = new QtSceneWorkspace(central_stack_);
  auto *viewport_container = new QWidget(scene_workspace_);
  viewport_container->setObjectName(QStringLiteral("sceneViewportContainer"));
  auto *viewport_layout = new QVBoxLayout(viewport_container);
  viewport_layout->setContentsMargins(0, 0, 0, 0);
  viewport_layout->setSpacing(0);
  std::vector<QAction *> viewport_actions;
  for (const auto tool : {ViewportToolId::Select, ViewportToolId::Move,
                          ViewportToolId::Rotate, ViewportToolId::Scale}) {
    if (auto *action = tool_toolbar_->tool_action(tool); action != nullptr) {
      viewport_actions.push_back(action);
    }
  }
  const auto snapping_actions = snapping_toolbar_->viewport_actions();
  viewport_actions.push_back(snapping_actions.front());
  auto *backface_selection = new QAction(
      qt_multiresolution_icon(QStringLiteral(":/quader/icons/selection_backface.png")),
      QString::fromStdString(localization_.text(
          "ui.menu.select.toggle_backface_selection", "Toggle Backface Selection")),
      viewport_container);
  backface_selection->setObjectName(QStringLiteral("backfaceSelection"));
  backface_selection->setCheckable(true);
  QObject::connect(backface_selection, &QAction::triggered, viewport_container,
                   [this]() {
                     if (editor_ != nullptr) {
                       (void)editor_->commands().execute(
                           quader_command::kSelectToggleBackfaceSelection);
                     }
                   });
  viewport_actions.push_back(backface_selection);
  viewport_actions.push_back(nullptr);
  for (std::size_t index = 1; index < snapping_actions.size(); ++index) {
    viewport_actions.push_back(snapping_actions[index]);
  }
  viewport_actions.push_back(nullptr);
  for (auto *action : viewport_display_toolbar_->viewport_actions()) {
    if (action != nullptr) {
      viewport_actions.push_back(action);
    }
  }
  viewport_panel_ = new QtViewportPanel(viewport_container, composed_viewports);
  viewport_panel_->set_presentation_requested_callback([this]() {
    if (viewport_presentation_requested_) {
      viewport_presentation_requested_();
    }
  });
  viewport_panel_->set_control_actions(
      viewport_actions, tool_toolbar_->tool_action(ViewportToolId::LassoSelect),
      tool_toolbar_->tool_action(ViewportToolId::PaintSelect));
  viewport_panel_->set_control_action_requested_callback(
      [this](ViewportPaneId pane, QAction &action) {
        if (editor_ == nullptr) {
          return;
        }
        const auto focused = editor_->viewports().focus_pane(pane);
        if (!focused.ok()) {
          present_lifecycle_result(focused);
          return;
        }
        // The original actions dispatch through the existing command owners.
        // Synchronize their toggle direction to the clicked pane first.
        synchronize_tool_toolbar(editor_->selection().summary(), true);
        snapping_toolbar_->synchronize(*editor_, true);
        viewport_display_toolbar_->synchronize(*editor_, true);
        const auto generation = runtime_binding_generation_;
        action.trigger();
        if (editor_ != nullptr && generation == runtime_binding_generation_) {
          viewport_panel_->refresh_control_actions(*editor_);
        }
      });
  viewport_panel_->set_viewport_settings(viewport_settings_);
  viewport_panel_->set_localization(localization_);
  viewport_layout->addWidget(viewport_panel_, 1);
  scene_workspace_->set_viewport_widget(viewport_container);
  central_stack_->addWidget(project_home_page_);
  central_stack_->addWidget(scene_workspace_);
  central_stack_->setCurrentWidget(project_home_page_);
  main_window_->setCentralWidget(central_stack_);

  project_home_page_->set_new_project_requested_callback([this]() {
    if (editor_ != nullptr) {
      const QuaderApiVoidResult result = editor_->project().create();
      present_lifecycle_result(result);
      refresh_project_workspace();
    }
  });
  project_home_page_->set_open_project_requested_callback([this]() {
    if (editor_ != nullptr) {
      const QuaderApiVoidResult result = editor_->project().open({});
      present_lifecycle_result(result);
      refresh_project_workspace();
    }
  });
  project_home_page_->set_recent_project_action_requested_callback(
      [this](QtRecentProjectAction action, QString manifest_path) {
        const std::filesystem::path path(manifest_path.toStdWString());
        switch (action) {
        case QtRecentProjectAction::Open:
          if (editor_ != nullptr) {
            const QuaderApiVoidResult result = editor_->project().open(path);
            present_lifecycle_result(result);
            refresh_project_workspace();
          }
          break;
        case QtRecentProjectAction::OpenFileLocation:
          (void)platform_reveal_file(path);
          break;
        case QtRecentProjectAction::CopyPath:
          if (QClipboard *clipboard = QApplication::clipboard()) {
            clipboard->setText(manifest_path);
          }
          break;
        case QtRecentProjectAction::RemoveFromRecent:
          if (editor_ != nullptr) {
            const QuaderApiVoidResult result =
                editor_->project().remove_recent_project(path);
            present_lifecycle_result(result);
            refresh_project_workspace();
          }
          break;
        }
      });
  scene_workspace_->set_new_scene_requested_callback([this]() {
    if (editor_ != nullptr) {
      const QuaderApiResult<SceneSessionId> result =
          editor_->scene_documents().new_scene();
      present_lifecycle_result(result);
      refresh_project_workspace();
    }
  });
  scene_workspace_->set_open_scene_requested_callback([this]() {
    if (editor_ != nullptr) {
      const QuaderApiResult<SceneSessionId> result =
          editor_->scene_documents().open();
      present_lifecycle_result(result);
      refresh_project_workspace();
    }
  });
  scene_workspace_->set_activate_scene_requested_callback(
      [this](QtSceneSessionId id) {
        if (editor_ != nullptr) {
          const QuaderApiVoidResult result =
              editor_->scene_documents().activate(
                  SceneSessionId::from_packed(id));
          present_lifecycle_result(result);
          refresh_project_workspace();
        }
      });
  scene_workspace_->set_close_scene_requested_callback(
      [this](QtSceneSessionId id) {
        if (editor_ != nullptr) {
          const QuaderApiVoidResult result = editor_->scene_documents().close(
              SceneSessionId::from_packed(id));
          present_lifecycle_result(result);
          refresh_project_workspace();
        }
      });
  scene_workspace_->set_exit_instance_edit_requested_callback([this]() {
    if (editor_ == nullptr) {
      return;
    }
    const QuaderApiVoidResult result =
        editor_->commands().execute(quader_command::kEditExitInstanceEdit);
    present_lifecycle_result(result);
    refresh_project_workspace();
  });
  create_docks();
  const QtUiScaleMetrics &initial_ui_metrics =
      ui_scale_controller_->metrics();

  settings_dialog_ = new QtSettingsDialog(main_window_.get());
  settings_dialog_->resize(initial_ui_metrics.size(QSize(720, 560)));
  QObject::connect(settings_dialog_, &QDialog::finished, settings_dialog_,
                   [this](int) {
                     if (auxiliary_window_state_.settings_dialog_visible) {
                       auxiliary_window_state_.settings_dialog_visible = false;
                       save_auxiliary_window_state();
                     }
                   });
  auxiliary_window_save_timer_ = new QTimer(main_window_.get());
  auxiliary_window_save_timer_->setSingleShot(true);
  auxiliary_window_save_timer_->setInterval(200);
  QObject::connect(auxiliary_window_save_timer_, &QTimer::timeout,
                   main_window_.get(),
                   [this]() { save_auxiliary_window_state(); });
  material_browser_import_message_box_ =
      new QtRetainedMessageBox(main_window_.get());
  if (release_has_feature(ReleaseFeature::Materials)) {
    if (face_texture_panel_ != nullptr) {
      configure_material_browser(face_texture_panel_->browser());
    }
    viewport_panel_->set_primitive_creation_material_provider([this]() {
      if (editor_ == nullptr || face_texture_panel_ == nullptr ||
          material_browser_project_generation_ !=
              material_browser_project_generation_signal_->load(
                  std::memory_order_acquire) ||
          !material_presentation_catalog_attempt_.has_value()) {
        return QuaderMaterialReference{};
      }
      const auto revision = editor_->project_assets().material_presentation_revision();
      if (!revision.ok() ||
          revision.value().project_activation_generation !=
              material_presentation_catalog_attempt_->owner_revision
                  .project_activation_generation) {
        return QuaderMaterialReference{};
      }
      return face_texture_panel_->browser().current_selected_material();
    });
    material_texture_info_window_ =
        new QtTextureInfoWindow(main_window_.get(), initial_ui_metrics);
    material_texture_info_window_->hide();

    material_find_replace_dialog_ =
        new QtMaterialFindReplaceDialog(main_window_.get());
    material_find_replace_dialog_->resize(
        initial_ui_metrics.size(QSize(380, 300)));
    material_find_replace_dialog_->set_browse_material_callback(
        [this](QtMaterialFindReplaceColumn column) {
          show_material_picker(column ==
                               QtMaterialFindReplaceColumn::Replace);
        });
    material_find_replace_dialog_->set_replace_materials_callback(
        [this](QuaderMaterialReference source,
               QuaderMaterialReference replacement) {
          validate_material_find_replace_context();
          if (editor_ == nullptr || material_find_replace_dialog_ == nullptr ||
              !material_find_replace_dialog_->isVisible()) {
            return false;
          }
          refresh_material_presentation_cache();
          const std::optional<QtMaterialPresentationItem> current_source =
              material_presentation_for(source, true);
          const std::optional<QtMaterialPresentationItem> current_replacement =
              material_presentation_for(replacement, true);
          material_find_replace_dialog_->set_find_material(current_source);
          material_find_replace_dialog_->set_replacement_material(
              current_replacement);
          if (!current_source.has_value() ||
              !current_replacement.has_value() ||
              !current_replacement->replacement_valid() ||
              source == replacement) {
            return false;
          }
          const QuaderApiVoidResult result =
              editor_->materials().replace_material_references(
                  source, replacement, material_find_replace_scope_);
          present_lifecycle_result(result);
          if (!result.succeeded()) {
            return false;
          }
          invalidate_material_presentation_cache();
          inspector_presenter_.invalidate();
          project_workspace_refresh_signal_->store(
              true, std::memory_order_relaxed);
          return true;
        });
    QObject::connect(material_find_replace_dialog_, &QDialog::finished,
                     material_find_replace_dialog_, [this](int) {
                       if (material_picker_window_ != nullptr) {
                         material_picker_window_->hide();
                         material_picker_window_->clear_materials();
                       }
                       if (material_find_replace_dialog_ != nullptr) {
                         material_find_replace_dialog_->clear_materials();
                       }
                       material_find_replace_scene_id_ = 0;
                       material_find_replace_scene_selection_revision_ = 0;
                       material_find_replace_modeling_selection_revision_ = 0;
                       material_find_replace_modeling_object_map_revision_ = 0;
                       material_find_replace_presentation_revision_ = 0;
                       material_picker_presentation_revision_ = 0;
                     });
    material_find_replace_dialog_->hide();
    material_picker_window_ =
        new QtMaterialBrowserWindow(material_find_replace_dialog_);
    material_picker_window_->resize(
        initial_ui_metrics.size(QSize(760, 520)));
    material_picker_window_->set_purpose(QtMaterialBrowserPurpose::Picker);
    material_picker_window_->setWindowModality(Qt::WindowModal);
    material_picker_window_->set_accept_material_callback(
        [this](QuaderMaterialReference reference) {
          validate_material_find_replace_context();
          if (material_find_replace_dialog_ == nullptr ||
              !material_find_replace_dialog_->isVisible()) {
            return;
          }
          refresh_material_presentation_cache();
          const std::optional<QtMaterialPresentationItem> presentation =
              material_presentation_for(reference, true);
          if (!presentation.has_value() ||
              (material_picker_replacement_column_ &&
               !presentation->replacement_valid())) {
            return;
          }
          material_find_replace_dialog_->set_material(
              material_picker_replacement_column_
                  ? QtMaterialFindReplaceColumn::Replace
                  : QtMaterialFindReplaceColumn::Find,
              presentation);
        });
    material_picker_window_->hide();
    standard_material_editor_window_ =
        new QtStandardMaterialEditorWindow(main_window_.get());
    standard_material_editor_window_->resize(
        initial_ui_metrics.size(QSize(1040, 680)));
    standard_material_unsaved_confirmation_ =
        new QtUnsavedConfirmationDialog(standard_material_editor_window_);
    standard_material_error_message_box_ =
        new QtRetainedMessageBox(standard_material_editor_window_);
    material_preview_surface_ = new QtMaterialPreviewSurface();
    QWidget *preview_container = QWidget::createWindowContainer(
        material_preview_surface_, standard_material_editor_window_);
    preview_container->setObjectName(
        QStringLiteral("standardMaterialEditor.previewSurfaceContainer"));
    preview_container->setFocusPolicy(Qt::StrongFocus);
    standard_material_editor_window_->install_preview_widget(preview_container);
    if (!composed_viewports &&
        !material_preview_surface_->initialize(render_system)) {
      LOG_ERROR("Unable to initialize the Standard Material preview surface");
      standard_material_editor_window_->install_preview_widget(nullptr);
      material_preview_surface_ = nullptr;
    }
    standard_material_editor_window_->set_file_action_callback(
        [this](QtStandardMaterialEditorFileAction action) {
          handle_standard_material_file_action(action);
        });
    standard_material_editor_window_->set_name_edited_callback(
        [this](QString name) {
          if (editor_ == nullptr) {
            return;
          }
          const QuaderApiVoidResult result =
              editor_->standard_material_documents().set_name(
                  name.toStdString());
          present_lifecycle_result(result);
          refresh_standard_material_editor();
        });
    standard_material_editor_window_->set_albedo_preview_begin_callback([this]() {
      if (editor_ == nullptr) {
        return false;
      }
      const auto revision =
          editor_->standard_material_documents().current_revision();
      if (!revision.ok()) {
        return false;
      }
      const auto generation = runtime_binding_generation_;
      const auto session = revision.value().session_id;
      const auto result =
          editor_->standard_material_documents().begin_base_color_preview(session);
      if (!result.ok()) {
        present_lifecycle_result(result);
        return false;
      }
      if (generation != runtime_binding_generation_) {
        return false;
      }
      material_color_preview_session_ = session;
      material_color_preview_binding_generation_ = generation;
      return true;
    });
    standard_material_editor_window_->set_albedo_edited_callback(
        [this](QColor color) {
          if (editor_ == nullptr || !material_color_preview_session_.has_value() ||
              material_color_preview_binding_generation_ !=
                  runtime_binding_generation_) {
            return;
          }
          const auto generation = runtime_binding_generation_;
          const QuaderApiVoidResult result =
              editor_->standard_material_documents().update_base_color_preview(
                  *material_color_preview_session_,
                  {static_cast<float>(color.redF()),
                   static_cast<float>(color.greenF()),
                   static_cast<float>(color.blueF())});
          if (generation != runtime_binding_generation_) {
            return;
          }
          if (!result.ok()) {
            material_color_preview_session_.reset();
            standard_material_editor_window_->abandon_interactions();
            present_lifecycle_result(result);
          }
          refresh_standard_material_editor();
        });
    standard_material_editor_window_->set_albedo_preview_finish_callback(
        [this](bool accepted) {
          if (editor_ == nullptr || !material_color_preview_session_.has_value() ||
              material_color_preview_binding_generation_ !=
                  runtime_binding_generation_) {
            return;
          }
          const auto session = *material_color_preview_session_;
          const auto generation = runtime_binding_generation_;
          material_color_preview_session_.reset();
          const auto result =
              editor_->standard_material_documents().finish_base_color_preview(
                  session, accepted);
          if (generation != runtime_binding_generation_) {
            return;
          }
          if (!result.ok()) {
            present_lifecycle_result(result);
          }
          refresh_standard_material_editor();
        });
    standard_material_editor_window_->set_scalar_edited_callback(
        [this](QtStandardMaterialScalar scalar, double value) {
          handle_standard_material_scalar(scalar, value);
        });
    standard_material_editor_window_->set_normal_convention_edited_callback(
        [this](bool direct_x) {
          if (editor_ == nullptr) {
            return;
          }
          const QuaderApiVoidResult result =
              editor_->standard_material_documents().set_normal_convention(
                  direct_x ? QuaderNormalMapConvention::DirectX
                           : QuaderNormalMapConvention::OpenGL);
          present_lifecycle_result(result);
          refresh_standard_material_editor();
        });
    standard_material_editor_window_->set_diffuse_model_edited_callback(
        [this](QtStandardMaterialDiffuseModel model) {
          if (editor_ == nullptr) {
            return;
          }
          const std::uint64_t generation = runtime_binding_generation_;
          const QuaderApiVoidResult result =
              editor_->standard_material_documents().set_diffuse_model(
                  model == QtStandardMaterialDiffuseModel::Lambert
                      ? QuaderMaterialDiffuseModel::Lambert
                      : QuaderMaterialDiffuseModel::Burley);
          if (generation != runtime_binding_generation_) {
            return;
          }
          present_lifecycle_result(result);
          if (generation != runtime_binding_generation_) {
            return;
          }
          refresh_standard_material_editor();
        });
    standard_material_editor_window_->set_texture_action_callback(
        [this](QtStandardMaterialTextureSlot slot,
               QtStandardMaterialTextureAction action) {
          handle_standard_material_texture(slot, action);
        });
    standard_material_editor_window_->set_texture_asset_dropped_callback(
        [this](QtStandardMaterialTextureSlot slot, ProjectAssetId id,
               std::uint64_t generation) {
          if (editor_ == nullptr || !editor_->project().has_project()) return;
          const auto revision = editor_->project_assets().asset_revision();
          if (!revision.ok() || revision.value().project_generation != generation) return;
          const auto result = editor_->standard_material_documents().set_texture(
              domain_texture_slot(slot), id);
          present_lifecycle_result(result);
          refresh_standard_material_editor();
        });
    standard_material_editor_window_->set_texture_transform_edited_callback(
        [this](QtStandardMaterialTextureTransformComponent component,
               double value) {
          handle_standard_material_texture_transform(component, value);
        });
    standard_material_editor_window_
        ->set_presentation_state_changed_callback(
            [this]() {
              if (material_preview_surface_ != nullptr) {
                (void)material_preview_surface_->set_preview_mesh(
                    preview_mesh_kind(
                        standard_material_editor_window_->preview_mesh()));
                (void)material_preview_surface_->set_preview_shading(
                    preview_shading_mode(
                        standard_material_editor_window_->preview_shading()));
                (void)material_preview_surface_->set_preview_environment(
                    standard_material_editor_window_->preview_environment()
                        .toStdString());
                (void)material_preview_surface_
                    ->set_show_environment_background(
                        standard_material_editor_window_
                            ->preview_environment_background_visible());
              }
              schedule_auxiliary_window_state_save();
            });
    standard_material_editor_window_->set_preview_reset_callback([this]() {
      if (material_preview_surface_ != nullptr) {
        (void)material_preview_surface_->reset_preview();
      }
    });
    standard_material_editor_window_->set_close_request_handler(
        [this]() { return resolve_standard_material_document(true); });
    standard_material_editor_window_->hide();
  }
  main_window_->setStatusBar(new QtMainStatusBar(main_window_.get()));
  status_message_ = new QtStatusMessage(main_window_.get());
  status_message_->setObjectName(QStringLiteral("statusMessage"));
  status_message_->setWordWrap(false);
  status_message_->hide();
  operation_input_hints_ = new QtOperationInputHints(main_window_->statusBar());
  if (auto *application = qobject_cast<QApplication *>(QCoreApplication::instance())) {
    qt_set_contextual_input_hints_refresh_callback(
        *application, *operation_input_hints_, [this]() { refresh_status_bar(); });
  }
  status_selection_ = new QLabel(main_window_->statusBar());
  status_selection_->setObjectName(QStringLiteral("statusSelection"));
  auto *selection_separator = new QFrame(main_window_->statusBar());
  selection_separator->setObjectName(QStringLiteral("statusSelectionSeparator"));
  selection_separator->setFrameShape(QFrame::VLine);
  selection_separator->setFrameShadow(QFrame::Sunken);
  status_version_ = new QLabel(
      QStringLiteral("v%1").arg(QString::fromUtf8(kQuaderAppVersion)),
      main_window_.get());
  status_version_->setObjectName(QStringLiteral("statusVersion"));
  main_window_->statusBar()->setObjectName(QStringLiteral("statusBar"));
  main_window_->statusBar()->addPermanentWidget(operation_input_hints_, 1);
  main_window_->statusBar()->addPermanentWidget(status_message_, 1);
  main_window_->statusBar()->addPermanentWidget(status_selection_);
  main_window_->statusBar()->addPermanentWidget(selection_separator);
  main_window_->statusBar()->addPermanentWidget(status_version_);

  apply_ui_scale_to_retained_surfaces(ui_scale_controller_->metrics());
  refresh_localized_chrome();
  restore_window_state();
  panels_.apply_settings(settings_);
  initialized_ = true;
  show_home();
  loading_cover_ = new QtLoadingCover(
      *main_window_, composed_viewports, ui_scale_controller_->metrics().px(96));
  return true;
}

void UISystem::install_runtime_hooks(
    QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
    ModelingSessionService &modeling, EditorSelectionService &selection,
    NativeViewportToolCoordinator &native_tools,
    ViewportPointPlacementService &point_placement, AssetId default_material) {
  ++runtime_binding_generation_;
  if (loading_presentation_active_) {
    startup_presentation_ = {};
  }
  material_color_preview_session_.reset();
  if (standard_material_editor_window_ != nullptr) {
    standard_material_editor_window_->abandon_interactions();
  }
  abandon_scene_backed_panels();
  if (viewport_panel_ != nullptr) {
    // Project rebinding can release the previous SceneSession before this
    // callback. Drop only Qt-owned state; the stored runtime pair may dangle.
    viewport_panel_->abandon_interactions();
  }
  if (main_window_close_in_progress_ && !editor.project().has_project() &&
      main_window_ != nullptr) {
    // Close preflight succeeded. Retire the top-level presentation before
    // fallback rebinding hides/resizes native viewport children: otherwise
    // their Qt backing store can expose old dimension glyphs for one frame.
    capture_workspace_window_state();
    main_window_->hide();
  }
  editor_ = &editor;
  if (auto *application =
          qobject_cast<QApplication *>(QCoreApplication::instance());
      application != nullptr && main_window_ != nullptr) {
    qt_set_numeric_snap_increment_provider(
        *application, *main_window_, [this]() {
          const int preset = editor_ != nullptr
                                 ? editor_->viewports().grid_preset()
                                 : viewport_default_grid_preset();
          // Most numeric distances use scene units; displayed-map-unit fields
          // adapt this increment at their presentation boundary.
          return viewport_grid_world_size_from_contract(preset);
        });
  }
  if (operation_input_hints_ != nullptr) {
    operation_input_hints_->invalidate();
  }
  document_ = &document;
  assets_ = &assets;
  modeling_ = &modeling;
  selection_ = &selection;
  native_tools_ = &native_tools;
  point_placement_ = &point_placement;
  default_material_ = default_material;
  if (command_palette_ == nullptr && main_window_ != nullptr) {
    command_palette_ =
        new QtCommandPalette(editor.commands(), localization_,
                             main_window_.get());
    command_palette_->resize(
        ui_scale_controller_->metrics().size(QSize(760, 460)));
    QObject::connect(command_palette_, &QDialog::finished, command_palette_,
                     [this](int) {
                       command_palette_open_pending_ = false;
                     });
    command_palette_->hide();
  } else if (command_palette_ != nullptr) {
    command_palette_->rebind_commands(editor.commands());
  }
  invalidate_material_presentation_cache();
  outliner_presenter_.invalidate();
  layers_presenter_.invalidate();
  inspector_presenter_.invalidate();
  inspector_material_query_presenter_.invalidate();
  toolbar_owner_presenter_.invalidate();
  toolbar_tool_presenter_.invalidate();
  toolbar_transform_presenter_.invalidate();
  tool_settings_presenter_.invalidate();
  face_texture_panel_presenter_.invalidate();
  standard_material_editor_presenter_.invalidate();
  status_presenter_.invalidate();
  status_selection_presenter_.invalidate();
  status_observed_command_revision_ = editor.commands().last_status_revision();
  status_notification_.reset();
  ++status_notification_generation_;
  if (status_message_ != nullptr) {
    status_message_->present({}, false);
    status_message_->hide();
  }
  project_workspace_instance_edit_revision_ = 0;
  project_workspace_instance_edit_revision_valid_ = false;
  bind_scene_backed_panels();
  bind_project_workspace_events(editor);

  selection_toolbar_->set_mode_requested_callback(
      [this](QuaderSelectionMode mode) {
        if (editor_ == nullptr || modeling_ == nullptr) {
          return;
        }
        const auto generation = runtime_binding_generation_;
        present_lifecycle_result(editor_->selection().set_mode(mode));
        if (editor_ == nullptr || modeling_ == nullptr ||
            generation != runtime_binding_generation_) {
          return;
        }
        const QuaderSelectionSummary selection =
            editor_->selection().summary();
        const QuaderSelectionMode active_mode =
            to_api_selection_mode(modeling_->selection_mode());
        selection_toolbar_->synchronize(*editor_, active_mode, true);
        synchronize_tool_toolbar(selection, true);
      });
  selection_toolbar_->set_group_selection_toggle_requested_callback([this]() {
    if (editor_ == nullptr || modeling_ == nullptr) {
      return;
    }
    const auto generation = runtime_binding_generation_;
    present_lifecycle_result(editor_->commands().execute(
        quader_command::kSelectToggleGroupSelection));
    if (editor_ == nullptr || modeling_ == nullptr ||
        generation != runtime_binding_generation_) {
      return;
    }
    const QuaderSelectionSummary selection = editor_->selection().summary();
    const QuaderSelectionMode active_mode =
        to_api_selection_mode(modeling_->selection_mode());
    selection_toolbar_->synchronize(*editor_, active_mode, true);
    synchronize_tool_toolbar(selection, true);
  });
  tool_toolbar_->set_tool_requested_callback(
      [this](std::string_view tool_id) {
        if (editor_ == nullptr) {
          return;
        }
        const std::string command_id =
            editor_->commands().command_id_for_tool_activation(tool_id);
        const auto generation = runtime_binding_generation_;
        present_lifecycle_result(editor_->commands().execute(command_id));
        if (editor_ != nullptr && generation == runtime_binding_generation_) {
          synchronize_tool_toolbar(editor_->selection().summary(), true);
        }
      });
  tool_toolbar_->set_command_requested_callback(
      [this](QuaderCommandId command_id) {
        if (editor_ == nullptr) {
          return;
        }
        const auto generation = runtime_binding_generation_;
        present_lifecycle_result(editor_->commands().execute(command_id));
        if (editor_ != nullptr && generation == runtime_binding_generation_) {
          synchronize_tool_toolbar(editor_->selection().summary(), true);
        }
      });
  snapping_toolbar_->set_transform_space_toggle_requested_callback([this]() {
    if (editor_ == nullptr) {
      return;
    }
    present_lifecycle_result(editor_->commands().execute(
        quader_command::kViewportToggleTransformSpace));
    snapping_toolbar_->synchronize(*editor_, true);
  });
  snapping_toolbar_->set_grid_preset_requested_callback(
      [this](int preset) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(editor_->viewports().set_grid_preset(preset));
        snapping_toolbar_->synchronize(*editor_, true);
      });
  snapping_toolbar_->set_rotation_snap_degrees_requested_callback(
      [this](double degrees) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(
            editor_->viewports().set_rotation_snap_degrees(
                editor_->viewports().focused_pane(), degrees));
      });
  viewport_display_toolbar_->set_command_requested_callback(
      [this](QuaderCommandId command_id) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(editor_->commands().execute(command_id));
        viewport_display_toolbar_->synchronize(*editor_, true);
      });
  if (texture_toolbar_ != nullptr) {
    texture_toolbar_->set_command_requested_callback(
        [this](QuaderCommandId command_id) {
          if (editor_ == nullptr) {
            return;
          }
          present_lifecycle_result(editor_->commands().execute(command_id));
          texture_toolbar_->synchronize(*editor_, true);
        });
  }
  snapping_toolbar_->set_snapping_enabled_requested_callback(
      [this](bool enabled) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(
            editor_->viewports().set_control(editor_->viewports().focused_pane(),
                QuaderViewportControl::SnappingEnabled, enabled));
      });
  snapping_toolbar_->set_grid_snap_enabled_requested_callback(
      [this](bool enabled) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(
            editor_->viewports().set_control(editor_->viewports().focused_pane(),
                QuaderViewportControl::GridSnap, enabled));
      });
  snapping_toolbar_->set_vertex_snap_enabled_requested_callback(
      [this](bool enabled) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(
            editor_->viewports().set_control(editor_->viewports().focused_pane(),
                QuaderViewportControl::VertexSnap, enabled));
      });
  snapping_toolbar_->set_rotation_snap_enabled_requested_callback(
      [this](bool enabled) {
        if (editor_ == nullptr) {
          return;
        }
        present_lifecycle_result(
            editor_->viewports().set_control(editor_->viewports().focused_pane(),
                QuaderViewportControl::RotationSnap, enabled));
      });

  ViewportWorkspaceService &workspace =
      QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
  apply_viewport_settings_to_workspace();
  viewport_panel_->attach_workspace(workspace);
  tool_settings_panel_->bind(editor);
  if (face_texture_panel_ != nullptr) {
    face_texture_panel_->bind(editor);
  }
  settings_dialog_->set_context(
      settings_, viewport_settings_, localization_, editor.commands(),
      [this](const QtSettingsChange &change) {
        handle_settings_dialog_change(change);
      },
      [this]() { return save_settings(); }, settings_paths_.directory);

  if (main_menu_ == nullptr) {
    main_menu_ = std::make_unique<QtMainMenu>();
    main_menu_->initialize(
        *main_window_->menuBar(),
        QtMainMenuContext{
          .editor = editor,
          .modeling = modeling,
          .viewport_settings = viewport_settings_,
          .localization = localization_,
          .request_quit = [this]() { main_window_->close(); },
          .show_settings = [this]() { show_settings_dialog(); },
          .show_about =
              [this]() {
                if (lifecycle_message_box_ == nullptr) {
                  return;
                }
                const QString version =
                    QString::fromUtf8(kQuaderAppVersionDisplay);
                (void)lifecycle_message_box_->present(
                    QtRetainedMessageBoxPresentation{
                        .icon = QMessageBox::Information,
                        .object_name =
                            QStringLiteral("about_quader_message_box"),
                        .title = QString::fromStdString(localization_.text(
                            "ui.about.title", "About Quader")),
                        .text =
                            QString::fromStdString(localization_.text(
                                "ui.about.version", "Quader %1"))
                                .arg(version),
                        .informative_text = QString::fromStdString(
                            localization_.text(
                                "ui.about.description",
                                "A native polygon modeling editor for "
                                "Windows.")),
                        .buttons =
                            {{
                                .button =
                                    QtRetainedMessageBoxButton::Primary,
                                .object_name = QStringLiteral(
                                    "aboutQuaderMessageBox.ok"),
                                .label = QString::fromStdString(
                                    localization_.text("ui.action.ok", "OK")),
                                .role = QMessageBox::AcceptRole,
                            }},
                        .default_button =
                            QtRetainedMessageBoxButton::Primary,
                        .escape_button =
                            QtRetainedMessageBoxButton::Primary,
                        .close_button =
                            QtRetainedMessageBoxButton::Primary,
                    });
              },
          .panel_visible = [this](UIPanelId id) { return panel_visible(id); },
          .set_panel_visible =
              [this](UIPanelId id, bool visible) {
                set_panel_visible(id, visible);
              },
          .selection_toolbar_visible =
              [this]() {
                return selection_toolbar_ != nullptr &&
                       selection_toolbar_->isVisible();
              },
          .set_selection_toolbar_visible =
              [this](bool visible) {
                if (selection_toolbar_ != nullptr) {
                  selection_toolbar_->setVisible(visible);
                }
              },
          .snapping_toolbar_visible =
              [this]() {
                return snapping_toolbar_ != nullptr &&
                       snapping_toolbar_->isVisible();
              },
          .set_snapping_toolbar_visible =
              [this](bool visible) {
                if (snapping_toolbar_ != nullptr) {
                  snapping_toolbar_->setVisible(visible);
                }
              },
          .texture_toolbar_visible =
              [this]() {
                return texture_toolbar_ != nullptr && texture_toolbar_->isVisible();
              },
          .set_texture_toolbar_visible =
              [this](bool visible) {
                if (texture_toolbar_ != nullptr) {
                  texture_toolbar_->setVisible(visible);
                }
              },
          .tool_toolbar_visible =
              [this]() {
                return tool_toolbar_ != nullptr && tool_toolbar_->isVisible();
              },
          .set_tool_toolbar_visible =
              [this](bool visible) {
                if (tool_toolbar_ != nullptr) {
                  tool_toolbar_->setVisible(visible);
                }
              },
          .viewport_settings_changed =
              [this]() {
                (void)save_viewport_settings();
                synchronize_settings_surfaces(true);
              },
          .show_material_find_replace =
              [this]() { show_material_find_replace(); },
          .material_find_replace_available =
              [this]() {
                return !home_visible_ && editor_ != nullptr &&
                       editor_->project().has_project() &&
                       editor_->scene_documents().has_active_scene();
              },
          .show_autosave_recovery = [this]() { show_autosave_recovery(); },
          .autosave_recovery_available = [this]() {
            return editor_ != nullptr && editor_->project().has_project();
          },
        });
  } else {
    // A close command can synchronously rebind the active SceneSession from a
    // QAction callback owned by this menu. Keep that callback owner alive and
    // update only the runtime references it resolves on subsequent access.
    main_menu_->rebind_runtime(editor, modeling);
  }

  editor.selection_diagnostics().set_runtime_hooks(
      QuaderSelectionDiagnosticsRuntimeHooks{
          .dump_directory = [directory = settings_paths_.directory]() {
            return directory / "selection-dumps";
          }});
  editor.viewports().set_runtime_hooks(QuaderViewportRuntimeHooks{
      .frame_all =
          [this](ViewportPaneId pane) {
            return viewport_panel_ != nullptr
                       ? viewport_panel_->frame_all(*document_, *assets_, pane)
                       : runtime_failure(localization_.text(
                             "ui.runtime.viewport_panel_unavailable",
                             "Viewport panel is not available."));
          },
      .frame_selection =
          [this](ViewportPaneId pane) {
            return viewport_panel_ != nullptr
                       ? viewport_panel_->frame_selection(*modeling_, pane)
                       : runtime_failure(localization_.text(
                             "ui.runtime.viewport_panel_unavailable",
                             "Viewport panel is not available."));
          },
  });
#ifndef NDEBUG
  editor.dev_tools().set_runtime_hooks(QuaderDevToolsRuntimeHooks{
      .set_clipboard_text =
          [this](std::string_view text, std::string &error_message) {
            QClipboard *clipboard = QApplication::clipboard();
            if (clipboard == nullptr) {
              error_message = localization_.text(
                  "ui.runtime.platform_unavailable",
                  "UI platform is not available.");
              return false;
            }
            clipboard->setText(QString::fromUtf8(
                text.data(), static_cast<qsizetype>(text.size())));
            error_message.clear();
            return true;
          },
  });
#endif
  editor.ui().set_runtime_hooks(QuaderUIRuntimeHooks{
      .is_panel_visible =
          [this](std::string_view panel) {
            const std::optional<UIPanelId> id = panel_id_from_name(panel);
            return id.has_value() && panel_visible(*id);
          },
      .set_panel_visible =
          [this](std::string_view panel, bool visible) {
            const std::optional<UIPanelId> id = panel_id_from_name(panel);
            if (!id.has_value()) {
              return runtime_failure(localization_.text(
                  "ui.runtime.unknown_panel", "Unknown UI panel."));
            }
            if (!ui_panel_available(*id)) {
              return runtime_failure(localization_.text(
                  "release.feature_unavailable", release_feature_unavailable_message()));
            }
            const bool before = panel_visible(*id);
            set_panel_visible(*id, visible);
            return runtime_status(before != panel_visible(*id));
          },
      .focus_panel =
          [this](std::string_view panel) {
            const std::optional<UIPanelId> id = panel_id_from_name(panel);
            if (!id.has_value()) {
              return runtime_failure(localization_.text(
                  "ui.runtime.unknown_panel", "Unknown UI panel."));
            }
            if (!ui_panel_available(*id)) {
              return runtime_failure(localization_.text(
                  "release.feature_unavailable", release_feature_unavailable_message()));
            }
            if (*id == UIPanelId::Viewport) {
              return runtime_status(viewport_panel_ != nullptr &&
                                    viewport_panel_->focus_active_pane());
            }
            if (*id == UIPanelId::AssetBrowser) {
              set_panel_visible(*id, true);
            }
            return runtime_status(panels_.focus(*id));
          },
      .show_command_search =
          [this]() {
            if (command_palette_ == nullptr) {
              return runtime_failure(localization_.text(
                  "ui.runtime.command_search_unavailable",
                  "Command Search is not available."));
            }
            command_palette_open_pending_ = true;
            command_palette_->open_palette();
            return runtime_status(false);
          },
      .toggle_asset_browser = [this]() {
        if (home_visible_ || editor_ == nullptr || !editor_->project().has_project()) {
          return runtime_failure(localization_.text(
              "ui.asset_browser.no_project", "Open a project to browse its assets."));
        }
        toggle_asset_browser();
        return runtime_status(true);
      },
      .notify =
          [this](QuaderNotification notification) {
            present_status_notification(std::move(notification));
            return runtime_status(false);
          },
      .settle_pending_property_edits = [this]() {
        if (inspector_panel_ != nullptr &&
            !inspector_panel_->settle_pending_edits()) {
          return runtime_failure(localization_.text(
              "ui.inspector.pending_edit_failed",
              "The pending property edit could not be accepted."));
        }
        return runtime_status(false);
      },
  });
  editor.preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{
      .locale = [this]() { return settings_.locale(); },
      .set_locale =
          [this](std::string locale) {
            if (!localization_.select_locale(locale)) {
              return runtime_failure(localization_.text(
                  "ui.runtime.unknown_locale", "Unknown locale."));
            }
            const bool changed =
                settings_.locale() != localization_.current_locale();
            settings_.set_locale(localization_.current_locale());
            if (changed) {
              (void)save_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .interface_scale =
          [this]() { return static_cast<double>(settings_.interface_scale()); },
      .set_interface_scale =
          [this](double factor) {
            if (!std::isfinite(factor)) {
              return runtime_failure(localization_.text(
                  "ui.runtime.interface_scale_invalid",
                  "Interface scale must be finite."));
            }
            const float canonical = static_cast<float>(std::clamp(
                factor,
                static_cast<double>(UISettings::kMinimumInterfaceScale),
                static_cast<double>(UISettings::kMaximumInterfaceScale)));
            const bool persisted_changed =
                settings_.interface_scale() != canonical;
            const bool presented_changed =
                ui_scale_controller_ != nullptr &&
                ui_scale_controller_->metrics().factor() != canonical;
            const bool changed = persisted_changed || presented_changed;
            const float previous_persisted = settings_.interface_scale();
            const float previous_presented =
                ui_scale_controller_ != nullptr
                    ? ui_scale_controller_->metrics().factor()
                    : previous_persisted;
            settings_.set_interface_scale(canonical);
            if (changed) {
              (void)apply_interface_scale(settings_.interface_scale());
              if (!save_settings()) {
                settings_.set_interface_scale(previous_persisted);
                (void)apply_interface_scale(previous_presented);
                return runtime_failure(localization_.text(
                    "ui.runtime.preferences_save_failed",
                    "Preferences could not be saved."));
              }
              if (settings_dialog_ != nullptr &&
                  settings_dialog_->isVisible()) {
                settings_dialog_->synchronize_interface_scale();
              }
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .vsync_enabled = [this]() { return settings_.vsync_enabled(); },
      .set_vsync_enabled =
          [this](bool enabled) {
            if (render_system_ == nullptr) {
              return runtime_failure(localization_.text(
                  "ui.runtime.renderer_unavailable",
                  "Renderer runtime is not attached."));
            }
            const bool previous_persisted = settings_.vsync_enabled();
            const bool previous_presented = render_system_->vsync_enabled();
            const bool changed = previous_persisted != enabled ||
                                 previous_presented != enabled;
            if (changed) {
              settings_.set_vsync_enabled(enabled);
              set_presentation_vsync(enabled);
              if (!save_settings()) {
                settings_.set_vsync_enabled(previous_persisted);
                set_presentation_vsync(previous_presented);
                return runtime_failure(localization_.text(
                    "ui.runtime.preferences_save_failed",
                    "Preferences could not be saved."));
              }
              if (settings_dialog_ != nullptr &&
                  settings_dialog_->isVisible()) {
                settings_dialog_->synchronize_vsync_enabled();
              }
            }
            return runtime_status(changed);
          },
      .session_settings = [this]() { return settings_.session_settings(); },
      .set_session_settings = [this](SessionPreferences value) {
        const SessionPreferences previous = settings_.session_settings();
        if (!value.valid()) {
          return runtime_failure(localization_.text(
              "ui.runtime.session_preferences_invalid",
              "Undo or autosave settings are outside the supported range."));
        }
        if (previous == value) {
          return runtime_status(false);
        }
        (void)settings_.set_session_settings(value);
        if (!save_settings()) {
          (void)settings_.set_session_settings(previous);
          return runtime_failure(localization_.text(
              "ui.runtime.preferences_save_failed",
              "Preferences could not be saved."));
        }
        if (session_settings_changed_) {
          session_settings_changed_(value);
        }
        if (settings_dialog_ != nullptr && settings_dialog_->isVisible()) {
          settings_dialog_->synchronize_session_settings();
        }
        return runtime_status(true);
      },
      .default_material_texture =
          [this]() { return settings_.default_material_texture(); },
      .set_default_material_texture = [this](std::filesystem::path source) {
        const auto imported = DefaultMaterialTextureStore::import_image(
            source, settings_paths_.directory);
        if (!imported.ok()) {
          return runtime_failure(localization_.text(
              "ui.settings.default_material.import_failed",
              "The default material image could not be imported. Choose a valid PNG or JPEG image."));
        }
        const std::filesystem::path previous = settings_.default_material_texture();
        if (previous == imported.value()) {
          return runtime_status(false);
        }
        settings_.set_default_material_texture(imported.value());
        if (!save_settings()) {
          settings_.set_default_material_texture(previous);
          return runtime_failure(localization_.text(
              "ui.runtime.preferences_save_failed",
              "Preferences could not be saved."));
        }
        if (default_material_texture_changed_) {
          default_material_texture_changed_(settings_.default_material_texture());
        }
        if (settings_dialog_ != nullptr && settings_dialog_->isVisible()) {
          settings_dialog_->synchronize_default_material_texture();
        }
        return runtime_status(true);
      },
      .save =
          [this]() {
            const bool ui_saved = save_settings();
            const bool viewport_saved = save_viewport_settings();
            save_window_state();
            if (!ui_saved || !viewport_saved) {
              return runtime_failure(localization_.text(
                  "ui.runtime.preferences_save_failed",
                  "Preferences could not be saved."));
            }
            return runtime_status(
                false, localization_.text("ui.runtime.preferences_saved",
                                          "Preferences saved."));
          },
      .snapping_enabled = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::SnappingEnabled);
      },
      .set_snapping_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::SnappingEnabled, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .backface_selection_enabled = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::BackfaceSelection);
      },
      .set_backface_selection_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::BackfaceSelection, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .backface_culling_enabled = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::BackfaceCulling);
      },
      .set_backface_culling_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::BackfaceCulling, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .grid_visible = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::ShowGrid);
      },
      .set_grid_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowGrid, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .gizmos_visible = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::ShowGizmos);
      },
      .set_gizmos_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowGizmos, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .mesh_grid_visible = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::ShowMeshGrid);
      },
      .set_mesh_grid_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowMeshGrid, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .overlays_visible = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::ShowOverlays);
      },
      .set_overlays_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowOverlays, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .dimensions_visible = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::ShowDimensions);
      },
      .set_dimensions_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowDimensions, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .collisions_visible = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::ShowCollisions);
      },
      .set_collisions_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowCollisions, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .lamina_faces_visible = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::ShowLaminaFaces);
      },
      .set_lamina_faces_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowLaminaFaces, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .texture_lock_enabled = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::TextureLock);
      },
      .set_texture_lock_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::TextureLock, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .texture_lock_scale_enabled = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::TextureLockScale);
      },
      .set_texture_lock_scale_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::TextureLockScale, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .grid_snap_enabled = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::GridSnap);
      },
      .set_grid_snap_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::GridSnap, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .vertex_snap_enabled = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::VertexSnap);
      },
      .set_vertex_snap_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::VertexSnap, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .rotation_snap_enabled = [this]() {
        return viewport_settings_.bool_value(
            ViewportBoolSettingId::RotationSnap);
      },
      .set_rotation_snap_enabled =
          [this](bool enabled) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::RotationSnap, enabled);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .rotation_snap_degrees = [this]() {
        return static_cast<double>(viewport_settings_.float_value(
            ViewportFloatSettingId::RotationSnapDegrees));
      },
      .set_rotation_snap_degrees =
          [this](double degrees) {
            const bool changed = viewport_settings_.set_float_value(
                ViewportFloatSettingId::RotationSnapDegrees,
                static_cast<float>(degrees));
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
      .viewport_info_visible = [this]() {
        return viewport_settings_.bool_value(ViewportBoolSettingId::ShowInfo);
      },
      .set_viewport_info_visible =
          [this](bool visible) {
            const bool changed = viewport_settings_.set_bool_value(
                ViewportBoolSettingId::ShowInfo, visible);
            if (changed) {
              (void)save_viewport_settings();
              synchronize_settings_surfaces(true);
            }
            return runtime_status(changed);
          },
  });

  if (command_event_filter_ == nullptr) {
    command_event_filter_ = std::make_unique<QtCommandEventFilter>(
        editor, [this]() {
          return viewport_panel_ != nullptr ? viewport_panel_->tool_state()
                                            : ViewportToolState{};
        });
    command_event_filter_->set_command_input_owner_provider([this]() {
      return shell_replacement_pending_ || loading_presentation_active_ ||
             command_palette_open_pending_ ||
             qt_foreign_window_owns_command_input(main_window_.get()) ||
             qt_window_owns_command_input(command_palette_) ||
             qt_window_owns_command_input(settings_dialog_) ||
             qt_window_owns_command_input(material_browser_window_) ||
             qt_window_owns_command_input(material_texture_info_window_) ||
             qt_window_owns_command_input(material_picker_window_) ||
             qt_window_owns_command_input(material_find_replace_dialog_) ||
             qt_window_owns_command_input(standard_material_editor_window_);
    });
    command_event_filter_->set_outliner_input_owner_provider([this]() {
      return qt_outliner_owns_command_input(outliner_panel_);
    });
    command_event_filter_->set_asset_browser_action_handler(
        [this](QuaderCommandId command) {
          if (asset_browser_ == nullptr) return false;
          if (command.string() == quader_command::kCutSelection.string())
            return asset_browser_->handle_action(QtAssetBrowserAction::Cut);
          if (command.string() == quader_command::kCopySelection.string())
            return asset_browser_->handle_action(QtAssetBrowserAction::Copy);
          if (command.string() == quader_command::kPasteSelection.string())
            return asset_browser_->handle_action(QtAssetBrowserAction::Paste);
          if (command.string() == quader_command::kDuplicateSelection.string())
            return asset_browser_->handle_action(QtAssetBrowserAction::Duplicate);
          if (command.string() == quader_command::kDeleteSelection.string())
            return asset_browser_->handle_action(QtAssetBrowserAction::Delete);
          return false;
        });
    command_event_filter_->set_input_context_provider([this]() {
      QuaderViewportKeybindingContext context =
          viewport_panel_ != nullptr ? viewport_panel_->command_input_context()
                                    : QuaderViewportKeybindingContext{};
      if (modeling_ != nullptr) {
        context.operation_preview_active = modeling_->operation_preview_active();
        context.modal_tool_active =
            context.modal_tool_active || modeling_->modal_tool_owns_viewport();
      }
      return context;
    });
    if (QCoreApplication *application = QApplication::instance()) {
      application->installEventFilter(command_event_filter_.get());
    }
  } else {
    // Ctrl+W can close a scene and re-enter this function from inside the
    // filter's own eventFilter call. Rebind it in place so event delivery can
    // return through the same live QObject.
    command_event_filter_->rebind_editor(editor);
  }
  refresh_project_workspace();
}

void UISystem::refresh_project_workspace(bool force) {
  QDR_PROFILE_SCOPE("UI project workspace refresh");
  if (shell_replacement_pending_ || editor_ == nullptr ||
      project_home_page_ == nullptr || scene_workspace_ == nullptr) {
    return;
  }

  observe_material_presentation_events();
  if (force) {
    project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
  }
  const std::uint64_t instance_edit_revision = [&]() {
    QDR_PROFILE_SCOPE("UI project workspace query instance edit revision");
    return editor_->modeling().instance_edit_revision();
  }();
  if (!project_workspace_instance_edit_revision_valid_ ||
      project_workspace_instance_edit_revision_ != instance_edit_revision) {
    project_workspace_instance_edit_revision_ = instance_edit_revision;
    project_workspace_instance_edit_revision_valid_ = true;
    project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
  }
  if (!project_workspace_refresh_signal_->exchange(
          false, std::memory_order_acq_rel)) {
    return;
  }

  {
    QDR_PROFILE_SCOPE("UI project workspace refresh material editor");
    refresh_standard_material_editor();
  }

  {
    QDR_PROFILE_SCOPE("UI project workspace synchronize presentation");
    const bool has_project = editor_->project().has_project();
    if (has_project) {
      show_workspace();
      // Prime retained browser rows as the project enters the workspace, so
      // the first reveal does not parse its catalog inside keyboard dispatch.
      refresh_asset_browser(false, true);
    } else {
      show_home();
    }

    if (!has_project) {
      std::vector<QtRecentProjectItem> recent;
      for (const QuaderRecentProject &project :
           editor_->project().recent_projects()) {
        recent.push_back({
            .name = QString::fromStdString(project.name),
            .manifest_path =
                QString::fromStdWString(project.manifest_path.wstring()),
            .available = project.available,
            .manifest_last_modified_unix_ms =
                project.manifest_last_modified_unix_ms,
            .last_saved_app_version =
                QString::fromStdString(project.last_saved_app_version),
        });
      }
      project_home_page_->set_recent_projects(std::move(recent));
    }

    std::vector<QtSceneTabItem> tabs;
    std::optional<QtSceneSessionId> active;
    for (const QuaderSceneDocumentInfo &scene :
         editor_->scene_documents().open_scenes()) {
      tabs.push_back({
          .session_id = scene.session_id.packed(),
          .name = QString::fromStdString(scene.name),
          .project_relative_path =
              scene.path.has_value()
                  ? QString::fromStdString(scene.path->portable_string())
                  : QString{},
          .dirty = scene.dirty,
      });
      if (scene.active) {
        active = scene.session_id.packed();
      }
    }
    if (scene_workspace_->scene_tabs() != tabs ||
        scene_workspace_->active_scene() != active) {
      (void)scene_workspace_->set_scene_tabs(std::move(tabs), active);
    }

    const QuaderInstanceEditState instance_edit =
        editor_->modeling().instance_edit_state();
    if (instance_edit.active) {
      scene_workspace_->set_instance_edit_breadcrumb(QtInstanceEditBreadcrumb{
          .scene_name = QString::fromStdString(instance_edit.scene_name),
          .instance_name = QString::fromStdString(instance_edit.instance_name),
      });
    } else {
      scene_workspace_->set_instance_edit_breadcrumb(std::nullopt);
    }

    const bool face_texture_panel_visible =
        panel_presented(UIPanelId::FaceTexture);
    const bool material_browser_visible =
        material_browser_window_ != nullptr &&
        material_browser_window_->isVisible();
    const bool material_texture_info_visible =
        material_texture_info_window_ != nullptr &&
        material_texture_info_window_->isVisible();
    const bool material_consumer_visible =
        face_texture_panel_visible ||
        panel_presented(UIPanelId::Inspector) ||
        material_browser_visible || material_texture_info_visible ||
        (material_picker_window_ != nullptr &&
         material_picker_window_->isVisible()) ||
        (material_find_replace_dialog_ != nullptr &&
         material_find_replace_dialog_->isVisible());
    if (has_project && material_consumer_visible) {
      refresh_material_presentation_cache();
    }
    if (has_project && (material_browser_visible || face_texture_panel_visible)) {
      refresh_material_browser_usage();
    }
    if (has_project && material_texture_info_visible) {
      refresh_material_texture_info();
    }
  }
}

void UISystem::refresh_face_texture_material(
    QuaderMaterialReference reference) {
  if (face_texture_panel_ == nullptr ||
      !panel_presented(UIPanelId::FaceTexture)) {
    return;
  }
  const FaceTextureMaterialPresentationStamp stamp{
      .catalog_revision = material_presentation_catalog_revision_,
      .reference = reference,
  };
  if (!face_texture_panel_presenter_.needs_refresh(stamp)) {
    return;
  }
  face_texture_panel_->set_material_presentation(
      material_presentation_for(reference, false));
  face_texture_panel_presenter_.mark_presented(stamp);
}

void UISystem::configure_material_browser(QtMaterialBrowserWindow &source) {
  const QPointer<QtMaterialBrowserWindow> browser(&source);
  browser->set_lifecycle_commands_provider([this]() {
    return editor_ != nullptr ? &editor_->commands() : nullptr;
  });
  browser->set_presentation_state_changed_callback(
      [this, browser]() { schedule_auxiliary_window_state_save(); });
  browser->set_assign_material_callback(
      [this, browser](QuaderMaterialReference reference) {
        if (editor_ == nullptr || !reference.valid()) {
          return;
        }
        const auto revision = editor_->modeling().face_texture_settings_revision();
        const auto settings = editor_->modeling().face_texture_settings();
        if (settings.selected_face_count == 0u) {
          return;
        }
        const auto generation = runtime_binding_generation_;
        const auto result = editor_->modeling().apply_material_to_selected_faces(
            reference, revision.scene_session_id, settings.selection_token);
        if (generation != runtime_binding_generation_) {
          return;
        }
        present_lifecycle_result(result);
        project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
      });
  browser->set_open_material_callback(
      [this, browser](QuaderMaterialReference reference) {
        if (reference.project_owned()) {
          open_standard_material(reference.project_asset.str());
        }
      });
  browser->set_import_textures_callback(
      [this, browser](QString destination) { import_material_textures(*browser, std::move(destination)); });
  browser->set_create_folder_callback(
      [this, browser](QString parent, QString name) {
        if (editor_ != nullptr && material_browser_project_generation_ ==
            material_browser_project_generation_signal_->load(std::memory_order_acquire)) {
          const auto generation = runtime_binding_generation_;
          const auto result = editor_->project_assets().create_material_folder(
              parent.toStdString(), name.toStdString());
          if (generation != runtime_binding_generation_) {
            return;
          }
          finish_material_folder_operation(result);
          if (result.ok()) {
            browser->set_selected_folder(parent + QLatin1Char('/') + name);
          }
        }
      });
  browser->set_rename_folder_callback(
      [this, browser](QString folder, QString name) {
        if (editor_ != nullptr && material_browser_project_generation_ ==
            material_browser_project_generation_signal_->load(std::memory_order_acquire)) {
          const auto generation = runtime_binding_generation_;
          const auto result = editor_->project_assets().rename_material_folder(
              folder.toStdString(), name.toStdString());
          if (generation != runtime_binding_generation_) {
            return;
          }
          if (result.ok() && result.changed()) {
            browser->follow_moved_folder(folder,
                folder.left(folder.lastIndexOf(QLatin1Char('/')) + 1) + name);
          }
          finish_material_folder_operation(result);
        }
      });
  browser->set_delete_folder_callback(
      [this, browser](QString folder) { delete_material_folder(std::move(folder)); });
  browser->set_move_folder_callback(
      [this, browser](QString folder, QString destination, std::uint64_t generation) {
        if (editor_ != nullptr && generation ==
            material_browser_project_generation_signal_->load(std::memory_order_acquire)) {
          const auto binding = runtime_binding_generation_;
          const auto result = editor_->project_assets().move_material_folder(
              folder.toStdString(), destination.toStdString());
          if (binding != runtime_binding_generation_) {
            return;
          }
          if (result.ok() && result.changed()) {
            browser->follow_moved_folder(folder,
                destination + folder.mid(folder.lastIndexOf(QLatin1Char('/'))));
          }
          finish_material_folder_operation(result);
        }
      });
  browser->set_move_materials_callback(
      [this, browser](std::vector<QuaderMaterialReference> references, QString destination,
             std::uint64_t generation) {
        if (editor_ == nullptr || generation !=
            material_browser_project_generation_signal_->load(std::memory_order_acquire)) {
          return;
        }
        std::vector<ProjectAssetId> ids;
        ids.reserve(references.size());
        for (const QuaderMaterialReference &reference : references) {
          if (!reference.project_owned()) {
            return;
          }
          ids.push_back(reference.project_asset);
        }
        const auto binding = runtime_binding_generation_;
        const auto result = editor_->project_assets().move_materials(
            ids, destination.toStdString());
        if (binding == runtime_binding_generation_) {
          finish_material_folder_operation(result);
        }
      });
  if (const auto default_material =
          ProjectAssetId::parse(kDefaultProjectMaterialId)) {
    browser->set_protected_material(
        QuaderMaterialReference::project(*default_material));
  }
  browser->set_material_action_callback(
      [this, browser](QtMaterialBrowserActionRequest request) {
        handle_material_browser_action(*browser, std::move(request));
      });
}

void UISystem::refresh_face_texture_browser_selection() {
  const auto revision = editor_->modeling().face_texture_settings_revision();
  const auto &face = face_texture_panel_->canvas().presentation();
  // History can republish an unchanged selection with a new revision. Only a
  // different face, material or edit context replaces the user's browser choice.
  const FaceTextureBrowserSelectionStamp stamp{
      .scene_session_id = revision.scene_session_id,
      .mode_generation = editor_->mode().generation(),
      .object_id = face.object_id,
      .face_id = face.face_id,
      .selected_face_count = face.selected_face_count,
      .reference = face_texture_panel_->canvas_material_reference(),
  };
  if (!face_texture_browser_selection_presenter_.needs_refresh(stamp) &&
      !(face_texture_browser_selection_pending_ &&
        face_texture_browser_selection_catalog_revision_ !=
            material_presentation_catalog_revision_)) {
    return;
  }
  face_texture_browser_selection_pending_ =
      stamp.selected_face_count == 1u && stamp.reference.valid() &&
      !face_texture_panel_->browser().reveal_material(stamp.reference);
  if (stamp.selected_face_count == 0u) {
    face_texture_panel_->browser().set_selected_material({});
  }
  face_texture_browser_selection_catalog_revision_ =
      material_presentation_catalog_revision_;
  face_texture_browser_selection_presenter_.mark_presented(stamp);
}

void UISystem::refresh_material_browser_usage() {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return;
  }
  if (face_texture_panel_ != nullptr) {
    face_texture_panel_->browser().set_project_generation(material_browser_project_generation_);
    face_texture_panel_->browser().set_material_paste_available(
        material_clipboard_.mode != MaterialClipboardMode::Empty &&
        material_clipboard_.project_generation == material_browser_project_generation_ &&
        !material_clipboard_.material_ids.empty());
  }


  if (material_browser_catalog_signature_ !=
      material_presentation_catalog_revision_) {
    std::vector<QtMaterialPresentationItem> project_materials;
    for (const auto &material : material_presentation_items_) {
      if (material.reference.project_owned() && material.available) {
        project_materials.push_back(material);
      }
    }
    for (auto *browser : {material_browser_window_,
                          face_texture_panel_ != nullptr
                              ? &face_texture_panel_->browser() : nullptr}) {
      if (browser == nullptr) {
        continue;
      }
      browser->set_folders(material_presentation_folders_);
      browser->set_materials(project_materials);
    }
    material_browser_catalog_signature_ = material_presentation_catalog_revision_;
  }

  // Live assignments and undo can change usage without changing the catalog or
  // emitting another dirty-state event. Keep their revision gates independent.
  const auto dependencies =
      editor_->project_assets().material_dependency_revision();
  if (dependencies.ok() &&
      material_browser_dependency_revision_ != dependencies.value()) {
    material_browser_dependency_revision_ = dependencies.value();
    if (material_browser_window_ != nullptr) {
      material_browser_window_->set_dependency_revision(dependencies.value().revision);
    }
    if (face_texture_panel_ != nullptr) {
      face_texture_panel_->browser().set_dependency_revision(dependencies.value().revision);
    }
  }

  const auto observed = editor_->project_assets().material_usage_revision();
  if (!observed.ok() || material_browser_usage_revision_ == observed.value()) {
    return;
  }
  material_browser_usage_revision_ = observed.value();

  QtMaterialBrowserUsageSnapshot presentation{
      .project_generation = material_browser_project_generation_,
      .revision = observed.value().revision,
      .complete = observed.value().complete,
  };
  const auto usage = editor_->project_assets().material_usage();
  if (!usage.ok()) {
    presentation.complete = false;
    presentation.errors.push_back(QString::fromStdString(usage.error_message()));
  } else {
    presentation.used_materials = usage.value().used_materials;
    presentation.complete = usage.value().complete;
    for (const QuaderProjectMaterialUsageIssue &issue : usage.value().issues) {
      const std::string &source = issue.project_relative_path.empty()
                                      ? issue.scene_name
                                      : issue.project_relative_path;
      const QString message = QString::fromStdString(source + ": " + issue.message);
      if (!presentation.errors.contains(message)) {
        presentation.errors.push_back(message);
      }
    }
  }
  if (material_browser_window_ != nullptr) {
    material_browser_window_->set_material_usage(presentation);
  }
  if (face_texture_panel_ != nullptr) {
    face_texture_panel_->browser().set_material_usage(std::move(presentation));
  }
}

void UISystem::invalidate_material_presentation_cache() {
  ++material_presentation_invalidation_generation_;
  material_presentation_cache_valid_ = false;
  inspector_material_query_presenter_.invalidate();
}

void UISystem::observe_material_presentation_events() {
  const std::uint64_t project_generation =
      material_browser_project_generation_signal_->load(std::memory_order_acquire);
  if (project_generation != material_browser_project_generation_) {
    material_browser_project_generation_ = project_generation;
    material_browser_dependency_revision_.reset();
    material_browser_usage_revision_.reset();
    material_presentation_cache_valid_ = false;
    inspector_material_query_presenter_.invalidate();
  }
  const std::uint64_t generation =
      material_presentation_event_generation_signal_->load(
          std::memory_order_acquire);
  if (generation == material_presentation_observed_event_generation_) {
    return;
  }
  material_presentation_observed_event_generation_ = generation;
  if (editor_ != nullptr && material_presentation_cache_valid_ &&
      material_presentation_catalog_attempt_) {
    const auto owner = editor_->project_assets().material_presentation_revision();
    if (owner.ok() &&
        owner.value() == material_presentation_catalog_attempt_->owner_revision) {
      // Project asset events also cover scenes and folders. With texture
      // contents included in the owner stamp, an unchanged material catalog
      // needs no full projection or image reads for those notifications.
      material_presentation_catalog_attempt_->event_generation = generation;
      return;
    }
  }
  material_presentation_cache_valid_ = false;
  inspector_material_query_presenter_.invalidate();
}

void UISystem::refresh_material_presentation_cache() {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return;
  }
  QDR_PROFILE_SCOPE("UI material presentation cache refresh");
  observe_material_presentation_events();

  const std::uint64_t active_scene_id = [&]() {
    if (editor_ == nullptr) {
      return std::uint64_t{0};
    }
    const std::optional<SceneSessionId> scene_id =
        editor_->scene_documents().active_scene_id();
    return scene_id.has_value() ? scene_id->packed() : std::uint64_t{0};
  }();

  if (editor_ == nullptr || !editor_->project().has_project()) {
    const bool presentation_changed =
        material_presentation_cache_valid_ ||
        !material_presentation_items_.empty();
    material_presentation_items_.clear();
    material_presentation_folders_.clear();
    material_browser_project_identity_.clear();
    material_presentation_signature_ = 0;
    material_presentation_cache_valid_ = false;
    material_presentation_catalog_attempt_.reset();
    material_presentation_context_observation_.reset();
    material_browser_dependency_revision_.reset();
    material_browser_usage_revision_.reset();
    material_clipboard_ = {};
    material_texture_info_catalog_revision_ = 0u;
    if (material_browser_window_ != nullptr) {
      material_browser_window_->set_material_paste_available(false);
    }
    if (material_texture_info_window_ != nullptr) {
      material_texture_info_window_->clear();
      material_texture_info_window_->hide();
    }
    if (presentation_changed) {
      ++material_presentation_catalog_revision_;
      ++material_presentation_revision_;
      inspector_presenter_.invalidate();
      inspector_material_query_presenter_.invalidate();
      synchronize_material_presentation_consumers();
    }
    return;
  }

  // Project materials remain available with no open scene. Do not include the
  // host's fallback scene or its embedded assets in that project-only state.
  const bool scene_available = active_scene_id != 0u &&
                               document_ != nullptr && assets_ != nullptr &&
                               modeling_ != nullptr;
  const MaterialPresentationContextStamp context_stamp{
      .document = scene_available ? document_ : nullptr,
      .modeling = scene_available ? modeling_ : nullptr,
      .assets = scene_available ? assets_ : nullptr,
      .active_scene_id = scene_available ? active_scene_id : 0u,
      .material_binding_revision =
          scene_available ? document_->material_binding_revision() : 0u,
      .mesh_binding_revision =
          scene_available ? document_->mesh_binding_revision() : 0u,
      .mesh_material_usage_revision =
          scene_available ? assets_->mesh_material_usage_revision() : 0u,
      .hierarchy_revision =
          scene_available ? document_->hierarchy_presentation_revision() : 0u,
      .modeling_content_revision =
          scene_available ? modeling_->content_revision() : 0u,
      .modeling_object_map_revision =
          scene_available ? modeling_->modeling_object_map_revision() : 0u,
  };
  const bool context_changed =
      !material_presentation_context_observation_.has_value() ||
      *material_presentation_context_observation_ != context_stamp;
  material_presentation_context_observation_ = context_stamp;

  const QuaderApiResult<QuaderMaterialPresentationRevision> owner_revision =
      editor_->project_assets().material_presentation_revision();
  if (!owner_revision.succeeded()) {
    material_presentation_cache_valid_ = false;
    material_presentation_catalog_attempt_.reset();
    return;
  }
  const MaterialPresentationCatalogStamp catalog_stamp{
      .editor = editor_,
      .owner_revision = owner_revision.value(),
      .event_generation =
          material_presentation_observed_event_generation_,
      .invalidation_generation =
          material_presentation_invalidation_generation_,
      .localization_revision = localization_.revision(),
  };
  if (material_presentation_catalog_attempt_.has_value() &&
      *material_presentation_catalog_attempt_ == catalog_stamp) {
    if (context_changed) {
      ++material_presentation_revision_;
      inspector_presenter_.invalidate();
      inspector_material_query_presenter_.invalidate();
      synchronize_material_presentation_consumers();
    }
    return;
  }

  // Record the attempted owner/event inputs before querying. A failed project
  // catalog read therefore stays gated until a real owner or event changes.
  material_presentation_catalog_attempt_ = catalog_stamp;
  ++material_presentation_snapshot_build_count_;
  QuaderApiResult<QuaderMaterialPresentationSnapshot> materials =
      editor_->project_assets().material_presentations();
  if (!materials.succeeded()) {
    const bool presentation_changed =
        material_presentation_cache_valid_ ||
        !material_presentation_items_.empty();
    material_presentation_items_.clear();
    material_presentation_signature_ = 0;
    material_presentation_cache_valid_ = false;
    if (presentation_changed) {
      ++material_presentation_catalog_revision_;
    }
    if (presentation_changed || context_changed) {
      ++material_presentation_revision_;
      inspector_presenter_.invalidate();
      inspector_material_query_presenter_.invalidate();
      synchronize_material_presentation_consumers();
    }
    return;
  }

  const auto reuse_or_make_thumbnail =
      [this](QtMaterialPresentationItem &item) {
        const auto existing = std::ranges::find_if(
            material_presentation_items_,
            [&item](const QtMaterialPresentationItem &candidate) {
              return candidate.reference == item.reference;
            });
        if (existing != material_presentation_items_.end() &&
            existing->content_revision == item.content_revision &&
            existing->encoded_albedo == item.encoded_albedo &&
            existing->fallback_color == item.fallback_color &&
            existing->thumbnail) {
          item.thumbnail = existing->thumbnail;
        } else {
          item.thumbnail = make_qt_material_thumbnail(item);
        }
      };

  std::vector<QtMaterialPresentationItem> next;
  next.reserve(materials.value().materials.size());
  for (const QuaderMaterialPresentationItem &material :
       materials.value().materials) {
    const QuaderMaterialReference reference = material.reference;
    const std::string_view type_key = [&material]() -> std::string_view {
      switch (material.kind) {
      case QuaderMaterialPresentationKind::Embedded:
        return "ui.material_browser.embedded_material_type";
      case QuaderMaterialPresentationKind::Standard:
        return "ui.material_browser.standard_material_type";
      case QuaderMaterialPresentationKind::Graph:
        return "ui.material_browser.graph_material_type";
      }
      return "ui.material_browser.embedded_material_type";
    }();
    const std::string_view type_fallback =
        material.kind == QuaderMaterialPresentationKind::Embedded
            ? "Embedded Material"
            : material.kind == QuaderMaterialPresentationKind::Standard
                  ? "Standard Material"
                  : "Graph Material";
    QtMaterialPresentationItem item{
        .reference = reference,
        .stable_id = qt_material_reference_stable_id(reference),
        .name = QString::fromStdString(material.name),
        .project_relative_path =
            QString::fromStdString(material.project_relative_path),
        .type_display_name =
            QString::fromStdString(localization_.text(type_key, type_fallback)),
        .qmat_size_bytes = static_cast<qint64>(material.qmat_size_bytes),
        .encoded_albedo = QByteArray(
            reinterpret_cast<const char *>(material.encoded_albedo.data()),
            static_cast<qsizetype>(material.encoded_albedo.size())),
        .fallback_color = QColor::fromRgbF(
            material.fallback_color[0], material.fallback_color[1],
            material.fallback_color[2]),
        .content_revision = material.content_revision,
        .available = material.available,
        .editable = material.editable,
    };
    reuse_or_make_thumbnail(item);
    item.has_valid_albedo =
        item.thumbnail != nullptr && item.thumbnail->source_image_valid;
    next.push_back(std::move(item));
  }

  std::uint64_t signature = 1469598103934665603ull;
  const auto mix = [&signature](std::uint64_t value) {
    signature ^= value;
    signature *= 1099511628211ull;
  };
  const auto mix_text = [&mix](std::string_view value) {
    for (const char character : value) {
      mix(static_cast<unsigned char>(character));
    }
  };
  const QuaderProjectMetadata project = editor_->project().metadata();
  const std::string project_identity = project.uuid + "|" +
      editor_->project().manifest_path().generic_string();
  if (material_browser_project_identity_ != project_identity) {
    material_browser_project_identity_ = project_identity;
    material_browser_project_generation_ =
        material_browser_project_generation_signal_->fetch_add(1u,
            std::memory_order_acq_rel) + 1u;
    material_browser_dependency_revision_.reset();
    material_browser_usage_revision_.reset();
    material_clipboard_ = {};
    material_texture_info_catalog_revision_ = 0u;
    if (material_browser_window_ != nullptr) {
      material_browser_window_->set_material_paste_available(false);
    }
    if (material_texture_info_window_ != nullptr) {
      material_texture_info_window_->clear();
      material_texture_info_window_->hide();
    }
  }
  mix(material_browser_project_generation_);
  mix_text(project.uuid);
  mix_text(editor_->project().manifest_path().generic_string());
  mix_text(localization_.current_locale());
  std::vector<QString> next_folders;
  next_folders.reserve(materials.value().material_folders.size());
  for (const std::string &folder : materials.value().material_folders) {
    mix_text(folder);
    mix(0xffu);
    next_folders.push_back(QString::fromStdString(folder));
  }
  for (const QtMaterialPresentationItem &item : next) {
    const QByteArray stable_id = item.stable_id.toUtf8();
    const QByteArray name = item.name.toUtf8();
    const QByteArray path = item.project_relative_path.toUtf8();
    const QByteArray type = item.type_display_name.toUtf8();
    mix_text(std::string_view(stable_id.constData(), stable_id.size()));
    mix_text(std::string_view(name.constData(), name.size()));
    mix_text(std::string_view(path.constData(), path.size()));
    mix_text(std::string_view(type.constData(), type.size()));
    mix(static_cast<std::uint64_t>(item.qmat_size_bytes));
    mix(item.content_revision);
    mix(item.has_valid_albedo ? 1u : 0u);
    mix(static_cast<std::uint64_t>(item.fallback_color.rgba()));
    mix(item.available ? 1u : 0u);
    mix(item.editable ? 1u : 0u);
  }

  if (signature == material_presentation_signature_) {
    material_presentation_cache_valid_ = true;
    if (context_changed) {
      ++material_presentation_revision_;
      inspector_presenter_.invalidate();
      inspector_material_query_presenter_.invalidate();
      synchronize_material_presentation_consumers();
    }
    return;
  }

  material_presentation_items_ = std::move(next);
  material_presentation_folders_ = std::move(next_folders);
  material_presentation_cache_valid_ = true;
  material_presentation_signature_ = signature;
  ++material_presentation_catalog_revision_;
  ++material_presentation_revision_;
  inspector_presenter_.invalidate();
  inspector_material_query_presenter_.invalidate();
  synchronize_material_presentation_consumers();
}

void UISystem::revalidate_material_find_replace_presentations() {
  if (material_find_replace_dialog_ == nullptr ||
      !material_find_replace_dialog_->isVisible() ||
      material_find_replace_presentation_revision_ ==
          material_presentation_revision_) {
    return;
  }
  const std::optional<QtMaterialPresentationItem> current_find =
      material_find_replace_dialog_->material(
          QtMaterialFindReplaceColumn::Find);
  const std::optional<QtMaterialPresentationItem> current_replacement =
      material_find_replace_dialog_->material(
          QtMaterialFindReplaceColumn::Replace);
  if (current_find.has_value()) {
    material_find_replace_dialog_->set_find_material(
        material_presentation_for(current_find->reference, true));
  }
  if (current_replacement.has_value()) {
    material_find_replace_dialog_->set_replacement_material(
        material_presentation_for(current_replacement->reference, true));
  }
  material_find_replace_presentation_revision_ =
      material_presentation_revision_;
}

void UISystem::refresh_material_picker_presentations() {
  if (material_picker_window_ == nullptr ||
      !material_picker_window_->isVisible() || editor_ == nullptr ||
      material_picker_presentation_revision_ ==
          material_presentation_revision_) {
    return;
  }
  const QuaderMaterialReference selected =
      material_picker_window_->selected_material();
  std::vector<QtMaterialPresentationItem> items;
  if (material_picker_replacement_column_) {
    items = replacement_material_presentations();
  } else {
    const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
        editor_->materials().used_materials(material_find_replace_scope_);
    if (used.succeeded()) {
      items = material_presentations_for(used.value(), true);
    }
  }
  material_picker_window_->set_project_generation(material_browser_project_generation_);
  material_picker_window_->set_folders(material_presentation_folders_);
  material_picker_window_->set_materials(std::move(items), selected);
  material_picker_presentation_revision_ =
      material_presentation_revision_;
}

void UISystem::synchronize_material_presentation_consumers() {
  revalidate_material_find_replace_presentations();
  refresh_material_picker_presentations();
}

std::optional<QtMaterialPresentationItem>
UISystem::material_presentation_for(QuaderMaterialReference reference,
                                    bool include_missing) const {
  const auto found = std::ranges::find_if(
      material_presentation_items_,
      [&reference](const QtMaterialPresentationItem &item) {
        return item.reference == reference;
      });
  if (found != material_presentation_items_.end()) {
    return *found;
  }
  if (!include_missing || !reference.valid()) {
    return std::nullopt;
  }
  QtMaterialPresentationItem missing{
      .reference = reference,
      .stable_id = qt_material_reference_stable_id(reference),
      .name = QString::fromStdString(localization_.text(
          "ui.inspector.material.missing", "Missing material")),
      .type_display_name = QString::fromStdString(localization_.text(
          "ui.inspector.material.missing", "Missing material")),
      .fallback_color = QColor(Qt::darkGray),
      .available = false,
      .editable = false,
  };
  missing.thumbnail = make_qt_material_thumbnail(missing);
  return missing;
}

std::vector<QtMaterialPresentationItem>
UISystem::material_presentations_for(
    const std::vector<QuaderMaterialReference> &references,
    bool include_missing) const {
  std::vector<QtMaterialPresentationItem> result;
  result.reserve(references.size());
  for (const QuaderMaterialReference &reference : references) {
    if (std::ranges::any_of(
            result, [&reference](const QtMaterialPresentationItem &item) {
              return item.reference == reference;
            })) {
      continue;
    }
    std::optional<QtMaterialPresentationItem> presentation =
        material_presentation_for(reference, include_missing);
    if (presentation.has_value()) {
      result.push_back(std::move(*presentation));
    }
  }
  return result;
}

std::vector<QtMaterialPresentationItem>
UISystem::replacement_material_presentations() const {
  std::vector<QtMaterialPresentationItem> result;
  for (const QtMaterialPresentationItem &material :
       material_presentation_items_) {
    if (material.replacement_valid()) {
      result.push_back(material);
    }
  }
  return result;
}

void UISystem::refresh_inspector_material_presentation() {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return;
  }
  if (inspector_panel_ == nullptr || editor_ == nullptr ||
      document_ == nullptr || assets_ == nullptr || modeling_ == nullptr) {
    return;
  }
  refresh_material_presentation_cache();
  const InspectorMaterialQueryStamp stamp{
      .editor_mode_revision = editor_->mode().revision(),
      .presentation_revision = material_presentation_revision_,
      .scene_selection_revision = document_->selection_revision(),
      .hierarchy_revision = document_->hierarchy_presentation_revision(),
      .material_binding_revision = document_->material_binding_revision(),
      .mesh_binding_revision = document_->mesh_binding_revision(),
      .mesh_material_usage_revision =
          assets_->mesh_material_usage_revision(),
      .modeling_content_revision = modeling_->content_revision(),
      .modeling_selection_revision = modeling_->selection_revision(),
      .modeling_object_map_revision =
          modeling_->modeling_object_map_revision(),
  };
  if (!inspector_material_query_presenter_.needs_refresh(stamp)) {
    return;
  }

  const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
      editor_->materials().used_materials(
          QuaderMaterialUsageScope::Selection);
  const QuaderSelectionSummary selection = editor_->selection().summary();
  const bool object_material_surface_selection =
      selection.mode == QuaderSelectionMode::Object &&
      std::ranges::any_of(
          document_->selected_entities(), [this](EntityId entity) {
            return scene_entity_has_material_surface(*document_, entity);
          });
  const bool section_visible =
      selection.mode == QuaderSelectionMode::Face ||
      selection.mode == QuaderSelectionMode::Group ||
      object_material_surface_selection;
  std::uint64_t revision = 1469598103934665603ull;
  const auto mix = [&revision](std::uint64_t value) {
    revision ^= value;
    revision *= 1099511628211ull;
  };
  mix(material_presentation_revision_);
  mix(stamp.editor_mode_revision);
  mix(stamp.scene_selection_revision);
  mix(stamp.hierarchy_revision);
  mix(stamp.material_binding_revision);
  mix(stamp.mesh_binding_revision);
  mix(stamp.mesh_material_usage_revision);
  mix(stamp.modeling_content_revision);
  mix(stamp.modeling_selection_revision);
  mix(stamp.modeling_object_map_revision);
  inspector_panel_->set_material_presentation(
      InspectorMaterialPresentationSnapshot{
          .revision = revision,
          .section_visible = used.succeeded() && section_visible,
          .materials = used.succeeded()
                           ? material_presentations_for(used.value(), true)
                           : std::vector<QtMaterialPresentationItem>{},
          .edit_material =
              [this](QuaderMaterialReference reference) {
                if (reference.project_owned()) {
                  open_standard_material(reference.project_asset.str());
                }
              },
          .replace_material =
              [this](QuaderMaterialReference reference) {
                show_material_find_replace(reference);
              },
      });
  inspector_material_query_presenter_.mark_presented(stamp);
}

void UISystem::show_home() {
  if (home_visible_) {
    return;
  }
  const bool presentation_changed = !home_visible_;
  if (presentation_changed && !main_window_close_in_progress_) {
    capture_workspace_window_state();
  }
  home_visible_ = true;
  if (presentation_changed && material_browser_window_ != nullptr) {
    material_browser_window_->hide();
    material_browser_window_->clear_materials();
    material_browser_catalog_signature_ = 0;
  }
  if (presentation_changed) {
    close_material_find_replace();
    if (face_texture_panel_ != nullptr) {
      face_texture_panel_->set_material_presentation(std::nullopt);
      face_texture_panel_->browser().clear_materials();
    }
    face_texture_panel_presenter_.invalidate();
    face_texture_browser_selection_presenter_.invalidate();
    material_presentation_items_.clear();
    material_presentation_folders_.clear();
    material_browser_project_identity_.clear();
    material_presentation_signature_ = 0;
    material_presentation_catalog_attempt_.reset();
    material_presentation_context_observation_.reset();
    invalidate_material_presentation_cache();
    ++material_presentation_catalog_revision_;
    ++material_presentation_revision_;
  }
  if (standard_material_editor_window_ != nullptr) {
    standard_material_editor_window_->hide();
    standard_material_editor_window_->clear_document();
    standard_material_editor_presenter_.invalidate();
  }
  if (material_texture_info_window_ != nullptr) {
    material_texture_info_window_->clear();
    material_texture_info_window_->hide();
  }
  if (central_stack_ != nullptr && project_home_page_ != nullptr) {
    central_stack_->setCurrentWidget(project_home_page_);
  }
  if (main_window_ != nullptr) {
    main_window_->menuBar()->hide();
    main_window_->statusBar()->hide();
  }
  if (selection_toolbar_ != nullptr) {
    selection_toolbar_->setEnabled(false);
    selection_toolbar_->hide();
  }
  if (snapping_toolbar_ != nullptr) {
    snapping_toolbar_->setEnabled(false);
    snapping_toolbar_->hide();
  }
  if (viewport_display_toolbar_ != nullptr) {
    viewport_display_toolbar_->setEnabled(false);
    viewport_display_toolbar_->hide();
  }
  if (texture_toolbar_ != nullptr) {
    texture_toolbar_->setEnabled(false);
    texture_toolbar_->hide();
  }
  if (tool_toolbar_ != nullptr) {
    tool_toolbar_->setEnabled(false);
    tool_toolbar_->hide();
  }
  for (UIPanelId id : kUiDockPanelIds) {
    if (QDockWidget *dock = panels_.dock(id)) {
      dock->hide();
    }
  }
  if (presentation_changed && main_menu_ != nullptr) {
    main_menu_->refresh();
  }
}

void UISystem::synchronize_tool_toolbar(
    const QuaderSelectionSummary &selection, bool force) {
  if (editor_ == nullptr) {
    return;
  }
  if (tool_toolbar_ != nullptr) {
    tool_toolbar_->synchronize(*editor_, selection, force);
  }
}

void UISystem::show_workspace() {
  const bool presentation_changed = home_visible_;
  if (!viewport_initialized_ && viewport_panel_ != nullptr &&
      render_system_ != nullptr && !render_system_->initialized() &&
      composition_anchor_ == nullptr) {
    // Native surfaces require the renderer before entering the workspace.
    // Only the composed bootstrap prepares chrome before its Qt device exists.
    project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
    return;
  }
  if (!viewport_initialized_ && viewport_panel_ != nullptr &&
      render_system_ != nullptr && render_system_->initialized()) {
    if (!viewport_panel_->initialize(*render_system_)) {
      LOG_ERROR("Unable to initialize the native viewport surface");
      project_workspace_refresh_signal_->store(true,
                                               std::memory_order_relaxed);
      return;
    }
    viewport_initialized_ = true;
    if (main_window_ != nullptr && main_window_->isVisible()) {
      prepare_startup_presentation();
    }
  }
  if (!presentation_changed) {
    return;
  }
  home_visible_ = false;
  if (central_stack_ != nullptr && scene_workspace_ != nullptr) {
    central_stack_->setCurrentWidget(scene_workspace_);
  }
  if (main_window_ != nullptr) {
    main_window_->menuBar()->show();
    main_window_->statusBar()->show();
  }
  if (selection_toolbar_ != nullptr) {
    selection_toolbar_->setEnabled(true);
    selection_toolbar_->setVisible(selection_toolbar_workspace_visible_);
  }
  if (snapping_toolbar_ != nullptr) {
    snapping_toolbar_->setEnabled(true);
    snapping_toolbar_->setVisible(snapping_toolbar_workspace_visible_);
  }
  if (viewport_display_toolbar_ != nullptr) {
    viewport_display_toolbar_->setEnabled(true);
  }
  if (texture_toolbar_ != nullptr) {
    texture_toolbar_->setEnabled(true);
    texture_toolbar_->setVisible(texture_toolbar_workspace_visible_);
  }
  if (tool_toolbar_ != nullptr) {
    tool_toolbar_->setEnabled(true);
    tool_toolbar_->setVisible(tool_toolbar_workspace_visible_);
  }
  if (presentation_changed && editor_ != nullptr) {
    const QuaderSelectionSummary selection = editor_->selection().summary();
    if (selection_toolbar_ != nullptr && modeling_ != nullptr) {
      const QuaderSelectionMode active_mode =
          to_api_selection_mode(modeling_->selection_mode());
      selection_toolbar_->synchronize(*editor_, active_mode, true);
    }
    if (snapping_toolbar_ != nullptr) {
      snapping_toolbar_->synchronize(*editor_, true);
    }
    if (viewport_display_toolbar_ != nullptr) {
      viewport_display_toolbar_->synchronize(*editor_, true);
    }
    if (texture_toolbar_ != nullptr) {
      texture_toolbar_->synchronize(*editor_, true);
    }
    if (tool_toolbar_ != nullptr) {
      synchronize_tool_toolbar(selection, true);
    }
  }
  panels_.apply_settings(settings_);
  if (presentation_changed && main_menu_ != nullptr) {
    main_menu_->refresh();
  }
}

QuaderDirtyProjectDecision
UISystem::confirm_dirty_project_replacement(std::string_view action_id) {
  const bool opening = action_id == "project.open";
  const bool closing = action_id == "application.close";
  if (unsaved_confirmation_ == nullptr) {
    return QuaderDirtyProjectDecision::Cancel;
  }
  const QtUnsavedConfirmationDecision decision =
      unsaved_confirmation_->present(QtUnsavedConfirmationPresentation{
          .object_name =
              QStringLiteral("project_unsaved_confirmation"),
          .title = QString::fromStdString(localization_.text(
              "ui.project.unsaved.title", "Unsaved Changes")),
          .message = QString::fromStdString(localization_.text(
              opening   ? "ui.project.unsaved.open"
              : closing ? "ui.project.unsaved.close"
                        : "ui.project.unsaved.new",
              opening   ? "Save changes before opening another project?"
              : closing ? "Save changes before closing Quader?"
                        : "Save changes before creating a new project?")),
          .save_label = QString::fromStdString(
              localization_.text("ui.project.unsaved.save", "Save")),
          .discard_label = QString::fromStdString(localization_.text(
              "ui.project.unsaved.discard", "Discard")),
          .cancel_label = QString::fromStdString(
              localization_.text("ui.project.unsaved.cancel", "Cancel")),
      });
  if (decision == QtUnsavedConfirmationDecision::Save) {
    return QuaderDirtyProjectDecision::Save;
  }
  if (decision == QtUnsavedConfirmationDecision::Discard) {
    return QuaderDirtyProjectDecision::Discard;
  }
  return QuaderDirtyProjectDecision::Cancel;
}

QuaderDirtySceneDecision
UISystem::confirm_dirty_scene(const QuaderSceneDocumentInfo &scene,
                              std::string_view action_id) {
  (void)action_id;
  if (unsaved_confirmation_ == nullptr) {
    return QuaderDirtySceneDecision::Cancel;
  }
  const QtUnsavedConfirmationDecision decision =
      unsaved_confirmation_->present(QtUnsavedConfirmationPresentation{
          .object_name = QStringLiteral("scene_unsaved_confirmation"),
          .title = QString::fromStdString(localization_.text(
              "ui.scene.unsaved.title", "Unsaved Scene")),
          .message = QString::fromStdString(
              scene.name + " has unsaved changes. Save it?"),
          .save_label = QString::fromStdString(
              localization_.text("ui.scene.unsaved.save", "Save")),
          .discard_label = QString::fromStdString(localization_.text(
              "ui.scene.unsaved.discard", "Discard")),
          .cancel_label = QString::fromStdString(
              localization_.text("ui.scene.unsaved.cancel", "Cancel")),
      });
  if (decision == QtUnsavedConfirmationDecision::Save) {
    return QuaderDirtySceneDecision::Save;
  }
  if (decision == QtUnsavedConfirmationDecision::Discard) {
    return QuaderDirtySceneDecision::Discard;
  }
  return QuaderDirtySceneDecision::Cancel;
}

bool UISystem::prepare_standard_material_project_transition(
    std::string_view action_id) {
  (void)action_id;
  return resolve_standard_material_document(true);
}

bool UISystem::allow_main_window_close() {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return true;
  }
  if (!resolve_standard_material_document(true)) {
    return false;
  }
  const QScopedValueRollback closing(main_window_close_in_progress_, true);
  const QuaderApiVoidResult closed = editor_->project().close();
  return closed.ok() && !editor_->project().has_project();
}

void UISystem::show() {
  if (!shell_replacement_pending_ && main_window_ != nullptr &&
      !main_window_->isVisible()) {
    startup_presentation_pending_ = true;
    prepare_startup_presentation();
    main_window_->show();
    // App initializes the renderer synchronously immediately after this call.
    // Submit the logo cover first instead of waiting for the first frame timer.
    static_cast<QtLoadingCover *>(loading_cover_)->flush_presentation();
    schedule_retained_message_box_prewarm();
  }
}

void UISystem::schedule_retained_message_box_prewarm() {
  if (main_window_ == nullptr || retained_message_box_prewarm_scheduled_) {
    return;
  }
  retained_message_box_prewarm_scheduled_ = true;
  const std::array<QPointer<QtRetainedMessageBox>, 5u> message_boxes{
      QPointer<QtRetainedMessageBox>(unsaved_confirmation_),
      QPointer<QtRetainedMessageBox>(standard_material_unsaved_confirmation_),
      QPointer<QtRetainedMessageBox>(lifecycle_message_box_),
      QPointer<QtRetainedMessageBox>(standard_material_error_message_box_),
      QPointer<QtRetainedMessageBox>(material_browser_import_message_box_),
  };
  QTimer::singleShot(0, main_window_.get(), [this, message_boxes]() {
    retained_message_box_prewarm_scheduled_ = false;
    if (main_window_ == nullptr || !main_window_->isVisible() ||
        !main_window_->updatesEnabled()) {
      return;
    }
    for (const QPointer<QtRetainedMessageBox> &message_box : message_boxes) {
      if (message_box != nullptr) {
        message_box->prewarm();
      }
    }
    if (viewport_panel_ != nullptr) {
      viewport_panel_->prewarm_retained_dialogs();
    }
  });
}

const RenderVulkanSharedContext *UISystem::composition_context() const {
  return composition_anchor_ != nullptr ? composition_anchor_->shared_context()
                                        : nullptr;
}

bool UISystem::initialize_render_resources() {
  if (render_system_ == nullptr || !render_system_->initialized()) {
    return false;
  }
  if (material_preview_surface_ != nullptr &&
      !material_preview_surface_->initialize(*render_system_)) {
    LOG_ERROR("Unable to initialize the Standard Material preview surface");
    return false;
  }
  return true;
}

void UISystem::set_composed_frame_prepare_callback(std::function<void()> callback) {
  if (viewport_panel_ != nullptr) {
    viewport_panel_->set_composed_frame_prepare_callback(std::move(callback));
  }
}

void UISystem::set_presentation_vsync(bool enabled) {
  if (render_system_ != nullptr) {
    render_system_->set_vsync_enabled(enabled);
  }
  if (composition_anchor_ != nullptr) {
    composition_anchor_->set_vsync_enabled(enabled);
  }
}

void UISystem::detach_render_resources() {
  viewport_presentation_requested_ = {};
  if (render_resources_detached_ ||
      (main_window_ == nullptr && render_system_ == nullptr)) {
    return;
  }
  render_resources_detached_ = true;
  set_composed_frame_prepare_callback({});
  if (composition_anchor_ != nullptr) {
    composition_anchor_->set_release_callback({});
  }
  if (face_texture_panel_ != nullptr) {
    face_texture_panel_->unbind();
  }
  clear_editor_runtime_hooks();
  abandon_scene_backed_panels();
  (void)save_settings();
  (void)save_viewport_settings();
  save_window_state();
  if (main_window_ != nullptr) {
    main_window_->hide();
  }
  clear_project_workspace_events();
  if (command_event_filter_ != nullptr) {
    if (QCoreApplication *application = QApplication::instance()) {
      application->removeEventFilter(command_event_filter_.get());
    }
    command_event_filter_.reset();
  }
  if (tool_settings_panel_ != nullptr) {
    tool_settings_panel_->unbind();
  }
  if (viewport_panel_ != nullptr) {
    viewport_panel_->set_settings_panel(nullptr);
    if (shell_replacement_pending_) {
      viewport_panel_->abandon_interactions();
    } else {
      viewport_panel_->reset_interactions(editor_, native_tools_);
    }
    viewport_panel_->shutdown();
  }
  if (material_preview_surface_ != nullptr) {
    material_preview_surface_->shutdown();
  }
  if (asset_thumbnails_ != nullptr) {
    asset_thumbnails_->shutdown();
  }
  panels_.set_visibility_changed_callback({});
  panels_.set_presentation_changed_callback({});
  if (command_palette_ != nullptr) {
    command_palette_->hide();
  }
  if (composition_anchor_ != nullptr) {
    composition_anchor_->set_render_system(nullptr);
  }
}

void UISystem::shutdown() {
  detach_render_resources();
  panels_.clear();
  main_menu_.reset();
  main_window_.reset();
  command_palette_ = nullptr;
  command_palette_open_pending_ = false;
  startup_presentation_ = {};
  loading_geometry_stable_since_ = {};
  loading_geometry_signature_ = 0u;
  loading_cover_ = nullptr;
  loading_previous_focus_.clear();
  loading_operation_depth_ = 0u;
  loading_presentation_active_ = false;
  loading_previous_presentation_pending_ = false;
  startup_presentation_pending_ = false;
  shell_replacement_pending_ = false;
  viewport_panel_ = nullptr;
  central_stack_ = nullptr;
  composition_anchor_ = nullptr;
  project_home_page_ = nullptr;
  scene_workspace_ = nullptr;
  selection_toolbar_ = nullptr;
  snapping_toolbar_ = nullptr;
  viewport_display_toolbar_ = nullptr;
  texture_toolbar_ = nullptr;
  tool_toolbar_ = nullptr;
  workspace_window_state_.clear();
  outliner_panel_ = nullptr;
  inspector_panel_ = nullptr;
  layer_panel_ = nullptr;
  console_panel_ = nullptr;
  asset_browser_ = nullptr;
  asset_browser_discovery_pending_ = false;
  asset_thumbnails_.reset();
  asset_thumbnail_picker_.clear();
  asset_thumbnail_picker_current_ = {};
  asset_browser_visible_thumbnail_ids_.clear();
  asset_picker_visible_thumbnail_ids_.clear();
  asset_thumbnail_dock_presented_ = false;
  asset_thumbnail_picker_presented_ = false;
  asset_thumbnail_provider_ = {};
  asset_browser_revision_ = 0u;
  asset_browser_generation_ = 0u;
  asset_clipboard_paths_.clear();
  settings_dialog_ = nullptr;
  material_browser_window_ = nullptr;
  material_texture_info_window_ = nullptr;
  face_texture_panel_ = nullptr;
  material_picker_window_ = nullptr;
  material_find_replace_dialog_ = nullptr;
  material_preview_surface_ = nullptr;
  standard_material_editor_window_ = nullptr;
  tool_settings_panel_ = nullptr;
  unsaved_confirmation_ = nullptr;
  standard_material_unsaved_confirmation_ = nullptr;
  lifecycle_message_box_ = nullptr;
  standard_material_error_message_box_ = nullptr;
  material_browser_import_message_box_ = nullptr;
  status_message_ = nullptr;
  operation_input_hints_ = nullptr;
  status_version_ = nullptr;
  status_selection_ = nullptr;
  auxiliary_window_save_timer_ = nullptr;
  ui_scale_controller_.reset();
  render_system_ = nullptr;
  editor_ = nullptr;
  document_ = nullptr;
  assets_ = nullptr;
  modeling_ = nullptr;
  selection_ = nullptr;
  native_tools_ = nullptr;
  point_placement_ = nullptr;
  initialized_ = false;
  retained_message_box_prewarm_scheduled_ = false;
  viewport_initialized_ = false;
  home_visible_ = false;
  project_workspace_instance_edit_revision_ = 0;
  project_workspace_instance_edit_revision_valid_ = false;
  project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
  material_presentation_items_.clear();
  material_presentation_signature_ = 0;
  material_presentation_revision_ = 0;
  material_presentation_folders_.clear();
  material_browser_project_identity_.clear();
  material_presentation_catalog_revision_ = 0;
  material_presentation_snapshot_build_count_ = 0;
  face_texture_panel_presenter_.invalidate();
  material_presentation_catalog_attempt_.reset();
  material_presentation_context_observation_.reset();
  material_presentation_cache_valid_ = false;
  material_presentation_observed_event_generation_ = 0;
  material_presentation_invalidation_generation_ = 1;
  material_presentation_event_generation_signal_->store(
      1u, std::memory_order_relaxed);
  inspector_material_query_presenter_.invalidate();
  material_browser_catalog_signature_ = 0;
  material_browser_dependency_revision_.reset();
  material_browser_usage_revision_.reset();
  material_clipboard_ = {};
  material_find_replace_project_identity_.clear();
  material_find_replace_scene_id_ = 0;
  material_find_replace_scene_selection_revision_ = 0;
  material_find_replace_modeling_selection_revision_ = 0;
  material_find_replace_modeling_object_map_revision_ = 0;
  material_find_replace_presentation_revision_ = 0;
  material_picker_presentation_revision_ = 0;
  tool_settings_presenter_.invalidate();
  standard_material_editor_presenter_.invalidate();
  status_presenter_.invalidate();
  status_selection_presenter_.invalidate();
  status_observed_command_revision_ = 0u;
  status_notification_.reset();
  ++status_notification_generation_;
}

void UISystem::refresh(const FrameTiming &frame_timing,
                       RenderFramePerformanceStats render_performance) {
  QDR_PROFILE_SCOPE("UI system refresh");
  if (shell_replacement_pending_) {
    return;
  }
  frame_timing_ = frame_timing;
  render_performance_ = render_performance;
  // Keep structural property/command presentation stable during a pointer drag.
  // Curve previews publish geometry each sample just like transform previews;
  // their final revisions are presented once the gesture ends. Inspector
  // transform fields have a separate retained path for live preview values.
  const NativeViewportToolPresentationSnapshot frame_native_tools =
      native_tools_ != nullptr
          ? native_tools_->presentation_snapshot()
          : NativeViewportToolPresentationSnapshot{};
  const bool edit_drag_active =
      (modeling_ != nullptr && modeling_->transform_drag_active()) ||
      frame_native_tools.curve_dragging;
  const auto *application =
      qobject_cast<QApplication *>(QCoreApplication::instance());
  const bool numeric_interaction_active =
      (application != nullptr && qt_numeric_drag_active(*application)) ||
      (tool_settings_panel_ != nullptr &&
       tool_settings_panel_->numeric_interaction_active()) ||
      (face_texture_panel_ != nullptr &&
       (face_texture_panel_->numeric_interaction_active() ||
        face_texture_panel_->interaction_active()));
  const ViewportPaneId frame_focused_pane = editor_ != nullptr
      ? editor_->viewports().focused_pane() : ViewportPaneId::Perspective;
  const std::uint64_t frame_pane_control_revision = editor_ != nullptr
      ? editor_->viewports().control_revision(frame_focused_pane) : 0u;
  const std::uint64_t frame_tool_settings_revision = editor_ != nullptr
      ? editor_->tools().settings_revision(frame_focused_pane) : 0u;
  const std::uint64_t frame_active_tool_revision =
      editor_ != nullptr ? editor_->tools().active_tool_revision() : 0u;
  const bool frame_operation_preview_active =
      modeling_ != nullptr && modeling_->operation_preview_active();
  const std::uint64_t frame_transform_space_revision =
      editor_ != nullptr ? editor_->viewports().transform_space_revision()
                         : 0u;
  {
    QDR_PROFILE_SCOPE("UI refresh project workspace");
    refresh_project_workspace(false);
  }
  refresh_asset_browser();
  validate_material_find_replace_context();
  const bool retained_material_surface_visible =
      panel_presented(UIPanelId::FaceTexture) ||
      (material_browser_window_ != nullptr &&
       material_browser_window_->isVisible()) ||
      (material_picker_window_ != nullptr &&
       material_picker_window_->isVisible()) ||
      (material_find_replace_dialog_ != nullptr &&
       material_find_replace_dialog_->isVisible());
  if (retained_material_surface_visible) {
    refresh_material_presentation_cache();
  }
  if (panel_presented(UIPanelId::FaceTexture) ||
      (material_browser_window_ != nullptr &&
       material_browser_window_->isVisible())) {
    // Revealing a dock and editing an already-dirty scene need no workspace
    // event. Publish both browser hosts after observing the project/catalog.
    refresh_material_browser_usage();
  }
  if (editor_ != nullptr) {
    if (!edit_drag_active && !numeric_interaction_active) {
      QDR_PROFILE_SCOPE("UI refresh retained toolbars");
      const bool owner_available =
          document_ != nullptr && modeling_ != nullptr;
      const ToolbarOwnerPresentationStamp owner_stamp = [&]() {
        QDR_PROFILE_SCOPE("UI toolbar query owner revisions");
        return ToolbarOwnerPresentationStamp{
            .owner_available = owner_available,
            .editor_mode_revision = editor_->mode().revision(),
            .scene_revision = owner_available ? document_->revision() : 0u,
            .asset_revision = assets_ != nullptr ? assets_->revision() : 0u,
            .scene_layer_revision =
                owner_available ? document_->layer_revision() : 0u,
            .scene_selection_revision =
                owner_available ? document_->selection_revision() : 0u,
            .modeling_content_revision =
                owner_available ? modeling_->content_revision() : 0u,
            .modeling_selection_revision =
                owner_available ? modeling_->selection_revision() : 0u,
        };
      }();
      const ToolbarToolPresentationStamp tool_stamp{
          .pane = frame_focused_pane,
          .pane_control_revision = frame_pane_control_revision,
          .active_tool_revision = frame_active_tool_revision,
          .availability_revision = editor_->tools().availability_revision(),
          .keybindings_revision = editor_->commands().keybindings_revision(),
          .preferred_primitive_shape =
              editor_->tools().preferred_primitive_shape(),
      };
      const ToolbarTransformPresentationStamp transform_stamp{
          .pane = frame_focused_pane,
          .transform_space_revision = frame_transform_space_revision,
      };
      const bool owner_state_changed =
          toolbar_owner_presenter_.needs_refresh(owner_stamp);
      const bool active_tool_revision_changed =
          toolbar_tool_presenter_.needs_refresh(tool_stamp);
      const bool transform_space_revision_changed =
          toolbar_transform_presenter_.needs_refresh(transform_stamp);
      if (owner_state_changed || active_tool_revision_changed ||
          transform_space_revision_changed) {
        const QuaderSelectionSummary selection = [&]() {
          QDR_PROFILE_SCOPE("UI toolbar query selection summary");
          return editor_->selection().summary();
        }();
        if (selection_toolbar_ != nullptr && modeling_ != nullptr &&
            (owner_state_changed || active_tool_revision_changed)) {
          QDR_PROFILE_SCOPE("UI synchronize selection toolbar");
          const QuaderSelectionMode active_mode =
              to_api_selection_mode(modeling_->selection_mode());
          selection_toolbar_->synchronize(*editor_, active_mode);
        }
        if (tool_toolbar_ != nullptr) {
          QDR_PROFILE_SCOPE("UI synchronize tool toolbar");
          synchronize_tool_toolbar(selection, owner_state_changed);
        }
        toolbar_owner_presenter_.mark_presented(owner_stamp);
        toolbar_tool_presenter_.mark_presented(tool_stamp);
        toolbar_transform_presenter_.mark_presented(transform_stamp);
      }
    }
    if (snapping_toolbar_ != nullptr) {
      QDR_PROFILE_SCOPE("UI synchronize snapping toolbar");
      snapping_toolbar_->synchronize(*editor_);
    }
    if (viewport_display_toolbar_ != nullptr) {
      QDR_PROFILE_SCOPE("UI synchronize viewport display toolbar");
      viewport_display_toolbar_->synchronize(*editor_);
    }
    if (texture_toolbar_ != nullptr) {
      QDR_PROFILE_SCOPE("UI synchronize texture toolbar");
      texture_toolbar_->synchronize(*editor_);
    }
    if (viewport_panel_ != nullptr) {
      viewport_panel_->refresh_control_actions(*editor_);
    }
  }
  if (viewport_panel_ != nullptr) {
    viewport_panel_->set_render_performance_stats(render_performance_);
  }
  if (!initialized_ || editor_ == nullptr || document_ == nullptr ||
      assets_ == nullptr || modeling_ == nullptr || selection_ == nullptr) {
    return;
  }

  UIInspectorPreviewContext preview{.document = *document_, .assets = *assets_};
  UIContext context{
      .intent = {.editor = *editor_},
      .scene =
          {
              .document = *document_,
              .assets = *assets_,
              .modeling = *modeling_,
              .selection = *selection_,
              .default_material = default_material_,
          },
      .inspector_preview = &preview,
      .preferences =
          {
              .ui = settings_,
              .viewport = viewport_settings_,
              .ui_settings_changed = &ui_settings_changed_,
          },
      .localization = localization_,
  };
  {
    QDR_PROFILE_SCOPE("UI refresh dock panels");
    const std::uint64_t localization_revision = localization_.revision();
    if (!edit_drag_active && panel_presented(UIPanelId::Outliner)) {
      const OutlinerPresentationStamp stamp{
          .editor_mode_revision = editor_->mode().revision(),
          .hierarchy_revision =
              document_->hierarchy_presentation_revision(),
          .light_property_revision = document_->light_property_revision(),
          .selection_revision = document_->selection_revision(),
          .instance_edit_revision =
              editor_->modeling().instance_edit_revision(),
          .localization_revision = localization_revision,
      };
      if (outliner_presenter_.needs_refresh(stamp)) {
        QDR_PROFILE_SCOPE("UI refresh outliner panel");
        outliner_panel_->refresh(context);
        outliner_presenter_.mark_presented(stamp);
      }
    }
    if (!edit_drag_active && panel_presented(UIPanelId::Layers)) {
      const LayersPresentationStamp stamp{
          .layer_revision = document_->layer_revision(),
          .selection_revision = document_->selection_revision(),
          .localization_revision = localization_revision,
      };
      if (layers_presenter_.needs_refresh(stamp)) {
        QDR_PROFILE_SCOPE("UI refresh layers panel");
        layer_panel_->refresh(context);
        layers_presenter_.mark_presented(stamp);
      }
    }
    if ((edit_drag_active || numeric_interaction_active) &&
        panel_presented(UIPanelId::Inspector)) {
      inspector_panel_->refresh_transform_preview(context);
    }
    if (!edit_drag_active && !numeric_interaction_active &&
        panel_presented(UIPanelId::Inspector)) {
      refresh_material_presentation_cache();
      const InspectorPresentationStamp stamp{
          .editor_mode_revision = editor_->mode().revision(),
          .viewport_defaults_revision = viewport_defaults_revision_,
          .scene_revision = document_->revision(),
          .modeling_content_revision = modeling_->content_revision(),
          .modeling_selection_revision = modeling_->selection_revision(),
          .material_presentation_revision = material_presentation_revision_,
          .localization_revision = localization_revision,
      };
      if (inspector_presenter_.needs_refresh(stamp)) {
        QDR_PROFILE_SCOPE("UI refresh inspector panel");
        refresh_inspector_material_presentation();
        inspector_panel_->refresh(context);
        inspector_presenter_.mark_presented(stamp);
      }
    }
    if (panel_presented(UIPanelId::Console)) {
      QDR_PROFILE_SCOPE("UI refresh console panel");
      console_panel_->refresh(context);
    }
  }
  const std::uint64_t settings_operation_serial =
      editor_ != nullptr
          ? editor_->tools().settings_presentation_serial(frame_focused_pane)
          : 0u;
  const auto *presented_settings = tool_settings_presenter_.presented_stamp();
  const bool settings_lifecycle_changed =
      presented_settings == nullptr ||
      presented_settings->pane != frame_focused_pane ||
      presented_settings->operation_serial != settings_operation_serial ||
      presented_settings->active_tool_revision != frame_active_tool_revision ||
      presented_settings->operation_preview_active !=
          frame_operation_preview_active ||
      presented_settings->curve_controls_editable !=
          frame_native_tools.curve_controls_editable;
  if (tool_settings_panel_ != nullptr &&
      (!numeric_interaction_active || settings_lifecycle_changed)) {
    const ToolSettingsPresentationStamp stamp{
        .pane = frame_focused_pane,
        .pane_control_revision = frame_pane_control_revision,
        .active_settings_revision = frame_tool_settings_revision,
        .operation_serial = settings_operation_serial,
        .active_tool_revision = frame_active_tool_revision,
        .operation_preview_active = frame_operation_preview_active,
        .curve_controls_editable = frame_native_tools.curve_controls_editable,
    };
    if (tool_settings_presenter_.needs_refresh(stamp)) {
      QDR_PROFILE_SCOPE("UI refresh viewport settings presentation");
      const auto binding_generation = runtime_binding_generation_;
      tool_settings_panel_->refresh();
      if (binding_generation != runtime_binding_generation_) {
        return;
      }
      tool_settings_presenter_.mark_presented(stamp);
    }
  }
  if (viewport_panel_ != nullptr && editor_ != nullptr) {
    const auto binding_generation = runtime_binding_generation_;
    viewport_panel_->refresh_settings_overlay(*editor_);
    if (binding_generation != runtime_binding_generation_) {
      return;
    }
  }
  if (panel_presented(UIPanelId::FaceTexture)) {
    // The panel gates settings, geometry and images by their owner revisions.
    face_texture_panel_->refresh();
    refresh_face_texture_material(face_texture_panel_->canvas_material_reference());
    refresh_face_texture_browser_selection();
  }

  {
    QDR_PROFILE_SCOPE("UI synchronize status bar");
    refresh_status_selection();
    refresh_status_bar();
  }

  if (localized_locale_ != localization_.current_locale() ||
      localized_revision_ != localization_.revision()) {
    QDR_PROFILE_SCOPE("UI synchronize localized presentation");
    localized_locale_ = localization_.current_locale();
    localized_revision_ = localization_.revision();
    project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
    status_presenter_.invalidate();
    refresh_localized_chrome();
    main_menu_->rebuild();
    viewport_panel_->set_localization(localization_);
    tool_settings_panel_->set_localization(localization_);
  }
  if (ui_settings_changed_) {
    ui_settings_changed_ = false;
    (void)save_settings();
  }
  {
    // Inspector, settings and localization above can all change child minima.
    // Decide reveal only after this frame's retained presentation is complete.
    QDR_PROFILE_SCOPE("UI advance startup presentation");
    advance_startup_presentation();
  }
}

void UISystem::process_viewport_input() {
  if (loading_presentation_active_ || home_visible_ || !initialized_ || viewport_panel_ == nullptr || editor_ == nullptr ||
      document_ == nullptr || assets_ == nullptr || modeling_ == nullptr ||
      native_tools_ == nullptr) {
    return;
  }
  viewport_panel_->process_input(*editor_, *document_, *assets_, *modeling_,
                                 default_material_, *native_tools_,
                                 point_placement_, frame_timing_.delta_seconds);
}

void UISystem::prepare_viewports(
    RenderSceneSink &render_bridge,
    RenderOverlaySnapshotCache &snapshot_cache) {
  if (home_visible_ || !initialized_ || editor_ == nullptr || document_ == nullptr ||
      assets_ == nullptr || modeling_ == nullptr || native_tools_ == nullptr) {
    return;
  }
  viewport_panel_->prepare_viewports(*editor_, render_bridge, snapshot_cache,
                                     *document_, *assets_, *modeling_,
                                     *native_tools_);
}

std::size_t UISystem::render_viewports() {
  return render_surfaces(true);
}

std::size_t UISystem::render_surfaces(bool render_scene_viewports) {
  if (render_system_ == nullptr) {
    return 0u;
  }
  const bool composed_scene = render_scene_viewports && !home_visible_ &&
                              render_system_->uses_composited_vulkan();
  if (!composed_scene) {
    render_system_->begin_presentation_frame();
  }
  std::size_t submitted = 0u;
  if (render_scene_viewports && !home_visible_ && viewport_panel_ != nullptr) {
    submitted += viewport_panel_->render_viewports(false);
  }
  if (material_preview_surface_ != nullptr &&
      standard_material_editor_window_ != nullptr &&
      standard_material_editor_window_->isVisible() &&
      material_preview_surface_->render_frame()) {
    ++submitted;
  }
  if (!composed_scene) {
    render_system_->complete_presentation_frame(submitted);
  }
  return submitted;
}

bool UISystem::needs_continuous_rendering() const {
  const bool scene_continuous =
      !home_visible_ && viewport_panel_ != nullptr &&
      viewport_panel_->needs_continuous_rendering();
  const bool preview_pending =
      material_preview_surface_ != nullptr &&
      standard_material_editor_window_ != nullptr &&
      standard_material_editor_window_->isVisible() &&
      !standard_material_editor_window_->isMinimized() &&
      material_preview_surface_->needs_rendering();
  const bool asset_preview_pending =
      ((!home_visible_ && panel_presented(UIPanelId::AssetBrowser)) ||
       (asset_thumbnail_picker_ && asset_thumbnail_picker_->isVisible())) &&
      asset_thumbnails_ != nullptr &&
      asset_thumbnails_->pending_count() != 0u;
  return loading_presentation_active_ || scene_continuous || preview_pending || asset_preview_pending;
}

void *UISystem::native_window_handle() const {
  if (main_window_ == nullptr) {
    return nullptr;
  }
  const WId id = main_window_->winId();
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));
}

bool UISystem::can_render_viewports() const {
  return !home_visible_ && main_window_ != nullptr && main_window_->isVisible() &&
         !main_window_->isMinimized() && main_window_->updatesEnabled() &&
         viewport_panel_ != nullptr &&
         !viewport_panel_->startup_presentation_blocked() &&
         !viewport_panel_->loading_presentation_blocked();
}

bool UISystem::save_settings() {
  if (settings_paths_.settings_file.empty()) {
    return false;
  }
  panels_.write_settings(settings_);
  if (settings_.save(settings_paths_.settings_file)) {
    return true;
  }
  LOG_WARN("Unable to save UI settings to {}",
           settings_paths_.settings_file.string());
  return false;
}

bool UISystem::save_viewport_settings() {
  ++viewport_defaults_revision_;
  apply_viewport_settings_to_workspace();
  if (settings_paths_.viewport_settings_file.empty()) {
    return false;
  }
  if (viewport_settings_.save(settings_paths_.viewport_settings_file)) {
    return true;
  }
  LOG_WARN("Unable to save viewport settings to {}",
           settings_paths_.viewport_settings_file.string());
  return false;
}

void UISystem::save_window_state() {
  if (main_window_ != nullptr &&
      !settings_paths_.qt_window_state_file.empty()) {
    std::string state_bytes = workspace_window_state_;
    if (!home_visible_ || state_bytes.empty()) {
      const QByteArray state = main_window_->saveState(kQtWindowStateVersion);
      state_bytes.assign(state.constData(),
                         static_cast<std::size_t>(state.size()));
    }
    (void)write_binary_file(
        settings_paths_.qt_window_state_file,
        QByteArray(state_bytes.data(),
                   static_cast<qsizetype>(state_bytes.size())));
  }
  save_auxiliary_window_state();
}

void UISystem::capture_workspace_window_state() {
  if (main_window_ == nullptr) {
    return;
  }
  const QByteArray state = main_window_->saveState(kQtWindowStateVersion);
  workspace_window_state_.assign(
      state.constData(), static_cast<std::size_t>(state.size()));
  selection_toolbar_workspace_visible_ =
      selection_toolbar_ != nullptr && !selection_toolbar_->isHidden();
  snapping_toolbar_workspace_visible_ =
      snapping_toolbar_ != nullptr && !snapping_toolbar_->isHidden();
  texture_toolbar_workspace_visible_ =
      texture_toolbar_ != nullptr && !texture_toolbar_->isHidden();
  tool_toolbar_workspace_visible_ =
      tool_toolbar_ != nullptr && !tool_toolbar_->isHidden();
}

void UISystem::create_docks() {
  panels_.set_visibility_changed_callback([this]() {
    if (initialized_) {
      (void)save_settings();
      synchronize_settings_surfaces(false);
    }
  });
  panels_.set_presentation_changed_callback([this](UIPanelId, bool presented) {
    if (presented) {
      project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
    }
  });
  outliner_panel_ = new OutlinerPanel(main_window_.get());
  inspector_panel_ = new InspectorPanel(main_window_.get());
  console_panel_ = new ConsolePanel(main_window_.get());
  console_panel_->set_command_handler([this](std::string_view command) {
    if (editor_ == nullptr || console_panel_ == nullptr) {
      return;
    }
    if (command != "/dump") {
      const auto localized = localization_.text(
          "ui.console.unknown_command", "Unknown command: %1");
      const QString message =
          QString::fromUtf8(localized.data(),
                            static_cast<qsizetype>(localized.size()))
              .arg(QString::fromUtf8(command.data(),
                                    static_cast<qsizetype>(command.size())));
      console_panel_->append_command_reply(spdlog::level::err,
                                           message.toUtf8().toStdString());
      return;
    }
    const auto result = editor_->commands().execute(
        quader_command::kDiagnosticsDumpSelection);
    if (!result.ok()) {
      console_panel_->append_command_reply(spdlog::level::err,
                                           result.error_message());
    }
  });
  auto *dump_timer = new QTimer(console_panel_);
  dump_timer->setInterval(200);
  QObject::connect(dump_timer, &QTimer::timeout, console_panel_, [this] {
    if (editor_ == nullptr || console_panel_ == nullptr ||
        !editor_->selection_diagnostics().pending()) {
      return;
    }
    if (auto reply = editor_->selection_diagnostics().take_completed();
        reply.has_value()) {
      console_panel_->append_command_reply(
          reply->ok() ? spdlog::level::info : spdlog::level::err,
          reply->ok() ? reply->value().summary : reply->error_message());
    }
  });
  dump_timer->start();
  asset_browser_ = new QtAssetBrowser(main_window_.get());
  asset_browser_->set_material_authoring_available(
      release_has_feature(ReleaseFeature::Materials));
  asset_browser_->set_action_callback(
      [this](QtAssetBrowserActionRequest request) {
        handle_asset_browser_action(std::move(request));
      });
  asset_browser_->set_presentation_state_changed_callback(
      [this]() { schedule_auxiliary_window_state_save(); });
  configure_asset_browser();
  tool_settings_panel_ = new QtToolSettingsPanel(main_window_.get());
  tool_settings_panel_->set_localization(localization_);
  viewport_panel_->set_settings_panel(tool_settings_panel_);

  QDockWidget &outliner = create_dock(
      UIPanelId::Outliner, *outliner_panel_, Qt::LeftDockWidgetArea);
  QDockWidget &inspector = create_dock(
      UIPanelId::Inspector, *inspector_panel_, Qt::RightDockWidgetArea);
  QDockWidget *layers = nullptr;
  if (release_has_feature(ReleaseFeature::Layers)) {
    layer_panel_ = new LayerPanel(main_window_.get());
    layers = &create_dock(
        UIPanelId::Layers, *layer_panel_, Qt::RightDockWidgetArea);
  }
  QDockWidget &console = create_dock(
      UIPanelId::Console, *console_panel_, Qt::BottomDockWidgetArea);
  QDockWidget &asset_browser = create_dock(
      UIPanelId::AssetBrowser, *asset_browser_, Qt::BottomDockWidgetArea);
  main_window_->setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
  main_window_->setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
  main_window_->tabifyDockWidget(&console, &asset_browser);
  if (layers != nullptr) {
    main_window_->splitDockWidget(&inspector, layers, Qt::Vertical);
  }
  const QtUiScaleMetrics &metrics = ui_scale_controller_->metrics();
  main_window_->resizeDocks({&outliner, &inspector},
                            {metrics.px(340), metrics.px(340)},
                            Qt::Horizontal);
  QList<QDockWidget *> vertical_docks{&inspector};
  QList<int> vertical_sizes{metrics.px(320)};
  if (release_has_feature(ReleaseFeature::UvEditing)) {
    face_texture_panel_ = new QtFaceTexturePanel(main_window_.get());
    face_texture_panel_->set_localization(localization_);
    QDockWidget &face_texture = create_dock(
        UIPanelId::FaceTexture, *face_texture_panel_, Qt::RightDockWidgetArea);
    main_window_->splitDockWidget(&inspector, &face_texture, Qt::Vertical);
    vertical_docks.push_back(&face_texture);
    vertical_sizes.push_back(metrics.px(320));
  }
  if (layers != nullptr) {
    vertical_docks.push_back(layers);
    vertical_sizes.push_back(metrics.px(180));
  }
  if (vertical_docks.size() > 1) {
    main_window_->resizeDocks(vertical_docks, vertical_sizes, Qt::Vertical);
  }
  inspector.raise();
  main_window_->resizeDocks({&console, &asset_browser},
                            {metrics.px(260), metrics.px(260)}, Qt::Vertical);
}

QDockWidget &UISystem::create_dock(UIPanelId id, QWidget &content,
                                   int default_area) {
  auto *dock = new QDockWidget(QString::fromUtf8(ui_panel_key(id)),
                               main_window_.get());
  dock->setObjectName(
      QStringLiteral("dock_%1").arg(QString::fromUtf8(ui_panel_stable_id(id))));
  dock->setAllowedAreas(Qt::AllDockWidgetAreas);
  dock->setFeatures(QDockWidget::DockWidgetClosable |
                    QDockWidget::DockWidgetMovable |
                    QDockWidget::DockWidgetFloatable);
  install_palette_aware_dock_title_presentation(*dock);
  apply_dock_content_minimum_width(content,
                                   ui_scale_controller_->metrics());
  dock->setWidget(&content);
  main_window_->addDockWidget(static_cast<Qt::DockWidgetArea>(default_area),
                              dock);
  panels_.add(id, *dock);
  QObject::connect(&content, &QWidget::windowTitleChanged, dock,
                   [dock](const QString &title) { dock->setWindowTitle(title); });
  return *dock;
}

void UISystem::restore_window_state() {
  auxiliary_window_state_ = QtAuxiliaryWindowState::load(
      settings_paths_.qt_auxiliary_window_state_file);
  QDockWidget *texture_dock = panels_.dock(UIPanelId::FaceTexture);
  const bool migrate_texture_layout =
      texture_dock != nullptr && !auxiliary_window_state_.face_texture_layout_migrated;
  if (migrate_texture_layout) {
    texture_dock->setObjectName(QStringLiteral("dock_active_material"));
  }
  if (main_window_ != nullptr &&
      !settings_paths_.qt_window_state_file.empty()) {
    const std::string bytes =
        read_binary_file(settings_paths_.qt_window_state_file);
    if (!bytes.empty()) {
      const QByteArray state(bytes.data(),
                             static_cast<qsizetype>(bytes.size()));
      (void)main_window_->restoreState(state, kQtWindowStateVersion);
    }
  }

  if (migrate_texture_layout) {
    texture_dock->setObjectName(QStringLiteral("dock_face_texture"));
    auxiliary_window_state_.face_texture_layout_migrated = true;
  }
  if (!auxiliary_window_state_.asset_browser_layout_migrated) {
    if (QDockWidget *browser = panels_.dock(UIPanelId::AssetBrowser)) {
      main_window_->setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
      main_window_->setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
      if (QDockWidget *console = panels_.dock(UIPanelId::Console);
          console != nullptr && !console->isFloating() &&
          main_window_->dockWidgetArea(console) == Qt::BottomDockWidgetArea) {
        main_window_->tabifyDockWidget(console, browser);
      }
      auxiliary_window_state_.asset_browser_layout_migrated = true;
    }
  }
  if (settings_dialog_ != nullptr &&
      !auxiliary_window_state_.settings_dialog_geometry.isEmpty()) {
    (void)settings_dialog_->restoreGeometry(
        auxiliary_window_state_.settings_dialog_geometry);
  }
  if (asset_browser_ != nullptr) {
    asset_browser_->restore_presentation_state(
        auxiliary_window_state_.asset_browser_grid_mode
            ? QtAssetBrowserViewMode::Grid : QtAssetBrowserViewMode::List,
        auxiliary_window_state_.asset_browser_tile_size,
        auxiliary_window_state_.asset_browser_sidebar_visible,
        auxiliary_window_state_.asset_browser_sidebar_width);
  }
  if (face_texture_panel_ != nullptr) {
    face_texture_panel_->browser().restore_presentation_state(
        auxiliary_window_state_.face_texture_browser_grid_mode
            ? QtMaterialBrowserViewMode::Grid : QtMaterialBrowserViewMode::List,
        auxiliary_window_state_.face_texture_browser_tile_size,
        auxiliary_window_state_.face_texture_browser_sidebar_visible,
        auxiliary_window_state_.face_texture_browser_sidebar_width);
    face_texture_panel_->restore_splitter_state(
        auxiliary_window_state_.face_texture_splitter_state);
  }
  if (standard_material_editor_window_ != nullptr) {
    if (!auxiliary_window_state_.standard_material_editor_geometry.isEmpty()) {
      (void)standard_material_editor_window_->restoreGeometry(
          auxiliary_window_state_.standard_material_editor_geometry);
    }
    const int preview_mesh = std::clamp(
        auxiliary_window_state_.standard_material_preview_mesh, 0, 4);
    const int preview_shading = std::clamp(
        auxiliary_window_state_.standard_material_preview_shading, 0, 2);
    standard_material_editor_window_->restore_presentation_state(
        static_cast<QtStandardMaterialPreviewMesh>(preview_mesh),
        static_cast<QtStandardMaterialPreviewShading>(preview_shading),
        auxiliary_window_state_.standard_material_preview_environment,
        auxiliary_window_state_.standard_material_preview_background);
    if (material_preview_surface_ != nullptr) {
      (void)material_preview_surface_->set_preview_mesh(
          preview_mesh_kind(
              standard_material_editor_window_->preview_mesh()));
      (void)material_preview_surface_->set_preview_shading(
          preview_shading_mode(
              standard_material_editor_window_->preview_shading()));
      (void)material_preview_surface_->set_preview_environment(
          standard_material_editor_window_->preview_environment()
              .toStdString());
      (void)material_preview_surface_->set_show_environment_background(
          standard_material_editor_window_
              ->preview_environment_background_visible());
    }
  }
}

void UISystem::begin_loading_presentation() {
  if (main_window_ == nullptr || loading_cover_ == nullptr) {
    return;
  }
  ++loading_operation_depth_;
  if (loading_operation_depth_ != 1u || shell_replacement_pending_) {
    return;
  }
  loading_previous_presentation_pending_ = loading_presentation_active_;
  prepare_startup_presentation();
  // Flush only this retained cover; never enter a nested input/event loop
  // while project owners are about to replace the current runtime.
  static_cast<QtLoadingCover *>(loading_cover_)->flush_presentation();
}

void UISystem::prepare_shell_replacement() {
  if (main_window_ == nullptr || shell_replacement_pending_) {
    return;
  }
  shell_replacement_pending_ = true;
  if (!home_visible_) {
    capture_workspace_window_state();
  }
  // The domain binding has already changed. Retire outgoing drafts without
  // consulting old owners before hiding the window delivers focus events.
  abandon_scene_backed_panels();
  if (viewport_panel_ != nullptr) {
    viewport_panel_->abandon_interactions();
    viewport_panel_->set_loading_presentation_blocked(true);
  }
  if (standard_material_editor_window_ != nullptr) {
    standard_material_editor_window_->abandon_interactions();
  }
  // App retires this root only after the initiating action returns. Keep its
  // device alive until all render borrowers have been released, without ever
  // mapping this window at the destination project's placement.
  main_window_->hide();
}

bool UISystem::loading_logo_presented() const {
  return loading_cover_ != nullptr &&
         static_cast<const QtLoadingCover *>(loading_cover_)->logo_presented();
}

void UISystem::end_loading_presentation(bool succeeded) {
  if (loading_operation_depth_ == 0u) {
    return;
  }
  --loading_operation_depth_;
  if (loading_operation_depth_ != 0u) {
    return;
  }
  if (shell_replacement_pending_) {
    loading_previous_presentation_pending_ = false;
    return;
  }
  const bool previous_presentation_pending = loading_previous_presentation_pending_;
  loading_previous_presentation_pending_ = false;
  if (!succeeded && !startup_presentation_pending_ && !previous_presentation_pending) {
    finish_startup_presentation();
    return;
  }
  refresh_project_workspace(true);
  startup_presentation_ = {};
  loading_geometry_stable_since_ = {};
  loading_geometry_signature_ = 0u;
  if (viewport_panel_ != nullptr) {
    viewport_panel_->reset_loading_frame_fence();
  }
  if (viewport_presentation_requested_) {
    viewport_presentation_requested_();
  }
}

void UISystem::prepare_startup_presentation() {
  if (main_window_ == nullptr || loading_cover_ == nullptr) {
    return;
  }
  if (!loading_presentation_active_) {
    loading_previous_focus_ = QApplication::focusWidget();
    static_cast<QtLoadingCover *>(loading_cover_)->begin_presentation();
  }
  loading_presentation_active_ = true;
  startup_presentation_ = {};
  loading_geometry_stable_since_ = {};
  loading_geometry_signature_ = 0u;
  if (viewport_panel_ != nullptr) {
    viewport_panel_->reset_loading_frame_fence();
    viewport_panel_->set_loading_presentation_blocked(true);
    if (startup_presentation_pending_ || home_visible_) {
      viewport_panel_->set_startup_presentation_blocked(true);
    }
  }
  loading_cover_->setGeometry(main_window_->rect());
  loading_cover_->show();
  loading_cover_->raise();
  static_cast<QtLoadingCover *>(loading_cover_)->set_input_blocked(true);
}

void UISystem::advance_startup_presentation() {
  if (!loading_presentation_active_ || loading_operation_depth_ != 0u ||
      main_window_ == nullptr || editor_ == nullptr) {
    return;
  }
  if (QLayout *layout = main_window_->layout()) {
    (void)layout->activate();
  }
  const QWindow *host_window = main_window_->windowHandle();
  void *const native_handle = native_window_handle();
  const PlatformWindowClientExtent native_client =
      platform_window_client_extent(native_handle);
  const qreal pixel_scale = main_window_->devicePixelRatioF();
  const int retained_client_width_pixels =
      static_cast<int>(std::lround(main_window_->width() * pixel_scale));
  const int retained_client_height_pixels =
      static_cast<int>(std::lround(main_window_->height() * pixel_scale));
  // Title-bar actions remain available while covered. Follow the current
  // requested state so restore/maximize cannot strand an earlier startup latch.
  const bool requested_maximized =
      main_window_->windowState().testFlag(Qt::WindowMaximized);
  const bool native_state_ready =
      requested_maximized == platform_window_is_maximized(native_handle);
  const bool native_client_ready = qt_startup_client_extents_match(
      retained_client_width_pixels, retained_client_height_pixels,
      native_client.width, native_client.height);
  const bool layout_ready = startup_layout_ready();
  const bool hard_invariants_ready =
      host_window != nullptr && host_window->isExposed() &&
      !main_window_->isMinimized() && native_state_ready &&
      native_client_ready && layout_ready;
  const std::uint64_t geometry_signature = hard_invariants_ready
      ? startup_layout_geometry_signature(native_client.width, native_client.height)
      : 0u;
  const auto now = std::chrono::steady_clock::now();
  if (geometry_signature == 0u || loading_geometry_signature_ != geometry_signature) {
    loading_geometry_signature_ = geometry_signature;
    loading_geometry_stable_since_ = now;
  }
  if (startup_presentation_.presentation_ready &&
      startup_presentation_.observed_geometry_signature != geometry_signature) {
    startup_presentation_ = {};
  }
  const QtStartupPresentationAction action = qt_startup_presentation_action(
      startup_presentation_, hard_invariants_ready, geometry_signature);
  if (action != QtStartupPresentationAction::WaitForStableGeometry &&
      viewport_panel_ != nullptr &&
      (viewport_panel_->startup_presentation_blocked() ||
       viewport_panel_->loading_presentation_blocked())) {
    // Expose the panes under the cover so their first real GPU submission can
    // join the same root composition. The bootstrap anchor remains alive.
    viewport_panel_->set_startup_presentation_blocked(false);
    viewport_panel_->set_loading_presentation_blocked(false);
    loading_cover_->raise();
    return;
  }
  const bool scene_frame_ready = home_visible_ ||
      !editor_->scene_documents().has_active_scene() ||
      (viewport_panel_ != nullptr && viewport_panel_->loading_frame_presented());
  constexpr auto kSettledLayoutDuration = std::chrono::milliseconds(100);
  const bool geometry_settled = hard_invariants_ready &&
      now - loading_geometry_stable_since_ >= kSettledLayoutDuration;
  if (action == QtStartupPresentationAction::PresentationReady && scene_frame_ready &&
      geometry_settled &&
      static_cast<QtLoadingCover *>(loading_cover_)->minimum_duration_elapsed()) {
    finish_startup_presentation();
  }
}

void UISystem::finish_startup_presentation() {
  if (main_window_ == nullptr || !loading_presentation_active_) {
    return;
  }
  if (viewport_panel_ != nullptr) {
    viewport_panel_->set_startup_presentation_blocked(false);
    viewport_panel_->set_loading_presentation_blocked(false);
  }
  loading_presentation_active_ = false;
  startup_presentation_pending_ = false;
  static_cast<QtLoadingCover *>(loading_cover_)->set_input_blocked(false);
  loading_cover_->hide();
  if (loading_previous_focus_ != nullptr && loading_previous_focus_->isVisible() &&
      loading_previous_focus_->isEnabled()) {
    loading_previous_focus_->setFocus(Qt::OtherFocusReason);
  }
  loading_previous_focus_.clear();
  main_window_->update();
  schedule_retained_message_box_prewarm();
  if (auxiliary_window_state_.settings_dialog_visible &&
      settings_dialog_ != nullptr && !settings_dialog_->isVisible()) {
    show_settings_dialog();
  }
}

bool UISystem::startup_layout_ready() const {
  if (main_window_ == nullptr || main_window_->centralWidget() == nullptr ||
      main_window_->menuBar() == nullptr ||
      main_window_->statusBar() == nullptr) {
    return false;
  }
  const auto geometry_ready = [](const QWidget *widget) {
    return widget != nullptr && widget->geometry().isValid();
  };
  if (!main_window_->contentsRect().isValid() ||
      !geometry_ready(main_window_->centralWidget()) ||
      (main_window_->menuBar()->isVisible() && !geometry_ready(main_window_->menuBar())) ||
      (main_window_->statusBar()->isVisible() && !geometry_ready(main_window_->statusBar()))) {
    return false;
  }

  for (UIPanelId id : kUiDockPanelIds) {
    const QDockWidget *dock = panels_.dock(id);
    if (dock != nullptr && dock->isVisible() && !geometry_ready(dock)) {
      return false;
    }
  }
  for (const QToolBar *toolbar :
       {static_cast<const QToolBar *>(selection_toolbar_),
        static_cast<const QToolBar *>(snapping_toolbar_),
        static_cast<const QToolBar *>(texture_toolbar_),
        static_cast<const QToolBar *>(tool_toolbar_)}) {
    if (toolbar != nullptr && toolbar->isVisible() &&
        !geometry_ready(toolbar)) {
      return false;
    }
  }
  return true;
}

std::uint64_t UISystem::startup_layout_geometry_signature(
    int native_client_width_pixels,
    int native_client_height_pixels) const {
  if (main_window_ == nullptr) {
    return 0u;
  }
  std::uint64_t signature = 1469598103934665603ull;
  const auto mix = [&signature](int value) {
    signature ^= static_cast<std::uint32_t>(value);
    signature *= 1099511628211ull;
  };
  const qreal pixel_scale = main_window_->devicePixelRatioF();
  const auto mix_rect = [&mix, pixel_scale](const QRect &rect) {
    mix(static_cast<int>(std::lround(rect.x() * pixel_scale)));
    mix(static_cast<int>(std::lround(rect.y() * pixel_scale)));
    mix(static_cast<int>(std::lround(rect.width() * pixel_scale)));
    mix(static_cast<int>(std::lround(rect.height() * pixel_scale)));
  };
  // Wrapping controls and scroll content can move below unchanged page/dock
  // rectangles. This bounded retained traversal runs only while covered.
  const auto mix_children = [&](const auto &self, const QWidget &parent) -> void {
    for (QObject *child : parent.children()) {
      const auto *widget = qobject_cast<const QWidget *>(child);
      if (widget == nullptr || widget->isWindow() || !widget->isVisibleTo(&parent)) {
        continue;
      }
      mix_rect(widget->geometry());
      if (widget->layout() != nullptr) {
        mix_rect(widget->layout()->geometry());
      }
      self(self, *widget);
    }
  };

  mix(static_cast<int>(std::lround(pixel_scale * 1000.0)));
  mix(native_client_width_pixels);
  mix(native_client_height_pixels);
  mix_rect(main_window_->rect());
  mix_rect(main_window_->contentsRect());
  if (const QWidget *central = main_window_->centralWidget()) {
    mix_rect(central->geometry());
  }
  if (const QWidget *page = central_stack_ != nullptr ? central_stack_->currentWidget()
                                                    : nullptr) {
    mix_rect(page->geometry());
    if (page->layout() != nullptr) {
      mix_rect(page->layout()->geometry());
    }
    mix_children(mix_children, *page);
  }
  mix_rect(main_window_->menuBar()->geometry());
  mix_rect(main_window_->statusBar()->geometry());
  for (UIPanelId id : kUiDockPanelIds) {
    mix(panels_.is_visible(id) ? 1 : 0);
    if (const QDockWidget *dock = panels_.dock(id)) {
      mix(dock->isFloating() ? 1 : 0);
      mix_rect(dock->geometry());
      if (dock->isVisible()) {
        mix_children(mix_children, *dock);
      }
    }
  }
  const auto mix_toolbar = [&mix, &mix_rect](const QToolBar *toolbar) {
    mix(toolbar != nullptr && toolbar->isVisible() ? 1 : 0);
    if (toolbar != nullptr) {
      mix(toolbar->isFloating() ? 1 : 0);
      mix_rect(toolbar->geometry());
    }
  };
  mix_toolbar(selection_toolbar_);
  mix_toolbar(snapping_toolbar_);
  mix_toolbar(texture_toolbar_);
  mix_toolbar(tool_toolbar_);
  return signature;
}

void UISystem::save_auxiliary_window_state() {
  if (settings_paths_.qt_auxiliary_window_state_file.empty()) {
    return;
  }
  if (auxiliary_window_save_timer_ != nullptr &&
      auxiliary_window_save_timer_->isActive()) {
    auxiliary_window_save_timer_->stop();
  }
  if (settings_dialog_ != nullptr) {
    auxiliary_window_state_.settings_dialog_geometry =
        settings_dialog_->saveGeometry();
  }
  if (asset_browser_ != nullptr) {
    auxiliary_window_state_.asset_browser_grid_mode =
        asset_browser_->view_mode() == QtAssetBrowserViewMode::Grid;
    auxiliary_window_state_.asset_browser_tile_size = asset_browser_->tile_size();
    auxiliary_window_state_.asset_browser_sidebar_visible = asset_browser_->sidebar_visible();
    auxiliary_window_state_.asset_browser_sidebar_width = asset_browser_->sidebar_width();
  }
  if (face_texture_panel_ != nullptr) {
    const auto &browser = face_texture_panel_->browser();
    auxiliary_window_state_.face_texture_browser_grid_mode =
        browser.view_mode() == QtMaterialBrowserViewMode::Grid;
    auxiliary_window_state_.face_texture_browser_tile_size = browser.tile_size();
    auxiliary_window_state_.face_texture_browser_sidebar_visible = browser.sidebar_visible();
    auxiliary_window_state_.face_texture_browser_sidebar_width = browser.sidebar_width();
    auxiliary_window_state_.face_texture_splitter_state = face_texture_panel_->save_splitter_state();
  }
  if (standard_material_editor_window_ != nullptr) {
    auxiliary_window_state_.standard_material_editor_geometry =
        standard_material_editor_window_->saveGeometry();
    auxiliary_window_state_.standard_material_preview_mesh =
        static_cast<int>(standard_material_editor_window_->preview_mesh());
    auxiliary_window_state_.standard_material_preview_shading =
        static_cast<int>(
            standard_material_editor_window_->preview_shading());
    auxiliary_window_state_.standard_material_preview_environment =
        standard_material_editor_window_->preview_environment();
    auxiliary_window_state_.standard_material_preview_background =
        standard_material_editor_window_
            ->preview_environment_background_visible();
  }
  (void)auxiliary_window_state_.save(
      settings_paths_.qt_auxiliary_window_state_file);
}

void UISystem::bind_project_workspace_events(QuaderEditorApi &editor) {
  if (project_workspace_events_editor_ == &editor) {
    return;
  }
  clear_project_workspace_events();
  project_workspace_refresh_signal_ =
      std::make_shared<std::atomic_bool>(true);
  material_presentation_event_generation_signal_ =
      std::make_shared<std::atomic_uint64_t>(1u);
  material_presentation_observed_event_generation_ = 0;
  project_workspace_events_editor_ = &editor;
  constexpr std::array<std::string_view, 11> kWorkspaceEvents{
      quader_event::kProjectOpened,
      quader_event::kProjectClosed,
      quader_event::kProjectAssetsChanged,
      quader_event::kSceneOpened,
      quader_event::kActiveSceneChanged,
      quader_event::kSceneDirtyChanged,
      quader_event::kSceneSaved,
      quader_event::kStandardMaterialOpened,
      quader_event::kStandardMaterialDirtyChanged,
      quader_event::kStandardMaterialSaved,
      quader_event::kStandardMaterialClosed,
  };
  constexpr std::array<std::string_view, 7> kMaterialPresentationEvents{
      quader_event::kProjectOpened,
      quader_event::kProjectClosed,
      quader_event::kProjectAssetsChanged,
      quader_event::kSceneOpened,
      quader_event::kActiveSceneChanged,
      quader_event::kStandardMaterialSaved,
      quader_event::kSceneClosed,
  };
  project_workspace_event_subscriptions_.reserve(kWorkspaceEvents.size() +
                                                  1u);
  const std::shared_ptr<std::atomic_bool> refresh_signal =
      project_workspace_refresh_signal_;
  const std::shared_ptr<std::atomic_uint64_t> material_signal =
      material_presentation_event_generation_signal_;
  const std::shared_ptr<std::atomic_uint64_t> browser_project_signal =
      material_browser_project_generation_signal_;
  for (const std::string_view event_name : kWorkspaceEvents) {
    const bool changes_project_session = event_name == quader_event::kProjectOpened ||
        event_name == quader_event::kProjectClosed;
    const bool affects_material_presentation =
        std::ranges::find(kMaterialPresentationEvents, event_name) !=
        kMaterialPresentationEvents.end();
    QuaderEventSubscription subscription =
        editor.events().subscribe(
            event_name,
            [refresh_signal, material_signal, browser_project_signal,
             affects_material_presentation, changes_project_session](const QuaderEvent &) {
              refresh_signal->store(true, std::memory_order_relaxed);
              if (changes_project_session) {
                browser_project_signal->fetch_add(1u, std::memory_order_release);
              }
              if (affects_material_presentation) {
                material_signal->fetch_add(1u,
                                           std::memory_order_release);
              }
            });
    if (subscription.valid()) {
      project_workspace_event_subscriptions_.push_back(subscription);
    }
  }
  QuaderEventSubscription scene_closed =
      editor.events().subscribe(
          quader_event::kSceneClosed,
          [refresh_signal, material_signal](const QuaderEvent &) {
            refresh_signal->store(true, std::memory_order_relaxed);
            material_signal->fetch_add(1u, std::memory_order_release);
          });
  if (scene_closed.valid()) {
    project_workspace_event_subscriptions_.push_back(scene_closed);
  }
  project_workspace_refresh_signal_->store(true, std::memory_order_relaxed);
}

void UISystem::clear_project_workspace_events() {
  if (project_workspace_events_editor_ != nullptr) {
    for (const QuaderEventSubscription subscription :
         project_workspace_event_subscriptions_) {
      (void)project_workspace_events_editor_->events().unsubscribe(
          subscription);
    }
  }
  project_workspace_event_subscriptions_.clear();
  project_workspace_events_editor_ = nullptr;
  project_workspace_refresh_signal_ =
      std::make_shared<std::atomic_bool>(false);
  material_presentation_event_generation_signal_ =
      std::make_shared<std::atomic_uint64_t>(0u);
  material_presentation_observed_event_generation_ = 0;
}

void UISystem::schedule_auxiliary_window_state_save() {
  if (auxiliary_window_save_timer_ != nullptr) {
    auxiliary_window_save_timer_->start();
  }
}

void UISystem::refresh_status_selection() {
  if (status_selection_ == nullptr || editor_ == nullptr ||
      document_ == nullptr || modeling_ == nullptr) {
    return;
  }
  const StatusSelectionPresentationStamp stamp{
      .editor_mode_revision = editor_->mode().revision(),
      .scene_revision = document_->revision(),
      .scene_selection_revision = document_->selection_revision(),
      .modeling_content_revision = modeling_->content_revision(),
      .modeling_selection_revision = modeling_->selection_revision(),
      .localization_revision = localization_.revision(),
  };
  if (!status_selection_presenter_.needs_refresh(stamp)) {
    return;
  }

  QDR_PROFILE_SCOPE("UI status selection count and presentation");
  const QuaderSelectionSummary selection = editor_->selection().summary();
  const ModelingSelectionMode mode = modeling_->selection_mode();
  std::size_t count = 0u;
  std::string_view singular_key;
  std::string_view plural_key;
  std::string_view singular_fallback;
  std::string_view plural_fallback;
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    count = selection.selected_vertex_count;
    singular_key = "ui.status.selection.vertex.one";
    plural_key = "ui.status.selection.vertex.other";
    singular_fallback = "%1 Vertex Selected";
    plural_fallback = "%1 Vertices Selected";
    break;
  case ModelingSelectionMode::Edge:
    count = selection.selected_edge_count;
    singular_key = "ui.status.selection.edge.one";
    plural_key = "ui.status.selection.edge.other";
    singular_fallback = "%1 Edge Selected";
    plural_fallback = "%1 Edges Selected";
    break;
  case ModelingSelectionMode::Face:
    count = selection.selected_face_count;
    singular_key = "ui.status.selection.face.one";
    plural_key = "ui.status.selection.face.other";
    singular_fallback = "%1 Face Selected";
    plural_fallback = "%1 Faces Selected";
    break;
  case ModelingSelectionMode::Object:
    count = document_->selected_entities().size();
    singular_key = "ui.status.selection.mesh.one";
    plural_key = "ui.status.selection.mesh.other";
    singular_fallback = "%1 Mesh Selected";
    plural_fallback = "%1 Meshes Selected";
    break;
  case ModelingSelectionMode::Group:
    for (const EntityId entity : document_->selected_entities()) {
      if (document_->kind(entity) == EntityKind::Empty) {
        ++count;
      }
    }
    singular_key = "ui.status.selection.group.one";
    plural_key = "ui.status.selection.group.other";
    singular_fallback = "%1 Group Selected";
    plural_fallback = "%1 Groups Selected";
    break;
  }
  const QString text =
      QString::fromStdString(localization_.text(
          count == 1u ? singular_key : plural_key,
          count == 1u ? singular_fallback : plural_fallback))
          .arg(static_cast<qulonglong>(count));
  if (status_selection_->text() != text) {
    status_selection_->setText(text);
    status_selection_->setAccessibleName(text);
  }
  status_selection_presenter_.mark_presented(stamp);
}

void UISystem::refresh_status_bar() {
  QDR_PROFILE_SCOPE("UI status bar refresh");
  if (status_message_ == nullptr || editor_ == nullptr || modeling_ == nullptr) {
    return;
  }
  if (operation_input_hints_ != nullptr) {
    QuaderOperationInputContext context{.viewport_available = false};
    if (!home_visible_ && viewport_panel_ != nullptr && viewport_panel_->isVisible()) {
      context = viewport_panel_->operation_input_context();
    }
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    const auto local = application != nullptr
                           ? qt_contextual_input_hints_state(*application)
                           : QtContextualInputHintState{};
    const bool viewport_capture = context.viewport_available &&
        (context.navigation != QuaderInputNavigation::None || context.pointer_dragging ||
         context.selection_gesture != QuaderInputSelectionGesture::None ||
         context.point_light_range_dragging);
    if (local.overrides_viewport &&
        (local.blocks_viewport || local.active || !viewport_capture)) {
      operation_input_hints_->refresh_snapshot(
          qt_contextual_input_hints(*application), local.revision,
          local.source_owner, editor_->commands(), localization_);
    } else {
      operation_input_hints_->refresh(editor_->tools(), editor_->commands(),
                                      localization_, context);
    }
  }
  const auto sync_message_visibility = [this]() {
    const bool has_message = !status_message_->text().isEmpty();
    const bool has_hints = operation_input_hints_ != nullptr &&
                           !operation_input_hints_->hints().empty();
    if (operation_input_hints_ != nullptr &&
        operation_input_hints_->isHidden() == (has_hints && !has_message)) {
      operation_input_hints_->setVisible(has_hints && !has_message);
    }
    if (status_message_->isHidden() == has_message) {
      status_message_->setVisible(has_message);
    }
  };
  sync_message_visibility();
  const std::uint64_t command_revision = [&]() {
    QDR_PROFILE_SCOPE("UI status bar query command status revision");
    return editor_->commands().last_status_revision();
  }();
  if (status_notification_.has_value() &&
      command_revision != status_notification_command_revision_) {
    status_notification_.reset();
    ++status_notification_generation_;
  }
  const StatusPresentationStamp stamp{
      .command_revision = command_revision,
      .localization_revision = localization_.revision(),
  };
  if (!status_presenter_.needs_refresh(stamp)) {
    return;
  }

  {
    QDR_PROFILE_SCOPE("UI status bar present message");
    QuaderNotification notification;
    if (status_notification_.has_value()) {
      notification = *status_notification_;
    } else if (auto result = editor_->commands().last_status(); result.has_value()) {
      if (!result->ok()) {
        notification = {.level = QuaderNotificationLevel::Error,
                        .message = std::move(result->message)};
      } else if (result->message == mesh_arrangement_grid_alignment_warning()) {
        notification = {.level = QuaderNotificationLevel::Warning,
                        .message = std::move(result->message)};
      }
    }
    const bool error = notification.level == QuaderNotificationLevel::Error;
    const QString message = QString::fromStdString(
        status_bar_message(localization_, notification));
    const bool new_command =
        status_observed_command_revision_ != command_revision;
    status_observed_command_revision_ = command_revision;
    status_message_->present(message, error, new_command && error);
    if (!message.isEmpty() &&
        (new_command || !status_message_->dismissal_active())) {
      restart_status_message_dismissal();
    }
    sync_message_visibility();
  }
  status_presenter_.mark_presented(stamp);
}

void UISystem::present_status_result(const QuaderApiStatus &result) {
  present_status_notification(
      {.level = result.ok() ? QuaderNotificationLevel::Info
                           : QuaderNotificationLevel::Error,
       .message = result.ok() ? std::string{} : result.message});
}

void UISystem::present_status_notification(QuaderNotification notification) {
  if (status_message_ == nullptr) return;
  const bool informational = notification.level == QuaderNotificationLevel::Info;
  if (informational) {
    // Retain a silent success until the next command, so a dismissed command
    // failure cannot reappear after a direct edit that did not change its revision.
    notification.message.clear();
  }
  status_notification_ = std::move(notification);
  status_notification_command_revision_ =
      editor_ != nullptr ? editor_->commands().last_status_revision() : 0u;
  status_observed_command_revision_ = status_notification_command_revision_;
  ++status_notification_generation_;
  const QString message = QString::fromStdString(
      status_bar_message(localization_, *status_notification_));
  const bool error = status_notification_->level == QuaderNotificationLevel::Error;
  status_message_->present(message, error, error);
  if (operation_input_hints_ != nullptr) {
    operation_input_hints_->setVisible(
        message.isEmpty() && !operation_input_hints_->hints().empty());
  }
  status_message_->setVisible(!message.isEmpty());
  status_presenter_.invalidate();
  if (!message.isEmpty()) {
    restart_status_message_dismissal();
  }
}

void UISystem::restart_status_message_dismissal() {
  const auto generation = ++status_notification_generation_;
  const auto command_revision =
      editor_ != nullptr ? editor_->commands().last_status_revision() : 0u;
  status_message_->restart_dismissal([this, generation, command_revision]() {
    if (status_notification_generation_ != generation || editor_ == nullptr) {
      return;
    }
    if (editor_->commands().last_status_revision() != command_revision) {
      // A command may have finished before the next UI frame. Present its
      // result instead of dismissing it with the previous message's timer.
      refresh_status_bar();
      return;
    }
    status_notification_ = QuaderNotification{};
    status_notification_command_revision_ = command_revision;
    ++status_notification_generation_;
    status_presenter_.invalidate();
    refresh_status_bar();
  });
}

void UISystem::present_lifecycle_result(const QuaderApiStatus &result) {
  QDR_PROFILE_SCOPE("UI present lifecycle result");
  const bool restored_scene_warning =
      result.ok() && result.message.find("restored scenes were skipped") !=
                         std::string::npos;
  if (restored_scene_warning) {
    present_status_notification({.level = QuaderNotificationLevel::Warning,
                                 .message = result.message});
  } else {
    present_status_result(result);
  }
  if (result.ok() && !restored_scene_warning) {
    return;
  }

  if (lifecycle_message_box_ == nullptr) {
    return;
  }
  (void)lifecycle_message_box_->present(QtRetainedMessageBoxPresentation{
      .icon = QMessageBox::Warning,
      .object_name =
          restored_scene_warning
              ? QStringLiteral("project_open_warning_message_box")
              : QStringLiteral("lifecycle_error_message_box"),
      .title = QString::fromStdString(localization_.text(
          restored_scene_warning ? "ui.project.open_warning.title"
                                 : "ui.lifecycle.error.title",
          restored_scene_warning ? "Project Opened with Warnings"
                                 : "Quader Operation Failed")),
      .text = QString::fromStdString(
          result.message.empty()
              ? localization_.text("ui.status.command_failed",
                                   "Command failed.")
              : localized_release_message(localization_, result.message)),
      .buttons =
          {{
              .button = QtRetainedMessageBoxButton::Primary,
              .object_name = QStringLiteral("lifecycleMessageBox.ok"),
              .label = QString::fromStdString(
                  localization_.text("ui.action.ok", "OK")),
              .role = QMessageBox::AcceptRole,
          }},
      .default_button = QtRetainedMessageBoxButton::Primary,
      .escape_button = QtRetainedMessageBoxButton::Primary,
      .close_button = QtRetainedMessageBoxButton::Primary,
  });
}

void UISystem::present_standard_material_error(std::string_view message) {
  if (message.empty()) {
    return;
  }
  present_status_result({.ok = false, .message = std::string(message)});
  if (standard_material_error_message_box_ == nullptr) {
    return;
  }
  (void)standard_material_error_message_box_->present(
      QtRetainedMessageBoxPresentation{
          .icon = QMessageBox::Warning,
          .object_name =
              QStringLiteral("standard_material_error_message_box"),
          .title = QString::fromStdString(localization_.text(
              "ui.standard_material_editor.error_title",
              "Standard Material Operation Failed")),
          .text = QString::fromUtf8(
              message.data(), static_cast<qsizetype>(message.size())),
          .buttons =
              {{
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name =
                      QStringLiteral("standardMaterialErrorMessageBox.ok"),
                  .label = QString::fromStdString(
                      localization_.text("ui.action.ok", "OK")),
                  .role = QMessageBox::AcceptRole,
              }},
          .default_button = QtRetainedMessageBoxButton::Primary,
          .escape_button = QtRetainedMessageBoxButton::Primary,
          .close_button = QtRetainedMessageBoxButton::Primary,
      });
}

void UISystem::refresh_standard_material_editor() {
  if (standard_material_editor_window_ == nullptr) {
    return;
  }
  const StandardMaterialEditorPresentationStamp empty_stamp{};
  if (editor_ == nullptr || !editor_->project().has_project()) {
    if (!standard_material_editor_presenter_.needs_refresh(empty_stamp)) {
      return;
    }
    standard_material_preview_image_cache_.clear();
    standard_material_editor_window_->clear_document();
    standard_material_editor_window_->hide();
    standard_material_editor_presenter_.mark_presented(empty_stamp);
    return;
  }

  const QuaderApiResult<QuaderStandardMaterialDocumentRevision> revision =
      editor_->standard_material_documents().current_revision();
  if (!revision.ok) {
    if (!standard_material_editor_presenter_.needs_refresh(empty_stamp)) {
      return;
    }
    standard_material_preview_image_cache_.clear();
    if (standard_material_editor_window_->has_document()) {
      standard_material_editor_window_->clear_document();
      standard_material_editor_window_->hide();
    }
    standard_material_editor_presenter_.mark_presented(empty_stamp);
    return;
  }

  const QuaderStandardMaterialDocumentRevision &source_revision =
      revision.value();
  const StandardMaterialEditorPresentationStamp stamp{
      .session_id = source_revision.session_id,
      .asset_id = source_revision.id,
      .draft_revision = source_revision.draft_revision,
      .clean_revision = source_revision.clean_revision,
      .dirty = source_revision.dirty,
      .has_document = true,
  };
  if (!standard_material_editor_presenter_.needs_refresh(stamp)) {
    return;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> current =
      editor_->standard_material_documents().current();
  if (!current.ok) {
    return;
  }
  const QuaderStandardMaterialDocumentInfo &source = current.value();
  QtStandardMaterialEditorDocument document{
      .stable_id = QString::fromStdString(std::string(source.id.str())),
      .session_generation = source.session_id.generation(),
      .name = QString::fromStdString(source.name),
      .project_relative_path =
          source.path.has_value()
              ? QString::fromStdString(source.path->portable_string())
              : QString{},
      .albedo = QColor::fromRgbF(source.material.base_color[0],
                                 source.material.base_color[1],
                                 source.material.base_color[2]),
      .roughness = source.material.roughness,
      .metallic = source.material.metallic,
      .normal_strength = source.material.normal_strength,
      .direct_x_normal = source.material.normal_convention ==
                         QuaderNormalMapConvention::DirectX,
      .diffuse_model = source.material.diffuse_model == QuaderMaterialDiffuseModel::Lambert
                           ? QtStandardMaterialDiffuseModel::Lambert
                           : QtStandardMaterialDiffuseModel::Burley,
      .texture_transform =
          QtStandardMaterialTextureTransform{
              .center_u = source.material.texture_transform.center[0],
              .center_v = source.material.texture_transform.center[1],
              .scale_u = source.material.texture_transform.scale[0],
              .scale_v = source.material.texture_transform.scale[1],
              .offset_u = source.material.texture_transform.offset[0],
              .offset_v = source.material.texture_transform.offset[1],
              .rotation_degrees =
                  source.material.texture_transform.rotation_degrees,
          },
      .dirty = source.dirty,
  };

  for (std::size_t index = 0u; index < kMaterialTextureSlots.size(); ++index) {
    QtStandardMaterialTexturePresentation &target = document.textures[index];
    if (source.pending_texture_sources[index].has_value()) {
      const std::filesystem::path &pending =
          *source.pending_texture_sources[index];
      target.project_relative_path =
          QString::fromStdWString(pending.filename().wstring());
      target.diagnostic = QString::fromStdWString(pending.wstring());
      target.absolute_path = QString::fromStdWString(pending.wstring());
      continue;
    }
    const std::optional<QuaderStandardMaterialTextureReference> *saved =
        source.material.texture(kMaterialTextureSlots[index]);
    if (saved != nullptr && saved->has_value()) {
      target.project_relative_path =
          QString::fromStdString((*saved)->project_relative_path);
      const std::filesystem::path absolute =
          editor_->project().root() /
          std::filesystem::path((*saved)->project_relative_path);
      target.absolute_path = QString::fromStdWString(absolute.wstring());
    }
  }
  const QuaderApiResult<QuaderStandardMaterialPreviewInfo> preview =
      editor_->standard_material_documents().preview();
  std::optional<StandardMaterialPreviewSnapshot> render_snapshot;
  if (material_preview_surface_ != nullptr) {
    render_snapshot = standard_material_preview_render_snapshot(
        source, preview.ok() ? &preview.value() : nullptr);
  }

  if (!preview.ok()) {
    standard_material_preview_image_cache_.clear();
  } else {
    const std::string material_identity(source.id.str());
    for (std::size_t index = 0u; index < preview.value().textures.size();
         ++index) {
      if (!preview.value().texture_diagnostics[index].empty()) {
        if (!document.status_message.isEmpty()) {
          document.status_message += QStringLiteral("; ");
        }
        document.status_message += QString::fromStdString(
            preview.value().texture_diagnostics[index]);
      }
      if (!preview.value().textures[index].has_value()) {
        standard_material_preview_image_cache_.clear_slot(index);
        continue;
      }
      const QuaderStandardMaterialPreviewTexture &texture =
          *preview.value().textures[index];
      QtStandardMaterialTexturePresentation &presentation =
          document.textures[index];
      presentation.content_revision = texture.content_revision;
      presentation.source_has_alpha = texture.has_alpha_channel;
      if (texture.id.has_value()) {
        const std::string_view texture_id = texture.id->str();
        presentation.stable_texture_id = QString::fromUtf8(
            texture_id.data(), static_cast<qsizetype>(texture_id.size()));
      }
      if (texture.width <=
              static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
          texture.height <=
              static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        presentation.pixel_size =
            QSize(static_cast<int>(texture.width),
                  static_cast<int>(texture.height));
      }
      const std::uintmax_t maximum_qint64 =
          static_cast<std::uintmax_t>(std::numeric_limits<qint64>::max());
      presentation.encoded_byte_size = static_cast<qint64>(
          std::min(texture.encoded_byte_size, maximum_qint64));
      presentation.format =
          QString::fromStdString(texture.detected_format).toUpper();
      if (!presentation.format.isEmpty()) {
        presentation.format += QStringLiteral(" / ");
      }
      presentation.format += QStringLiteral("RGBA8");
      presentation.color_space =
          texture.color_space == QuaderProjectTextureColorSpace::Srgb
              ? QStringLiteral("sRGB")
              : QStringLiteral("Linear");
      presentation.preview_image = standard_material_preview_image_cache_.image(
          index, material_identity, source.session_id.generation(),
          texture.content_revision, texture.rgba8, texture.width,
          texture.height);

    }
  }
  if (material_preview_surface_ != nullptr && render_snapshot.has_value()) {
    material_preview_surface_->set_material(std::move(*render_snapshot));
  }
  standard_material_editor_window_->set_document(std::move(document));
  standard_material_editor_presenter_.mark_presented(stamp);
}

void UISystem::open_standard_material(std::string_view stable_id) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return;
  }
  if (editor_ == nullptr || standard_material_editor_window_ == nullptr ||
      !editor_->project().has_project()) {
    return;
  }
  const std::optional<ProjectAssetId> id = ProjectAssetId::parse(stable_id);
  if (!id.has_value()) {
    present_standard_material_error("Material identity is invalid.");
    return;
  }

  const QuaderApiResult<std::vector<QuaderProjectMaterialInfo>> materials =
      editor_->project_assets().materials();
  if (!materials.ok) {
    present_standard_material_error(materials.message);
    return;
  }
  const auto found = std::find_if(
      materials.value().begin(), materials.value().end(),
      [&id](const QuaderProjectMaterialInfo &material) {
        return material.id == *id;
      });
  if (found == materials.value().end()) {
    present_standard_material_error("Project material is unavailable.");
    return;
  }
  const std::map<QuaderProjectMaterialKind, std::function<void()>>
      editor_registry{
      {QuaderProjectMaterialKind::Standard,
       [this, material_id = *id]() {
         open_standard_material_document(material_id);
       }},
  };
  const auto editor = editor_registry.find(found->kind);
  if (editor == editor_registry.end()) {
    present_standard_material_error(
        localization_.text("ui.standard_material_editor.graph_unavailable",
                           "Graph Material editing is not available yet."));
    return;
  }
  editor->second();
}

void UISystem::open_standard_material_document(ProjectAssetId id) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> current =
      editor_->standard_material_documents().current();
  if (current.ok && current.value().id == id) {
    refresh_standard_material_editor();
    standard_material_editor_window_->show();
    standard_material_editor_window_->raise();
    standard_material_editor_window_->activateWindow();
    return;
  }
  if (!resolve_standard_material_document(false)) {
    return;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> opened =
      editor_->standard_material_documents().open(id);
  present_lifecycle_result(opened);
  if (opened.ok && material_preview_surface_ != nullptr) {
    (void)material_preview_surface_->reset_preview();
  }
  refresh_standard_material_editor();
  if (!opened.ok) {
    return;
  }
  standard_material_editor_window_->show();
  standard_material_editor_window_->raise();
  standard_material_editor_window_->activateWindow();
}

void UISystem::open_standard_material_from_dialog() {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return;
  }
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return;
  }
  PlatformFileDialogOptions options;
  options.owner_window_handle = native_window_handle();
  options.initial_directory = editor_->project().root() / "Materials";
  const std::optional<std::filesystem::path> selected =
      platform_open_material_file_dialog(options);
  if (!selected.has_value()) {
    return;
  }
  const std::optional<ProjectRelativePath> relative =
      project_relative_dialog_path(*selected, editor_->project().root());
  if (!relative.has_value()) {
    present_standard_material_error(localization_.text(
        "ui.standard_material_editor.path_outside_project",
        "Choose a material inside the current project."));
    return;
  }
  const QuaderApiResult<std::vector<QuaderProjectMaterialInfo>> materials =
      editor_->project_assets().materials();
  if (!materials.ok) {
    present_standard_material_error(materials.message);
    return;
  }
  const std::string portable_path = relative->portable_string();
  const auto found = std::find_if(
      materials.value().begin(), materials.value().end(),
      [&portable_path](const QuaderProjectMaterialInfo &material) {
        return material.project_relative_path == portable_path;
      });
  if (found == materials.value().end()) {
    present_standard_material_error(localization_.text(
        "ui.standard_material_editor.not_in_catalog",
        "The selected QMAT is not present in the project material catalog."));
    return;
  }
  open_standard_material(found->id.str());
}

void UISystem::new_standard_material() {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return;
  }
  if (editor_ == nullptr || standard_material_editor_window_ == nullptr ||
      !editor_->project().has_project() ||
      !resolve_standard_material_document(false)) {
    return;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> created =
      editor_->standard_material_documents().new_material();
  present_lifecycle_result(created);
  if (created.ok && material_preview_surface_ != nullptr) {
    (void)material_preview_surface_->reset_preview();
  }
  refresh_standard_material_editor();
  if (created.ok) {
    standard_material_editor_window_->show();
    standard_material_editor_window_->raise();
    standard_material_editor_window_->activateWindow();
  }
}

bool UISystem::save_standard_material(bool save_as) {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return false;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> current =
      editor_->standard_material_documents().current();
  if (!current.ok) {
    present_standard_material_error(current.message);
    return false;
  }

  QuaderApiVoidResult result;
  if (!save_as && current.value().path.has_value()) {
    result = editor_->standard_material_documents().save();
  } else {
    const std::optional<ProjectRelativePath> relative =
        choose_project_material_path(current.value());
    if (!relative.has_value()) return false;
    result = editor_->standard_material_documents().save_as(*relative);
  }
  present_lifecycle_result(result);
  refresh_project_workspace();
  return result.ok;
}

bool UISystem::resolve_standard_material_document(bool close_after) {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return true;
  }
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> current =
      editor_->standard_material_documents().current();
  if (!current.ok) {
    return true;
  }

  bool discard = false;
  if (current.value().dirty) {
    std::string message = localization_.text(
        "ui.standard_material_editor.unsaved_message",
        "{name} has unsaved changes. Save it?");
    constexpr std::string_view kNameToken = "{name}";
    if (const std::size_t token = message.find(kNameToken);
        token != std::string::npos) {
      message.replace(token, kNameToken.size(), current.value().name);
    }
    if (standard_material_unsaved_confirmation_ == nullptr) {
      return false;
    }
    const QtUnsavedConfirmationDecision decision =
        standard_material_unsaved_confirmation_->present(
            QtUnsavedConfirmationPresentation{
                .object_name = QStringLiteral(
                    "standard_material_unsaved_confirmation"),
                .title = QString::fromStdString(localization_.text(
                    "ui.standard_material_editor.unsaved_title",
                    "Unsaved Standard Material")),
                .message = QString::fromStdString(message),
                .save_label = QString::fromStdString(localization_.text(
                    "ui.standard_material_editor.unsaved_save", "Save")),
                .discard_label = QString::fromStdString(localization_.text(
                    "ui.standard_material_editor.unsaved_discard",
                    "Discard")),
                .cancel_label = QString::fromStdString(localization_.text(
                    "ui.standard_material_editor.unsaved_cancel", "Cancel")),
            });
    if (decision == QtUnsavedConfirmationDecision::Save) {
      if (!save_standard_material(false)) {
        return false;
      }
    } else if (decision == QtUnsavedConfirmationDecision::Discard) {
      discard = true;
    } else {
      return false;
    }
  }

  if (discard || close_after) {
    const QuaderApiVoidResult closed =
        editor_->standard_material_documents().close(discard);
    present_lifecycle_result(closed);
    refresh_standard_material_editor();
    return closed.ok;
  }
  return true;
}

void UISystem::handle_standard_material_file_action(
    QtStandardMaterialEditorFileAction action) {
  switch (action) {
  case QtStandardMaterialEditorFileAction::New:
    new_standard_material();
    return;
  case QtStandardMaterialEditorFileAction::Open:
    open_standard_material_from_dialog();
    return;
  case QtStandardMaterialEditorFileAction::Save:
    (void)save_standard_material(false);
    return;
  case QtStandardMaterialEditorFileAction::SaveAs:
    (void)save_standard_material(true);
    return;
  }
}

void UISystem::handle_standard_material_scalar(
    QtStandardMaterialScalar scalar, double value) {
  if (editor_ == nullptr) {
    return;
  }
  const QuaderApiVoidResult result =
      editor_->standard_material_documents().set_scalar(
          domain_material_scalar(scalar), static_cast<float>(value));
  present_lifecycle_result(result);
  refresh_standard_material_editor();
}

void UISystem::handle_standard_material_texture(
    QtStandardMaterialTextureSlot slot,
    QtStandardMaterialTextureAction action) {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return;
  }
  QuaderApiVoidResult result;
  if (action == QtStandardMaterialTextureAction::Clear) {
    result = editor_->standard_material_documents().clear_texture(
        domain_texture_slot(slot));
  } else {
    select_project_material_texture(slot);
    return;
  }
  present_lifecycle_result(result);
  refresh_standard_material_editor();
}

void UISystem::handle_standard_material_texture_transform(
    QtStandardMaterialTextureTransformComponent component, double value) {
  if (editor_ == nullptr || !std::isfinite(value)) {
    return;
  }
  const QuaderApiVoidResult result =
      editor_->standard_material_documents().set_texture_transform_component(
          domain_texture_transform_component(component),
          static_cast<float>(value));
  present_lifecycle_result(result);
  refresh_standard_material_editor();
}

void UISystem::refresh_localized_chrome() {
  QDR_PROFILE_SCOPE("UI refresh localized chrome");
  refresh_asset_browser_labels();
  toolbar_owner_presenter_.invalidate();
  toolbar_tool_presenter_.invalidate();
  toolbar_transform_presenter_.invalidate();
  const auto text = [this](std::string_view key, std::string_view fallback) {
    return QString::fromStdString(localization_.text(key, fallback));
  };
  const auto update_title = [this](UIPanelId id) {
    if (QDockWidget *dock = panels_.dock(id)) {
      dock->setWindowTitle(QString::fromStdString(localization_.text(
          ui_panel_localization_key(id), ui_panel_key(id))));
    }
  };
  if (command_palette_ != nullptr) {
    command_palette_->refresh_localization();
  }
  update_title(UIPanelId::Outliner);
  update_title(UIPanelId::Inspector);
  update_title(UIPanelId::Layers);
  update_title(UIPanelId::FaceTexture);
  update_title(UIPanelId::Console);
  update_title(UIPanelId::AssetBrowser);
  if (face_texture_panel_ != nullptr) {
    face_texture_panel_->set_localization(localization_);
  }
  if (selection_toolbar_ != nullptr) {
    selection_toolbar_->set_localization(localization_);
  }
  if (snapping_toolbar_ != nullptr) {
    snapping_toolbar_->set_localization(localization_);
  }
  if (viewport_display_toolbar_ != nullptr) {
    viewport_display_toolbar_->set_localization(localization_);
  }
  if (texture_toolbar_ != nullptr) {
    texture_toolbar_->set_localization(localization_);
  }
  if (tool_toolbar_ != nullptr) {
    tool_toolbar_->set_localization(localization_);
  }
  if (project_home_page_ != nullptr) {
    project_home_page_->set_labels(QtProjectHomeLabels{
        .new_project = text("ui.home.new_project", "New project"),
        .open_project = text("ui.home.open_project", "Open project…"),
        .recent_projects =
            text("ui.home.recent_projects", "Recent projects"),
        .recent_name_column = text("ui.home.recent_name", "Project"),
        .recent_last_modified_column = text(
            "ui.home.recent_last_modified", "Last modified"),
        .recent_editor_version_column =
            text("ui.home.recent_editor_version", "Editor version"),
        .recent_actions_column =
            text("ui.home.recent_actions", "Actions"),
        .no_recent_projects =
            text("ui.home.no_recent_projects", "No recent projects"),
        .unavailable_project =
            text("ui.home.unavailable_project", "Project is unavailable"),
        .open_recent_project =
            text("ui.home.recent_action.open", "Open"),
        .open_recent_project_location = text(
            "ui.home.recent_action.open_file_location",
            "Open file location"),
        .copy_recent_project_path =
            text("ui.home.recent_action.copy_path", "Copy path"),
        .remove_recent_project = text(
            "ui.home.recent_action.remove", "Remove from recent"),
        .title = text("ui.home.title", "Projects"),
        .tutorial_title = text(
            "ui.home.tutorial.title", "Your first hour in Quader"),
        .tutorial_description = text(
            "ui.home.tutorial.description",
            "Learn the basics and build your first scene."),
        .tutorial_action = text("ui.home.tutorial.action", "Start tutorial"),
        .documentation_title = text(
            "ui.home.documentation.title", "Read the Docs"),
        .documentation_description = text(
            "ui.home.documentation.description",
            "Explore guides to Quader’s tools and workflows."),
        .documentation_action = text(
            "ui.home.documentation.action", "Open docs"),
        .modified_just_now = text("ui.home.modified.just_now", "Just now"),
        .modified_minute_ago = text("ui.home.modified.minute_ago", "1 minute ago"),
        .modified_minutes_ago =
            text("ui.home.modified.minutes_ago", "%1 minutes ago"),
        .modified_hour_ago = text("ui.home.modified.hour_ago", "1 hour ago"),
        .modified_hours_ago = text("ui.home.modified.hours_ago", "%1 hours ago"),
        .modified_yesterday = text("ui.home.modified.yesterday", "Yesterday"),
        .modified_days_ago = text("ui.home.modified.days_ago", "%1 days ago"),
        .modified_week_ago = text("ui.home.modified.week_ago", "1 week ago"),
        .modified_weeks_ago = text("ui.home.modified.weeks_ago", "%1 weeks ago"),
        .modified_month_ago = text("ui.home.modified.month_ago", "1 month ago"),
        .modified_months_ago = text("ui.home.modified.months_ago", "%1 months ago"),
        .modified_year_ago = text("ui.home.modified.year_ago", "1 year ago"),
        .modified_years_ago = text("ui.home.modified.years_ago", "%1 years ago"),
    });
  }
  if (scene_workspace_ != nullptr) {
    scene_workspace_->set_labels(QtSceneWorkspaceLabels{
        .untitled_scene =
            text("ui.workspace.untitled_scene", "Untitled Scene"),
        .unsaved_scene =
            text("ui.workspace.unsaved_scene", "Unsaved scene"),
        .new_scene = text("ui.workspace.new_scene", "New Scene"),
        .open_scene = text("ui.workspace.open_scene", "Open Scene"),
        .empty_title = text("ui.workspace.empty_title", "No scenes open"),
        .empty_description = text(
            "ui.workspace.empty_description",
            "Create a new scene or open one from this project."),
        .return_to_scene = text("ui.workspace.return_to_scene",
                                "Return to scene %1"),
        .editing_instance = text("ui.workspace.editing_instance",
                                 "Editing instance %1"),
    });
  }
  if (material_browser_window_ != nullptr ||
      material_picker_window_ != nullptr) {
    const QtMaterialBrowserLabels labels{
        .window_title =
            text("ui.material_browser.title", "Material Browser"),
        .search_placeholder = text("ui.material_browser.search",
                                   "Search materials"),
        .grid = text("ui.material_browser.grid", "Grid"),
        .list = text("ui.material_browser.list", "List"),
        .tile_size =
            text("ui.material_browser.tile_size", "Preview size"),
        .tile_size_value = text("ui.material_browser.tile_size_value",
                                "Preview size: %1 px"),
        .import_textures = text("ui.material_browser.import",
                                "Import…"),
        .name_column = text("ui.material_browser.name", "Name"),
        .type_column = text("ui.material_browser.type", "Type"),
        .size_column = text("ui.material_browser.size", "Size"),
        .material_type = text(
            "ui.material_browser.standard_material_type",
            "Standard Material"),
        .empty_catalog = text("ui.material_browser.empty",
                              "No materials in this project"),
        .no_results = text("ui.material_browser.no_results",
                           "No materials match this search"),
        .accept = text("ui.material_browser.accept", "Accept"),
        .assign = text("ui.material_browser.assign", "Assign"),
        .picker_window_title = text(
            "ui.material_browser.picker_title",
            "Replace Material Browser"),
        .folders = text("ui.material_browser.folders", "Folders"),
        .all = text("ui.material_browser.all", "All"),
        .new_folder = text("ui.material_browser.new_folder", "New Folder"),
        .rename_folder = text("ui.material_browser.rename_folder", "Rename"),
        .delete_folder = text("ui.material_browser.delete_folder", "Delete"),
        .folder_name = text("ui.material_browser.folder_name", "Folder name"),
        .empty_folder = text("ui.material_browser.empty_folder", "No materials in this folder"),
        .filter = text("ui.material_browser.filter", "Filter materials"),
        .filter_title =
            text("ui.material_browser.filter.title", "Filter Materials"),
        .filter_used = text("ui.material_browser.filter.used", "Used"),
        .filter_not_used =
            text("ui.material_browser.filter.not_used", "Not Used"),
        .filter_clear = text("ui.material_browser.filter.clear", "Clear"),
        .filter_apply = text("ui.material_browser.filter.apply", "Apply"),
        .filter_cancel =
            text("ui.material_browser.filter.cancel", "Cancel"),
        .filtered_empty = text(
            "ui.material_browser.no_filter_results",
            "No materials match the current filters"),
        .usage_inspection_unavailable = text(
            "ui.material_browser.filter.incomplete",
            "Material usage could not be fully inspected."),
        .copy_texture_name = text(
            "ui.material_browser.context.copy_texture_name",
            "Copy Texture Name"),
        .copy_texture_path = text(
            "ui.material_browser.context.copy_texture_path",
            "Copy Texture Path"),
        .open_file_location = text(
            "ui.material_browser.context.open_file_location",
            "Open File Location"),
        .show_texture_info = text(
            "ui.material_browser.context.show_texture_info",
            "Show Texture Info"),
        .rename_material =
            text("ui.material_browser.context.rename", "Rename"),
        .duplicate_material =
            text("ui.material_browser.context.duplicate", "Duplicate"),
        .cut_material = text("ui.material_browser.context.cut", "Cut"),
        .copy_material = text("ui.material_browser.context.copy", "Copy"),
        .paste_material =
            text("ui.material_browser.context.paste", "Paste"),
        .delete_material =
            text("ui.material_browser.context.delete", "Delete"),
        .rename_material_title = text(
            "ui.material_browser.rename_material.title", "Rename Material"),
        .material_name =
            text("ui.material_browser.material_name", "Material name"),
        .edit_material = text("ui.material_browser.context.edit", "Edit Material"),
        .usage_incomplete = text("ui.material_browser.filter.incomplete_status",
            "Material usage is incomplete; Not Used is unavailable."),
        .clear_usage_filter = text("ui.material_browser.filter.clear_predicate",
                                  "Clear usage filter"),
        .filtered_by = text("ui.material_browser.filter.applied_label", "Filtered by:"),
        .filter_active = text("ui.material_browser.filter.active_tooltip", "%1 (1 active)"),
        .remove_usage_filter = text("ui.material_browser.filter.remove_predicate",
                                    "Remove %1 filter"),
    };
    if (material_browser_window_ != nullptr) {
      material_browser_window_->set_labels(labels);
      if (face_texture_panel_ != nullptr) {
        face_texture_panel_->browser().set_labels(labels);
      }
    }
    if (material_picker_window_ != nullptr) {
      material_picker_window_->set_labels(labels);
    }
    material_browser_catalog_signature_ = 0u;
    invalidate_material_presentation_cache();
  }
  if (material_find_replace_dialog_ != nullptr) {
    material_find_replace_dialog_->set_labels(
        QtMaterialFindReplaceLabels{
            .window_title = text("ui.material_find_replace.title",
                                 "Find/Replace Materials"),
            .find = text("ui.material_find_replace.find", "Find"),
            .replace = text("ui.material_find_replace.replace", "Replace"),
            .browse = text("ui.material_find_replace.browse", "Browse…"),
            .empty = text("ui.material_find_replace.empty",
                          "No material selected"),
            .replace_action = text(
                "ui.material_find_replace.action.replace", "Replace"),
            .cancel_action = text(
                "ui.material_find_replace.action.cancel", "Cancel"),
        });
  }
  if (standard_material_editor_window_ != nullptr) {
    standard_material_editor_window_->set_labels(
        QtStandardMaterialEditorLabels{
            .window_title = text("ui.standard_material_editor.title",
                                 "Standard Material Editor"),
            .file_menu = text("ui.standard_material_editor.file", "File"),
            .new_material =
                text("ui.standard_material_editor.file.new", "New"),
            .open_material = text("ui.standard_material_editor.file.open",
                                  "Open..."),
            .save_material =
                text("ui.standard_material_editor.file.save", "Save"),
            .save_material_as = text(
                "ui.standard_material_editor.file.save_as", "Save As..."),
            .close_window =
                text("ui.standard_material_editor.file.close", "Close"),
            .view_menu = text("ui.standard_material_editor.view", "View"),
            .preview_mesh_menu = text(
                "ui.standard_material_editor.view.preview_mesh",
                "Preview Mesh"),
            .reset_preview = text(
                "ui.standard_material_editor.view.reset_preview",
                "Reset Preview"),
            .environment_menu = text(
                "ui.standard_material_editor.environment", "Environment"),
            .show_environment_background = text(
                "ui.standard_material_editor.environment.show_background",
                "Show Background"),
            .sphere = text("ui.standard_material_editor.mesh.sphere",
                           "Sphere"),
            .box = text("ui.standard_material_editor.mesh.box", "Box"),
            .rounded_box = text(
                "ui.standard_material_editor.mesh.rounded_box",
                "Rounded Box"),
            .cylinder = text("ui.standard_material_editor.mesh.cylinder",
                             "Cylinder"),
            .plane =
                text("ui.standard_material_editor.mesh.plane", "Plane"),
            .wireframe = text(
                "ui.standard_material_editor.shading.wireframe",
                "Wireframe"),
            .shaded = text("ui.standard_material_editor.shading.shaded",
                           "Shaded"),
            .rendered = text(
                "ui.standard_material_editor.shading.rendered",
                "Rendered"),
            .preview_unavailable = text(
                "ui.standard_material_editor.preview_unavailable",
                "Material preview is unavailable"),
            .preview_navigation = text(
                "ui.standard_material_editor.preview_navigation",
                "MMB Tumble - Shift+MMB Pan - Ctrl+MMB Environment - Wheel Zoom"),
            .properties_group = text(
                "ui.standard_material_editor.properties", "Material"),
            .name = text("ui.standard_material_editor.name", "Name"),
            .diffuse_model = text("ui.standard_material_editor.diffuse_model", "Diffuse Model"),
            .diffuse_burley = text("ui.standard_material_editor.diffuse_model.burley", "Burley"),
            .diffuse_lambert = text("ui.standard_material_editor.diffuse_model.lambert", "Lambert"),
            .type = text("ui.standard_material_editor.type", "Type"),
            .standard_material = text(
                "ui.standard_material_editor.standard_material",
                "Standard Material"),
            .albedo = text("ui.standard_material_editor.albedo", "Albedo"),
            .albedo_texture = text(
                "ui.standard_material_editor.albedo_texture",
                "Albedo Texture"),
            .roughness = text("ui.standard_material_editor.roughness",
                              "Roughness"),
            .roughness_texture = text(
                "ui.standard_material_editor.roughness_texture",
                "Roughness Texture"),
            .metallic =
                text("ui.standard_material_editor.metallic", "Metallic"),
            .metallic_texture = text(
                "ui.standard_material_editor.metallic_texture",
                "Metallic Texture"),
            .normal_strength = text(
                "ui.standard_material_editor.normal_strength",
                "Normal Strength"),
            .normal_texture = text(
                "ui.standard_material_editor.normal_texture",
                "Normal Texture"),
            .direct_x_normal = text(
                "ui.standard_material_editor.directx_normal",
                "DirectX"),
            .choose_albedo_title = text(
                "ui.standard_material_editor.choose_albedo_title",
                "Choose Albedo Color"),
            .browse_texture = text(
                "ui.standard_material_editor.texture.browse", "Browse..."),
            .clear_texture = text(
                "ui.standard_material_editor.texture.clear", "Clear"),
            .no_texture = text(
                "ui.standard_material_editor.texture.none", "No texture"),
            .untitled_material = text(
                "ui.standard_material_editor.untitled",
                "Untitled Material"),
            .unsaved =
                text("ui.standard_material_editor.unsaved", "Unsaved"),
            .inspect_texture = text(
                "ui.standard_material_editor.texture.inspect",
                "Inspect texture"),
            .texture_transform = text(
                "ui.standard_material_editor.texture_transform",
                "Texture Transform"),
            .texture_center = text(
                "ui.standard_material_editor.texture_transform.center",
                "Center (tiles)"),
            .texture_scale = text(
                "ui.standard_material_editor.texture_transform.scale",
                "UV Tiling"),
            .texture_offset = text(
                "ui.standard_material_editor.texture_transform.offset",
                "Offset (tiles)"),
            .texture_rotation = text(
                "ui.standard_material_editor.texture_transform.rotation",
                "Rotation (\xC2\xB0)"),
            .texture_transform_tooltip = text(
                "ui.standard_material_editor.texture_transform.tooltip",
                "Shared material transform: affects every texture channel and every surface using this material, after face UV mapping."),
            .texture_center_tooltip = text(
                "ui.standard_material_editor.texture_transform.center.tooltip",
                "Pivot for material UV tiling and rotation, measured in texture tiles. The default 0.5, 0.5 is the image center."),
            .texture_scale_tooltip = text(
                "ui.standard_material_editor.texture_transform.scale.tooltip",
                "Multiply material UV repetitions. Larger magnitudes make the texture smaller; negative values mirror it. Applies to every surface using this material."),
            .texture_offset_tooltip = text(
                "ui.standard_material_editor.texture_transform.offset.tooltip",
                "Offset the material UV coordinates in texture tiles after tiling and rotation. One unit equals a full texture tile."),
            .texture_rotation_tooltip = text(
                "ui.standard_material_editor.texture_transform.rotation.tooltip",
                "Rotate material UV coordinates around Center, after tiling and before Offset. Applies to every surface using this material."),
            .texture_inspector = text(
                "ui.standard_material_editor.texture_inspector",
                "Texture Inspector"),
            .texture_info_property =
                text("ui.texture_info.property", "Property"),
            .texture_info_value = text("ui.texture_info.value", "Value"),
            .texture_info_name = text("ui.texture_info.name", "Name"),
            .texture_info_location =
                text("ui.texture_info.location", "Location"),
            .texture_info_format = text("ui.texture_info.format", "Format"),
            .texture_info_size = text("ui.texture_info.size", "Size"),
            .texture_info_file_size =
                text("ui.texture_info.file_size", "File size"),
            .texture_info_red_channel =
                text("ui.texture_info.channel.red", "Red channel"),
            .texture_info_green_channel =
                text("ui.texture_info.channel.green", "Green channel"),
            .texture_info_blue_channel =
                text("ui.texture_info.channel.blue", "Blue channel"),
            .texture_info_alpha_channel =
                text("ui.texture_info.channel.alpha", "Alpha channel"),
        });
    std::vector<QtStandardMaterialPreviewEnvironment> environments;
    environments.reserve(scene_environment_catalog().size());
    for (const SceneEnvironmentDescriptor &environment :
         scene_environment_catalog()) {
      environments.push_back(QtStandardMaterialPreviewEnvironment{
          .stable_id = QString::fromUtf8(environment.id.data(),
                                         static_cast<qsizetype>(
                                             environment.id.size())),
          .display_name =
              text(environment.label_key, environment.fallback_label),
      });
    }
    standard_material_editor_window_->set_preview_environments(
        std::move(environments));
  }
  if (material_texture_info_window_ != nullptr) {
    material_texture_info_window_->set_labels(QtTextureInfoLabels{
        .window_title = text("ui.texture_info.title", "Texture Info"),
        .property = text("ui.texture_info.property", "Property"),
        .value = text("ui.texture_info.value", "Value"),
        .name = text("ui.texture_info.name", "Name"),
        .location = text("ui.texture_info.location", "Location"),
        .format = text("ui.texture_info.format", "Format"),
        .size = text("ui.texture_info.size", "Size"),
        .file_size = text("ui.texture_info.file_size", "File size"),
        .red_channel =
            text("ui.texture_info.channel.red", "Red channel"),
        .green_channel =
            text("ui.texture_info.channel.green", "Green channel"),
        .blue_channel =
            text("ui.texture_info.channel.blue", "Blue channel"),
        .alpha_channel =
            text("ui.texture_info.channel.alpha", "Alpha channel"),
    });
  }
}

void UISystem::set_session_settings_changed_callback(
    std::function<void(const SessionPreferences &)> callback) {
  session_settings_changed_ = std::move(callback);
}

void UISystem::set_viewport_presentation_requested_callback(
    std::function<void()> callback) {
  viewport_presentation_requested_ = std::move(callback);
}

void UISystem::set_default_material_texture_changed_callback(
    std::function<void(const std::filesystem::path &)> callback) {
  default_material_texture_changed_ = std::move(callback);
}

void UISystem::handle_settings_dialog_change(const QtSettingsChange &change) {
  switch (change.kind) {
  case QtSettingsChangeKind::ViewportPreview:
    return;
  case QtSettingsChangeKind::ViewportCommit:
    (void)save_viewport_settings();
    break;
  case QtSettingsChangeKind::InterfacePreview:
    if (change.interface_scale.has_value()) {
      (void)apply_interface_scale(*change.interface_scale);
    }
    return;
  case QtSettingsChangeKind::InterfaceCommit:
    if (change.interface_scale.has_value()) {
      (void)apply_interface_scale(*change.interface_scale);
    }
    break;
  case QtSettingsChangeKind::RenderingPreview:
  case QtSettingsChangeKind::RenderingCommit:
    if (render_system_ != nullptr && change.vsync_enabled.has_value()) {
      set_presentation_vsync(*change.vsync_enabled);
    }
    return;
  case QtSettingsChangeKind::KeybindingsCommit:
    if (editor_ != nullptr) {
      const QuaderSelectionSummary selection = editor_->selection().summary();
      if (selection_toolbar_ != nullptr && modeling_ != nullptr) {
        const QuaderSelectionMode active_mode =
            to_api_selection_mode(modeling_->selection_mode());
        selection_toolbar_->synchronize(*editor_, active_mode, true);
      }
      if (tool_toolbar_ != nullptr) {
        synchronize_tool_toolbar(selection, true);
      }
      if (snapping_toolbar_ != nullptr) {
        snapping_toolbar_->synchronize(*editor_, true);
      }
      if (viewport_display_toolbar_ != nullptr) {
        viewport_display_toolbar_->synchronize(*editor_, true);
      }
      if (texture_toolbar_ != nullptr) {
        texture_toolbar_->synchronize(*editor_, true);
      }
      if (viewport_panel_ != nullptr) {
        viewport_panel_->refresh_keybinding_presentation(*editor_);
      }
    }
    break;
  case QtSettingsChangeKind::SessionCommit:
    if (change.session_settings.has_value() && session_settings_changed_) {
      session_settings_changed_(*change.session_settings);
    }
    return;
  case QtSettingsChangeKind::DefaultMaterialCommit:
    if (change.default_material_texture.has_value() &&
        default_material_texture_changed_) {
      default_material_texture_changed_(*change.default_material_texture);
    }
    return;
  }
  if (main_menu_ != nullptr) {
    main_menu_->refresh();
  }
}

bool UISystem::apply_interface_scale(float factor) {
  if (ui_scale_controller_ == nullptr ||
      !ui_scale_controller_->apply_factor(factor)) {
    return false;
  }
  apply_ui_scale_to_retained_surfaces(ui_scale_controller_->metrics());
  return true;
}

void UISystem::apply_ui_scale_to_retained_surfaces(
    const QtUiScaleMetrics &metrics) {
  if (loading_cover_ != nullptr) {
    static_cast<QtLoadingCover *>(loading_cover_)->set_logo_extent(metrics.px(96));
  }
  if (main_window_ != nullptr) {
    const int padding = metrics.px(10);
    // QStatusBar adds 3 native pixels above its row and 2 below. Balance
    // that offset for every child, including fixed-height shortcut hints.
    const int top = (padding - 1) / 2;
    main_window_->statusBar()->setContentsMargins(0, top, 0, padding - top);
  }
  if (operation_input_hints_ != nullptr) {
    operation_input_hints_->apply_ui_scale(metrics);
  }
  if (selection_toolbar_ != nullptr) {
    selection_toolbar_->apply_ui_scale(metrics);
  }
  if (snapping_toolbar_ != nullptr) {
    snapping_toolbar_->apply_ui_scale(metrics);
  }
  if (viewport_display_toolbar_ != nullptr) {
    viewport_display_toolbar_->apply_ui_scale(metrics);
  }
  if (texture_toolbar_ != nullptr) {
    texture_toolbar_->apply_ui_scale(metrics);
  }
  if (tool_toolbar_ != nullptr) {
    tool_toolbar_->apply_ui_scale(metrics);
  }
  if (project_home_page_ != nullptr) {
    project_home_page_->apply_ui_scale(metrics);
  }
  if (scene_workspace_ != nullptr) {
    scene_workspace_->apply_ui_scale(metrics);
  }
  if (viewport_panel_ != nullptr) {
    viewport_panel_->apply_ui_scale(metrics);
  }
  if (outliner_panel_ != nullptr) {
    outliner_panel_->apply_ui_scale(metrics);
  }
  if (inspector_panel_ != nullptr) {
    inspector_panel_->apply_ui_scale(metrics);
  }
  if (layer_panel_ != nullptr) {
    layer_panel_->apply_ui_scale(metrics);
  }
  if (tool_settings_panel_ != nullptr) {
    tool_settings_panel_->apply_ui_scale(metrics);
  }
  if (face_texture_panel_ != nullptr) {
    face_texture_panel_->apply_ui_scale(metrics);
  }
  if (settings_dialog_ != nullptr) {
    settings_dialog_->apply_ui_scale(metrics);
  }
  if (asset_browser_ != nullptr) {
    asset_browser_->apply_ui_scale(metrics);
  }
  if (material_browser_window_ != nullptr) {
    material_browser_window_->apply_ui_scale(metrics);
  }
  if (material_texture_info_window_ != nullptr) {
    material_texture_info_window_->apply_ui_scale(metrics);
  }
  if (material_picker_window_ != nullptr) {
    material_picker_window_->apply_ui_scale(metrics);
  }
  if (material_find_replace_dialog_ != nullptr) {
    material_find_replace_dialog_->apply_ui_scale(metrics);
  }
  if (standard_material_editor_window_ != nullptr) {
    standard_material_editor_window_->apply_ui_scale(metrics);
  }
  for (QWidget *widget : QApplication::allWidgets()) {
    if (auto *message_box = dynamic_cast<QtRetainedMessageBox *>(widget)) {
      message_box->apply_ui_scale(metrics);
    }
  }
  for (UIPanelId id : kUiDockPanelIds) {
    if (QDockWidget *dock = panels_.dock(id)) {
      if (QWidget *content = dock->widget()) {
        apply_dock_content_minimum_width(*content, metrics);
      }
    }
  }
  if (main_window_ != nullptr) {
    const QList<QDockWidget *> docks =
        main_window_->findChildren<QDockWidget *>();
    for (QDockWidget *dock : docks) {
      if (dock != nullptr) {
        apply_ui_scale_to_palette_aware_dock_title_presentation(*dock,
                                                                 metrics);
      }
    }
    if (main_window_->layout() != nullptr) {
      main_window_->layout()->invalidate();
    }
    main_window_->updateGeometry();
    main_window_->update();
  }
}

void UISystem::apply_viewport_settings_to_workspace() {
  if (editor_ == nullptr) {
    return;
  }
  ViewportWorkspaceService &workspace =
      QuaderViewportApiRuntimeAccess::workspace(editor_->viewports());
  workspace.update_control_defaults(viewport_settings_, viewport_defaults_revision_);
  (void)workspace.set_default_fly_speed(viewport_settings_.float_value(
      ViewportFloatSettingId::DefaultFlyNavigationSpeed));
}

void UISystem::synchronize_settings_surfaces(bool refresh_dialog) {
  QDR_PROFILE_SCOPE("UI synchronize settings surfaces");
  apply_viewport_settings_to_workspace();
  toolbar_owner_presenter_.invalidate();
  toolbar_tool_presenter_.invalidate();
  toolbar_transform_presenter_.invalidate();
  if (snapping_toolbar_ != nullptr && editor_ != nullptr) {
    snapping_toolbar_->synchronize(*editor_, true);
  }
  if (viewport_display_toolbar_ != nullptr && editor_ != nullptr) {
    viewport_display_toolbar_->synchronize(*editor_, true);
  }
  if (texture_toolbar_ != nullptr && editor_ != nullptr) {
    texture_toolbar_->synchronize(*editor_, true);
  }
  if (viewport_panel_ != nullptr && editor_ != nullptr) {
    viewport_panel_->refresh_control_actions(*editor_);
  }
  if (main_menu_ != nullptr) {
    main_menu_->refresh();
  }
  if (refresh_dialog && settings_dialog_ != nullptr &&
      settings_dialog_->isVisible()) {
    settings_dialog_->refresh();
  }
}

void UISystem::show_autosave_recovery() {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return;
  }
  const auto directory = editor_->scene_documents().autosave_directory();
  if (!directory.ok()) {
    present_lifecycle_result(directory);
    return;
  }
  // Native dialogs run a nested message loop. Resolve through the current API
  // after it closes, and reject a selection if that loop replaced the project.
  const auto project_root = editor_->project().root();
  const auto binding_generation = runtime_binding_generation_;
  PlatformFileDialogOptions options;
  options.owner_window_handle = native_window_handle();
  options.initial_directory = directory.value();
  const auto selected = platform_open_scene_recovery_file_dialog(options);
  if (!selected || editor_ == nullptr ||
      binding_generation != runtime_binding_generation_ ||
      !editor_->project().has_project() ||
      editor_->project().root() != project_root) {
    return;
  }
  present_lifecycle_result(
      editor_->scene_documents().recover_autosave_native_path(*selected));
}

void UISystem::show_settings_dialog() {
  if (settings_dialog_ == nullptr) {
    return;
  }
  settings_dialog_->refresh();
  settings_dialog_->show();
  settings_dialog_->raise();
  settings_dialog_->activateWindow();
  if (!auxiliary_window_state_.settings_dialog_visible) {
    auxiliary_window_state_.settings_dialog_visible = true;
    save_auxiliary_window_state();
  }
}

void UISystem::show_material_browser() {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return;
  }
  if (material_browser_window_ == nullptr || home_visible_ ||
      editor_ == nullptr || !editor_->project().has_project()) {
    return;
  }
  material_browser_window_->show();
  refresh_project_workspace();
  material_browser_window_->raise();
  material_browser_window_->activateWindow();
}

void UISystem::show_material_find_replace() {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return;
  }
  if (material_find_replace_dialog_ == nullptr || home_visible_ ||
      editor_ == nullptr || document_ == nullptr || modeling_ == nullptr ||
      !editor_->project().has_project() ||
      !editor_->scene_documents().has_active_scene()) {
    return;
  }
  material_find_replace_scope_ = QuaderMaterialUsageScope::ActiveScene;
  material_find_replace_dialog_->clear_materials();
  refresh_material_presentation_cache();
  const QuaderProjectMetadata project = editor_->project().metadata();
  material_find_replace_project_identity_ =
      project.uuid + '\n' +
      editor_->project().manifest_path().generic_string();
  const std::optional<SceneSessionId> scene_id =
      editor_->scene_documents().active_scene_id();
  material_find_replace_scene_id_ =
      scene_id.has_value() ? scene_id->packed() : 0u;
  material_find_replace_scene_selection_revision_ =
      document_->selection_revision();
  material_find_replace_modeling_selection_revision_ =
      modeling_->selection_revision();
  material_find_replace_modeling_object_map_revision_ =
      modeling_->modeling_object_map_revision();
  material_find_replace_presentation_revision_ =
      material_presentation_revision_;
  material_find_replace_dialog_->open();
  material_find_replace_dialog_->raise();
  material_find_replace_dialog_->activateWindow();
}

void UISystem::show_material_find_replace(
    QuaderMaterialReference source) {
  if (!source.valid()) {
    return;
  }
  show_material_find_replace();
  if (material_find_replace_dialog_ == nullptr ||
      !material_find_replace_dialog_->isVisible()) {
    return;
  }
  material_find_replace_scope_ = QuaderMaterialUsageScope::Selection;
  material_find_replace_scene_selection_revision_ =
      document_ != nullptr ? document_->selection_revision() : 0u;
  material_find_replace_modeling_selection_revision_ =
      modeling_ != nullptr ? modeling_->selection_revision() : 0u;
  material_find_replace_modeling_object_map_revision_ =
      modeling_ != nullptr ? modeling_->modeling_object_map_revision() : 0u;
  material_find_replace_dialog_->set_find_material(
      material_presentation_for(source, true));
}

void UISystem::show_material_picker(bool replacement_column) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return;
  }
  validate_material_find_replace_context();
  if (material_picker_window_ == nullptr ||
      material_find_replace_dialog_ == nullptr || editor_ == nullptr ||
      !material_find_replace_dialog_->isVisible()) {
    return;
  }
  refresh_material_presentation_cache();
  material_picker_replacement_column_ = replacement_column;
  std::vector<QtMaterialPresentationItem> items;
  if (replacement_column) {
    items = replacement_material_presentations();
  } else {
    const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
        editor_->materials().used_materials(material_find_replace_scope_);
    if (used.succeeded()) {
      items = material_presentations_for(used.value(), true);
    }
  }
  const QtMaterialFindReplaceColumn column =
      replacement_column ? QtMaterialFindReplaceColumn::Replace
                         : QtMaterialFindReplaceColumn::Find;
  QuaderMaterialReference selected;
  const std::optional<QtMaterialPresentationItem> &current =
      material_find_replace_dialog_->material(column);
  if (current.has_value()) {
    selected = current->reference;
  }
  material_picker_window_->set_project_generation(material_browser_project_generation_);
  material_picker_window_->set_folders(material_presentation_folders_);
  material_picker_window_->set_materials(std::move(items), selected);
  material_picker_presentation_revision_ =
      material_presentation_revision_;
  material_picker_window_->show();
  material_picker_window_->raise();
  material_picker_window_->activateWindow();
}

void UISystem::close_material_find_replace() {
  if (material_picker_window_ != nullptr) {
    material_picker_window_->hide();
    material_picker_window_->clear_materials();
  }
  if (material_find_replace_dialog_ != nullptr) {
    if (material_find_replace_dialog_->isVisible()) {
      material_find_replace_dialog_->reject();
    } else {
      material_find_replace_dialog_->clear_materials();
    }
  }
  material_find_replace_project_identity_.clear();
  material_find_replace_scene_id_ = 0;
  material_find_replace_scene_selection_revision_ = 0;
  material_find_replace_modeling_selection_revision_ = 0;
  material_find_replace_modeling_object_map_revision_ = 0;
  material_find_replace_presentation_revision_ = 0;
  material_picker_presentation_revision_ = 0;
}

void UISystem::validate_material_find_replace_context() {
  if ((material_find_replace_dialog_ == nullptr ||
       !material_find_replace_dialog_->isVisible()) &&
      (material_picker_window_ == nullptr ||
       !material_picker_window_->isVisible())) {
    return;
  }
  if (editor_ == nullptr || document_ == nullptr || modeling_ == nullptr ||
      !editor_->project().has_project()) {
    close_material_find_replace();
    return;
  }
  const QuaderProjectMetadata project = editor_->project().metadata();
  const std::string project_identity =
      project.uuid + '\n' +
      editor_->project().manifest_path().generic_string();
  const std::optional<SceneSessionId> scene_id =
      editor_->scene_documents().active_scene_id();
  if (!scene_id.has_value() ||
      project_identity != material_find_replace_project_identity_ ||
      scene_id->packed() != material_find_replace_scene_id_ ||
      (material_find_replace_scope_ == QuaderMaterialUsageScope::Selection &&
       (document_->selection_revision() !=
            material_find_replace_scene_selection_revision_ ||
        modeling_->selection_revision() !=
            material_find_replace_modeling_selection_revision_ ||
        modeling_->modeling_object_map_revision() !=
            material_find_replace_modeling_object_map_revision_))) {
    close_material_find_replace();
  }
}

void UISystem::finish_material_folder_operation(const QuaderApiStatus &result) {
  const auto generation = runtime_binding_generation_;
  if (editor_ != nullptr) {
    (void)editor_->ui().notify({
        .level = result.ok() ? QuaderNotificationLevel::Info
                            : QuaderNotificationLevel::Error,
        .message = result.message,
    });
  }
  if (result.ok() && result.changed() && generation == runtime_binding_generation_) {
    invalidate_material_presentation_cache();
    refresh_project_workspace();
    refresh_standard_material_editor();
  }
}

void UISystem::show_material_texture_info(ProjectAssetId material_id,
                                          bool activate) {
  if (editor_ == nullptr || material_texture_info_window_ == nullptr ||
      !editor_->project().has_project()) {
    return;
  }
  const auto inspected =
      editor_->project_assets().material_albedo_texture_info(material_id);
  if (!inspected.succeeded()) {
    material_texture_info_window_->clear();
    material_texture_info_window_->hide();
    present_status_result(inspected);
    return;
  }
  const QuaderProjectMaterialTextureInfo &texture = inspected.value();
  if (texture.rgba8 == nullptr) {
    material_texture_info_window_->clear();
    material_texture_info_window_->hide();
    present_status_result(
        {.ok = false,
         .message = localization_.text(
             "ui.material_browser.texture.no_pixels",
             "The material albedo texture has no decoded pixels.")});
    return;
  }
  const auto absolute_utf8 = texture.absolute_path.u8string();
  const std::uint64_t dependency_revision =
      material_browser_dependency_revision_.has_value()
          ? material_browser_dependency_revision_->revision
          : 0u;
  const std::string material_identity(texture.material_id.str());
  const std::string texture_identity(texture.texture_id.str());
  const bool presented = material_texture_info_window_->present(
      TextureInfoPresentation{
          .context =
              TextureInfoContext{
                  .project_identity = material_browser_project_identity_,
                  .activation_generation = material_browser_project_generation_,
                  .dependency_revision = dependency_revision,
                  .material_identity = material_identity,
                  .texture_identity = texture_identity,
              },
          .name = texture.name,
          .location =
              std::string(absolute_utf8.begin(), absolute_utf8.end()),
          .format = texture.format,
          .width = texture.width,
          .height = texture.height,
          .encoded_byte_size = texture.encoded_size_bytes,
          .source_has_alpha = texture.has_alpha_channel,
          .rgba8 = std::span<const std::byte>(texture.rgba8->data(),
                                             texture.rgba8->size()),
          .content_revision = texture.content_revision,
      });
  if (!presented) {
    material_texture_info_window_->clear();
    material_texture_info_window_->hide();
    present_status_result(
        {.ok = false,
         .message = localization_.text(
             "ui.material_browser.texture.invalid_pixels",
             "The material albedo texture has invalid image data.")});
    return;
  }
  material_texture_info_catalog_revision_ =
      material_presentation_catalog_revision_;
  material_texture_info_window_->show();
  if (activate) {
    material_texture_info_window_->raise();
    material_texture_info_window_->activateWindow();
  }
}

void UISystem::refresh_material_texture_info() {
  if (material_texture_info_window_ == nullptr ||
      !material_texture_info_window_->isVisible()) {
    return;
  }
  const TextureInfoContext *context =
      material_texture_info_window_->current_context();
  if (context != nullptr && context->material_identity.empty()) {
    const auto revision = editor_->project_assets().asset_revision();
    if (!revision.ok() ||
        revision.value().project_generation != context->activation_generation) {
      material_texture_info_window_->clear();
      material_texture_info_window_->hide();
      return;
    }
    if (revision.value().revision != context->dependency_revision) {
      if (const auto id = ProjectAssetId::parse(context->texture_identity)) {
        show_asset_texture_info(*id, false);
      }
    }
    return;
  }
  if (material_texture_info_catalog_revision_ ==
      material_presentation_catalog_revision_) {
    return;
  }
  const std::optional<ProjectAssetId> material_id =
      context != nullptr
          ? ProjectAssetId::parse(context->material_identity)
          : std::nullopt;
  const bool material_available =
      material_id.has_value() &&
      std::ranges::any_of(
          material_presentation_items_, [&](const auto &material) {
            return material.available && material.reference.project_owned() &&
                   material.reference.project_asset == *material_id;
          });
  if (!material_available) {
    material_texture_info_window_->clear();
    material_texture_info_window_->hide();
    material_texture_info_catalog_revision_ =
        material_presentation_catalog_revision_;
    return;
  }
  show_material_texture_info(*material_id, false);
}

void UISystem::handle_material_browser_action(
    QtMaterialBrowserWindow &source, QtMaterialBrowserActionRequest request) {
  const QPointer<QtMaterialBrowserWindow> browser(&source);
  if (editor_ == nullptr ||
      !editor_->project().has_project() ||
      request.project_generation != material_browser_project_generation_ ||
      request.project_generation !=
          material_browser_project_generation_signal_->load(
              std::memory_order_acquire)) {
    return;
  }

  const auto dependency_revision =
      editor_->project_assets().material_dependency_revision();
  if (!dependency_revision.succeeded() ||
      request.dependency_revision != dependency_revision.value().revision ||
      !material_browser_dependency_revision_.has_value() ||
      *material_browser_dependency_revision_ != dependency_revision.value()) {
    return;
  }

  std::vector<ProjectAssetId> selected;
  selected.reserve(request.selected_materials.size());
  for (const QuaderMaterialReference &reference :
       request.selected_materials) {
    if (!reference.project_owned()) {
      return;
    }
    selected.push_back(reference.project_asset);
  }
  const auto select_created = [this, browser](
                                  const std::vector<QuaderProjectMaterialInfo> &materials) {
    if (!materials.empty() && browser != nullptr) {
      invalidate_material_presentation_cache();
      refresh_project_workspace();
      browser->set_selected_material(
          QuaderMaterialReference::project(materials.front().id));
    }
  };
  const auto inspect_target = [this, &request]()
      -> std::optional<QuaderProjectMaterialTextureInfo> {
    if (!request.target.project_owned()) {
      return std::nullopt;
    }
    const auto result = editor_->project_assets().material_albedo_texture_info(
        request.target.project_asset);
    if (!result.succeeded()) {
      present_status_result(result);
      return std::nullopt;
    }
    return result.value();
  };

  switch (request.action) {
  case QtMaterialBrowserAction::CopyTextureName:
  case QtMaterialBrowserAction::CopyTexturePath: {
    const auto texture = inspect_target();
    if (!texture.has_value()) {
      return;
    }
    if (QClipboard *clipboard = QApplication::clipboard()) {
      clipboard->setText(
          request.action == QtMaterialBrowserAction::CopyTextureName
              ? QString::fromStdString(texture->name)
              : QString::fromStdWString(texture->absolute_path.wstring()));
    }
    return;
  }
  case QtMaterialBrowserAction::OpenFileLocation: {
    const auto texture = inspect_target();
    if (texture.has_value() &&
        !platform_reveal_file(texture->absolute_path)) {
      present_status_result(
          {.ok = false,
           .message = localization_.text(
               "ui.material_browser.texture.reveal_failed",
               "Could not open the texture file location.")});
    }
    return;
  }
  case QtMaterialBrowserAction::ShowTextureInfo:
    if (request.target.project_owned()) {
      show_material_texture_info(request.target.project_asset);
    }
    return;
  case QtMaterialBrowserAction::Rename: {
    if (!request.target.project_owned()) {
      return;
    }
    const auto result = editor_->project_assets().rename_material(
        request.target.project_asset, request.proposed_name.toStdString());
    finish_material_folder_operation(result);
    return;
  }
  case QtMaterialBrowserAction::Duplicate: {
    const auto result =
        editor_->project_assets().duplicate_materials(selected);
    if (result.succeeded()) {
      select_created(result.value());
    }
    finish_material_folder_operation(result);
    return;
  }
  case QtMaterialBrowserAction::Cut:
  case QtMaterialBrowserAction::Copy: {
    if (selected.empty()) {
      return;
    }
    const auto catalog = editor_->project_assets().materials();
    if (!catalog.succeeded()) {
      finish_material_folder_operation(catalog);
      return;
    }
    std::vector<MaterialClipboardSource> sources;
    sources.reserve(selected.size());
    for (const ProjectAssetId id : selected) {
      const auto found = std::ranges::find(catalog.value(), id,
                                          &QuaderProjectMaterialInfo::id);
      if (found == catalog.value().end()) {
        finish_material_folder_operation({
            .ok = false,
            .message = localization_.text(
                "ui.material_browser.clipboard_stale",
                "The material selection changed before it could be copied."),
        });
        return;
      }
      sources.push_back({
          .id = found->id,
          .project_relative_path = found->project_relative_path,
          .content_revision = found->content_revision,
      });
    }
    material_clipboard_ = {
        .mode = request.action == QtMaterialBrowserAction::Cut
                    ? MaterialClipboardMode::Cut
                    : MaterialClipboardMode::Copy,
        .project_generation = material_browser_project_generation_,
        .material_ids = std::move(selected),
        .sources = std::move(sources),
    };
    if (material_browser_window_ != nullptr) {
      material_browser_window_->set_material_paste_available(true);
    }
    if (face_texture_panel_ != nullptr) {
      face_texture_panel_->browser().set_material_paste_available(true);
    }
    return;
  }
  case QtMaterialBrowserAction::Paste: {
    if (material_clipboard_.mode == MaterialClipboardMode::Empty ||
        material_clipboard_.project_generation !=
            material_browser_project_generation_ ||
        material_clipboard_.material_ids.empty()) {
      material_clipboard_ = {};
      if (material_browser_window_ != nullptr) {
        material_browser_window_->set_material_paste_available(false);
      }
      if (face_texture_panel_ != nullptr) {
        face_texture_panel_->browser().set_material_paste_available(false);
      }
      return;
    }
    const auto catalog = editor_->project_assets().materials();
    const bool clipboard_current = catalog.succeeded() &&
        material_clipboard_.sources.size() ==
            material_clipboard_.material_ids.size() &&
        std::ranges::all_of(
            material_clipboard_.sources,
            [&](const MaterialClipboardSource &source) {
              const auto found = std::ranges::find(
                  catalog.value(), source.id, &QuaderProjectMaterialInfo::id);
              return found != catalog.value().end() &&
                     found->project_relative_path ==
                         source.project_relative_path &&
                     found->content_revision == source.content_revision;
            });
    if (!clipboard_current) {
      material_clipboard_ = {};
      if (material_browser_window_ != nullptr) {
        material_browser_window_->set_material_paste_available(false);
      }
      if (face_texture_panel_ != nullptr) {
        face_texture_panel_->browser().set_material_paste_available(false);
      }
      finish_material_folder_operation({
          .ok = false,
          .message = localization_.text(
              "ui.material_browser.clipboard_stale",
              "The copied materials changed and can no longer be pasted."),
      });
      return;
    }
    if (material_clipboard_.mode == MaterialClipboardMode::Copy) {
      const auto result = editor_->project_assets().copy_materials(
          material_clipboard_.material_ids,
          request.destination_folder.toStdString());
      if (result.succeeded()) {
        select_created(result.value());
      }
      finish_material_folder_operation(result);
      return;
    }
    const auto result = editor_->project_assets().move_materials(
        material_clipboard_.material_ids,
        request.destination_folder.toStdString());
    if (result.succeeded()) {
      material_clipboard_ = {};
      if (material_browser_window_ != nullptr) {
        material_browser_window_->set_material_paste_available(false);
      }
      if (face_texture_panel_ != nullptr) {
        face_texture_panel_->browser().set_material_paste_available(false);
      }
    }
    finish_material_folder_operation(result);
    return;
  }
  case QtMaterialBrowserAction::Delete:
    delete_materials(std::move(selected));
    return;
  }
}

void UISystem::delete_materials(std::vector<ProjectAssetId> material_ids) {
  if (editor_ == nullptr || material_browser_import_message_box_ == nullptr ||
      material_ids.empty() || !editor_->project().has_project() ||
      material_browser_project_generation_ !=
          material_browser_project_generation_signal_->load(
              std::memory_order_acquire)) {
    return;
  }
  const auto binding_generation = runtime_binding_generation_;
  const auto project_generation = material_browser_project_generation_;
  const QuaderProjectMetadata project = editor_->project().metadata();
  const std::string project_identity = project.uuid + "|" +
      editor_->project().manifest_path().generic_string();
  const auto deletion_context_is_current =
      [this, binding_generation, project_generation, project_identity]() {
        if (editor_ == nullptr ||
            binding_generation != runtime_binding_generation_ ||
            project_generation != material_browser_project_generation_ ||
            project_generation !=
                material_browser_project_generation_signal_->load(
                    std::memory_order_acquire) ||
            !editor_->project().has_project()) {
          return false;
        }
        const QuaderProjectMetadata current_project =
            editor_->project().metadata();
        const std::string current_project_identity =
            current_project.uuid + "|" +
            editor_->project().manifest_path().generic_string();
        return current_project_identity == project_identity;
      };
  const auto preview_result =
      editor_->project_assets().preview_delete_materials(material_ids);
  if (!preview_result.succeeded()) {
    finish_material_folder_operation(preview_result);
    return;
  }
  const QuaderMaterialDeletionPreview &preview = preview_result.value();
  if (!preview.complete) {
    QStringList errors;
    for (const std::string &error : preview.errors) {
      errors.push_back(QString::fromStdString(error));
    }
    finish_material_folder_operation({
        .ok = false,
        .message =
            errors.isEmpty()
                ? localization_.text(
                      "ui.material_browser.delete_material.incomplete",
                      "The selected material dependencies could not be fully "
                      "inspected.")
                : errors.join(QLatin1Char('\n')).toStdString(),
    });
    return;
  }
  const auto text = [this](std::string_view key, std::string_view fallback) {
    return QString::fromStdString(localization_.text(key, fallback));
  };
  QStringList details;
  if (!preview.material_names.empty()) {
    details.push_back(
        text("ui.material_browser.delete.materials", "Materials:"));
    for (const std::string &name : preview.material_names) {
      details.push_back(QString::fromStdString(name));
    }
  }
  if (!preview.affected_scenes.empty()) {
    details.push_back(text(
        "ui.material_browser.delete.scenes",
        "References in these scenes will become Default Material:"));
    for (const auto &scene : preview.affected_scenes) {
      details.push_back(
          material_deletion_scene_label(scene, localization_));
    }
  }
  if (preview.discards_material_draft) {
    details.push_back(text(
        "ui.material_browser.delete.draft",
        "The open material draft will be closed and its unsaved changes "
        "discarded."));
  }
  details.push_back(text(
      "ui.material_browser.delete.permanent",
      "Deletion is permanent. Existing scene undo history remains available; "
      "restored references to deleted materials use Default Material."));
  const auto choice = material_browser_import_message_box_->present({
      .icon = QMessageBox::Warning,
      .object_name =
          QStringLiteral("material_browser_delete_materials_message_box"),
      .title = text("ui.material_browser.delete_material.title",
                    "Delete Materials"),
      .text = text("ui.material_browser.delete_material.question",
                   "Delete %1 selected material(s)?")
                  .arg(static_cast<qulonglong>(
                      preview.material_ids.size())),
      .informative_text = details.join(QLatin1Char('\n')),
      .buttons = {{
                      .button = QtRetainedMessageBoxButton::Primary,
                      .object_name = QStringLiteral(
                          "materialBrowserDeleteMaterialsMessageBox.delete"),
                      .label = text(
                          "ui.material_browser.delete.confirm",
                          "Delete and Replace with Default"),
                      .role = QMessageBox::DestructiveRole,
                  },
                  {
                      .button = QtRetainedMessageBoxButton::Secondary,
                      .object_name = QStringLiteral(
                          "materialBrowserDeleteMaterialsMessageBox.cancel"),
                      .label = text("ui.action.cancel", "Cancel"),
                      .role = QMessageBox::RejectRole,
                  }},
      .default_button = QtRetainedMessageBoxButton::Secondary,
      .escape_button = QtRetainedMessageBoxButton::Secondary,
      .close_button = QtRetainedMessageBoxButton::Secondary,
  });
  if (choice != QtRetainedMessageBoxButton::Primary ||
      !deletion_context_is_current()) {
    return;
  }
  const auto result = editor_->project_assets().delete_materials(
      preview.material_ids, preview.revision);
  if (!deletion_context_is_current()) {
    return;
  }
  if (result.succeeded()) {
    const auto removed = [&preview](ProjectAssetId id) {
      return std::ranges::find(preview.material_ids, id) !=
             preview.material_ids.end();
    };
    if (std::ranges::any_of(material_clipboard_.material_ids, removed)) {
      material_clipboard_ = {};
      if (material_browser_window_ != nullptr) {
        material_browser_window_->set_material_paste_available(false);
      }
    }
    if (material_texture_info_window_ != nullptr) {
      const TextureInfoContext *context =
          material_texture_info_window_->current_context();
      if (context != nullptr &&
          std::ranges::any_of(preview.material_ids, [&](ProjectAssetId id) {
            return context->material_identity == id.str();
          })) {
        material_texture_info_window_->clear();
        material_texture_info_window_->hide();
      }
    }
  }
  finish_material_folder_operation(result);
}

void UISystem::delete_material_folder(QString folder) {
  if (editor_ == nullptr || material_browser_import_message_box_ == nullptr ||
      !editor_->project().has_project() || material_browser_project_generation_ !=
          material_browser_project_generation_signal_->load(std::memory_order_acquire)) {
    return;
  }
  const auto generation = runtime_binding_generation_;
  const auto project_generation = material_browser_project_generation_;
  const QuaderProjectMetadata project = editor_->project().metadata();
  const std::string project_identity = project.uuid + "|" +
      editor_->project().manifest_path().generic_string();
  const auto deletion_context_is_current =
      [this, generation, project_generation, project_identity]() {
        if (editor_ == nullptr || generation != runtime_binding_generation_ ||
            project_generation != material_browser_project_generation_ ||
            project_generation !=
                material_browser_project_generation_signal_->load(
                    std::memory_order_acquire) ||
            !editor_->project().has_project()) {
          return false;
        }
        const QuaderProjectMetadata current_project =
            editor_->project().metadata();
        return current_project.uuid + "|" +
                   editor_->project().manifest_path().generic_string() ==
               project_identity;
      };
  const auto preview_result =
      editor_->project_assets().preview_delete_material_folder(folder.toStdString());
  if (!preview_result.ok()) {
    finish_material_folder_operation(preview_result);
    return;
  }
  const auto &preview = preview_result.value();
  if (!preview.complete) {
    QStringList errors;
    for (const std::string &error : preview.errors) {
      errors.push_back(QString::fromStdString(error));
    }
    finish_material_folder_operation({
        .ok = false,
        .message = errors.isEmpty()
            ? localization_.text("ui.material_browser.delete.incomplete",
                                 "The folder dependencies could not be fully inspected.")
            : errors.join(QLatin1Char('\n')).toStdString(),
    });
    return;
  }
  const auto text = [this](std::string_view key, std::string_view fallback) {
    return QString::fromStdString(localization_.text(key, fallback));
  };
  QStringList details;
  details.push_back(text("ui.material_browser.delete.counts", "%1 material(s) in %2 folder(s).")
      .arg(static_cast<qulonglong>(preview.material_ids.size()))
      .arg(static_cast<qulonglong>(preview.folder_count)));
  if (!preview.material_names.empty()) {
    details.push_back(text("ui.material_browser.delete.materials", "Materials:"));
    for (const std::string &name : preview.material_names) {
      details.push_back(QString::fromStdString(name));
    }
  }
  if (!preview.affected_scenes.empty()) {
    details.push_back(text("ui.material_browser.delete.scenes", "References in these scenes will become Default Material:"));
    for (const auto &scene : preview.affected_scenes) {
      details.push_back(
          material_deletion_scene_label(scene, localization_));
    }
  }
  if (preview.discards_material_draft) {
    details.push_back(text("ui.material_browser.delete.draft", "The open material draft will be closed and its unsaved changes discarded."));
  }
  details.push_back(text("ui.material_browser.delete.permanent", "Deletion is permanent. Existing scene undo history remains available; restored references to deleted materials use Default Material."));
  const auto choice = material_browser_import_message_box_->present({
      .icon = QMessageBox::Warning,
      .object_name = QStringLiteral("material_browser_delete_message_box"),
      .title = text("ui.material_browser.delete.title", "Delete Material Folder"),
      .text = text("ui.material_browser.delete.question", "Delete folder \"%1\" and its materials?").arg(folder),
      .informative_text = details.join(QLatin1Char('\n')),
      .buttons = {{
          .button = QtRetainedMessageBoxButton::Primary,
          .object_name = QStringLiteral("materialBrowserDeleteMessageBox.delete"),
          .label = text("ui.material_browser.delete.confirm", "Delete and Replace with Default"),
          .role = QMessageBox::DestructiveRole,
      }, {
          .button = QtRetainedMessageBoxButton::Secondary,
          .object_name = QStringLiteral("materialBrowserDeleteMessageBox.cancel"),
          .label = text("ui.action.cancel", "Cancel"),
          .role = QMessageBox::RejectRole,
      }},
      .default_button = QtRetainedMessageBoxButton::Secondary,
      .escape_button = QtRetainedMessageBoxButton::Secondary,
      .close_button = QtRetainedMessageBoxButton::Secondary,
  });
  if (choice != QtRetainedMessageBoxButton::Primary ||
      !deletion_context_is_current()) {
    return;
  }
  const auto result = editor_->project_assets().delete_material_folder(
      folder.toStdString(), preview.revision);
  if (!deletion_context_is_current()) {
    return;
  }
  if (result.succeeded()) {
    const auto removed = [&preview](ProjectAssetId id) {
      return std::ranges::find(preview.material_ids, id) !=
             preview.material_ids.end();
    };
    if (std::ranges::any_of(material_clipboard_.material_ids, removed)) {
      material_clipboard_ = {};
      if (material_browser_window_ != nullptr) {
        material_browser_window_->set_material_paste_available(false);
      }
    }
  }
  finish_material_folder_operation(result);
}

void UISystem::import_material_textures(
    QtMaterialBrowserWindow &source, QString destination_folder) {
  if (editor_ == nullptr ||
      !editor_->project().has_project() || material_browser_project_generation_ !=
          material_browser_project_generation_signal_->load(std::memory_order_acquire)) {
    return;
  }

  PlatformFileDialogOptions options;
  const auto generation = runtime_binding_generation_;
  const WId browser_window_id = source.window()->winId();
  options.owner_window_handle = reinterpret_cast<void *>(
      static_cast<std::uintptr_t>(browser_window_id));
  options.initial_directory = editor_->project().root() / "Textures";
  const std::optional<std::vector<std::filesystem::path>> selected =
      platform_open_texture_files_dialog(options);
  if (!selected.has_value() || selected->empty() ||
      generation != runtime_binding_generation_ || editor_ == nullptr) {
    return;
  }

  const QuaderApiResult<QuaderProjectMaterialImportReport> result =
      editor_->project_assets().import_textures_as_materials(
          *selected, destination_folder.toStdString());
  if (generation != runtime_binding_generation_) {
    return;
  }
  finish_material_texture_import(result);
}

void UISystem::finish_material_texture_import(
    const QuaderApiResult<QuaderProjectMaterialImportReport> &result) {
  const auto generation = runtime_binding_generation_;
  if (result.changed()) {
    refresh_project_workspace();
    if (generation != runtime_binding_generation_) return;
    refresh_asset_browser();
  }
  const std::size_t imported_count = result.value().imported.size();
  const std::size_t failed_count = result.value().failed.size();

  const QString status = [&]() {
    if (!result.ok) {
      return QString::fromStdString(localization_.text(
          "ui.material_browser.import.status.failed",
          "Texture import failed."));
    }
    if (failed_count == 0u) {
      return QString::fromStdString(localization_.text(
          "ui.material_browser.import.status.success",
                 "Materials imported: %1."))
          .arg(static_cast<qulonglong>(imported_count));
    }
    return QString::fromStdString(localization_.text(
               "ui.material_browser.import.status.with_failures",
               "Materials imported: %1. Failed: %2."))
        .arg(static_cast<qulonglong>(imported_count))
        .arg(static_cast<qulonglong>(failed_count));
  }();
  present_status_result(
      {.ok = result.ok() && failed_count == 0u, .message = status.toStdString()});

  if (result.ok && failed_count == 0u) {
    return;
  }
  if (material_browser_import_message_box_ == nullptr) {
    return;
  }

  constexpr std::size_t kVisibleFailureLimit = 10u;
  QStringList failure_lines;
  const std::size_t visible_failure_count =
      std::min(failed_count, kVisibleFailureLimit);
  failure_lines.reserve(static_cast<qsizetype>(visible_failure_count + 1u));
  for (std::size_t index = 0u; index < visible_failure_count; ++index) {
    const QuaderProjectMaterialImportFailure &failure =
        result.value().failed[index];
    QString file_name =
        QString::fromStdWString(failure.source_path.filename().wstring());
    if (file_name.isEmpty()) {
      file_name = QString::fromStdWString(failure.source_path.wstring());
    }
    failure_lines.push_back(
        QStringLiteral("%1: %2")
            .arg(file_name, QString::fromStdString(failure.error_message)));
  }
  if (failed_count > visible_failure_count) {
    failure_lines.push_back(
        QString::fromStdString(localization_.text(
            "ui.material_browser.import.warning.remaining",
            "...and %1 more."))
            .arg(static_cast<qulonglong>(failed_count -
                                        visible_failure_count)));
  }
  if (!result.ok && !result.message.empty()) {
    failure_lines.push_back(QString::fromStdString(result.message));
  }

  (void)material_browser_import_message_box_->present(
      QtRetainedMessageBoxPresentation{
          .icon = QMessageBox::Warning,
          .object_name =
              QStringLiteral("material_browser_import_warning_message_box"),
          .title = QString::fromStdString(localization_.text(
              "ui.material_browser.import.warning.title",
              "Texture Import Warnings")),
          .text = status,
          .informative_text = failure_lines.join(QLatin1Char('\n')),
          .buttons =
              {{
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name = QStringLiteral(
                      "materialBrowserImportWarningMessageBox.ok"),
                  .label = QString::fromStdString(
                      localization_.text("ui.action.ok", "OK")),
                  .role = QMessageBox::AcceptRole,
              }},
          .default_button = QtRetainedMessageBoxButton::Primary,
          .escape_button = QtRetainedMessageBoxButton::Primary,
          .close_button = QtRetainedMessageBoxButton::Primary,
      });
}

bool UISystem::panel_visible(UIPanelId id) const {
  return id == UIPanelId::Viewport ? true : panels_.is_visible(id);
}

bool UISystem::panel_presented(UIPanelId id) const {
  return panels_.is_presented(id);
}

void UISystem::set_panel_visible(UIPanelId id, bool visible) {
  if (id != UIPanelId::AssetBrowser || main_window_ == nullptr) {
    (void)panels_.set_visible(id, visible);
    return;
  }
  if (visible) {
    // Retained rows are ready before exposure. Physical discovery waits for
    // the dock's first paint, keeping the shortcut and native layout immediate.
    const bool revealing = !panel_presented(id);
    refresh_asset_browser(false, true);
    if (revealing) asset_browser_discovery_pending_ = true;
  } else {
    asset_browser_discovery_pending_ = false;
  }
  const bool animated = main_window_->isAnimated();
  const bool updates_enabled = main_window_->updatesEnabled();
  const bool composed_deferred_layout = render_system_ != nullptr &&
      render_system_->uses_composited_vulkan();
  const bool suspend_updates = updates_enabled && !composed_deferred_layout;
  // Native child panes need the uncovered footprint held until their clips
  // settle. Composed panes queue one paint after layout; disabling the entire
  // window would invalidate every retained widget when updates resume.
  if (suspend_updates) main_window_->setUpdatesEnabled(false);
  main_window_->setAnimated(false);
  (void)panels_.set_visible(id, visible);
  if (visible) {
    if (QDockWidget *dock = panels_.dock(id)) {
      // Show settles child metrics before the dock's explicit minimum is read.
      if (QLayout *layout = dock->layout()) (void)layout->activate();
      dock->raise();
    }
  }
  if (QLayout *layout = main_window_->layout()) (void)layout->activate();
  main_window_->setAnimated(animated);
  if (suspend_updates) main_window_->setUpdatesEnabled(true);
  refresh_asset_thumbnails();
}

} // namespace quader
