////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/ui_system.hpp"

#include <QApplication>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QStyle>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using namespace quader;

/// Controls one opt-in, visible native Vulkan benchmark run.
struct Options final {
  int warmup = 24;
  int frames = 120;
  int interval_ms = 16;
  int points = 8;
  int meshes = 0;
  int history = 0;
  float scale = 128.0f;
  std::string label = "unspecified";
  std::filesystem::path resources;
  std::filesystem::path output;
};

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

template <typename Result> void require_ok(const Result &result) {
  if (!result.ok()) {
    throw std::runtime_error(std::string(result.error_message()));
  }
}

void require_command(const CommandResult &result) {
  require(result.ok, result.message);
}

Options parse_options(int argc, char **argv) {
  Options options;
  options.resources = std::filesystem::absolute(argv[0]).parent_path();
  for (int i = 1; i < argc; ++i) {
    const std::string argument(argv[i]);
    if (argument == "--help") {
      std::cout << "qdr_curve_viewport_performance [--resources-dir DIR] "
                   "[--output JSON] [--label NAME] [--frames 120] [--warmup 24] "
                   "[--interval-ms 16] [--scale 128] [--points 8] "
                   "[--meshes 0] [--history 0]\n"
                   "Opens the real native shell. Reports CPU-side stage elapsed "
                   "milliseconds, not GPU time or operation-loop FPS.\n";
      std::exit(0);
    }
    require(i + 1 < argc, "Missing option value.");
    const std::string value(argv[++i]);
    if (argument == "--resources-dir") options.resources = value;
    else if (argument == "--output") options.output = value;
    else if (argument == "--label") options.label = value;
    else if (argument == "--frames") options.frames = std::stoi(value);
    else if (argument == "--warmup") options.warmup = std::stoi(value);
    else if (argument == "--interval-ms") options.interval_ms = std::stoi(value);
    else if (argument == "--scale") options.scale = std::stof(value);
    else if (argument == "--points") options.points = std::stoi(value);
    else if (argument == "--meshes") options.meshes = std::stoi(value);
    else if (argument == "--history") options.history = std::stoi(value);
    else throw std::runtime_error("Unknown option: " + argument);
  }
  require(options.frames > 0 && options.frames <= 100000 &&
              options.warmup >= 0 && options.warmup <= 100000 &&
              options.interval_ms >= 0 && options.interval_ms <= 1000 &&
              options.points >= 3 && options.points <= 1024 &&
              options.meshes >= 0 && options.meshes <= 10000 &&
              options.history >= 0 && options.history <= 10000 &&
              std::isfinite(options.scale) && options.scale > 0.0f,
          "Option value is outside the supported range.");
  return options;
}

/// Redirects only this diagnostic process's application preferences to a temp.
class ConfigIsolation final {
public:
  explicit ConfigIsolation(const QString &directory)
      : previous_(qgetenv("LOCALAPPDATA")), existed_(qEnvironmentVariableIsSet("LOCALAPPDATA")) {
    require(qputenv("LOCALAPPDATA", directory.toLocal8Bit()),
            "Cannot isolate benchmark settings.");
  }
  ~ConfigIsolation() {
    if (existed_) (void)qputenv("LOCALAPPDATA", previous_);
    else (void)qunsetenv("LOCALAPPDATA");
  }
private:
  QByteArray previous_;
  bool existed_ = false;
};

constexpr std::array<std::string_view, 9> kStages{
    "qt_events", "ui_refresh", "fixture_action", "viewport_input_mutation",
    "scene_sync", "scene_settings", "viewport_prepare", "viewport_render",
    "total_cpu_frame"};

/// Stores one actual App-equivalent frame, including submission provenance.
struct FrameSample final {
  std::array<double, kStages.size()> milliseconds{};
  std::size_t submissions = 0;
};

/// Retains all attempted and submitted frame observations for one gesture.
struct Scenario final {
  std::string name;
  std::vector<FrameSample> frames;
  std::size_t anchors = 0;
  std::size_t display_samples_before = 0;
  std::size_t display_samples_after = 0;
  bool validated = false;
};

std::size_t display_samples(const modeling::CurveDocument &document) {
  // Called only outside timed frames, with the renderer's default tolerance.
  const auto samples = document.sample();
  require_ok(samples);
  return samples.value().size();
}

QVec3 native(modeling::Vec3 point) {
  return {point.x, point.y, point.z};
}

