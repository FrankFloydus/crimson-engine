////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_editor_api.hpp"
#include "api/quader_command_ids.hpp"
#include "app/app.hpp"
#include "core/release_features.hpp"
#include "editor/session/project_session.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_vulkan_composition_widget.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QAction>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QImage>
#include <QIcon>
#include <QKeyEvent>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPixmap>
#include <QScreen>
#include <QStyle>
#include <QStyleFactory>
#include <QTableView>
#include <QTimer>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace {

/** Restores the caller's settings location after the isolated native App exits. */
class FlyPacingProfile final {
public:
  explicit FlyPacingProfile(const std::filesystem::path &directory)
      : previous_(qgetenv("LOCALAPPDATA")), existed_(qEnvironmentVariableIsSet("LOCALAPPDATA")) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    active_ = !error && qputenv("LOCALAPPDATA", QByteArray::fromStdString(directory.string()));
  }
  ~FlyPacingProfile() {
    if (existed_) qputenv("LOCALAPPDATA", previous_);
    else qunsetenv("LOCALAPPDATA");
  }
  [[nodiscard]] bool active() const { return active_; }
private:
  QByteArray previous_;
  bool existed_ = false;
  bool active_ = false;
};

/** Rejects desktop pointer motion inside this process while preserving capture loss. */
class FlyPacingPointerIsolation final : public QObject, public QAbstractNativeEventFilter {
public:
  ~FlyPacingPointerIsolation() override { end(); }

  void begin(QWidget &host) {
    host_ = &host;
    native_window_ = reinterpret_cast<HWND>(host.effectiveWinId());
    active_ = true;
    QCoreApplication::instance()->installEventFilter(this);
    prioritize_native_filter();
  }

  void prioritize_native_filter() {
    // Qt visits the most recently installed filter first. The production raw
    // filter is registered when App accepts RMB and establishes mouse capture.
    if (active_) QCoreApplication::instance()->installNativeEventFilter(this);
  }

  void end() {
    if (!active_) return;
    active_ = false;
    QCoreApplication::instance()->removeNativeEventFilter(this);
    QCoreApplication::instance()->removeEventFilter(this);
    host_.clear();
    native_window_ = nullptr;
  }

  [[nodiscard]] std::size_t raw_messages() const { return raw_messages_; }
  [[nodiscard]] std::size_t mouse_moves() const { return mouse_moves_; }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (active_ && watched == host_ && event->type() == QEvent::MouseMove) {
      ++mouse_moves_;
      return true;
    }
    return QObject::eventFilter(watched, event);
  }

  bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override {
    const auto *native_message = static_cast<const MSG *>(message);
    if (!active_ || native_message->hwnd != native_window_ || native_message->message != WM_INPUT)
      return false;
    ++raw_messages_;
    // Foreground raw input requires DefWindowProc cleanup even when Qt and the
    // production relative-pointer filter deliberately do not consume its data.
    *result = GET_RAWINPUT_CODE_WPARAM(native_message->wParam) == RIM_INPUT
        ? DefWindowProcW(native_message->hwnd, native_message->message,
                         native_message->wParam, native_message->lParam)
        : 0;
    return true;
  }

private:
  QPointer<QWidget> host_;
  HWND native_window_ = nullptr;
  bool active_ = false;
  std::size_t raw_messages_ = 0u;
  std::size_t mouse_moves_ = 0u;
};

/** Keeps monotonic delivery samples in memory so CSV writes cannot pace the hold. */
class FlyPacingSamples final : public QObject {
public:
  /** Separates App timer delivery from root composition and synthetic repeat input. */
  enum class Kind { AppTimer, RootSubmitted, PaneSubmitted, Observer, AutoRepeat };
  /** One callback's timestamp and its last accepted semantic navigation state. */
  struct Sample {
    Kind kind;
    qint64 nanoseconds;
    bool fly_active;
  };

  explicit FlyPacingSamples(QObject *parent) : QObject(parent) {
    samples.reserve(250000u);
    clock.start();
  }

