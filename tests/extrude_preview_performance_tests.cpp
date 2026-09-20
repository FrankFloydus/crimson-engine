////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "render/filament_material_library.hpp"
#include "render/png_texture_loader.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_renderer.hpp"
#include "support/curve_modeling_test_support.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QString>

#include <filament/RenderableManager.h>
#include <filament/SwapChain.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

using PerformanceClock = std::chrono::steady_clock;
constexpr int kGridSide = 32;
constexpr int kSelectedSide = 16;
constexpr int kSampleCount = 100;

double milliseconds(PerformanceClock::time_point start) {
  return std::chrono::duration<double, std::milli>(PerformanceClock::now() - start)
      .count();
}

double percentile(std::vector<double> samples, double quantile) {
  std::sort(samples.begin(), samples.end());
  return samples[static_cast<std::size_t>(
      std::ceil(quantile * static_cast<double>(samples.size())) - 1.0)];
}

struct PerformanceSamples final {
  std::vector<double> update_ms;
  std::vector<double> sync_ms;
  std::vector<double> render_ms;
  double total_ms = 0.0;
  std::size_t peak_renderables = 0u;
  std::size_t first_renderables = 0u;
  SceneRenderResourceDiagnostics before;
  SceneRenderResourceDiagnostics after;
  std::uint64_t asset_revision_delta = 0u;
  std::uint64_t geometry_update_delta = 0u;
  std::size_t layout_changes = 0u;
};

struct SampleGeometry final {
  float cap_min_z = std::numeric_limits<float>::infinity();
  float cap_max_z = -std::numeric_limits<float>::infinity();
  std::size_t vertices = 0u;
  std::size_t indices = 0u;
  std::uint64_t layout_signature = 1469598103934665603ULL;
};

struct PerformanceResult final {
  QuaderMeshAsset mesh;
  std::vector<quader_poly::ElementId> cap_ids;
  PerformanceSamples changed;
  PerformanceSamples repeated;
};