/// Owns the real retained shell, scene adapters, and native-child swapchains.
class Benchmark final {
public:
  Benchmark(const Options &options, const std::filesystem::path &temporary)
      : options_(options), editor_(project_) {
    std::cout << "Initializing native Curve benchmark shell..." << std::endl;
    project_.set_resource_root(options.resources);
    const auto project_root = temporary / "project";
    std::filesystem::create_directories(project_root);
    require_ok(editor_.project().create(project_root));
    require(project_.active_scene() != nullptr, "No benchmark scene was created.");
    session_ = project_.active_scene()->session.get();
    require_ok(editor_.commands().set_keybinding_storage_path({}));
    require(renderer_.initialize(nullptr, options.resources,
                                 {.backend = RendererBackend::Vulkan}),
            "Native Vulkan renderer initialization failed.");
    renderer_.initialize_scene_render_bridge(bridge_, options.resources);
    require(ui_.initialize(renderer_, options.resources), "Native UI initialization failed.");
    ui_.install_runtime_hooks(editor_, session().scene(), session().assets(),
        session().modeling(), session().selection(), session().pivot(),
        session().linear_array(), session().radial_array(),
        session().point_placement(), {}, &session().curve());
    ui_.main_window()->resize(1440, 900);
    ui_.main_window()->setWindowTitle(QStringLiteral("Quader Curve Performance Benchmark"));
    ui_.refresh_project_workspace();
    ui_.show();
    QWidget *widget = ui_.main_window()->findChild<QWidget *>(QStringLiteral("viewportWorkspace"));
    panel_ = dynamic_cast<QtViewportPanel *>(widget);
    require(panel_ != nullptr, "The native viewport panel is missing.");
    surface_ = panel_->surface(ViewportPaneId::Perspective);
    require(surface_ != nullptr, "The native viewport surface is missing.");
    require_ok(editor_.viewports().set_layout(ViewportLayoutId::Single));
    require_ok(editor_.main_viewport().set_view(ViewportView::Top));
    require_ok(editor_.preferences().set_snapping_enabled(false));
    build_fixture();
    const float extent = options.scale;
    session().viewport_workspace().camera(ViewportPaneId::Perspective)
        .focus_bounds({-0.65f * extent, -0.1f * extent, -0.45f * extent},
                      {0.65f * extent, 0.2f * extent, 0.45f * extent}, 1.15f);
    require_ok(editor_.tools().activate(ViewportToolId::Select));
    const auto deadline = Clock::now() + std::chrono::seconds(20);
    std::size_t ready_frames = 0;
    while (ready_frames < 8 && Clock::now() < deadline) {
      const auto sample = frame({});
      ready_frames += sample.submissions != 0u ? 1u : 0u;
      pace();
    }
    require(ready_frames == 8, "Native viewport did not submit startup frames.");
    focus_surface();
    (void)frame({});
  }

  ~Benchmark() {
    ui_.shutdown();
    bridge_.shutdown();
    renderer_.shutdown();
  }

  SceneSession &session() { return *session_; }

  FrameSample frame(const std::function<void()> &action) {
    FrameSample sample;
    const auto begin = Clock::now();
    auto previous = begin;
    const auto mark = [&](std::size_t index) {
      const auto now = Clock::now();
      sample.milliseconds[index] =
          std::chrono::duration<double, std::milli>(now - previous).count();
      previous = now;
    };
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    mark(0);
    ui_.refresh({.elapsed_seconds = static_cast<double>(frame_index_) / 60.0,
                 .frame_index = frame_index_++}, renderer_.frame_performance_stats());
    mark(1);
    if (action) action();
    mark(2);
    ui_.process_viewport_input();
    mark(3);
    require_ok(QuaderRenderApiRuntimeAccess::sync_scene(editor_.render(), bridge_));
    mark(4);
    if (const auto *settings = session().scene().scene_settings(session().scene().scene_root())) {
      renderer_.apply_scene_settings(*settings);
    }
    mark(5);
    ui_.prepare_viewports(bridge_, overlays_);
    mark(6);
    sample.submissions = ui_.render_surfaces(ui_.can_render_viewports());
    mark(7);
    sample.milliseconds[8] =
        std::chrono::duration<double, std::milli>(previous - begin).count();
    return sample;
  }

  void pace() const {
    // Deliberate pacing is outside every reported timing, never counted as FPS.
    if (options_.interval_ms > 0) QThread::msleep(static_cast<unsigned long>(options_.interval_ms));
  }

