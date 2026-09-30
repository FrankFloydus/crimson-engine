////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_command_ids.hpp"
#include "core/user_config_paths.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/render/standard_material_render_snapshot_adapter.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "ui/ui_system.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QKeyEvent>
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
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
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
    const auto previews = bridge_.tool_preview_work_counters();
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
              << " preview_families=" << previews.active_dynamic_geometry_families
              << " preview_participants=" << previews.active_dynamic_geometry_participants
              << " preview_family_creations=" << previews.dynamic_family_group_creations
              << " preview_inactive_families=" << previews.inactive_dynamic_geometry_families
              << " preview_retired_proxies=" << previews.retired_proxy_count
              << " overlay_position_uploads=" << bridge_.overlay_stats().uploads.position_upload_count
              << " overlay_index_uploads=" << bridge_.overlay_stats().uploads.index_upload_count
              << " overlay_appearance_uploads=" << bridge_.overlay_stats().uploads.appearance_upload_count
              << " overlay_bvh_builds=" << bridge_.overlay_stats().point_visibility.object_build_count
              << " overlay_geometry_builds="
              << session_.modeling().selection_overlay_geometry_work_counters().geometry_build_count
              << " overlay_triangulations="
              << session_.modeling().selection_overlay_geometry_work_counters().face_triangulation_count
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
  ASSERT_GE(selected.selected_object_count, 5000u)
      << "Dense frame measurements require a broad saved camera; this view selects too few meshes.";
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

/// Pins exact compiled content per modeling object across history restoration.
struct DenseGeometryFingerprint final {
  std::vector<std::pair<QuaderModelingObjectId, std::uint64_t>> objects;
  std::uint64_t checksum = 0u;
};

using DenseSelectionHandles = std::vector<std::array<std::uint64_t, 4u>>;

DenseSelectionHandles dense_selection_handles(const ModelingSelectionExport &selection) {
  DenseSelectionHandles handles;
  for (const auto &object : selection.objects) {
    const auto object_id = static_cast<std::uint64_t>(object.modeling_object_id);
    for (const auto &vertex : object.selected_vertices) {
      handles.push_back({object_id, 0u, vertex.id, 0u});
    }
    for (const auto &edge : object.selected_edges) {
      handles.push_back({object_id, 1u, std::min(edge.a, edge.b), std::max(edge.a, edge.b)});
    }
    for (const auto &face : object.selected_faces) {
      handles.push_back({object_id, 2u, face.id, 0u});
    }
  }
  std::sort(handles.begin(), handles.end());
  return handles;
}

std::uint64_t dense_selection_checksum(const DenseSelectionHandles &handles) {
  constexpr std::uint64_t kFnvPrime = 1099511628211ull;
  std::uint64_t checksum = 14695981039346656037ull;
  for (const auto &handle : handles) {
    for (const auto value : handle) {
      for (unsigned int byte = 0u; byte < 8u; ++byte) {
        checksum ^= (value >> (byte * 8u)) & 0xffu;
        checksum *= kFnvPrime;
      }
    }
  }
  return checksum;
}

/// Keeps unrelated desktop events out of the native Qt replay target.
class DenseNativeInputIsolation final : public QObject {
public:
  DenseNativeInputIsolation(QtViewportPanel &panel, QtViewportSurface &surface)
      : panel_(panel), surface_(surface) {
    QCoreApplication::instance()->installEventFilter(this);
  }
  ~DenseNativeInputIsolation() override {
    QCoreApplication::instance()->removeEventFilter(this);
  }
  std::uint64_t discarded_events() const { return discarded_events_; }

protected:
  bool eventFilter(QObject *target, QEvent *event) override {
    if (!event->spontaneous()) return false;
    const auto *widget = qobject_cast<QWidget *>(target);
    if (target != &surface_ && target != &panel_ &&
        (widget == nullptr || !panel_.isAncestorOf(widget))) return false;
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
      ++discarded_events_;
      return true;
    default:
      return false;
    }
  }

private:
  QtViewportPanel &panel_;
  QtViewportSurface &surface_;
  std::uint64_t discarded_events_ = 0u;
};

/// Restores the saved snapping preference on successful or aborted drag replay.
class DenseGridSnapScope final {
public:
  explicit DenseGridSnapScope(QuaderEditorApi &editor)
      : editor_(editor), original_(editor.viewports().control(
            ViewportPaneId::Perspective, QuaderViewportControl::GridSnap)) {}
  ~DenseGridSnapScope() {
    if (active_) (void)restore();
  }
  bool original() const { return original_; }
  testing::AssertionResult disable() {
    const auto result = editor_.viewports().set_control(
        ViewportPaneId::Perspective, QuaderViewportControl::GridSnap, false);
    active_ = result.ok();
    return dense_frame_checked(result);
  }
  testing::AssertionResult restore() {
    const auto result = editor_.viewports().set_control(
        ViewportPaneId::Perspective, QuaderViewportControl::GridSnap, original_);
    if (result.ok()) active_ = false;
    return dense_frame_checked(result);
  }

private:
  QuaderEditorApi &editor_;
  bool original_ = false;
  bool active_ = false;
};

/// Measures the retained editor shell as well as native input and presentation.
/// Diagnostic snapshots are sampled after each timed stage, never inside it.
class DenseWorkflowHarness final {
public:
  DenseWorkflowHarness(QuaderEditorApi &editor, SceneSession &session,
                       ProjectSession &project)
      : editor_(editor), session_(session), resolver_(project) {}

  ~DenseWorkflowHarness() {
    input_isolation_.reset();
    ui_.shutdown();
    bridge_.shutdown();
    renderer_.shutdown();
  }

  bool initialize() {
    const std::filesystem::path resources(QCoreApplication::applicationDirPath().toStdWString());
    if (!renderer_.initialize(nullptr, resources, {.backend = RendererBackend::Vulkan})) return false;
    renderer_.initialize_scene_render_bridge(bridge_, resources);
    bridge_.set_project_material_resolver(&resolver_);
    if (!ui_.initialize(renderer_, resources)) return false;
    resolver_.set_default_material_texture(ui_.default_material_texture());
    ui_.install_runtime_hooks(editor_, session_.scene(), session_.assets(), session_.modeling(),
        session_.selection(), session_.native_tools(), session_.point_placement(), kInvalidAssetId);
    ui_.show_workspace();
    auto *window = ui_.main_window();
    if (window == nullptr) return false;
    panel_ = dynamic_cast<QtViewportPanel *>(window->findChild<QWidget *>(QStringLiteral("viewportWorkspace")));
    if (panel_ == nullptr) return false;
    window->setWindowTitle(QStringLiteral("Quader full editor dense workflow benchmark"));
    window->resize(1800, 1000);
    ui_.show();
    window->activateWindow();
    QCoreApplication::processEvents();
    // Keep the actual Inspector, Outliner and Face Texture presenters visible.
    for (const auto *name : {"inspector", "outliner", "face_texture"}) {
      if (!editor_.ui().show_panel(name).ok()) return false;
    }
    QCoreApplication::processEvents();
    auto &surface = this->surface();
    const qreal dpr = surface.devicePixelRatio();
    auto *surface_container = panel_->findChild<QWidget *>(
        QStringLiteral("viewportSurfaceContainer_perspective"));
    if (surface_container == nullptr) return false;
    const QSize surface_size(qRound(1280.0 / dpr), qRound(720.0 / dpr));
    const QSize pane_chrome = panel_->size() - surface_container->size();
    const QSize panel_size = surface_size + pane_chrome;
    const QSize window_size = window->size() + panel_size - panel_->size();
    // Retained status/tool chrome can change its size after selection/history.
    // Fix the actual native container and its owning panel so those changes
    // cannot silently alter camera rays or the benchmark's render resolution.
    surface_container->setFixedSize(surface_size);
    panel_->setFixedSize(panel_size);
    window->resize(window_size);
    renderer_.apply_scene_settings(session_.scene().scene_settings());
    settings_revision_ = session_.scene().scene_settings_revision();
    QCoreApplication::processEvents();
    input_isolation_ = std::make_unique<DenseNativeInputIsolation>(*panel_, surface);
    return true;
  }

