////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/session/scene_session.hpp"
#include "render/render_host.hpp"
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
#include "render/crimson/crimson_render_host.hpp"
#endif
#include "render/overlay/render_overlay_snapshot.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "ui/qt_vulkan_composition_widget.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"

#include <quader/modeling/neutral/polygon/document_model.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

struct ProbeLaminaDiagnostics final {
  std::size_t triangles = 0u;
  std::size_t lines = 0u;
  bool face_style_correct = true;
  bool line_style_correct = true;
  float minimum_y = 0.0f;
  float maximum_y = 0.0f;
  std::size_t horizontal_triangles = 0u;
};

/** Observes the production sink's submitted bounds without replacing its render path. */
class ProbeSceneSink final : public RenderSceneSink {
public:
  explicit ProbeSceneSink(std::unique_ptr<RenderSceneSink> sink) : sink_(std::move(sink)) {}
  void shutdown() override { sink_->shutdown(); }
  void set_project_material_resolver(const RenderProjectMaterialResolver *resolver) override {
    sink_->set_project_material_resolver(resolver);
  }
  const RenderProjectMaterialResolver *project_material_resolver() const override {
    return sink_->project_material_resolver();
  }
  void bind_scene(RenderSceneBindingKey binding) override { sink_->bind_scene(binding); }
  void set_backface_culling_enabled(bool enabled) override { sink_->set_backface_culling_enabled(enabled); }
  void sync(const SceneDocument &scene, const AssetRegistry &assets, std::uint64_t frame,
            RenderSceneBindingKey binding, RenderInstanceEditScope scope,
            const ViewportLocalVisibilitySnapshot *visibility) override {
    sink_->sync(scene, assets, frame, binding, scope, visibility);
  }
  void sync(RenderSceneSnapshot snapshot) override { sink_->sync(std::move(snapshot)); }
  void update_mesh_surface_grid(float spacing, const ViewportSettings &settings) override {
    sink_->update_mesh_surface_grid(spacing, settings);
  }
  void sync_scene_asset_overlays(const RenderOverlaySelectedObjectSnapshot &selected,
      const RenderOverlaySceneWireframeSnapshot &wireframe,
      const RenderOverlayCollisionSnapshot &collision) override {
    sink_->sync_scene_asset_overlays(selected, wireframe, collision);
  }
  RenderToolPreviewSceneSnapshot build_tool_preview_scene_snapshot(ToolPreviewScene preview,
      const AssetRegistry &assets, const ViewportLocalVisibilitySnapshot *visibility) override {
    return sink_->build_tool_preview_scene_snapshot(std::move(preview), assets, visibility);
  }
  ToolPreviewSubmissionResult set_tool_preview_scene(RenderToolPreviewSceneSnapshot preview) override {
    return sink_->set_tool_preview_scene(std::move(preview));
  }
  const RenderToolPreviewSuppressionSnapshot &tool_preview_suppression() const noexcept override {
    return sink_->tool_preview_suppression();
  }
  std::uint64_t tool_preview_submission_revision() const override {
    return sink_->tool_preview_submission_revision();
  }
  bool scene_asset_overlay_sync_pending() const override { return sink_->scene_asset_overlay_sync_pending(); }
  void perform_idle_resource_maintenance() override { sink_->perform_idle_resource_maintenance(); }
  void set_active_overlay_viewports(std::uint8_t mask) override { sink_->set_active_overlay_viewports(mask); }
  ViewportOverlaySubmissionResult submit_viewport_overlay(ViewportOverlaySubmissionRequest request,
      const ViewportSettings &settings) override {
    const auto pane = request.viewport_index;
    if (pane < lamina_.size()) {
      auto &diagnostics = lamina_[pane];
      diagnostics = {};
      if (request.volatile_modeling_overlay_frame != nullptr) {
        for (const auto &triangle : request.volatile_modeling_overlay_frame->triangles) {
          if (triangle.layer != ViewportOverlayLayer::DiagnosticFace) continue;
          ++diagnostics.triangles;
          if (diagnostics.triangles == 1u) diagnostics.minimum_y = diagnostics.maximum_y = triangle.a.y;
          for (const auto point : {triangle.a, triangle.b, triangle.c}) {
            diagnostics.minimum_y = std::min(diagnostics.minimum_y, point.y);
            diagnostics.maximum_y = std::max(diagnostics.maximum_y, point.y);
          }
          diagnostics.horizontal_triangles += std::abs(triangle.a.y - triangle.b.y) < 0.00001f &&
              std::abs(triangle.a.y - triangle.c.y) < 0.00001f;
          diagnostics.face_style_correct &= triangle.style_role == ViewportOverlayStyleRole::LaminaFace;
        }
        for (const auto &line : request.volatile_modeling_overlay_frame->lines) {
          if (line.layer != ViewportOverlayLayer::DiagnosticEdge) continue;
          ++diagnostics.lines;
          diagnostics.line_style_correct &= line.style_role == ViewportOverlayStyleRole::LaminaWire;
        }
      }
    }
    auto result = sink_->submit_viewport_overlay(std::move(request), settings);
    if (pane < bounds_.size()) bounds_[pane] = result.selection_dimension_bounds;
    if (pane < handles_.size()) {
      handles_[pane].clear();
      if (result.edge_hud_frame != nullptr) {
        for (const auto &line : result.edge_hud_frame->lines) {
          if (line.layer == ViewportOverlayLayer::ToolHandle) handles_[pane].push_back(line);
        }
      }
    }
    return result;
  }
  const std::optional<ViewportSelectionDimensionBounds> &bounds(std::size_t pane) const { return bounds_[pane]; }
  const ProbeLaminaDiagnostics &lamina(std::size_t pane) const { return lamina_[pane]; }
  const std::vector<ViewportOverlayLine> &handles(std::size_t pane) const { return handles_[pane]; }

private:
  std::unique_ptr<RenderSceneSink> sink_;
  std::array<std::optional<ViewportSelectionDimensionBounds>, 4> bounds_;
  std::array<ProbeLaminaDiagnostics, 4> lamina_;
  std::array<std::vector<ViewportOverlayLine>, 4> handles_;
};

