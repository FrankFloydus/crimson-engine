////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "render/filament_material_library.hpp"
#include "render/overlay/viewport_selection_marquee_overlay_builder.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/png_texture_loader.hpp"
#include "render/render_layers.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/viewport_renderer.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <backend/PixelBufferDescriptor.h>
#include <filament/SwapChain.h>

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QImage>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace quader {
namespace {

filament::Texture *create_test_solid_color_texture(
    filament::Engine &engine) {
  const std::array<std::byte, 4u> neutral_gray{
      std::byte{181u}, std::byte{181u}, std::byte{181u}, std::byte{255u}};
  return load_rgba_texture(
      engine, neutral_gray, 1u, 1u,
      PngTextureLoadOptions{
          .color_space = PngTextureColorSpace::Srgb,
          .generate_mipmaps = true,
          .label = "selection runtime Solid viewport texture",
      });
}

/** Publishes the same semantic frame to the production retained overlay path. */
class SelectionRuntimeFrame final : public ViewportOverlaySelectionFrameProvider {
public:
  ViewportOverlayFrame build_selection_overlay_frame() const override {
    return frame;
  }

  ViewportOverlayFrame frame;
};

/** Runs the production scene and overlay passes on a readable Vulkan surface. */
class SelectionOverlayRuntimeHarness final
    : public ViewportOverlaySelectionFrameProvider {
public:
  static constexpr std::uint32_t kWidth = 640u;
  static constexpr std::uint32_t kHeight = 480u;
  static constexpr QVec2 kVisiblePixel{200.0f, 240.0f};
  static constexpr QVec2 kHiddenPixel{440.0f, 240.0f};

  ~SelectionOverlayRuntimeHarness() {
    if (engine_ == nullptr) return;
    (void)engine_->flushAndWait(5'000'000'000ULL);
    engine_->pumpMessageQueues();
    view_.shutdown();
    bridge_.shutdown();
    registry_.clear();
    materials_.shutdown();
    if (solid_color_texture_) engine_->destroy(solid_color_texture_);
    if (swapchain_) engine_->destroy(swapchain_);
    if (renderer_) engine_->destroy(renderer_);
    if (scene_) engine_->destroy(scene_);
    filament::Engine::destroy(&engine_);
  }

  bool initialize(float wall_min_z = -0.02f,
                  bool create_visibility_wall = true) {
    const std::filesystem::path resources(
        QCoreApplication::applicationDirPath().toStdWString());
    engine_ = filament::Engine::create(filament::Engine::Backend::VULKAN);
    if (engine_ == nullptr) return false;
    scene_ = engine_->createScene();
    renderer_ = engine_->createRenderer();
    swapchain_ = engine_->createSwapChain(
        kWidth, kHeight, filament::SwapChain::CONFIG_READABLE);
    if (!scene_ || !renderer_ || !swapchain_ ||
        !materials_.initialize(*engine_, resources)) return false;
    solid_color_texture_ = create_test_solid_color_texture(*engine_);
    if (solid_color_texture_ == nullptr) return false;
    bridge_.initialize(
        *engine_, *scene_, materials_.quader_mesh(),
        materials_.quader_mesh_lit(), materials_.vertex_color_overlay(),
        materials_.instance_selection_fill(), materials_.overlay_line(),
        materials_.selected_object_line(), materials_.edit_wire_line(),
        materials_.overlay_point(), materials_.overlay_icon(),
        *solid_color_texture_, resources);
    view_.initialize(*engine_, *scene_, registry_, materials_.ground_grid(),
                     viewport_grid_layer_mask(0u),
                     ViewportRenderSurface::Swapchain);
    camera_.set_viewport_size(kWidth, kHeight);
    camera_.set_view_direction({0.0f, 0.0f, -1.0f});
    camera_.set_clip_planes(0.001f, 200.0f);
    view_.resize(kWidth, kHeight);
    view_.set_overlay_visible(true);
    view_.set_scene_layer_mask(kViewportRenderedSceneLayerMask);
    view_.set_shadowing_enabled(false);
    view_.set_material_debug_mode(ViewportMaterialDebugMode::Albedo);
    SceneSettings settings;
    settings.environment.enabled = false;
    settings.environment.show_background = false;
    settings.environment.clear_color = {};
    settings.post_process.anti_aliasing = false;
    settings.post_process.dithering = false;
    settings.post_process.ambient_occlusion = false;
    settings.post_process.bloom = false;
    settings.post_process.tone_mapping = SceneToneMapping::Linear;
    view_.apply_scene_settings(settings, true, ViewportSettings::defaults());

    if (!create_visibility_wall) return true;

    QuaderApi api(QuaderApiSessionInternalAccess::from_scene_session(session_));
    const auto wall = api.modeling().create_box(
        {.min = {-100.0f, -100.0f, wall_min_z},
         .max = {100.0f, 100.0f, 0.0f}}, "Selection visibility wall");
    if (!wall.ok()) return false;
    const auto material = api.assets().add_material(QuaderMaterialAsset{
        .name = "Selection visibility neutral gray",
        .base_color = {0.18f, 0.18f, 0.18f}, .roughness = 1.0f});
    if (!material.ok() ||
        !api.assets().assign_material_to_entity(
            wall.value().entity, material.value().id).ok()) return false;
    bridge_.sync(session_.scene(), session_.assets());
    return true;
  }

  void set_sample(float eye_distance, bool hidden_point_on_top = false) {
    camera_.focus_origin(eye_distance);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    const auto overlay_camera = ViewportCameraAdapter::overlay_camera(
        camera_, {kWidth, kHeight});
    SelectionRuntimeFrame provider;
    // The same wall that is rendered by the scene pass supplies component
    // depth-stamp triangles. Both triangles face the camera.
    provider.frame.triangles = {
        {.layer = ViewportOverlayLayer::SourceWireDepthStamp,
         .a = {-100.0f, -100.0f, 0.0f},
         .b = {100.0f, -100.0f, 0.0f},
         .c = {100.0f, 100.0f, 0.0f},
         .source = ViewportOverlaySource::ComponentSelection,
         .element = {.object_id = 2}},
        {.layer = ViewportOverlayLayer::SourceWireDepthStamp,
         .a = {-100.0f, -100.0f, 0.0f},
         .b = {100.0f, 100.0f, 0.0f},
         .c = {-100.0f, 100.0f, 0.0f},
         .source = ViewportOverlaySource::ComponentSelection,
         .element = {.object_id = 2}},
    };
    // The editable mesh is an inward room; the opaque wall is a different
    // scene object. This is the exact old failure: camera-inside detection
    // promoted every room vertex above the unrelated wall's GPU depth.
    append_box_depth_stamps(provider.frame, -1.0f, 2.0f, true);
    constexpr auto kCyan = rgba8(40, 225, 255);
    provider.frame.points = {
        {.layer = ViewportOverlayLayer::UnselectedVertex,
         .position = point_at_plane(kVisiblePixel, 0.0f),
         .color = kCyan,
         .size_pixels = 9.0f,
         .style_role = ViewportOverlayStyleRole::ExplicitColor,
         .source = ViewportOverlaySource::ComponentSelection,
         .element = {.object_id = 1, .vertex_id = 0}},
        {.layer = hidden_point_on_top ? ViewportOverlayLayer::ToolHandle
                                     : ViewportOverlayLayer::UnselectedVertex,
         .position = point_at_plane(kHiddenPixel, -0.25f),
         .color = kCyan,
         .size_pixels = 9.0f,
         .style_role = ViewportOverlayStyleRole::ExplicitColor,
         .source = ViewportOverlaySource::ComponentSelection,
         .element = {.object_id = 1, .vertex_id = 1}},
    };
    const std::vector<QVec2> lasso{
        {100.0f, 145.0f}, {240.0f, 105.0f}, {370.0f, 140.0f},
        {500.0f, 115.0f}, {540.0f, 325.0f}, {380.0f, 370.0f},
        {310.0f, 300.0f}, {220.0f, 385.0f}, {105.0f, 325.0f}};
    ViewportSelectionMarqueeOverlayBuilder::append(
        provider.frame, overlay_camera, {.active = true, .lasso_points = lasso});
    (void)bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u),
         .camera = overlay_camera,
         .selection_source = {.source_identity = 104u,
                              .selection_revision = ++revision_},
         .selection_frame_provider = &provider,
         .style_selection_context = ViewportOverlaySelectionContext::Vertex,
         .overlays_visible = true},
        ViewportSettings::defaults());
  }

  bool set_selected_vertex_sample(float eye_distance,
                                  ViewportProjection projection, int side) {
    camera_.view_axis(ViewportViewAxis::Z, side);
    camera_.focus_origin(eye_distance);
    if (projection == ViewportProjection::Perspective &&
        !camera_.restore_perspective_pose({
            .eye = {0.0f, 0.0f, side * eye_distance},
            .target = {}, .up = {0.0f, 1.0f, 0.0f}})) {
      return false;
    }
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    selected_vertex_frame_.frame = {};
    append_box_depth_stamps(selected_vertex_frame_.frame, -0.25f, 0.0f, false);
    const float near_z = side > 0 ? 0.0f : -0.25f;
    const float far_z = side > 0 ? -0.25f : 0.0f;
    selected_vertex_frame_.frame.points = {
        {.layer = ViewportOverlayLayer::SelectedVertex,
         .position = point_at_plane(kVisiblePixel, near_z),
         .color = rgba8(40, 225, 255),
         .size_pixels = 9.0f,
         .style_role = ViewportOverlayStyleRole::ExplicitColor,
         .source = ViewportOverlaySource::ComponentSelection,
         .element = {.object_id = 1, .vertex_id = 0}},
        {.layer = ViewportOverlayLayer::SelectedVertex,
         .position = point_at_plane(kHiddenPixel, far_z),
         .color = rgba8(40, 225, 255),
         .size_pixels = 9.0f,
         .style_role = ViewportOverlayStyleRole::ExplicitColor,
         .source = ViewportOverlaySource::ComponentSelection,
         .element = {.object_id = 1, .vertex_id = 1}},
    };
    ++revision_;
    return true;
  }

  void present_selected_vertex_sample(bool wireframe) {
    // Match the production pane: wireframe hides solid surfaces while the
    // semantic selection frame still supplies the editable object's topology.
    view_.set_scene_visible(!wireframe);
    (void)bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u),
         .camera = ViewportCameraAdapter::overlay_camera(camera_, {kWidth, kHeight}),
         .selection_source = {.source_identity = 105u,
                              .selection_revision = revision_},
         .selection_frame_provider = &selected_vertex_frame_,
         .scene_selection_mode = RenderOverlaySelectionMode::Component,
         .style_selection_context = ViewportOverlaySelectionContext::Vertex,
         .overlays_visible = true,
         .wireframe_pane = wireframe},
        ViewportSettings::defaults());
  }

  bool create_extrusion_sample(modeling::FaceExtrudeMode mode) {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {-0.75f, -0.5f, 0.0f}),
        quader_poly::add_vertex(document, {0.75f, -0.5f, 0.0f}),
        quader_poly::add_vertex(document, {0.75f, 0.5f, 0.0f}),
        quader_poly::add_vertex(document, {-0.75f, 0.5f, 0.0f})};
    (void)quader_poly::add_face(document, vertices);
    const auto object = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Extrusion outline pixel regression"});
    if (!object.ok() || !editor.selection().set_mode(QuaderSelectionMode::Face).ok() ||
        !editor.selection().clear().ok() || !session_.extrude().activate(mode).ok ||
        !editor.selection().select_faces(object.value(), {1}).ok()) return false;

    // The oblique view separates the fixed base and moving cap on screen.
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.set_view_direction({0.0f, 3.0f, -2.0f});
    camera_.focus_origin(3.0f);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  ExtrudeToolService &extrusion() { return session_.extrude(); }
  std::size_t undo_count() const { return session_.history().undo_count(); }

  bool create_dimension_sample(bool tilted) {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    quader_poly::Document document;
    if (tilted) {
      const std::array vertices{
          quader_poly::add_vertex(document, {-1.0f, -0.6f, -0.4f}),
          quader_poly::add_vertex(document, {1.0f, -0.6f, -0.4f}),
          quader_poly::add_vertex(document, {1.0f, 0.6f, 0.4f}),
          quader_poly::add_vertex(document, {-1.0f, 0.6f, 0.4f})};
      (void)quader_poly::add_face(document, vertices);
    } else {
      const std::array vertices{
          quader_poly::add_vertex(document, {-1.0f, 0.0f, -0.6f}),
          quader_poly::add_vertex(document, {1.0f, 0.0f, -0.6f}),
          quader_poly::add_vertex(document, {1.0f, 0.0f, 0.6f})};
      (void)quader_poly::add_face(document, vertices);
    }
    const auto object = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Dimension frame pixel regression"});
    if (!object.ok() || !editor.selection().set_mode(QuaderSelectionMode::Face).ok() ||
        !editor.selection().clear().ok() ||
        !editor.selection().select_face(object.value(), 1).ok()) return false;
    if (!camera_.restore_perspective_pose(
            {.eye = {2.8f, -4.0f, 2.6f}, .target = {}, .up = {0.0f, 0.0f, 1.0f}})) {
      return false;
    }
    camera_.focus_bounds({-1.0f, -0.6f, -0.6f}, {1.0f, 0.6f, 0.6f}, 1.6f);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  bool present_face_sample(bool dimensions_visible = false) {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    if (!QuaderRenderApiRuntimeAccess::sync_scene(editor.render(), bridge_).ok() ||
        !QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(), bridge_).ok()) {
      return false;
    }
    const auto &modeling = session_.modeling();
    last_submission_ = bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u),
         .camera = ViewportCameraAdapter::overlay_camera(camera_, {kWidth, kHeight}),
         .selection_source = {
             .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
             .content_revision = modeling.content_revision(),
             .selection_revision = modeling.selection_revision(),
             .scene_revision = session_.scene().revision(),
             .asset_revision = session_.assets().revision(),
             .overlay_signature = modeling.selection_overlay_signature(),
             .selection_context = ViewportOverlaySelectionContext::Face},
         .selection_frame_provider = this,
         .scene_selection_mode = RenderOverlaySelectionMode::Component,
         .style_selection_context = ViewportOverlaySelectionContext::Face,
         .overlays_visible = true,
         .selection_dimensions_visible = dimensions_visible},
        ViewportSettings::defaults());
    return true;
  }

  bool create_active_selection_sample(bool groups) {
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session_));
    for (std::size_t index = 0; index < active_entities_.size(); ++index) {
      const float center = index == 0 ? -1.25f : 1.25f;
      const auto object = editor.modeling().create_box(
          {.min = {center - 0.65f, -0.65f, -0.4f},
           .max = {center + 0.65f, 0.65f, 0.4f}}, "Active selection sample");
      if (!object.ok()) return false;
      active_entities_[index] = object.value().entity;
      if (groups) {
        const auto group = editor.scene().create_empty();
        if (!group.ok() || !editor.scene().reparent(object.value().entity, group.value().id).ok()) return false;
        active_entities_[index] = group.value().id;
      }
    }
    if (!editor.selection().set_mode(groups ? QuaderSelectionMode::Group : QuaderSelectionMode::Object).ok()) return false;
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.focus_bounds({-2, -1, -0.5f}, {2, 1, 0.5f}, 1.5f);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  bool present_active_selection_sample(bool groups, std::size_t active, float scale) {
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session_));
    if (!editor.selection().select_objects(active_entities_, active_entities_[active]).ok() ||
        !QuaderRenderApiRuntimeAccess::sync_scene(editor.render(), bridge_).ok()) return false;
    const auto settings = ViewportSettings::defaults();
    const auto style = ViewportOverlayStyleResolver::persistent_style_snapshot(settings, scale);
    // Use the viewport's runtime adapters so scene and overlay publications share
    // the live editor binding and unchanged snapshots remain retained.
    if (!QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays(
            editor.render(), bridge_, active_snapshots_,
            {.selected_object_overlay_visible = true,
             .selected_mesh_outline_only = true, .style = style}).ok()) return false;
    const auto &scene = QuaderRenderApiRuntimeAccess::build_scene_overlay_snapshot(
        editor.render(), active_snapshots_);
    last_submission_ = bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u),
         .raster_scale = scale,
         .camera = ViewportCameraAdapter::overlay_camera(camera_, {kWidth, kHeight}),
         .scene_overlay_snapshot = &scene,
         .selected_mesh_outline_snapshot = active_snapshots_.selected_object_snapshot(),
         .scene_selection_mode = groups ? RenderOverlaySelectionMode::Group : RenderOverlaySelectionMode::Object,
         .style_selection_context = groups ? ViewportOverlaySelectionContext::Group : ViewportOverlaySelectionContext::Object,
         .overlays_visible = true,
         .scene_overlay_requested = true,
         .selection_dimensions_visible = groups}, settings);
    return true;
  }

  const ViewportOverlaySubmissionResult &last_submission() const {
    return last_submission_;
  }

  ViewportOverlayFrame build_selection_overlay_frame() const override {
    ++selection_frame_builds_;
    return session_.modeling().selection_overlay_frame();
  }

  std::uint64_t selection_frame_builds() const { return selection_frame_builds_; }

  std::optional<QVec2> front_cap_edge_pixel() const {
    std::optional<QVec2> result;
    // Derive the expected pixel from current authoritative cap geometry, not
    // from the retained frame whose invalidation is being tested.
    for (const auto &line : session_.modeling().selection_overlay_frame().lines) {
      if (line.layer != ViewportOverlayLayer::SelectedFaceEdge) continue;
      const auto projected = camera_.project_world_point((line.start + line.end) * 0.5f);
      if (projected.visible && (!result || projected.position.y > result->y)) {
        result = projected.position;
      }
    }
    if (result) {
      // capture() mirrors the Vulkan readback vertically. Convert the camera's
      // viewport pixel to that image space after choosing the visible front edge.
      const float viewport_y = result->y;
      result->y = static_cast<float>(kHeight - 1u) - viewport_y;
      std::cout << "Extrusion outline GPU probe: viewport=(" << result->x
                << ',' << viewport_y << "), image=(" << result->x << ','
                << result->y << ")\n";
    }
    return result;
  }

  QImage capture() {
    for (int frame = 0; frame < 5; ++frame) {
      if (!begin_frame()) return {};
      view_.render(*renderer_);
      renderer_->endFrame();
    }
    if (!begin_frame()) return {};
    view_.render(*renderer_);
    auto readback = std::make_shared<Readback>();
    auto *owner = new std::shared_ptr<Readback>(readback);
    renderer_->readPixels(
        0, 0, kWidth, kHeight,
        filament::backend::PixelBufferDescriptor(
            readback->bytes.data(), readback->bytes.size(),
            filament::backend::PixelDataFormat::RGBA,
            filament::backend::PixelDataType::UBYTE,
            [](void *, std::size_t, void *user) {
              auto *holder = static_cast<std::shared_ptr<Readback> *>(user);
              (*holder)->complete.store(true, std::memory_order_release);
              delete holder;
            }, owner));
    renderer_->endFrame();
    for (int attempt = 0; attempt < 10 &&
         !readback->complete.load(std::memory_order_acquire); ++attempt) {
      if (!engine_->flushAndWait(1'000'000'000ULL)) return {};
      engine_->pumpMessageQueues();
    }
    if (!readback->complete.load(std::memory_order_acquire)) return {};
    return QImage(readback->bytes.data(), kWidth, kHeight,
                   QImage::Format_RGBA8888).mirrored(false, true);
  }

private:
  static void append_box_depth_stamps(ViewportOverlayFrame &frame,
                                      float min_z, float max_z, bool inward) {
    const auto quad = [&](QVec3 a, QVec3 b, QVec3 c, QVec3 d) {
      for (auto triangle :
           std::array<std::array<QVec3, 3>, 2>{{{a, b, c}, {a, c, d}}}) {
        if (inward) {
          std::swap(triangle[1], triangle[2]);
        }
        frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SourceWireDepthStamp,
            .a = triangle[0], .b = triangle[1], .c = triangle[2],
            .source = ViewportOverlaySource::ComponentSelection,
            .element = {.object_id = 1},
        });
      }
    };
    quad({-100, -100, max_z}, {100, -100, max_z},
         {100, 100, max_z}, {-100, 100, max_z});
    quad({100, -100, min_z}, {-100, -100, min_z},
         {-100, 100, min_z}, {100, 100, min_z});
    quad({-100, -100, min_z}, {-100, -100, max_z},
         {-100, 100, max_z}, {-100, 100, min_z});
    quad({100, -100, max_z}, {100, -100, min_z},
         {100, 100, min_z}, {100, 100, max_z});
    quad({-100, -100, min_z}, {100, -100, min_z},
         {100, -100, max_z}, {-100, -100, max_z});
    quad({-100, 100, max_z}, {100, 100, max_z},
         {100, 100, min_z}, {-100, 100, min_z});
  }

  /** Retains CPU bytes until Filament invokes its asynchronous release callback. */
  struct Readback final {
    std::array<unsigned char, kWidth * kHeight * 4> bytes{};
    std::atomic<bool> complete = false;
  };

  QVec3 point_at_plane(QVec2 pixel, float z) const {
    const auto ray = camera_.ray_from_viewport_point(pixel);
    return ray.origin + ray.direction * ((z - ray.origin.z) / ray.direction.z);
  }

  bool begin_frame() {
    for (int attempt = 0; attempt < 80; ++attempt) {
      if (renderer_->beginFrame(swapchain_)) return true;
      if (!engine_->flushAndWait(1'000'000'000ULL)) return false;
      engine_->pumpMessageQueues();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }

  SceneSession session_;
  std::array<EntityId, 2> active_entities_{};
  RenderOverlaySnapshotCache active_snapshots_;
  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Renderer *renderer_ = nullptr;
  filament::SwapChain *swapchain_ = nullptr;
  filament::Texture *solid_color_texture_ = nullptr;
  FilamentMaterialLibrary materials_;
  TextureRegistry registry_;
  SceneRenderBridge bridge_;
  ViewportRenderer view_;
  ViewportCameraController camera_;
  SelectionRuntimeFrame selected_vertex_frame_;
  ViewportOverlaySubmissionResult last_submission_;
  std::uint64_t revision_ = 0;
  mutable std::uint64_t selection_frame_builds_ = 0;
};

int cyan_pixels(const QImage &image, QVec2 center) {
  int count = 0;
  for (int y = int(center.y) - 3; y <= int(center.y) + 3; ++y) {
    for (int x = int(center.x) - 3; x <= int(center.x) + 3; ++x) {
      const QColor pixel = image.pixelColor(x, y);
      if (pixel.green() > pixel.red() + 45 &&
          pixel.blue() > pixel.red() + 45 && pixel.green() > 130) ++count;
    }
  }
  return count;
}

TEST(SelectionOverlayRuntimeTests,
     VulkanLassoAndOccludedVerticesStayCorrectAsCameraApproachesSurface) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 to read "
                    "and capture production offscreen Vulkan selection overlays.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-selection-overlay-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  QString output_directory =
      qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (output_directory.isEmpty()) {
    output_directory = QDir(QCoreApplication::applicationDirPath())
                           .absoluteFilePath(QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(output_directory));
  for (const auto &[label, distance] :
       std::array<std::pair<const char *, float>, 3>{{
           {"near", 0.1f}, {"middle", 1.0f}, {"far", 20.0f}}}) {
    SCOPED_TRACE(label);
    harness.set_sample(distance);
    const QImage image = harness.capture();
    ASSERT_FALSE(image.isNull());
    const QString path = QDir(output_directory).filePath(
        QString::fromLatin1(label) + QStringLiteral("_lasso_depth.png"));
    ASSERT_TRUE(image.save(path));
    const int visible = cyan_pixels(image, SelectionOverlayRuntimeHarness::kVisiblePixel);
    const int hidden = cyan_pixels(image, SelectionOverlayRuntimeHarness::kHiddenPixel);
    EXPECT_GE(visible, 9) << "Visible surface vertex must survive depth testing.";
    EXPECT_EQ(hidden, 0) << "Occluded vertex must not leak through the wall.";
    std::cout << "Selection GPU " << label << ": visible_cyan=" << visible
              << ", hidden_cyan=" << hidden << ", png="
              << path.toStdString() << '\n';
  }

  // The same hidden position must be visible when deliberately drawn on top;
  // this rules out an offscreen point or failed point pipeline hiding the bug.
  harness.set_sample(0.1f, true);
  const QImage control = harness.capture();
  ASSERT_FALSE(control.isNull());
  ASSERT_TRUE(control.save(QDir(output_directory).filePath(
      QStringLiteral("near_on_top_control.png"))));
  EXPECT_GE(cyan_pixels(control, SelectionOverlayRuntimeHarness::kHiddenPixel), 9);
}

int yellow_pixels(const QImage &image, QVec2 center) {
  int count = 0;
  for (int y = int(center.y) - 3; y <= int(center.y) + 3; ++y) {
    for (int x = int(center.x) - 3; x <= int(center.x) + 3; ++x) {
      if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) continue;
      const QColor pixel = image.pixelColor(x, y);
      if (pixel.red() > 130 && pixel.green() > 110 &&
          pixel.red() > pixel.blue() + 50 &&
          pixel.green() > pixel.blue() + 35) ++count;
    }
  }
  return count;
}

TEST(SelectionOverlayRuntimeTests,
     VulkanWireframeShowsSelectedBackVerticesAndSolidRestoresOcclusion) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 to read "
                    "and capture production offscreen Vulkan selection overlays.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-wireframe-selection-overlay-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(-0.25f));
  QString output_directory =
      qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (output_directory.isEmpty()) {
    output_directory = QDir(QCoreApplication::applicationDirPath())
                           .absoluteFilePath(QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(output_directory));

  for (const ViewportProjection projection :
       {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    for (const int side : {1, -1}) {
      for (const auto &[distance_label, distance] :
           std::array<std::pair<const char *, float>, 2>{{
               {"near", 0.5f}, {"far", 20.0f}}}) {
        const QString sample =
            QStringLiteral("selected_vertices_%1_%2_%3")
                .arg(projection == ViewportProjection::Perspective
                         ? QStringLiteral("perspective")
                         : QStringLiteral("orthographic"))
                .arg(side > 0 ? QStringLiteral("front") : QStringLiteral("back"))
                .arg(QString::fromLatin1(distance_label));
        SCOPED_TRACE(sample.toStdString());
        ASSERT_TRUE(harness.set_selected_vertex_sample(distance, projection, side));
        // Keep selection and geometry revisions fixed through both shading
        // transitions: the pane mode alone must refresh its retained overlay.
        for (int phase = 0; phase < 3; ++phase) {
          const bool wireframe = phase == 1;
          SCOPED_TRACE(phase);
          harness.present_selected_vertex_sample(wireframe);
          const QImage image = harness.capture();
          ASSERT_FALSE(image.isNull());
          const QString suffix = phase == 0 ? QStringLiteral("_solid.png")
              : wireframe ? QStringLiteral("_wireframe.png")
                          : QStringLiteral("_solid_restored.png");
          const QString path = QDir(output_directory).filePath(sample + suffix);
          ASSERT_TRUE(image.save(path));
          const int visible = cyan_pixels(
              image, SelectionOverlayRuntimeHarness::kVisiblePixel);
          const int hidden = cyan_pixels(
              image, SelectionOverlayRuntimeHarness::kHiddenPixel);
          EXPECT_GE(visible, 9) << "The nearest selected vertex stays visible.";
          if (wireframe) {
            EXPECT_GE(hidden, 9)
                << "A selected back vertex must remain visible when wireframe hides surfaces.";
          } else {
            EXPECT_EQ(hidden, 0)
                << "Solid geometry must occlude the back vertex without an X-ray fallback.";
          }
          std::cout << "Selection GPU " << sample.toStdString()
                    << " phase=" << phase << ": visible_cyan=" << visible
                    << ", hidden_cyan=" << hidden << ", png="
                    << path.toStdString() << '\n';
        }
      }
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanExtrusionCapOutlineFollowsPreviewAndLeavesNoOldYellowEdge) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 to read "
                    "and capture production offscreen Vulkan selection overlays.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-extrusion-outline-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  QString output_directory =
      qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (output_directory.isEmpty()) {
    output_directory = QDir(QCoreApplication::applicationDirPath())
                           .absoluteFilePath(QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(output_directory));

  for (const auto &[mode, label] :
       std::array<std::pair<modeling::FaceExtrudeMode, const char *>, 3>{{
           {modeling::FaceExtrudeMode::Region, "region"},
           {modeling::FaceExtrudeMode::AlongNormals, "normals"},
           {modeling::FaceExtrudeMode::Individual, "individual"}}}) {
    SCOPED_TRACE(label);
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_extrusion_sample(mode));
    ASSERT_TRUE(harness.extrusion().preview_active());
    ASSERT_DOUBLE_EQ(harness.extrusion().amount(), 0.0);
    const auto initial_undo = harness.undo_count();
    ASSERT_TRUE(harness.present_face_sample());
    const auto source_pixel = harness.front_cap_edge_pixel();
    ASSERT_TRUE(source_pixel.has_value());
    const QImage source = harness.capture();
    ASSERT_FALSE(source.isNull());
    EXPECT_GE(yellow_pixels(source, *source_pixel), 3);
    const auto save = [&](const QImage &image, const char *phase) {
      const QString path = QDir(output_directory).filePath(
          QStringLiteral("extrude_%1_%2.png")
              .arg(QString::fromLatin1(label), QString::fromLatin1(phase)));
      EXPECT_TRUE(image.save(path));
      std::cout << "Extrusion outline GPU png=" << path.toStdString() << '\n';
    };
    save(source, "source");

    ASSERT_TRUE(harness.extrusion().begin_drag().ok);
    QVec2 previous_pixel = *source_pixel;
    for (const auto &[amount, phase] :
         std::array<std::pair<double, const char *>, 2>{{
             {0.25, "preview_first"}, {0.6, "preview_moved"}}}) {
      SCOPED_TRACE(phase);
      ASSERT_TRUE(harness.extrusion().update_drag_world_amount(amount).ok);
      ASSERT_TRUE(harness.present_face_sample());
      const auto cap_pixel = harness.front_cap_edge_pixel();
      ASSERT_TRUE(cap_pixel.has_value());
      ASSERT_GT(std::abs(cap_pixel->y - previous_pixel.y), 15.0f);
      const QImage preview = harness.capture();
      ASSERT_FALSE(preview.isNull());
      save(preview, phase);
      EXPECT_GE(yellow_pixels(preview, *cap_pixel), 3)
          << "Selected cap outline must follow the rendered preview cap.";
      EXPECT_EQ(yellow_pixels(preview, previous_pixel), 0)
          << "The previous selected cap outline must disappear.";
      EXPECT_EQ(yellow_pixels(preview, *source_pixel), 0)
          << "Stationary base wires may remain cyan, but must not remain selected.";
      const auto builds = harness.selection_frame_builds();
      ASSERT_TRUE(harness.present_face_sample());
      EXPECT_EQ(harness.selection_frame_builds(), builds)
          << "An unchanged preview must reuse the retained selection frame.";
      previous_pixel = *cap_pixel;
    }

    ASSERT_TRUE(harness.extrusion().release_drag().ok);
    EXPECT_TRUE(harness.extrusion().preview_active());
    EXPECT_EQ(harness.undo_count(), initial_undo);
    ASSERT_TRUE(harness.present_face_sample());
    const QImage released = harness.capture();
    ASSERT_FALSE(released.isNull());
    EXPECT_GE(yellow_pixels(released, previous_pixel), 3);
    save(released, "released");

    ASSERT_TRUE(harness.extrusion().commit_preview().ok);
    EXPECT_FALSE(harness.extrusion().preview_active());
    EXPECT_DOUBLE_EQ(harness.extrusion().amount(),
                     0.6 * viewport_host_defaults().map_units_per_world_unit);
    EXPECT_EQ(harness.undo_count(), initial_undo + 1u);
    ASSERT_TRUE(harness.present_face_sample());
    const QImage committed = harness.capture();
    ASSERT_FALSE(committed.isNull());
    EXPECT_GE(yellow_pixels(committed, previous_pixel), 3);
    save(committed, "committed");

    ASSERT_TRUE(harness.extrusion().begin_drag().ok);
    ASSERT_TRUE(harness.extrusion().update_drag_world_amount(0.35).ok);
    ASSERT_TRUE(harness.present_face_sample());
    const auto adjusted_pixel = harness.front_cap_edge_pixel();
    ASSERT_TRUE(adjusted_pixel.has_value());
    ASSERT_GT(std::abs(adjusted_pixel->y - previous_pixel.y), 15.0f);
    const QImage adjusted = harness.capture();
    ASSERT_FALSE(adjusted.isNull());
    EXPECT_GE(yellow_pixels(adjusted, *adjusted_pixel), 3);
    EXPECT_EQ(yellow_pixels(adjusted, previous_pixel), 0);
    save(adjusted, "next_step");
    EXPECT_EQ(harness.undo_count(), initial_undo + 1u);

    ASSERT_TRUE(harness.extrusion().cancel_drag().ok);
    EXPECT_EQ(harness.undo_count(), initial_undo + 1u);
    ASSERT_TRUE(harness.present_face_sample());
    const QImage restored = harness.capture();
    ASSERT_FALSE(restored.isNull());
    EXPECT_GE(yellow_pixels(restored, previous_pixel), 3);
    EXPECT_EQ(yellow_pixels(restored, *adjusted_pixel), 0);
    save(restored, "restored");
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanSelectionDimensionsDrawBoxOrRectangleAndToggleWithoutStaleLines) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 to read "
                    "and capture production offscreen Vulkan selection overlays.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-dimension-frame-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  QString output_directory =
      qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (output_directory.isEmpty()) {
    output_directory = QDir(QCoreApplication::applicationDirPath())
                           .absoluteFilePath(QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(output_directory));

  for (const bool tilted : {true, false}) {
    const QString sample = tilted ? QStringLiteral("dimensions_tilted_box")
                                  : QStringLiteral("dimensions_planar_rectangle");
    SCOPED_TRACE(sample.toStdString());
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_dimension_sample(tilted));
    ASSERT_TRUE(harness.present_face_sample(false));
    const QImage disabled = harness.capture();
    ASSERT_FALSE(disabled.isNull());
    const auto selection_builds = harness.selection_frame_builds();
    ASSERT_TRUE(harness.present_face_sample(true));
    EXPECT_EQ(harness.selection_frame_builds(), selection_builds);
    ASSERT_TRUE(harness.last_submission().selection_dimension_bounds.has_value());
    const auto bounds = *harness.last_submission().selection_dimension_bounds;
    EXPECT_FLOAT_EQ(bounds.min.x, -1.0f);
    EXPECT_FLOAT_EQ(bounds.max.x, 1.0f);
    EXPECT_FLOAT_EQ(bounds.min.y, tilted ? -0.6f : 0.0f);
    EXPECT_FLOAT_EQ(bounds.max.y, tilted ? 0.6f : 0.0f);
    EXPECT_FLOAT_EQ(bounds.min.z, tilted ? -0.4f : -0.6f);
    EXPECT_FLOAT_EQ(bounds.max.z, tilted ? 0.4f : 0.6f);
    const QImage enabled = harness.capture();
    ASSERT_FALSE(enabled.isNull());

    // Count only pixels changed by the display toggle. This excludes selected
    // face wires/fill and proves both colored spans and the yellow frame reach
    // the production Vulkan surface. Qt text is verified by the HUD suite.
    std::array<int, 3> axis_pixels{};
    int frame_pixels = 0;
    int changed_pixels = 0;
    for (int y = 0; y < enabled.height(); ++y) {
      for (int x = 0; x < enabled.width(); ++x) {
        if (enabled.pixel(x, y) == disabled.pixel(x, y)) continue;
        ++changed_pixels;
        const QColor pixel = enabled.pixelColor(x, y);
        const int r = pixel.red();
        const int g = pixel.green();
        const int b = pixel.blue();
        if (r > 100 && r > g * 1.5f && r > b * 1.25f) ++axis_pixels[0];
        if (g > 100 && g > r * 1.25f && g > b * 1.25f) ++axis_pixels[1];
        if (b > 100 && b > r * 1.4f && b > g * 1.15f) ++axis_pixels[2];
        if (r > 110 && g > 100 && r > b + 50 && g > b + 35) ++frame_pixels;
      }
    }
    EXPECT_GE(changed_pixels, 50);
    EXPECT_GE(axis_pixels[0], 8);
    EXPECT_GE(axis_pixels[2], 8);
    if (tilted) {
      EXPECT_GE(axis_pixels[1], 8);
    } else {
      EXPECT_EQ(axis_pixels[1], 0) << "A zero Y extent must not acquire a depth axis.";
    }
    EXPECT_GE(frame_pixels, 12) << "The bounds beyond the selected face must be visible.";
    ASSERT_TRUE(harness.present_face_sample(false));
    EXPECT_EQ(harness.selection_frame_builds(), selection_builds);
    const QImage restored = harness.capture();
    ASSERT_FALSE(restored.isNull());
    EXPECT_EQ(restored, disabled) << "Disabling dimensions must leave no retained frame.";
    for (const auto &[suffix, image] :
         std::array<std::pair<const char *, const QImage *>, 3>{{
             {"_off.png", &disabled}, {"_on.png", &enabled},
             {"_off_restored.png", &restored}}}) {
      const QString path = QDir(output_directory).filePath(
          sample + QString::fromLatin1(suffix));
      EXPECT_TRUE(image->save(path));
      std::cout << "Dimension frame GPU png=" << path.toStdString() << '\n';
    }
    std::cout << "Dimension frame GPU " << sample.toStdString()
              << ": changed=" << changed_pixels << ", axis_rgb="
              << axis_pixels[0] << ',' << axis_pixels[1] << ',' << axis_pixels[2]
              << ", frame=" << frame_pixels << '\n';
  }
}

TEST(SelectionOverlayRuntimeTests, VulkanActiveObjectsAndGroupsSwitchBrightnessAt100And200Percent) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-active-selection-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (directory.isEmpty()) directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) directory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
      QStringLiteral("../active_selection_visual"));
  ASSERT_TRUE(QDir().mkpath(directory));
  for (const bool groups : {false, true}) {
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_active_selection_sample(groups));
    for (const float scale : {1.0f, 2.0f}) {
      const QString stem = QStringLiteral("active-%1-%2")
          .arg(groups ? QStringLiteral("groups") : QStringLiteral("objects")).arg(int(scale * 100));
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 0u, scale));
      const QImage left_active = harness.capture();
      ASSERT_FALSE(left_active.isNull());
      ASSERT_TRUE(left_active.save(QDir(directory).filePath(stem + QStringLiteral("-left.png"))));
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 1u, scale));
      const QImage right_active = harness.capture();
      ASSERT_FALSE(right_active.isNull());
      ASSERT_TRUE(right_active.save(QDir(directory).filePath(stem + QStringLiteral("-right.png"))));
      std::array<long long, 2> brightness_delta{};
      std::array<int, 2> changed{};
      for (int y = 0; y < left_active.height(); ++y) {
        for (int x = 0; x < left_active.width(); ++x) {
          const QColor before = left_active.pixelColor(x, y);
          const QColor after = right_active.pixelColor(x, y);
          const std::size_t side = x < left_active.width() / 2 ? 0u : 1u;
          if (before != after) ++changed[side];
          brightness_delta[side] += after.red() + after.green() + after.blue() -
                                    before.red() - before.green() - before.blue();
        }
      }
      EXPECT_GT(changed[0], 8);
      EXPECT_GT(changed[1], 8);
      EXPECT_LT(brightness_delta[0], 0) << "The previous active boundary must dim.";
      EXPECT_GT(brightness_delta[1], 0) << "The new active boundary must brighten.";
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 1u, scale));
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_FALSE(harness.last_submission().dynamic_submitted);
      EXPECT_EQ(harness.capture(), right_active);
      std::cout << "Active selection GPU " << stem.toStdString() << " changed="
                << changed[0] << ',' << changed[1] << " brightness="
                << brightness_delta[0] << ',' << brightness_delta[1] << '\n';
    }
  }
}

} // namespace
} // namespace quader