  QtViewportPanel &panel() { return *panel_; }
  QtViewportSurface &surface() { return *panel_->surface(ViewportPaneId::Perspective); }
  void force_cpu_selection(bool enabled) { force_cpu_selection_ = enabled; }

  void mouse(QEvent::Type type, QPointF local, Qt::MouseButton button, Qt::MouseButtons held) {
    auto &target = surface();
    last_pointer_ = local;
    QMouseEvent event(type, local, QPointF(target.mapToGlobal(local.toPoint())),
                      button, held, Qt::NoModifier);
    QApplication::sendEvent(&target, &event);
  }

  void focus_pointer(QPointF point) {
    auto &target = surface();
    target.requestActivate();
    QEnterEvent enter(point, point, QPointF(target.mapToGlobal(point.toPoint())));
    QApplication::sendEvent(&target, &enter);
    QFocusEvent focus(QEvent::FocusIn);
    QApplication::sendEvent(&target, &focus);
  }

  void select_all_key() {
    QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QApplication::sendEvent(&surface(), &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, Qt::ControlModifier);
    QApplication::sendEvent(&surface(), &release);
  }

  struct Sample final {
    double elapsed_ms = 0.0;
    double pacing_ms = 0.0;
    std::array<double, 6> phases{};
    std::size_t attempts = 0u;
    bool gpu_available = false;
    double gpu_ms = 0.0;
    ToolPreviewCacheWorkCounters previews;
    std::uint64_t content_revision = 0u;
    std::uint64_t preview_revision = 0u;
    std::uint64_t render_submission_revision = 0u;
    bool drag_active = false;
    bool navigation_active = false;
    std::uint64_t camera_signature = 0u;
    QVec2 raster{};
    QPointF requested_pointer;
    int physical_modifiers = 0;
  };

  testing::AssertionResult frame(const std::function<testing::AssertionResult()> &action,
                                Sample &sample) {
    const auto begin = DenseFrameClock::now();
    constexpr auto kTimeout = std::chrono::seconds(45);
    bool dispatched = false;
    for (;;) {
      const auto pacing = DenseFrameClock::now();
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      while (!renderer_.presentation_is_synchronized() && !renderer_.ready_for_next_frame()) {
        if (DenseFrameClock::now() - begin > kTimeout) {
          return testing::AssertionFailure() << "GPU readiness timed out";
        }
        QThread::msleep(1u);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      }
      const auto refresh_begin = DenseFrameClock::now();
      sample.pacing_ms += dense_frame_milliseconds(pacing, refresh_begin);
      ui_.refresh({.raw_delta_seconds = 1.0 / 60.0, .delta_seconds = 1.0 / 60.0,
                   .elapsed_seconds = static_cast<double>(frame_index_) / 60.0,
                   .frame_index = frame_index_++}, renderer_.frame_performance_stats());
      const auto action_begin = DenseFrameClock::now();
      if (!dispatched) {
        const auto result = action();
        if (!result) return result;
        dispatched = true;
      }
      const auto input_begin = DenseFrameClock::now();
      if (QGuiApplication::queryKeyboardModifiers() != Qt::NoModifier) {
        return testing::AssertionFailure() << "Physical keyboard modifiers interfered with native replay";
      }
#if !defined(QUADER_DENSE_WORKFLOW_BASELINE)
      if (force_cpu_selection_) session_.scene_surface_query().set_selection_depth_source({});
#endif
      ui_.process_viewport_input();
      const auto sync_begin = DenseFrameClock::now();
      const auto sync = QuaderRenderApiRuntimeAccess::sync_scene(editor_.render(), bridge_);
      if (!sync.ok()) return dense_frame_checked(sync);
      const auto revision = session_.scene().scene_settings_revision();
      if (revision != settings_revision_) {
        renderer_.apply_scene_settings(session_.scene().scene_settings());
        settings_revision_ = revision;
      }
      const auto prepare_begin = DenseFrameClock::now();
      ui_.prepare_viewports(bridge_, snapshots_);
      const auto render_begin = DenseFrameClock::now();
      const auto accepted = ui_.render_surfaces(true);
      const auto end = DenseFrameClock::now();
      const std::array marks{refresh_begin, action_begin, input_begin, sync_begin,
                             prepare_begin, render_begin, end};
      for (std::size_t index = 0u; index < sample.phases.size(); ++index) {
        sample.phases[index] += dense_frame_milliseconds(marks[index], marks[index + 1u]);
      }
      ++sample.attempts;
      if (accepted != 0u) {
        sample.elapsed_ms = dense_frame_milliseconds(begin, end);
        const auto gpu = renderer_.frame_performance_stats();
        sample.gpu_available = gpu.gpu_frame_ms_available;
        sample.gpu_ms = gpu.gpu_frame_ms;
        sample.previews = bridge_.tool_preview_work_counters();
        sample.content_revision = session_.modeling().content_revision();
        sample.preview_revision = session_.modeling().transient_transform_preview_revision();
        sample.render_submission_revision = bridge_.tool_preview_submission_revision();
        sample.drag_active = session_.modeling().transform_drag_active();
        sample.navigation_active = panel_->command_input_context().navigation_active;
        sample.camera_signature = viewport_camera_pose_persistence_signature(
            session_.viewport_workspace().camera(ViewportPaneId::Perspective).pose());
        sample.raster = surface().render_size_pixels();
        if (sample.raster.x != 1280.0f || sample.raster.y != 720.0f) {
          return testing::AssertionFailure() << "Native replay raster changed to "
              << sample.raster.x << 'x' << sample.raster.y;
        }
        sample.requested_pointer = last_pointer_;
        sample.physical_modifiers = static_cast<int>(QGuiApplication::queryKeyboardModifiers());
        return testing::AssertionSuccess();
      }
      if (end - begin > kTimeout) return testing::AssertionFailure() << "Native frame timed out";
      QThread::msleep(1u);
    }
  }