  QVec2 screen(QVec3 world) const {
    const auto point = session_->viewport_workspace().camera(ViewportPaneId::Perspective)
                           .project_world_point(world);
    require(point.visible, "Fixture control is outside the measured viewport.");
    return point.position;
  }

  void mouse(QEvent::Type type, QVec2 pixels, bool down) {
    const qreal ratio = surface_->devicePixelRatio();
    const QPointF local(pixels.x / ratio, pixels.y / ratio);
    const QPointF global(surface_->mapToGlobal(local.toPoint()));
    const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, local, global, button,
                     down ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(surface_, &event);
    pointer_ = pixels;
  }

  void focus_surface() {
    require(panel_->focus_active_pane(), "Cannot focus the native benchmark pane.");
    const QPointF local(100.0, 100.0);
    const QPointF global(surface_->mapToGlobal(local.toPoint()));
    QEnterEvent enter(local, local, global);
    QFocusEvent focus(QEvent::FocusIn);
    QCoreApplication::sendEvent(surface_, &enter);
    QCoreApplication::sendEvent(surface_, &focus);
  }

  Scenario measure(std::string name, const std::function<void(int)> &action,
                   const modeling::CurveDocument &before) {
    std::cout << "Starting " << name << " (" << before.point_count()
              << " anchors)..." << std::endl;
    Scenario result{.name = std::move(name), .anchors = before.point_count(),
                    .display_samples_before = display_samples(before)};
    for (int i = 0; i < options_.warmup; ++i) {
      (void)frame([&]() { action(i); });
      pace();
    }
    result.frames.reserve(static_cast<std::size_t>(options_.frames));
    for (int i = 0; i < options_.frames; ++i) {
      result.frames.push_back(frame([&]() { action(options_.warmup + i); }));
      pace();
    }
    const auto submitted = std::count_if(result.frames.begin(), result.frames.end(),
        [](const auto &sample) { return sample.submissions != 0u; });
    require(submitted != 0, "Scenario submitted no actual native viewport frames.");
    std::cout << result.name << ": " << submitted << '/' << options_.frames
              << " frames submitted" << std::endl;
    return result;
  }

  std::vector<Scenario> run() {
    std::vector<Scenario> results;
    const auto authored = snapshot();
    auto idle = measure("idle_selected_curve", [](int) {}, authored);
    idle.display_samples_after = display_samples(snapshot());
    idle.validated = snapshot().has_same_geometry(authored);
    results.push_back(std::move(idle));

    begin_draft();
    auto hover = measure("drawing_hover", [&](int i) {
      const float phase = static_cast<float>(i) * 0.17f;
      mouse(QEvent::MouseMove, screen({options_.scale * 0.47f, 0.0f,
          options_.scale * (0.10f + 0.05f * std::sin(phase))}), false);
    }, *session().curve().preview_state().document);
    hover.display_samples_after = display_samples(*session().curve().preview_state().document);
    hover.validated = session().curve().point_count() == static_cast<std::size_t>(options_.points - 1);
    results.push_back(std::move(hover));

    const QVec3 new_anchor = native(fixture_points_.back().position);
    (void)frame([&]() { mouse(QEvent::MouseButtonPress, screen(new_anchor), true); });
    require(session().curve().dragging(), "Draft pointer press did not start a curve gesture.");
    auto draft = measure("draft_handle_drag", [&](int i) {
      const float phase = static_cast<float>(i) * 0.13f;
      mouse(QEvent::MouseMove,
            screen(new_anchor + QVec3{options_.scale * (0.04f + 0.02f * std::cos(phase)),
                                      0.0f, options_.scale * (0.04f + 0.025f * std::sin(phase))}), true);
    }, *session().curve().preview_state().document);
    (void)frame([&]() { mouse(QEvent::MouseButtonRelease, pointer_, false); });
    require(!session().curve().dragging() && session().curve().can_apply(),
            "Draft release did not finish the gesture.");
    const auto draft_document = *session().curve().preview_state().document;
    const auto &last = draft_document.points().back();
    require(length_squared(native(last.outgoing_handle)) > 0.0f &&
                length_squared(native(last.incoming_handle) + native(last.outgoing_handle)) < 1.0e-8f,
            "Draft drag did not preserve symmetric handles.");
    draft.display_samples_after = display_samples(draft_document);
    require_ok(editor_.tools().commit_active_tool_preview());
    const EntityId created = session().curve().preview_state().entity;
    require(session().scene().curve(created) != nullptr, "Apply did not create a scene curve.");
    require_ok(editor_.history().undo());
    require(!session().scene().exists(created), "Draft Apply did not undo as one action.");
    draft.validated = true;
    results.push_back(std::move(draft));
    require_ok(editor_.tools().cancel_active_tool_preview());

    results.push_back(control_drag(CurveControlKind::Anchor, "existing_anchor_drag"));
    results.push_back(control_drag(CurveControlKind::OutgoingHandle, "existing_handle_drag"));
    results.push_back(transform_drag());
    return results;
  }