/** Drives the production four-pane adapter without selecting it for an app. */
class EditorCompositionProbe final {
public:
  EditorCompositionProbe(QApplication &application, QString output_directory,
                         std::filesystem::path resources, bool overlay_parity,
                         QString reference_directory, bool face_dimensions, bool lamina_overlap,
                         bool instance_preview, bool edge_gizmo)
      : application_(application), output_directory_(std::move(output_directory)),
        resources_(std::move(resources)),
        reference_directory_(std::move(reference_directory)), overlay_parity_(overlay_parity),
        face_dimensions_(face_dimensions || instance_preview),
        lamina_overlap_(lamina_overlap),
        instance_preview_(instance_preview),
        edge_gizmo_(edge_gizmo),
        editor_(QuaderApiSessionInternalAccess::from_scene_session(session_)),
#if defined(QUADER_FILAMENT_REFERENCE_PROBE)
        host_(create_app_render_host()) {
#else
        host_(create_crimson_render_host()) {
#endif
    timer_.setInterval(50);
    QObject::connect(&timer_, &QTimer::timeout, &application_, [this] { advance(); });
  }

  ~EditorCompositionProbe() {
    trace_retirement("probe_destructor_enter");
    retire_root();
    trace_retirement("probe_destructor_exit");
  }

  int run() {
    if (!QDir().mkpath(output_directory_) || !host_) {
      std::cerr << "Cannot initialize editor probe output/host\n";
      return 1;
    }
    if (!accept(editor_.commands().set_keybinding_storage_path({})) ||
        !accept(editor_.viewports().set_layout(ViewportLayoutId::FourQuad))) {
      return 1;
    }
    const auto created = lamina_overlap_ ? create_lamina_source()
        : face_dimensions_ ? create_face_dimension_box() : editor_.modeling().create_box(
        {.min = {-0.6f, -0.35f, -0.45f}, .max = {0.6f, 0.35f, 0.45f}},
        "Crimson public composition box");
    if (!accept(created)) {
      return 1;
    }
    object_ = created.value();
    if (!accept(editor_.selection().select_object(object_))) {
      return 1;
    }
    settings_ = ViewportSettings::defaults();
    (void)settings_.set_color(ViewportColorSettingId::Background, {0, 0, 0, 1});
    for (const auto setting : {ViewportBoolSettingId::ShowGrid,
                              ViewportBoolSettingId::ShowMeshGrid,
                              ViewportBoolSettingId::ShowOverlays,
                              ViewportBoolSettingId::ShowGizmos,
                              ViewportBoolSettingId::ShowDimensions,
                              ViewportBoolSettingId::ShowHoverDimensions}) {
      settings_.set_bool_value(setting, false);
    }
    (void)session_.viewport_workspace().update_control_defaults(settings_, ++settings_revision_);
    for (const auto pane : kPanes) {
      if (!accept(editor_.viewports().set_shading_mode(pane, QuaderShadingMode::Shaded))) {
        return 1;
      }
      session_.viewport_workspace().camera(pane).focus_bounds({-2, -2, -2}, {2, 2, 2});
    }
    if (instance_preview_ && !begin_instance_preview()) return 1;
    if (face_dimensions_ && !begin_face_dimensions()) return 1;
    if (lamina_overlap_ && !begin_lamina_overlap()) return 1;
    if (edge_gizmo_ && !begin_edge_gizmo()) return 1;
    create_root();
    step_clock_.start();
    settle_clock_.start();
    timer_.start();
    return application_.exec();
  }

private:
  /** API mutations and shell transitions each require a fresh presented epoch. */
  enum class Step {
    Initial, Unchanged, Pose, Preview, Cancel, Camera, Resize, Hidden, Reshow, Replaced,
    ObjectSingleton, ObjectSingletonCustom, ObjectsActivePrimary, ObjectsActiveOther,
    FacesBase, FacesSelected, FacesHovered, FacesHoverCleared, Edges, Vertices, VertexHovered,
    GridBase, Grid, GridCamera, GridRetained, Solid, ShadedWire, Wireframe,
    Gizmo, GizmoCamera, GizmoRetained,
    ActivePreviewBase, ActivePreview, ActivePreviewRetained, ActivePreviewCancel,
    HostRejected, HostRetry, CollisionOff, CollisionRestored,
    CollisionMaxOpacity, CollisionOpacityRestored
  };

  static constexpr std::array kPanes{
      ViewportPaneId::Perspective, ViewportPaneId::Top,
      ViewportPaneId::Front, ViewportPaneId::Side};
  static constexpr QVec3 kPreviewDelta{0.7f, 0.7f, 0.2f};
#if defined(QUADER_FILAMENT_REFERENCE_PROBE)
  static constexpr const char *kBackendName = "FullFilament";
#else
  static constexpr const char *kBackendName = "Crimson";
#endif

  void fail(std::string_view message) {
    if (error_.empty()) {
      error_ = message;
      std::cerr << "FAIL editor_step=" << static_cast<int>(step_) << ": " << error_ << std::endl;
    }
  }

  bool accept(const QuaderApiStatus &status) {
    if (status.ok()) {
      return true;
    }
    fail(status.error_message());
    return false;
  }

  QuaderApiResult<QuaderModelingObjectRef> create_lamina_source() {
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {-4, 4, 11}),
        quader_poly::add_vertex(document, {-4, 4, 15}),
        quader_poly::add_vertex(document, {0, 4, 15}),
        quader_poly::add_vertex(document, {0, 4, 11})};
    (void)quader_poly::add_face(document, vertices);
    document.faces.front().id = 5;
    document.next_face_id = 6;
    return editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "SEL-20261021-5DD1BB93 face5 interface"});
  }

  bool begin_lamina_overlap() {
    // These are the exact world-space interface faces from the supplied dump
    // and saved peer mesh, including its noncentral coplanar split seam.
    constexpr float kSplitZ = 12.620665788650513f;
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {-4, 4, 11}),
        quader_poly::add_vertex(document, {0, 4, 11}),
        quader_poly::add_vertex(document, {0, 4, kSplitZ}),
        quader_poly::add_vertex(document, {-4, 4, kSplitZ}),
        quader_poly::add_vertex(document, {-4, 4, 15}),
        quader_poly::add_vertex(document, {0, 4, 15})};
    const std::array first{vertices[0], vertices[1], vertices[2], vertices[3]};
    const std::array second{vertices[4], vertices[3], vertices[2], vertices[5]};
    (void)quader_poly::add_face(document, first);
    (void)quader_poly::add_face(document, second);
    document.faces[0].id = 6;
    document.faces[1].id = 12;
    document.next_face_id = 13;
    const auto peer = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "SEL-20261021-5DD1BB93 opposing faces6and12"});
    if (!accept(peer)) return false;
    lamina_peer_ = peer.value();
    settings_.set_bool_value(ViewportBoolSettingId::ShowOverlays, true);
    settings_.set_bool_value(ViewportBoolSettingId::BackfaceCulling, true);
    if (std::abs(settings_.float_value(ViewportFloatSettingId::LaminaWireLineSize) - 1.5f) > 0.00001f) {
      fail("The default lamina warning wire must remain 1.5 logical pixels wide");
      return false;
    }
    (void)settings_.set_color(ViewportColorSettingId::SelectionWire, {0, 1, 0, 1});
    (void)settings_.set_color(ViewportColorSettingId::SelectionFace, {0, 1, 0, 0.5f});
    (void)settings_.set_color(ViewportColorSettingId::HoverWire, {0, 0, 1, 1});
    (void)settings_.set_color(ViewportColorSettingId::HoverFace, {0, 0, 1, 0.5f});
    (void)settings_.set_color(ViewportColorSettingId::SourceWire, {0, 1, 1, 1});
    (void)settings_.set_color(ViewportColorSettingId::OpenEdge, {0, 1, 1, 1});
    apply_settings();
    for (std::size_t pane = 0u; pane < kPanes.size(); ++pane) {
      auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
      const bool above = pane % 2u == 0u;
      if (!camera.restore_perspective_pose({
              .eye = {-2, above ? 10.0f : -2.0f, pane < 2u ? 13.0f : 17.0f},
              .target = {-2, 4, 13}, .up = {0, 0, 1}})) return false;
      camera.focus_bounds({-4, 3.9f, 11}, {0, 4.1f, 15}, 1.6f);
    }
    return select_lamina_source();
  }

  bool select_lamina_source() {
    return accept(editor_.mode().set(QuaderEditorMode::Object)) &&
        accept(editor_.selection().select_object(object_)) &&
        accept(editor_.mode().set(QuaderEditorMode::Edit)) &&
        accept(editor_.selection().set_mode(QuaderSelectionMode::Face)) &&
        accept(editor_.selection().clear()) &&
        accept(editor_.selection().select_face(object_, 5));
  }

  QuaderTransformDragBeginRequest lamina_drag() const {
    return {.tool = QuaderTransformTool::Move, .axis = QuaderTransformAxis::Y,
        .pivot = {-2, 4, 13}, .screen_position = {100, 100}, .screen_pivot = {100, 100},
        .screen_tip = {100, 120}, .has_world_direction = true, .world_direction = {0, 1, 0},
        .ray = {.origin = {-2, 4, 20}, .direction = {0, 0, -1}},
        .camera = {.orthographic = true, .origin = {-2, 4, 20},
            .x_axis = {1, 0, 0}, .y_axis = {0, 1, 0}, .z_axis = {0, 0, 1},
            .viewport_size = {800, 600}, .orthographic_size = 8},
        .vertex_snap_radius = 0, .grid_snap_enabled = false};
  }

  bool move_lamina_away() {
    if (!accept(editor_.modeling().begin_transform_drag(lamina_drag()))) return false;
    const auto begin = lamina_drag();
    const auto moved = editor_.modeling().update_transform_drag({
        .screen_position = {100, 50},
        .ray = {.origin = {-2, 3.5f, 20}, .direction = {0, 0, -1}},
        .camera = begin.camera, .vertex_snap_radius = 0, .grid_snap_enabled = false});
    if (!accept(moved)) return false;
    if (std::abs(moved.value().move_delta.y + 0.5f) > 0.00001f) {
      fail("The public lamina drag did not separate the source face");
      return false;
    }
    lamina_expected_ = false;
    return true;
  }

  bool create_warped_lamina_fixture() {
    if (!accept(editor_.scene().destroy(object_.entity)) ||
        !accept(editor_.scene().destroy(lamina_peer_.entity))) return false;
    constexpr std::array<QVec3, 4> kPositions{{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 1}}};
    quader_poly::Document warped;
    std::array<quader_poly::ElementId, 4> vertices;
    for (std::size_t index = 0u; index < vertices.size(); ++index)
      vertices[index] = quader_poly::add_vertex(warped, kPositions[index]);
    (void)quader_poly::add_face(warped, vertices);
    const auto source = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(warped)),
        {.name = "Warped authored face must not inherit triangle lamina diagnostics"});
    if (!accept(source)) return false;
    object_ = source.value();
    quader_poly::Document tetrahedron;
    for (std::size_t index = 0u; index < vertices.size(); ++index)
      vertices[index] = quader_poly::add_vertex(tetrahedron, kPositions[index]);
    constexpr std::array<std::array<std::size_t, 3>, 4> kTriangles{{
        {{0, 1, 2}}, {{0, 1, 3}}, {{0, 2, 3}}, {{1, 2, 3}}}};
    for (const auto &indices : kTriangles) {
      const std::array loop{vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]};
      (void)quader_poly::add_face(tetrahedron, loop);
    }
    const auto peer = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(tetrahedron)),
        {.name = "Warped face triangle witnesses share only authored edges"});
    if (!accept(peer) || !accept(editor_.selection().clear())) return false;
    lamina_peer_ = peer.value();
    for (std::size_t pane = 0u; pane < kPanes.size(); ++pane) {
      auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
      if (!camera.restore_perspective_pose({
              .eye = {pane < 2u ? 1.0f : 5.0f, pane % 2u == 0u ? 6.0f : -4.0f, 4},
              .target = {1, 1, 0.3f}, .up = {0, 0, 1}})) return false;
      camera.focus_bounds({0, 0, 0}, {2, 2, 1}, 1.6f);
    }
    lamina_expected_ = false;
    return true;
  }

  QuaderApiResult<QuaderModelingObjectRef> create_closed_lamina_box(float bottom, float top) {
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {-4, bottom, 15}),
        quader_poly::add_vertex(document, {0, bottom, 15}),
        quader_poly::add_vertex(document, {0, top, 15}),
        quader_poly::add_vertex(document, {-4, top, 15}),
        quader_poly::add_vertex(document, {-4, bottom, 11}),
        quader_poly::add_vertex(document, {-4, top, 11}),
        quader_poly::add_vertex(document, {0, top, 11}),
        quader_poly::add_vertex(document, {0, bottom, 11})};
    constexpr std::array<std::array<std::size_t, 4>, 6> kLoops{{
        {{0, 1, 2, 3}}, {{4, 5, 6, 7}}, {{1, 7, 6, 2}},
        {{4, 0, 3, 5}}, {{5, 3, 2, 6}}, {{4, 7, 1, 0}}}};
    for (const auto &indices : kLoops) {
      const std::array loop{vertices[indices[0]], vertices[indices[1]], vertices[indices[2]], vertices[indices[3]]};
      (void)quader_poly::add_face(document, loop);
    }
    return editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Shared eight-vertex closed box for raised-face lamina"});
  }

  bool create_raised_face_fixture() {
    if (!accept(editor_.scene().destroy(object_.entity)) ||
        !accept(editor_.scene().destroy(lamina_peer_.entity))) return false;
    const auto source = create_closed_lamina_box(0, 4);
    if (!accept(source)) return false;
    object_ = source.value();
    const auto peer = create_closed_lamina_box(4, 8);
    if (!accept(peer)) return false;
    lamina_peer_ = peer.value();
    constexpr std::array<QVec3, 4> kEyes{{{-2, 6, 23}, {-2, 6, 3}, {10, 6, 13}, {-14, 6, 13}}};
    for (std::size_t pane = 0u; pane < kPanes.size(); ++pane) {
      auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
      if (!camera.restore_perspective_pose({.eye = kEyes[pane], .target = {-2, 4, 13}, .up = {0, 1, 0}})) return false;
      camera.focus_bounds({-4, 0, 11}, {0, 8, 15}, 1.4f);
    }
    lamina_expected_ = true;
    return select_lamina_source();
  }

  bool verify_lamina_overlap() {
    for (std::size_t pane = 0u; pane < kPanes.size(); ++pane) {
      const auto &submitted = recorded_sink_->lamina(pane);
      std::cout << "lamina_records; backend=" << kBackendName << "; phase=" << lamina_phase_
                << "; pane=" << pane << "; expected=" << lamina_expected_
                << "; triangles=" << submitted.triangles << "; lines=" << submitted.lines << std::endl;
      const bool partial = lamina_phase_ == 28u || lamina_phase_ == 29u || lamina_phase_ >= 32u;
      if ((lamina_expected_ && (submitted.triangles < (partial ? 1u : 6u) ||
                               submitted.lines < (partial ? 1u : 8u))) ||
          (!lamina_expected_ && (submitted.triangles != 0u || submitted.lines != 0u)) ||
          !submitted.face_style_correct || !submitted.line_style_correct) {
        fail("The native lamina publication lost its complete persistent geometry or appearance roles");
        return false;
      }
      QImage image;
      if (!verify_pane(pane, image)) return false;
      if (lamina_phase_ >= 36u) {
        const auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
        const auto render_size = panel_->surface(kPanes[pane])->render_size_pixels();
        const auto interior = [&](const QImage &sample) {
          const auto projected = camera.project_world_point({-2.2f, 4, 13.5f});
          return sample.pixelColor(qRound(projected.position.x * sample.width() / render_size.x),
                                   qRound(projected.position.y * sample.height() / render_size.y));
        };
        if (lamina_phase_ == 36u) lamina_toggle_baseline_[pane] = image;
        if (lamina_phase_ == 37u &&
            (image == lamina_toggle_baseline_[pane] ||
             !near_colors(interior(image), interior(lamina_toggle_baseline_[pane]), 2))) {
          fail("Hiding lamina wire must change boundary pixels while preserving interior fill");
          return false;
        }
        if ((lamina_phase_ == 38u && image != lamina_previous_[pane]) ||
            (lamina_phase_ == 39u && image != lamina_toggle_baseline_[pane])) {
          fail("Lamina wire display toggle retained stale pixels or lost its restored appearance");
          return false;
        }
        lamina_previous_[pane] = image;
        continue;
      }
      if (lamina_phase_ == 30u || lamina_phase_ == 31u) {
        for (int y = 0; y < image.height(); ++y) {
          for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            if (std::abs(color.green() - color.blue()) <= 16 &&
                color.red() > std::max(color.green(), color.blue()) + 12) {
              fail("A warped authored face acquired red warning from its coincident render triangles");
              return false;
            }
          }
        }
        if (lamina_phase_ == 31u && image != lamina_previous_[pane]) {
          fail("An unchanged warped-face diagnostic frame changed GPU pixels");
          return false;
        }
        lamina_previous_[pane] = image;
        continue;
      }
      if (lamina_phase_ >= 32u) {
        const bool raised = lamina_phase_ == 33u || lamina_phase_ == 34u;
        if (raised && (submitted.minimum_y < 4.0f - 0.00001f ||
                       submitted.maximum_y > 5.0f + 0.00001f || submitted.horizontal_triangles != 0u)) {
          fail("A raised shared-vertex face marked a whole wall or its selected horizontal cap as lamina");
          return false;
        }
        const auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
        const auto render_size = panel_->surface(kPanes[pane])->render_size_pixels();
        const auto wall_red = [&](float height) {
          const QVec3 world = pane == 0u ? QVec3{-2, height, 15}
              : pane == 1u ? QVec3{-2, height, 11}
              : pane == 2u ? QVec3{0, height, 13} : QVec3{-4, height, 13};
          const auto projected = camera.project_world_point(world);
          const QPoint pixel(qRound(projected.position.x * image.width() / render_size.x),
                             qRound(projected.position.y * image.height() / render_size.y));
          if (!image.rect().contains(pixel)) return 256;
          const auto color = image.pixelColor(pixel);
          return std::abs(color.green() - color.blue()) <= 16
              ? color.red() - std::max(color.green(), color.blue()) : -255;
        };
        const int lower = wall_red(2), overlap = wall_red(4.5f), upper = wall_red(6);
        std::cout << "raised_lamina; backend=" << kBackendName << "; phase=" << lamina_phase_
                  << "; pane=" << pane << "; diagnostic_y=" << submitted.minimum_y << ',' << submitted.maximum_y
                  << "; lower=" << lower << "; overlap=" << overlap << "; upper=" << upper << std::endl;
        if (lower > 12 || upper > 12 || (raised ? overlap < 12 : overlap > 12)) {
          fail("Live raised-face lamina must cover only the Y4..5 wall overlap, preserving ordinary lower and upper walls");
          return false;
        }
        if (lamina_phase_ == 32u) lamina_closed_base_[pane] = image;
        if ((lamina_phase_ == 34u && image != lamina_previous_[pane]) ||
            (lamina_phase_ == 35u && image != lamina_closed_base_[pane])) {
          fail("Raised-face retention or cancellation failed to preserve the corresponding accepted wall pixels");
          return false;
        }
        lamina_previous_[pane] = image;
        continue;
      }
      const auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
      const auto render_size = panel_->surface(kPanes[pane])->render_size_pixels();
      const auto fill_color_at = [&](QVec3 world) {
        const auto projected = camera.project_world_point(world);
        const QPoint pixel(qRound(projected.position.x * image.width() / render_size.x),
                           qRound(projected.position.y * image.height() / render_size.y));
        return image.rect().contains(pixel) ? image.pixelColor(pixel) : QColor{};
      };
      const std::array fill_colors{
          fill_color_at({-2.2f, 4, 11.7f}), fill_color_at({-2.2f, 4, 13.5f})};
      if (lamina_phase_ == 18u) {
        if (submitted.triangles <= lamina_previous_triangles_[pane] ||
            !near_colors(fill_colors[0], lamina_previous_fill_colors_[pane][0], 2) ||
            !near_colors(fill_colors[1], lamina_previous_fill_colors_[pane][1], 2)) {
          fail("Adding a coincident diagnostic face accumulated red fill at an already covered pixel");
          return false;
        }
        std::cout << "lamina_single_blend; backend=" << kBackendName << "; pane=" << pane
                  << "; triangles_before=" << lamina_previous_triangles_[pane]
                  << "; triangles_after=" << submitted.triangles
                  << "; both_interior_colors=unchanged" << std::endl;
      }
      const auto score_at = [&](QVec3 world, int radius, int channel, bool neutral_red) {
        const auto projected = camera.project_world_point(world);
        const QPoint center(qRound(projected.position.x * image.width() / render_size.x),
                            qRound(projected.position.y * image.height() / render_size.y));
        int score = -255;
        for (int y = -radius; y <= radius; ++y) {
          for (int x = -radius; x <= radius; ++x) {
            if (!image.rect().contains(center + QPoint(x, y))) continue;
            const auto color = image.pixelColor(center + QPoint(x, y));
            if (neutral_red && std::abs(color.green() - color.blue()) > 16) continue;
            const std::array channels{color.red(), color.green(), color.blue()};
            score = std::max(score, channels[channel] -
                std::max(channels[(channel + 1) % 3], channels[(channel + 2) % 3]));
          }
        }
        return score;
      };
      const int first_fill = score_at({-2.2f, 4, 11.7f}, 0, 0, true);
      const int second_fill = score_at({-2.2f, 4, 13.5f}, 0, 0, true);
      const int wire = score_at({-4, 4, 13.5f}, 3, 0, true);
      std::cout << "lamina_pixels; backend=" << kBackendName << "; phase=" << lamina_phase_
                << "; pane=" << pane << "; fill=" << first_fill << ',' << second_fill
                << "; wire=" << wire << std::endl;
      const bool selected_source = lamina_phase_ == 0u || lamina_phase_ == 4u ||
          lamina_phase_ == 7u || lamina_phase_ == 10u || lamina_phase_ == 12u;
      const bool hover_source = lamina_phase_ == 19u;
      const bool customized = lamina_phase_ >= 21u && lamina_phase_ <= 25u;
      if (lamina_expected_ && partial) {
        const int overlap_fill = score_at({-1, 4, 13.5f}, 0, 0, true);
        const int clipped_wire = score_at({0, 4, 13.5f}, 3, 0, true);
        if (score_at({-3, 4, 13.5f}, 0, 0, true) > 12 ||
            score_at({1, 4, 13.5f}, 0, 0, true) > 12 || wire > 12 ||
            overlap_fill < 12 || clipped_wire < overlap_fill + 30) {
          fail("Partial lamina warning escaped its overlapping patch or retained a nonoverlapping authored edge");
          return false;
        }
      } else if (lamina_expected_ && (selected_source || hover_source)) {
        const int channel = hover_source ? 2 : 1;
        if (score_at({-2.2f, 4, 11.7f}, 0, channel, false) < 20 ||
            score_at({-2.2f, 4, 13.5f}, 0, channel, false) < 20 ||
            score_at({-4, 4, 13.5f}, 3, channel, false) < 30) {
          fail("Normal selection or hover appearance did not cover the underlying lamina warning");
          return false;
        }
      } else if (lamina_expected_ && lamina_phase_ == 1u) {
        if (score_at({-2.2f, 4, 11.7f}, 0, 1, false) < 20 || second_fill < 12 || wire < second_fill + 30) {
          fail("Selecting one peer face must retain red warning on its unselected neighbor");
          return false;
        }
      } else if (lamina_expected_ && !customized &&
                 (first_fill < 12 || second_fill < 12 || wire < std::max(first_fill, second_fill) + 30)) {
        fail("Uncovered lamina GPU tint must remain red beneath its stronger boundary");
        return false;
      }
      if (!lamina_expected_ && (first_fill > 12 || second_fill > 12 || wire > 12)) {
        fail("Separated, transverse or touching surfaces retained stale red lamina pixels");
        return false;
      }
      if (customized) {
        const bool transparent_fill = lamina_phase_ >= 24u;
        for (const auto color : fill_colors) {
          if ((!transparent_fill && (color.blue() - color.red() < 25 || color.green() - color.red() < 10)) ||
              (transparent_fill && std::max({color.red(), color.green(), color.blue()}) -
                  std::min({color.red(), color.green(), color.blue()}) > 8)) {
            fail("Live lamina fill color and opacity did not reach the GPU surface");
            return false;
          }
        }
        const auto projected = camera.project_world_point({-4, 4, 13.5f});
        const QPoint center(qRound(projected.position.x * image.width() / render_size.x),
                            qRound(projected.position.y * image.height() / render_size.y));
        int wire_pixels = 0;
        for (int y = -8; y <= 8; ++y) {
          for (int x = -8; x <= 8; ++x) {
            if (!image.rect().contains(center + QPoint(x, y))) continue;
            const auto color = image.pixelColor(center + QPoint(x, y));
            wire_pixels += color.red() > color.green() + 15 && color.red() > color.blue() + 30 &&
                color.green() > color.blue() + 15;
          }
        }
        const int wire_contrast = score_at({-4, 4, 13.5f}, 6, 0, false);
        if (lamina_phase_ == 21u && wire_pixels < 10) {
          fail("Live lamina wire color did not replace the default red wire");
          return false;
        }
        if (lamina_phase_ == 22u &&
            (wire_pixels * 2 <= lamina_previous_wire_pixels_[pane] * 3 ||
             !near_colors(fill_colors[0], lamina_previous_fill_colors_[pane][0], 2) ||
             !near_colors(fill_colors[1], lamina_previous_fill_colors_[pane][1], 2))) {
          fail("Increasing lamina wire width failed to widen its GPU coverage independently of the fill");
          return false;
        }
        if (lamina_phase_ == 23u && wire_contrast >= lamina_previous_wire_contrast_[pane] - 10) {
          fail("Reducing lamina wire opacity did not reduce its GPU contrast");
          return false;
        }
        std::cout << "lamina_preferences; phase=" << lamina_phase_ << "; pane=" << pane
                  << "; width=" << settings_.float_value(ViewportFloatSettingId::LaminaWireLineSize)
                  << "; wire_pixels=" << wire_pixels << "; wire_contrast=" << wire_contrast << std::endl;
        lamina_previous_wire_pixels_[pane] = wire_pixels;
        lamina_previous_wire_contrast_[pane] = wire_contrast;
      }
      if ((lamina_phase_ == 3u || lamina_phase_ == 6u || lamina_phase_ == 17u ||
           lamina_phase_ == 25u || lamina_phase_ == 27u || lamina_phase_ == 29u) &&
          image != lamina_previous_[pane]) {
        fail("An unchanged lamina publication changed the accepted GPU pixels");
        return false;
      }
      lamina_previous_[pane] = image;
      lamina_previous_fill_colors_[pane] = fill_colors;
      lamina_previous_triangles_[pane] = submitted.triangles;
    }
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    if (lamina_phase_ == 3u || lamina_phase_ == 6u || lamina_phase_ == 17u ||
        lamina_phase_ == 25u || lamina_phase_ == 27u || lamina_phase_ == 29u ||
        lamina_phase_ == 31u || lamina_phase_ == 34u) {
      CrimsonRenderDiagnostics after;
      if (!diagnostics(after)) return false;
      if (after.editor_projection.position_packs != before_.editor_projection.position_packs ||
          after.editor_projection.attribute_packs != before_.editor_projection.attribute_packs ||
          after.editor_projection.index_packs != before_.editor_projection.index_packs ||
          after.editor_gpu.vertex_uploads != before_.editor_gpu.vertex_uploads ||
          after.editor_gpu.index_uploads != before_.editor_gpu.index_uploads) {
        fail("Unchanged lamina frames repacked or uploaded editor geometry");
        return false;
      }
    }