  void record(Kind kind) {
    if (!holding || panel == nullptr) return;
    const bool active = panel->command_input_context().fly_navigation_active;
    if (active) active_seen = true;
    else if (active_seen) ++inactive_samples;
    samples.push_back({kind, clock.nsecsElapsed() - hold_begin, active});
  }

  void write(const std::filesystem::path &directory) const {
    std::ofstream csv(directory / "fly-pacing.csv");
    csv << "event,elapsed_ms,interval_ms,fly_active\n";
    std::array<std::optional<qint64>, 5> previous{};
    for (const auto &sample : samples) {
      const auto index = static_cast<std::size_t>(sample.kind);
      csv << name(sample.kind) << ',' << static_cast<double>(sample.nanoseconds) / 1000000.0 << ',';
      if (previous[index]) csv << static_cast<double>(sample.nanoseconds - *previous[index]) / 1000000.0;
      csv << ',' << sample.fly_active << '\n';
      previous[index] = sample.nanoseconds;
    }
    for (const auto kind : {Kind::AppTimer, Kind::RootSubmitted, Kind::PaneSubmitted,
                            Kind::Observer, Kind::AutoRepeat}) {
      std::vector<double> intervals;
      std::optional<qint64> last;
      for (const auto &sample : samples) if (sample.kind == kind) {
        if (last) intervals.push_back(static_cast<double>(sample.nanoseconds - *last) / 1000000.0);
        last = sample.nanoseconds;
      }
      if (intervals.empty()) continue;
      std::sort(intervals.begin(), intervals.end());
      const auto quantile = [&](double fraction) {
        return intervals[static_cast<std::size_t>(fraction * static_cast<double>(intervals.size() - 1u))];
      };
      std::cout << "FLY_PACING event=" << name(kind) << " intervals=" << intervals.size()
                << " p50_ms=" << quantile(0.5) << " p95_ms=" << quantile(0.95)
                << " p99_ms=" << quantile(0.99) << " max_ms=" << intervals.back();
      for (const double threshold : {25.0, 33.333, 50.0, 100.0}) {
        std::cout << " over_" << threshold << "ms="
                  << std::count_if(intervals.begin(), intervals.end(),
                       [threshold](double value) { return value > threshold; });
      }
      std::cout << std::endl;
    }
  }

  [[nodiscard]] std::size_t count(Kind kind) const {
    return static_cast<std::size_t>(std::count_if(samples.begin(), samples.end(),
        [kind](const Sample &sample) { return sample.kind == kind; }));
  }

  QPointer<quader::QtViewportPanel> panel;
  QElapsedTimer clock;
  qint64 hold_begin = 0;
  bool holding = false;
  bool active_seen = false;
  std::size_t inactive_samples = 0u;
  std::vector<Sample> samples;
  std::function<void()> after_app_timer;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    auto *timer = qobject_cast<QTimer *>(watched);
    // App owns an unparented precise frame timer. Fixture timers are parented.
    if (event->type() == QEvent::Timer && timer != nullptr &&
        timer->parent() == nullptr && timer->timerType() == Qt::PreciseTimer) {
      if (app_timer_ == nullptr) {
        app_timer_ = timer;
        QObject::connect(timer, &QTimer::timeout, this, [this] {
          if (after_app_timer) after_app_timer();
        });
      }
      record(Kind::AppTimer);
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<QTimer> app_timer_;

  static const char *name(Kind kind) {
    switch (kind) {
    case Kind::AppTimer: return "app_timer";
    case Kind::RootSubmitted: return "root_submitted";
    case Kind::PaneSubmitted: return "pane_submitted";
    case Kind::Observer: return "observer";
    case Kind::AutoRepeat: return "autorepeat";
    }
    return "unknown";
  }
};

} // namespace

