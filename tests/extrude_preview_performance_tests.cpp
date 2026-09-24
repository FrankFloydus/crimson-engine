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
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QString>
#include <QTimer>

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
    SceneSettings settings;
    settings.environment.enabled = false;
    settings.environment.show_background = false;
    settings.post_process.enabled = false;
    view_.apply_scene_settings(settings, false, ViewportSettings::defaults());
    return create_patch();
  }

  ExtrudeToolService &tool() { return session_.extrude(); }
  QuaderEditorApi &editor() { return editor_; }
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

  void expect_selection_overlay_height(
      double displayed_amount,
      std::optional<double> previous_cap_amount = std::nullopt) const {
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
            if (previous_cap_amount &&
                std::abs(endpoint.z - expected_z) > 0.0000002F) {
              EXPECT_NEAR(endpoint.z, static_cast<float>(*previous_cap_amount /
                  viewport_host_defaults().map_units_per_world_unit), 0.0000002F);
            } else {
              EXPECT_NEAR(endpoint.z, expected_z, 0.0000002F);
            }
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
  ASSERT_TRUE(harness.tool().preview_active());
  ASSERT_DOUBLE_EQ(harness.tool().amount(), 0.0);
  const auto initial_undo = harness.undo_count();
  ASSERT_TRUE(harness.tool().set_amount(1.0).ok);
  EXPECT_EQ(harness.undo_count(), initial_undo);
  ASSERT_TRUE(harness.sync());
  ASSERT_TRUE(harness.render());
  ASSERT_TRUE(harness.finish_gpu());
  const auto initial_caps = harness.cap_ids();
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
      if (repeated) {
        EXPECT_EQ(harness.preview_submission_revision(), before_submission);
      } else {
        EXPECT_GT(harness.preview_submission_revision(), before_submission)
            << "Each accepted position must cross the real render API source gates";
      }
      EXPECT_TRUE(harness.source_suppressed());
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
    print_samples(preview ? "drag_preview" : "pending_settings",
                  repeated ? "identical" : "tiny_changes", samples);
  }
  if (preview) ASSERT_TRUE(harness.tool().release_drag().ok);
  EXPECT_TRUE(harness.tool().preview_active());
  EXPECT_EQ(harness.undo_count(), initial_undo);
  ASSERT_TRUE(harness.tool().commit_preview().ok);
  EXPECT_FALSE(harness.tool().preview_active());
  EXPECT_DOUBLE_EQ(harness.tool().amount(), amount);
  ASSERT_TRUE(harness.sync());
  harness.expect_selection_overlay_height(amount);
  ASSERT_TRUE(harness.render());
  ASSERT_TRUE(harness.finish_gpu());
  const auto mesh = harness.mesh();
  ASSERT_TRUE(mesh.has_value());
  result.mesh = *mesh;
  result.cap_ids = harness.cap_ids();
  EXPECT_EQ(result.cap_ids, initial_caps);
  EXPECT_EQ(harness.undo_count(), initial_undo + 1u);
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
      ASSERT_TRUE(harness.tool().begin_drag().ok);
      ASSERT_TRUE(harness.tool().update_drag(0.002).ok);
      ASSERT_TRUE(harness.sync());
      harness.expect_selection_overlay_height(amount + 0.002, amount);
      EXPECT_TRUE(harness.source_suppressed());
      const auto active_submission = harness.preview_submission_revision();
      if (publication_failure) {
        harness.inject_failure(ModelingSdkProjectionFailurePoint::AfterSdkMutation);
        const auto rejected = harness.tool().update_drag(0.003);
        harness.inject_failure(ModelingSdkProjectionFailurePoint::None);
        ASSERT_FALSE(rejected.ok);
        ASSERT_TRUE(harness.sync());
        harness.expect_selection_overlay_height(amount);
        EXPECT_TRUE(harness.preview_suppression_empty());
        EXPECT_GT(harness.preview_submission_revision(), active_submission);
      }
      ASSERT_TRUE(harness.tool().cancel_drag().ok);
      EXPECT_EQ(harness.undo_count(), initial_undo + 1u);
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
  // Applying closes the operation: the next numeric amount builds a new step
  // from the accepted cap and receives its own history entry.
  ASSERT_TRUE(harness.tool().set_amount(0.002).ok);
  EXPECT_EQ(harness.undo_count(), initial_undo + 1u);
  ASSERT_TRUE(harness.sync());
  harness.expect_selection_overlay_height(amount + 0.002, amount);
  ASSERT_TRUE(harness.tool().commit_preview().ok);
  EXPECT_EQ(harness.undo_count(), initial_undo + 2u);
  EXPECT_DOUBLE_EQ(harness.tool().amount(), 0.002);
  ASSERT_TRUE(harness.sync());
  harness.expect_selection_overlay_height(amount + 0.002, amount);
  EXPECT_TRUE(harness.preview_suppression_empty());
}