  testing::AssertionResult stage(std::string_view label, std::size_t count,
      const std::function<testing::AssertionResult(std::size_t)> &action,
      bool require_preview_change = false) {
    std::vector<Sample> samples(count);
    const auto geometry_before = bridge_.mesh_geometry_work_counters();
    const auto overlays_before = session_.modeling().selection_overlay_geometry_work_counters();
    const auto wall_begin = DenseFrameClock::now();
    for (std::size_t index = 0u; index < count; ++index) {
      const auto preview_before = session_.modeling().transient_transform_preview_revision();
      const auto submission_before = bridge_.tool_preview_submission_revision();
      const auto result = frame([&] { return action(index); }, samples[index]);
      if (!result) return result;
      if (require_preview_change &&
          (!samples[index].drag_active || samples[index].preview_revision <= preview_before ||
           bridge_.tool_preview_submission_revision() <= submission_before)) {
        return testing::AssertionFailure() << label << " sample " << index
            << " did not publish changed drag geometry: preview " << preview_before << " -> "
            << samples[index].preview_revision << ", render submission " << submission_before << " -> "
            << bridge_.tool_preview_submission_revision();
      }
    }
    const double wall_seconds = std::chrono::duration<double>(DenseFrameClock::now() - wall_begin).count();
    const auto resources = bridge_.diagnostics();
    const auto surfaces = bridge_.editable_surface_work_counters();
    const auto geometry_work = bridge_.mesh_geometry_work_counters();
    const auto overlays = session_.modeling().selection_overlay_geometry_work_counters();
    const auto selection_depth = session_.scene_surface_query().stats().selection_depth;
    const auto selection_renderer = surface().renderer().component_selection().stats();
    std::vector<double> times;
    double sum_ms = 0.0;
    for (std::size_t index = 0u; index < count; ++index) {
      const auto &sample = samples[index];
      times.push_back(sample.elapsed_ms);
      sum_ms += sample.elapsed_ms;
      std::cout << std::fixed << std::setprecision(3)
          << "DENSE_WORKFLOW_FRAME stage=" << label << " sample=" << index
          << " frame_ms=" << sample.elapsed_ms << " pacing_ms=" << sample.pacing_ms
          << " ui_ms=" << sample.phases[0] << " event_ms=" << sample.phases[1]
          << " input_ms=" << sample.phases[2] << " sync_ms=" << sample.phases[3]
          << " prepare_ms=" << sample.phases[4] << " render_ms=" << sample.phases[5]
          << " attempts=" << sample.attempts << " gpu_available=" << sample.gpu_available
          << " reported_gpu_ms=" << sample.gpu_ms
          << " preview_families=" << sample.previews.active_dynamic_geometry_families
          << " preview_participants=" << sample.previews.active_dynamic_geometry_participants
          << " retired_previews=" << sample.previews.retired_proxy_count
          << " content_revision=" << sample.content_revision
          << " preview_revision=" << sample.preview_revision
          << " render_submission_revision=" << sample.render_submission_revision
          << " dragging=" << sample.drag_active << " navigating=" << sample.navigation_active
          << " camera_signature=" << sample.camera_signature
          << " raster_width=" << sample.raster.x << " raster_height=" << sample.raster.y
          << " requested_pointer_x=" << std::setprecision(9) << sample.requested_pointer.x()
          << " requested_pointer_y=" << sample.requested_pointer.y()
          << " physical_modifiers=" << sample.physical_modifiers << std::setprecision(3) << '\n';
    }
    std::sort(times.begin(), times.end());
    const auto p95 = static_cast<std::size_t>(std::ceil(static_cast<double>(count) * 0.95)) - 1u;
    const auto selection = editor_.selection().summary();
    std::cout << "DENSE_WORKFLOW_STAGE stage=" << label << " frames=" << count
              << " wall_seconds=" << wall_seconds
              << " accepted_fps=" << static_cast<double>(count) / wall_seconds
              << " frame_mean_ms=" << sum_ms / static_cast<double>(count)
              << " frame_p50_ms=" << times[count / 2u] << " frame_p95_ms=" << times[p95]
              << " frame_max_ms=" << times.back() << " objects=" << selection.selected_object_count
              << " vertices=" << selection.selected_vertex_count << " edges=" << selection.selected_edge_count
              << " faces=" << selection.selected_face_count
              << " retained_payloads=" << resources.unique_compiled_payload_count
              << " geometry_resources=" << resources.gpu_geometry_resource_count
              << " geometry_uploads=" << resources.gpu_geometry_upload_count
              << " instance_chunks=" << resources.base_instance_batch_chunk_count
              << " instance_participants=" << resources.base_instance_participant_count
              << " standalone_primitives=" << resources.standalone_primitive_submissions
              << " planned_primitives=" << resources.planned_primitive_submissions << std::endl;
    std::cout << "DENSE_WORKFLOW_COUNTERS stage=" << label
              << " surface_source_records=" << surfaces.source_record_count
              << " surface_eligible_records=" << surfaces.eligible_record_count
              << " surface_batches=" << surfaces.active_batch_count
              << " surface_retained_objects=" << surfaces.active_retained_object_count
              << " surface_preview_objects=" << surfaces.active_preview_object_count
              << " fallback_static=" << surfaces.fallback_static_count
              << " fallback_nonrenderable=" << surfaces.fallback_nonrenderable_count
              << " fallback_unknown_opacity=" << surfaces.fallback_unknown_opacity_count
              << " fallback_transparent=" << surfaces.fallback_transparent_count
              << " fallback_reflected=" << surfaces.fallback_reflected_count
              << " fallback_singular=" << surfaces.fallback_singular_count
              << " fallback_nonconformal=" << surfaces.fallback_nonconformal_count
              << " fallback_unmapped_source=" << surfaces.fallback_unmapped_source_count
              << " fallback_presentation=" << surfaces.fallback_presentation_count
              << " fallback_oversized_geometry=" << surfaces.fallback_oversized_geometry_count
              << " surface_retired_proxies=" << surfaces.retired_proxy_count
              << " surface_batch_creates_total=" << surfaces.batch_create_count
              << " geometry_vertex_bytes_total=" << resources.gpu_uploaded_vertex_bytes
              << " geometry_index_bytes_total=" << resources.gpu_uploaded_index_bytes
              << " surface_live_geometry=" << geometry_work.editable_surface_live_geometry_count
              << " surface_geometry_creates_total=" << geometry_work.editable_surface_geometry_create_count
              << " surface_attribute_packs_total=" << geometry_work.editable_surface_attribute_pack_count
              << " surface_canonical_reuses_total=" << geometry_work.editable_surface_canonical_reuse_count
              << " surface_position_uploads_total=" << geometry_work.editable_surface_position_upload_count
              << " surface_attribute_uploads_total=" << geometry_work.editable_surface_attribute_upload_count
              << " surface_index_uploads_total=" << geometry_work.editable_surface_index_upload_count
              << " surface_palette_uploads_total=" << geometry_work.editable_surface_palette_upload_count
              << " surface_vertex_bytes_total=" << geometry_work.editable_surface_uploaded_vertex_bytes
              << " surface_index_bytes_total=" << geometry_work.editable_surface_uploaded_index_bytes
              << " overlay_geometry_builds_total=" << overlays.geometry_build_count
              << " overlay_face_triangulations_total=" << overlays.face_triangulation_count
              << " overlay_geometry_position_refreshes_total=" << overlays.geometry_position_refresh_count
              << " overlay_frame_position_refreshes_total=" << overlays.frame_position_refresh_count
              << " selection_gpu_accepted_total=" << selection_depth.gpu_accepted_witnesses
              << " selection_gpu_rejected_total=" << selection_depth.gpu_rejected_witnesses
              << " selection_gpu_overflow_total=" << selection_depth.gpu_overflow_witnesses
              << " selection_cpu_fallback_total=" << selection_depth.cpu_fallback_witnesses
              << " selection_stale_snapshot_total=" << selection_depth.stale_snapshot_queries
              << " selection_gpu_submissions_total=" << selection_renderer.submission_count
              << " selection_completed_readbacks_total=" << selection_renderer.completed_readbacks
              << " selection_readback_bytes_total=" << selection_renderer.readback_bytes
              << " selection_readback_completion_latency_ms=" << selection_renderer.readback_latency_ms
              << " discarded_desktop_events_total=" << input_isolation_->discarded_events()
              << std::endl;
    if (label.ends_with("_navigation_held") &&
        (geometry_work.upload_count != geometry_before.upload_count ||
         geometry_work.dynamic_geometry_create_count != geometry_before.dynamic_geometry_create_count ||
         geometry_work.dynamic_geometry_upload_count != geometry_before.dynamic_geometry_upload_count ||
         geometry_work.editable_surface_geometry_create_count != geometry_before.editable_surface_geometry_create_count ||
         geometry_work.editable_surface_position_upload_count != geometry_before.editable_surface_position_upload_count ||
         geometry_work.editable_surface_attribute_upload_count != geometry_before.editable_surface_attribute_upload_count ||
         geometry_work.editable_surface_index_upload_count != geometry_before.editable_surface_index_upload_count ||
         overlays.geometry_build_count != overlays_before.geometry_build_count ||
         overlays.face_triangulation_count != overlays_before.face_triangulation_count)) {
      return testing::AssertionFailure() << label
          << " rebuilt or uploaded geometry during unchanged held navigation: ordinary_uploads="
          << geometry_work.upload_count - geometry_before.upload_count
          << " dynamic_creates=" << geometry_work.dynamic_geometry_create_count - geometry_before.dynamic_geometry_create_count
          << " dynamic_uploads=" << geometry_work.dynamic_geometry_upload_count - geometry_before.dynamic_geometry_upload_count
          << " surface_creates=" << geometry_work.editable_surface_geometry_create_count - geometry_before.editable_surface_geometry_create_count
          << " surface_position_uploads=" << geometry_work.editable_surface_position_upload_count - geometry_before.editable_surface_position_upload_count
          << " surface_attribute_uploads=" << geometry_work.editable_surface_attribute_upload_count - geometry_before.editable_surface_attribute_upload_count
          << " surface_index_uploads=" << geometry_work.editable_surface_index_upload_count - geometry_before.editable_surface_index_upload_count
          << " overlay_builds=" << overlays.geometry_build_count - overlays_before.geometry_build_count
          << " overlay_triangulations=" << overlays.face_triangulation_count - overlays_before.face_triangulation_count;
    }
    return testing::AssertionSuccess();
  }