TEST(QtFlyNavigationPacingTests, RealAppHeldForwardHasMeasuredPresentationCadence) {
  if (qEnvironmentVariableIntValue("QUADER_NATIVE_FLY_PACING") != 1) {
    GTEST_SKIP() << "Set QUADER_NATIVE_FLY_PACING=1 for the 15-second native RMB+W pacing sample.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QString vsync = qEnvironmentVariable("QUADER_NATIVE_FLY_VSYNC", QStringLiteral("1"));
  ASSERT_TRUE(vsync == QStringLiteral("0") || vsync == QStringLiteral("1"));
  const bool show_info = qEnvironmentVariableIntValue("QUADER_NATIVE_FLY_SHOW_INFO") == 1;
  const auto identity = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const auto profile = std::filesystem::temp_directory_path() / ("quader-fly-pacing-" + identity);
  FlyPacingProfile isolated(profile);
  ASSERT_TRUE(isolated.active());
  const QString requested_output = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  const auto output = requested_output.isEmpty() ? profile / "evidence"
      : std::filesystem::path(requested_output.toStdWString());
  ASSERT_TRUE(std::filesystem::create_directories(output) || std::filesystem::is_directory(output));

  int argc = 1;
  char application_name[] = "quader-native-fly-pacing";
  char *argv[] = {application_name, nullptr};
  QLocale::setDefault(QLocale::c());
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setApplicationName(QStringLiteral("Quader"));
  application.setOrganizationName(QStringLiteral("Quader"));
  QStyle *style = QStyleFactory::create(QStringLiteral("Fusion"));
  ASSERT_NE(style, nullptr);
  QApplication::setStyle(style);
  quader::apply_qt_obsidian_application_theme(style->standardPalette());
  const QIcon application_icon(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons/quader.ico"));
  ASSERT_FALSE(application_icon.isNull());
  application.setWindowIcon(application_icon);
  const auto executable = std::filesystem::path(QCoreApplication::applicationFilePath().toStdWString());
  const auto resources = executable.parent_path();
  const auto project_root = profile / "Fly Project";
  std::filesystem::path project_manifest;
  quader::ViewportCameraPose initial_pose{
      .eye = {0, 2, 45}, .target = {0, 0, 0}, .up = {0, 1, 0}};
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  {
    quader::ProjectSession project;
    project.set_resource_root(resources);
    quader::QuaderEditorApi editor(project);
    ASSERT_TRUE(editor.project().create(project_root).ok());
    project_manifest = editor.project().manifest_path();
    ASSERT_TRUE(editor.viewports().set_layout(quader::ViewportLayoutId::Single).ok());
    ASSERT_TRUE(editor.viewports().set_shading_mode(
        quader::ViewportPaneId::Perspective, quader::QuaderShadingMode::Shaded).ok());
    ASSERT_TRUE(editor.modeling().create_box(
        {.min = {-3, -3, -3}, .max = {3, 3, 3}}, "Fly pacing cube").ok());
    ASSERT_TRUE(project.active_scene()->session->restore_persisted_viewport_camera_pose(initial_pose));
    initial_pose = project.active_scene()->session->persisted_viewport_camera_pose();
    const auto scene = editor.scene_documents().active_scene_id();
    ASSERT_TRUE(scene.has_value());
    const auto path = quader::ProjectRelativePath::parse_portable("Fly.qdr");
    ASSERT_TRUE(path.ok());
    ASSERT_TRUE(editor.scene_documents().save_as(*scene, path.value()).ok());
  }
  const auto settings_paths = quader::UISettings::resolve_paths(resources);
  auto settings = quader::UISettings::defaults();
  settings.set_interface_scale(1.0f);
  settings.set_vsync_enabled(vsync == QStringLiteral("1"));
  settings.set_panel_visible(quader::UIPanelId::AssetBrowser, false);
  ASSERT_TRUE(settings.save(settings_paths.settings_file));
  auto viewport_settings = quader::ViewportSettings::defaults();
  ASSERT_TRUE(viewport_settings.set_float_value(quader::ViewportFloatSettingId::DefaultFlyNavigationSpeed, 1.0f));
  (void)viewport_settings.set_bool_value(quader::ViewportBoolSettingId::ShowInfo, show_info);
  ASSERT_TRUE(viewport_settings.save(settings_paths.viewport_settings_file));

  quader::App app;
  ASSERT_TRUE(app.initialize(application, executable));
  std::cout << "FLY_PACING_SETUP profile=" << profile.string()
            << " executable=" << executable.string() << " output=" << output.string() << std::endl;
  FlyPacingSamples samples(&application);
  FlyPacingPointerIsolation pointer_isolation;
  application.installEventFilter(&samples);
  QPointer<quader::QtVulkanCompositionWidget> host;
  QPointer<quader::QtVulkanCompositionWidget> anchor;
  QTimer observer(&application), autorepeat(&application), deadline(&application);
  observer.setTimerType(Qt::PreciseTimer);
  observer.setInterval(5);
  autorepeat.setTimerType(Qt::PreciseTimer);
  autorepeat.setInterval(33);
  deadline.setSingleShot(true);
  bool project_requested = false, completed = false, timed_out = false, arming = false;
  bool capture_filter_prioritized = false;
  bool pose_save_requested = false;
  qint64 ready_since = -1, released_at = -1;
  qint64 last_wait_log = -1;
  std::string last_wait_reason;
  QImage before;
  samples.after_app_timer = [&] {
    if (!arming || capture_filter_prioritized || samples.panel == nullptr) return;
    const auto *surface = samples.panel->surface(quader::ViewportPaneId::Perspective);
    if (surface != nullptr && surface->pointer_captured()) {
      pointer_isolation.prioritize_native_filter();
      capture_filter_prioritized = true;
    }
  };
  const auto trace_wait = [&](const char *reason, QMainWindow *current,
                              quader::QtViewportPanel *panel = nullptr) {
    const qint64 now = samples.clock.elapsed();
    if (last_wait_reason == reason && last_wait_log >= 0 && now - last_wait_log < 1000) return;
    last_wait_reason = reason;
    last_wait_log = now;
    std::cout << "FLY_PACING_PHASE elapsed_ms=" << now << " phase=" << reason
              << " project_requested=" << project_requested;
    if (current != nullptr) {
      std::cout << " title=" << current->windowTitle().toStdString()
                << " root=" << current->width() << 'x' << current->height();
      if (auto *recent = current->findChild<QTableView *>(QStringLiteral("projectHome.recentProjects.view")))
        std::cout << " recent_rows=" << recent->model()->rowCount() << " recent_visible=" << recent->isVisible();
      if (auto *cover = current->findChild<QWidget *>(QStringLiteral("quaderLoadingCover")))
        std::cout << " loading_cover_visible=" << cover->isVisible();
    }
    if (panel != nullptr) {
      std::cout << " panel_visible=" << panel->isVisible()
                << " startup_blocked=" << panel->startup_presentation_blocked()
                << " loading_blocked=" << panel->loading_presentation_blocked()
                << " loading_presented=" << panel->loading_frame_presented()
                << " fly=" << panel->command_input_context().fly_navigation_active;
      if (auto *surface = panel->surface(quader::ViewportPaneId::Perspective))
        std::cout << " surface_ready=" << surface->ready_to_render()
                  << " captured=" << surface->pointer_captured();
    }
    std::cout << std::endl;
  };
  const auto key = [&](QEvent::Type type, bool repeat) {
    if (host == nullptr) return;
    QKeyEvent event(type, Qt::Key_W, Qt::NoModifier, QStringLiteral("w"), repeat, 1);
    QCoreApplication::sendEvent(host, &event);
  };
  const auto button = [&](QEvent::Type type) {
    if (host == nullptr) return;
    const QPointF local(host->rect().center());
    const QPointF global(host->mapToGlobal(local.toPoint()));
    QMouseEvent event(type, local, global, Qt::RightButton,
        type == QEvent::MouseButtonPress ? Qt::RightButton : Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(host, &event);
  };
  const auto release = [&] {
    arming = false;
    samples.holding = false;
    autorepeat.stop();
    key(QEvent::KeyRelease, false);
    button(QEvent::MouseButtonRelease);
    pointer_isolation.end();
  };
  QObject::connect(&autorepeat, &QTimer::timeout, &application, [&] {
    if (!samples.holding || samples.clock.nsecsElapsed() - samples.hold_begin < 400000000) return;
    samples.record(FlyPacingSamples::Kind::AutoRepeat);
    key(QEvent::KeyRelease, true);
    key(QEvent::KeyPress, true);
  });
  QObject::connect(&deadline, &QTimer::timeout, &application, [&] {
    timed_out = true;
    std::cout << "FLY_PACING_DEADLINE phase=" << last_wait_reason
              << " project_requested=" << project_requested << " arming=" << arming
              << " holding=" << samples.holding << std::endl;
    if (samples.holding || arming) release();
    for (auto *widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("quaderMainWindow") || !widget->isVisible()) continue;
      const auto root_image = widget->grab();
      EXPECT_TRUE(root_image.save(QString::fromStdWString((output / "deadline-root.png").wstring())));
      if (auto *pane = dynamic_cast<quader::QtVulkanCompositionWidget *>(
              widget->findChild<QWidget *>(QStringLiteral("viewportSurfaceContainer_perspective")))) {
        const auto pane_image = pane->grabFramebuffer();
        std::cout << "FLY_PACING_DEADLINE pane_image_null=" << pane_image.isNull() << std::endl;
        if (!pane_image.isNull())
          EXPECT_TRUE(pane_image.save(QString::fromStdWString((output / "deadline-pane.png").wstring())));
      }
    }
    application.quit();
  });
  QObject::connect(&observer, &QTimer::timeout, &application, [&] {
    if (arming) {
      trace_wait("arming_rmb", host != nullptr ? qobject_cast<QMainWindow *>(host->window()) : nullptr,
                 samples.panel);
      if (samples.panel == nullptr || !samples.panel->command_input_context().fly_navigation_active) return;
      pointer_isolation.prioritize_native_filter();
      EXPECT_TRUE(capture_filter_prioritized);
      arming = false;
      samples.hold_begin = samples.clock.nsecsElapsed();
      samples.holding = true;
      key(QEvent::KeyPress, false);
      autorepeat.start();
      std::cout << "FLY_PACING_BEGIN profile="
                << quader::product_release_label(quader::current_product_release())
                << " vsync=" << vsync.toStdString() << " show_info=" << show_info
                << " duration_ms=15000 fly_speed=1 output="
                << output.string();
      if (const QScreen *screen = host->screen()) {
        std::cout << " screen=" << screen->name().toStdString()
                  << " refresh_hz=" << screen->refreshRate()
                  << " dpr=" << screen->devicePixelRatio();
      }
      std::cout << " pane=" << host->width() << 'x' << host->height()
                << " synchronized=" << anchor->presentation_is_synchronized()
                << " present_mode=" << anchor->resolved_present_mode() << std::endl;
      return;
    }
    if (samples.holding) {
      samples.record(FlyPacingSamples::Kind::Observer);
      if (samples.clock.nsecsElapsed() - samples.hold_begin >= 15000000000LL) {
        release();
        released_at = samples.clock.nsecsElapsed();
      }
      return;
    }
    if (released_at >= 0) {
      if (samples.clock.nsecsElapsed() - released_at < 350000000) return;
      ASSERT_NE(host, nullptr);
      const QImage after = host->grabFramebuffer();
      EXPECT_FALSE(after.isNull());
      EXPECT_NE(before, after);
      EXPECT_TRUE(before.save(QString::fromStdWString((output / "before.png").wstring())));
      EXPECT_TRUE(after.save(QString::fromStdWString((output / "after.png").wstring())));
      QAction *save_action = nullptr;
      const auto command_id = quader::quader_command::kSceneDocumentSave.string();
      for (auto *action : host->window()->findChildren<QAction *>()) {
        if (action->property("quaderCommandId").toString() ==
            QString::fromUtf8(command_id.data(), static_cast<qsizetype>(command_id.size()))) {
          save_action = action;
          break;
        }
      }
      ASSERT_NE(save_action, nullptr);
      auto *menu = qobject_cast<QMenu *>(save_action->parent());
      ASSERT_NE(menu, nullptr);
      ASSERT_TRUE(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
      ASSERT_TRUE(save_action->isEnabled());
      save_action->trigger();
      pose_save_requested = true;
      completed = true;
      application.quit();
      return;
    }
    QMainWindow *current = nullptr;
    for (auto *widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("quaderMainWindow") && widget->isVisible())
        current = qobject_cast<QMainWindow *>(widget);
    }
    if (current == nullptr) { trace_wait("waiting_visible_root", nullptr); return; }
    // A submitted pane can precede the cover's mandatory two-second display.
    // Its application event filter still consumes RMB until the cover hides.
    if (auto *cover = current->findChild<QWidget *>(QStringLiteral("quaderLoadingCover"));
        cover != nullptr && cover->isVisible()) {
      trace_wait("waiting_loading_cover", current);
      return;
    }
    if (!project_requested) {
      auto *recent = current->findChild<QTableView *>(QStringLiteral("projectHome.recentProjects.view"));
      if (recent == nullptr || !recent->isVisible() || recent->model()->rowCount() != 1) {
        trace_wait("waiting_recent_project", current);
        return;
      }
      // Match the existing real-App fixture: finalize the ordinary native
      // placement before project startup latches its first-paint geometry.
      current->showMaximized();
      QApplication::setActiveWindow(current);
      project_requested = true;
      trace_wait("opening_recent_project", current);
      EXPECT_TRUE(QMetaObject::invokeMethod(recent, "activated", Qt::DirectConnection,
          Q_ARG(QModelIndex, recent->model()->index(0, 0))));
      return;
    }
    auto *panel = dynamic_cast<quader::QtViewportPanel *>(
        current->findChild<QWidget *>(QStringLiteral("viewportWorkspace")));
    if (panel == nullptr || !panel->isVisible() || panel->startup_presentation_blocked() ||
        panel->loading_presentation_blocked() || !panel->loading_frame_presented()) {
      trace_wait("waiting_workspace_presentation", current, panel);
      return;
    }
    auto *surface = panel->surface(quader::ViewportPaneId::Perspective);
    if (surface == nullptr || !surface->ready_to_render()) {
      trace_wait("waiting_surface", current, panel);
      return;
    }
    auto *pane = dynamic_cast<quader::QtVulkanCompositionWidget *>(
        current->findChild<QWidget *>(QStringLiteral("viewportSurfaceContainer_perspective")));
    auto *root_anchor = dynamic_cast<quader::QtVulkanCompositionWidget *>(
        current->findChild<QWidget *>(QStringLiteral("viewportCompositionAnchor")));
    if (pane == nullptr || root_anchor == nullptr || root_anchor->shared_context() == nullptr) {
      trace_wait("waiting_composition_context", current, panel);
      return;
    }
    trace_wait("settling_presented_workspace", current, panel);
    if (ready_since < 0) { ready_since = samples.clock.nsecsElapsed(); return; }
    if (samples.clock.nsecsElapsed() - ready_since < 1000000000) return;
    host = pane;
    anchor = root_anchor;
    samples.panel = panel;
    QObject::connect(anchor, &QRhiWidget::frameSubmitted, &samples, [&] {
      // Qt emits this after the root backing store's swapchain endFrame.
      samples.record(FlyPacingSamples::Kind::RootSubmitted);
    });
    QObject::connect(host, &QRhiWidget::frameSubmitted, &samples, [&] {
      samples.record(FlyPacingSamples::Kind::PaneSubmitted);
    });
    before = host->grabFramebuffer();
    ASSERT_FALSE(before.isNull());
    QApplication::setActiveWindow(current);
    host->setFocus(Qt::OtherFocusReason);
    const QPointF center(host->rect().center());
    QEnterEvent enter(center, center, QPointF(host->mapToGlobal(center.toPoint())));
    QFocusEvent focus(QEvent::FocusIn, Qt::OtherFocusReason);
    QCoreApplication::sendEvent(host, &enter);
    QCoreApplication::sendEvent(host, &focus);
    // Give the real App one turn to establish RMB navigation before W can
    // otherwise be interpreted as the ordinary Move command.
    pointer_isolation.begin(*host);
    arming = true;
    button(QEvent::MouseButtonPress);
  });
  deadline.start(45000);
  observer.start();
  EXPECT_EQ(app.run(), 0);
  observer.stop();
  autorepeat.stop();
  deadline.stop();
  if (samples.holding || arming) release();
  application.removeEventFilter(&samples);
  pointer_isolation.end();
  std::cout << "FLY_PACING_POINTER_ISOLATION raw_messages=" << pointer_isolation.raw_messages()
            << " mouse_moves=" << pointer_isolation.mouse_moves()
            << " capture_filter_prioritized=" << capture_filter_prioritized << std::endl;
  samples.write(output);
  EXPECT_FALSE(timed_out);
  EXPECT_TRUE(completed);
  EXPECT_TRUE(samples.active_seen);
  EXPECT_EQ(samples.inactive_samples, 0u);
  EXPECT_GT(samples.count(FlyPacingSamples::Kind::AppTimer), 0u);
  EXPECT_GT(samples.count(FlyPacingSamples::Kind::RootSubmitted), 0u);
  EXPECT_GT(samples.count(FlyPacingSamples::Kind::PaneSubmitted), 0u);
  EXPECT_GT(samples.count(FlyPacingSamples::Kind::AutoRepeat), 0u);
  if (vsync == QStringLiteral("1")) {
    // This simple scene should follow root feedback. Allow occasional watchdog
    // recovery, but reject a second app cadence racing every other composition.
    const auto app_frames = samples.count(FlyPacingSamples::Kind::AppTimer);
    const auto root_frames = samples.count(FlyPacingSamples::Kind::RootSubmitted);
    EXPECT_LE(app_frames, root_frames + root_frames / 10u + 5u);
  }
  app.shutdown();
  ASSERT_TRUE(pose_save_requested);
  quader::ProjectSession reopened;
  reopened.set_resource_root(resources);
  quader::QuaderEditorApi reopened_editor(reopened);
  ASSERT_TRUE(reopened_editor.project().open(project_manifest).ok());
  ASSERT_NE(reopened.active_scene(), nullptr);
  const auto final_pose = reopened.active_scene()->session->persisted_viewport_camera_pose();
  const auto initial_forward = quader::normalize(initial_pose.target - initial_pose.eye);
  const auto final_forward = quader::normalize(final_pose.target - final_pose.eye);
  const auto displacement = final_pose.eye - initial_pose.eye;
  const auto forward_distance = quader::dot(displacement, initial_forward);
  const auto lateral_displacement = displacement - initial_forward * forward_distance;
  std::cout << "FLY_PACING_POSE forward_distance=" << forward_distance
            << " lateral_distance=" << quader::length(lateral_displacement)
            << " direction_dot=" << quader::dot(initial_forward, final_forward)
            << " final_eye=" << final_pose.eye.x << ',' << final_pose.eye.y << ',' << final_pose.eye.z
            << std::endl;
  // These are navigation semantics, not pacing thresholds: a fixed speed-one
  // hold must translate forward without pointer-driven orientation changes.
  EXPECT_NEAR(forward_distance, 15.0f, 0.1f);
  EXPECT_LT(quader::length(lateral_displacement), 0.01f);
  EXPECT_GT(quader::dot(initial_forward, final_forward), 0.99999f);
  EXPECT_LT(quader::length(final_pose.up - initial_pose.up), 0.0001f);
}