/// Measures the existing dispatch slot without changing its connection order or
/// replacing the production numeric gesture and tool-setting adapters.
class NumericDispatchProbe final : public QObject {
public:
  NumericDispatchProbe(QTimer &timer, std::vector<double> &samples)
      : samples_(samples) {
    timer.installEventFilter(this);
    QObject::connect(&timer, &QTimer::timeout, this, [this] {
      samples_.push_back(milliseconds(start_));
    });
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Timer) {
      start_ = PerformanceClock::now();
    }
    return QObject::eventFilter(watched, event);
  }

private:
  std::vector<double> &samples_;
  PerformanceClock::time_point start_;
};

struct NumericInputSample final {
  double value = 0.0;
  PerformanceClock::time_point received;
};

struct NumericDragPerformanceResult final {
  QuaderMeshAsset mesh;
  std::vector<double> candidate_values;
};

void send_numeric_mouse(QLineEdit &editor, QEvent::Type type,
                        QPoint position, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
  const QPointF global = editor.mapToGlobal(position);
  QMouseEvent event(type, QPointF(position), global, button, buttons,
                    Qt::NoModifier);
  QCoreApplication::sendEvent(&editor, &event);
}

QPoint begin_numeric_drag(QLineEdit &editor) {
  const QPoint press = editor.rect().center();
  send_numeric_mouse(editor, QEvent::MouseButtonPress, press,
                      Qt::LeftButton, Qt::LeftButton);
  const QPoint start = press + QPoint(QApplication::startDragDistance() + 1, 0);
  send_numeric_mouse(editor, QEvent::MouseMove, start,
                      Qt::NoButton, Qt::LeftButton);
  return start;
}

void process_numeric_events_for(int duration_ms) {
  QEventLoop loop;
  QTimer::singleShot(duration_ms, &loop, &QEventLoop::quit);
  loop.exec();
}