#endif
    return true;
  }

  void advance_lamina_overlap() {
    bool ok = true;
    switch (lamina_phase_) {
    case 0:
      ok = accept(editor_.mode().set(QuaderEditorMode::Object)) &&
          accept(editor_.selection().select_object(lamina_peer_)) &&
          accept(editor_.mode().set(QuaderEditorMode::Edit)) &&
          accept(editor_.selection().set_mode(QuaderSelectionMode::Face)) &&
          accept(editor_.selection().select_face(lamina_peer_, 6));
      break;
    case 1:
      ok = accept(editor_.selection().clear()) && accept(editor_.selection().clear_hover());
      break;
    case 2: break;
    case 3: ok = select_lamina_source() && accept(editor_.modeling().begin_transform_drag(lamina_drag())); break;
    case 4: {
      const auto begin = lamina_drag();
      ok = accept(editor_.modeling().update_transform_drag({
          .screen_position = {100, 50}, .ray = {.origin = {-2, 3.5f, 20}, .direction = {0, 0, -1}},
          .camera = begin.camera, .vertex_snap_radius = 0, .grid_snap_enabled = false}));
      lamina_expected_ = false;
      break;
    }
    case 5: break;
    case 6:
      ok = accept(editor_.modeling().cancel_transform_drag());
      lamina_expected_ = true;
      break;
    case 7: ok = move_lamina_away(); break;
    case 8: ok = accept(editor_.modeling().commit_transform_drag()); break;
    case 9:
      ok = accept(editor_.history().undo());
      lamina_expected_ = true;
      break;
    case 10:
      ok = accept(editor_.history().redo());
      lamina_expected_ = false;
      break;
    case 11:
      ok = accept(editor_.history().undo());
      lamina_expected_ = true;
      break;
    case 12:
      ok = accept(editor_.mode().set(QuaderEditorMode::Object)) &&
          accept(editor_.selection().clear()) &&
          accept(editor_.scene().set_transform(lamina_peer_.entity, {.location = {4, 0, 0}}));
      lamina_expected_ = false;
      break;
    case 13:
      ok = accept(editor_.scene().set_transform(lamina_peer_.entity,
          {.location = {0, 17, 9}, .rotation_euler = {1.57079632679f, 0, 0}}));
      break;
    case 14:
      ok = accept(editor_.scene().set_transform(lamina_peer_.entity, {}));
      lamina_expected_ = true;
      break;
    case 15:
      settings_.set_bool_value(ViewportBoolSettingId::BackfaceCulling, false);
      apply_settings();
      break;
    case 16: break;
    case 17: {
      const auto duplicate = create_lamina_source();
      ok = accept(duplicate) && accept(editor_.selection().clear());
      if (ok) lamina_duplicate_ = duplicate.value();
      break;
    }
    case 18:
      ok = select_lamina_source() && accept(editor_.selection().clear()) &&
          accept(editor_.selection().hover_face(object_, 5));
      break;
    case 19: ok = accept(editor_.selection().clear_hover()); break;
    case 20:
      (void)settings_.set_color(ViewportColorSettingId::LaminaFace, {0, 0.5f, 1, 0.5f});
      (void)settings_.set_color(ViewportColorSettingId::LaminaWire, {1, 0.5f, 0, 1});
      (void)settings_.set_float_value(ViewportFloatSettingId::LaminaWireLineSize, 1.5f);
      apply_settings();
      break;
    case 21:
      (void)settings_.set_float_value(ViewportFloatSettingId::LaminaWireLineSize, 5.0f);
      apply_settings();
      break;
    case 22:
      (void)settings_.set_color(ViewportColorSettingId::LaminaWire, {1, 0.5f, 0, 0.25f});
      apply_settings();
      break;
    case 23:
      (void)settings_.set_color(ViewportColorSettingId::LaminaFace, {0, 0.5f, 1, 0});
      apply_settings();
      break;
    case 24: break;
    case 25: {
      const auto defaults = ViewportSettings::defaults();
      (void)settings_.set_color(ViewportColorSettingId::LaminaFace,
                                defaults.color(ViewportColorSettingId::LaminaFace));
      (void)settings_.set_color(ViewportColorSettingId::LaminaWire,
                                defaults.color(ViewportColorSettingId::LaminaWire));
      (void)settings_.set_float_value(ViewportFloatSettingId::LaminaWireLineSize, 1.5f);
      apply_settings();
      break;
    }
    case 26: break;
    case 27:
      ok = accept(editor_.mode().set(QuaderEditorMode::Object)) &&
          accept(editor_.selection().clear()) &&
          accept(editor_.scene().destroy(lamina_duplicate_.entity)) &&
          accept(editor_.scene().set_transform(lamina_peer_.entity, {.location = {2, 0, 0}}));
      break;
    case 28: break;
    case 29: ok = create_warped_lamina_fixture(); break;
    case 30: break;
    case 31: ok = create_raised_face_fixture(); break;
    case 32: {
      const auto begin = lamina_drag();
      ok = accept(editor_.modeling().begin_transform_drag(begin));
      if (ok) {
        const auto moved = editor_.modeling().update_transform_drag({
            .screen_position = {100, 200}, .ray = {.origin = {-2, 5, 20}, .direction = {0, 0, -1}},
            .camera = begin.camera, .vertex_snap_radius = 0, .grid_snap_enabled = false});
        ok = accept(moved);
        if (ok && std::abs(moved.value().move_delta.y - 1.0f) > 0.00001f) {
          fail("The public closed-box face drag did not raise its shared vertices by one world unit");
          ok = false;
        }
      }
      break;
    }
    case 33: break;
    case 34: ok = accept(editor_.modeling().cancel_transform_drag()); break;
    case 35: {
      ok = accept(editor_.mode().set(QuaderEditorMode::Object)) &&
          accept(editor_.scene().destroy(object_.entity)) &&
          accept(editor_.scene().destroy(lamina_peer_.entity));
      if (ok) {
        const auto source = create_lamina_source();
        ok = accept(source);
        if (ok) object_ = source.value();
      }
      if (ok) ok = begin_lamina_overlap() && accept(editor_.selection().clear()) &&
          accept(editor_.selection().clear_hover());
      break;
    }
    case 36:
      (void)settings_.set_bool_value(ViewportBoolSettingId::ShowLaminaFaces, false);
      apply_settings();
      break;
    case 37: break;
    case 38:
      (void)settings_.set_bool_value(ViewportBoolSettingId::ShowLaminaFaces, true);
      apply_settings();
      break;
    default: finish(); return;
    }
    if (ok) {
      ++lamina_phase_;
      next(Step::Initial);
    }
  }

  QuaderApiResult<QuaderModelingObjectRef> create_face_dimension_box() {
    quader_poly::Document document;
    std::array<quader_poly::ElementId, 8> vertices;
    constexpr float kDiagonal = 0.70710678118f;
    for (std::size_t corner = 0u; corner < vertices.size(); ++corner) {
      const float x = (corner & 1u) ? 0.6f : -0.6f;
      const float y = (corner & 2u) ? 0.35f : -0.35f;
      vertices[corner] = quader_poly::add_vertex(document,
          {(x - y) * kDiagonal, (x + y) * kDiagonal, (corner & 4u) ? 0.45f : -0.45f});
    }
    constexpr std::array<std::array<std::size_t, 4>, 6> kFaces{{
        {{0, 4, 6, 2}}, {{1, 3, 7, 5}}, {{0, 1, 5, 4}},
        {{2, 6, 7, 3}}, {{0, 2, 3, 1}}, {{4, 5, 7, 6}}}};
    for (std::size_t face = 0u; face < kFaces.size(); ++face) {
      std::array<quader_poly::ElementId, 4> loop;
      for (std::size_t index = 0u; index < loop.size(); ++index) loop[index] = vertices[kFaces[face][index]];
      const auto id = quader_poly::add_face(document, loop);
      if (face == 1u) face_dimension_face_ = id;
    }
    return editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Closed box face dimension preview"});
  }

  bool begin_edge_gizmo() {
    settings_.set_bool_value(ViewportBoolSettingId::ShowOverlays, true);
    settings_.set_bool_value(ViewportBoolSettingId::ShowGizmos, true);
    for (const auto setting : {ViewportColorSettingId::SelectionWire,
                              ViewportColorSettingId::ActiveSelection,
                              ViewportColorSettingId::SourceWire,
                              ViewportColorSettingId::OpenEdge}) {
      (void)settings_.set_color(setting, {0.5f, 0.5f, 0.5f, 1});
    }
    apply_settings();
    if (!accept(editor_.mode().set(QuaderEditorMode::Edit)) ||
        !accept(editor_.selection().set_mode(QuaderSelectionMode::Edge)) ||
        !accept(editor_.selection().clear()) ||
        !accept(editor_.selection().select_edge(object_, 1, 2)) ||
        !accept(editor_.tools().activate(ViewportToolId::Move))) return false;
    const auto center = editor_.query().selection_center();
    if (!center) return false;
    edge_gizmo_origin_ = *center;
    for (std::size_t pane = 0u; pane < kPanes.size(); ++pane) {
      if (!accept(editor_.viewports().record_selected_tool(kPanes[pane], ViewportToolId::Move)) ||
          !accept(editor_.viewports().set_transform_space(kPanes[pane], QuaderTransformSpace::Global))) return false;
      auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
      const float offset = static_cast<float>(pane) * 0.4f;
      if (!camera.restore_perspective_pose({.eye = *center + QVec3{5 + offset, 4, 6 - offset},
              .target = *center + kEdgeGizmoDelta * 0.5f, .up = {0, 1, 0}})) return false;
      camera.focus_bounds(*center - QVec3{2, 2, 2}, *center + QVec3{3, 3, 3}, 1.15f);
    }
    return true;
  }

  bool verify_edge_gizmo() {
    const auto axis_hue = [](QColor color) {
      const int red = color.red(), green = color.green(), blue = color.blue();
      if (red > green + 30 && red > blue + 30) return 4;
      if (green > red + 30 && green > blue + 30) return 5;
      if (blue > red + 30 && blue > green + 30) return 6;
      return 0;
    };
    const bool moved = edge_gizmo_phase_ >= 3u && edge_gizmo_phase_ != 8u;
    const QVec3 expected = edge_gizmo_origin_ + (moved ? kEdgeGizmoDelta : QVec3{});
    const auto center = editor_.query().selection_center();
    const auto pivot = editor_.query().selection_transform_pivot();
    if (!center || !pivot || length(*center - expected) > 0.0001f || length(*pivot - expected) > 0.0001f) {
      fail("The public edge move or untouched Quick Pivot left a stale selection pivot");
      return false;
    }
    for (std::size_t pane = 0u; pane < kPanes.size(); ++pane) {
      const auto &handles = recorded_sink_->handles(pane);
      std::array<std::optional<ViewportOverlayLine>, 3> axes;
      for (const auto &line : handles) {
        const auto color = line.color;
        const int color_hue = axis_hue(QColor(color & 255u, (color >> 8u) & 255u, (color >> 16u) & 255u));
        if (color_hue >= 4 && color_hue <= 6 && length(line.start - expected) < 0.0001f &&
            length(line.end - line.start) > 0.0001f) axes[color_hue - 4] = line;
      }
      std::cout << "edge_gizmo_submission; backend=" << kBackendName << "; phase=" << edge_gizmo_phase_
                << "; pane=" << pane << "; handles=" << handles.size()
                << "; axes=" << axes[0].has_value() << ',' << axes[1].has_value() << ',' << axes[2].has_value()
                << "; expected=" << expected.x << ',' << expected.y << ',' << expected.z << std::endl;
      if (std::ranges::any_of(axes, [](const auto &axis) { return !axis; })) {
        fail("The production Qt pane did not submit all Move axes from the current edge center");
        return false;
      }
      QImage image;
      if (!verify_pane(pane, image)) return false;
      const auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
      const auto render_size = panel_->surface(kPanes[pane])->render_size_pixels();
      const auto projected_pixel = [&](QVec3 point) {
        const auto projected = camera.project_world_point(point);
        return QVec2{projected.position.x * image.width() / render_size.x,
                     projected.position.y * image.height() / render_size.y};
      };
      const auto colored_pixels = [&](QVec2 point, int expected_hue) {
        int result = 0;
        for (int y = -3; y <= 3; ++y) {
          for (int x = -3; x <= 3; ++x) {
            const QPoint pixel(qRound(point.x) + x, qRound(point.y) + y);
            if (image.rect().contains(pixel)) result += axis_hue(image.pixelColor(pixel)) == expected_hue;
          }
        }
        return result;
      };
      const auto distance_to_line = [](QVec2 point, QVec2 start, QVec2 end) {
        const QVec2 delta{end.x - start.x, end.y - start.y};
        const float squared = delta.x * delta.x + delta.y * delta.y;
        const float along = squared > 0 ? std::clamp(((point.x - start.x) * delta.x +
            (point.y - start.y) * delta.y) / squared, 0.0f, 1.0f) : 0.0f;
        return std::hypot(point.x - start.x - along * delta.x, point.y - start.y - along * delta.y);
      };
      int sampled_axes = 0, clear_old_samples = 0;
      for (std::size_t axis = 0u; axis < axes.size(); ++axis) {
        const auto &line = *axes[axis];
        bool colored = false;
        for (const float along : {0.3f, 0.5f, 0.7f}) {
          const auto sample = line.start + (line.end - line.start) * along;
          const auto current = projected_pixel(sample);
          colored |= colored_pixels(current, static_cast<int>(axis) + 4) > 0;
          if (!moved) continue;
          const auto old = projected_pixel(sample - kEdgeGizmoDelta);
          const bool clear = std::ranges::all_of(handles, [&](const auto &other) {
            return distance_to_line(old, projected_pixel(other.start), projected_pixel(other.end)) > 12.0f;
          });
          if (!clear) continue;
          ++clear_old_samples;
          if (colored_pixels(old, static_cast<int>(axis) + 4) != 0) {
            fail("The GPU retained a Move axis at the old edge center after the accepted move");
            return false;
          }
        }
        sampled_axes += colored;
      }
      std::cout << "edge_gizmo; backend=" << kBackendName << "; phase=" << edge_gizmo_phase_
                << "; pane=" << pane << "; pivot=" << expected.x << ',' << expected.y << ',' << expected.z
                << "; submitted_axes=" << axes.size() << "; colored_axes=" << sampled_axes
                << "; clear_old_samples=" << clear_old_samples << std::endl;
      if (sampled_axes < 3 || (moved && clear_old_samples < 2)) {
        fail("The GPU axis colors did not follow the submitted edge gizmo origin");
        return false;
      }
      if ((edge_gizmo_phase_ == 1u || edge_gizmo_phase_ == 2u ||
           (edge_gizmo_phase_ >= 5u && edge_gizmo_phase_ <= 7u)) && image != edge_gizmo_previous_[pane]) {
        fail("An unchanged edge gizmo or untouched Quick Pivot changed retained GPU pixels");
        return false;
      }
      edge_gizmo_previous_[pane] = image;
    }
    return true;
  }

  void advance_edge_gizmo() {
    bool ok = true;
    switch (edge_gizmo_phase_) {
    case 0: break;
    case 1:
      session_.pivot().begin_quick_edit(edge_gizmo_origin_);
      session_.pivot().end_quick_edit();
      if (session_.pivot().selection_pivot_override().valid || session_.pivot().quick_edit_active()) {
        fail("Ending an untouched native Quick Pivot hold retained an override");
        return;
      }
      break;
    case 2:
      ok = accept(editor_.modeling().begin_transform_preview()) &&
          accept(editor_.modeling().update_transform_translate(kEdgeGizmoDelta));
      break;
    case 3: ok = accept(editor_.modeling().commit_transform_preview()); break;
    case 4: case 5: case 6: break;
    case 7: ok = accept(editor_.history().undo()); break;
    case 8: ok = accept(editor_.history().redo()); break;
    default: finish(); return;
    }
    if (ok) {
      ++edge_gizmo_phase_;
      next(Step::Initial);
    }
  }

  bool begin_instance_preview() {
    // Keep the explicit black capture background during instance editing too.
    // The ordinary context tint remains enabled on the sibling occurrence.
    (void)settings_.set_color(ViewportColorSettingId::InstanceEditBackground, {0, 0, 0, 1});
    const auto created = editor_.modeling().create_instance_from_selection();
    if (!accept(created)) return false;
    const auto occurrence = created.value().occurrence.root.id;
    const auto sibling = editor_.scene().duplicate(occurrence);
    if (!accept(sibling) || !accept(editor_.scene().set_transform(
            sibling.value().id, {.location = kInstanceSiblingOffset})) ||
        !accept(editor_.modeling().enter_instance_edit(occurrence)) ||
        !accept(editor_.selection().select_object(object_))) return false;
    if (!editor_.modeling().instance_edit_state().active) {
      fail("The public instance conversion did not enter its definition editing scope");
      return false;
    }
    for (std::size_t pane = 0u; pane < kPanes.size(); ++pane) {
      auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
      const float offset = static_cast<float>(pane) * 0.15f;
      if (!camera.restore_perspective_pose({
              .eye = {5.15f + offset, 6.85f - offset, 0.2f},
              .target = {-0.85f, 0.85f, 0.2f}, .up = {0, 0, 1}})) return false;
      camera.focus_bounds({-2.5f, -1.0f, -0.7f}, {1.0f, 2.5f, 1.5f}, 1.3f);
    }
    std::cout << "public_instance_preview; backend=" << kBackendName
              << "; occurrence=" << occurrence << "; sibling=" << sibling.value().id
              << "; definition=" << created.value().definition.root.id << std::endl;
    return true;
  }

  bool verify_instance_preview_pixels(std::size_t pane, const QImage &image) {
    constexpr float kFaceCenter = 0.6f * 0.70710678118f;
    const auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
    const auto render_size = panel_->surface(kPanes[pane])->render_size_pixels();
    for (const auto offset : {QVec3{}, kInstanceSiblingOffset}) {
      const bool sibling = offset.x != 0.0f;
      // A live definition edit previews only its active occurrence. Applying
      // the edit publishes the shared geometry to every linked occurrence.
      const bool shared_commit = face_dimension_phase_ == 8u || face_dimension_phase_ == 10u;
      const float surface_delta = sibling && !shared_commit ? 0.0f : face_dimension_delta_;
      for (const bool extension : {false, true}) {
        const QVec3 point = QVec3{kFaceCenter, kFaceCenter,
            extension ? 0.45f + (face_dimension_delta_ > 0.0f ? face_dimension_delta_ * 0.5f : 0.2f)
                      : surface_delta} + offset;
        const auto projected = camera.project_world_point(point);
        if (!projected.visible) {
          fail("The instance fixture camera lost a linked occurrence surface sample");
          return false;
        }
        const QPoint center(qRound(projected.position.x * image.width() / render_size.x),
                            qRound(projected.position.y * image.height() / render_size.y));
        int surface_pixels = 0;
        for (int y = -1; y <= 1; ++y) {
          for (int x = -1; x <= 1; ++x) {
            const auto pixel = center + QPoint(x, y);
            if (!image.rect().contains(pixel)) continue;
            const auto color = image.pixelColor(pixel);
            surface_pixels += std::max({color.red(), color.green(), color.blue()}) > 24;
          }
        }
        const bool expected = !extension || surface_delta > 0.0f;
        std::cout << "instance_surface_pixels; backend=" << kBackendName
                  << "; phase=" << face_dimension_phase_ << "; pane=" << pane
                  << "; sibling=" << sibling << "; extension=" << extension
                  << "; surface_delta=" << surface_delta
                  << "; expected=" << expected << "; pixels=" << surface_pixels << std::endl;
        if ((expected && surface_pixels < 7) || (!expected && surface_pixels != 0)) {
          fail("A linked occurrence surface violated active preview or shared committed geometry");
          return false;
        }
      }
    }
    return true;
  }

  bool begin_face_dimensions() {
    settings_.set_bool_value(ViewportBoolSettingId::ShowOverlays, true);
    settings_.set_bool_value(ViewportBoolSettingId::ShowDimensions, true);
    (void)settings_.set_color(ViewportColorSettingId::SelectionWire, {0, 1, 1, 1});
    (void)settings_.set_color(ViewportColorSettingId::SelectionFace, {0, 0, 0, 0});
    (void)settings_.set_color(ViewportColorSettingId::BoundingBoxWire, {1, 1, 0, 1});
    apply_settings();
    return accept(editor_.mode().set(QuaderEditorMode::Edit)) &&
        accept(editor_.selection().set_mode(QuaderSelectionMode::Face)) &&
        accept(editor_.selection().clear()) &&
        accept(editor_.selection().select_face(object_, face_dimension_face_));
  }

  QuaderTransformDragBeginRequest face_dimension_drag() const {
    return {.tool = QuaderTransformTool::Move, .axis = QuaderTransformAxis::Z,
        .screen_position = {100, 100}, .screen_pivot = {100, 100},
        .screen_tip = {100, 120}, .has_world_direction = true, .world_direction = {0, 0, 1},
        .ray = {.origin = {5, 0, 0}, .direction = {-1, 0, 0}},
        .camera = {.orthographic = true, .origin = {5, 0, 0},
            .x_axis = {0, 1, 0}, .y_axis = {0, 0, 1}, .z_axis = {1, 0, 0},
            .viewport_size = {800, 600}, .orthographic_size = 8},
        .vertex_snap_radius = 0, .grid_snap_enabled = false};
  }

  bool update_face_dimensions(float amount) {
    const auto begin = face_dimension_drag();
    const auto moved = editor_.modeling().update_transform_drag({
        .screen_position = {100, 100 + amount * 100},
        .ray = {.origin = {5, 0, amount}, .direction = {-1, 0, 0}},
        .camera = begin.camera, .vertex_snap_radius = 0, .grid_snap_enabled = false});
    if (!accept(moved)) return false;
    if (std::abs(moved.value().move_delta.z - amount) > 0.00001f) {
      fail("The public face drag did not produce its requested displacement");
      return false;
    }
    face_dimension_delta_ = amount;
    return true;
  }

  bool verify_face_dimensions() {
    constexpr float kDiagonal = 0.70710678118f;
    const ViewportSelectionDimensionBounds expected{
        {(0.6f - 0.35f) * kDiagonal, (0.6f - 0.35f) * kDiagonal, -0.45f + face_dimension_delta_},
        {(0.6f + 0.35f) * kDiagonal, (0.6f + 0.35f) * kDiagonal, 0.45f + face_dimension_delta_}};
    for (std::size_t pane = 0u; pane < kPanes.size(); ++pane) {
      const auto &bounds = recorded_sink_->bounds(pane);
      std::cout << "face_dimension_bounds; backend=" << kBackendName << "; phase=" << face_dimension_phase_
                << "; pane=" << pane << "; expected=" << expected.min.x << ',' << expected.min.y << ',' << expected.min.z
                << ':' << expected.max.x << ',' << expected.max.y << ',' << expected.max.z;
      if (bounds) {
        std::cout << "; actual=" << bounds->min.x << ',' << bounds->min.y << ',' << bounds->min.z
                  << ':' << bounds->max.x << ',' << bounds->max.y << ',' << bounds->max.z;
      } else {
        std::cout << "; actual=absent";
      }
      std::cout << std::endl;
      const auto close = [](QVec3 left, QVec3 right) {
        return std::abs(left.x - right.x) < 0.00001f && std::abs(left.y - right.y) < 0.00001f &&
            std::abs(left.z - right.z) < 0.00001f;
      };
      if (!bounds || !close(bounds->min, expected.min) || !close(bounds->max, expected.max)) {
        if (bounds) {
          std::cerr << "face_dimension_bounds; phase=" << face_dimension_phase_ << "; pane=" << pane
                    << "; actual=" << bounds->min.x << ',' << bounds->min.y << ',' << bounds->min.z << ':'
                    << bounds->max.x << ',' << bounds->max.y << ',' << bounds->max.z << std::endl;
        }
        fail("The production Qt pane submitted dimensions at stale face coordinates");
        return false;
      }
      QImage image;
      if (!verify_pane(pane, image)) return false;
      if (instance_preview_ && !verify_instance_preview_pixels(pane, image)) return false;
      std::array<QVec2, 8> corners;
      const auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
      const auto render_size = panel_->surface(kPanes[pane])->render_size_pixels();
      for (std::size_t corner = 0u; corner < corners.size(); ++corner) {
        const auto projected = camera.project_world_point({
            (corner & 1u) ? expected.max.x : expected.min.x,
            (corner & 2u) ? expected.max.y : expected.min.y,
            (corner & 4u) ? expected.max.z : expected.min.z});
        corners[corner] = {projected.position.x * image.width() / render_size.x,
                           projected.position.y * image.height() / render_size.y};
      }
      int yellow = 0, stale = 0;
      for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
          const auto color = image.pixelColor(x, y);
          if (color.red() < 140 || color.green() < 130 || color.blue() + 70 > std::min(color.red(), color.green())) continue;
          ++yellow;
          bool current = false;
          for (std::size_t axis = 0u; axis < 3u; ++axis) {
            for (std::size_t corner = 0u; corner < corners.size(); ++corner) {
              if ((corner & (1u << axis)) != 0u) continue;
              const auto start = corners[corner], end = corners[corner | (1u << axis)];
              const float dx = end.x - start.x, dy = end.y - start.y;
              const float squared = dx * dx + dy * dy;
              const float along = squared > 0 ? std::clamp(
                  ((x + 0.5f - start.x) * dx + (y + 0.5f - start.y) * dy) / squared, 0.0f, 1.0f) : 0.0f;
              current |= std::hypot(x + 0.5f - start.x - along * dx, y + 0.5f - start.y - along * dy) <= 4.0f;
            }
          }
          if (!current) ++stale;
        }
      }
      std::cout << "face_dimensions; phase=" << face_dimension_phase_ << "; pane=" << pane
                << "; delta=" << face_dimension_delta_ << "; yellow=" << yellow << "; stale=" << stale << std::endl;
      if ((pane == 0u && yellow < 12) || stale != 0) {
        fail("The presented dimension cage does not follow the accepted face bounds");
        return false;
      }
      if (instance_preview_ && face_dimension_phase_ == 5u && image != previous_captures_[pane]) {
        fail("An unchanged shared definition preview changed its accepted GPU pixels");
        return false;
      }
      if (instance_preview_) previous_captures_[pane] = image;
    }
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    if (instance_preview_) {
      CrimsonRenderDiagnostics after;
      if (!diagnostics(after) || after.scene_instances < 2u) {
        fail("The public editor instance workflow did not retain both GPU occurrences");
        return false;
      }
      if (face_dimension_phase_ == 5u &&
          (after.translation.position_packs != before_.translation.position_packs ||
           after.translation.attribute_packs != before_.translation.attribute_packs ||
           after.translation.index_packs != before_.translation.index_packs ||
           after.editor_projection.position_packs != before_.editor_projection.position_packs ||
           after.editor_projection.attribute_packs != before_.editor_projection.attribute_packs ||
           after.editor_projection.index_packs != before_.editor_projection.index_packs ||
           after.editor_gpu.vertex_uploads != before_.editor_gpu.vertex_uploads ||
           after.editor_gpu.index_uploads != before_.editor_gpu.index_uploads ||
           after.frame_submissions != before_.frame_submissions)) {
        fail("An unchanged instance preview rebuilt geometry, overlays or another shared GPU frame");
        return false;
      }
    }