  QVec2 viewport_size() const { return surface_->render_size_pixels(); }
  double device_pixel_ratio() const { return surface_->devicePixelRatio(); }

private:
  void build_fixture() {
    const float step = options_.scale / static_cast<float>(options_.points - 1);
    for (int i = 0; i < options_.points; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(options_.points - 1);
      fixture_points_.push_back({
          .position = {(t - 0.5f) * options_.scale, 0.0f,
                       0.13f * options_.scale * std::sin(t * 6.2831853f)},
          .incoming_handle = {-0.35f * step, 0.0f, -0.12f * options_.scale},
          .outgoing_handle = {0.35f * step, 0.0f, 0.12f * options_.scale}});
    }
    const auto created = editor_.modeling().create_curve(
        {.points = fixture_points_, .name = "Curve Performance Fixture"});
    require_ok(created);
    curve_ = created.value();
    require_ok(editor_.scene().set_transform(curve_.entity,
        {.position = {0.0f, 0.05f * options_.scale, 0.0f}}));
    for (int i = 0; i < options_.meshes; ++i) {
      const float x = (static_cast<float>(i % 24) / 24.0f - 0.5f) * options_.scale;
      const float z = options_.scale * (0.32f + 0.035f * static_cast<float>(i / 24));
      const float half = options_.scale * 0.01f;
      require_ok(editor_.modeling().create_box(
          {.min = {x - half, -half, z - half}, .max = {x + half, half, z + half}},
          "Background Mesh " + std::to_string(i)));
    }
    require_ok(editor_.selection().select_object(curve_));
    session().history().clear();
    const auto authored = snapshot();
    const auto point_id = authored.points().front().id;
    for (int i = 0; i < options_.history; ++i) {
      auto point = fixture_points_.front();
      point.position.y += options_.scale * (i % 2 == 0 ? 0.01f : -0.01f);
      require_ok(editor_.modeling().set_curve_point(curve_, point_id, point));
    }
    if (options_.history != 0) {
      // One final SDK history edit restores identical measured fixture geometry.
      require_ok(editor_.modeling().set_curve_point(curve_, point_id, fixture_points_.front()));
      require(snapshot().has_same_geometry(authored), "History seeding changed the measured fixture.");
    }
    session().mark_clean();
  }

  modeling::CurveDocument snapshot() {
    const auto result = editor_.modeling().curve_snapshot(curve_);
    require_ok(result);
    return result.value().document;
  }

  void begin_draft() {
    require_ok(editor_.selection().clear());
    require_ok(editor_.tools().activate(kCurveToolId));
    for (int i = 0; i + 1 < options_.points; ++i) {
      const auto &point = fixture_points_[static_cast<std::size_t>(i)];
      require_command(session().curve().begin_point(native(point.position)));
      require_command(session().curve().update_drag(native(point.position) + native(point.outgoing_handle)));
      require_command(session().curve().commit_drag());
    }
    (void)frame({});
    focus_surface();
  }

  Scenario control_drag(CurveControlKind kind, std::string name) {
    require_ok(editor_.selection().select_object(curve_));
    require_ok(editor_.tools().activate(kCurveToolId));
    (void)frame({});
    focus_surface();
    const auto before = snapshot();
    const auto &preview = session().curve().preview_state();
    const QVec3 start = transform_point(preview.world_transform,
        curve_control_local_position(before.points().front(), kind));
    (void)frame([&]() { mouse(QEvent::MouseButtonPress, screen(start), true); });
    require(session().curve().dragging() &&
                session().curve().preview_state().control_selection.active ==
                    CurveControlTarget{before.points().front().id, kind},
            "Pointer did not pick the requested curve control.");
    auto result = measure(std::move(name), [&](int i) {
      const float phase = static_cast<float>(i) * 0.11f;
      mouse(QEvent::MouseMove,
            screen(start + QVec3{options_.scale * (0.04f + 0.025f * std::cos(phase)),
                                 0.0f, options_.scale * (0.03f + 0.02f * std::sin(phase))}), true);
    }, before);
    (void)frame([&]() { mouse(QEvent::MouseButtonRelease, pointer_, false); });
    const auto after = snapshot();
    require(!after.has_same_geometry(before), "Measured control drag did not alter authored controls.");
    result.display_samples_after = display_samples(after);
    require_ok(editor_.history().undo());
    require(snapshot().has_same_geometry(before), "Curve drag undo did not restore authored controls.");
    require_ok(editor_.history().redo());
    require(snapshot().has_same_geometry(after), "Curve drag redo did not restore authored controls.");
    require_ok(editor_.history().undo());
    require_ok(editor_.tools().cancel_active_tool_preview());
    result.validated = true;
    return result;
  }