  testing::AssertionResult geometry_fingerprint(std::string_view label,
                                                DenseGeometryFingerprint &fingerprint) {
    const auto objects = editor_.query().modeling_objects();
    fingerprint.objects.clear();
    fingerprint.objects.reserve(objects.size());
    for (const auto &object : objects) {
      const auto *mesh = session_.assets().mesh(object.mesh);
      if (mesh == nullptr) {
        return testing::AssertionFailure() << "Missing committed mesh for object " << object.modeling_object_id;
      }
      fingerprint.objects.emplace_back(object.modeling_object_id, mesh->geometry().content_id().value);
    }
    std::sort(fingerprint.objects.begin(), fingerprint.objects.end());
    // Content IDs cover positions, normals, UV carriers, source vertices,
    // indices and primitive material slots. Asset allocation IDs may change
    // after history restoration and deliberately do not enter this identity.
    constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;
    fingerprint.checksum = kFnvOffset;
    for (const auto &[object, content] : fingerprint.objects) {
      for (const std::uint64_t value : {static_cast<std::uint64_t>(object), content}) {
        for (unsigned int byte = 0u; byte < 8u; ++byte) {
          fingerprint.checksum ^= (value >> (byte * 8u)) & 0xffu;
          fingerprint.checksum *= kFnvPrime;
        }
      }
    }
    std::cout << "DENSE_WORKFLOW_GEOMETRY stage=" << label
              << " objects=" << fingerprint.objects.size()
              << " checksum=" << fingerprint.checksum << std::endl;
    return testing::AssertionSuccess();
  }

  testing::AssertionResult drag_axis(ViewportToolId tool, std::size_t choice,
                                    QPointF &point, QPointF &direction) {
    auto &workspace = session_.viewport_workspace();
    const ViewportInteractionSelectionState selection{
        .summary = editor_.selection().summary(), .scene_revision = session_.scene().revision(),
        .asset_revision = session_.assets().revision(),
        .pivot_revision = session_.pivot().selection_pivot_revision()};
    TransformGizmoPresentation presentation;
    const auto *gizmo = presentation.prepare(editor_, {
        .camera = workspace.camera(ViewportPaneId::Perspective),
        .settings = workspace.effective_settings(ViewportPaneId::Perspective),
        .selection_state = selection, .tool = tool,
        .transform_space = editor_.viewports().transform_space(ViewportPaneId::Perspective),
        .grid_preset = workspace.grid_preset(),
        .raster_scale = panel_->viewport_raster_scale(ViewportPaneId::Perspective),
        .transform_preview_revision = session_.modeling().transient_transform_preview_revision(),
        .pane_settings_revision = workspace.control_revision(ViewportPaneId::Perspective),
        .source_identity = reinterpret_cast<std::uintptr_t>(&session_.scene())});
    if (gizmo == nullptr || gizmo->axes.empty()) return testing::AssertionFailure() << "No native gizmo";
    std::vector<quader_editor_core::TransformGizmoAxisPrimitive> axes;
    for (const auto &axis : gizmo->axes) {
      const auto delta = axis.screen_tip - axis.screen_start;
      if (std::hypot(delta.x, delta.y) > 20.0f) axes.push_back(axis);
    }
    if (axes.empty()) return testing::AssertionFailure() << "No projected gizmo axis";
    const auto &axis = axes[choice % axes.size()];
    const auto delta = axis.screen_tip - axis.screen_start;
    const auto hit = axis.screen_start + delta * 0.80f;
    const auto dpr = surface().devicePixelRatio();
    point = {hit.x / dpr, hit.y / dpr};
    const double magnitude = std::hypot(delta.x, delta.y);
    direction = {delta.x / magnitude, delta.y / magnitude};
    return testing::AssertionSuccess();
  }

private:
  QuaderEditorApi &editor_;
  SceneSession &session_;
  ProjectSessionRenderMaterialResolver resolver_;
  RenderSystem renderer_;
  SceneRenderBridge bridge_;
  RenderOverlaySnapshotCache snapshots_;
  UISystem ui_;
  std::unique_ptr<DenseNativeInputIsolation> input_isolation_;
  QtViewportPanel *panel_ = nullptr;
  std::uint64_t frame_index_ = 0u;
  std::uint64_t settings_revision_ = 0u;
  QPointF last_pointer_;
  bool force_cpu_selection_ = false;
};