#endif
    return true;
  }

  void advance_face_dimensions() {
    bool ok = true;
    switch (face_dimension_phase_) {
    case 0: ok = accept(editor_.modeling().begin_transform_drag(face_dimension_drag())); break;
    case 1: ok = update_face_dimensions(0.3f); break;
    case 2: ok = update_face_dimensions(0.6f); break;
    case 3: ok = update_face_dimensions(0.9f); break;
    case 4: break;
    case 5:
      ok = accept(editor_.modeling().cancel_transform_drag());
      face_dimension_delta_ = 0.0f;
      break;
    case 6:
      ok = accept(editor_.modeling().begin_transform_drag(face_dimension_drag())) && update_face_dimensions(0.6f);
      break;
    case 7: ok = accept(editor_.modeling().commit_transform_drag()); break;
    case 8:
      ok = accept(editor_.history().undo());
      face_dimension_delta_ = 0.0f;
      break;
    case 9:
      ok = accept(editor_.history().redo());
      face_dimension_delta_ = 0.6f;
      break;
    default:
      if (instance_preview_ &&
          (!accept(editor_.mode().set(QuaderEditorMode::Object)) ||
           !accept(editor_.modeling().exit_instance_edit()))) return;
      finish();
      return;
    }
    if (ok) {
      ++face_dimension_phase_;
      next(Step::Initial);
    }
  }

  bool select_preview_box_vertices() {
    // Edit entry clears first-admission components. Select All only extends
    // an already participating component selection (or a sticky hover seed),
    // so seed this known box through the same explicit public API as the
    // canonical EditorModeServiceTests fixture.
    if (!accept(editor_.selection().select_vertices(object_, {1, 2, 3, 4, 5, 6, 7, 8}))) {
      return false;
    }
    const auto mode = editor_.mode().state();
    const auto selected = editor_.selection().summary();
    if (mode.mode != QuaderEditorMode::Edit || mode.participants.size() != 1u ||
        mode.participants.front().id != object_.entity ||
        selected.mode != QuaderSelectionMode::Vertex || selected.selected_vertex_count != 8u) {
      std::cerr << "preview_participants=" << mode.participants.size()
                << "; editor_mode=" << static_cast<int>(mode.mode)
                << "; selection_mode=" << static_cast<int>(selected.mode)
                << "; selected_vertices=" << selected.selected_vertex_count << std::endl;
      fail("The explicit box selection did not establish its Edit participant and eight vertices");
      return false;
    }
    return true;
  }

  void apply_settings() {
    (void)session_.viewport_workspace().update_control_defaults(settings_, ++settings_revision_);
  }

  bool set_shading(QuaderShadingMode mode) {
    for (const auto pane : kPanes) {
      if (!accept(editor_.viewports().set_shading_mode(pane, mode))) {
        return false;
      }
    }
    return true;
  }

  bool begin_overlay_fixture() {
    if (!accept(editor_.mode().set(QuaderEditorMode::Object))) {
      return false;
    }
    const auto other = editor_.modeling().create_box(
        {.min = {-1.25f, -0.35f, -0.45f}, .max = {-0.35f, 0.35f, 0.45f}},
        "Ordinary versus active selection");
    if (!accept(other)) {
      return false;
    }
    other_object_ = other.value();
    settings_.set_bool_value(ViewportBoolSettingId::ShowOverlays, true);
    settings_.set_bool_value(ViewportBoolSettingId::ShowCollisions, true);
    collision_color_ = settings_.color(ViewportColorSettingId::CollisionFill);
    settings_.set_bool_value(ViewportBoolSettingId::ShowGrid, false);
    settings_.set_bool_value(ViewportBoolSettingId::ShowGizmos, false);
    (void)settings_.set_color(ViewportColorSettingId::SelectionWire, {1, 0.45f, 0.05f, 1});
    (void)settings_.set_color(ViewportColorSettingId::ActiveSelection, {1, 105.0f / 255.0f, 180.0f / 255.0f, 1});
    apply_settings();
    return accept(editor_.selection().select_object(object_));
  }

  bool begin_face_fixture() {
    if (!accept(editor_.mode().set(QuaderEditorMode::Object)) ||
        !accept(editor_.scene().set_visible(other_object_.entity, false))) {
      return false;
    }
    quader_poly::Document document;
    const auto quad = [&](float left, float right, float bottom, float top, float z, bool front) {
      std::array vertices{
          quader_poly::add_vertex(document, {left, bottom, z}),
          quader_poly::add_vertex(document, {right, bottom, z}),
          quader_poly::add_vertex(document, {right, top, z}),
          quader_poly::add_vertex(document, {left, top, z})};
      if (component_vertices_.empty()) {
        component_vertices_.assign(vertices.begin(), vertices.end());
      }
      if (!front) {
        std::swap(vertices[1], vertices[3]);
      }
      component_faces_.push_back(quader_poly::add_face(document, vertices));
    };
    // Back-facing coplanar source surfaces let the component tint priority show
    // without opaque source color. The front-facing inset still occludes it.
    quad(-1.6f, -0.65f, -0.3f, 0.35f, 0.6f, false);
    quad(-1.05f, -0.1f, -0.3f, 0.35f, 0.6f, false);
    quad(-0.95f, -0.75f, 0.15f, 0.3f, 0.8f, true);
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Public component overlay priority"});
    if (!accept(created)) {
      return false;
    }
    component_object_ = created.value();
    settings_.set_bool_value(ViewportBoolSettingId::BackfaceCulling, true);
    // Orthographic hover is outline-only when this picking preference is off.
    // This fixture explicitly exercises the enabled hover-fill priority path.
    settings_.set_bool_value(ViewportBoolSettingId::BackfaceSelection, true);
    (void)settings_.set_color(ViewportColorSettingId::SelectionFace, {1, 0, 0, 0.35f});
    (void)settings_.set_color(ViewportColorSettingId::ActiveSelection, {0, 1, 1, 1});
    (void)settings_.set_color(ViewportColorSettingId::HoverFace, {1, 1, 0, 0.35f});
    (void)settings_.set_color(ViewportColorSettingId::SelectedVertex, {1, 0.45f, 0.05f, 1});
    (void)settings_.set_color(ViewportColorSettingId::HoverVertex, {1, 1, 0, 1});
    apply_settings();
    // This analytic fixture defines front at +Z and its opaque inset closer
    // to that eye; use an explicit camera instead of the Front preset's -Z eye.
    session_.viewport_workspace().camera(ViewportPaneId::Front)
        .view_axis(ViewportViewAxis::Z, 1);
    return accept(editor_.selection().select_object(component_object_)) &&
        accept(editor_.mode().set(QuaderEditorMode::Edit)) &&
        accept(editor_.selection().set_mode(QuaderSelectionMode::Face)) &&
        accept(editor_.selection().clear()) && accept(editor_.selection().clear_hover());
  }

  QRect object_rectangle(std::size_t pane, bool other = false) const {
    const auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
    QRect bounds;
    for (const float x : {other ? -1.25f : 0.2f, other ? -0.35f : 1.4f}) {
      for (const float y : {-0.35f, 0.35f}) {
        for (const float z : {-0.45f, 0.45f}) {
          const auto projected = camera.project_world_point({x, y, z});
          const QPoint pixel(qRound(projected.position.x), qRound(projected.position.y));
          bounds = bounds.isNull() ? QRect(pixel, QSize(1, 1)) : bounds.united(QRect(pixel, QSize(1, 1)));
        }
      }
    }
    return bounds.adjusted(-8, -8, 8, 8);
  }

  std::size_t pink_pixels(const QImage &image, QRect rectangle) const {
    rectangle = rectangle.intersected(image.rect());
    std::size_t result = 0u;
    for (int y = rectangle.top(); y <= rectangle.bottom(); ++y) {
      for (int x = rectangle.left(); x <= rectangle.right(); ++x) {
        const QColor color = image.pixelColor(x, y);
        result += color.red() > color.green() + 30 && color.blue() > color.green() + 20;
      }
    }
    return result;
  }

  QColor world_sample(const QImage &image, std::size_t pane, QVec3 world) const {
    const auto projected = session_.viewport_workspace().camera(kPanes[pane]).project_world_point(world);
    const QPoint pixel(qRound(projected.position.x), qRound(projected.position.y));
    return image.pixelColor(std::clamp(pixel.x(), 0, image.width() - 1),
                            std::clamp(pixel.y(), 0, image.height() - 1));
  }

  bool near_colors(QColor first, QColor second, int tolerance = 6) const {
    return std::abs(first.red() - second.red()) <= tolerance &&
        std::abs(first.green() - second.green()) <= tolerance &&
        std::abs(first.blue() - second.blue()) <= tolerance;
  }

  int hue(QColor color) const {
    const int r = color.red(), g = color.green(), b = color.blue();
    if (r > g + 30 && b > g + 20) return 1;
    if (g > r + 30 && b > r + 30) return 2;
    if (r > b + 30 && g > b + 30 && r * 4 >= g * 3 && g * 4 >= r * 3) return 3;
    if (r > g + 30 && r > b + 30) return 4;
    if (g > r + 30 && g > b + 30) return 5;
    if (b > r + 30 && b > g + 30) return 6;
    return 0;
  }

  bool compare_reference(const QImage &image, std::size_t pane) {
    if (reference_directory_.isEmpty() || step_ < Step::ObjectSingleton ||
        step_ == Step::HostRejected || step_ == Step::HostRetry) {
      return true;
    }
    const QString name = QStringLiteral("editor-%1-pane-%2-offscreen.png")
        .arg(static_cast<int>(step_)).arg(pane);
    const QImage reference(QDir(reference_directory_).filePath(name));
    if (reference.isNull() || reference.size() != image.size()) {
      fail("The Full reference image is missing or has different pane dimensions");
      return false;
    }
    std::size_t reference_colored = 0u, actual_colored = 0u, matched = 0u;
    std::size_t reference_collision_pixels = 0u, matched_collision_colors = 0u;
    for (int y = 2; y < image.height() - 2; ++y) {
      for (int x = 2; x < image.width() - 2; ++x) {
        const QColor reference_color = reference.pixelColor(x, y);
        const int expected = hue(reference_color);
        actual_colored += hue(image.pixelColor(x, y)) != 0;
        if (expected == 0) continue;
        ++reference_colored;
        const bool collision_color = step_ == Step::CollisionMaxOpacity && expected == 2;
        reference_collision_pixels += collision_color;
        bool found = false;
        bool color_matched = false;
        for (int dy = -2; dy <= 2 && (!found || (collision_color && !color_matched)); ++dy) {
          for (int dx = -2; dx <= 2; ++dx) {
            const QColor actual = image.pixelColor(x + dx, y + dy);
            const bool same_hue = hue(actual) == expected;
            found |= same_hue;
            color_matched |= collision_color && same_hue && near_colors(reference_color, actual, 12);
          }
        }
        matched += found;
        matched_collision_colors += color_matched;
      }
    }
    if (reference_colored > 20u &&
        (matched * 100u < reference_colored * 90u ||
         actual_colored * 100u < reference_colored * 85u ||
         actual_colored * 100u > reference_colored * 118u)) {
      std::cerr << "reference_pane=" << pane << "; colored=" << reference_colored
                << "; actual=" << actual_colored << "; matched_within_2px=" << matched << std::endl;
      fail("GPU overlay color/coverage disagrees with the Full reference");
      return false;
    }
    // Selected collision wires multiply authored alpha by 1.6. Match RGB as
    // well as hue so clamping that value in vertex storage cannot pass by
    // preserving only the cyan edge coverage. The spatial tolerance absorbs AA.
    if (step_ == Step::CollisionMaxOpacity &&
        (reference_collision_pixels == 0u ||
         matched_collision_colors * 100u < reference_collision_pixels * 90u)) {
      std::cerr << "reference_pane=" << pane
                << "; collision_cyan=" << reference_collision_pixels
                << "; rgb_matched_within_2px_12codes=" << matched_collision_colors << std::endl;
      fail("Maximum-opacity collision RGB disagrees with the Full reference");
      return false;
    }
    return true;
  }

  bool has_hue_near(const QImage &image, std::size_t pane, QVec3 world, int expected,
                    int radius = 8) const {
    const auto projected = session_.viewport_workspace().camera(kPanes[pane]).project_world_point(world);
    const QPoint center(qRound(projected.position.x), qRound(projected.position.y));
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        const QPoint pixel = center + QPoint(x, y);
        if (image.rect().contains(pixel) && hue(image.pixelColor(pixel)) == expected) {
          return true;
        }
      }
    }
    return false;
  }

  bool verify_overlay_frame(const std::array<QImage, 4> &captures) {
    if (step_ < Step::ObjectSingleton) return true;
    constexpr std::size_t kFront = 2u;
    const auto &front = captures[kFront];
    if (step_ == Step::ObjectSingleton) collision_on_captures_ = captures;
    if (step_ == Step::CollisionOff || step_ == Step::CollisionRestored ||
        step_ == Step::CollisionMaxOpacity || step_ == Step::CollisionOpacityRestored) {
      for (std::size_t pane = 0u; pane < captures.size(); ++pane) {
        const auto cyan_pixels = [this](const QImage &image) {
          std::size_t count = 0u;
          for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
              count += hue(image.pixelColor(x, y)) == 2;
            }
          }
          return count;
        };
        const auto baseline_cyan = cyan_pixels(collision_on_captures_[pane]);
        const auto current_cyan = cyan_pixels(captures[pane]);
        std::cout << "collision_pixels; step=" << static_cast<int>(step_)
                  << "; pane=" << pane << "; baseline_cyan=" << baseline_cyan
                  << "; current_cyan=" << current_cyan << std::endl;
        if (baseline_cyan < 10u ||
            (step_ == Step::CollisionOff &&
             (current_cyan != 0u || captures[pane] == collision_on_captures_[pane])) ||
            (step_ == Step::CollisionMaxOpacity &&
             (current_cyan == 0u || captures[pane] == collision_on_captures_[pane])) ||
            ((step_ == Step::CollisionRestored || step_ == Step::CollisionOpacityRestored) &&
             captures[pane] != collision_on_captures_[pane])) {
          fail("Collision visibility or opacity failed to change its cyan geometry and restore the accepted pane pixels");
          return false;
        }
      }
    }
    if (step_ == Step::ObjectSingletonCustom) {
      // A style mutation can rebuild proxy raster state. Preserve the ordinary
      // silhouette's color and physical width while allowing a one-pixel edge
      // coverage decision; unchanged-frame checks below remain pixel-exact.
      for (std::size_t pane = 0; pane < captures.size(); ++pane) {
        const auto &before = previous_captures_[pane];
        const auto &after = captures[pane];
        const QRect bounds = object_rectangle(pane).intersected(after.rect().adjusted(1, 1, -1, -1));
        std::array<std::size_t, 2> orange{}, cyan{};
        const auto ordinary = [](int value) { return value == 3 || value == 4; };
        for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
          for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const int old_hue = hue(before.pixelColor(x, y));
            const int new_hue = hue(after.pixelColor(x, y));
            // Display encoding moves the orange core into the broad yellow
            // class. Include that core rather than measuring only its red AA fringe.
            orange[0] += ordinary(old_hue); orange[1] += ordinary(new_hue);
            cyan[0] += old_hue == 2; cyan[1] += new_hue == 2;
            for (const bool reverse : {false, true}) {
              if (!ordinary(reverse ? new_hue : old_hue)) continue;
              bool matched = false;
              const auto &target = reverse ? before : after;
              for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                  matched |= ordinary(hue(target.pixelColor(x + dx, y + dy)));
                }
              }
              if (!matched) {
                fail("A singleton's ordinary silhouette moved or lost its configured orange color");
                return false;
              }
            }
          }
        }
        if (orange[0] == 0u || orange[1] * 100u < orange[0] * 99u ||
            orange[1] * 100u > orange[0] * 101u || cyan[0] != cyan[1]) {
          std::cerr << "singleton_pane=" << pane << "; ordinary_pixels=" << orange[0] << ',' << orange[1]
                    << "; cyan_pixels=" << cyan[0] << ',' << cyan[1] << std::endl;
          fail("A singleton object adopted the active-only color or changed ordinary silhouette width");
          return false;
        }
      }
    }
    if (step_ == Step::ObjectsActivePrimary) {
      active_primary_captures_ = captures;
      if (pink_pixels(front, object_rectangle(kFront)) < 20u ||
          pink_pixels(front, object_rectangle(kFront)) <=
              pink_pixels(previous_captures_[kFront], object_rectangle(kFront))) {
        fail("Adding another selected object did not enable the primary pink silhouette");
        return false;
      }
    }
    if (step_ == Step::ObjectsActiveOther) {
      if (pink_pixels(front, object_rectangle(kFront)) != 0u ||
          pink_pixels(front, object_rectangle(kFront, true)) < 20u ||
          pink_pixels(front, object_rectangle(kFront, true)) <=
              pink_pixels(active_primary_captures_[kFront], object_rectangle(kFront, true))) {
        fail("Changing the active object did not transfer the pink silhouette");
        return false;
      }
    }
    if (step_ == Step::FacesBase) face_base_captures_ = captures;
    if (step_ == Step::FacesSelected || step_ == Step::FacesHovered) {
      // Keep face samples away from the source-wire diagonals retained by
      // the component frame; their separately styled coverage is not face tint.
      constexpr std::array<QVec3, 4> kSamples{{
          {-1.35f, -0.15f, 0.6f}, {-0.9f, -0.1f, 0.6f},
          {-0.35f, -0.15f, 0.6f}, {-0.9f, 0.25f, 0.8f}}};
      const QColor left = world_sample(front, kFront, kSamples[0]);
      const QColor overlap = world_sample(front, kFront, kSamples[1]);
      const QColor right = world_sample(front, kFront, kSamples[2]);
      const QColor blocker = world_sample(front, kFront, kSamples[3]);
      const QColor blocked_base = world_sample(face_base_captures_[kFront], kFront, kSamples[3]);
      // The configured .35 opacity becomes89/255 in the packed overlay color.
      // Linear OVER on black and one sRGB encode yields about159 display codes.
      const int tint = static_cast<int>(std::lround(
          (1.055 * std::pow(89.0 / 255.0, 1.0 / 2.4) - 0.055) * 255.0));
      const QColor active(0, tint, tint);
      const QColor ordinary(tint, 0, 0);
      const QColor hovered(tint, tint, 0);
      const bool priority = step_ == Step::FacesSelected
          ? near_colors(left, active) && near_colors(right, ordinary) && near_colors(left, overlap)
          : near_colors(left, active) && near_colors(right, hovered) && near_colors(right, overlap);
      if (!priority || !near_colors(blocker, blocked_base)) {
        for (std::size_t index = 0; index < kSamples.size(); ++index) {
          const auto projected = session_.viewport_workspace().camera(kPanes[kFront])
              .project_world_point(kSamples[index]);
          const auto actual = world_sample(front, kFront, kSamples[index]);
          const auto base = world_sample(face_base_captures_[kFront], kFront, kSamples[index]);
          std::cerr << "face_sample=" << index << "; projected=" << projected.position.x << ',' << projected.position.y
                    << "; actual_rgb=" << actual.red() << ',' << actual.green() << ',' << actual.blue()
                    << "; base_rgb=" << base.red() << ',' << base.green() << ',' << base.blue()
                    << "; expected_tint=" << tint << std::endl;
        }
        fail("Component face priority accumulated tints or crossed opaque source depth");
        return false;
      }
      if (step_ == Step::FacesSelected) face_selected_captures_ = captures;
    }
    if (step_ == Step::FacesHoverCleared && captures != face_selected_captures_) {
      fail("Clearing component hover did not restore the accepted selected-face pixels");
      return false;
    }
    if (step_ == Step::Edges &&
        (!has_hue_near(front, kFront, {-1.125f, -0.3f, 0.6f}, 2) ||
         !has_hue_near(front, kFront, {-1.125f, 0.35f, 0.6f}, 4))) {
      fail("Active and ordinary component edges did not reach their source spans");
      return false;
    }
    if ((step_ == Step::Vertices || step_ == Step::VertexHovered) &&
        (!has_hue_near(front, kFront, {-1.6f, -0.3f, 0.6f}, 2) ||
         !has_hue_near(front, kFront, {-0.65f, -0.3f, 0.6f}, step_ == Step::Vertices ? 4 : 3))) {
      fail("Selected/active/hover vertex markers lost their visible authored positions");
      return false;
    }
    if (step_ == Step::ActivePreviewBase) active_preview_base_captures_ = captures;
    if (step_ == Step::ActivePreview || step_ == Step::ActivePreviewRetained) {
      if (!has_hue_near(front, kFront, {-1.4f, -0.1f, 0.6f}, 2, 3) ||
          !has_hue_near(front, kFront, {-0.45f, -0.1f, 0.6f}, 4, 3) ||
          has_hue_near(front, kFront, {-1.6f, -0.3f, 0.6f}, 2, 3) ||
          has_hue_near(front, kFront, {-0.65f, -0.3f, 0.6f}, 4, 3)) {
        fail("The first active preview candidate retained old selected points or lost moved overlay points");
        return false;
      }
    }
    if (step_ == Step::ActivePreviewRetained && captures != previous_captures_) {
      fail("An unchanged active preview changed retained pane pixels");
      return false;
    }
    if (step_ == Step::ActivePreviewCancel && captures != active_preview_base_captures_) {
      fail("Cancelling the active preview did not restore the source mesh and overlay pixels");
      return false;
    }
    if (step_ == Step::GridBase) grid_base_captures_ = captures;
    if (step_ == Step::Grid) {
      std::size_t panes_with_grid = 0u;
      for (std::size_t pane = 0; pane < captures.size(); ++pane) {
        std::size_t changed = 0u;
        for (int y = 0; y < captures[pane].height(); ++y) {
          for (int x = 0; x < captures[pane].width(); ++x) {
            changed += !near_colors(captures[pane].pixelColor(x, y),
                grid_base_captures_[pane].pixelColor(x, y), 12);
          }
        }
        panes_with_grid += changed > 100u;
      }
      if (panes_with_grid < 2u) {
        fail("Ground grid toggling did not change actual pixels in independent panes");
        return false;
      }
    }
    if ((step_ == Step::GridRetained || step_ == Step::GizmoRetained) &&
        captures != previous_captures_) {
      fail("An unchanged grid/gizmo frame changed its retained GPU pixels");
      return false;
    }
    if (step_ == Step::ShadedWire) {
      bool colored = false;
      const QRect bounds = object_rectangle(kFront).intersected(front.rect());
      for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
          colored |= hue(front.pixelColor(x, y)) == 2;
        }
      }
      if (!colored) {
        fail("Wireframe-on-Shaded did not display its configured topology color");
        return false;
      }
    }
    if (step_ == Step::Wireframe) {
      // The exact center lies on a legitimate projected topology diagonal.
      // These four interiors avoid both diagonal families while still testing
      // both halves of the face for retained opaque scene color.
      for (const QVec3 world : std::array<QVec3, 4>{{
               {0.4f, 0.1f, 0}, {1.15f, 0.1f, 0},
               {0.4f, -0.1f, 0}, {1.15f, -0.1f, 0}}}) {
        const QColor interior = world_sample(front, kFront, world);
        if (std::max({interior.red(), interior.green(), interior.blue()}) > 12) {
          const auto projected = session_.viewport_workspace().camera(kPanes[kFront]).project_world_point(world);
          std::cerr << "wireframe_interior=" << projected.position.x << ',' << projected.position.y
                    << "; actual_rgb=" << interior.red() << ',' << interior.green() << ',' << interior.blue() << std::endl;
          fail("Wireframe retained an opaque mesh interior away from authored wires");
          return false;
        }
      }
    }
    if (step_ == Step::Gizmo || step_ == Step::GizmoCamera) {
      const auto pivot = session_.viewport_workspace().camera(ViewportPaneId::Front)
          .project_world_point({0.8f, 0, 0});
      std::array<std::size_t, 7> counts{};
      const QRect bounds(qRound(pivot.position.x) - 100, qRound(pivot.position.y) - 100, 201, 201);
      const QRect clipped = bounds.intersected(front.rect());
      for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
          ++counts[static_cast<std::size_t>(hue(front.pixelColor(x, y)))];
        }
      }
      if (static_cast<int>(counts[4] > 6u) + static_cast<int>(counts[5] > 6u) +
          static_cast<int>(counts[6] > 6u) < 2) {
        fail("The Move gizmo did not display independent semantic axes");
        return false;
      }
    }
    for (std::size_t pane = 0; pane < captures.size(); ++pane) {
      if (!compare_reference(captures[pane], pane)) return false;
    }
    return true;
  }