/// Keeps production scene/preview synchronization and GPU resource lifetimes
/// in the measurement. Geometry comes exclusively from existing tool commands.
class ExtrudePerformanceHarness final
    : public ViewportOverlaySelectionFrameProvider {
public:
  ExtrudePerformanceHarness()
      : editor_(QuaderApiSessionInternalAccess::from_scene_session(session_)) {}

  ~ExtrudePerformanceHarness() {
    if (engine_ == nullptr) return;
    (void)finish_gpu();
    view_.shutdown();
    bridge_.shutdown();
    registry_.clear();
    materials_.shutdown();
    if (neutral_) engine_->destroy(neutral_);
    if (swapchain_) engine_->destroy(swapchain_);
    if (renderer_) engine_->destroy(renderer_);
    if (scene_) engine_->destroy(scene_);
    filament::Engine::destroy(&engine_);
  }

  bool initialize() {
    const std::filesystem::path resources(
        QCoreApplication::applicationDirPath().toStdWString());
    engine_ = filament::Engine::create(filament::Engine::Backend::VULKAN);
    if (engine_ == nullptr) return false;
    scene_ = engine_->createScene();
    renderer_ = engine_->createRenderer();
    swapchain_ = engine_->createSwapChain(640u, 480u,
                                         filament::SwapChain::CONFIG_READABLE);
    if (!scene_ || !renderer_ || !swapchain_ ||
        !materials_.initialize(*engine_, resources)) return false;
    const std::array<std::byte, 4u> gray{
        std::byte{181u}, std::byte{181u}, std::byte{181u}, std::byte{255u}};
    neutral_ = load_rgba_texture(*engine_, gray, 1u, 1u);
    if (neutral_ == nullptr) return false;
    bridge_.initialize(
        *engine_, *scene_, materials_.quader_mesh(), materials_.quader_mesh_lit(),
        materials_.vertex_color_overlay(), materials_.instance_selection_fill(),
        materials_.overlay_line(), materials_.selected_object_line(),
        materials_.edit_wire_line(), materials_.overlay_point(),
        materials_.overlay_icon(), *neutral_, resources);
    view_.initialize(*engine_, *scene_, registry_, materials_.ground_grid(),
                     viewport_grid_layer_mask(0u), ViewportRenderSurface::Swapchain);
    camera_.set_viewport_size(640.0F, 480.0F);
    camera_.focus_origin(4.0F);
    camera_.set_view_direction({0.0F, 0.0F, -1.0F});
    camera_.set_clip_planes(0.01F, 100.0F);
    view_.resize(640u, 480u);
    view_.update_camera(camera_);
    view_.set_overlay_visible(true);
    view_.set_scene_layer_mask(kViewportShadedSceneLayerMask);
    view_.set_shadowing_enabled(false);
    SceneSettingsComponent settings;
    settings.environment.enabled = false;
    settings.environment.show_background = false;
    settings.post_process.enabled = false;
    view_.apply_scene_settings(settings, false, ViewportSettings::defaults());
    return create_patch();
  }

  ExtrudeToolService &tool() { return session_.extrude(); }
  SceneRenderResourceDiagnostics diagnostics() const { return bridge_.diagnostics(); }
  std::size_t renderables() const {
    return engine_->getRenderableManager().getComponentCount();
  }
  std::uint64_t asset_revision() const { return session_.assets().revision(); }
  std::uint64_t preview_submission_revision() const {
    return bridge_.tool_preview_submission_revision();
  }
  bool source_suppressed() const {
    const auto &entities = bridge_.tool_preview_suppression().entities;
    return std::find(entities.begin(), entities.end(), object_.entity) != entities.end();
  }
  bool preview_suppression_empty() const {
    return bridge_.tool_preview_suppression().empty();
  }
  void inject_failure(ModelingSdkProjectionFailurePoint point) {
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(point);
  }

  bool sync() {
    if (!QuaderRenderApiRuntimeAccess::sync_scene(editor_.render(), bridge_).ok() ||
        !QuaderRenderApiRuntimeAccess::sync_tool_previews(editor_.render(), bridge_).ok()) {
      return false;
    }
    const auto &modeling = session_.modeling();
    const ViewportOverlaySelectionSourceStamp selection_source{
        .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
        .content_revision = modeling.content_revision(),
        .selection_revision = modeling.selection_revision(),
        .scene_revision = session_.scene().revision(),
        .asset_revision = session_.assets().revision(),
        .overlay_signature = modeling.selection_overlay_signature(),
        .selection_context = ViewportOverlaySelectionContext::Face,
    };
    (void)bridge_.submit_viewport_overlay(
        {.viewport_index = 0u,
         .layer_mask = viewport_grid_layer_mask(0u),
         .camera = ViewportCameraAdapter::overlay_camera(camera_, {640.0F, 480.0F}),
         .selection_source = selection_source,
         .selection_frame_provider = this,
         .scene_selection_mode = RenderOverlaySelectionMode::Component,
         .style_selection_context = ViewportOverlaySelectionContext::Face,
         .overlays_visible = true},
        ViewportSettings::defaults());
    return true;
  }

  ViewportOverlayFrame build_selection_overlay_frame() const override {
    ++selection_overlay_builds_;
    selection_overlay_ = session_.modeling().selection_overlay_frame();
    return selection_overlay_;
  }

  std::uint64_t selection_overlay_builds() const {
    return selection_overlay_builds_;
  }

  void expect_selection_overlay_height(double displayed_amount) const {
    const float expected_z = static_cast<float>(displayed_amount /
        viewport_host_defaults().map_units_per_world_unit);
    std::size_t selected_cap_lines = 0u;
    std::size_t elevated_source_endpoints = 0u;
    for (const auto &line : selection_overlay_.lines) {
      if (line.layer == ViewportOverlayLayer::SelectedFaceEdge) {
        ++selected_cap_lines;
        EXPECT_NEAR(line.start.z, expected_z, 0.0000002F);
        EXPECT_NEAR(line.end.z, expected_z, 0.0000002F);
      } else if (line.layer == ViewportOverlayLayer::SourceWire) {
        for (const auto &endpoint : {line.start, line.end}) {
          if (endpoint.z > 0.0000001F) {
            ++elevated_source_endpoints;
            EXPECT_NEAR(endpoint.z, expected_z, 0.0000002F);
          }
        }
      }
    }
    EXPECT_GT(selected_cap_lines, 0u);
    EXPECT_GT(elevated_source_endpoints, 0u);
  }

  bool render() {
    for (int attempt = 0; attempt < 20; ++attempt) {
      if (renderer_->beginFrame(swapchain_)) {
        view_.render(*renderer_);
        renderer_->endFrame();
        return true;
      }
      if (!engine_->flushAndWait(1'000'000'000ULL)) return false;
      engine_->pumpMessageQueues();
    }
    return false;
  }

  bool finish_gpu() {
    const bool completed = engine_->flushAndWait(10'000'000'000ULL);
    engine_->pumpMessageQueues();
    return completed;
  }

  std::optional<QuaderMeshAsset> mesh() {
    return editor_.query().compiled_object_mesh(object_);
  }

  std::optional<SampleGeometry> sample_geometry() const {
    const auto previews = session_.modeling().tool_preview_meshes();
    const auto preview = std::find_if(previews.begin(), previews.end(),
        [&](const auto &candidate) { return candidate.source_entity == object_.entity; });
    std::shared_ptr<const MeshGeometryPayload> payload;
    std::shared_ptr<const std::vector<QVec3>> positions;
    if (preview != previews.end()) {
      payload = preview->mesh.geometry_payload();
      positions = preview->dynamic_position_payload;
    } else {
      const auto *binding = session_.scene().mesh(object_.entity);
      const auto *asset = binding ? session_.assets().mesh(binding->mesh) : nullptr;
      if (asset == nullptr) return std::nullopt;
      payload = asset->geometry_payload();
    }
    if (payload == nullptr ||
        (positions && positions->size() != payload->vertices.size())) return std::nullopt;
    SampleGeometry result;
    result.vertices = payload->vertices.size();
    result.indices = payload->indices.size();
    const auto mix = [&](std::uint32_t value) {
      result.layout_signature ^= value;
      result.layout_signature *= 1099511628211ULL;
    };
    for (std::size_t index = 0; index < payload->vertices.size(); ++index) {
      const auto &vertex = payload->vertices[index];
      const QVec3 position = positions ? (*positions)[index] : vertex.position;
      if (position.z > 0.0000001F) {
        result.cap_min_z = std::min(result.cap_min_z, position.z);
        result.cap_max_z = std::max(result.cap_max_z, position.z);
      }
      mix(vertex.source_vertex_id);
    }
    for (const auto index : payload->indices) mix(index);
    return result;
  }

  std::size_t selected_face_count() const {
    return session_.modeling().selection_summary().selected_face_count;
  }

  std::vector<quader_poly::ElementId> cap_ids() const {
    std::vector<quader_poly::ElementId> result;
    for (const auto &object : session_.modeling().export_selection().objects) {
      for (const auto &face : object.selected_faces) result.push_back(face.id);
    }
    std::sort(result.begin(), result.end());
    return result;
  }

  std::size_t undo_count() const { return session_.history().undo_count(); }
  std::size_t mesh_count() const { return session_.assets().mesh_count(); }

private:
  bool create_patch() {
    quader_poly::Document document;
    std::vector<quader_poly::ElementId> vertices;
    vertices.reserve((kGridSide + 1) * (kGridSide + 1));
    for (int y = 0; y <= kGridSide; ++y) {
      for (int x = 0; x <= kGridSide; ++x) {
        vertices.push_back(quader_poly::add_vertex(document,
            {static_cast<float>(x) / kGridSide * 2.0F - 1.0F,
             static_cast<float>(y) / kGridSide * 2.0F - 1.0F, 0.0F}));
      }
    }
    std::vector<QuaderElementId> selected;
    constexpr int kFirstSelected = (kGridSide - kSelectedSide) / 2;
    for (int y = 0; y < kGridSide; ++y) {
      for (int x = 0; x < kGridSide; ++x) {
        const auto index = static_cast<std::size_t>(y * (kGridSide + 1) + x);
        const auto face = quader_poly::add_face(document, std::array{
            vertices[index], vertices[index + 1u],
            vertices[index + kGridSide + 2u], vertices[index + kGridSide + 1u]});
        if (x >= kFirstSelected && x < kFirstSelected + kSelectedSide &&
            y >= kFirstSelected && y < kFirstSelected + kSelectedSide) {
          selected.push_back(face);
        }
      }
    }
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Extrusion performance patch"});
    if (!created.ok()) return false;
    object_ = created.value();
    return editor_.selection().set_mode(QuaderSelectionMode::Face).ok() &&
           editor_.selection().select_components(
               {.object = object_, .kind = QuaderComponentKind::Face,
                .faces = selected}).ok() &&
           tool().activate().ok;
  }

  SceneSession session_;
  QuaderEditorApi editor_;
  QuaderModelingObjectRef object_;
  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Renderer *renderer_ = nullptr;
  filament::SwapChain *swapchain_ = nullptr;
  filament::Texture *neutral_ = nullptr;
  FilamentMaterialLibrary materials_;
  TextureRegistry registry_;
  SceneRenderBridge bridge_;
  ViewportRenderer view_;
  ViewportCameraController camera_;
  mutable ViewportOverlayFrame selection_overlay_;
  mutable std::uint64_t selection_overlay_builds_ = 0u;
};

void print_samples(std::string_view path, std::string_view phase,
                   const PerformanceSamples &samples) {
  const auto &before = samples.before;
  const auto &after = samples.after;
  std::cout << std::fixed << std::setprecision(3)
            << "EXTRUDE_PERF path=" << path << " phase=" << phase
            << " faces=" << kGridSide * kGridSide
            << " selected=" << kSelectedSide * kSelectedSide
            << " samples=" << samples.update_ms.size()
            << " update_p50_ms=" << percentile(samples.update_ms, 0.50)
            << " update_p95_ms=" << percentile(samples.update_ms, 0.95)
            << " sync_p50_ms=" << percentile(samples.sync_ms, 0.50)
            << " sync_p95_ms=" << percentile(samples.sync_ms, 0.95)
            << " submit_p50_ms=" << percentile(samples.render_ms, 0.50)
            << " submit_p95_ms=" << percentile(samples.render_ms, 0.95)
            << " validated_loop_wall_ms=" << samples.total_ms
            << " static_resource_delta="
            << static_cast<std::int64_t>(after.gpu_geometry_resource_count) -
                   static_cast<std::int64_t>(before.gpu_geometry_resource_count)
            << " static_upload_delta="
            << after.gpu_geometry_upload_count - before.gpu_geometry_upload_count
            << " static_vertex_bytes="
            << after.gpu_uploaded_vertex_bytes - before.gpu_uploaded_vertex_bytes
            << " static_index_bytes="
            << after.gpu_uploaded_index_bytes - before.gpu_uploaded_index_bytes
            << " allocated_renderables_initial=" << samples.first_renderables
            << " allocated_renderables_peak=" << samples.peak_renderables
            << " asset_revision_delta=" << samples.asset_revision_delta
            << " tool_geometry_updates=" << samples.geometry_update_delta
            << " layout_changes=" << samples.layout_changes << '\n';
}

void measure_path(bool preview, PerformanceResult &result) {
  ExtrudePerformanceHarness harness;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(harness.sync());
  ASSERT_TRUE(harness.render());
  ASSERT_TRUE(harness.finish_gpu());
  const auto initial_caps = harness.cap_ids();
  const auto initial_undo = harness.undo_count();
  const auto initial_meshes = harness.mesh_count();
  ASSERT_EQ(initial_caps.size(), kSelectedSide * kSelectedSide);
  if (preview) ASSERT_TRUE(harness.tool().begin_adjust_drag().ok);

  double amount = 1.0;
  double previous_amount = amount;
  std::optional<SampleGeometry> previous_geometry;
  for (const bool repeated : {false, true}) {
    PerformanceSamples &samples = repeated ? result.repeated : result.changed;
    samples.before = harness.diagnostics();
    samples.first_renderables = harness.renderables();
    samples.peak_renderables = samples.first_renderables;
    const auto before_asset = harness.asset_revision();
    const auto before_updates = harness.tool().work_stats_for_tests().geometry_update_count;
    const auto total_begin = PerformanceClock::now();
    for (int index = 0; index < kSampleCount; ++index) {
      // One continuous sub-grid movement, with a direction reversal halfway.
      // Settings and pointer preview receive identical displayed-unit values.
      if (!repeated) {
        amount = 1.0 + (index < 50 ? static_cast<double>(index + 1)
                                   : 99.5 - static_cast<double>(index)) * 0.0001;
      }
      const auto update_begin = PerformanceClock::now();
      const auto changed = preview ? harness.tool().update_drag(amount)
                                   : harness.tool().set_amount(amount);
      samples.update_ms.push_back(milliseconds(update_begin));
      ASSERT_TRUE(changed.ok) << changed.message;
      const auto before_submission = harness.preview_submission_revision();
      const auto before_overlay_builds = harness.selection_overlay_builds();
      const auto sync_begin = PerformanceClock::now();
      ASSERT_TRUE(harness.sync());
      samples.sync_ms.push_back(milliseconds(sync_begin));
      EXPECT_EQ(harness.selection_overlay_builds(),
                before_overlay_builds + (repeated ? 0u : 1u));
      harness.expect_selection_overlay_height(amount);
      if (preview) {
        if (repeated) {
          EXPECT_EQ(harness.preview_submission_revision(), before_submission);
        } else {
          EXPECT_GT(harness.preview_submission_revision(), before_submission)
              << "Each accepted position must cross the real render API source gates";
        }
        EXPECT_TRUE(harness.source_suppressed());
      }
      samples.peak_renderables = std::max(samples.peak_renderables,
                                         harness.renderables());
      // Verify the exact payload consumed by the renderer, outside both timed
      // CPU stages. A timing improvement alone cannot establish drag stability.
      const auto geometry = harness.sample_geometry();
      ASSERT_TRUE(geometry.has_value());
      ASSERT_EQ(harness.selected_face_count(), kSelectedSide * kSelectedSide);
      const float expected_z = static_cast<float>(amount /
          viewport_host_defaults().map_units_per_world_unit);
      EXPECT_NEAR(geometry->cap_min_z, expected_z, 0.0000002F);
      EXPECT_NEAR(geometry->cap_max_z, expected_z, 0.0000002F);
      if (previous_geometry) {
        EXPECT_EQ(geometry->vertices, previous_geometry->vertices);
        EXPECT_EQ(geometry->indices, previous_geometry->indices);
        if (amount > previous_amount) {
          EXPECT_GT(geometry->cap_min_z, previous_geometry->cap_min_z);
        } else if (amount < previous_amount) {
          EXPECT_LT(geometry->cap_max_z, previous_geometry->cap_max_z);
        }
        samples.layout_changes += geometry->layout_signature !=
                                  previous_geometry->layout_signature ? 1u : 0u;
      }
      previous_geometry = geometry;
      previous_amount = amount;
      const auto render_begin = PerformanceClock::now();
      ASSERT_TRUE(harness.render());
      samples.render_ms.push_back(milliseconds(render_begin));
    }
    ASSERT_TRUE(harness.finish_gpu());
    samples.total_ms = milliseconds(total_begin);
    samples.after = harness.diagnostics();
    samples.asset_revision_delta = harness.asset_revision() - before_asset;
    samples.geometry_update_delta =
        harness.tool().work_stats_for_tests().geometry_update_count - before_updates;
    print_samples(preview ? "drag_preview" : "committed_settings",
                  repeated ? "identical" : "tiny_changes", samples);
  }
  if (preview) ASSERT_TRUE(harness.tool().end_drag().ok);
  ASSERT_TRUE(harness.sync());
  harness.expect_selection_overlay_height(amount);
  ASSERT_TRUE(harness.render());
  ASSERT_TRUE(harness.finish_gpu());
  const auto mesh = harness.mesh();
  ASSERT_TRUE(mesh.has_value());
  result.mesh = *mesh;
  result.cap_ids = harness.cap_ids();
  EXPECT_EQ(result.cap_ids, initial_caps);
  EXPECT_EQ(harness.undo_count(), initial_undo);
  EXPECT_EQ(harness.mesh_count(), initial_meshes);
  EXPECT_EQ(result.repeated.asset_revision_delta, 0u);
  EXPECT_EQ(result.repeated.geometry_update_delta, 0u);
  EXPECT_EQ(result.repeated.after.gpu_geometry_upload_count,
            result.repeated.before.gpu_geometry_upload_count);
  EXPECT_EQ(result.repeated.after.gpu_geometry_resource_count,
            result.repeated.before.gpu_geometry_resource_count);
  if (preview) {
    EXPECT_TRUE(harness.preview_suppression_empty());
    // Exercise source-stamp retirement through the real render API after an
    // accepted dynamic sample. Owner getters alone cannot catch a stale gate.
    for (const bool publication_failure : {false, true}) {
      ASSERT_TRUE(harness.tool().begin_adjust_drag().ok);
      ASSERT_TRUE(harness.tool().update_drag(amount + 0.002).ok);
      ASSERT_TRUE(harness.sync());
      harness.expect_selection_overlay_height(amount + 0.002);
      EXPECT_TRUE(harness.source_suppressed());
      const auto active_submission = harness.preview_submission_revision();
      if (publication_failure) {
        harness.inject_failure(ModelingSdkProjectionFailurePoint::AfterSdkMutation);
        const auto rejected = harness.tool().update_drag(amount + 0.003);
        harness.inject_failure(ModelingSdkProjectionFailurePoint::None);
        ASSERT_FALSE(rejected.ok);
        ASSERT_TRUE(harness.sync());
        harness.expect_selection_overlay_height(amount);
        EXPECT_TRUE(harness.preview_suppression_empty());
        EXPECT_GT(harness.preview_submission_revision(), active_submission);
      }
      ASSERT_TRUE(harness.tool().cancel_drag().ok);
      ASSERT_TRUE(harness.sync());
      harness.expect_selection_overlay_height(amount);
      EXPECT_TRUE(harness.preview_suppression_empty());
      EXPECT_GT(harness.preview_submission_revision(), active_submission);
      const auto retired_submission = harness.preview_submission_revision();
      const auto retired_overlay_builds = harness.selection_overlay_builds();
      ASSERT_TRUE(harness.sync());
      EXPECT_EQ(harness.preview_submission_revision(), retired_submission);
      EXPECT_EQ(harness.selection_overlay_builds(), retired_overlay_builds);
      const auto restored = harness.mesh();
      ASSERT_TRUE(restored.has_value());
      test::curve::expect_same_mesh_geometry(*restored, result.mesh);
    }
  }
}

TEST(ExtrudePreviewPerformanceTests,
     CommittedSettingsAndDragHaveMatchingResultsAndReportCosts) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_EXTRUDE_PERFORMANCE") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_EXTRUDE_PERFORMANCE=1 for the controlled "
                   "modeling and windowless Vulkan extrusion benchmark";
  }
  int argc = 1;
  char application_name[] = "quader-extrude-performance-test";
  char *argv[] = {application_name, nullptr};
  std::unique_ptr<QCoreApplication> application;
  if (QCoreApplication::instance() == nullptr) {
    application = std::make_unique<QCoreApplication>(argc, argv);
  }
  PerformanceResult committed;
  PerformanceResult preview;
  measure_path(false, committed);
  ASSERT_FALSE(HasFatalFailure());
  measure_path(true, preview);
  ASSERT_FALSE(HasFatalFailure());
  EXPECT_EQ(preview.changed.asset_revision_delta, 0u)
      << "Dragging must keep the published asset unchanged until acceptance";
  EXPECT_LE(preview.changed.after.gpu_geometry_resource_count,
            preview.changed.before.gpu_geometry_resource_count + 1u)
      << "Tiny samples must reuse mutable preview geometry";
  EXPECT_LE(preview.changed.peak_renderables,
            preview.changed.first_renderables + 6u)
      << "At most the active and preceding three-pass preview may be retained";
  EXPECT_EQ(preview.changed.layout_changes, 0u)
      << "Stable positive extrusion samples must preserve source IDs and triangle layout";
  EXPECT_EQ(preview.cap_ids, committed.cap_ids);
  test::curve::expect_same_mesh_geometry(preview.mesh, committed.mesh);
  ASSERT_FALSE(HasFailure());
  std::cout << "EXTRUDE_PERF semantic_match=1 final_vertices="
            << preview.mesh.vertices.size() << " final_indices="
            << preview.mesh.indices.size() << " cap_ids=" << preview.cap_ids.size()
            << " (static GPU counters exclude mutable preview stream uploads)\n";
}

} // namespace
} // namespace quader