  Scenario transform_drag() {
    require_ok(editor_.selection().select_object(curve_));
    require_ok(editor_.tools().activate(ViewportToolId::Move));
    (void)frame({});
    const auto before = snapshot();
    const auto transform_before = editor_.query().entity(curve_.entity)->transform;
    const QuaderViewportCamera camera{
        .orthographic = true, .origin = {0.0f, 1000.0f, 0.0f},
        .x_axis = {1.0f, 0.0f, 0.0f}, .y_axis = {0.0f, 0.0f, -1.0f},
        .z_axis = {0.0f, 1.0f, 0.0f}, .viewport_size = {800.0f, 600.0f},
        .orthographic_size = 200.0};
    const QuaderRay ray{.origin = {0.0f, 1000.0f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}};
    const auto begun = editor_.modeling().begin_transform_drag({
        .tool = QuaderTransformTool::Move, .axis = QuaderTransformAxis::X,
        .screen_tip = {10.0f, 0.0f}, .has_world_direction = true,
        .world_direction = {1.0f, 0.0f, 0.0f}, .ray = ray, .camera = camera,
        .grid_size = 1.0, .vertex_snap_radius = 0.0, .grid_snap_enabled = false});
    require_ok(begun);
    require(begun.value().active && begun.value().scene_transform,
            "Curve Move must use the real scene transform drag owner.");
    auto result = measure("scene_transform_move", [&](int i) {
      const float distance = 12.0f + 6.0f * std::sin(static_cast<float>(i) * 0.11f);
      require_ok(editor_.modeling().update_transform_drag({
          .screen_position = {distance, 0.0f}, .ray = ray, .camera = camera,
          .grid_size = 1.0, .vertex_snap_radius = 0.0, .grid_snap_enabled = false}));
    }, before);
    const auto committed = editor_.modeling().commit_transform_drag();
    require_ok(committed);
    require(committed.value().changed, "Scene transform drag did not change the curve transform.");
    require(snapshot().has_same_geometry(before), "Scene Move changed local authored curve geometry.");
    require_ok(editor_.history().undo());
    const auto restored = editor_.query().entity(curve_.entity)->transform;
    require(length_squared(restored.position - transform_before.position) < 1.0e-8f,
            "Scene Move undo did not restore the original translation.");
    result.display_samples_after = display_samples(snapshot());
    result.validated = true;
    return result;
  }

  const Options &options_;
  ProjectSession project_;
  QuaderEditorApi editor_;
  SceneSession *session_ = nullptr;
  RenderSystem renderer_;
  SceneRenderBridge bridge_;
  RenderOverlaySnapshotCache overlays_;
  UISystem ui_;
  QtViewportPanel *panel_ = nullptr;
  QtViewportSurface *surface_ = nullptr;
  QuaderModelingObjectRef curve_;
  std::vector<modeling::CurvePointSpec> fixture_points_;
  QVec2 pointer_{};
  std::uint64_t frame_index_ = 0;
};

std::string quoted(std::string_view input) {
  std::string result = "\"";
  for (char character : input) {
    if (character == '\\' || character == '"') result += '\\';
    if (character == '\n') result += "\\n";
    else if (character == '\r') result += "\\r";
    else result += character;
  }
  return result + '"';
}

void write_summary(std::ostream &output, std::vector<double> values) {
  require(!values.empty(), "A timing summary needs samples.");
  std::sort(values.begin(), values.end());
  const auto percentile = [&](double fraction) {
    const auto index = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size()))) - 1u;
    return values[std::min(index, values.size() - 1u)];
  };
  output << "{\"count\":" << values.size()
         << ",\"mean_ms\":" << std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size())
         << ",\"p50_ms\":" << percentile(0.50)
         << ",\"p95_ms\":" << percentile(0.95)
         << ",\"max_ms\":" << values.back() << '}';
}