#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
  bool diagnostics(CrimsonRenderDiagnostics &value) {
    if (!crimson_render_diagnostics(*host_, value)) {
      fail("The facade did not retain the requested Crimson host");
      return false;
    }
    const bool expected_rejection = (step_ == Step::HostRejected || step_ == Step::HostRetry) &&
        value.error.find("out-of-range triangle index") != std::string::npos;
    if (!value.error.empty() && !expected_rejection) {
      fail(value.error);
      return false;
    }
    return true;
  }

  bool prepare_host_rejection() {
    const auto *selected = snapshots_.selected_object_snapshot();
    if (selected == nullptr) {
      fail("The accepted host fixture has no selected-object scene binding");
      return false;
    }
    host_retry_scene_ = build_render_scene_snapshot(session_.scene(), session_.assets(),
        {.scene_binding = selected->scene_binding});
    if (host_retry_scene_.meshes.empty() || !host_retry_scene_.meshes.front().mesh.geometry_payload) {
      fail("The host rejection fixture has no pinned ordinary mesh");
      return false;
    }
    host_rejected_scene_ = host_retry_scene_;
    auto &mesh = host_rejected_scene_.meshes.front().mesh;
    auto malformed = std::make_shared<MeshGeometryPayload>(*mesh.geometry_payload);
    if (malformed->indices.empty()) {
      fail("The host rejection fixture has no source triangle indices");
      return false;
    }
    malformed->indices.back() = static_cast<std::uint32_t>(malformed->vertices.size());
    mesh.geometry_payload = malformed;
    mesh.indices = malformed->indices;
    ++mesh.mesh_revision;
    (void)settings_.set_color(ViewportColorSettingId::SelectionWire, {0, 1, 1, 1});
    apply_settings();
    return true;
  }
