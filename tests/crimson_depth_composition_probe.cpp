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
#include "editor/session/scene_session.hpp"
#include "render/render_host.hpp"
#include "render/render_layers.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
#include "render/crimson/crimson_render_host.hpp"
#endif
#include "ui/qt_vulkan_composition_widget.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <quader/modeling/neutral/polygon/document_model.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

/** Exercises physical-pixel wire continuity and real foreground depth on one borrowed Qt device. */
class DepthCompositionProbe final : public ViewportOverlaySelectionFrameProvider {
public:
  DepthCompositionProbe(QApplication &application, QString output_directory,
                        std::filesystem::path resources)
      : application_(application), output_directory_(std::move(output_directory)),
        resources_(std::move(resources)),
        editor_(QuaderApiSessionInternalAccess::from_scene_session(session_)),
#if defined(QUADER_FILAMENT_REFERENCE_PROBE)
        host_(create_app_render_host()) {
#else
        host_(create_crimson_render_host()) {
#endif
    timer_.setInterval(40);
    QObject::connect(&timer_, &QTimer::timeout, &application_, [this] { advance(); });
  }

  ~DepthCompositionProbe() { retire(); }

  ViewportOverlayFrame build_selection_overlay_frame() const override { return source_frame_; }

  int run() {
    if (!host_ || !QDir().mkpath(output_directory_)) return 1;
    appearance_ = ViewportSettings::defaults();
    (void)appearance_.set_color(ViewportColorSettingId::Background, {0, 0, 0, 1});
    (void)appearance_.set_color(ViewportColorSettingId::WireframeOnShaded, {1, 1, 1, 1});
    (void)appearance_.set_float_value(ViewportFloatSettingId::ShadedKeyIntensity, 0.0f);
    (void)appearance_.set_float_value(ViewportFloatSettingId::ShadedGroundFill, 0.1f);
    (void)appearance_.set_float_value(ViewportFloatSettingId::ShadedSkyFill, 0.1f);
    appearance_.set_bool_value(ViewportBoolSettingId::ShowGrid, false);
    appearance_.set_bool_value(ViewportBoolSettingId::ShowMeshGrid, false);
    appearance_.set_bool_value(ViewportBoolSettingId::ShowGizmos, false);
    root_ = std::make_unique<QWidget>();
    root_->setWindowTitle(QStringLiteral("Quader %1 physical wire depth gate")
        .arg(QString::fromLatin1(kBackendName)));
    root_->setWindowFlag(Qt::WindowStaysOnTopHint);
    root_->resize(kWidth, kHeight);
    if (const auto *screen = application_.primaryScreen()) {
      root_->move(screen->availableGeometry().center() - root_->rect().center());
    }
    auto *layout = new QVBoxLayout(root_.get());
    layout->setContentsMargins(0, 0, 0, 0);
    widget_ = new QtVulkanCompositionWidget(root_.get());
    widget_->setFixedColorBufferSize({kWidth, kHeight});
    layout->addWidget(widget_);
    widget_->set_render_callback([this](const RenderVulkanCompositionTarget &target) {
      if (!initialized_) return false;
      if (target.width != kWidth || target.height != kHeight ||
          !host_->render_composited_viewport(*view_, *surface_, target)) {
        const auto capacity = surface_->capacity();
        std::cerr << "target=" << target.width << 'x' << target.height
                  << "; view=" << view_->width() << 'x' << view_->height()
                  << "; capacity=" << capacity.width << 'x' << capacity.height
                  << "; surface_valid=" << surface_->valid()
                  << "; initialized=" << host_->initialized()
                  << "; composited=" << host_->uses_composited_vulkan()
                  << "; image=" << target.image << "; old_layout=" << target.old_layout << std::endl;
        fail("The fixed physical composition target could not be rendered");
        return false;
      }
      rendered_generation_ = prepared_generation_;
      return true;
    });
    widget_->set_release_callback([this] {
      if (host_->initialized()) qFatal("Depth probe released Qt's device before its renderer drained");
      ++release_count_;
    });
    QObject::connect(widget_, &QRhiWidget::frameSubmitted, widget_, [this] {
      if (initialized_) host_->complete_composited_presentation_frame();
      presented_generation_ = rendered_generation_;
      presentation_clock_.restart();
    });
    root_->show();
    step_clock_.start();
    timer_.start();
    return application_.exec();
  }

private:
  /** Each wire-on capture has its own wire-off source-depth baseline. */
  enum class Step {
    ObliqueOff, ObliqueOn, ObliqueRetained, ObliqueForegroundOff, ObliqueForegroundOn,
    GrazingOff, GrazingOn, GrazingForegroundOff, GrazingForegroundOn,
    SourceOff, SourceOn, SourceRetained
  };

  static constexpr int kWidth = 640;
  static constexpr int kHeight = 480;
#if defined(QUADER_FILAMENT_REFERENCE_PROBE)
  static constexpr const char *kBackendName = "FullFilament";
#else
  static constexpr const char *kBackendName = "Crimson";
#endif

  void fail(std::string_view message) {
    if (error_.empty()) {
      error_ = message;
      std::cerr << "FAIL depth_projection=" << projection_index_ << "; step="
                << static_cast<int>(step_) << ": " << error_ << std::endl;
    }
  }

  bool accept(const QuaderApiStatus &status) {
    if (status.ok()) return true;
    fail(status.error_message());
    return false;
  }

  bool initialize() {
    const auto *context = widget_->shared_context();
    if (context == nullptr) return false;
    if (!host_->initialize(nullptr, resources_, {.backend = RendererBackend::Vulkan}, context)) {
      fail("The depth host could not borrow Qt's Vulkan device");
      return false;
    }
    sink_ = host_->create_scene_sink(resources_);
    // Match QtViewportSurface: render into the host's composed sRGB surface.
    // Texture mode owns a separate target and cannot signal this surface's copy.
    view_ = host_->create_view(viewport_grid_layer_mask(0u), ViewportRenderSurface::SrgbSwapchain);
    surface_ = host_->create_composited_surface(kWidth, kHeight);
    if (!sink_ || !view_ || !surface_ || !surface_->valid()) {
      fail("The depth host could not create its view, sink or retained surface");
      return false;
    }
    sink_->set_backface_culling_enabled(true);
    sink_->set_active_overlay_viewports(1u);
    view_->set_active(true);
    view_->resize(kWidth, kHeight);
    view_->set_presentation_bounds(kWidth, kHeight, 0u, 0u);
    view_->set_overlay_visible(true);
    view_->set_scene_visible(true);
    view_->set_scene_layer_mask(kViewportShadedSceneLayerMask);
    view_->set_shadowing_enabled(false);
    view_->set_material_presentation(ViewportMaterialDebugMode::None, false, false);
    host_->bind_view_surface(*view_, *surface_);
    widget_->set_render_system(host_.get());
    initialized_ = true;
    configure_camera();
    return create_oblique();
  }

  void configure_camera() {
    camera_.set_viewport_size(kWidth, kHeight);
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.focus_origin(5.0f);
    if (projection_index_ == 0u && !camera_.restore_perspective_pose(
            {.eye = {0, 0, 5}, .target = {}, .up = {0, 1, 0}})) {
      fail("The physical depth camera rejected its canonical perspective pose");
    }
    camera_.set_clip_planes(0.001f, 200.0f);
  }

  QVec3 world_point(QVec2 pixel, float camera_depth) const {
    // QRhiWidget's native framebuffer has top-left image coordinates. Derive
    // geometry from that same camera; the legacy harness's readback Y flip
    // does not belong in this composition route.
    const auto ray = camera_.ray_from_viewport_point(pixel);
    const float world_z = 5.0f - camera_depth;
    return ray.origin + ray.direction * ((world_z - ray.origin.z) / ray.direction.z);
  }

  bool clear_scene() {
    for (const auto &object : objects_) {
      if (!accept(editor_.scene().destroy(object.entity))) return false;
    }
    objects_.clear();
    source_frame_ = {};
    ++source_revision_;
    return true;
  }

  bool create_mesh(std::span<const QVec3> positions,
                   const std::vector<std::vector<std::size_t>> &faces,
                   std::string name, bool shaded_wire = true) {
    quader_poly::Document document;
    std::vector<quader_poly::ElementId> vertices;
    for (const auto position : positions) vertices.push_back(quader_poly::add_vertex(document, position));
    for (const auto &face : faces) {
      std::vector<quader_poly::ElementId> polygon;
      // Top-left image order is clockwise toward +Z; reverse it to create
      // front-facing source geometry without changing projected edge spans.
      for (auto index = face.rbegin(); index != face.rend(); ++index) polygon.push_back(vertices[*index]);
      (void)quader_poly::add_face(document, polygon);
    }
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)), {.name = std::move(name)});
    if (!accept(created)) return false;
    objects_.push_back(created.value());
    return accept(editor_.scene().set_mesh_component(created.value().entity,
        {.mesh = created.value().mesh, .wireframe_on_shaded = shaded_wire})) &&
        accept(editor_.selection().clear());
  }

  bool create_quad(const std::array<QVec2, 4> &pixels, const std::array<float, 4> &depths,
                   std::string name, bool shaded_wire = true) {
    std::array<QVec3, 4> positions;
    for (std::size_t index = 0; index < pixels.size(); ++index) positions[index] = world_point(pixels[index], depths[index]);
    return create_mesh(positions, {{0u, 1u, 2u, 3u}}, std::move(name), shaded_wire);
  }

  bool create_oblique() {
    if (!clear_scene()) return false;
    constexpr float kDepthScale = 6.0f;
    for (std::size_t panel = 0; panel < 2u; ++panel) {
      const float left = panel == 0u ? 125.25f : 335.25f;
      const float right = panel == 0u ? 265.25f : 605.25f;
      const float center = panel == 0u ? 190.25f : 450.25f;
      const std::array<QVec2, 6> pixels{{{left, 80}, kEdges[panel][0], {right, 80},
          {right, 400}, kEdges[panel][1], {left, 400}}};
      std::array<QVec3, 6> positions;
      for (std::size_t index = 0; index < pixels.size(); ++index) {
        const auto pixel = pixels[index];
        const float along = (pixel.y - 80.0f) / 320.0f;
        const float depth = projection_index_ == 0u
            ? 1.0f / ((1.0f - along) / (5.0f * kDepthScale) + along / (25.0f * kDepthScale) +
                (pixel.x - center) * (panel == 0u ? 0.00012f : 0.00006f) / kDepthScale)
            : kDepthScale * (5.0f + 20.0f * along - 0.03f * (pixel.x - center));
        positions[index] = world_point(pixel, depth);
      }
      if (!create_mesh(positions, {{0u, 1u, 4u, 5u}, {1u, 2u, 3u, 4u}}, "Oblique physical shaded wire")) return false;
    }
    return true;
  }

  bool create_grazing() {
    return clear_scene() && create_quad(
        {{{320.625f, 100}, {320.657f, 100}, {320.657f, 380}, {320.625f, 380}}},
        {{30, 90, 90, 30}}, "Subpixel physical grazing wire");
  }

  bool create_source() {
    return clear_scene() && create_quad(
        {{{80, 140}, {180, 140}, {180, 340}, {80, 340}}},
        {{5, 150, 150, 5}}, "Sloped component source-wire depth", false);
  }

  void enable_source_lines() {
    source_frame_ = {};
    for (int index = 0; index < 2; ++index) {
      const float y = index == 0 ? 200.0f : 290.0f;
      const float offset = index == 0 ? 0.0f : 0.1f;
      source_frame_.lines.push_back({
          .layer = ViewportOverlayLayer::SourceWire,
          .start = world_point({80, y}, 5.0f + offset),
          .end = world_point({180, y}, 150.0f + offset),
          .color = rgba8(40, 225, 255), .width_pixels = 2.5f,
          .style_role = ViewportOverlayStyleRole::ExplicitColor,
          .source = ViewportOverlaySource::ComponentSelection,
          .element = {.object_id = 1, .edge_a = index * 2, .edge_b = index * 2 + 1}});
    }
    ++source_revision_;
  }

  bool wires_enabled() const {
    return step_ == Step::ObliqueOn || step_ == Step::ObliqueRetained ||
        step_ == Step::ObliqueForegroundOn || step_ == Step::GrazingOn ||
        step_ == Step::GrazingForegroundOn;
  }

  void prepare() {
    host_->begin_shared_command_batch();
    host_->begin_view_preparation();
    {
      // This direct facade fixture uses binding0 for both scene and overlays.
      // Mixing API-bound scene1 with an empty selected snapshot would retire
      // Full's scene cache before rebuilding only its topology wire proxies.
      sink_->sync(session_.scene(), session_.assets());
      sink_->update_mesh_surface_grid(1.0f, appearance_);
      sink_->sync_scene_asset_overlays({}, build_render_overlay_scene_wireframe_snapshot(
          session_.scene(), session_.assets(), 0u,
          ViewportOverlayStyleResolver::persistent_style_snapshot(appearance_),
          {}, nullptr, false, {}, false, wires_enabled() ? viewport_grid_layer_mask(0u) : 0u));
      (void)sink_->submit_viewport_overlay({
          .layer_mask = viewport_grid_layer_mask(0u),
          .camera = ViewportCameraAdapter::overlay_camera(camera_, {kWidth, kHeight}),
          .selection_source = {.source_identity = 1u, .selection_revision = source_revision_},
          .selection_frame_provider = this,
          .scene_selection_mode = RenderOverlaySelectionMode::Component,
          .style_selection_context = ViewportOverlaySelectionContext::Vertex,
          .overlays_visible = true, .selection_dimensions_visible = false}, appearance_);
      view_->update_camera(camera_);
      view_->apply_scene_settings(session_.scene().scene_settings(), false, appearance_);
      view_->update_ground_grid(camera_, 1.0f, appearance_, false);
      prepared_generation_ = requested_generation_;
    }
    host_->complete_view_preparation();
    host_->begin_presentation_frame();
    host_->end_shared_command_batch();
    needs_prepare_ = false;
    widget_->update();
  }

  bool verify_continuity(const QImage &image, const QImage &baseline, bool occluded) {
    for (std::size_t edge = 0; edge < kEdges.size(); ++edge) {
      int missing = 0;
      for (int y = 88; y <= 392; ++y) {
        if (occluded && y >= 195 && y <= 285) continue;
        const float fraction = (static_cast<float>(y) + 0.5f - 80.0f) / 320.0f;
        const float center = kEdges[edge][0].x + (kEdges[edge][1].x - kEdges[edge][0].x) * fraction;
        int strongest = 0;
        for (int x = static_cast<int>(std::floor(center)) - 2;
             x <= static_cast<int>(std::floor(center)) + 2; ++x) {
          const auto pixel = image.pixelColor(x, y), base = baseline.pixelColor(x, y);
          strongest = std::max(strongest, std::min({pixel.red() - base.red(),
              pixel.green() - base.green(), pixel.blue() - base.blue()}));
        }
        missing += strongest <= 60;
      }
      if (missing != 0) {
        std::cerr << "edge=" << edge << "; missing_white_core_rows=" << missing << std::endl;
        fail("White shaded wire lost an interior scanline on an oblique source surface");
        return false;
      }
    }
    return true;
  }

  int cyan_pixels(const QImage &image, QVec2 center) const {
    int count = 0;
    for (int y = qRound(center.y) - 2; y <= qRound(center.y) + 2; ++y) {
      for (int x = qRound(center.x) - 2; x <= qRound(center.x) + 2; ++x) {
        if (!image.rect().contains(x, y)) continue;
        const auto color = image.pixelColor(x, y);
        count += color.green() > color.red() + 40 && color.blue() > color.red() + 40;
      }
    }
    return count;
  }

  bool verify_source(const QImage &image) {
    for (const float projected : {0.1f, 0.25f, 0.5f, 0.7f, 0.9f}) {
      const float fraction = projection_index_ == 0u
          ? projected * 5.0f / ((1.0f - projected) * 150.0f + projected * 5.0f) : projected;
      for (std::size_t index = 0; index < source_frame_.lines.size(); ++index) {
        const auto &line = source_frame_.lines[index];
        const auto position = camera_.project_world_point(line.start + (line.end - line.start) * fraction).position;
        const int count = cyan_pixels(image, position);
        if ((index == 0u && count < 3) || (index == 1u && count != 0)) {
          std::cerr << "fraction=" << projected << "; line=" << index << "; cyan_pixels=" << count << std::endl;
          fail("Sloped source-wire cap depth hid a coplanar span or exposed a truly hidden span");
          return false;
        }
      }
    }
    return true;
  }

  bool verify_contract(const QImage &image) {
    if ((step_ == Step::ObliqueOff &&
         (image.pixelColor(193, 240).lightness() <= 20 || image.pixelColor(470, 240).lightness() <= 20)) ||
        ((step_ == Step::ObliqueForegroundOff || step_ == Step::GrazingForegroundOff) &&
         image.pixelColor(320, 240).lightness() <= 20) ||
        (step_ == Step::SourceOff && image.pixelColor(130, 240).lightness() <= 20)) {
      fail("The depth baseline did not render its source or foreground face");
      return false;
    }
    if (step_ == Step::ObliqueOff || step_ == Step::GrazingOff || step_ == Step::SourceOff) baseline_ = image;
    if (step_ == Step::ObliqueForegroundOff || step_ == Step::GrazingForegroundOff) foreground_baseline_ = image;
    if (step_ == Step::ObliqueOn || step_ == Step::ObliqueRetained) {
      if (!verify_continuity(image, baseline_, false)) return false;
      if (image.copy(QRect(153, 225, 13, 30)) != baseline_.copy(QRect(153, 225, 13, 30))) {
        fail("An internal triangulation diagonal leaked into authored topology wire pixels");
        return false;
      }
    }
    if (step_ == Step::ObliqueForegroundOn) {
      if (!verify_continuity(image, foreground_baseline_, true)) return false;
      if (image.copy(QRect(80, 215, 525, 50)) != foreground_baseline_.copy(QRect(80, 215, 525, 50))) {
        fail("Oblique shaded wire crossed a real foreground face");
        return false;
      }
    }
    if (step_ == Step::GrazingOn) {
      int brighter = 0;
      for (int y = 108; y <= 372; ++y) {
        for (int x = 318; x <= 323; ++x) {
          brighter += image.pixelColor(x, y).lightness() - baseline_.pixelColor(x, y).lightness() > 12;
        }
      }
      if (brighter <= 100) {
        fail("The unoccluded subpixel grazing fixture did not render its real white wire");
        return false;
      }
    }
    if (step_ == Step::GrazingForegroundOn && image != foreground_baseline_) {
      fail("A ribbon from a source at depth30..90 escaped a depth10 foreground face");
      return false;
    }
    if ((step_ == Step::SourceOn || step_ == Step::SourceRetained) && !verify_source(image)) return false;
    if ((step_ == Step::ObliqueRetained || step_ == Step::SourceRetained) && image != previous_) {
      fail("An unchanged wire frame changed retained GPU pixels");
      return false;
    }
    return true;
  }

  bool capture() {
    QScreen *screen = root_->windowHandle()->screen();
    const QPoint origin = widget_->mapToGlobal(QPoint{}) - screen->geometry().topLeft();
    const QImage presented = screen->grabWindow(0, origin.x(), origin.y(), widget_->width(), widget_->height()).toImage();
    const QImage image = widget_->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
    const QString stem = QStringLiteral("depth-%1-%2-").arg(projection_index_).arg(static_cast<int>(step_));
    if (image.size() != QSize(kWidth, kHeight) || presented.isNull() ||
        !image.save(QDir(output_directory_).filePath(stem + QStringLiteral("offscreen.png"))) ||
        !presented.save(QDir(output_directory_).filePath(stem + QStringLiteral("presented.png")))) {
      fail("Cannot capture the exact640x480 physical GPU target and its presentation");
      return false;
    }
    for (int y = 24; y < kHeight - 24; y += 23) {
      for (int x = 24; x < kWidth - 24; x += 31) {
        const auto expected = image.pixelColor(x, y);
        const auto actual = presented.pixelColor(x * presented.width() / kWidth, y * presented.height() / kHeight);
        if (std::abs(expected.red() - actual.red()) > 6 || std::abs(expected.green() - actual.green()) > 6 ||
            std::abs(expected.blue() - actual.blue()) > 6) {
          fail("The presented depth fixture disagreed with its actual GPU target");
          return false;
        }
      }
    }
    if (!verify_contract(image)) return false;
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    CrimsonRenderDiagnostics current;
    if (!crimson_render_diagnostics(*host_, current) || !current.error.empty()) {
      fail(current.error.empty() ? "The depth facade lost its Crimson diagnostics" : current.error);
      return false;
    }
    if (step_ == Step::ObliqueRetained || step_ == Step::SourceRetained) {
      if (current.translation.position_packs != retained_.translation.position_packs ||
          current.translation.attribute_packs != retained_.translation.attribute_packs ||
          current.translation.index_packs != retained_.translation.index_packs ||
          current.editor_projection.position_packs != retained_.editor_projection.position_packs ||
          current.editor_projection.attribute_packs != retained_.editor_projection.attribute_packs ||
          current.editor_projection.index_packs != retained_.editor_projection.index_packs ||
          current.editor_gpu.vertex_uploads != retained_.editor_gpu.vertex_uploads ||
          current.editor_gpu.index_uploads != retained_.editor_gpu.index_uploads) {
        fail("An unchanged wire fixture rebuilt retained source or editor GPU streams");
        return false;
      }
    }
    retained_ = current;
#endif
    previous_ = image;
    std::cout << "PASS depth_projection=" << projection_index_ << "; step=" << static_cast<int>(step_)
              << "; backend=" << kBackendName << "; target=640x480; presented_and_gpu=verified" << std::endl;
    return true;
  }

  void next(Step step) {
    step_ = step;
    ++requested_generation_;
    needs_prepare_ = true;
    step_clock_.restart();
  }

  void retire() {
    if (!root_) return;
    initialized_ = false;
    if (view_) { view_->shutdown(); view_.reset(); }
    if (surface_) { surface_->reset(); surface_.reset(); }
    if (sink_) { sink_->shutdown(); sink_.reset(); }
    host_->shutdown();
    widget_->set_render_system(nullptr);
    root_.reset();
    widget_ = nullptr;
  }

  void advance() {
    if (!error_.empty() || step_clock_.elapsed() > 25000) {
      if (error_.empty()) fail("Timed out waiting for the current physical depth frame");
      retire(); timer_.stop(); application_.exit(1); return;
    }
    if (!initialized_ && !initialize()) return;
    if (needs_prepare_) { prepare(); return; }
    if (presented_generation_ != requested_generation_ || presentation_clock_.elapsed() < 100) return;
    if (!capture()) return;
    switch (step_) {
    case Step::ObliqueOff: next(Step::ObliqueOn); break;
    case Step::ObliqueOn: next(Step::ObliqueRetained); break;
    case Step::ObliqueRetained:
      if (create_quad({{{70, 205}, {615, 205}, {615, 275}, {70, 275}}}, {{0.1f, 0.1f, 0.1f, 0.1f}},
              "Near oblique-wire occluder", false)) next(Step::ObliqueForegroundOff);
      break;
    case Step::ObliqueForegroundOff: next(Step::ObliqueForegroundOn); break;
    case Step::ObliqueForegroundOn:
      if (create_grazing()) next(Step::GrazingOff);
      break;
    case Step::GrazingOff: next(Step::GrazingOn); break;
    case Step::GrazingOn:
      if (create_quad({{{300, 80}, {340, 80}, {340, 400}, {300, 400}}}, {{10, 10, 10, 10}},
              "Foreground grazing-wire occluder", false)) next(Step::GrazingForegroundOff);
      break;
    case Step::GrazingForegroundOff: next(Step::GrazingForegroundOn); break;
    case Step::GrazingForegroundOn:
      if (create_source()) next(Step::SourceOff);
      break;
    case Step::SourceOff: enable_source_lines(); next(Step::SourceOn); break;
    case Step::SourceOn: next(Step::SourceRetained); break;
    case Step::SourceRetained:
      if (projection_index_ == 0u) {
        ++projection_index_; configure_camera();
        if (create_oblique()) next(Step::ObliqueOff);
      } else {
        retire(); timer_.stop();
        if (release_count_ != 1u) fail("The depth root failed to release its drained borrowed device");
        std::cout << "depth_phases=24; projections=2; releases=" << release_count_ << std::endl;
        application_.exit(error_.empty() ? 0 : 1);
      }
      break;
    }
  }

  QApplication &application_;
  QString output_directory_;
  std::filesystem::path resources_;
  SceneSession session_;
  QuaderEditorApi editor_;
  std::unique_ptr<RenderHost> host_;
  std::unique_ptr<RenderSceneSink> sink_;
  std::unique_ptr<RenderView> view_;
  std::unique_ptr<RenderSurface> surface_;
  ViewportSettings appearance_;
  ViewportCameraController camera_;
  ViewportOverlayFrame source_frame_;
  std::vector<QuaderModelingObjectRef> objects_;
  std::unique_ptr<QWidget> root_;
  QtVulkanCompositionWidget *widget_ = nullptr;
  QTimer timer_;
  QElapsedTimer step_clock_, presentation_clock_;
  QImage baseline_, foreground_baseline_, previous_;
  static constexpr std::array kEdges{
      std::array<QVec2, 2>{{{190.25f, 80}, {197.25f, 400}}},
      std::array<QVec2, 2>{{{390.25f, 80}, {550.25f, 400}}}};
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
  CrimsonRenderDiagnostics retained_;
#endif
  Step step_ = Step::ObliqueOff;
  std::uint64_t requested_generation_ = 1u, prepared_generation_ = 0u;
  std::uint64_t rendered_generation_ = 0u, presented_generation_ = 0u, source_revision_ = 1u;
  std::uint32_t projection_index_ = 0u, release_count_ = 0u;
  bool initialized_ = false, needs_prepare_ = true;
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
  const QCommandLineOption output(QStringLiteral("output-dir"), QStringLiteral("Physical depth GPU evidence directory."), QStringLiteral("directory"));
  const QCommandLineOption resources(QStringLiteral("resource-dir"), QStringLiteral("Native runtime directory containing packaged materials."), QStringLiteral("directory"));
  parser.addOption(output); parser.addOption(resources); parser.process(application);
  if (parser.value(output).isEmpty() || parser.value(resources).isEmpty()) {
    std::cerr << "--output-dir and --resource-dir are required\n"; return 2;
  }
  quader::DepthCompositionProbe probe(application, parser.value(output), std::filesystem::path(parser.value(resources).toStdWString()));
  return probe.run();
}