/// Uses the whole immutable authored project and exercises repeated transitions
/// through the real editor shell. The earlier bounded fixture remains separate.
TEST(DenseComponentFrameRuntimeTests, ClonedProjectFullEditorMixedComponentWorkflow) {
  if (qEnvironmentVariable("QUADER_TEST_DENSE_COMPONENT_WORKFLOW") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_TEST_DENSE_COMPONENT_WORKFLOW=1 for full-editor Vulkan measurements.";
  }
  const std::filesystem::path source_project(
      qEnvironmentVariable("QUADER_TEST_LARGE_SELECTION_PROJECT").toStdWString());
  const std::filesystem::path source_scene(
      qEnvironmentVariable("QUADER_TEST_LARGE_SELECTION_SCENE").toStdWString());
  ASSERT_TRUE(std::filesystem::is_regular_file(source_project));
  ASSERT_TRUE(std::filesystem::is_regular_file(source_scene));
  const auto relative_scene = source_scene.lexically_relative(source_project.parent_path());
  ASSERT_FALSE(relative_scene.empty());
  ASSERT_FALSE(relative_scene.is_absolute());
  ASSERT_EQ(std::find(relative_scene.begin(), relative_scene.end(), std::filesystem::path("..")), relative_scene.end());
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  DenseFramePlatform platform;
  int argc = 1;
  char name[] = "quader-full-editor-dense-workflow";
  char *argv[]{name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStringLiteral("Fusion"));
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path root(temporary.path().toStdWString());
  const auto clone = root / "project";
  std::error_code error;
  std::filesystem::copy(source_project.parent_path(), clone,
      std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_symlinks, error);
  ASSERT_FALSE(error) << error.message();
  const auto preferences = qEnvironmentVariable("QUADER_TEST_DENSE_PREFERENCES_DIRECTORY");
  if (!preferences.isEmpty()) {
    const std::filesystem::path directory(preferences.toStdWString());
    const auto destination = quader_user_config_directory({});
    std::filesystem::create_directories(destination, error);
    ASSERT_FALSE(error) << error.message();
    for (const auto *file : {"ui_settings.ini", "viewport_settings.ini", "keybindings.json"}) {
      if (!std::filesystem::exists(directory / file)) continue;
      std::filesystem::copy_file(directory / file, destination / file,
          std::filesystem::copy_options::overwrite_existing, error);
      ASSERT_FALSE(error) << error.message();
    }
  }
  ProjectSession project(root / "recent-scenes.json");
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) / "resources/modeling_assets");
  QuaderEditorApi editor(project);
  ASSERT_TRUE(dense_frame_checked(editor.project().open(clone / source_project.filename())));
  ASSERT_TRUE(dense_frame_checked(editor.scene_documents().open_native_path(clone / relative_scene)));
  auto &session = *project.active_scene()->session;
  const auto objects = editor.query().modeling_objects();
  constexpr std::size_t kExpectedMeshes = 6274u;
  ASSERT_EQ(objects.size(), kExpectedMeshes);
  ASSERT_TRUE(dense_frame_checked(editor.viewports().set_layout(ViewportLayoutId::Single)));
  DenseWorkflowHarness harness(editor, session, project);
  ASSERT_TRUE(harness.initialize());
  const auto idle = [](std::size_t) { return testing::AssertionSuccess(); };
  ASSERT_TRUE(harness.stage("warmup", 12u, idle));
  auto &surface = harness.surface();
  const auto raster = surface.render_size_pixels();
  ASSERT_NEAR(raster.x, 1280.0f, 1.0f);
  ASSERT_NEAR(raster.y, 720.0f, 1.0f);
  auto &camera = session.viewport_workspace().camera(ViewportPaneId::Perspective);
  const auto saved_pose = camera.pose();
  std::cout << "DENSE_WORKFLOW_CONFIG meshes=" << objects.size() << " raster=" << raster.x << 'x' << raster.y
            << " preferences=" << (preferences.isEmpty() ? "isolated_defaults" : "frozen_user_copy")
            << " ui=full inspector=visible outliner=visible face_texture=visible"
            << " native_input=1 snap_grid=" << editor.viewports().control(QuaderViewportControl::GridSnap)
            << " native_backfaces=" << session.viewport_workspace().effective_settings(
                   ViewportPaneId::Perspective).bool_value(ViewportBoolSettingId::BackfaceSelection)
            << std::endl;

  const auto navigation = [&](std::string label) {
    const QPointF origin{surface.width() * 0.5, surface.height() * 0.5};
    harness.focus_pointer(origin);
    const auto signature = viewport_camera_pose_persistence_signature(camera.pose());
    auto result = harness.stage(label + "_press", 1u, [&](std::size_t) {
      harness.mouse(QEvent::MouseButtonPress, origin, Qt::MiddleButton, Qt::MiddleButton);
      return testing::AssertionSuccess();
    });
    if (!result) return result;
    result = harness.stage(label + "_held", 16u, [&](std::size_t index) {
      harness.mouse(QEvent::MouseMove, origin + QPointF{2.0 * static_cast<double>(index + 1u),
          0.5 * static_cast<double>(index + 1u)}, Qt::NoButton, Qt::MiddleButton);
      return testing::AssertionSuccess();
    });
    if (!result) return result;
    if (!harness.panel().command_input_context().navigation_active ||
        viewport_camera_pose_persistence_signature(camera.pose()) == signature) {
      return testing::AssertionFailure() << "Held native navigation did not remain active";
    }
    return harness.stage(label + "_release", 1u, [&](std::size_t) {
      harness.mouse(QEvent::MouseButtonRelease, origin + QPointF{34.0, 8.5}, Qt::MiddleButton, Qt::NoButton);
      return testing::AssertionSuccess();
    });
  };
  ASSERT_TRUE(navigation("unselected_navigation"));
  ASSERT_TRUE(camera.restore_perspective_pose(saved_pose));

  const auto drag = [&](const std::string &label, ViewportToolId tool, std::size_t axis) {
    DenseGridSnapScope grid_snap(editor);
    auto result = grid_snap.disable();
    if (!result) return result;
    std::cout << "DENSE_WORKFLOW_DRAG_CONFIG stage=" << label
              << " original_grid_snap=" << grid_snap.original()
              << " effective_grid_snap=" << editor.viewports().control(
                     ViewportPaneId::Perspective, QuaderViewportControl::GridSnap)
              << " require_changed_samples=1" << std::endl;
    result = harness.stage(label + "_activate", 1u, [&](std::size_t) {
      return dense_frame_checked(editor.tools().activate(tool));
    });
    if (!result) return result;
    result = harness.stage(label + "_selected_static", 3u, idle);
    if (!result) return result;
    const auto drag_camera_signature = viewport_camera_pose_persistence_signature(camera.pose());
    QPointF press;
    QPointF direction;
    result = harness.drag_axis(tool, axis, press, direction);
    if (!result) return result;
    if (press.x() < 0.0 || press.y() < 0.0 ||
        press.x() >= surface.width() || press.y() >= surface.height()) {
      return testing::AssertionFailure() << label << " gizmo press is outside the native viewport: "
          << press.x() << ',' << press.y();
    }
    const auto pivot = editor.query().selection_transform_pivot();
    std::cout << "DENSE_WORKFLOW_INPUT stage=" << label << std::setprecision(9)
              << " press_x=" << press.x() << " press_y=" << press.y()
              << " direction_x=" << direction.x() << " direction_y=" << direction.y()
              << " camera_signature=" << viewport_camera_pose_persistence_signature(camera.pose())
              << " pivot_x=" << (pivot ? pivot->x : 0.0f)
              << " pivot_y=" << (pivot ? pivot->y : 0.0f)
              << " pivot_z=" << (pivot ? pivot->z : 0.0f) << std::setprecision(3) << std::endl;
    harness.focus_pointer(press);
    const auto before_content = session.modeling().content_revision();
    result = harness.stage(label + "_press", 1u, [&](std::size_t) {
      harness.mouse(QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
      return testing::AssertionSuccess();
    });
    if (!result) return result;
    const double first_step = static_cast<double>(QApplication::startDragDistance()) + 3.0;
    result = harness.stage(label + (tool == ViewportToolId::Scale ? "_scale_drag" : "_move_drag"),
        8u, [&](std::size_t index) {
      harness.mouse(QEvent::MouseMove, press + direction * (first_step + 3.0 * static_cast<double>(index)),
                    Qt::NoButton, Qt::LeftButton);
      return testing::AssertionSuccess();
    }, true);
    if (!result) return result;
    if (!session.modeling().transform_drag_active() ||
        editor.selection().summary().selected_object_count != kExpectedMeshes) {
      return testing::AssertionFailure() << "Native drag did not retain every mesh";
    }
    // Native input applies this changed last pointer position and commits in
    // the same measured frame, rather than timing an already-finished preview.
    result = harness.stage(label + "_release_final_update_commit", 1u, [&](std::size_t) {
      harness.mouse(QEvent::MouseButtonRelease, press + direction * (first_step + 25.0),
                    Qt::LeftButton, Qt::NoButton);
      return testing::AssertionSuccess();
    });
    if (!result) return result;
    if (session.modeling().transform_drag_active() ||
        session.modeling().content_revision() <= before_content || !editor.query().history().can_undo) {
      return testing::AssertionFailure() << "Native release did not commit an edit";
    }
    if (viewport_camera_pose_persistence_signature(camera.pose()) != drag_camera_signature) {
      return testing::AssertionFailure() << "Camera changed during native component transform replay";
    }
    result = grid_snap.restore();
    if (!result) return result;
    std::cout << "DENSE_WORKFLOW_DRAG_COMPLETE stage=" << label
              << " changed_samples=8 original_grid_snap=" << grid_snap.original()
              << " restored_grid_snap=" << editor.viewports().control(
                     ViewportPaneId::Perspective, QuaderViewportControl::GridSnap) << std::endl;
    return testing::AssertionSuccess();
  };

  // Frame the complete project for selection only; rendered/edit frames retain
  // its saved perspective. Fill genuinely occluded objects explicitly so this
  // cannot silently become a favorable 5,438-of-6,274 benchmark again.
  ASSERT_TRUE(dense_frame_checked(editor.selection().set_mode(QuaderSelectionMode::Object)));
  ASSERT_TRUE(dense_frame_checked(editor.selection().select_all()));
  const auto bounds = session.modeling().selection_bounds();
  ASSERT_TRUE(bounds.has_value());
  const auto center = (bounds->min + bounds->max) * 0.5f;
  const auto extent = bounds->max - bounds->min;
  const float size = std::max({extent.x, extent.y, extent.z, 1.0f});
  const QuaderViewportCamera selection_camera{
      .orthographic = true, .origin = {center.x, center.y, bounds->max.z + size},
      .x_axis = {1.0f, 0.0f, 0.0f}, .y_axis = {0.0f, 1.0f, 0.0f}, .z_axis = {0.0f, 0.0f, 1.0f},
      .viewport_size = raster, .orthographic_size = size * 1.2, .fov_degrees = 60.0};
  const std::array modes{QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge, QuaderSelectionMode::Face};
  const std::array names{"vertex", "edge", "face"};
  for (std::size_t cycle = 0u; cycle < 2u; ++cycle) {
    for (std::size_t mode_index = 0u; mode_index < modes.size(); ++mode_index) {
      const auto mode = modes[mode_index];
      const std::string label = "cycle" + std::to_string(cycle) + "_" + names[mode_index];
      ASSERT_TRUE(camera.restore_perspective_pose(saved_pose));
      ASSERT_TRUE(harness.stage(label + "_selection", 1u, [&](std::size_t) {
        auto result = editor.selection().set_mode(mode);
        if (!result.ok()) return dense_frame_checked(result);
        result = editor.selection().clear();
        if (!result.ok()) return dense_frame_checked(result);
        return dense_frame_checked(editor.selection().box_select({.camera = selection_camera,
            .start = raster, .end = {}, .edit = QuaderSelectionEdit::Replace,
            .rule = QuaderBoxSelectionRule::Crossing, .select_backfaces = false}));
      }));
      const auto exported = session.modeling().export_selection();
      std::unordered_set<int> selected;
      for (const auto &object : exported.objects) {
        if (!object.selected_vertices.empty() || !object.selected_edges.empty() || !object.selected_faces.empty()) {
          selected.insert(object.modeling_object_id);
        }
      }
      std::size_t supplemental = 0u;
      for (const auto &object : objects) {
        if (selected.contains(object.modeling_object_id)) continue;
        const auto reference = editor.query().modeling_object_for_id(object.modeling_object_id);
        const auto *mesh = session.assets().mesh(object.mesh);
        ASSERT_TRUE(reference.has_value());
        ASSERT_NE(mesh, nullptr);
        ASSERT_NE(mesh->authored_polygon_document, nullptr);
        const auto &document = *mesh->authored_polygon_document;
        ASSERT_FALSE(document.vertices.empty());
        ASSERT_FALSE(document.faces.empty());
        const auto &face = document.faces[(cycle + mode_index) % document.faces.size()];
        ASSERT_GE(face.vertices.size(), 2u);
        if (mode == QuaderSelectionMode::Vertex) {
          ASSERT_TRUE(dense_frame_checked(editor.selection().select_vertex(*reference,
              face.vertices.front(), QuaderSelectionEdit::Add)));
        } else if (mode == QuaderSelectionMode::Edge) {
          ASSERT_TRUE(dense_frame_checked(editor.selection().select_edge(*reference,
              face.vertices[0], face.vertices[1], QuaderSelectionEdit::Add)));
        } else {
          ASSERT_TRUE(dense_frame_checked(editor.selection().select_face(*reference,
              face.id, QuaderSelectionEdit::Add)));
        }
        ++supplemental;
      }
      ASSERT_EQ(editor.selection().summary().selected_object_count, kExpectedMeshes);
      std::cout << "DENSE_WORKFLOW_SELECTION stage=" << label << " supplemental_objects=" << supplemental
                << " expected_objects=" << kExpectedMeshes
                << " handles_checksum=" << dense_selection_checksum(
                       dense_selection_handles(session.modeling().export_selection())) << std::endl;
      const bool scale = ((cycle + mode_index) % 2u) != 0u;
      const auto tool = scale ? ViewportToolId::Scale : ViewportToolId::Move;
      DenseGeometryFingerprint before_edit;
      DenseGeometryFingerprint committed_edit;
      DenseGeometryFingerprint restored_edit;
      ASSERT_TRUE(harness.geometry_fingerprint(label + "_before", before_edit));
      ASSERT_TRUE(drag(label, tool, mode_index + cycle));
      ASSERT_TRUE(harness.geometry_fingerprint(label + "_committed", committed_edit));
      ASSERT_NE(before_edit.objects, committed_edit.objects) << label << " did not alter mesh geometry";
      if (qEnvironmentVariable("QUADER_TEST_DENSE_FIRST_DRAG_ONLY") == QStringLiteral("1")) {
        std::cout << "DENSE_WORKFLOW_FIRST_DRAG_COMPLETE diagnostic-only replay" << std::endl;
        return;
      }
      ASSERT_TRUE(navigation(label + "_committed_navigation"));
      ASSERT_TRUE(harness.stage(label + "_undo", 1u, [&](std::size_t) {
        return dense_frame_checked(editor.history().undo());
      }));
      ASSERT_TRUE(harness.stage(label + "_undo_retained_ui", 1u, idle));
      ASSERT_TRUE(harness.geometry_fingerprint(label + "_undone", restored_edit));
      ASSERT_EQ(before_edit.objects, restored_edit.objects) << label << " undo changed compiled geometry";
      ASSERT_TRUE(navigation(label + "_undo_navigation"));
      ASSERT_TRUE(harness.stage(label + "_redo", 1u, [&](std::size_t) {
        return dense_frame_checked(editor.history().redo());
      }));
      ASSERT_TRUE(harness.stage(label + "_redo_retained_ui", 1u, idle));
      ASSERT_TRUE(harness.geometry_fingerprint(label + "_redone", restored_edit));
      ASSERT_EQ(committed_edit.objects, restored_edit.objects) << label << " redo changed compiled geometry";
    }
  }

  ASSERT_TRUE(camera.restore_perspective_pose(saved_pose));
  ASSERT_TRUE(dense_frame_checked(editor.selection().set_mode(QuaderSelectionMode::Vertex)));
  const QPointF keyboard_focus{surface.width() * 0.5, surface.height() * 0.5};
  harness.focus_pointer(keyboard_focus);
  ASSERT_TRUE(harness.stage("all_vertices_native_ctrl_a", 1u, [&](std::size_t) {
    harness.select_all_key();
    return testing::AssertionSuccess();
  }));
  ASSERT_EQ(editor.selection().summary().selected_object_count, kExpectedMeshes);
  ASSERT_EQ(editor.selection().summary().selected_vertex_count, 163670u);
  DenseGeometryFingerprint all_vertices_before;
  DenseGeometryFingerprint all_vertices_committed;
  ASSERT_TRUE(harness.geometry_fingerprint("all_vertices_before", all_vertices_before));
  ASSERT_TRUE(drag("all_vertices", ViewportToolId::Move, 0u));
  ASSERT_TRUE(harness.geometry_fingerprint("all_vertices_committed", all_vertices_committed));
  ASSERT_NE(all_vertices_before.objects, all_vertices_committed.objects);
  ASSERT_TRUE(navigation("all_vertices_committed_navigation"));

  // A common-pivot component deformation separates local geometry at distinct
  // object placements without changing object transforms or bypassing UV rules.
  // Report this variant separately from the original authored-scene replay.
  ASSERT_TRUE(harness.stage("unique_geometry_setup", 1u, [&](std::size_t) {
    return dense_frame_checked(editor.modeling().scale_selection(
        {1.073f, 1.047f, 1.031f}, {0.137f, 0.293f, -0.419f}));
  }));
  std::unordered_set<std::uint64_t> geometry_ids;
  for (const auto &object : editor.query().modeling_objects()) {
    const auto *mesh = session.assets().mesh(object.mesh);
    ASSERT_NE(mesh, nullptr);
    geometry_ids.insert(mesh->geometry().content_id().value);
  }
  std::cout << "DENSE_WORKFLOW_UNIQUE geometry_content_ids=" << geometry_ids.size()
            << " objects=" << kExpectedMeshes << std::endl;
  ASSERT_GE(geometry_ids.size(), 3000u)
      << "This authored-scene variant must retain at least 3,000 distinct payloads";
  ASSERT_EQ(editor.selection().summary().selected_object_count, kExpectedMeshes);
  ASSERT_EQ(editor.selection().summary().selected_vertex_count, 163670u);
  ASSERT_TRUE(camera.restore_perspective_pose(saved_pose));
  DenseGeometryFingerprint unique_before;
  DenseGeometryFingerprint unique_committed;
  DenseGeometryFingerprint unique_restored;
  ASSERT_TRUE(harness.geometry_fingerprint("unique_before", unique_before));
  ASSERT_TRUE(drag("unique_all_vertices", ViewportToolId::Scale, 1u));
  ASSERT_TRUE(harness.geometry_fingerprint("unique_committed", unique_committed));
  ASSERT_NE(unique_before.objects, unique_committed.objects);
  ASSERT_TRUE(navigation("unique_committed_navigation"));
  ASSERT_TRUE(harness.stage("unique_undo", 1u, [&](std::size_t) {
    return dense_frame_checked(editor.history().undo());
  }));
  ASSERT_TRUE(harness.geometry_fingerprint("unique_undone", unique_restored));
  ASSERT_EQ(unique_before.objects, unique_restored.objects);
  ASSERT_TRUE(navigation("unique_undo_navigation"));

  // Retain the original native marquee replay and its frozen user preference.
  // With the recorded BackfaceSelection setting these are X-ray selections;
  // supplementary visible-only CPU/GPU parity is measured separately below.
  ASSERT_TRUE(camera.restore_perspective_pose(saved_pose));
  ASSERT_TRUE(dense_frame_checked(editor.tools().activate(ViewportToolId::Select)));
  for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Face}) {
    const std::string label = mode == QuaderSelectionMode::Vertex ? "native_vertex_marquee" : "native_face_marquee";
    ASSERT_TRUE(dense_frame_checked(editor.selection().set_mode(mode)));
    ASSERT_TRUE(dense_frame_checked(editor.selection().clear()));
    ASSERT_TRUE(harness.stage(label + "_camera_settle", 12u, idle));
    const QPointF start{static_cast<double>(surface.width() - 6), static_cast<double>(surface.height() - 6)};
    const QPointF end{6.0, 6.0};
    harness.focus_pointer(start);
    ASSERT_TRUE(harness.stage(label + "_press", 1u, [&](std::size_t) {
      harness.mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
      return testing::AssertionSuccess();
    }));
    ASSERT_TRUE(harness.stage(label + "_held", 4u, [&](std::size_t index) {
      harness.mouse(QEvent::MouseMove, start + (end - start) * (static_cast<double>(index + 1u) / 4.0),
                    Qt::NoButton, Qt::LeftButton);
      return testing::AssertionSuccess();
    }));
    ASSERT_TRUE(harness.stage(label + "_release", 1u, [&](std::size_t) {
      harness.mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
      return testing::AssertionSuccess();
    }));
    ASSERT_TRUE(editor.selection().summary().has_selection);
    ASSERT_TRUE(harness.stage(label + "_retained_ui", 1u, idle));
  }