#endif

  void fit_root_to_screen(QSize requested) {
    QScreen *screen = root_->screen();
    if (screen == nullptr) screen = application_.primaryScreen();
    if (screen == nullptr) {
      root_->resize(requested);
      return;
    }
    // Qt reports available geometry in logical coordinates, including under
    // QT_SCALE_FACTOR. Reserve the real native frame before fitting the client.
    (void)root_->winId();
    const QMargins margins = root_->windowHandle()->frameMargins();
    const QRect available = screen->availableGeometry();
    const QSize frame(margins.left() + margins.right(), margins.top() + margins.bottom());
    const QSize capacity(std::max(1, available.width() - frame.width()),
                         std::max(1, available.height() - frame.height()));
    QSize fitted = requested;
    if (requested.width() > capacity.width() || requested.height() > capacity.height()) {
      // Fit both canonical sizes against the larger resize fixture so a small
      // logical desktop still exercises a real change in all pane extents.
      const double scale = std::min({1.0, static_cast<double>(capacity.width()) / 1060.0,
                                    static_cast<double>(capacity.height()) / 740.0});
      fitted = {std::max(1, static_cast<int>(std::floor(requested.width() * scale))),
                std::max(1, static_cast<int>(std::floor(requested.height() * scale)))};
    }
    root_->resize(fitted);
    root_->move(available.center() - QPoint((fitted.width() + frame.width()) / 2,
                                            (fitted.height() + frame.height()) / 2));
    std::cout << "editor_window; requested=" << requested.width() << ',' << requested.height()
              << "; fitted=" << fitted.width() << ',' << fitted.height()
              << "; available=" << available.width() << ',' << available.height()
              << "; device_pixel_ratio=" << screen->devicePixelRatio() << std::endl;
  }

  void create_root() {
    root_ = std::make_unique<QWidget>();
    root_->setWindowFlag(Qt::WindowStaysOnTopHint);
    root_->setWindowTitle(QStringLiteral("Quader %1 public editor adapter gate")
        .arg(QString::fromLatin1(kBackendName)));
    auto *layout = new QVBoxLayout(root_.get());
    layout->setContentsMargins(0, 0, 0, 0);
    panel_ = new QtViewportPanel(root_.get(), true);
    panel_->attach_workspace(session_.viewport_workspace());
    panel_->set_viewport_settings(settings_);
    layout->addWidget(panel_);
    anchor_ = new QtVulkanCompositionWidget(root_.get());
    anchor_->setFixedSize(1, 1);
    anchor_->setFixedColorBufferSize({1, 1});
    anchor_->setFocusPolicy(Qt::NoFocus);
    anchor_->setAttribute(Qt::WA_TransparentForMouseEvents);
    anchor_->set_release_callback([this] {
      if (host_->initialized()) {
        qFatal("Editor GPU probe released a borrowed Qt device before its host drained");
      }
      ++release_count_;
    });
    QObject::connect(anchor_, &QRhiWidget::frameSubmitted, anchor_, [this] {
      if (host_->initialized()) {
        host_->complete_composited_presentation_frame();
      }
    });
    for (std::size_t index = 0; index < kPanes.size(); ++index) {
      const QString name = QStringLiteral("viewportSurfaceContainer_%1")
          .arg(QString::fromUtf8(viewport_pane_stable_id(kPanes[index]).data()));
      pane_widgets_[index] = dynamic_cast<QtVulkanCompositionWidget *>(panel_->findChild<QWidget *>(name));
      if (pane_widgets_[index] == nullptr) {
        fail("The production pane is missing its composed widget");
        return;
      }
      QObject::connect(pane_widgets_[index], &QRhiWidget::frameSubmitted, root_.get(), [this, index] {
        submitted_generations_[index] = requested_generation_;
        presentation_clock_.restart();
      });
    }
    panel_->set_composed_frame_prepare_callback([this] {
      if (initialized_ && !preparing_) {
        prepare_frame();
      }
    });
    panel_->set_presentation_requested_callback([this] {
      if (!preparing_) {
        needs_prepare_ = true;
      }
    });
    // Fitting queries the native frame. Construct every Vulkan child first so
    // Qt selects the Vulkan surface and backing store when winId() creates it.
    fit_root_to_screen({920, 680});
    ++root_count_;
    root_->show();
    anchor_->show();
    anchor_->lower();
  }

  void trace_retirement(std::string_view milestone) const {
    std::cout << "editor_retirement; backend=" << kBackendName << "; root=" << root_count_
              << "; milestone=" << milestone << "; root_present=" << (root_ != nullptr) << std::endl;
  }

  void retire_root() {
    if (!root_) {
      return;
    }
    trace_retirement("retire_root_enter");
    initialized_ = false;
    if (panel_ != nullptr) {
      panel_->set_presentation_requested_callback({});
      panel_->set_composed_frame_prepare_callback({});
      panel_->reset_interactions(&editor_, &session_.native_tools());
      trace_retirement("before_panel_shutdown");
      panel_->shutdown();
      trace_retirement("after_panel_shutdown");
    }
    if (sink_) {
      trace_retirement("before_sink_shutdown");
      sink_->shutdown();
      trace_retirement("after_sink_shutdown");
      trace_retirement("before_sink_reset");
      sink_.reset();
      trace_retirement("after_sink_reset");
    }
    trace_retirement("before_snapshot_reset");
    snapshots_.reset();
    trace_retirement("after_snapshot_reset");
    if (host_) {
      // Production shutdown is fatal if completion cannot be proved. This call
      // precedes the widget release callback and destruction of Qt's device.
      trace_retirement("before_host_shutdown");
      host_->shutdown();
      trace_retirement("after_host_shutdown");
    }
    if (anchor_ != nullptr) {
      anchor_->set_render_system(nullptr);
    }
    trace_retirement("before_root_reset");
    root_.reset();
    trace_retirement("after_root_reset");
    preparation_count_ = 0u;
    preparation_frame_baseline_ = 0u;
    panel_ = nullptr;
    anchor_ = nullptr;
    pane_widgets_.fill(nullptr);
    submitted_generations_.fill(0u);
    trace_retirement("retire_root_exit");
  }

  bool initialize_host() {
    const auto *context = anchor_->shared_context();
    if (context == nullptr) {
      return false;
    }
    if (!host_->initialize(nullptr, resources_, {.backend = RendererBackend::Vulkan}, context)) {
      fail("The renderer facade could not initialize on Qt's real device");
      return false;
    }
    auto production_sink = host_->create_scene_sink(resources_);
    if (!production_sink || !panel_->initialize(*host_)) {
      fail("The four-pane facade resources could not initialize");
      return false;
    }
    auto recorded = std::make_unique<ProbeSceneSink>(std::move(production_sink));
    recorded_sink_ = recorded.get();
    sink_ = std::move(recorded);
    anchor_->set_render_system(host_.get());
    panel_->set_startup_presentation_blocked(false);
    initialized_ = true;
    needs_prepare_ = true;
    std::cout << "backend=" << kBackendName << "; borrowed_qt_device=" << context->device
              << "; root=" << root_count_ << "; panes=4" << std::endl;
    return true;
  }

  void prepare_frame() {
    if (!initialized_ || preparing_) {
      return;
    }
    preparing_ = true;
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    CrimsonRenderDiagnostics current;
    if (!diagnostics(current)) {
      preparing_ = false;
      return;
    }
    if (preparation_count_ != 0u && current.frame_submissions - preparation_frame_baseline_ > 1u) {
      fail("Pane callbacks advanced multiple global frames within one preparation");
      preparing_ = false;
      return;
    }
    preparation_frame_baseline_ = current.frame_submissions;
#endif
    ++preparation_count_;
    host_->begin_shared_command_batch();
    if (accept(QuaderRenderApiRuntimeAccess::sync_scene(editor_.render(), *sink_))) {
      panel_->prepare_viewports(editor_, *sink_, snapshots_, session_.scene(),
          session_.assets(), session_.modeling(), session_.native_tools());
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
      // Inject after the ordinary source sync and after new editor semantics
      // are staged. Every nested preparation must retain this rejection until
      // the explicit retry, so a later pane cannot silently replace the source.
      if (step_ == Step::HostRejected) sink_->sync(host_rejected_scene_);
      if (step_ == Step::HostRetry && !host_retry_staged_) {
        sink_->sync(host_retry_scene_);
        host_retry_staged_ = true;
      }
#endif
      host_->begin_presentation_frame();
      (void)panel_->render_viewports(false);
      anchor_->update();
    }
    host_->end_shared_command_batch();
    preparing_ = false;
  }

  bool verify_pane(std::size_t index, QImage &offscreen) {
    auto *widget = pane_widgets_[index];
    const auto *context = widget->shared_context();
    const auto *root_context = anchor_->shared_context();
    if (context == nullptr || root_context == nullptr || context->device != root_context->device ||
        context->graphics_queue != root_context->graphics_queue) {
      fail("The pane did not borrow the one root Vulkan device/queue");
      return false;
    }
    QScreen *screen = root_->windowHandle()->screen();
    const QPoint global_origin = widget->mapToGlobal(QPoint{});
    if (!screen->availableGeometry().contains(QRect(global_origin, widget->size()))) {
      fail("A composed pane extends outside the available logical desktop");
      return false;
    }
    const QPoint origin = global_origin - screen->geometry().topLeft();
    const QImage presented = screen->grabWindow(0, origin.x(), origin.y(),
                                               widget->width(), widget->height()).toImage();
    offscreen = widget->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
    const QString stem = edge_gizmo_
        ? QStringLiteral("edge-gizmo-%1-pane-%2-").arg(edge_gizmo_phase_).arg(index)
        : instance_preview_
        ? QStringLiteral("instance-preview-%1-pane-%2-").arg(face_dimension_phase_).arg(index)
        : lamina_overlap_
        ? QStringLiteral("lamina-overlap-%1-pane-%2-").arg(lamina_phase_).arg(index)
        : face_dimensions_
        ? QStringLiteral("face-dimensions-%1-pane-%2-").arg(face_dimension_phase_).arg(index)
        : QStringLiteral("editor-%1-pane-%2-").arg(static_cast<int>(step_)).arg(index);
    const bool saved = presented.save(QDir(output_directory_).filePath(stem + QStringLiteral("presented.png"))) &&
        offscreen.save(QDir(output_directory_).filePath(stem + QStringLiteral("offscreen.png")));
    if (!saved || presented.isNull() || offscreen.isNull()) {
      fail("Cannot capture/save the real composed pane");
      return false;
    }
    std::size_t foreground = 0u, compared_foreground = 0u;
    for (int y = 0; y < offscreen.height(); ++y) {
      for (int x = 0; x < offscreen.width(); ++x) {
        const QColor color = offscreen.pixelColor(x, y);
        foreground += std::max({color.red(), color.green(), color.blue()}) > 24;
      }
    }
    const auto area = static_cast<std::size_t>(offscreen.width()) * offscreen.height();
    if (foreground < area / 500u || foreground > area * 4u / 5u) {
      fail("A pane lost its mesh or drew unrelated full-target pixels");
      return false;
    }
    // Dimension numbers are Qt labels composed above the GPU texture. Exclude
    // only their real visible rectangles from this scenario's texture/desktop
    // comparison; the cage pixels remain subject to the ordinary parity checks.
    std::vector<QRect> dimension_labels;
    if (face_dimensions_) {
      for (const auto *label : root_->findChildren<QLabel *>()) {
        if (!label->isVisible() ||
            !label->objectName().startsWith(QStringLiteral("viewportSelectionDimension"))) continue;
        const auto local = label->mapToGlobal(QPoint{}) - global_origin;
        dimension_labels.emplace_back(
            QPoint(qFloor(static_cast<double>(local.x()) * presented.width() / widget->width()),
                   qFloor(static_cast<double>(local.y()) * presented.height() / widget->height())),
            QSize(qCeil(static_cast<double>(label->width()) * presented.width() / widget->width()),
                  qCeil(static_cast<double>(label->height()) * presented.height() / widget->height())));
        dimension_labels.back().adjust(-2, -2, 2, 2);
      }
    }
    const auto dimension_label_at = [&](QPoint pixel) {
      return std::ranges::any_of(dimension_labels,
          [&](const QRect &rectangle) { return rectangle.contains(pixel); });
    };
    // Pane controls occupy the outer strip. Compare content samples only;
    // the offscreen path contains no Qt chrome, and its pixels remain the oracle.
    for (int row = 3; row < 18; ++row) {
      for (int column = 3; column < 18; ++column) {
        const int x = column * offscreen.width() / 20;
        const int y = row * offscreen.height() / 20;
        const QPoint presented_pixel(column * presented.width() / 20, row * presented.height() / 20);
        if (dimension_label_at(presented_pixel)) continue;
        const QColor expected = offscreen.pixelColor(x, y);
        const QColor actual = presented.pixelColor(presented_pixel);
        if (std::abs(expected.red() - actual.red()) > 4 ||
            std::abs(expected.green() - actual.green()) > 4 ||
            std::abs(expected.blue() - actual.blue()) > 4) {
          std::cerr << "pane=" << index << "; sample=" << column << ',' << row
                    << "; offscreen=" << expected.red() << ',' << expected.green() << ',' << expected.blue()
                    << "; presented=" << actual.red() << ',' << actual.green() << ',' << actual.blue() << std::endl;
          fail("Presented pane content disagrees with the GPU target");
          return false;
        }
        compared_foreground += std::max({expected.red(), expected.green(), expected.blue()}) > 24;
      }
    }
    // Sparse Wireframe edges can fall between every regular grid sample.
    // Keep those comparisons and also prove that actual foreground pixels in
    // independent content cells reached the desktop with the same RGB bounds.
    const QRect content_region(
        QPoint(offscreen.width() * 3 / 20, offscreen.height() * 3 / 20),
        QPoint(offscreen.width() * 17 / 20, offscreen.height() * 17 / 20));
    for (int row = 0; row < 8; ++row) {
      for (int column = 0; column < 8; ++column) {
        const QRect cell(
            content_region.left() + content_region.width() * column / 8,
            content_region.top() + content_region.height() * row / 8,
            content_region.width() * (column + 1) / 8 - content_region.width() * column / 8,
            content_region.height() * (row + 1) / 8 - content_region.height() * row / 8);
        bool sampled = false;
        for (int y = cell.top(); y <= cell.bottom() && !sampled; ++y) {
          for (int x = cell.left(); x <= cell.right(); ++x) {
            const QColor expected = offscreen.pixelColor(x, y);
            if (std::max({expected.red(), expected.green(), expected.blue()}) <= 24) continue;
            const QPoint presented_pixel(
                static_cast<int>((static_cast<std::int64_t>(2 * x + 1) * presented.width()) / (2 * offscreen.width())),
                static_cast<int>((static_cast<std::int64_t>(2 * y + 1) * presented.height()) / (2 * offscreen.height())));
            if (dimension_label_at(presented_pixel)) continue;
            const QColor actual = presented.pixelColor(presented_pixel);
            if (!near_colors(expected, actual, 4)) {
              std::cerr << "pane=" << index << "; foreground_sample=" << x << ',' << y
                        << "; offscreen=" << expected.red() << ',' << expected.green() << ',' << expected.blue()
                        << "; presented=" << actual.red() << ',' << actual.green() << ',' << actual.blue() << std::endl;
              fail("Presented foreground disagrees with an actual GPU edge or surface pixel");
              return false;
            }
            ++compared_foreground;
            sampled = true;
            break;
          }
        }
      }
    }
    if (compared_foreground == 0u) {
      fail("Presented comparisons missed all mesh pixels");
      return false;
    }
    QVec3 expected_center = lamina_overlap_
        ? (lamina_phase_ == 30u || lamina_phase_ == 31u ? QVec3{1, 1, 0.3f} : QVec3{-2, 4, 13})
        : step_ >= Step::Pose ? QVec3{0.8f, 0, 0} : QVec3{};
    if (step_ == Step::Preview) {
      expected_center += kPreviewDelta;
    }
    const auto projected = session_.viewport_workspace().camera(kPanes[index]).project_world_point(expected_center);
    if (!projected.visible) {
      fail("The canonical camera lost the expected scene center");
      return false;
    }
    const QVec2 render_size = panel_->surface(kPanes[index])->render_size_pixels();
    const QPoint center(qRound(projected.position.x * offscreen.width() / render_size.x),
                        qRound(projected.position.y * offscreen.height() / render_size.y));
    bool covered = false;
    for (int y = -3; y <= 3; ++y) {
      for (int x = -3; x <= 3; ++x) {
        const QPoint pixel = center + QPoint(x, y);
        if (offscreen.rect().contains(pixel)) {
          const QColor color = offscreen.pixelColor(pixel);
          covered |= std::max({color.red(), color.green(), color.blue()}) > 24;
        }
      }
    }
    if (!covered && step_ != Step::Wireframe && !face_dimensions_ && !edge_gizmo_) {
      fail("The GPU pane did not cover the API scene/preview center projected by its own camera");
      return false;
    }
    return true;
  }

  bool verify_frame() {
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    CrimsonRenderDiagnostics after;
    if (!diagnostics(after) || after.active_views != 4u || after.accepted_scene_generation == 0u) {
      fail("Four independent views did not accept the shared scene");
      return false;
    }
#endif
    std::array<QImage, 4> captures;
    for (std::size_t index = 0; index < captures.size(); ++index) {
      if (!verify_pane(index, captures[index])) {
        return false;
      }
    }
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    if (!diagnostics(after)) {
      return false;
    }
    for (const auto &view : after.editor_views) {
      std::cout << "accepted_editor_view; step=" << static_cast<int>(step_)
                << "; layer=" << static_cast<unsigned>(view.layer_mask)
                << "; identity=" << view.frame_identity << "; revision=" << view.frame_revision
                << "; scene_generation=" << view.scene_generation
                << "; masks=" << view.ordinary_masks << ',' << view.active_masks
                << "; ordinary_rgba=" << view.ordinary_color[0] << ',' << view.ordinary_color[1]
                << ',' << view.ordinary_color[2] << ',' << view.ordinary_color[3]
                << "; active_rgba=" << view.active_color[0] << ',' << view.active_color[1]
                << ',' << view.active_color[2] << ',' << view.active_color[3] << std::endl;
    }
    const std::uint64_t submitted = after.frame_submissions - before_.frame_submissions;
    const std::uint64_t preparations = preparation_count_ - step_preparation_baseline_;
    if (after.frame_submissions - preparation_frame_baseline_ > 1u ||
        submitted > preparations || (step_ == Step::Initial && submitted == 0u)) {
      fail("Pane callbacks advanced more than one global renderer frame for one preparation");
      return false;
    }
    if (step_ == Step::HostRejected &&
        (after.error.find("out-of-range triangle index") == std::string::npos || submitted != 0u ||
         after.accepted_scene_generation != before_.accepted_scene_generation ||
         after.accepted_editor_scene_generation != before_.accepted_editor_scene_generation)) {
      fail("A rejected scene published dirty editor state over the prior accepted host frame");
      return false;
    }
    if (step_ == Step::HostRetry &&
        (!after.error.empty() || submitted != 1u ||
         after.accepted_editor_scene_generation != after.accepted_scene_generation)) {
      fail("A corrected host candidate failed to publish scene and editor state together");
      return false;
    }
    if (step_ == Step::Unchanged || step_ == Step::Camera || step_ == Step::Pose ||
        step_ == Step::GridCamera || step_ == Step::GridRetained ||
        step_ == Step::GizmoCamera || step_ == Step::GizmoRetained ||
        step_ == Step::ActivePreviewRetained) {
      if (after.translation.position_packs != before_.translation.position_packs ||
          after.translation.attribute_packs != before_.translation.attribute_packs ||
          after.translation.index_packs != before_.translation.index_packs ||
          after.translation.image_decodes != before_.translation.image_decodes) {
        fail("Unchanged/camera/pose preparation repacked retained geometry or decoded its image");
        return false;
      }
    }
    if (step_ == Step::GridCamera || step_ == Step::GridRetained || step_ == Step::GizmoRetained ||
        step_ == Step::ActivePreviewRetained) {
      if (after.editor_projection.position_packs != before_.editor_projection.position_packs ||
          after.editor_projection.attribute_packs != before_.editor_projection.attribute_packs ||
          after.editor_projection.index_packs != before_.editor_projection.index_packs ||
          after.editor_gpu.vertex_uploads != before_.editor_gpu.vertex_uploads ||
          after.editor_gpu.index_uploads != before_.editor_gpu.index_uploads) {
        fail("Camera/unchanged editor preparation rebuilt retained overlay streams");
        return false;
      }
    }
    if ((step_ == Step::GridRetained || step_ == Step::GizmoRetained ||
         step_ == Step::ActivePreviewRetained) && submitted != 0u) {
      fail("An unchanged editor overlay advanced another shared frame");
      return false;
    }
    if (step_ >= Step::ActivePreviewBase && step_ <= Step::ActivePreviewCancel &&
        after.accepted_editor_scene_generation != after.accepted_scene_generation) {
      fail("Editor overlays did not use the same accepted scene generation as their visible mesh");
      return false;
    }
    if (step_ == Step::ActivePreview &&
        (submitted != 1u || after.accepted_scene_generation == before_.accepted_scene_generation ||
         after.translation.position_packs == before_.translation.position_packs)) {
      fail("The active preview needed a later shared submission instead of its first accepted candidate");
      return false;
    }
#endif
    std::size_t changed = 0u;
    for (std::size_t index = 0; index < captures.size(); ++index) {
      changed += captures[index] != previous_captures_[index];
    }
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    if (step_ == Step::Unchanged && submitted != 0u) {
      fail("An unchanged preparation submitted another shared frame");
      return false;
    }
#endif
    if (step_ == Step::Unchanged && changed != 0u) {
      fail("An unchanged preparation changed pane pixels or submitted another shared frame");
      return false;
    }
    if (step_ == Step::HostRejected && changed != 0u) {
      fail("A rejected host source changed pixels in a previously accepted pane");
      return false;
    }
    if (step_ == Step::HostRetry &&
        (changed == 0u || !has_hue_near(captures[2], 2u, {0.2f, -0.35f, 0.45f}, 2))) {
      fail("The corrected host retry did not display its newly staged cyan silhouette");
      return false;
    }
    if ((step_ == Step::Pose && changed < 2u) || (step_ == Step::Preview && changed < 3u)) {
      fail("API placement/preview changes did not reach independent pane pixels");
      return false;
    }
    if (step_ == Step::ObjectsActivePrimary || step_ == Step::ObjectsActiveOther) {
      std::cout << "object_selection_pixels; step=" << static_cast<int>(step_)
                << "; primary_pink=" << pink_pixels(captures[2], object_rectangle(2u))
                << "; other_pink=" << pink_pixels(captures[2], object_rectangle(2u, true)) << std::endl;
      if (const auto *selected = snapshots_.selected_object_snapshot()) {
        for (const auto &mesh : selected->meshes) {
          std::cout << "object_selection_source; step=" << static_cast<int>(step_)
                    << "; entity=" << mesh.entity << "; selected=" << mesh.selected
                    << "; active=" << mesh.active_selected << std::endl;
        }
      }
    }
    if (!verify_overlay_frame(captures)) return false;
    previous_captures_ = std::move(captures);
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    std::cout << "PASS editor_step=" << static_cast<int>(step_)
              << "; active_views=" << after.active_views
              << "; global_submits=" << submitted << "; preparations=" << preparations
              << "; scene_generation=" << after.accepted_scene_generation
              << "; packs=" << after.translation.position_packs << ','
              << after.translation.attribute_packs << ',' << after.translation.index_packs
              << "; image_decodes=" << after.translation.image_decodes
              << "; editor_position_packs=" << after.editor_projection.position_packs
              << "; editor_attribute_packs=" << after.editor_projection.attribute_packs
              << "; editor_index_packs=" << after.editor_projection.index_packs
              << "; editor_vertex_uploads=" << after.editor_gpu.vertex_uploads
              << "; editor_index_uploads=" << after.editor_gpu.index_uploads
              << "; editor_overlays_pending=" << after.editor_overlays_pending
              << "; all_panes_presented_and_gpu=verified" << std::endl;
#else
    std::cout << "PASS editor_step=" << static_cast<int>(step_)
              << "; backend=FullFilament; bgfx_counters=inapplicable"
              << "; all_panes_presented_and_gpu=verified" << std::endl;
#endif
    return true;
  }

  void next(Step step) {
    step_ = step;
    needs_prepare_ = true;
    requested_generation_ = 0u;
    settle_clock_.restart();
    step_clock_.restart();
  }

  void finish() {
    retire_root();
    timer_.stop();
    if (release_count_ != (face_dimensions_ || lamina_overlap_ || edge_gizmo_ ? 1u : 2u)) {
      fail("The replacement editor root did not release its Vulkan device");
    }
    std::cout << "roots=" << root_count_ << "; releases=" << release_count_
              << "; four_pane_epochs=" << next_generation_
              << "; overlay_parity=" << overlay_parity_ << std::endl;
    application_.exit(error_.empty() ? 0 : 1);
  }

  void advance() {
    if (!error_.empty() || step_clock_.elapsed() > 25000) {
      if (error_.empty()) {
        fail("Timed out waiting for four current presented panes");
      }
      retire_root();
      timer_.stop();
      application_.exit(1);
      return;
    }
    if (step_ == Step::Hidden) {
      if (settle_clock_.elapsed() >= 150) {
        root_->show();
        next(Step::Reshow);
      }
      return;
    }
    if (!initialized_ && !initialize_host()) {
      return;
    }
    if (needs_prepare_) {
      if (settle_clock_.elapsed() < 100) {
        return;
      }
      if (requested_generation_ == 0u) {
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
        if (!diagnostics(before_)) {
          return;
        }
#endif
        step_preparation_baseline_ = preparation_count_;
      }
      requested_generation_ = ++next_generation_;
      needs_prepare_ = false;
      prepare_frame();
      return;
    }
    if (requested_generation_ == 0u ||
        !std::ranges::all_of(submitted_generations_, [this](std::uint64_t value) {
          return value == requested_generation_;
        }) || !presentation_clock_.isValid() || presentation_clock_.elapsed() < 100) {
      return;
    }
    if (edge_gizmo_) {
      if (verify_edge_gizmo()) advance_edge_gizmo();
      return;
    }
    if (lamina_overlap_) {
      if (verify_lamina_overlap()) advance_lamina_overlap();
      return;
    }
    if (face_dimensions_) {
      if (verify_face_dimensions()) advance_face_dimensions();
      return;
    }
    if (!verify_frame()) {
      return;
    }
    switch (step_) {
    case Step::Initial: next(Step::Unchanged); break;
    case Step::Unchanged:
      if (accept(editor_.scene().set_transform(object_.entity, {.location = {0.8f, 0, 0}}))) {
        next(Step::Pose);
      }
      break;
    case Step::Pose:
      if (accept(editor_.mode().set(QuaderEditorMode::Edit)) &&
          accept(editor_.selection().set_mode(QuaderSelectionMode::Vertex)) &&
          select_preview_box_vertices() &&
          accept(editor_.modeling().begin_transform_preview()) &&
          accept(editor_.modeling().update_transform_translate(kPreviewDelta))) {
        next(Step::Preview);
      }
      break;
    case Step::Preview:
      if (accept(editor_.modeling().cancel_transform_preview())) {
        next(Step::Cancel);
      }
      break;
    case Step::Cancel:
      if (!session_.viewport_workspace().camera(ViewportPaneId::Perspective).restore_perspective_pose(
              {.eye = {5, 3, 5}, .target = {0.3f, 0.5f, 0}, .up = {0, 1, 0}})) {
        fail("The canonical camera rejected the probe pose");
        break;
      }
      next(Step::Camera);
      break;
    case Step::Camera:
      {
        const QSize previous_size = root_->size();
        fit_root_to_screen({1060, 740});
        if (root_->size() == previous_size) {
          fail("The available desktop did not admit a real four-pane resize");
          break;
        }
      }
      next(Step::Resize);
      break;
    case Step::Resize:
      root_->hide();
      step_ = Step::Hidden;
      settle_clock_.restart();
      step_clock_.restart();
      break;
    case Step::Reshow:
      retire_root();
      if (release_count_ != 1u) {
        fail("The first editor root did not release its Vulkan device");
        break;
      }
      next(Step::Replaced);
      create_root();
      break;
    case Step::Replaced:
      if (overlay_parity_) {
        if (begin_overlay_fixture()) next(Step::ObjectSingleton);
      } else {
        finish();
      }
      break;
    case Step::ObjectSingleton:
      settings_.set_bool_value(ViewportBoolSettingId::ShowCollisions, false);
      apply_settings();
      next(Step::CollisionOff);
      break;
    case Step::CollisionOff:
      settings_.set_bool_value(ViewportBoolSettingId::ShowCollisions, true);
      apply_settings();
      next(Step::CollisionRestored);
      break;
    case Step::CollisionRestored: {
      auto maximum_opacity = collision_color_;
      maximum_opacity.a = 1.0f;
      (void)settings_.set_color(ViewportColorSettingId::CollisionFill, maximum_opacity);
      apply_settings();
      next(Step::CollisionMaxOpacity);
      break;
    }
    case Step::CollisionMaxOpacity:
      (void)settings_.set_color(ViewportColorSettingId::CollisionFill, collision_color_);
      apply_settings();
      next(Step::CollisionOpacityRestored);
      break;
    case Step::CollisionOpacityRestored:
      (void)settings_.set_color(ViewportColorSettingId::ActiveSelection, {0, 1, 1, 1});
      (void)settings_.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, 7.0f);
      apply_settings();
      next(Step::ObjectSingletonCustom);
      break;
    case Step::ObjectSingletonCustom:
      (void)settings_.set_color(ViewportColorSettingId::ActiveSelection, {1, 105.0f / 255.0f, 180.0f / 255.0f, 1});
      (void)settings_.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, 2.0f);
      apply_settings();
      if (accept(editor_.selection().select_objects({object_.entity, other_object_.entity}, object_.entity))) {
        next(Step::ObjectsActivePrimary);
      }
      break;
    case Step::ObjectsActivePrimary:
      if (accept(editor_.selection().select_objects({object_.entity, other_object_.entity}, other_object_.entity))) {
        next(Step::ObjectsActiveOther);
      }
      break;
    case Step::ObjectsActiveOther:
      if (begin_face_fixture()) next(Step::FacesBase);
      break;
    case Step::FacesBase:
      if (accept(editor_.selection().select_components({
              .object = component_object_, .kind = QuaderComponentKind::Face,
              .faces = {component_faces_[0], component_faces_[1]},
              .active = {.object = component_object_, .kind = QuaderComponentKind::Face,
                         .face = component_faces_[0]}}))) {
        next(Step::FacesSelected);
      }
      break;
    case Step::FacesSelected:
      if (accept(editor_.selection().hover_face(component_object_, component_faces_[1]))) {
        next(Step::FacesHovered);
      }
      break;
    case Step::FacesHovered:
      if (accept(editor_.selection().clear_hover())) next(Step::FacesHoverCleared);
      break;
    case Step::FacesHoverCleared:
      if (accept(editor_.selection().set_mode(QuaderSelectionMode::Edge)) &&
          accept(editor_.selection().select_components({
              .object = component_object_, .kind = QuaderComponentKind::Edge,
              .edges = {{component_vertices_[0], component_vertices_[1]},
                        {component_vertices_[3], component_vertices_[2]}},
              .active = {.object = component_object_, .kind = QuaderComponentKind::Edge,
                         .edge_a = component_vertices_[0], .edge_b = component_vertices_[1]}}))) {
        next(Step::Edges);
      }
      break;
    case Step::Edges:
      if (accept(editor_.selection().set_mode(QuaderSelectionMode::Vertex)) &&
          accept(editor_.selection().select_components({
              .object = component_object_, .kind = QuaderComponentKind::Vertex,
              .vertices = {component_vertices_[0], component_vertices_[1]},
              .active = {.object = component_object_, .kind = QuaderComponentKind::Vertex,
                         .vertex = component_vertices_[0]}}))) {
        next(Step::Vertices);
      }
      break;
    case Step::Vertices:
      if (accept(editor_.selection().hover_vertex(component_object_, component_vertices_[1]))) {
        next(Step::VertexHovered);
      }
      break;
    case Step::VertexHovered:
      if (accept(editor_.selection().clear_hover())) next(Step::ActivePreviewBase);
      break;
    case Step::ActivePreviewBase:
      if (accept(editor_.modeling().begin_transform_preview()) &&
          accept(editor_.modeling().update_transform_translate({0.2f, 0.2f, 0}))) {
        next(Step::ActivePreview);
      }
      break;
    case Step::ActivePreview: next(Step::ActivePreviewRetained); break;
    case Step::ActivePreviewRetained:
      if (accept(editor_.modeling().cancel_transform_preview())) next(Step::ActivePreviewCancel);
      break;
    case Step::ActivePreviewCancel:
      if (
          accept(editor_.mode().set(QuaderEditorMode::Object)) &&
          accept(editor_.selection().clear())) {
        (void)settings_.set_color(ViewportColorSettingId::GridMinor, {0, 0.55f, 0, 1});
        (void)settings_.set_color(ViewportColorSettingId::GridMajor, {0, 0.7f, 0.7f, 1});
        apply_settings();
        next(Step::GridBase);
      }
      break;
    case Step::GridBase:
      settings_.set_bool_value(ViewportBoolSettingId::ShowGrid, true);
      apply_settings();
      next(Step::Grid);
      break;
    case Step::Grid:
      if (!session_.viewport_workspace().camera(ViewportPaneId::Perspective).restore_perspective_pose(
              {.eye = {4, 4, 6}, .target = {0, 0, 0}, .up = {0, 1, 0}})) {
        fail("The grid camera rejected the requested fixture pose");
      } else {
        next(Step::GridCamera);
      }
      break;
    case Step::GridCamera: next(Step::GridRetained); break;
    case Step::GridRetained:
      settings_.set_bool_value(ViewportBoolSettingId::ShowGrid, false);
      apply_settings();
      if (set_shading(QuaderShadingMode::Solid)) next(Step::Solid);
      break;
    case Step::Solid:
      settings_.set_bool_value(ViewportBoolSettingId::WireframeOnShaded, true);
      (void)settings_.set_color(ViewportColorSettingId::WireframeOnShaded, {0, 1, 1, 1});
      apply_settings();
      if (set_shading(QuaderShadingMode::Shaded)) next(Step::ShadedWire);
      break;
    case Step::ShadedWire:
      settings_.set_bool_value(ViewportBoolSettingId::WireframeOnShaded, false);
      apply_settings();
      if (set_shading(QuaderShadingMode::Wireframe)) next(Step::Wireframe);
      break;
    case Step::Wireframe:
      settings_.set_bool_value(ViewportBoolSettingId::ShowGizmos, true);
      apply_settings();
      if (set_shading(QuaderShadingMode::Shaded) &&
          accept(editor_.selection().select_object(object_)) &&
          accept(editor_.tools().activate(ViewportToolId::Move))) {
        next(Step::Gizmo);
      }
      break;
    case Step::Gizmo:
      if (!session_.viewport_workspace().camera(ViewportPaneId::Perspective).restore_perspective_pose(
              {.eye = {5, 2.5f, 4}, .target = {0.8f, 0, 0}, .up = {0, 1, 0}})) {
        fail("The gizmo camera rejected the requested fixture pose");
      } else {
        next(Step::GizmoCamera);
      }
      break;
    case Step::GizmoCamera: next(Step::GizmoRetained); break;
    case Step::GizmoRetained:
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
      if (prepare_host_rejection()) next(Step::HostRejected);