void write_report(std::ostream &output, const Options &options,
                  QVec2 size, double device_pixel_ratio,
                  const std::vector<Scenario> &scenarios) {
  output << std::fixed << std::setprecision(6)
         << "{\n\"benchmark\":\"native_curve_viewport\",\n\"schema\":1,\n\"label\":" << quoted(options.label)
         << ",\n\"pipeline\":\"UISystem refresh + native Qt pointer input + scene sync + prepare + Vulkan swapchain submission\","
            "\n\"timing_note\":\"CPU-side elapsed stage time, not GPU time or FPS. Pacing excluded; skipped submissions reported. Pointer mutation is inside viewport_input_mutation. Scene transform mutation is inside fixture_action.\","
         << "\n\"warmup_frames\":" << options.warmup << ",\n\"sample_frames\":" << options.frames
         << ",\n\"pacing_ms\":" << options.interval_ms << ",\n\"curve_span\":" << options.scale
         << ",\n\"curve_anchors\":" << options.points << ",\n\"background_meshes\":" << options.meshes
         << ",\n\"requested_sdk_history_edits\":" << options.history
         << ",\n\"seed_sdk_history_edits_including_restore\":" << (options.history == 0 ? 0 : options.history + 1)
         << ",\n\"viewport_pixels\":[" << size.x << ',' << size.y << "],\n\"device_pixel_ratio\":" << device_pixel_ratio
         << ",\n\"scenarios\":[\n";
  for (std::size_t s = 0; s < scenarios.size(); ++s) {
    const auto &scenario = scenarios[s];
    const auto submitted = std::count_if(scenario.frames.begin(), scenario.frames.end(),
        [](const auto &frame) { return frame.submissions != 0u; });
    output << (s == 0u ? "" : ",\n") << "{\"name\":" << quoted(scenario.name)
           << ",\"validated\":" << (scenario.validated ? "true" : "false")
           << ",\"anchors\":" << scenario.anchors
           << ",\"display_samples_before\":" << scenario.display_samples_before
           << ",\"display_samples_after\":" << scenario.display_samples_after
           << ",\"submitted_frames\":" << submitted
           << ",\"skipped_frames\":" << scenario.frames.size() - static_cast<std::size_t>(submitted)
           << ",\"stages\":{";
    for (std::size_t stage = 0; stage < kStages.size(); ++stage) {
      output << (stage == 0u ? "" : ",") << quoted(kStages[stage]) << ':';
      std::vector<double> values;
      for (const auto &frame : scenario.frames) values.push_back(frame.milliseconds[stage]);
      write_summary(output, std::move(values));
    }
    output << "},\"submitted_total_cpu_frame\":";
    std::vector<double> submitted_values;
    for (const auto &frame : scenario.frames) {
      if (frame.submissions != 0u) submitted_values.push_back(frame.milliseconds.back());
    }
    write_summary(output, std::move(submitted_values));
    output << '}';
  }
  output << "\n]}\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Options options = parse_options(argc, argv);
    QTemporaryDir temporary(QStringLiteral("quader-curve-benchmark-XXXXXX"));
    require(temporary.isValid(), "Cannot create isolated benchmark directory.");
    ConfigIsolation configuration(temporary.path());
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setPalette(QApplication::style()->standardPalette());
    Logger::initialize({.name = "QuaderCurveBenchmark",
        .file_path = (std::filesystem::path(temporary.path().toStdWString()) / "benchmark.log").string(),
        .level = spdlog::level::warn});
    std::vector<Scenario> scenarios;
    QVec2 viewport_size;
    double pixel_ratio = 1.0;
    {
      Benchmark benchmark(options, std::filesystem::path(temporary.path().toStdWString()));
      scenarios = benchmark.run();
      viewport_size = benchmark.viewport_size();
      pixel_ratio = benchmark.device_pixel_ratio();
    }
    for (const auto &scenario : scenarios) require(scenario.validated, "Scenario validation failed.");
    if (!options.output.empty()) {
      if (!options.output.parent_path().empty()) std::filesystem::create_directories(options.output.parent_path());
      std::ofstream output(options.output);
      require(output.is_open(), "Cannot open benchmark JSON output.");
      write_report(output, options, viewport_size, pixel_ratio, scenarios);
      require(output.good(), "Cannot write benchmark JSON output.");
    }
    write_report(std::cout, options, viewport_size, pixel_ratio, scenarios);
    Logger::shutdown();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Curve viewport benchmark failed: " << error.what() << '\n';
    Logger::shutdown();
    return 1;
  }
}