void measure_numeric_drag(bool legacy_cadence,
                          NumericDragPerformanceResult &result) {
  ExtrudePerformanceHarness harness;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(harness.editor().tools().activate(ViewportToolId::Extrude).ok());
  ASSERT_TRUE(harness.tool().set_amount(1.0).ok);
  ASSERT_TRUE(harness.sync());
  ASSERT_TRUE(harness.render());
  ASSERT_TRUE(harness.finish_gpu());
  const auto initial_undo = harness.undo_count();

  QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.set_expanded(true);
  panel.bind(harness.editor());
  panel.show();
  panel.resize(panel.sizeHint());
  QCoreApplication::processEvents();
  auto *spin = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  ASSERT_NE(spin, nullptr);
  auto *line = spin->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  const auto dispatch_timers = spin->parentWidget()->findChildren<QTimer *>(
      QString(), Qt::FindDirectChildrenOnly);
  ASSERT_EQ(dispatch_timers.size(), 1);
  QTimer &dispatch = *dispatch_timers.front();
  if (legacy_cadence) {
    // Replay the previous UI pacing in the same binary and same scene. The
    // production case deliberately retains the owner's configured interval.
    dispatch.setInterval(50);
  }
  const int dispatch_interval = dispatch.interval();
  ASSERT_GT(dispatch_interval, 0);
  ASSERT_DOUBLE_EQ(spin->value(), 1.0);

  std::vector<double> input_cpu_ms;
  std::vector<double> dispatch_cpu_ms;
  std::vector<double> sync_cpu_ms;
  std::vector<double> render_cpu_ms;
  std::vector<double> displayed_input_age_ms;
  std::vector<double> accepted_input_latency_ms;
  std::vector<double> submission_intervals_ms;
  NumericDispatchProbe dispatch_probe(dispatch, dispatch_cpu_ms);
  constexpr int kPointerSamples = 240;
  std::vector<NumericInputSample> inputs;
  inputs.reserve(kPointerSamples + 1u);
  result.candidate_values.reserve(kPointerSamples);
  const QPoint origin = begin_numeric_drag(*line);
  ASSERT_TRUE(qt_install_numeric_spin_box(*spin).is_dragging());
  const auto started = PerformanceClock::now();
  inputs.push_back({spin->value(), started});
  auto previous_submission_at = started;
  auto presented_revision = harness.preview_submission_revision();
  const auto initial_geometry_updates =
      harness.tool().work_stats_for_tests().geometry_update_count;
  std::size_t published_frames = 0u;
  std::size_t frame_count = 0u;
  int pointer_index = 0;
  QPoint position = origin;
  bool completed = false;
  QEventLoop loop;
  QTimer pointer_timer;
  QTimer frame_timer;
  QTimer deadline;
  pointer_timer.setTimerType(Qt::PreciseTimer);
  pointer_timer.setInterval(4);
  frame_timer.setTimerType(Qt::PreciseTimer);
  frame_timer.setInterval(16);
  deadline.setSingleShot(true);
  deadline.setInterval(10'000);
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  QObject::connect(&pointer_timer, &QTimer::timeout, &loop, [&] {
    ++pointer_index;
    // Identical one-pixel samples with a reversal halfway through both runs.
    const int travel = pointer_index <= kPointerSamples / 2
                           ? pointer_index
                           : kPointerSamples - pointer_index;
    position = origin + QPoint(travel, 0);
    const auto received = PerformanceClock::now();
    send_numeric_mouse(*line, QEvent::MouseMove, position,
                        Qt::NoButton, Qt::LeftButton);
    input_cpu_ms.push_back(milliseconds(received));
    inputs.push_back({spin->value(), received});
    result.candidate_values.push_back(spin->value());
    if (pointer_index == kPointerSamples) {
      completed = true;
      loop.quit();
    }
  });
  QObject::connect(&frame_timer, &QTimer::timeout, &loop, [&] {
    const double amount = harness.tool().amount();
    const auto sync_begin = PerformanceClock::now();
    if (!harness.sync()) {
      ADD_FAILURE() << "Numeric drag failed to synchronize the viewport";
      loop.quit();
      return;
    }
    sync_cpu_ms.push_back(milliseconds(sync_begin));
    const auto render_begin = PerformanceClock::now();
    if (!harness.render()) {
      ADD_FAILURE() << "Numeric drag failed to submit the viewport";
      loop.quit();
      return;
    }
    render_cpu_ms.push_back(milliseconds(render_begin));
    const auto submitted_at = PerformanceClock::now();
    ++frame_count;
    const auto input = std::find_if(inputs.rbegin(), inputs.rend(),
        [amount](const NumericInputSample &sample) {
          return std::abs(sample.value - amount) < 0.000000001;
        });
    if (input == inputs.rend()) {
      ADD_FAILURE() << "The viewport published a value outside the input stream";
      loop.quit();
      return;
    }
    const double age = std::chrono::duration<double, std::milli>(
                           submitted_at - input->received).count();
    // Include frames that still show an older sample: measuring only newly
    // accepted values hides the visible staleness between dispatches.
    displayed_input_age_ms.push_back(age);
    const auto revision = harness.preview_submission_revision();
    if (revision != presented_revision) {
      ++published_frames;
      accepted_input_latency_ms.push_back(age);
      submission_intervals_ms.push_back(
          std::chrono::duration<double, std::milli>(
              submitted_at - previous_submission_at).count());
      previous_submission_at = submitted_at;
      presented_revision = revision;
      harness.expect_selection_overlay_height(amount);
    }
  });
  frame_timer.start();
  pointer_timer.start();
  deadline.start();
  loop.exec();
  pointer_timer.stop();
  frame_timer.stop();
  deadline.stop();
  const double elapsed_ms = milliseconds(started);
  ASSERT_TRUE(completed) << "Numeric input benchmark exceeded its bounded run";
  ASSERT_FALSE(::testing::Test::HasFailure());
  const double final_value = spin->value();
  send_numeric_mouse(*line, QEvent::MouseButtonRelease, position,
                      Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(qt_install_numeric_spin_box(*spin).is_dragging());
  EXPECT_DOUBLE_EQ(harness.tool().amount(), final_value);
  EXPECT_EQ(harness.undo_count(), initial_undo);
  ASSERT_TRUE(harness.sync());
  harness.expect_selection_overlay_height(final_value);
  ASSERT_TRUE(harness.render());
  ASSERT_TRUE(harness.finish_gpu());
  const auto final_geometry = harness.sample_geometry();
  ASSERT_TRUE(final_geometry.has_value());
  const float expected_z = static_cast<float>(final_value /
      viewport_host_defaults().map_units_per_world_unit);
  EXPECT_NEAR(final_geometry->cap_min_z, expected_z, 0.0000002F);
  EXPECT_NEAR(final_geometry->cap_max_z, expected_z, 0.0000002F);

  ASSERT_FALSE(input_cpu_ms.empty());
  ASSERT_FALSE(dispatch_cpu_ms.empty());
  ASSERT_FALSE(sync_cpu_ms.empty());
  ASSERT_FALSE(render_cpu_ms.empty());
  ASSERT_FALSE(accepted_input_latency_ms.empty());
  std::cout << std::fixed << std::setprecision(3)
            << "NUMERIC_DRAG_PERF mode="
            << (legacy_cadence ? "legacy_50ms" : "production")
            << " faces=" << kGridSide * kGridSide
            << " selected=" << kSelectedSide * kSelectedSide
            << " dispatch_interval_ms=" << dispatch_interval
            << " input_samples=" << pointer_index
            << " frames=" << frame_count
            << " changed_submissions=" << published_frames
            << " geometry_updates="
            << harness.tool().work_stats_for_tests().geometry_update_count -
                   initial_geometry_updates
            << " wall_ms=" << elapsed_ms
            << " changed_submission_hz=" << published_frames * 1000.0 / elapsed_ms
            << " displayed_input_age_p50_ms=" << percentile(displayed_input_age_ms, 0.50)
            << " displayed_input_age_p95_ms=" << percentile(displayed_input_age_ms, 0.95)
            << " accepted_input_latency_p50_ms=" << percentile(accepted_input_latency_ms, 0.50)
            << " accepted_input_latency_p95_ms=" << percentile(accepted_input_latency_ms, 0.95)
            << " changed_submission_interval_p50_ms=" << percentile(submission_intervals_ms, 0.50)
            << " changed_submission_interval_p95_ms=" << percentile(submission_intervals_ms, 0.95)
            << " input_cpu_p50_ms=" << percentile(input_cpu_ms, 0.50)
            << " input_cpu_p95_ms=" << percentile(input_cpu_ms, 0.95)
            << " dispatch_cpu_p50_ms=" << percentile(dispatch_cpu_ms, 0.50)
            << " dispatch_cpu_p95_ms=" << percentile(dispatch_cpu_ms, 0.95)
            << " sync_cpu_p50_ms=" << percentile(sync_cpu_ms, 0.50)
            << " sync_cpu_p95_ms=" << percentile(sync_cpu_ms, 0.95)
            << " submit_cpu_p50_ms=" << percentile(render_cpu_ms, 0.50)
            << " submit_cpu_p95_ms=" << percentile(render_cpu_ms, 0.95) << '\n';

  // Cancellation must restore the latest accepted scalar through the real
  // retained render gates, with no delayed sample resurrecting the draft.
  const QPoint cancel_origin = begin_numeric_drag(*line);
  send_numeric_mouse(*line, QEvent::MouseMove, cancel_origin + QPoint(8, 0),
                      Qt::NoButton, Qt::LeftButton);
  process_numeric_events_for(dispatch_interval + 32);
  EXPECT_GT(harness.tool().amount(), final_value);
  ASSERT_TRUE(harness.sync());
  const auto canceled_submission = harness.preview_submission_revision();
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(line, &escape);
  EXPECT_DOUBLE_EQ(harness.tool().amount(), final_value);
  ASSERT_TRUE(harness.sync());
  EXPECT_GT(harness.preview_submission_revision(), canceled_submission);
  harness.expect_selection_overlay_height(final_value);
  const auto idle_submission = harness.preview_submission_revision();
  const auto idle_overlay_builds = harness.selection_overlay_builds();
  const auto idle_updates = harness.tool().work_stats_for_tests().geometry_update_count;
  const auto idle_diagnostics = harness.diagnostics();
  process_numeric_events_for(dispatch_interval * 2 + 16);
  for (int frame = 0; frame < 3; ++frame) {
    ASSERT_TRUE(harness.sync());
    ASSERT_TRUE(harness.render());
  }
  EXPECT_EQ(harness.tool().work_stats_for_tests().geometry_update_count, idle_updates);
  EXPECT_EQ(harness.preview_submission_revision(), idle_submission);
  EXPECT_EQ(harness.selection_overlay_builds(), idle_overlay_builds);
  EXPECT_EQ(harness.diagnostics().gpu_geometry_upload_count,
            idle_diagnostics.gpu_geometry_upload_count);
  EXPECT_DOUBLE_EQ(harness.tool().amount(), final_value);
  EXPECT_EQ(harness.undo_count(), initial_undo);
  panel.unbind();
  ASSERT_TRUE(harness.tool().commit_preview().ok);
  EXPECT_EQ(harness.undo_count(), initial_undo + 1u);
  ASSERT_TRUE(harness.sync());
  ASSERT_TRUE(harness.finish_gpu());
  const auto mesh = harness.mesh();
  ASSERT_TRUE(mesh.has_value());
  result.mesh = *mesh;
}

TEST(ExtrudePreviewPerformanceTests,
     NumericDragCadenceReportsSameSceneLatencyAndPreservesFinalGeometry) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_EXTRUDE_PERFORMANCE") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_EXTRUDE_PERFORMANCE=1 for the controlled "
                   "numeric input and windowless Vulkan extrusion benchmark";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-numeric-drag-performance-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStringLiteral("Fusion"));
  NumericDragPerformanceResult legacy;
  NumericDragPerformanceResult production;
  measure_numeric_drag(true, legacy);
  ASSERT_FALSE(HasFatalFailure());
  measure_numeric_drag(false, production);
  ASSERT_FALSE(HasFatalFailure());
  EXPECT_EQ(legacy.candidate_values, production.candidate_values);
  test::curve::expect_same_mesh_geometry(legacy.mesh, production.mesh);
}