#else
      finish();
#endif
      break;
    case Step::HostRejected: next(Step::HostRetry); break;
    case Step::HostRetry: finish(); break;
    case Step::Hidden: break;
    }
  }

  QApplication &application_;
  QString output_directory_;
  std::filesystem::path resources_;
  QString reference_directory_;
  bool overlay_parity_ = false;
  bool face_dimensions_ = false;
  bool lamina_overlap_ = false;
  bool instance_preview_ = false;
  bool edge_gizmo_ = false;
  unsigned int edge_gizmo_phase_ = 0u;
  QVec3 edge_gizmo_origin_;
  static constexpr QVec3 kEdgeGizmoDelta{2.0f, -2.0f, 0.0f};
  std::array<QImage, 4> edge_gizmo_previous_;
  static constexpr QVec3 kInstanceSiblingOffset{-1.7f, 1.7f, 0};
  SceneSession session_;
  QuaderEditorApi editor_;
  QuaderModelingObjectRef object_;
  QuaderModelingObjectRef other_object_, component_object_;
  std::vector<QuaderElementId> component_faces_, component_vertices_;
  std::unique_ptr<RenderHost> host_;
  std::unique_ptr<RenderSceneSink> sink_;
  ProbeSceneSink *recorded_sink_ = nullptr;
  QuaderElementId face_dimension_face_ = 0;
  unsigned int face_dimension_phase_ = 0u;
  float face_dimension_delta_ = 0.0f;
  QuaderModelingObjectRef lamina_peer_;
  QuaderModelingObjectRef lamina_duplicate_;
  unsigned int lamina_phase_ = 0u;
  bool lamina_expected_ = true;
  std::array<QImage, 4> lamina_previous_;
  std::array<QImage, 4> lamina_closed_base_;
  std::array<QImage, 4> lamina_toggle_baseline_;
  std::array<std::array<QColor, 2>, 4> lamina_previous_fill_colors_;
  std::array<std::size_t, 4> lamina_previous_triangles_{};
  std::array<int, 4> lamina_previous_wire_pixels_{};
  std::array<int, 4> lamina_previous_wire_contrast_{};
  RenderOverlaySnapshotCache snapshots_;
  ViewportSettings settings_;
  ViewportSettingColor collision_color_;
  std::unique_ptr<QWidget> root_;
  QtViewportPanel *panel_ = nullptr;
  QtVulkanCompositionWidget *anchor_ = nullptr;
  std::array<QtVulkanCompositionWidget *, 4> pane_widgets_{};
  std::array<std::uint64_t, 4> submitted_generations_{};
  std::array<QImage, 4> previous_captures_;
  std::array<QImage, 4> collision_on_captures_;
  std::array<QImage, 4> active_primary_captures_, face_base_captures_;
  std::array<QImage, 4> face_selected_captures_, grid_base_captures_;
  std::array<QImage, 4> active_preview_base_captures_;
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
  CrimsonRenderDiagnostics before_;
  RenderSceneSnapshot host_rejected_scene_, host_retry_scene_;
  bool host_retry_staged_ = false;
