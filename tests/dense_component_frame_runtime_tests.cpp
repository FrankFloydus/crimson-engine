////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/render/standard_material_render_snapshot_adapter.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

using DenseFrameClock = std::chrono::steady_clock;

double dense_frame_milliseconds(DenseFrameClock::time_point begin,
                                DenseFrameClock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

template <typename Result>
testing::AssertionResult dense_frame_checked(const Result &result) {
  return result.ok() ? testing::AssertionSuccess()
                     : testing::AssertionFailure() << result.error_message();
}

/// Restores the caller's Qt platform after the opt-in native-window fixture.
class DenseFramePlatform final {
public:
  DenseFramePlatform() : previous_(qgetenv("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "windows");
  }
  ~DenseFramePlatform() {
    if (previous_.isNull()) {
      qunsetenv("QT_QPA_PLATFORM");
    } else {
      qputenv("QT_QPA_PLATFORM", previous_);
    }
  }

private:
  QByteArray previous_;
};

/// Captures one complete CPU frame and its production-order sub-stages.
struct DenseFrameSample {
  double total_ms = 0.0;
  double pacing_ms = 0.0;
  std::array<double, 5> phase_ms{};
  std::size_t accepted_surfaces = 0u;
  std::size_t submission_attempts = 0u;
  bool gpu_time_available = false;
  double reported_gpu_ms = 0.0;
};

/// Owns real native viewport surfaces; teardown deliberately follows the app.
class DenseFrameHarness final {
public:
  DenseFrameHarness(QuaderEditorApi &editor, SceneSession &session,
                    ProjectSession &project)
      : editor_(editor), session_(session), resolver_(project) {}

  ~DenseFrameHarness() {
    panel_.reset_interactions(&editor_, &session_.native_tools());
    panel_.shutdown();
    bridge_.shutdown();
    // Do not insert flush/wait fences before this owner teardown: large-scene
    // resource retirement is part of the regression, including after timings.
    renderer_.shutdown();
  }

  bool initialize() {
    const std::filesystem::path resources(
        QCoreApplication::applicationDirPath().toStdWString());
    if (!renderer_.initialize(nullptr, resources,
                              {.backend = RendererBackend::Vulkan})) {
      return false;
    }
    renderer_.set_vsync_enabled(false);
    renderer_.initialize_scene_render_bridge(bridge_, resources);
    bridge_.set_project_material_resolver(&resolver_);
    renderer_.apply_scene_settings(session_.scene().scene_settings());
    panel_.setWindowTitle(QStringLiteral("Quader dense component frame benchmark"));
    panel_.attach_workspace(session_.viewport_workspace());
    panel_.set_viewport_settings(settings_);
    if (!panel_.initialize(renderer_)) {
      return false;
    }
    panel_.set_startup_presentation_blocked(false);
    panel_.resize(1280, 756);
    panel_.show();
    QCoreApplication::processEvents();
    auto *surface = panel_.surface(ViewportPaneId::Perspective);
    const qreal pixel_scale = surface->devicePixelRatio();
    // Keep the raster workload fixed, including on a scaled Windows desktop.
    panel_.resize(panel_.width() + qRound(1280.0 / pixel_scale) - surface->width(),
                  panel_.height() + qRound(720.0 / pixel_scale) - surface->height());
    QCoreApplication::processEvents();
    return true;
  }

  testing::AssertionResult frame(
      const std::function<testing::AssertionResult()> &action,
      DenseFrameSample &sample) {
    const auto begin = DenseFrameClock::now();
    constexpr auto kFrameTimeout = std::chrono::seconds(30);
    bool action_applied = false;
    for (;;) {
      const auto pacing_begin = DenseFrameClock::now();
      // Match the app's nonblocking GPU-readiness gate. Yield between rejected
      // submissions so the native event loop and driver can make progress; no
      // flush, wait fence, or discarded camera/drag step is hidden in samples.
      if (sample.submission_attempts != 0u) {
        QThread::msleep(1u);
      }
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      while (!renderer_.presentation_is_synchronized() && !renderer_.ready_for_next_frame()) {
        if (DenseFrameClock::now() - begin >= kFrameTimeout) {
          return testing::AssertionFailure() << "Native renderer readiness timed out";
        }
        QThread::msleep(1u);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      }
      const auto action_begin = DenseFrameClock::now();
      sample.pacing_ms += dense_frame_milliseconds(pacing_begin, action_begin);
      if (!action_applied) {
        const auto acted = action();
        if (!acted) {
          return acted;
        }
        action_applied = true;
      }
      const auto after_action = DenseFrameClock::now();
      panel_.process_input(editor_, session_.scene(), session_.assets(),
          session_.modeling(), kInvalidAssetId, session_.native_tools(),
          &session_.point_placement(), 1.0 / 60.0);
      const auto after_input = DenseFrameClock::now();
      const auto synced = QuaderRenderApiRuntimeAccess::sync_scene(editor_.render(), bridge_);
      if (!synced.ok()) {
        return dense_frame_checked(synced);
      }
      const auto after_sync = DenseFrameClock::now();
      panel_.prepare_viewports(editor_, bridge_, snapshots_, session_.scene(),
                              session_.assets(), session_.modeling(), session_.native_tools());
      const auto after_prepare = DenseFrameClock::now();
      sample.accepted_surfaces = panel_.render_viewports();
      ++sample.submission_attempts;
      const auto end = DenseFrameClock::now();
      sample.phase_ms[0] += dense_frame_milliseconds(action_begin, after_action);
      sample.phase_ms[1] += dense_frame_milliseconds(after_action, after_input);
      sample.phase_ms[2] += dense_frame_milliseconds(after_input, after_sync);
      sample.phase_ms[3] += dense_frame_milliseconds(after_sync, after_prepare);
      sample.phase_ms[4] += dense_frame_milliseconds(after_prepare, end);
      if (sample.accepted_surfaces != 0u) {
        // Filament reports GPU history asynchronously; this is the latest
        // available sample, not a synchronous measurement of this CPU frame.
        const auto &render_stats = renderer_.frame_performance_stats();
        sample.gpu_time_available = render_stats.gpu_frame_ms_available;
        sample.reported_gpu_ms = render_stats.gpu_frame_ms;
        sample.total_ms = dense_frame_milliseconds(begin, end);
        return testing::AssertionSuccess();
      }
      if (end - begin >= kFrameTimeout) {
        const auto *surface = panel_.surface(ViewportPaneId::Perspective);
        return testing::AssertionFailure() << "Native presentation timed out after "
            << sample.submission_attempts << " attempts; exposed=" << surface->isExposed()
            << " visible=" << surface->isVisible();
      }
    }
  }

  testing::AssertionResult stage(
      std::string_view mode, std::string_view label, std::size_t frames,
      const std::function<testing::AssertionResult(std::size_t)> &action) {
    std::vector<double> times;
    times.reserve(frames);
    std::vector<double> cpu_times;
    cpu_times.reserve(frames);
    std::array<double, 5> phases{};
    std::size_t accepted = 0u;
    std::size_t submission_attempts = 0u;
    double pacing_ms = 0.0;
    double reported_gpu_ms = 0.0;
    std::size_t gpu_samples = 0u;
    const auto begin = DenseFrameClock::now();
    for (std::size_t index = 0u; index < frames; ++index) {
      DenseFrameSample sample;
      const auto result = frame([&] { return action(index); }, sample);
      if (!result) {
        return result;
      }
      times.push_back(sample.total_ms);
      cpu_times.push_back(sample.total_ms - sample.pacing_ms);
      accepted += sample.accepted_surfaces != 0u ? 1u : 0u;
      submission_attempts += sample.submission_attempts;
      pacing_ms += sample.pacing_ms;
      if (sample.gpu_time_available) {
        reported_gpu_ms += sample.reported_gpu_ms;
        ++gpu_samples;
      }
      for (std::size_t phase = 0u; phase < phases.size(); ++phase) {
        phases[phase] += sample.phase_ms[phase];
      }
    }
    const double wall_seconds = std::chrono::duration<double>(DenseFrameClock::now() - begin).count();
    std::sort(times.begin(), times.end());
    std::sort(cpu_times.begin(), cpu_times.end());
    const auto percentile = [](const std::vector<double> &samples, double fraction) {
      return samples[static_cast<std::size_t>(std::ceil(fraction * samples.size())) - 1u];
    };
    const auto selection = editor_.selection().summary();
    const auto resources = diagnostics();
    std::cout << std::fixed << std::setprecision(3)
              << "DENSE_COMPONENT_FRAME mode=" << mode << " stage=" << label
              << " frames=" << frames << " accepted_frames=" << accepted
              << " submission_attempts=" << submission_attempts
              << " wall_seconds=" << wall_seconds
              << " accepted_fps=" << static_cast<double>(accepted) / wall_seconds
              << " frame_p50_ms=" << percentile(times, 0.50)
              << " frame_p95_ms=" << percentile(times, 0.95) << " frame_max_ms=" << times.back()
              << " cpu_p50_ms=" << percentile(cpu_times, 0.50)
              << " cpu_p95_ms=" << percentile(cpu_times, 0.95) << " cpu_max_ms=" << cpu_times.back()
              << " pacing_mean_ms=" << pacing_ms / frames
              << " action_mean_ms=" << phases[0] / frames
              << " input_mean_ms=" << phases[1] / frames
              << " sync_mean_ms=" << phases[2] / frames
              << " prepare_mean_ms=" << phases[3] / frames
              << " render_mean_ms=" << phases[4] / frames
              << " gpu_samples=" << gpu_samples
              << " reported_gpu_mean_ms=" << (gpu_samples != 0u ? reported_gpu_ms / gpu_samples : 0.0)
              << " objects=" << selection.selected_object_count
              << " vertices=" << selection.selected_vertex_count
              << " edges=" << selection.selected_edge_count
              << " faces=" << selection.selected_face_count
              << " geometry_resources=" << resources.gpu_geometry_resource_count
              << " instance_chunks=" << resources.base_instance_batch_chunk_count
              << " wire_chunks=" << resources.wire_batch_chunk_count
              << " planned_primitives=" << resources.planned_primitive_submissions
              << std::endl;
    return accepted == frames ? testing::AssertionSuccess()
        : testing::AssertionFailure() << "Incomplete accepted native frames in " << label;
  }

  const RenderOverlaySnapshotCacheStats &overlay_stats() const { return snapshots_.stats(); }
  SceneRenderResourceDiagnostics diagnostics() const { return bridge_.diagnostics(); }
  QtViewportPanel &panel() { return panel_; }

private:
  QuaderEditorApi &editor_;
  SceneSession &session_;
  ProjectSessionRenderMaterialResolver resolver_;
  RenderSystem renderer_;
  SceneRenderBridge bridge_;
  RenderOverlaySnapshotCache snapshots_;
  ViewportSettings settings_ = ViewportSettings::defaults();
  QtViewportPanel panel_;
};

/// Opt-in real-window benchmark. Run this test alone with the two existing
/// QUADER_TEST_LARGE_SELECTION_PROJECT/SCENE paths and
/// QUADER_TEST_DENSE_COMPONENT_FRAME=1. Mode defaults to vertex; override with
/// QUADER_TEST_DENSE_COMPONENT_MODE=edge, face, or object. No files are saved to
/// the source project, and the test executable supplies isolated preferences.
TEST(DenseComponentFrameRuntimeTests, ClonedProjectNativeViewportFrameStages) {
  if (qEnvironmentVariable("QUADER_TEST_DENSE_COMPONENT_FRAME") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_TEST_DENSE_COMPONENT_FRAME=1 for native Vulkan frame measurements.";
  }
  const QString source_name = qEnvironmentVariable("QUADER_TEST_LARGE_SELECTION_PROJECT");
  ASSERT_FALSE(source_name.isEmpty());
  const std::filesystem::path source_project(source_name.toStdWString());
  ASSERT_TRUE(std::filesystem::is_regular_file(source_project));
  const QString scene_name = qEnvironmentVariable("QUADER_TEST_LARGE_SELECTION_SCENE");
  const auto source_scene = scene_name.isEmpty()
      ? source_project.parent_path() / "Scenes" / "one.qdr"
      : std::filesystem::path(scene_name.toStdWString());
  ASSERT_TRUE(std::filesystem::is_regular_file(source_scene));
  std::error_code error;
  const auto relative_scene = std::filesystem::relative(source_scene, source_project.parent_path(), error);
  ASSERT_FALSE(error) << error.message();
  ASSERT_FALSE(relative_scene.empty());
  ASSERT_FALSE(relative_scene.is_absolute());
  ASSERT_EQ(std::find(relative_scene.begin(), relative_scene.end(), std::filesystem::path("..")),
            relative_scene.end());
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  DenseFramePlatform platform;
  int argc = 1;
  char application_name[] = "quader-dense-component-frame-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_EQ(QGuiApplication::platformName(), QStringLiteral("windows"));
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path root(temporary.path().toStdWString());
  const auto project_root = root / source_project.parent_path().filename();
  std::filesystem::copy(source_project.parent_path(), project_root,
      std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_symlinks, error);
  ASSERT_FALSE(error) << error.message();
  ProjectSession project(root / "recent-scenes.json");
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) / "resources/modeling_assets");
  QuaderEditorApi editor(project);
  ASSERT_TRUE(dense_frame_checked(editor.project().open(project_root / source_project.filename())));
  ASSERT_TRUE(dense_frame_checked(editor.scene_documents().open_native_path(project_root / relative_scene)));
  ASSERT_NE(project.active_scene(), nullptr);
  auto &session = *project.active_scene()->session;
  ASSERT_GE(session.modeling().modeling_sdk_object_count_for_tests(), 6000u);
  ASSERT_TRUE(dense_frame_checked(editor.viewports().set_layout(ViewportLayoutId::Single)));
  ASSERT_TRUE(dense_frame_checked(editor.viewports().set_view(ViewportPaneId::Perspective, ViewportView::Perspective)));
  ASSERT_TRUE(dense_frame_checked(editor.viewports().set_shading_mode(ViewportPaneId::Perspective, QuaderShadingMode::Shaded)));
  const std::string mode_name = qEnvironmentVariable("QUADER_TEST_DENSE_COMPONENT_MODE", QStringLiteral("vertex")).toStdString();
  QuaderSelectionMode mode = QuaderSelectionMode::Vertex;
  if (mode_name == "edge") mode = QuaderSelectionMode::Edge;
  else if (mode_name == "face") mode = QuaderSelectionMode::Face;
  else if (mode_name == "object") mode = QuaderSelectionMode::Object;
  else ASSERT_EQ(mode_name, "vertex");
  ASSERT_TRUE(dense_frame_checked(editor.selection().set_mode(mode)));
  ASSERT_TRUE(dense_frame_checked(editor.selection().clear()));
  DenseFrameHarness harness(editor, session, project);
  ASSERT_TRUE(harness.initialize());
  auto &camera = session.viewport_workspace().camera(ViewportPaneId::Perspective);
  const auto initial_pose = camera.pose();
  const auto idle = [](std::size_t) { return testing::AssertionSuccess(); };
  // Expose and prime all native surfaces before collecting comparable samples.
  ASSERT_TRUE(harness.stage(mode_name, "warmup", 12u, idle));
  const auto raster = harness.panel().surface(ViewportPaneId::Perspective)->render_size_pixels();
  EXPECT_NEAR(raster.x, 1280.0f, 1.0f);
  EXPECT_NEAR(raster.y, 720.0f, 1.0f);
  const auto content_revision = session.modeling().content_revision();
  const auto initial_camera_signature = viewport_camera_pose_persistence_signature(initial_pose);
  const auto navigate = [&](std::size_t index) {
    const auto previous_signature = viewport_camera_pose_persistence_signature(camera.pose());
    if (!camera.restore_perspective_pose(initial_pose)) {
      return testing::AssertionFailure() << "Could not restore navigation baseline";
    }
    // Each semantic sample is self-contained. Panel input may end a native
    // gesture between frames, but it cannot turn later samples into no-ops.
    camera.begin_drag({640.0f, 360.0f}, ViewportDragMode::Orbit);
    camera.update_drag({640.0f + 2.0f * static_cast<float>(index + 1u),
                        360.0f + 0.5f * static_cast<float>(index + 1u)});
    camera.end_drag();
    return viewport_camera_pose_persistence_signature(camera.pose()) != previous_signature
        ? testing::AssertionSuccess()
        : testing::AssertionFailure() << "Navigation sample " << index << " left the camera unchanged";
  };
  ASSERT_TRUE(harness.stage(mode_name, "unselected_navigation", 32u, navigate));
  const auto unselected_camera_signature = viewport_camera_pose_persistence_signature(camera.pose());
  EXPECT_NE(unselected_camera_signature, initial_camera_signature);
  ASSERT_TRUE(camera.restore_perspective_pose(initial_pose));
  const auto api_camera = to_api_viewport_camera(ViewportCameraAdapter::modeling_camera(camera, raster));
  const bool select_backfaces = qEnvironmentVariable("QUADER_TEST_COMPONENT_DRAG_BACKFACES") == QStringLiteral("1");
  std::cout << "DENSE_COMPONENT_FRAME configuration raster=" << raster.x << 'x' << raster.y
            << " backfaces=" << select_backfaces << " camera=saved_perspective" << std::endl;
  ASSERT_TRUE(harness.stage(mode_name, "selected_setup", 1u, [&](std::size_t) {
    return dense_frame_checked(editor.selection().box_select({.camera = api_camera,
        .start = raster, .end = {}, .edit = QuaderSelectionEdit::Replace,
        .rule = QuaderBoxSelectionRule::Crossing, .select_backfaces = select_backfaces}));
  }));
  const auto selected = editor.selection().summary();
  ASSERT_TRUE(selected.has_selection);
  const auto component_count = [mode](const QuaderSelectionSummary &summary) {
    if (mode == QuaderSelectionMode::Vertex) return summary.selected_vertex_count;
    if (mode == QuaderSelectionMode::Edge) return summary.selected_edge_count;
    if (mode == QuaderSelectionMode::Face) return summary.selected_face_count;
    return summary.selected_object_count;
  };
  ASSERT_GT(component_count(selected), 0u);
  const auto retained = harness.overlay_stats();
  const auto uploads = harness.diagnostics().gpu_geometry_upload_count;
  ASSERT_TRUE(harness.stage(mode_name, "selected_static", 16u, idle));
  ASSERT_TRUE(harness.stage(mode_name, "selected_navigation", 32u, navigate));
  EXPECT_EQ(viewport_camera_pose_persistence_signature(camera.pose()), unselected_camera_signature);
  EXPECT_EQ(session.modeling().content_revision(), content_revision);
  EXPECT_EQ(harness.overlay_stats().selection_build_count, retained.selection_build_count);
  EXPECT_EQ(harness.overlay_stats().scene_wireframe_build_count, retained.scene_wireframe_build_count);
  EXPECT_EQ(harness.diagnostics().gpu_geometry_upload_count, uploads);
  const auto after_navigation = editor.selection().summary();
  EXPECT_EQ(after_navigation.selected_object_count, selected.selected_object_count);
  EXPECT_EQ(after_navigation.selected_vertex_count, selected.selected_vertex_count);
  EXPECT_EQ(after_navigation.selected_edge_count, selected.selected_edge_count);
  EXPECT_EQ(after_navigation.selected_face_count, selected.selected_face_count);
  ASSERT_TRUE(camera.restore_perspective_pose(initial_pose));
  // Visible-only selection already deforms a subset of each mesh. Keep its
  // full viewport extent so this measures thousands of affected objects.
  ASSERT_EQ(component_count(editor.selection().summary()), component_count(selected));
  const auto ray_value = camera.ray_from_viewport_point(raster * 0.5f);
  const QuaderRay ray{.origin = ray_value.origin, .direction = ray_value.direction};
  ASSERT_TRUE(harness.stage(mode_name, "partial_drag", 8u, [&](std::size_t index) {
    if (index == 0u) {
      const auto begun = editor.modeling().begin_transform_drag({
          .tool = QuaderTransformTool::Move, .operation = QuaderTransformOperation::Transform,
          .axis = QuaderTransformAxis::X, .screen_position = {}, .screen_pivot = {},
          .screen_tip = {10.0f, 0.0f}, .has_world_direction = true,
          .world_direction = {1.0f, 0.0f, 0.0f}, .ray = ray, .camera = api_camera,
          .grid_size = 1.0, .vertex_snap_radius = 0.0, .grid_snap_enabled = false});
      if (!begun.ok()) return dense_frame_checked(begun);
      if (!begun.value().active) return testing::AssertionFailure() << "Drag did not start";
    }
    const auto updated = editor.modeling().update_transform_drag({
        .screen_position = {0.25f * static_cast<float>(index + 1u), 0.0f},
        .ray = ray, .camera = api_camera, .grid_size = 1.0,
        .vertex_snap_radius = 0.0, .grid_snap_enabled = false});
    if (!updated.ok()) return dense_frame_checked(updated);
    return updated.changed() ? testing::AssertionSuccess()
                             : testing::AssertionFailure() << "Drag update did not change the preview";
  }));
  ASSERT_TRUE(harness.stage(mode_name, "commit", 1u, [&](std::size_t) {
    return dense_frame_checked(editor.modeling().commit_transform_drag());
  }));
  EXPECT_FALSE(session.modeling().transform_drag_active());
  EXPECT_GT(session.modeling().content_revision(), content_revision);
  ASSERT_TRUE(harness.stage(mode_name, "committed_navigation", 32u, navigate));
  const auto query_rectangle = [&](std::size_t index) {
    const auto moved = navigate(index);
    if (!moved) return moved;
    const auto query_camera = to_api_viewport_camera(
        ViewportCameraAdapter::modeling_camera(camera, raster));
    return dense_frame_checked(editor.selection().box_select({.camera = query_camera,
        .start = raster, .end = {}, .edit = QuaderSelectionEdit::Replace,
        .rule = QuaderBoxSelectionRule::Crossing, .select_backfaces = select_backfaces}));
  };
  ASSERT_TRUE(harness.stage(mode_name, "marquee_after_commit", 1u, query_rectangle));
  const auto facts_builds = session.modeling().rectangle_pick_facts_build_count();
  const auto facts_reuses = session.modeling().rectangle_pick_facts_reuse_count();
  ASSERT_TRUE(harness.stage(mode_name, "retained_marquee", 3u, [&](std::size_t index) {
    return query_rectangle(index + 1u);
  }));
  EXPECT_EQ(session.modeling().rectangle_pick_facts_build_count(), facts_builds);
  if (mode != QuaderSelectionMode::Object) {
    EXPECT_GT(session.modeling().rectangle_pick_facts_reuse_count(), facts_reuses);
  }
  std::cout << "DENSE_COMPONENT_FACTS camera_queries=3 builds="
            << session.modeling().rectangle_pick_facts_build_count() - facts_builds
            << " reuses=" << session.modeling().rectangle_pick_facts_reuse_count() - facts_reuses
            << std::endl;
  std::cout << "DENSE_COMPONENT_FRAME completed; entering normal resource teardown" << std::endl;
}

} // namespace
} // namespace quader