#if !defined(QUADER_DENSE_WORKFLOW_BASELINE)
  // These additional stages do not reinterpret the frozen preference replay.
  // Both passes use the same native gesture, camera, geometry and frame count;
  // the CPU oracle detaches only the derived depth certificate before input.
  const bool original_backfaces = session.viewport_workspace().effective_settings(
      ViewportPaneId::Perspective).bool_value(ViewportBoolSettingId::BackfaceSelection);
  if (original_backfaces) {
    ASSERT_TRUE(dense_frame_checked(editor.commands().execute(
        quader_command::kSelectToggleBackfaceSelection)));
  }
  ASSERT_FALSE(session.viewport_workspace().effective_settings(ViewportPaneId::Perspective)
                   .bool_value(ViewportBoolSettingId::BackfaceSelection));
  for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Face}) {
    DenseSelectionHandles cpu_selection;
    const std::string mode_name = mode == QuaderSelectionMode::Vertex ? "vertex" : "face";
    for (const bool cpu_oracle : {true, false}) {
      const std::string label = "supplement_visible_" + mode_name + (cpu_oracle ? "_cpu" : "_gpu");
      ASSERT_TRUE(camera.restore_perspective_pose(saved_pose));
      ASSERT_TRUE(dense_frame_checked(editor.selection().set_mode(mode)));
      ASSERT_TRUE(dense_frame_checked(editor.selection().clear()));
      harness.force_cpu_selection(cpu_oracle);
      ASSERT_TRUE(harness.stage(label + "_camera_settle", 12u, idle));
      const auto selection_before = session.scene_surface_query().stats().selection_depth;
      const auto renderer_before = surface.renderer().component_selection().stats();
      const QPointF start{static_cast<double>(surface.width() - 6), static_cast<double>(surface.height() - 6)};
      const QPointF end{6.0, 6.0};
      harness.focus_pointer(start);
      ASSERT_TRUE(harness.stage(label + "_press", 1u, [&](std::size_t) {
        harness.mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        return testing::AssertionSuccess();
      }));
      ASSERT_TRUE(harness.stage(label + "_held", 4u, [&](std::size_t index) {
        harness.mouse(QEvent::MouseMove, start + (end - start) * (static_cast<double>(index + 1u) / 4.0),
                      Qt::NoButton, Qt::LeftButton);
        return testing::AssertionSuccess();
      }));
      ASSERT_TRUE(harness.stage(label + "_release", 1u, [&](std::size_t) {
        harness.mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
        return testing::AssertionSuccess();
      }));
      harness.force_cpu_selection(false);
      const auto selection_after = session.scene_surface_query().stats().selection_depth;
      const auto selected = dense_selection_handles(session.modeling().export_selection());
      ASSERT_FALSE(selected.empty());
      const auto accepted = selection_after.gpu_accepted_witnesses - selection_before.gpu_accepted_witnesses;
      const auto rejected = selection_after.gpu_rejected_witnesses - selection_before.gpu_rejected_witnesses;
      const auto fallback = selection_after.cpu_fallback_witnesses - selection_before.cpu_fallback_witnesses;
      if (cpu_oracle) {
        cpu_selection = selected;
        ASSERT_EQ(accepted + rejected, 0u);
        ASSERT_GT(fallback, 0u);
      } else {
        ASSERT_EQ(selected, cpu_selection) << mode_name << " GPU/native selection differs from CPU/native oracle";
        ASSERT_GT(renderer_before.completed_readbacks, 0u) << "Settled camera never completed a GPU certificate";
        ASSERT_GT(accepted + rejected, 0u) << "Visible native marquee never exercised the GPU certificate";
      }
      std::cout << "DENSE_WORKFLOW_VISIBLE_PARITY stage=" << label
                << " handles=" << selected.size() << " checksum=" << dense_selection_checksum(selected)
                << " gpu_accepted=" << accepted << " gpu_rejected=" << rejected
                << " cpu_fallback=" << fallback << " oracle=" << cpu_oracle << std::endl;
      ASSERT_TRUE(harness.stage(label + "_retained_ui", 1u, idle));
    }
  }
  if (original_backfaces) {
    ASSERT_TRUE(dense_frame_checked(editor.commands().execute(
        quader_command::kSelectToggleBackfaceSelection)));
  }
  ASSERT_EQ(session.viewport_workspace().effective_settings(ViewportPaneId::Perspective)
                .bool_value(ViewportBoolSettingId::BackfaceSelection), original_backfaces);
#endif
  std::cout << "DENSE_WORKFLOW_COMPLETE entering normal editor/resource shutdown" << std::endl;
}

} // namespace
} // namespace quader