#endif
  QTimer timer_;
  QElapsedTimer step_clock_, settle_clock_, presentation_clock_;
  std::uint64_t requested_generation_ = 0u, next_generation_ = 0u;
  std::uint64_t preparation_count_ = 0u, step_preparation_baseline_ = 0u;
  std::uint64_t preparation_frame_baseline_ = 0u;
  std::uint64_t settings_revision_ = 0u;
  std::uint32_t root_count_ = 0u, release_count_ = 0u;
  Step step_ = Step::Initial;
  bool initialized_ = false, preparing_ = false, needs_prepare_ = true;
  std::string error_;
};

} // namespace
} // namespace quader

int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QCommandLineParser parser;
  parser.addHelpOption();
  const QCommandLineOption output_option(QStringLiteral("output-dir"),
      QStringLiteral("Editor GPU evidence directory."), QStringLiteral("directory"));
  const QCommandLineOption resource_option(QStringLiteral("resource-dir"),
      QStringLiteral("Native runtime directory containing packaged materials."), QStringLiteral("directory"));
  const QCommandLineOption overlay_option(QStringLiteral("overlay-parity"),
      QStringLiteral("Exercise public selection, hover, grid, wire and gizmo GPU contracts."));
  const QCommandLineOption reference_option(QStringLiteral("reference-dir"),
      QStringLiteral("Compare overlay color coverage with the same Full reference captures."), QStringLiteral("directory"));
  const QCommandLineOption face_dimensions_option(QStringLiteral("face-dimensions"),
      QStringLiteral("Exercise continuously visible dimensions while moving one closed-box face."));
  const QCommandLineOption lamina_option(QStringLiteral("lamina-overlap"),
      QStringLiteral("Exercise persistent diagnostic fill and wire on opposing coplanar faces."));
  const QCommandLineOption instance_option(QStringLiteral("instance-preview"),
      QStringLiteral("Convert a public mesh into linked occurrences and edit their shared face preview."));
  const QCommandLineOption edge_gizmo_option(QStringLiteral("edge-gizmo"),
      QStringLiteral("Verify a native edge Move gizmo after untouched Quick Pivot, commit and history."));
  parser.addOption(output_option);
  parser.addOption(resource_option);
  parser.addOption(overlay_option);
  parser.addOption(reference_option);
  parser.addOption(face_dimensions_option);
  parser.addOption(lamina_option);
  parser.addOption(instance_option);
  parser.addOption(edge_gizmo_option);
  parser.process(application);
  if (parser.value(output_option).isEmpty() || parser.value(resource_option).isEmpty()) {
    std::cerr << "--output-dir and --resource-dir are required\n";
    return 2;
  }
  quader::EditorCompositionProbe probe(application, parser.value(output_option),
      std::filesystem::path(parser.value(resource_option).toStdWString()),
      parser.isSet(overlay_option), parser.value(reference_option), parser.isSet(face_dimensions_option),
      parser.isSet(lamina_option), parser.isSet(instance_option), parser.isSet(edge_gizmo_option));
  return probe.run();
}