TEST(ExtrudePreviewPerformanceTests,
     PendingSettingsAndDragHaveMatchingResultsAndReportCosts) {
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
  PerformanceResult settings;
  PerformanceResult preview;
  measure_path(false, settings);
  ASSERT_FALSE(HasFatalFailure());
  measure_path(true, preview);
  ASSERT_FALSE(HasFatalFailure());
  EXPECT_EQ(preview.changed.asset_revision_delta, 0u)
      << "Dragging must keep the published asset unchanged until acceptance";
  EXPECT_EQ(settings.changed.asset_revision_delta, 0u)
      << "Numeric settings must keep the published asset unchanged until acceptance";
  EXPECT_LE(preview.changed.after.gpu_geometry_resource_count,
            preview.changed.before.gpu_geometry_resource_count + 1u)
      << "Tiny samples must reuse mutable preview geometry";
  EXPECT_LE(preview.changed.peak_renderables,
            preview.changed.first_renderables + 6u)
      << "At most the active and preceding three-pass preview may be retained";
  EXPECT_EQ(preview.changed.layout_changes, 0u)
      << "Stable positive extrusion samples must preserve source IDs and triangle layout";
  EXPECT_EQ(preview.cap_ids, settings.cap_ids);
  test::curve::expect_same_mesh_geometry(preview.mesh, settings.mesh);
  ASSERT_FALSE(HasFailure());
  std::cout << "EXTRUDE_PERF semantic_match=1 final_vertices="
            << preview.mesh.vertices.size() << " final_indices="
            << preview.mesh.indices.size() << " cap_ids=" << preview.cap_ids.size()
            << " (static GPU counters exclude mutable preview stream uploads)\n";
}

} // namespace
} // namespace quader
