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
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/path_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/project_session.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "render/filament_material_library.hpp"
#include "render/overlay/selected_mesh_outline_mask_cache.hpp"
#include "render/overlay/viewport_selection_marquee_overlay_builder.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/png_texture_loader.hpp"
#include "render/render_layers.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/viewport_renderer.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_host_interaction_policy.hpp"
#include "editor/viewport/viewport_scene_projection.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <backend/PixelBufferDescriptor.h>
#include <filament/RenderTarget.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QRect>
#include <QTemporaryDir>

#include <algorithm>
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
    mesh_outline_masks_.shutdown();
    point_audit_cache_.shutdown();
    point_audit_factory_.shutdown();
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
                  bool create_visibility_wall = true,
                  ViewportRenderSurface surface = ViewportRenderSurface::Swapchain) {
    const std::filesystem::path resources(
        QCoreApplication::applicationDirPath().toStdWString());
    engine_ = filament::Engine::create(filament::Engine::Backend::VULKAN);
    if (engine_ == nullptr) return false;
    scene_ = engine_->createScene();
    renderer_ = engine_->createRenderer();
    swapchain_ = engine_->createSwapChain(
        kWidth, kHeight, filament::SwapChain::CONFIG_READABLE |
                            filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER);
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
    surface_ = surface;
    view_.initialize(*engine_, *scene_, registry_, materials_.ground_grid(),
                     materials_.overlay_stencil_reset(),
                     viewport_grid_layer_mask(0u), surface_);
    if (!mesh_outline_masks_.initialize(*engine_, materials_.selected_mesh_outline_mask()) ||
        !view_.configure_selected_mesh_outlines(mesh_outline_masks_,
            materials_.selected_mesh_outline_composite(), viewport_grid_layer_mask(0u))) return false;
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

  bool prepare_overlay_audit_camera(float distance, ViewportProjection projection,
                                    float near_plane = 0.001f) {
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.focus_origin(distance);
    if (projection == ViewportProjection::Perspective &&
        !camera_.restore_perspective_pose({
            .eye = {0.0f, 0.0f, distance}, .target = {},
            .up = {0.0f, 1.0f, 0.0f}})) return false;
    camera_.set_clip_planes(near_plane, 200.0f);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  QVec3 overlay_audit_world_point(QVec2 image_pixel, float z) const {
    image_pixel.y = static_cast<float>(kHeight - 1u) - image_pixel.y;
    return point_at_plane(image_pixel, z);
  }

  bool create_outline_depth_quad(const std::array<MeshVertex, 4u> &points) {
    QuaderApi api(QuaderApiSessionInternalAccess::from_scene_session(session_));
    quader_poly::Document document;
    // overlay_audit_world_point accounts for the mirrored readback, so this
    // image-space order produces a front surface toward positive camera Z.
    const std::array vertices{
        quader_poly::add_vertex(document, points[0].position),
        quader_poly::add_vertex(document, points[1].position),
        quader_poly::add_vertex(document, points[2].position),
        quader_poly::add_vertex(document, points[3].position)};
    (void)quader_poly::add_face(document, vertices);
    const auto object = api.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Sloped outline depth regression"});
    const auto material = api.assets().add_material(QuaderMaterialAsset{
        .name = "Sloped outline neutral gray", .base_color = {0.18f, 0.18f, 0.18f}, .roughness = 1.0f});
    if (!object.ok() || !material.ok() ||
        !api.assets().assign_material_to_entity(object.value().entity, material.value().id).ok()) return false;
    bridge_.sync(session_.scene(), session_.assets());
    return true;
  }

  void set_overlay_audit_frame(ViewportOverlayFrame frame) {
    overlay_audit_frame_.frame = std::move(frame);
    ++revision_;
  }

  void present_retained_point_frame(const ViewportOverlayFrame &frame) {
    initialize_point_audit();
    (void)point_audit_cache_.set_overlay_frame(
        0u, viewport_grid_layer_mask(0u), frame,
        ViewportCameraAdapter::overlay_camera(camera_, {kWidth, kHeight}));
  }

  OverlayRenderableCacheStats retained_point_stats() const {
    return point_audit_cache_.stats(0u);
  }

  bool update_retained_point_positions(
      const std::vector<RenderToolPreviewMeshRecord> &previews) {
    return point_audit_cache_.set_transform_position_streams(previews);
  }

  QImage capture_legacy_point_frame(const ViewportOverlayFrame &frame) {
    initialize_point_audit();
    point_audit_cache_.clear_slot(0u);
    const auto camera =
        ViewportCameraAdapter::overlay_camera(camera_, {kWidth, kHeight});
    const auto signatures = OverlayRenderableBuilder::frame_signatures(frame);
    OverlayDepthStampPointVisibilityFilter visibility;
    std::vector<OverlayRenderableProxy> proxies;
    for (const auto layer : kViewportOverlayDrawOrder) {
      auto layer_proxies = point_audit_factory_.create_layer_proxies(
          OverlayRenderableBuilder::build_layer(
              frame, camera, layer, &signatures, 0u, &visibility),
          viewport_grid_layer_mask(0u));
      for (auto &proxy : layer_proxies) proxies.push_back(std::move(proxy));
    }
    auto image = capture();
    for (auto &proxy : proxies) point_audit_factory_.destroy(proxy);
    return image;
  }

  static void append_overlay_audit_occluder(ViewportOverlayFrame &frame, float z) {
    append_box_depth_stamps(frame, z - 0.001f, z, false);
  }

  void present_overlay_audit_frame(float raster_scale = 1.0f,
      ViewportOverlaySelectionContext context = ViewportOverlaySelectionContext::Object,
      bool publish_dynamic_frame = false) {
    ViewportOverlayFrame dynamic_frame;
    if (publish_dynamic_frame) dynamic_frame.lines = overlay_audit_frame_.frame.lines;
    last_submission_ = bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u),
         .raster_scale = raster_scale,
         .camera = ViewportCameraAdapter::overlay_camera(camera_, {kWidth, kHeight}),
         .selection_source = {.source_identity = 108u,
                              .selection_revision = revision_},
         .selection_frame_provider = &overlay_audit_frame_,
         .volatile_modeling_overlay_frame = publish_dynamic_frame ? &dynamic_frame : nullptr,
         .volatile_modeling_source = publish_dynamic_frame
             ? ViewportOverlayVolatileModelingSourceStamp{
                 .source_identity = 109u, .publication_revision = revision_}
             : ViewportOverlayVolatileModelingSourceStamp{},
         .scene_selection_mode = context == ViewportOverlaySelectionContext::Vertex
             ? RenderOverlaySelectionMode::Component : RenderOverlaySelectionMode::Object,
         .style_selection_context = context,
         .overlays_visible = true,
         .selection_dimensions_visible = false},
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

  bool create_edge_loop_sample() {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {-1.0f, -0.75f, 0.0f}),
        quader_poly::add_vertex(document, {1.0f, -0.75f, 0.0f}),
        quader_poly::add_vertex(document, {1.0f, 0.75f, 0.0f}),
        quader_poly::add_vertex(document, {-1.0f, 0.75f, 0.0f})};
    (void)quader_poly::add_face(document, vertices);
    const auto object = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Edge loop preview vertex pixel regression"});
    if (!object.ok() || !modeling().activate_viewport_tool(
                            ModelingViewportTool::InsertEdgeLoop).ok) return false;
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.focus_origin(3.0f);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  ModelingSessionService &modeling() { return session_.modeling(); }

  bool create_tool_topology_sample(ViewportToolId tool) {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {-1.0f, -0.75f, 0.0f}),
        quader_poly::add_vertex(document, {0.0f, -0.75f, 0.0f}),
        quader_poly::add_vertex(document, {1.0f, -0.75f, 0.0f}),
        quader_poly::add_vertex(document, {1.0f, 0.75f, 0.0f}),
        quader_poly::add_vertex(document, {0.0f, 0.75f, 0.0f}),
        quader_poly::add_vertex(document, {-1.0f, 0.75f, 0.0f})};
    (void)quader_poly::add_face(document,
        std::array{vertices[0], vertices[1], vertices[4], vertices[5]});
    (void)quader_poly::add_face(document,
        std::array{vertices[1], vertices[2], vertices[3], vertices[4]});
    const auto object = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Unselected cutting tool topology pixel regression"});
    if (!object.ok() ||
        !editor.selection().set_mode(QuaderSelectionMode::Object).ok() ||
        !editor.selection().clear().ok() || !editor.tools().activate(tool).ok()) {
      return false;
    }
    tool_topology_object_ = object.value();
    tool_topology_tool_ = tool;
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.focus_origin(3.0f);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  ModelingToolRayEvent tool_topology_event(QVec3 world) const {
    const auto projected = camera_.project_world_point(world);
    const auto ray = camera_.ray_from_viewport_point(projected.position);
    return {
        .screen_position = projected.position,
        .ray = {.origin = ray.origin, .direction = ray.direction},
        .camera = ViewportCameraAdapter::modeling_camera(camera_, {kWidth, kHeight}),
        .grid_size = 0.5,
        .edge_radius = 0.05,
        .snap_enabled = false,
    };
  }

  bool update_tool_topology_hover(QVec3 world) {
    const auto event = tool_topology_event(world);
    return tool_topology_tool_ == ViewportToolId::Knife
        ? modeling().update_knife_hover(event).ok
        : modeling().update_insert_edge_loop_hover(event).ok;
  }

  bool select_tool_topology_sample() {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    return editor.selection().select_object(tool_topology_object_).ok() &&
           editor.selection().set_mode(QuaderSelectionMode::Edge).ok() &&
           editor.tools().activate(tool_topology_tool_).ok();
  }

  bool occlude_tool_topology_internal_edge() {
    QuaderApi api(QuaderApiSessionInternalAccess::from_scene_session(session_));
    return api.modeling().create_box(
        {.min = {-0.2f, 0.2f, 0.2f}, .max = {0.2f, 0.6f, 0.4f}},
        "Cutting topology foreground occluder").ok();
  }

  QuaderSelectionSummary tool_topology_selection() {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    return editor.selection().summary();
  }

  QVec2 tool_topology_internal_edge_pixel() const {
    // This authored shared edge is not a silhouette or triangulation diagonal.
    // Its upper segment is also clear of the cut preview at y = -0.25.
    auto pixel = camera_.project_world_point({0.0f, 0.4f, 0.0f}).position;
    pixel.y = static_cast<float>(kHeight - 1u) - pixel.y;
    return pixel;
  }

  QVec2 cutting_preview_pixel(QVec3 world) const {
    auto pixel = camera_.project_world_point(world).position;
    pixel.y = static_cast<float>(kHeight - 1u) - pixel.y;
    return pixel;
  }

  bool create_face_fill_depth_sample(bool front_facing) {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    quader_poly::Document document;
    std::array near_vertices{
        quader_poly::add_vertex(document, {-1.0f, -0.75f, 0.0f}),
        quader_poly::add_vertex(document, {1.0f, -0.75f, 0.0f}),
        quader_poly::add_vertex(document, {1.0f, 0.75f, 0.0f}),
        quader_poly::add_vertex(document, {-1.0f, 0.75f, 0.0f})};
    if (!front_facing) std::swap(near_vertices[1], near_vertices[3]);
    (void)quader_poly::add_face(document, near_vertices);
    const std::array rear_vertices{
        quader_poly::add_vertex(document, {-0.6f, -0.35f, -0.25f}),
        quader_poly::add_vertex(document, {0.6f, -0.35f, -0.25f}),
        quader_poly::add_vertex(document, {0.0f, 0.35f, -0.25f})};
    (void)quader_poly::add_face(document, rear_vertices);
    const auto object = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Culled face fill depth regression"});
    if (!object.ok() || !editor.selection().set_mode(QuaderSelectionMode::Face).ok()) {
      return false;
    }
    face_fill_object_ = object.value();
    if (!camera_.restore_perspective_pose({
            .eye = {0.0f, 0.0f, 3.0f}, .target = {}, .up = {0.0f, 1.0f, 0.0f}})) {
      return false;
    }
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  bool set_face_fill_hover(bool hovered) {
    if (!hovered) return modeling().clear_selection_hover().ok;
    const auto result = modeling().update_selection_hover({
        .ray = {.origin = {0.0f, 0.0f, 3.0f}, .direction = {0.0f, 0.0f, -1.0f}},
        .select_backfaces = true,
    });
    return result.ok && modeling().selection_hover_summary().target.face_id == 1;
  }

  bool select_near_face_fill() {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    return editor.selection().select_face(face_fill_object_, 1).ok();
  }

  QVec2 face_fill_probe_pixel(bool rear_wire) const {
    const QVec3 world = rear_wire ? QVec3{0.0f, -0.35f, -0.25f}
                                 : QVec3{0.75f, 0.25f, 0.0f};
    auto pixel = camera_.project_world_point(world).position;
    pixel.y = static_cast<float>(kHeight - 1u) - pixel.y;
    return pixel;
  }

  bool create_overlapping_face_fill_sample() {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    quader_poly::Document document;
    const auto quad = [&](float min_x, float max_x, float min_y, float max_y,
                          float z, bool front_facing) {
      std::array vertices{
          quader_poly::add_vertex(document, {min_x, min_y, z}),
          quader_poly::add_vertex(document, {max_x, min_y, z}),
          quader_poly::add_vertex(document, {max_x, max_y, z}),
          quader_poly::add_vertex(document, {min_x, max_y, z})};
      if (!front_facing) std::swap(vertices[1], vertices[3]);
      (void)quader_poly::add_face(document, vertices);
    };
    // Back-facing source surfaces leave both overlays visible at the shared
    // pixels. Face 3 keeps faces 1 and 2 non-active without entering the image.
    quad(-1.0f, 0.3f, -0.65f, 0.65f, 0.0f, false);
    quad(-0.3f, 1.0f, -0.65f, 0.65f, 0.0f, false);
    quad(10.0f, 11.0f, 0.0f, 0.5f, 0.0f, false);
    // This unselected opaque surface must still block every tint behind it.
    quad(-0.2f, 0.2f, 0.2f, 0.5f, 0.25f, true);
    const auto object = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Overlapping component face fill regression"});
    if (!object.ok() || !editor.selection().set_mode(QuaderSelectionMode::Face).ok()) {
      return false;
    }
    face_fill_object_ = object.value();
    if (!camera_.restore_perspective_pose({
            .eye = {0.0f, 0.0f, 3.0f}, .target = {}, .up = {0.0f, 1.0f, 0.0f}})) {
      return false;
    }
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  bool set_overlapping_face_selection(const std::vector<QuaderElementId> &faces,
                                      QuaderElementId active, bool hover_first = false) {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    if (!editor.selection().clear_hover().ok() ||
        !editor.selection().select_components({
            .object = face_fill_object_,
            .kind = QuaderComponentKind::Face,
            .faces = faces,
            .active = {.object = face_fill_object_,
                       .kind = QuaderComponentKind::Face,
                       .face = active},
        }).ok()) return false;
    return !hover_first || editor.selection().hover_face(face_fill_object_, 1).ok();
  }

  QVec2 overlapping_face_fill_probe_pixel(bool occluded) const {
    const QVec3 world = occluded ? QVec3{0.0f, 0.35f, 0.25f}
                                : QVec3{0.0f, -0.25f, 0.0f};
    auto pixel = camera_.project_world_point(world).position;
    pixel.y = static_cast<float>(view_.height() - 1u) - pixel.y;
    return pixel;
  }

  void resize_face_fill_sample(std::uint32_t width, std::uint32_t height) {
    camera_.set_viewport_size(width, height);
    view_.resize(width, height);
    view_.update_camera(camera_);
  }

  bool pan_face_fill_camera(float x) {
    if (!camera_.restore_perspective_pose({
            .eye = {x, 0.0f, 3.0f}, .target = {x, 0.0f, 0.0f},
            .up = {0.0f, 1.0f, 0.0f}})) return false;
    view_.update_camera(camera_);
    return true;
  }

  ModelingToolRayEvent edge_loop_event(float y) const {
    const auto projected = camera_.project_world_point({1.0f, y, 0.0f});
    const auto ray = camera_.ray_from_viewport_point(projected.position);
    return {
        .screen_position = projected.position,
        .ray = {.origin = ray.origin, .direction = ray.direction},
        .camera = ViewportCameraAdapter::modeling_camera(camera_, {kWidth, kHeight}),
        .grid_size = 0.5,
        .edge_radius = 0.05,
        .snap_enabled = true,
    };
  }

  std::array<QVec2, 3> edge_loop_pixels(float y) const {
    std::array<QVec2, 3> pixels;
    // Probe the two expected split-edge vertices and the connector midpoint.
    // These positions come from the fixture, independently of retained frames.
    for (std::size_t index = 0; index < pixels.size(); ++index) {
      const float x = index == 0 ? -1.0f : index == 1 ? 1.0f : 0.0f;
      pixels[index] = camera_.project_world_point({x, y, 0.0f}).position;
      pixels[index].y = static_cast<float>(kHeight - 1u) - pixels[index].y;
    }
    return pixels;
  }

  bool present_edge_loop_sample(float scale = 1.0f) {
    return present_component_sample(ViewportOverlaySelectionContext::Edge, scale);
  }

  bool create_component_transform_sample(ViewportOverlaySelectionContext context) {
    auto &editor = array_editor();
    quader_poly::Document document;
    const std::array front{
        quader_poly::add_vertex(document, {-1.2f, -0.6f, 0.0f}),
        quader_poly::add_vertex(document, {-0.6f, -0.6f, 0.0f}),
        quader_poly::add_vertex(document, {-0.6f, 0.6f, 0.0f}),
        quader_poly::add_vertex(document, {-1.2f, 0.6f, 0.0f})};
    const auto face = quader_poly::add_face(document, front);
    const std::array rear{
        quader_poly::add_vertex(document, {-1.4f, -0.25f, -0.3f}),
        quader_poly::add_vertex(document, {1.4f, -0.25f, -0.3f}),
        quader_poly::add_vertex(document, {1.4f, 0.25f, -0.3f}),
        quader_poly::add_vertex(document, {-1.4f, 0.25f, -0.3f})};
    (void)quader_poly::add_face(document, rear);
    const auto object = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Retained component transform pixel regression"});
    if (!object.ok()) return false;
    QuaderComponentSelectionArgs selection{.object = object.value()};
    QuaderSelectionMode mode = QuaderSelectionMode::Face;
    if (context == ViewportOverlaySelectionContext::Vertex) {
      mode = QuaderSelectionMode::Vertex;
      selection.kind = QuaderComponentKind::Vertex;
      selection.vertices.assign(front.begin(), front.end());
    } else if (context == ViewportOverlaySelectionContext::Edge) {
      mode = QuaderSelectionMode::Edge;
      selection.kind = QuaderComponentKind::Edge;
      selection.edges = {{front[0], front[3]}, {front[1], front[2]}};
    } else {
      selection.kind = QuaderComponentKind::Face;
      selection.faces = {static_cast<QuaderElementId>(face)};
    }
    if (!editor.selection().set_mode(mode).ok() ||
        !editor.selection().select_components(selection).ok() ||
        !camera_.restore_perspective_pose({
            .eye = {0.0f, 0.0f, 4.0f}, .target = {}, .up = {0.0f, 1.0f, 0.0f}})) {
      return false;
    }
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  bool present_component_transform_sample(ViewportOverlaySelectionContext context,
                                          const ViewportSettings &settings) {
    return present_component_sample(context, 1.0f, settings, true);
  }

  OverlayRenderableCacheStats component_overlay_stats() const {
    return bridge_.overlay_stats();
  }

  bool present_component_sample(ViewportOverlaySelectionContext context,
                                float scale = 1.0f,
                                const ViewportSettings &settings = ViewportSettings::defaults(),
                                bool sync_preview = false) {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    if (!QuaderRenderApiRuntimeAccess::sync_scene(editor.render(), bridge_).ok()) {
      return false;
    }
    if (sync_preview && !QuaderRenderApiRuntimeAccess::sync_tool_previews(
                            array_editor().render(), bridge_).ok()) {
      return false;
    }
    const auto &modeling = session_.modeling();
    const auto volatile_overlay = modeling.volatile_overlay_snapshot();
    last_submission_ = bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u),
         .raster_scale = scale,
         .camera = ViewportCameraAdapter::overlay_camera(
             camera_, {static_cast<float>(view_.width()), static_cast<float>(view_.height())}),
         .selection_source = {
             .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
             .content_revision = modeling.content_revision(),
             .selection_revision = modeling.selection_revision(),
             .scene_revision = session_.scene().revision(),
             .asset_revision = session_.assets().revision(),
             .overlay_signature = modeling.selection_overlay_signature(),
             .selection_context = context},
         .selection_frame_provider = this,
         .volatile_modeling_overlay_frame = volatile_overlay.frame.get(),
         .volatile_modeling_source = {
             .source_identity = volatile_overlay.source.source_identity,
             .publication_revision = volatile_overlay.source.publication_revision},
         .scene_selection_mode = RenderOverlaySelectionMode::Component,
         .style_selection_context = context,
         .overlays_visible = true,
         .selection_dimensions_visible = false},
        settings);
    return true;
  }

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

  bool create_array_wire_sample(ViewportToolId tool) {
    auto &editor = array_editor();
    const auto object = editor.modeling().create_box(
        {.min = {-0.25f, -0.25f, -0.25f}, .max = {0.25f, 0.25f, 0.25f}},
        "Array wire preview source");
    if (!object.ok() || !editor.selection().select_object(object.value()).ok()) return false;
    if (tool == ViewportToolId::PathArray) {
      const auto path = editor.modeling().create_curve({
          .points = {{.position = {}}, {.position = {4.0f, 0.0f, 0.0f}}},
          .name = "Array wire preview path"});
      if (!path.ok() || !editor.selection().select_object(path.value()).ok() ||
          !editor.tools().activate(tool).ok() ||
          !session_.path_array().select_source(object.value().entity).ok) return false;
    } else {
      if (!editor.tools().activate(tool).ok()) return false;
      if (tool == ViewportToolId::LinearArray &&
          !session_.linear_array().set_endpoint({4.0f, 0.0f, 0.0f}, false, 1.0).ok) return false;
      if (tool == ViewportToolId::RadialArray &&
          (!session_.radial_array().update_center_candidate(
               {-2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 1.0).ok ||
           !session_.radial_array().lock_center().ok)) return false;
    }
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.focus_bounds({-5.0f, -4.0f, -1.0f}, {5.0f, 4.0f, 1.0f}, 1.25f);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    view_.set_scene_visible(false);
    view_.set_overlay_visible(should_enable_viewport_overlay_pass({
        .overlays_visible = false, .gizmos_visible = false, .wireframe_pane = true}));
    return true;
  }

  bool present_array_wire_sample(std::uint8_t wire_panes, float scale = 1.0f) {
    auto &editor = array_editor();
    if (!QuaderRenderApiRuntimeAccess::sync_scene(editor.render(), bridge_).ok() ||
        !QuaderRenderApiRuntimeAccess::sync_tool_previews(
            editor.render(), bridge_, {}, [&](RenderToolPreviewSceneSnapshot preview) {
              if (!preview.meshes.empty() && !preview.meshes.front().instance_transforms.empty()) {
                // Probe an authored box edge, independently of the wire snapshot.
                array_probe_world_ = transform_point(
                    preview.meshes.front().instance_transforms.back(), {-0.25f, 0.0f, 0.25f});
              }
              return bridge_.set_tool_preview_scene(std::move(preview));
            }).ok()) return false;
    const auto style = ViewportOverlayStyleResolver::persistent_style_snapshot(
        ViewportSettings::defaults(), scale);
    ViewportPersistentOverlayRasterGroupSet groups;
    if (!groups.add(viewport_grid_layer_mask(0u), style) ||
        !groups.add(viewport_grid_layer_mask(1u), style)) return false;
    return QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays(
        editor.render(), bridge_, active_snapshots_,
        {.scene_wireframe_layer_mask = wire_panes,
         .selected_scene_wire_visible = false,
         .selected_mesh_outline_only = true,
         .style = style, .raster_groups = groups}).ok();
  }

  QVec2 array_wire_probe_pixel() const {
    auto pixel = camera_.project_world_point(array_probe_world_).position;
    pixel.y = static_cast<float>(view_.height() - 1u) - pixel.y;
    return pixel;
  }

  SceneRenderResourceDiagnostics render_diagnostics() const { return bridge_.diagnostics(); }

  bool reject_array_wire_candidate() {
    return bridge_.set_tool_preview_scene({.complete = false}) == ToolPreviewSubmissionResult::Failed;
  }

  bool change_array_wire_count() {
    auto &editor = array_editor();
    const auto tool = editor.tools().active_tool();
    return editor.tools().set_setting(tool,
        tool == "radial_array" ? "radial_array.copies" : tool + ".count", 6.0).ok();
  }

  bool cancel_array_wire_sample() {
    auto &editor = array_editor();
    return editor.tools().cancel_active_tool_preview().ok();
  }

  void retire_array_scene_binding() { bridge_.bind_scene({.scene_session_id = 999u}); }

  bool set_array_dimensions_visible(bool visible) {
    auto &editor = array_editor();
    const auto tool = editor.tools().active_tool();
    return editor.tools().set_setting(tool, tool + ".show_bounding_box_dimensions", visible).ok();
  }

  bool configure_array_dimensions_camera() {
    if (!camera_.restore_perspective_pose({
            .eye = {7.0f, 8.0f, 14.0f}, .target = {}, .up = {0.0f, 1.0f, 0.0f}})) return false;
    camera_.focus_bounds({-5.0f, -4.0f, -1.0f}, {5.0f, 4.0f, 1.0f}, 1.1f);
    view_.update_camera(camera_);
    return true;
  }

  bool present_array_dimensions_sample(bool wireframe, bool overlays = true,
                                       float scale = 1.0f) {
    if (!present_array_wire_sample(wireframe ? viewport_grid_layer_mask(0u) : 0u, scale)) return false;
    view_.set_scene_visible(!wireframe);
    auto &editor = array_editor();
    const auto array_visible = editor.tools().active_array_bounding_box_dimensions();
    const auto &scene = QuaderRenderApiRuntimeAccess::build_scene_overlay_snapshot(
        editor.render(), active_snapshots_);
    const auto &modeling = session_.modeling();
    last_submission_ = bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u),
         .raster_scale = scale,
         .camera = ViewportCameraAdapter::overlay_camera(camera_, {kWidth, kHeight}),
         .selection_source = {
             .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
             .content_revision = modeling.content_revision(),
             .selection_revision = modeling.selection_revision(),
             .scene_revision = session_.scene().revision(),
             .asset_revision = session_.assets().revision(),
             .overlay_signature = modeling.selection_overlay_signature()},
         .selection_frame_provider = this,
         .scene_overlay_snapshot = &scene,
         .overlays_visible = overlays,
         .scene_overlay_requested = overlays,
         .wireframe_pane = wireframe,
         .selection_dimensions_visible = false,
         .array_dimensions_mode = !array_visible ? ViewportArrayDimensionsMode::Inactive
             : *array_visible ? ViewportArrayDimensionsMode::Visible : ViewportArrayDimensionsMode::Hidden},
        ViewportSettings::defaults());
    return true;
  }

  bool configure_linear_guide_sample(LinearArrayAxisConstraint axis,
                                     bool orthographic) {
    auto &tool = session_.linear_array();
    if (!tool.set_axis_constraint(LinearArrayAxisConstraint::Free).ok ||
        !tool.set_endpoint({2.0f, 5.0f, 1.0f}, false, 1.0).ok ||
        !tool.set_axis_constraint(axis).ok) return false;
    if (orthographic) {
      camera_.view_axis(axis == LinearArrayAxisConstraint::Z
                            ? ViewportViewAxis::Y : ViewportViewAxis::Z, 1);
    } else if (!camera_.restore_perspective_pose({
                   .eye = {10.0f, 11.0f, 13.0f}, .target = {2.0f, 5.0f, 1.0f},
                   .up = {0.0f, 1.0f, 0.0f}})) {
      return false;
    }
    camera_.focus_bounds({-3.0f, 0.0f, -4.0f}, {7.0f, 10.0f, 6.0f}, 1.25f);
    view_.update_camera(camera_);
    return true;
  }

  void present_linear_guide_sample(float scale) {
    const auto handle = session_.linear_array().handle_state();
    const auto overlay_camera = ViewportCameraAdapter::overlay_camera(
        camera_, {static_cast<float>(view_.width()), static_cast<float>(view_.height())});
    const ViewportToolOverlaySource source({
        .source_identity = reinterpret_cast<std::uintptr_t>(this),
        .raster_scale = scale,
        .overlay_camera = &overlay_camera,
        .viewport_camera = &camera_,
        .linear_array_handle = &handle,
        .interaction_pane = true,
        .gizmos_visible = true,
        .overlays_visible = true});
    last_submission_ = bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u),
         .raster_scale = scale,
         .camera = overlay_camera,
         .dynamic_tool_source = source.source_stamp(),
         .dynamic_tool_frame_provider = &source,
         .overlays_visible = true,
         .selection_dimensions_visible = false}, ViewportSettings::defaults());
  }

  std::array<QVec2, 2> linear_guide_probe_pixels(LinearArrayAxisConstraint axis) const {
    const QVec3 direction = axis == LinearArrayAxisConstraint::X ? QVec3{1, 0, 0}
        : axis == LinearArrayAxisConstraint::Y ? QVec3{0, 1, 0} : QVec3{0, 0, 1};
    std::array<QVec2, 2> pixels;
    for (std::size_t index = 0u; index < pixels.size(); ++index) {
      // Both samples lie beyond the endpoint marker and away from the A-to-B
      // segment. A short colored span cannot satisfy these guide probes.
      const QVec3 world = QVec3{2.0f, 5.0f, 1.0f} + direction * (index == 0u ? -3.0f : 3.0f);
      pixels[index] = camera_.project_world_point(world).position;
      pixels[index].y = static_cast<float>(view_.height() - 1u) - pixels[index].y;
    }
    return pixels;
  }

  bool clear_linear_guide_constraint() {
    return session_.linear_array().set_axis_constraint(LinearArrayAxisConstraint::Free).ok;
  }

  void pan_linear_guide_camera() {
    camera_.begin_drag({320.0f, 240.0f}, ViewportDragMode::Pan);
    camera_.update_drag({351.0f, 261.0f});
    camera_.end_drag();
    view_.update_camera(camera_);
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

  bool create_active_selection_sample(bool groups, bool instances = false) {
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
      } else if (instances) {
        if (!editor.selection().select_object(object.value()).ok()) return false;
        const auto instance = editor.modeling().create_instance_from_selection();
        if (!instance.ok()) return false;
        active_entities_[index] = instance.value().occurrence.root.id;
      }
    }
    if (!editor.selection().set_mode(groups ? QuaderSelectionMode::Group : QuaderSelectionMode::Object).ok()) return false;
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.focus_bounds({-2, -1, -0.5f}, {2, 1, 0.5f}, 1.5f);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  std::array<QVec2, 2> active_face_pixels() const {
    std::array<QVec2, 2> pixels;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
      const float center = index == 0 ? -1.25f : 1.25f;
      pixels[index] = camera_.project_world_point({center, 0.0f, 0.4f}).position;
      pixels[index].y = static_cast<float>(kHeight - 1u) - pixels[index].y;
    }
    return pixels;
  }

  bool present_active_selection_sample(bool groups, std::size_t active, float scale,
                                       bool selected = true) {
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session_));
    const auto selection = selected
        ? editor.selection().select_objects(active_entities_, active_entities_[active])
        : editor.selection().clear();
    if (!selection.ok() ||
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
    const bool gpu_outlines = view_.update_selected_mesh_outlines(
        active_snapshots_.selected_object_snapshot(), &scene, style, true);
    last_submission_ = bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u),
         .raster_scale = scale,
         .camera = ViewportCameraAdapter::overlay_camera(camera_, {kWidth, kHeight}),
         .scene_overlay_snapshot = &scene,
         .selected_mesh_outline_snapshot = active_snapshots_.selected_object_snapshot(),
         .gpu_mesh_outlines = gpu_outlines,
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

  void sync_selected_object_snapshot(
      const RenderOverlaySelectedObjectSnapshot &snapshot) {
    bridge_.sync_scene_asset_overlays(snapshot, {}, {});
  }

  void sync_render_scene(const SceneSession &session) {
    bridge_.sync(session.scene(), session.assets());
  }

  bool present_mesh_outline_snapshot(const RenderOverlaySelectedObjectSnapshot &snapshot,
                                     float scale = 1.0f, bool gpu = true,
                                     bool visible = true) {
    const auto settings = ViewportSettings::defaults();
    const auto style = ViewportOverlayStyleResolver::persistent_style_snapshot(settings, scale);
    if (!view_.update_selected_mesh_outlines(&snapshot, nullptr, style, gpu && visible)) return false;
    last_submission_ = bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u),
         .raster_scale = scale,
         .camera = ViewportCameraAdapter::overlay_camera(camera_, {kWidth, kHeight}),
         .selected_mesh_outline_snapshot = &snapshot,
         .gpu_mesh_outlines = gpu,
         .scene_selection_mode = RenderOverlaySelectionMode::Object,
         .style_selection_context = ViewportOverlaySelectionContext::Object,
         .overlays_visible = visible,
         .selection_dimensions_visible = false}, settings);
    return true;
  }

  SelectedMeshOutlineMaskWorkStats mesh_outline_work() const {
    return mesh_outline_masks_.work_stats();
  }

  bool render_one_frame() {
    if (!begin_frame()) return false;
    view_.render(*renderer_);
    renderer_->endFrame();
    return true;
  }

  std::size_t renderable_count() const {
    return scene_->getRenderableCount();
  }

  void set_sample_camera(const ViewportCameraController &camera) {
    camera_ = camera;
    camera_.set_viewport_size(kWidth, kHeight);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
  }

  bool finish_gpu_work() {
    return engine_->flushAndWait(5'000'000'000ULL);
  }

  bool material_lifetime_validation_enabled() const {
    return engine_->getFeatureFlag("engine.debug.assert_material_instance_in_use")
        .value_or(false);
  }

  ViewportOverlayFrame build_selection_overlay_frame() const override {
    ++selection_frame_builds_;
    return session_.modeling().selection_overlay_render_frame();
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

  QImage capture(bool warm_up = true) {
    const auto destroy_target = [this](filament::RenderTarget *target) {
      if (target != nullptr) engine_->destroy(target);
    };
    std::unique_ptr<filament::RenderTarget, decltype(destroy_target)>
        texture_read_target(nullptr, destroy_target);
    if (surface_ == ViewportRenderSurface::Texture) {
      auto *texture = registry_.texture(view_.overlay_texture_handle());
      if (texture == nullptr) return {};
      texture_read_target.reset(filament::RenderTarget::Builder()
                                    .texture(filament::RenderTarget::AttachmentPoint::COLOR, texture)
                                    .build(*engine_));
      if (!texture_read_target) return {};
    }
    for (int frame = 0; frame < (warm_up ? 5 : 0); ++frame) {
      if (!begin_frame()) return {};
      view_.render(*renderer_);
      renderer_->endFrame();
    }
    if (!begin_frame()) return {};
    view_.render(*renderer_);
    auto readback = std::make_shared<Readback>();
    const auto width = view_.width();
    const auto height = view_.height();
    readback->bytes.resize(static_cast<std::size_t>(width) * height * 4u);
    auto *owner = new std::shared_ptr<Readback>(readback);
    filament::backend::PixelBufferDescriptor descriptor(
            readback->bytes.data(), readback->bytes.size(),
            filament::backend::PixelDataFormat::RGBA,
            filament::backend::PixelDataType::UBYTE,
            [](void *, std::size_t, void *user) {
              auto *holder = static_cast<std::shared_ptr<Readback> *>(user);
              (*holder)->complete.store(true, std::memory_order_release);
              delete holder;
            }, owner);
    if (texture_read_target) {
      renderer_->readPixels(texture_read_target.get(), 0, 0, width, height,
                            std::move(descriptor));
    } else {
      renderer_->readPixels(0, 0, width, height, std::move(descriptor));
    }
    renderer_->endFrame();
    for (int attempt = 0; attempt < 10 &&
         !readback->complete.load(std::memory_order_acquire); ++attempt) {
      if (!engine_->flushAndWait(1'000'000'000ULL)) return {};
      engine_->pumpMessageQueues();
    }
    if (!readback->complete.load(std::memory_order_acquire)) return {};
    return QImage(readback->bytes.data(), width, height,
                   QImage::Format_RGBA8888).mirrored(false, true);
  }

private:
  void initialize_point_audit() {
    if (point_audit_factory_.initialized()) return;
    point_audit_factory_.initialize(
        *engine_, *scene_, materials_.vertex_color_overlay(),
        materials_.overlay_line(), materials_.edit_wire_line(),
        materials_.overlay_point(), materials_.overlay_icon(),
        std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()));
    point_audit_cache_.initialize(point_audit_factory_);
  }

  QuaderEditorApi &array_editor() {
    // Match the viewport's retained facade: recreating it every frame discards
    // QuaderRenderApi's source stamps and republishes an unchanged preview.
    if (!array_editor_) {
      array_editor_ = std::make_unique<QuaderEditorApi>(
          QuaderApiSessionInternalAccess::from_scene_session(session_));
    }
    return *array_editor_;
  }

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
    std::vector<unsigned char> bytes;
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
  std::unique_ptr<QuaderEditorApi> array_editor_;
  ViewportRenderSurface surface_ = ViewportRenderSurface::Swapchain;
  QuaderModelingObjectRef face_fill_object_;
  QuaderModelingObjectRef tool_topology_object_;
  ViewportToolId tool_topology_tool_ = ViewportToolId::Select;
  std::array<EntityId, 2> active_entities_{};
  QVec3 array_probe_world_;
  RenderOverlaySnapshotCache active_snapshots_;
  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Renderer *renderer_ = nullptr;
  filament::SwapChain *swapchain_ = nullptr;
  filament::Texture *solid_color_texture_ = nullptr;
  FilamentMaterialLibrary materials_;
  TextureRegistry registry_;
  SceneRenderBridge bridge_;
  OverlayProxyFactory point_audit_factory_;
  OverlayRenderableCache point_audit_cache_;
  SelectedMeshOutlineMaskCache mesh_outline_masks_;
  ViewportRenderer view_;
  ViewportCameraController camera_;
  SelectionRuntimeFrame selected_vertex_frame_;
  SelectionRuntimeFrame overlay_audit_frame_;
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
     VulkanMeshMaskOutlinesPreserveUnionOcclusionActiveStyleAndVisibility) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-mesh-mask-outline-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const QString output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!output.isEmpty()) ASSERT_TRUE(QDir().mkpath(output));
  const auto color_delta = [](const QImage &image, const QImage &base, QRect region) {
    long long total = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
      for (int x = region.left(); x <= region.right(); ++x) {
        const QColor pixel = image.pixelColor(x, y), background = base.pixelColor(x, y);
        total += std::abs(pixel.red() - background.red()) +
            std::abs(pixel.green() - background.green()) + std::abs(pixel.blue() - background.blue());
      }
    }
    return total;
  };
  for (const auto surface : {ViewportRenderSurface::Swapchain, ViewportRenderSurface::Texture}) {
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(-0.02f, true, surface));
    for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
      SCOPED_TRACE(static_cast<int>(surface));
      SCOPED_TRACE(static_cast<int>(projection));
      ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, projection));
      RenderOverlaySelectedObjectSnapshot snapshot;
      snapshot.selection_revision = 1u;
      snapshot.selected_mesh_outline_only = true;
      ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot));
      const QImage background = harness.capture(false);
      ASSERT_FALSE(background.isNull());
      std::array<std::array<MeshVertex, 4u>, 6u> vertices;
      const std::array<std::uint32_t, 6u> indices{0u, 1u, 2u, 0u, 2u, 3u};
      const auto append_quad = [&](std::size_t index, QRect rectangle, float z, bool active) {
        const std::array<QVec2, 4u> pixels{{
            {float(rectangle.left()), float(rectangle.top())},
            {float(rectangle.right()), float(rectangle.top())},
            {float(rectangle.right()), float(rectangle.bottom())},
            {float(rectangle.left()), float(rectangle.bottom())}}};
        for (std::size_t corner = 0; corner < pixels.size(); ++corner) {
          vertices[index][corner].position = harness.overlay_audit_world_point(pixels[corner], z);
        }
        snapshot.meshes.push_back({
            .entity = static_cast<EntityId>(index + 1), .mesh_id = static_cast<AssetId>(index + 1),
            .mesh_revision = 1u, .selected = true, .active_selected = active,
            .vertices = vertices[index], .indices = indices});
      };
      append_quad(0u, {70, 120, 91, 121}, 0.1f, false);
      append_quad(1u, {120, 120, 131, 121}, 0.1f, false);
      append_quad(2u, {70, 300, 181, 121}, -0.1f, false);
      append_quad(3u, {400, 120, 161, 121}, 0.1f, true);
      append_quad(4u, {400, 300, 161, 121}, 0.1f, false);
      append_quad(5u, {400, 300, 161, 121}, -0.1f, true);
      ++snapshot.selection_revision;
      QImage initial;
      for (const float scale : {1.0f, 2.0f, 1.0f}) {
        ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot, scale));
        // The first rendered frame after target allocation or style changes
        // must already match the settled presentation; warmups hide stale masks.
        const QImage image = harness.capture(false);
        ASSERT_FALSE(image.isNull());
        EXPECT_EQ(harness.capture(), image);
        const auto visible = color_delta(image, background, {63, 145, 15, 60});
        const auto hidden = color_delta(image, background, {63, 325, 15, 60});
        EXPECT_GT(visible, 0);
        EXPECT_GT(hidden, 0) << "Occluded selected silhouettes retain their X-ray companion.";
        EXPECT_LT(hidden, visible) << "Occluded silhouettes use reduced alpha.";
        const QColor ordinary_boundary = image.pixelColor(70, 180);
        const QColor active_boundary = image.pixelColor(400, 180);
        EXPECT_GT(active_boundary.red() + active_boundary.green() + active_boundary.blue(),
                  ordinary_boundary.red() + ordinary_boundary.green() + ordinary_boundary.blue())
            << "The active selected boundary remains brighter over the same backdrop.";
        for (const int offset : {-1, 0, 1}) {
          const QColor ordinary = image.pixelColor(70 + offset, 180);
          const QColor ordinary_over_active = image.pixelColor(400 + offset, 360);
          EXPECT_NEAR(ordinary.red(), ordinary_over_active.red(), 1)
              << "An aligned active silhouette behind a visible ordinary one must not replace its boundary.";
          EXPECT_NEAR(ordinary.green(), ordinary_over_active.green(), 1);
          EXPECT_NEAR(ordinary.blue(), ordinary_over_active.blue(), 1);
        }
        EXPECT_EQ(color_delta(image, background, {113, 145, 15, 60}), 0)
            << "An overlapping selected quad must not leave an internal boundary.";
        EXPECT_EQ(color_delta(image, background, {153, 145, 15, 60}), 0);
        if (scale == 1.0f) {
          if (initial.isNull()) initial = image;
          else EXPECT_EQ(initial, image) << "Scale-down restores the exact retained presentation.";
        }
        const auto work = harness.mesh_outline_work();
        ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot, scale));
        EXPECT_EQ(harness.mesh_outline_work().geometry_builds, work.geometry_builds);
        EXPECT_EQ(harness.mesh_outline_work().uploaded_vertices, work.uploaded_vertices);
        EXPECT_EQ(harness.capture(false), image);
        ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot, scale, true, false));
        EXPECT_EQ(harness.capture(false), background);
        EXPECT_EQ(harness.mesh_outline_work().geometry_builds, work.geometry_builds);
        ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot, scale));
        EXPECT_EQ(harness.capture(false), image);
        if (!output.isEmpty()) {
          ASSERT_TRUE(image.save(QDir(output).filePath(QStringLiteral("mesh-mask-%1-%2-%3.png")
              .arg(static_cast<int>(surface)).arg(static_cast<int>(projection)).arg(int(scale * 100)))));
        }
      }
      snapshot.meshes.clear();
      ++snapshot.selection_revision;
      ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot));
      EXPECT_EQ(harness.capture(false), background) << "Deselecting clears both retained mask targets.";
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanSlopedMeshInnerOutlineUsesItsOwnSurfaceDepth) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-sloped-outline-depth-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, projection));
    std::array<std::array<MeshVertex, 4u>, 2u> vertices;
    const std::array<std::uint32_t, 6u> indices{0u, 2u, 1u, 0u, 3u, 2u};
    RenderOverlaySelectedObjectSnapshot snapshot;
    snapshot.selection_revision = 1u;
    snapshot.selected_mesh_outline_only = true;
    for (std::size_t index = 0u; index < vertices.size(); ++index) {
      const float left = index == 0u ? 80.0f : 370.0f;
      const std::array<QVec2, 4u> pixels{{
          {left, 100.0f}, {left + 190.0f, 100.0f},
          {left + 190.0f, 300.0f}, {left, 300.0f}}};
      for (std::size_t corner = 0u; corner < pixels.size(); ++corner) {
        const float z = index == 0u ? (corner == 0u || corner == 3u ? -1.0f : 1.0f) : 0.0f;
        vertices[index][corner].position = harness.overlay_audit_world_point(pixels[corner], z);
      }
      ASSERT_TRUE(harness.create_outline_depth_quad(vertices[index]));
      snapshot.meshes.push_back({
          .entity = static_cast<EntityId>(index + 1u), .mesh_id = static_cast<AssetId>(index + 1u),
          .mesh_revision = 1u, .selected = true, .vertices = vertices[index], .indices = indices});
    }
    ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot, 1.0f, true, false));
    const QImage background = harness.capture();
    ASSERT_FALSE(background.isNull());
    ASSERT_NE(background.pixelColor(180, 180), background.pixelColor(320, 400));
    ASSERT_NE(background.pixelColor(470, 180), background.pixelColor(320, 400));
    for (const float scale : {1.0f, 2.0f}) {
      SCOPED_TRACE(static_cast<int>(projection));
      SCOPED_TRACE(scale);
      ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot, scale));
      const QImage image = harness.capture();
      ASSERT_FALSE(image.isNull());
      int reference_signal = 0;
      for (const int y : {140, 180, 220, 260}) {
        for (const int offset : {0, 1}) {
          const QColor sloped = image.pixelColor(80 + offset, y);
          const QColor flat = image.pixelColor(370 + offset, y);
          const QColor sloped_base = background.pixelColor(80 + offset, y);
          const QColor flat_base = background.pixelColor(370 + offset, y);
          if (offset == 0) {
            // This pixel has complete ribbon coverage and the authored ordinary
            // color has a fully opaque red channel. Matching two half-alpha
            // results must not make a broken depth comparison pass this test.
            EXPECT_NEAR(flat.red(), 255, 2)
                << "The flat reference must use the full-alpha visible pass.";
          }
          reference_signal += std::abs(flat.red() - flat_base.red()) +
              std::abs(flat.green() - flat_base.green()) + std::abs(flat.blue() - flat_base.blue());
          EXPECT_NEAR(sloped.red() - sloped_base.red(), flat.red() - flat_base.red(), 2)
              << "A sloped surface must not classify its own inner outline as occluded.";
          EXPECT_NEAR(sloped.green() - sloped_base.green(), flat.green() - flat_base.green(), 2);
          EXPECT_NEAR(sloped.blue() - sloped_base.blue(), flat.blue() - flat_base.blue(), 2);
        }
      }
      EXPECT_GT(reference_signal, 0) << "The probes must actually cover the visible outline.";
      const QString output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
      if (!output.isEmpty()) {
        ASSERT_TRUE(QDir().mkpath(output));
        ASSERT_TRUE(image.save(QDir(output).filePath(QStringLiteral("sloped-mesh-outline-%1-%2.png")
            .arg(static_cast<int>(projection)).arg(int(scale * 100)))));
      }
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanSixThousandSelectedMeshesSubmitAndRetireWithoutCommandOverflow) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-large-selection-command-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  std::unique_ptr<QTemporaryDir> copied_directory;
  std::unique_ptr<ProjectSession> copied_project;
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.prepare_overlay_audit_camera(
      5.0f, ViewportProjection::Perspective));
  const std::array<MeshVertex, 3> vertices{{
      {.position = {-0.02f, -0.02f, 0.0f}},
      {.position = {0.02f, -0.02f, 0.0f}},
      {.position = {0.0f, 0.02f, 0.0f}},
  }};
  const std::array<std::uint32_t, 3> indices{0u, 1u, 2u};
  constexpr std::size_t kMeshCount = 6400u;
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.selection_revision = 1u;
  snapshot.style = ViewportOverlayStyleResolver::persistent_style_snapshot(
      ViewportSettings::defaults(), 1.0f);
  snapshot.meshes.reserve(kMeshCount);
  for (std::size_t index = 0u; index < kMeshCount; ++index) {
    QMat4 transform;
    transform.values[12] = (static_cast<float>(index % 80u) - 39.5f) * 0.05f;
    transform.values[13] = (static_cast<float>(index / 80u) - 39.5f) * 0.05f;
    snapshot.meshes.push_back({
        .entity = static_cast<EntityId>(index + 1u),
        .mesh_id = static_cast<AssetId>(index + 1u),
        .mesh_revision = 1u,
        .world_transform = transform,
        .selected = true,
        .active_selected = index == 0u,
        .vertices = vertices,
        .indices = indices,
    });
  }
  float translation = 0.025f;
  const QString source = qEnvironmentVariable("QUADER_TEST_LARGE_SELECTION_SCENE");
  if (!source.isEmpty()) {
    copied_directory = std::make_unique<QTemporaryDir>();
    ASSERT_TRUE(copied_directory->isValid());
    const std::filesystem::path root(copied_directory->path().toStdWString());
    copied_project = std::make_unique<ProjectSession>(root / "recent-scenes.json");
    copied_project->set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                                      "resources/modeling_assets");
    QuaderEditorApi editor(*copied_project);
    std::error_code error;
    std::filesystem::create_directories(root / "project", error);
    ASSERT_FALSE(error) << error.message();
    std::filesystem::path scene_path;
    const QString project_source =
        qEnvironmentVariable("QUADER_TEST_LARGE_SELECTION_PROJECT");
    if (!project_source.isEmpty()) {
      const std::filesystem::path source_manifest(project_source.toStdWString());
      const auto project_root = root / source_manifest.parent_path().filename();
      const auto relative_scene = std::filesystem::relative(
          std::filesystem::path(source.toStdWString()),
          source_manifest.parent_path(), error);
      ASSERT_FALSE(error) << error.message();
      ASSERT_FALSE(relative_scene.empty());
      ASSERT_FALSE(relative_scene.is_absolute());
      ASSERT_EQ(std::find(relative_scene.begin(), relative_scene.end(), ".."),
                relative_scene.end());
      // Keep scene UUID references valid by copying the complete authored
      // project. Every open/save performed by the fixture targets this clone.
      std::filesystem::copy(source_manifest.parent_path(), project_root,
                            std::filesystem::copy_options::recursive |
                                std::filesystem::copy_options::skip_symlinks,
                            error);
      ASSERT_FALSE(error) << error.message();
      const auto project_opened = editor.project().open(
          project_root / source_manifest.filename());
      ASSERT_TRUE(project_opened.ok()) << project_opened.error_message();
      scene_path = editor.project().root() / relative_scene;
    } else {
      const auto created = editor.project().create(root / "project");
      ASSERT_TRUE(created.ok()) << created.error_message();
      scene_path = editor.project().root() / "Scenes" / "large.qdr";
      std::filesystem::create_directories(scene_path.parent_path(), error);
      ASSERT_FALSE(error) << error.message();
      ASSERT_TRUE(std::filesystem::copy_file(
          std::filesystem::path(source.toStdWString()), scene_path, error))
          << error.message();
    }
    const auto opened = editor.scene_documents().open_native_path(scene_path);
    ASSERT_TRUE(opened.ok()) << opened.error_message();
    ASSERT_TRUE(editor.selection().clear().ok());
    const auto selected = editor.selection().select_all();
    ASSERT_TRUE(selected.ok()) << selected.error_message();
    const auto &session = *copied_project->active_scene()->session;
    snapshot = build_render_overlay_selected_object_snapshot(
        session.scene(), session.assets(), true, snapshot.style);
    ASSERT_GE(snapshot.meshes.size(), 6000u);
    ASSERT_TRUE(snapshot.instance_selection_fills.empty());
    ASSERT_TRUE(snapshot.instance_selection_wires.empty());
    const auto &camera = session.viewport_workspace().camera(ViewportPaneId::Perspective);
    harness.set_sample_camera(camera);
    translation = std::max(translation, camera.state().distance * 0.05f);
  }
  const std::size_t mesh_count = std::ranges::count_if(snapshot.meshes,
      [](const auto &mesh) {
        return mesh.has_triangle_geometry() && mesh.mesh_id != kInvalidAssetId;
      });
  const auto benchmark_snapshot = snapshot;
  const QImage background = harness.capture();
  ASSERT_FALSE(background.isNull());
  const std::size_t background_renderables = harness.renderable_count();
  const auto started = std::chrono::steady_clock::now();
  harness.sync_selected_object_snapshot(snapshot);
  const auto submitted = std::chrono::steady_clock::now();
  const std::size_t selected_renderables = harness.renderable_count();
  EXPECT_GT(selected_renderables, background_renderables);
  EXPECT_LT(selected_renderables, background_renderables + mesh_count)
      << "Selected face geometry must use bounded batches, not one proxy set per mesh.";
  // This executes beginFrame's material preparation as well as actual draws;
  // it catches the original command-stream overflow after bulk allocation.
  const QImage selected = harness.capture();
  ASSERT_FALSE(selected.isNull());
  EXPECT_NE(selected, background);
  for (int iteration = 0; iteration < 100; ++iteration) {
    harness.sync_selected_object_snapshot(snapshot);
  }
  EXPECT_EQ(harness.renderable_count(), selected_renderables);
  EXPECT_EQ(harness.capture(), selected);

  ++snapshot.transform_revision;
  for (auto &mesh : snapshot.meshes) mesh.world_transform.values[12] += translation;
  harness.sync_selected_object_snapshot(snapshot);
  const QImage moved = harness.capture();
  ASSERT_FALSE(moved.isNull());
  if (source.isEmpty()) EXPECT_NE(moved, selected);

  snapshot.meshes.clear();
  ++snapshot.selection_revision;
  const auto idle_started = std::chrono::steady_clock::now();
  ASSERT_TRUE(harness.finish_gpu_work());
  const auto retire_started = std::chrono::steady_clock::now();
  harness.sync_selected_object_snapshot(snapshot);
  const auto retired = std::chrono::steady_clock::now();
  EXPECT_EQ(harness.renderable_count(), background_renderables);
  EXPECT_EQ(harness.capture(), background);
  const QString output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!output.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(output));
    ASSERT_TRUE(selected.save(QDir(output).filePath(source.isEmpty()
        ? QStringLiteral("large-selection-synthetic.png")
        : QStringLiteral("large-selection-copied-scene.png"))));
  }
  std::cout << "Large selection Vulkan resource submission: meshes=" << mesh_count
            << ", source=" << (source.isEmpty() ? "synthetic" : "copied_scene")
            << ", create_ms="
            << std::chrono::duration<double, std::milli>(submitted - started).count()
            << ", pre_retire_gpu_wait_ms="
            << std::chrono::duration<double, std::milli>(retire_started - idle_started).count()
            << ", material_lifetime_validation="
            << harness.material_lifetime_validation_enabled()
            << ", retire_ms="
            << std::chrono::duration<double, std::milli>(retired - retire_started).count()
            << '\n';

  if (copied_project != nullptr) {
    const auto &session = *copied_project->active_scene()->session;
    const auto bounds = ViewportSceneProjection::selection_bounds(session.modeling());
    ASSERT_TRUE(bounds.has_value());
    auto framed = session.viewport_workspace().camera(ViewportPaneId::Perspective);
    framed.set_viewport_size(SelectionOverlayRuntimeHarness::kWidth, SelectionOverlayRuntimeHarness::kHeight);
    framed.focus_bounds(bounds->min, bounds->max, 1.4f);
    const auto pose = framed.pose();
    harness.sync_render_scene(session);
    harness.sync_selected_object_snapshot(benchmark_snapshot);
    constexpr std::size_t kCameraSteps = 8u;
    for (const bool gpu : {false, true}) {
      harness.set_sample_camera(session.viewport_workspace().camera(ViewportPaneId::Perspective));
      ASSERT_TRUE(harness.present_mesh_outline_snapshot(benchmark_snapshot, 1.0f, gpu));
      ASSERT_TRUE(harness.render_one_frame());
      ASSERT_TRUE(harness.finish_gpu_work());
      const auto retained_work = harness.mesh_outline_work();
      std::array<double, kCameraSteps> submission_times{}, render_times{};
      for (std::size_t step = 0u; step < kCameraSteps; ++step) {
        auto camera = framed;
        const float angle = float(step) * 2.0f * std::acos(-1.0f) / float(kCameraSteps);
        const float cosine = std::cos(angle), sine = std::sin(angle);
        const auto rotate_y = [cosine, sine](QVec3 point) {
          return QVec3{point.x * cosine + point.z * sine, point.y,
                       -point.x * sine + point.z * cosine};
        };
        if (step != 0u) {
          ASSERT_TRUE(camera.restore_perspective_pose({
              .eye = pose.target + rotate_y(pose.eye - pose.target),
              .target = pose.target, .up = rotate_y(pose.up)}));
        }
        auto start = std::chrono::steady_clock::now();
        harness.set_sample_camera(camera);
        ASSERT_TRUE(harness.present_mesh_outline_snapshot(benchmark_snapshot, 1.0f, gpu));
        submission_times[step] = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        start = std::chrono::steady_clock::now();
        ASSERT_TRUE(harness.render_one_frame());
        render_times[step] = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        ASSERT_TRUE(harness.finish_gpu_work());
        EXPECT_EQ(harness.mesh_outline_work().geometry_builds, retained_work.geometry_builds);
        EXPECT_EQ(harness.mesh_outline_work().uploaded_vertices, retained_work.uploaded_vertices);
        const auto camera_pose = camera.pose();
        std::cout << "LARGE_SELECTION_GPU_CAMERA path=" << (gpu ? "gpu_mask" : "cpu_fallback")
                  << " step=" << step << " submit_ms=" << submission_times[step]
                  << " render_cpu_ms=" << render_times[step]
                  << " eye=" << camera_pose.eye.x << ',' << camera_pose.eye.y << ',' << camera_pose.eye.z
                  << " renderables=" << harness.renderable_count() << std::endl;
      }
      const auto print_times = [gpu](const char *phase, std::array<double, kCameraSteps> samples) {
        double total = 0.0;
        for (const auto sample : samples) total += sample;
        std::sort(samples.begin(), samples.end());
        std::cout << "LARGE_SELECTION_GPU_REGIME path=" << (gpu ? "gpu_mask" : "cpu_fallback")
                  << " phase=" << phase << " samples=" << samples.size()
                  << " mean_ms=" << total / samples.size() << " median_ms=" << samples[samples.size() / 2u]
                  << " max_ms=" << samples.back() << std::endl;
      };
      print_times("submission", submission_times);
      print_times("render_cpu", render_times);
      if (!output.isEmpty()) {
        const QImage framed_selection = harness.capture();
        ASSERT_FALSE(framed_selection.isNull());
        ASSERT_TRUE(framed_selection.save(QDir(output).filePath(gpu
            ? QStringLiteral("large-selection-framed-gpu.png")
            : QStringLiteral("large-selection-framed-cpu.png"))));
      }
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanObjectSilhouetteDepthBiasAndOcclusionAt100And200Percent) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-outline-composition-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) directory = QDir(QCoreApplication::applicationDirPath())
      .absoluteFilePath(QStringLiteral("../overlay_audit_visual"));
  ASSERT_TRUE(QDir().mkpath(directory));
  // Compare with normal overlay draws over the same backdrop. This also works
  // when the readable surface's transfer function differs from its image tag.
  const auto expect_same_color = [](QColor actual, QColor reference) {
    EXPECT_NEAR(actual.red(), reference.red(), 1);
    EXPECT_NEAR(actual.green(), reference.green(), 1);
    EXPECT_NEAR(actual.blue(), reference.blue(), 1);
  };
  const auto ordinary = rgba8(245, 85, 30, 192);
  const auto active = ViewportOverlayStyleResolver::active_selection_color(ordinary);
  for (const auto surface : {ViewportRenderSurface::Swapchain, ViewportRenderSurface::Texture}) {
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(-0.02f, true, surface));
    for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
      for (const float distance : {0.15f, 2.0f, 80.0f}) {
        ASSERT_TRUE(harness.prepare_overlay_audit_camera(distance, projection));
        for (const auto &[scale, label] : std::array<std::pair<float, const char *>, 3>{{
                 {1.0f, "100"}, {2.0f, "200"}, {1.0f, "down"}}}) {
          const QString stem = QStringLiteral("silhouette-%1-%2-%3-%4")
              .arg(surface == ViewportRenderSurface::Swapchain ? "swapchain" : "texture")
              .arg(projection == ViewportProjection::Perspective ? "perspective" : "orthographic")
              .arg(distance).arg(label);
          SCOPED_TRACE(stem.toStdString());
          harness.set_overlay_audit_frame({});
          harness.present_overlay_audit_frame(scale);
          const QImage base = harness.capture();
          ASSERT_FALSE(base.isNull());
          ViewportOverlayFrame frame;
          const auto line = [&](QVec2 a, float a_z, QVec2 b, float b_z,
                                std::uint32_t color, ViewportOverlayLayer layer,
                                ViewportOverlaySource source = ViewportOverlaySource::MeshOutline) {
            frame.lines.push_back({
                .layer = layer,
                .start = harness.overlay_audit_world_point(a, a_z),
                .end = harness.overlay_audit_world_point(b, b_z),
                .color = color, .width_pixels = 4.0f,
                .style_role = ViewportOverlayStyleRole::ExplicitColor,
                .source = source});
          };
          const std::array colors{ordinary, active, rgba8(245, 85, 30), rgba8(210, 135, 245, 160)};
          for (std::size_t row = 0; row < colors.size(); ++row) {
            const float y = 80.0f + 70.0f * static_cast<float>(row);
            const auto layer = row == 3u ? ViewportOverlayLayer::GroupBounds : ViewportOverlayLayer::SelectedEdge;
            line({80, y}, 0.0f, {280, y}, 0.0f, colors[row], layer);
            line({360, y}, -0.1f, {560, y}, -0.1f, colors[row], layer);
            const auto half_alpha = (colors[row] & 0x00ffffffu) |
                                    (((colors[row] >> 24u) / 2u) << 24u);
            line({80, y + 20}, 0.0f, {280, y + 20}, 0.0f, colors[row],
                 ViewportOverlayLayer::ToolHandle, ViewportOverlaySource::Tool);
            line({360, y + 20}, 0.0f, {560, y + 20}, 0.0f, half_alpha,
                 ViewportOverlayLayer::ToolHandle, ViewportOverlaySource::Tool);
          }
          // Equal visible spans must remain equal when one has a differently
          // colored hidden silhouette underneath, including their AA fringe.
          line({80, 360}, 0.0f, {280, 360}, 0.0f, active, ViewportOverlayLayer::SelectedEdge);
          line({360, 360}, -0.1f, {560, 360}, -0.1f, ordinary, ViewportOverlayLayer::SelectedEdge);
          line({360, 360}, 0.0f, {560, 360}, 0.0f, active, ViewportOverlayLayer::SelectedEdge);
          line({80, 420}, 0.0f, {280, 420}, 0.0f, rgba8(40, 225, 255),
               ViewportOverlayLayer::SelectedEdge, ViewportOverlaySource::ComponentSelection);
          line({360, 420}, -0.1f, {560, 420}, -0.1f, rgba8(40, 225, 255),
               ViewportOverlayLayer::SelectedEdge, ViewportOverlaySource::ComponentSelection);
          // This span changes depth substantially along its screen projection.
          // Neither its near endpoint nor its interior may be pulled through the wall.
          line({80, 455}, -0.1f, {560, 455}, -2.0f, ordinary, ViewportOverlayLayer::SelectedEdge);
          harness.set_overlay_audit_frame(std::move(frame));
          harness.present_overlay_audit_frame(scale);
          const QImage image = harness.capture();
          ASSERT_FALSE(image.isNull());
          ASSERT_TRUE(image.save(QDir(directory).filePath(stem + QStringLiteral(".png"))));
          for (int row = 0; row < 4; ++row) {
            const int y = 80 + 70 * row;
            EXPECT_NE(image.pixelColor(180, y), base.pixelColor(180, y));
            expect_same_color(image.pixelColor(180, y), image.pixelColor(180, y + 20));
            expect_same_color(image.pixelColor(460, y), image.pixelColor(460, y + 20));
          }
          EXPECT_GT(image.pixelColor(180, 150).green(), image.pixelColor(180, 80).green());
          EXPECT_LT(image.pixelColor(180, 80).red(), image.pixelColor(180, 220).red());
          for (int dy = -int(4 * scale); dy <= int(4 * scale); ++dy) {
            EXPECT_EQ(image.pixelColor(180, 360 + dy), image.pixelColor(460, 360 + dy))
                << "Hidden silhouettes must not add a second tint under visible coverage.";
          }
          EXPECT_GT(cyan_pixels(image, {180, 420}), 0);
          EXPECT_EQ(image.pixelColor(460, 420), base.pixelColor(460, 420))
              << "The silhouette companion must not give component edges X-ray visibility.";
          for (const int x : {100, 320, 540}) {
            expect_same_color(image.pixelColor(x, 455), image.pixelColor(460, 100));
          }
          const auto resources = harness.render_diagnostics();
          harness.present_overlay_audit_frame(scale);
          EXPECT_FALSE(harness.last_submission().persistent_submitted);
          EXPECT_FALSE(harness.last_submission().dynamic_submitted);
          EXPECT_EQ(harness.render_diagnostics().gpu_geometry_upload_count,
                    resources.gpu_geometry_upload_count);
          EXPECT_EQ(harness.capture(), image);

          // A formerly visible span becomes entirely occluded at the same screen
          // location. A retained custom render target must clear its coverage bit
          // before the hidden companion renders the next frame.
          frame = {};
          line({80, 360}, -0.1f, {280, 360}, -0.1f, active, ViewportOverlayLayer::SelectedEdge);
          const auto half_active = (active & 0x00ffffffu) | (((active >> 24u) / 2u) << 24u);
          line({80, 380}, 0.0f, {280, 380}, 0.0f, half_active,
               ViewportOverlayLayer::ToolHandle, ViewportOverlaySource::Tool);
          harness.set_overlay_audit_frame(std::move(frame));
          harness.present_overlay_audit_frame(scale);
          const auto became_hidden = harness.capture();
          ASSERT_FALSE(became_hidden.isNull());
          expect_same_color(became_hidden.pixelColor(180, 360), became_hidden.pixelColor(180, 380));
          EXPECT_NE(became_hidden.pixelColor(180, 360), base.pixelColor(180, 360));
          ASSERT_TRUE(became_hidden.save(QDir(directory).filePath(stem + QStringLiteral("-became-hidden.png"))));
          harness.present_overlay_audit_frame(scale);
          EXPECT_FALSE(harness.last_submission().dynamic_submitted);
          EXPECT_EQ(harness.capture(), became_hidden);
        }
      }
    }
  }
}

TEST(SelectionOverlayRuntimeTests, VulkanPointVisibilityIgnoresNearClippedOccluders) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-near-clipped-vertices-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) directory = QDir(QCoreApplication::applicationDirPath())
      .absoluteFilePath(QStringLiteral("../overlay_audit_visual"));
  ASSERT_TRUE(QDir().mkpath(directory));
  for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(1.0f, projection, 0.1f));
    for (const float scale : {1.0f, 2.0f}) {
      for (const bool clipped : {true, false}) {
        const QString stem = QStringLiteral("near-clip-%1-%2-%3")
            .arg(projection == ViewportProjection::Perspective ? "perspective" : "orthographic")
            .arg(int(scale * 100)).arg(clipped ? "clipped" : "visible-occluder");
        SCOPED_TRACE(stem.toStdString());
        // The off-axis ray reaches this clipped plane beyond a ray distance of
        // 0.1, exercising near/cos(angle), not just a fixed near-ray distance.
        const float z = clipped ? 0.905f : 0.8f;
        ViewportOverlayFrame frame;
        // Closed outward stamps exercise ordinary component visibility without
        // an open sheet's ambiguous inside-out orientation classification.
        harness.append_overlay_audit_occluder(frame, z);
        for (const int x : {320, 560}) {
          frame.points.push_back({
              .layer = ViewportOverlayLayer::UnselectedVertex,
              .position = harness.overlay_audit_world_point({float(x), 240}, 0.5f),
              .color = rgba8(40, 225, 255), .size_pixels = 9.0f,
              .style_role = ViewportOverlayStyleRole::ExplicitColor,
              .source = ViewportOverlaySource::ComponentSelection,
              .element = {.object_id = 1, .vertex_id = x}});
        }
        harness.set_overlay_audit_frame(std::move(frame));
        harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
        const QImage image = harness.capture();
        ASSERT_FALSE(image.isNull());
        ASSERT_TRUE(image.save(QDir(directory).filePath(stem + QStringLiteral(".png"))));
        for (const int x : {320, 560}) {
          if (clipped) EXPECT_GE(cyan_pixels(image, {float(x), 240}), 9);
          else EXPECT_EQ(cyan_pixels(image, {float(x), 240}), 0);
        }
        harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
        EXPECT_FALSE(harness.last_submission().dynamic_submitted);
      }
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanPointClipUniformsRefreshWithUnrelatedFrameChanges) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-retained-point-clip-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    SCOPED_TRACE(projection == ViewportProjection::Perspective ? "perspective" : "orthographic");
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(1.0f, projection, 0.1f));
    ViewportOverlayFrame frame;
    harness.append_overlay_audit_occluder(frame, -0.2f);
    frame.points.push_back({
        .layer = ViewportOverlayLayer::UnselectedVertex,
        .position = harness.overlay_audit_world_point({320.0f, 240.0f}, 0.5f),
        .color = rgba8(40, 225, 255), .size_pixels = 9.0f,
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::ComponentSelection,
        .element = {.object_id = 1, .vertex_id = 0}});
    harness.set_overlay_audit_frame(frame);
    harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex, true);
    ASSERT_TRUE(harness.last_submission().persistent_submitted);
    ASSERT_TRUE(harness.last_submission().dynamic_submitted);
    const auto visible = harness.capture();
    ASSERT_FALSE(visible.isNull());
    EXPECT_GE(cyan_pixels(visible, {320.0f, 240.0f}), 9);

    ASSERT_TRUE(harness.prepare_overlay_audit_camera(1.0f, projection, 0.6f));
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::ToolHandle,
        .start = {-0.2f, -0.2f, 0.0f},
        .end = {0.2f, -0.2f, 0.0f},
        .color = rgba8(255, 80, 40),
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::Tool});
    harness.set_overlay_audit_frame(frame);
    harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex, true);
    ASSERT_TRUE(harness.last_submission().persistent_submitted);
    ASSERT_TRUE(harness.last_submission().dynamic_submitted);
    const auto clipped = harness.capture();
    ASSERT_FALSE(clipped.isNull());
    EXPECT_EQ(cyan_pixels(clipped, {320.0f, 240.0f}), 0);

    ASSERT_TRUE(harness.prepare_overlay_audit_camera(1.0f, projection, 0.1f));
    frame.lines.front().end.x += 0.1f;
    harness.set_overlay_audit_frame(frame);
    harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex, true);
    ASSERT_TRUE(harness.last_submission().persistent_submitted);
    ASSERT_TRUE(harness.last_submission().dynamic_submitted);
    const auto restored = harness.capture();
    ASSERT_FALSE(restored.isNull());
    EXPECT_GE(cyan_pixels(restored, {320.0f, 240.0f}), 9);
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanComponentPointClassesRetainGeometryAndMatchLegacyPasses) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-component-point-retention-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  for (const auto projection : {ViewportProjection::Perspective,
                                ViewportProjection::Orthographic}) {
    SCOPED_TRACE(projection == ViewportProjection::Perspective ? "perspective" : "orthographic");
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(1.0f, projection, 0.1f));
    ViewportOverlayFrame frame;
    harness.append_overlay_audit_occluder(frame, -0.2f);
    const auto point = [&](ViewportOverlayLayer layer, QVec2 pixel,
                           std::uint32_t color, int vertex, float z = 0.5f) {
      const bool selected = layer == ViewportOverlayLayer::SelectedVertex;
      return ViewportOverlayPoint{
          .layer = layer,
          .position = harness.overlay_audit_world_point(pixel, z),
          .color = color, .size_pixels = selected ? 13.0f : 9.0f,
          .style_role = ViewportOverlayStyleRole::ExplicitColor,
          .source = ViewportOverlaySource::ComponentSelection,
          .element = {.object_id = 1, .vertex_id = vertex},
          .outline_growth_pixels = selected ? 2.0f : 0.0f};
    };
    frame.points = {
        point(ViewportOverlayLayer::UnselectedVertex, {319.0f, 240.0f}, rgba8(40, 220, 255, 192), 0),
        point(ViewportOverlayLayer::SelectedVertex, {323.0f, 240.0f}, rgba8(250, 80, 40, 160), 1),
        point(ViewportOverlayLayer::SelectedVertex, {325.0f, 240.0f}, rgba8(255, 225, 80, 160), 2),
        point(ViewportOverlayLayer::UnselectedVertex, {440.0f, 240.0f}, rgba8(40, 220, 255), 3, -0.5f)};
    harness.present_retained_point_frame(frame);
    const auto initial = harness.capture();
    ASSERT_FALSE(initial.isNull());
    EXPECT_EQ(initial, harness.capture_legacy_point_frame(frame))
        << "Overlapping ordinary/active colors and outline ordering match existing passes.";
    harness.present_retained_point_frame(frame);
    const auto before_class_change = harness.retained_point_stats();
    frame.points[0].layer = ViewportOverlayLayer::SelectedVertex;
    frame.points[0].size_pixels = 13.0f;
    frame.points[0].outline_growth_pixels = 2.0f;
    frame.points[1].layer = ViewportOverlayLayer::UnselectedVertex;
    frame.points[1].size_pixels = 9.0f;
    frame.points[1].outline_growth_pixels = 0.0f;
    harness.present_retained_point_frame(frame);
    const auto reclassified = harness.retained_point_stats();
    EXPECT_EQ(reclassified.uploads.position_upload_count, before_class_change.uploads.position_upload_count);
    EXPECT_EQ(reclassified.uploads.index_upload_count, before_class_change.uploads.index_upload_count);
    EXPECT_EQ(reclassified.uploads.static_upload_count, before_class_change.uploads.static_upload_count);
    EXPECT_EQ(reclassified.point_geometry_update_count, before_class_change.point_geometry_update_count);
    EXPECT_GT(reclassified.uploads.appearance_upload_count, before_class_change.uploads.appearance_upload_count);
    const auto class_image = harness.capture();
    ASSERT_FALSE(class_image.isNull());
    EXPECT_NE(class_image, initial);
    EXPECT_EQ(class_image, harness.capture_legacy_point_frame(frame));

    harness.present_retained_point_frame(frame);
    const auto before_style = harness.retained_point_stats();
    for (auto &marker : frame.points) {
      marker.color = viewport_active_selection_color(marker.color);
      marker.size_pixels *= 2.0f;
      marker.outline_growth_pixels = *marker.outline_growth_pixels * 2.0f;
    }
    harness.present_retained_point_frame(frame);
    const auto restyled = harness.retained_point_stats();
    EXPECT_EQ(restyled.uploads.position_upload_count, before_style.uploads.position_upload_count);
    EXPECT_EQ(restyled.uploads.index_upload_count, before_style.uploads.index_upload_count);
    EXPECT_EQ(restyled.uploads.static_upload_count, before_style.uploads.static_upload_count);
    EXPECT_GT(restyled.uploads.appearance_upload_count, before_style.uploads.appearance_upload_count);
    const auto style_image = harness.capture();
    ASSERT_FALSE(style_image.isNull());
    EXPECT_NE(style_image, class_image);
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(1.4f, projection, 0.1f));
    harness.present_retained_point_frame(frame);
    const auto camera_only = harness.retained_point_stats();
    EXPECT_EQ(camera_only.uploads.position_upload_count, restyled.uploads.position_upload_count);
    EXPECT_EQ(camera_only.uploads.index_upload_count, restyled.uploads.index_upload_count);
    EXPECT_EQ(camera_only.uploads.static_upload_count, restyled.uploads.static_upload_count);
    EXPECT_EQ(camera_only.uploads.appearance_upload_count, restyled.uploads.appearance_upload_count);
    const auto camera_image = harness.capture();
    ASSERT_FALSE(camera_image.isNull());
    EXPECT_EQ(camera_image, harness.capture_legacy_point_frame(frame));

    harness.present_retained_point_frame(frame);
    const auto before_membership = harness.retained_point_stats();
    frame.points.erase(frame.points.begin() + 1);
    harness.present_retained_point_frame(frame);
    const auto changed_membership = harness.retained_point_stats();
    EXPECT_GT(changed_membership.uploads.position_upload_count, before_membership.uploads.position_upload_count);
    EXPECT_GT(changed_membership.uploads.index_upload_count, before_membership.uploads.index_upload_count);
    const auto membership_image = harness.capture();
    ASSERT_FALSE(membership_image.isNull());
    EXPECT_EQ(membership_image, harness.capture_legacy_point_frame(frame));

    harness.present_retained_point_frame(frame);
    std::vector<RenderToolPreviewMeshRecord> previews(1u);
    auto &preview = previews.front();
    preview.source_object_id = 1;
    preview.dynamic_positions = true;
    preview.layout_key = 1u;
    preview.position_revision = 1u;
    auto positions = std::make_shared<std::vector<QVec3>>();
    for (const auto &marker : frame.points) {
      preview.mesh.edit_geometry().vertices.push_back({
          .position = marker.position,
          .source_vertex_id = static_cast<std::uint32_t>(marker.element.vertex_id)});
      positions->push_back(marker.position + QVec3{0.03f, 0.0f, 0.0f});
    }
    preview.dynamic_position_payload = positions;
    ASSERT_TRUE(harness.update_retained_point_positions(previews));
    const auto preview_stats = harness.retained_point_stats();
    const auto moved = harness.capture();
    ASSERT_FALSE(moved.isNull());
    EXPECT_NE(moved, membership_image);
    frame.points.front().color = rgba8(80, 255, 40);
    harness.present_retained_point_frame(frame);
    const auto preview_restyled = harness.retained_point_stats();
    EXPECT_EQ(preview_restyled.uploads.position_upload_count, preview_stats.uploads.position_upload_count)
        << "A classification/style publication must not restore and re-upload active preview positions.";
    EXPECT_EQ(preview_restyled.uploads.index_upload_count, preview_stats.uploads.index_upload_count);
    EXPECT_GT(preview_restyled.uploads.appearance_upload_count, preview_stats.uploads.appearance_upload_count);
    const auto preview_image = harness.capture();
    ASSERT_FALSE(preview_image.isNull());
    EXPECT_NE(preview_image, moved);
    ASSERT_TRUE(harness.update_retained_point_positions({}));
    for (std::size_t index = 0u; index < frame.points.size(); ++index) {
      frame.points[index].position = (*positions)[index];
    }
    EXPECT_EQ(preview_image, harness.capture_legacy_point_frame(frame))
        << "Retained point streams match freshly projected authored positions after style changes.";

    // Heterogeneous sizes preserve the original batch-order fallback.
    frame.points.back().size_pixels += 1.0f;
    frame.points.back().layer = frame.points.front().layer;
    harness.present_retained_point_frame(frame);
    const auto heterogeneous = harness.capture();
    ASSERT_FALSE(heterogeneous.isNull());
    EXPECT_EQ(heterogeneous, harness.capture_legacy_point_frame(frame));
  }
}

TEST(SelectionOverlayRuntimeTests, VulkanSourceWireSlotsRetainGeometryAndMatchLegacyPreviewPixels) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-source-wire-retention-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    SCOPED_TRACE(projection == ViewportProjection::Perspective ? "perspective" : "orthographic");
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(1.0f, projection, 0.1f));
    const auto edge = [&](QVec2 first, QVec2 second, int index, std::uint32_t color) {
      return ViewportOverlayLine{
          .layer = ViewportOverlayLayer::SourceWire,
          .start = harness.overlay_audit_world_point(first, 0.5f),
          .end = harness.overlay_audit_world_point(second, 0.5f),
          .color = color, .width_pixels = 4.0f,
          .style_role = ViewportOverlayStyleRole::ExplicitColor,
          .source = ViewportOverlaySource::ComponentSelection,
          .element = {.object_id = 1, .edge_a = index * 2, .edge_b = index * 2 + 1}};
    };
    ViewportOverlayFrame frame;
    frame.lines = {
        edge({260.0f, 240.0f}, {380.0f, 240.0f}, 0, rgba8(255, 50, 40, 160)),
        edge({320.0f, 185.0f}, {320.0f, 295.0f}, 1, rgba8(40, 220, 255, 192)),
        edge({260.0f, 210.0f}, {380.0f, 270.0f}, 2, rgba8(240, 220, 60, 160)),
        edge({270.0f, 290.0f}, {390.0f, 290.0f}, 3, rgba8(120, 240, 80, 192))};
    // The last segment crosses the near plane; retained endpoints use exactly
    // the existing edit-wire shader clipping and constant-pixel expansion.
    frame.lines.back().end.z = 0.95f;
    const auto initial_reference = harness.capture_legacy_point_frame(frame);
    ASSERT_FALSE(initial_reference.isNull());
    EXPECT_NE(initial_reference, harness.capture_legacy_point_frame({}));
    auto subset = frame;
    subset.lines.erase(subset.lines.begin() + 1);
    const auto subset_reference = harness.capture_legacy_point_frame(subset);
    std::reverse(subset.lines.begin(), subset.lines.end());
    const auto reordered_reference = harness.capture_legacy_point_frame(subset);
    auto colored = frame;
    colored.lines.front().color = rgba8(100, 255, 50, 192);
    auto colored_subset = colored;
    colored_subset.lines.erase(colored_subset.lines.begin() + 1);
    const auto restored_reference = harness.capture_legacy_point_frame(colored_subset);
    auto translated = colored_subset;
    for (auto &line : translated.lines) {
      line.start.x += 0.08f;
      line.end.x += 0.08f;
    }
    const auto preview_reference = harness.capture_legacy_point_frame(translated);
    auto committed = colored_subset;
    committed.lines.front().end.y += 0.04f;
    const auto committed_reference = harness.capture_legacy_point_frame(committed);

    // Generate references first so this entire mutation sequence retains one
    // live arena, including removal, order changes, preview edits and cancel.
    harness.present_retained_point_frame(frame);
    EXPECT_EQ(harness.capture(), initial_reference);
    auto before = harness.retained_point_stats();
    EXPECT_EQ(before.retained_source_wire_capacity, 4u);
    EXPECT_EQ(before.retained_source_wire_edges, 4u);
    auto ordered_subset = frame;
    ordered_subset.lines.erase(ordered_subset.lines.begin() + 1);
    harness.present_retained_point_frame(ordered_subset);
    auto after = harness.retained_point_stats();
    EXPECT_EQ(after.uploads.geometry_creation_count, before.uploads.geometry_creation_count);
    EXPECT_EQ(after.uploads.position_upload_count, before.uploads.position_upload_count);
    EXPECT_EQ(after.uploads.static_upload_count, before.uploads.static_upload_count);
    EXPECT_EQ(after.uploads.index_upload_count, before.uploads.index_upload_count + 1u);
    EXPECT_EQ(harness.capture(), subset_reference);
    before = after;
    harness.present_retained_point_frame(subset);
    after = harness.retained_point_stats();
    EXPECT_EQ(after.uploads.geometry_creation_count, before.uploads.geometry_creation_count);
    EXPECT_EQ(after.uploads.position_upload_count, before.uploads.position_upload_count);
    EXPECT_EQ(after.uploads.static_upload_count, before.uploads.static_upload_count);
    EXPECT_EQ(after.uploads.index_upload_count, before.uploads.index_upload_count + 1u);
    EXPECT_EQ(harness.capture(), reordered_reference);
    harness.present_retained_point_frame(frame);
    EXPECT_EQ(harness.capture(), initial_reference);
    EXPECT_EQ(harness.retained_point_stats().uploads.geometry_creation_count,
              before.uploads.geometry_creation_count);

    std::vector<RenderToolPreviewMeshRecord> previews(1u);
    auto &preview = previews.front();
    preview.source_object_id = 1;
    preview.dynamic_positions = true;
    preview.layout_key = 1u;
    preview.position_revision = 1u;
    auto base_positions = std::make_shared<std::vector<QVec3>>();
    auto moved_positions = std::make_shared<std::vector<QVec3>>();
    for (const auto &line : frame.lines) {
      for (const auto endpoint : {line.start, line.end}) {
        preview.mesh.edit_geometry().vertices.push_back({
            .position = endpoint,
            .source_vertex_id = static_cast<std::uint32_t>(base_positions->size())});
        base_positions->push_back(endpoint);
        moved_positions->push_back(endpoint + QVec3{0.08f, 0.0f, 0.0f});
      }
    }
    preview.dynamic_position_payload = moved_positions;
    ASSERT_TRUE(harness.update_retained_point_positions(previews));
    harness.present_retained_point_frame(colored);
    harness.present_retained_point_frame(colored_subset);
    EXPECT_EQ(harness.capture(), preview_reference)
        << "Membership and color changes preserve active preview endpoints.";
    ++preview.position_revision;
    preview.dynamic_position_payload = base_positions;
    ASSERT_TRUE(harness.update_retained_point_positions(previews));
    EXPECT_EQ(harness.capture(), restored_reference) << "A zero displacement restores authored endpoints.";
    ++preview.position_revision;
    preview.dynamic_position_payload = moved_positions;
    ASSERT_TRUE(harness.update_retained_point_positions(previews));
    EXPECT_EQ(harness.capture(), preview_reference);
    ASSERT_TRUE(harness.update_retained_point_positions({}));
    EXPECT_EQ(harness.capture(), restored_reference) << "Cancel restores the currently active edge subset.";
    before = harness.retained_point_stats();
    harness.present_retained_point_frame(committed);
    after = harness.retained_point_stats();
    EXPECT_EQ(after.uploads.geometry_creation_count, before.uploads.geometry_creation_count);
    EXPECT_EQ(after.uploads.index_upload_count, before.uploads.index_upload_count);
    EXPECT_GT(after.uploads.position_upload_count, before.uploads.position_upload_count);
    EXPECT_EQ(harness.capture(), committed_reference);
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(1.2f, projection, 0.1f));
    harness.present_retained_point_frame(committed);
    const auto camera_stats = harness.retained_point_stats();
    EXPECT_EQ(camera_stats.uploads.position_upload_count, after.uploads.position_upload_count);
    EXPECT_EQ(camera_stats.uploads.index_upload_count, after.uploads.index_upload_count);
    EXPECT_EQ(camera_stats.uploads.static_upload_count, after.uploads.static_upload_count);
    const auto camera_image = harness.capture();
    EXPECT_EQ(camera_image, harness.capture_legacy_point_frame(committed));
    harness.present_retained_point_frame(frame);
    harness.present_retained_point_frame({});
    EXPECT_EQ(harness.retained_point_stats().retained_source_wire_capacity, 0u);
    EXPECT_EQ(harness.retained_point_stats().retained_source_wire_edges, 0u);
  }
}

TEST(SelectionOverlayRuntimeTests, VulkanFragmentedSourceWireCommitBoundsUploadsAndPreservesPixels) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-fragmented-wire-commit-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.prepare_overlay_audit_camera(1.0f, ViewportProjection::Orthographic));

  // Alternate dirty and unchanged edges across a large retained buffer. One
  // upload per dirty edge used to exhaust Filament's backend command arena.
  constexpr int kEdgeCount = 65536;
  constexpr int kColumns = 256;
  ViewportOverlayFrame frame;
  frame.lines.reserve(kEdgeCount);
  std::vector<RenderToolPreviewMeshRecord> previews(1u);
  auto &preview = previews.front();
  preview.source_object_id = 1;
  preview.dynamic_positions = true;
  preview.layout_key = 1u;
  preview.position_revision = 1u;
  auto moved_positions = std::make_shared<std::vector<QVec3>>();
  moved_positions->reserve(kEdgeCount * 2u);
  auto &vertices = preview.mesh.edit_geometry().vertices;
  vertices.reserve(kEdgeCount * 2u);
  const auto origin = harness.overlay_audit_world_point({40.0f, 40.0f}, 0.5f);
  const auto pixel_x = harness.overlay_audit_world_point({41.0f, 40.0f}, 0.5f) - origin;
  const auto pixel_y = harness.overlay_audit_world_point({40.0f, 41.0f}, 0.5f) - origin;
  for (int index = 0; index < kEdgeCount; ++index) {
    const auto start = origin + pixel_x * (2.1f * static_cast<float>(index % kColumns)) +
        pixel_y * (1.5f * static_cast<float>(index / kColumns));
    const auto end = start + pixel_x * 1.25f;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire, .start = start, .end = end,
        .color = index % 2 == 0 ? rgba8(250, 90, 40, 192) : rgba8(40, 220, 255, 192),
        .width_pixels = 1.0f, .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::ComponentSelection,
        .element = {.object_id = 1, .edge_a = index * 2, .edge_b = index * 2 + 1}});
    for (const auto endpoint : {start, end}) {
      vertices.push_back({.position = endpoint,
          .source_vertex_id = static_cast<std::uint32_t>(moved_positions->size())});
      moved_positions->push_back(index % 2 == 0 ? endpoint + pixel_y * 0.6f : endpoint);
    }
  }
  preview.dynamic_position_payload = moved_positions;
  auto committed = frame;
  for (std::size_t index = 0u; index < committed.lines.size(); ++index) {
    committed.lines[index].start = (*moved_positions)[index * 2u];
    committed.lines[index].end = (*moved_positions)[index * 2u + 1u];
  }
  const auto original_reference = harness.capture_legacy_point_frame(frame);
  const auto committed_reference = harness.capture_legacy_point_frame(committed);
  ASSERT_FALSE(original_reference.isNull());
  ASSERT_FALSE(committed_reference.isNull());
  ASSERT_NE(original_reference, committed_reference);

  harness.present_retained_point_frame(frame);
  EXPECT_EQ(harness.capture(), original_reference);
  ASSERT_TRUE(harness.update_retained_point_positions(previews));
  EXPECT_EQ(harness.capture(), committed_reference);
  ASSERT_TRUE(harness.update_retained_point_positions({}));
  const auto before_commit = harness.retained_point_stats();
  harness.present_retained_point_frame(committed);
  const auto after_commit = harness.retained_point_stats();
  EXPECT_EQ(after_commit.retained_source_wire_edges, static_cast<std::size_t>(kEdgeCount));
  EXPECT_EQ(after_commit.uploads.geometry_creation_count, before_commit.uploads.geometry_creation_count);
  EXPECT_EQ(after_commit.uploads.index_upload_count, before_commit.uploads.index_upload_count);
  EXPECT_GT(after_commit.uploads.position_upload_count, before_commit.uploads.position_upload_count);
  EXPECT_LE(after_commit.uploads.position_upload_count - before_commit.uploads.position_upload_count, 8u)
      << "Fragmented edits must not enqueue commands in proportion to dirty edge count.";
  EXPECT_LE(after_commit.uploads.static_upload_count - before_commit.uploads.static_upload_count, 4u);
  EXPECT_EQ(harness.capture(), committed_reference)
      << "Consolidation preserves both changed edges and intervening unchanged edges.";

  // Reverting the same topology exercises a second fragmented upload without
  // recreating buffers, then an unchanged publication must upload nothing.
  harness.present_retained_point_frame(frame);
  const auto restored = harness.retained_point_stats();
  EXPECT_EQ(restored.uploads.geometry_creation_count, after_commit.uploads.geometry_creation_count);
  EXPECT_EQ(restored.uploads.index_upload_count, after_commit.uploads.index_upload_count);
  EXPECT_LE(restored.uploads.position_upload_count - after_commit.uploads.position_upload_count, 8u);
  EXPECT_LE(restored.uploads.static_upload_count - after_commit.uploads.static_upload_count, 4u);
  EXPECT_EQ(harness.capture(), original_reference);
  harness.present_retained_point_frame(frame);
  const auto unchanged = harness.retained_point_stats();
  EXPECT_EQ(unchanged.uploads.position_upload_count, restored.uploads.position_upload_count);
  EXPECT_EQ(unchanged.uploads.static_upload_count, restored.uploads.static_upload_count);
  EXPECT_EQ(unchanged.uploads.index_upload_count, restored.uploads.index_upload_count);
}

TEST(SelectionOverlayRuntimeTests, VulkanOverlayLineEndsAndPointRingsHaveAnalyticCoverage) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-overlay-analytic-coverage-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.prepare_overlay_audit_camera(2.0f, ViewportProjection::Perspective));
  harness.set_overlay_audit_frame({});
  harness.present_overlay_audit_frame();
  const QImage background = harness.capture();
  ASSERT_FALSE(background.isNull());
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) directory = QDir(QCoreApplication::applicationDirPath())
      .absoluteFilePath(QStringLiteral("../overlay_audit_visual"));
  ASSERT_TRUE(QDir().mkpath(directory));
  QImage initial;
  for (const auto &[scale, label] : std::array<std::pair<float, const char *>, 3>{{
           {1.0f, "100"}, {2.0f, "200"}, {1.0f, "down"}}}) {
    SCOPED_TRACE(label);
    ViewportOverlayFrame frame;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::ToolHandle,
        .start = harness.overlay_audit_world_point({90.25f, 110.25f}, 0.0f),
        .end = harness.overlay_audit_world_point({220.25f, 170.25f}, 0.0f),
        .color = rgba8(255, 255, 255), .width_pixels = 6.0f,
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::Tool});
    frame.points = {
        {.layer = ViewportOverlayLayer::SnapTargetVertex,
         .position = harness.overlay_audit_world_point({440.25f, 120.25f}, 0.0f),
         .color = rgba8(255, 255, 255), .size_pixels = 20.0f,
         .style_role = ViewportOverlayStyleRole::ExplicitColor, .source = ViewportOverlaySource::Tool},
        {.layer = ViewportOverlayLayer::UnselectedVertex,
         .position = harness.overlay_audit_world_point({540.25f, 120.25f}, 0.0f),
         .color = rgba8(255, 255, 255), .size_pixels = 10.0f,
         .style_role = ViewportOverlayStyleRole::ExplicitColor, .source = ViewportOverlaySource::Tool}};
    harness.set_overlay_audit_frame(std::move(frame));
    harness.present_overlay_audit_frame(scale);
    const QImage image = harness.capture();
    ASSERT_FALSE(image.isNull());
    ASSERT_TRUE(image.save(QDir(directory).filePath(
        QStringLiteral("analytic-coverage-%1.png").arg(label))));
    int cap_partial = 0;
    const float dx = 130.0f / std::hypot(130.0f, 60.0f);
    const float dy = 60.0f / std::hypot(130.0f, 60.0f);
    for (int y = 95; y < 125; ++y) {
      for (int x = 75; x < 105; ++x) {
        const float px = x + 0.5f - 90.25f;
        const float py = y + 0.5f - 110.25f;
        const float along = px * dx + py * dy;
        const float across = -px * dy + py * dx;
        const int intensity = image.pixelColor(x, y).red();
        const int backdrop = background.pixelColor(x, y).red();
        if (std::abs(along + 3.0f * scale) < 1.5f &&
            std::abs(across) < 3.0f * scale - 1.25f &&
            intensity > backdrop + 2 && intensity < 240) ++cap_partial;
      }
    }
    EXPECT_GE(cap_partial, 2) << "End caps need analytic coverage away from the feathered sides.";
    int inner_partial = 0;
    int outer_partial = 0;
    int solid_ring = 0;
    const float radius = 10.0f * scale;
    for (int y = 96; y < 145; ++y) {
      for (int x = 416; x < 465; ++x) {
        const float radius_at_pixel = std::hypot(x + 0.5f - 440.25f, y + 0.5f - 120.25f);
        const int intensity = image.pixelColor(x, y).red();
        if (intensity > background.pixelColor(x, y).red() + 2 && intensity < 240) {
          if (std::abs(radius_at_pixel - radius * 0.62f) < 1.5f) ++inner_partial;
          if (std::abs(radius_at_pixel - radius) < 1.5f) ++outer_partial;
        }
        if (intensity > 245) ++solid_ring;
      }
    }
    EXPECT_GE(inner_partial, 6);
    EXPECT_GE(outer_partial, 8);
    EXPECT_GT(solid_ring, 12);
    EXPECT_EQ(image.pixelColor(440, 120), background.pixelColor(440, 120))
        << "Snap rings keep a hollow center.";
    EXPECT_GT(image.pixelColor(540, 120).red(), 245);
    EXPECT_GT(image.pixelColor(540 + int(3 * scale), 120 + int(3 * scale)).red(), 245)
        << "Ordinary vertex squares retain their opaque corners.";
    if (initial.isNull()) initial = image;
    else if (scale == 1.0f) EXPECT_EQ(image, initial);
    harness.present_overlay_audit_frame(scale);
    EXPECT_FALSE(harness.last_submission().dynamic_submitted);
    EXPECT_EQ(harness.capture(), image);
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanArrayMeshPreviewsRetainWireGeometryInTheirWireframePanes) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-array-wire-preview-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  const auto bright_pixels = [](const QImage &image, QVec2 pixel) {
    int count = 0;
    for (int y = int(pixel.y) - 3; y <= int(pixel.y) + 3; ++y) {
      for (int x = int(pixel.x) - 3; x <= int(pixel.x) + 3; ++x) {
        if (image.rect().contains(x, y) && image.pixelColor(x, y).lightness() > 80) ++count;
      }
    }
    return count;
  };
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) directory = QDir(QCoreApplication::applicationDirPath())
                                         .absoluteFilePath(QStringLiteral("../selection_features_visual"));
  ASSERT_TRUE(QDir().mkpath(directory));
  for (const auto &[tool, label] : std::array{
           std::pair{ViewportToolId::LinearArray, "linear"},
           std::pair{ViewportToolId::RadialArray, "radial"},
           std::pair{ViewportToolId::PathArray, "curve"}}) {
    SCOPED_TRACE(label);
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(-0.02f, false));
    ASSERT_TRUE(harness.create_array_wire_sample(tool));
    for (const float scale : {1.0f, 2.0f, 1.0f}) {
      SCOPED_TRACE(scale);
      ASSERT_TRUE(harness.present_array_wire_sample(viewport_grid_layer_mask(0u), scale));
      const auto probe = harness.array_wire_probe_pixel();
      const auto visible = harness.capture();
      ASSERT_FALSE(visible.isNull());
      ASSERT_GE(bright_pixels(visible, probe), 3) << "Generated mesh edges must survive without fill passes.";
      const QString path = QDir(directory).filePath(
          QStringLiteral("%1_array_wire_%2.png").arg(QString::fromLatin1(label)).arg(scale));
      ASSERT_TRUE(visible.save(path));
      const auto resources = harness.render_diagnostics();
      ASSERT_GT(resources.wire_participant_count, 0u);
      for (int frame = 0; frame < 16; ++frame) {
        ASSERT_TRUE(harness.present_array_wire_sample(viewport_grid_layer_mask(0u), scale));
      }
      EXPECT_EQ(harness.render_diagnostics().gpu_geometry_upload_count, resources.gpu_geometry_upload_count);
      EXPECT_EQ(harness.render_diagnostics().gpu_geometry_reuse_count, resources.gpu_geometry_reuse_count);
      EXPECT_EQ(harness.capture(), visible);
      ASSERT_TRUE(harness.reject_array_wire_candidate());
      EXPECT_EQ(harness.capture(), visible) << "Failed publication preserves the accepted wire preview.";

      ASSERT_TRUE(harness.present_array_wire_sample(viewport_grid_layer_mask(1u), scale));
      EXPECT_EQ(bright_pixels(harness.capture(), probe), 0)
          << "A sibling wireframe pane must not leak preview wires into this pane.";
      ASSERT_TRUE(harness.present_array_wire_sample(viewport_grid_layer_mask(0u), scale));
      EXPECT_GE(bright_pixels(harness.capture(), probe), 3);
    }
    ASSERT_TRUE(harness.change_array_wire_count());
    ASSERT_TRUE(harness.present_array_wire_sample(viewport_grid_layer_mask(0u)));
    const auto last_probe = harness.array_wire_probe_pixel();
    EXPECT_GE(bright_pixels(harness.capture(), last_probe), 3);
    harness.retire_array_scene_binding();
    EXPECT_EQ(bright_pixels(harness.capture(), last_probe), 0)
        << "Switching scene bindings must retire outgoing generated wires.";
    ASSERT_TRUE(harness.present_array_wire_sample(viewport_grid_layer_mask(0u)));
    EXPECT_GE(bright_pixels(harness.capture(), last_probe), 3);
    ASSERT_TRUE(harness.cancel_array_wire_sample());
    ASSERT_TRUE(harness.present_array_wire_sample(viewport_grid_layer_mask(0u)));
    EXPECT_EQ(bright_pixels(harness.capture(), last_probe), 0)
        << "Cancel must retire all generated mesh edges.";
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanLinearArrayConstraintGuideTracksEndpointCameraAndRasterScale) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-array-constraint-guide-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(-0.02f, false));
  ASSERT_TRUE(harness.create_array_wire_sample(ViewportToolId::LinearArray));
  const auto has_axis_color = [](QColor pixel, LinearArrayAxisConstraint axis) {
    const int r = pixel.red(), g = pixel.green(), b = pixel.blue();
    if (axis == LinearArrayAxisConstraint::X) return r > 100 && r > g * 1.5f && r > b * 1.25f;
    // A one-pixel green guide straddles two pixels: the production AA sample
    // is (40, 89, 12). Channel dominance still excludes white/cyan handles.
    if (axis == LinearArrayAxisConstraint::Y) return g > 50 && g > r * 1.25f && g > b * 1.25f;
    return b > 100 && b > r * 1.4f && b > g * 1.15f;
  };
  const auto probe_count = [&](const QImage &image, QVec2 point, LinearArrayAxisConstraint axis) {
    int count = 0;
    for (int y = int(point.y) - 3; y <= int(point.y) + 3; ++y) {
      for (int x = int(point.x) - 3; x <= int(point.x) + 3; ++x) {
        if (image.rect().contains(x, y) && has_axis_color(image.pixelColor(x, y), axis)) ++count;
      }
    }
    return count;
  };
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) directory = QDir(QCoreApplication::applicationDirPath())
                                         .absoluteFilePath(QStringLiteral("../selection_features_visual"));
  ASSERT_TRUE(QDir().mkpath(directory));
  const std::array axes{LinearArrayAxisConstraint::X, LinearArrayAxisConstraint::Y,
                        LinearArrayAxisConstraint::Z};
  for (bool orthographic : {false, true}) {
    SCOPED_TRACE(orthographic ? "orthographic" : "perspective");
    for (std::size_t index = 0u; index < axes.size(); ++index) {
      const auto axis = axes[index];
      SCOPED_TRACE(index);
      ASSERT_TRUE(harness.configure_linear_guide_sample(axis, orthographic));
      const auto probes = harness.linear_guide_probe_pixels(axis);
      QImage at_100_percent;
      for (float scale : {1.0f, 2.0f, 1.0f}) {
        SCOPED_TRACE(scale);
        harness.present_linear_guide_sample(scale);
        const auto image = harness.capture();
        ASSERT_FALSE(image.isNull());
        for (auto probe : probes) {
          EXPECT_GE(probe_count(image, probe, axis), 3)
              << "The guide must extend in both directions through the off-axis endpoint.";
        }
        ASSERT_TRUE(image.save(QDir(directory).filePath(QStringLiteral("linear_guide_%1_%2_%3.png")
            .arg(orthographic ? "ortho" : "perspective").arg(index).arg(scale))));
        if (at_100_percent.isNull()) at_100_percent = image;
        else if (scale == 1.0f) EXPECT_EQ(image, at_100_percent);
        for (int frame = 0; frame < 16; ++frame) {
          harness.present_linear_guide_sample(scale);
          EXPECT_FALSE(harness.last_submission().dynamic_submitted);
        }
        EXPECT_EQ(harness.capture(), image);
      }
      const auto before_camera = harness.last_submission().dynamic_source_signature;
      harness.pan_linear_guide_camera();
      harness.present_linear_guide_sample(1.0f);
      EXPECT_TRUE(harness.last_submission().dynamic_submitted);
      EXPECT_NE(harness.last_submission().dynamic_source_signature, before_camera);
      const auto moved = harness.capture();
      ASSERT_FALSE(moved.isNull());
      for (auto probe : harness.linear_guide_probe_pixels(axis)) {
        EXPECT_GE(probe_count(moved, probe, axis), 3);
      }
      for (auto other_axis : axes) {
        if (other_axis == axis) continue;
        int stale = 0;
        for (int y = 0; y < moved.height(); ++y) {
          for (int x = 0; x < moved.width(); ++x) {
            if (has_axis_color(moved.pixelColor(x, y), other_axis)) ++stale;
          }
        }
        EXPECT_EQ(stale, 0) << "Switching constraints must retire the previous axis guide.";
      }
      const auto moved_probes = harness.linear_guide_probe_pixels(axis);
      ASSERT_TRUE(harness.clear_linear_guide_constraint());
      harness.present_linear_guide_sample(1.0f);
      const auto free = harness.capture();
      ASSERT_FALSE(free.isNull());
      for (auto probe : moved_probes) EXPECT_EQ(probe_count(free, probe, axis), 0);
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanArrayDimensionsCoverAllCopiesAndToggleWithoutStaleFrames) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-array-dimensions-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) directory = QDir(QCoreApplication::applicationDirPath())
                                         .absoluteFilePath(QStringLiteral("../selection_features_visual"));
  ASSERT_TRUE(QDir().mkpath(directory));
  for (const auto &[tool, label] : std::array{
           std::pair{ViewportToolId::LinearArray, "linear"},
           std::pair{ViewportToolId::RadialArray, "radial"},
           std::pair{ViewportToolId::PathArray, "curve"}}) {
    SCOPED_TRACE(label);
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_array_wire_sample(tool));
    ASSERT_TRUE(harness.configure_array_dimensions_camera());
    for (bool wireframe : {false, true}) {
      SCOPED_TRACE(wireframe ? "wireframe" : "shaded");
      for (float scale : {1.0f, 2.0f, 1.0f}) {
        SCOPED_TRACE(scale);
        ASSERT_TRUE(harness.present_array_dimensions_sample(wireframe, true, scale));
        ASSERT_TRUE(harness.last_submission().selection_dimension_bounds.has_value())
            << "Default-on tool dimensions must override the disabled global display preference.";
        const auto bounds = *harness.last_submission().selection_dimension_bounds;
        EXPECT_GT(bounds.max.x - bounds.min.x, 1.5f)
            << "The 0.5-unit source alone must not determine the dimensions.";
        if (tool == ViewportToolId::LinearArray) {
          EXPECT_NEAR(bounds.min.x, -0.25f, 0.0001f);
          EXPECT_NEAR(bounds.max.x, 4.25f, 0.0001f);
        }
        const auto enabled = harness.capture();
        ASSERT_FALSE(enabled.isNull());
        ASSERT_TRUE(enabled.save(QDir(directory).filePath(QStringLiteral("%1_array_dimensions_%2_%3.png")
            .arg(QString::fromLatin1(label)).arg(wireframe ? "wire" : "shaded").arg(scale))));
        const auto signature = harness.last_submission().selection_dimension_signature;
        for (int frame = 0; frame < 8; ++frame) {
          ASSERT_TRUE(harness.present_array_dimensions_sample(wireframe, true, scale));
          EXPECT_FALSE(harness.last_submission().dynamic_submitted);
          EXPECT_EQ(harness.last_submission().selection_dimension_signature, signature);
        }
        EXPECT_EQ(harness.capture(), enabled);
        const auto geometry_uploads = harness.render_diagnostics().gpu_geometry_upload_count;
        ASSERT_TRUE(harness.set_array_dimensions_visible(false));
        ASSERT_TRUE(harness.present_array_dimensions_sample(wireframe, true, scale));
        EXPECT_FALSE(harness.last_submission().selection_dimension_bounds.has_value());
        const auto disabled = harness.capture();
        ASSERT_FALSE(disabled.isNull());
        int changed = 0, colored = 0;
        for (int y = 0; y < enabled.height(); ++y) {
          for (int x = 0; x < enabled.width(); ++x) {
            if (enabled.pixel(x, y) == disabled.pixel(x, y)) continue;
            ++changed;
            const auto pixel = enabled.pixelColor(x, y);
            if (pixel.red() > pixel.blue() + 35 || pixel.green() > pixel.blue() + 35 ||
                pixel.blue() > pixel.red() + 35) ++colored;
          }
        }
        EXPECT_GT(changed, 40);
        EXPECT_GT(colored, 20) << "The colored dimensions and cage must reach the Vulkan surface.";
        ASSERT_TRUE(harness.set_array_dimensions_visible(true));
        ASSERT_TRUE(harness.present_array_dimensions_sample(wireframe, true, scale));
        EXPECT_EQ(harness.render_diagnostics().gpu_geometry_upload_count, geometry_uploads)
            << "A measurement toggle must not rebuild array mesh geometry.";
        EXPECT_EQ(harness.capture(), enabled);
        ASSERT_TRUE(harness.present_array_dimensions_sample(wireframe, false, scale));
        EXPECT_FALSE(harness.last_submission().selection_dimension_bounds.has_value());
        ASSERT_TRUE(harness.present_array_dimensions_sample(wireframe, true, scale));
        EXPECT_EQ(harness.capture(), enabled);
      }
      const auto before = *harness.last_submission().selection_dimension_bounds;
      harness.pan_linear_guide_camera();
      ASSERT_TRUE(harness.present_array_dimensions_sample(wireframe));
      ASSERT_TRUE(harness.last_submission().selection_dimension_bounds.has_value());
      const auto after = *harness.last_submission().selection_dimension_bounds;
      EXPECT_FLOAT_EQ(after.min.x, before.min.x);
      EXPECT_FLOAT_EQ(after.max.x, before.max.x);
      EXPECT_FLOAT_EQ(after.min.y, before.min.y);
      EXPECT_FLOAT_EQ(after.max.y, before.max.y);
      EXPECT_TRUE(harness.last_submission().dynamic_submitted);
    }
    ASSERT_TRUE(harness.cancel_array_wire_sample());
    ASSERT_TRUE(harness.present_array_dimensions_sample(true));
    EXPECT_FALSE(harness.last_submission().selection_dimension_bounds.has_value());
  }
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
     VulkanComponentPositionStreamsMoveRetainedPointsEdgesFacesAndDepth) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-retained-component-transform-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) {
    directory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(directory));
  auto settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionFace,
                                 {.r = 1.0f, .g = 0.0f, .b = 1.0f, .a = 0.6f}));
  for (const auto &[context, label] :
       std::array<std::pair<ViewportOverlaySelectionContext, const char *>, 3>{{
           {ViewportOverlaySelectionContext::Vertex, "vertex"},
           {ViewportOverlaySelectionContext::Edge, "edge"},
           {ViewportOverlaySelectionContext::Face, "face"}}}) {
    SCOPED_TRACE(label);
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_component_transform_sample(context));
    const auto present = [&] {
      return harness.present_component_transform_sample(context, settings);
    };
    const auto selected_pixel = [&](float delta) {
      return harness.cutting_preview_pixel({
          -1.2f + delta,
          context == ViewportOverlaySelectionContext::Vertex ? -0.6f : 0.0f,
          0.0f});
    };
    const auto wire_pixel = [&](float delta) {
      return harness.cutting_preview_pixel({-0.9f + delta, -0.6f, 0.0f});
    };
    const auto rear_pixel = [&](float delta) {
      return harness.cutting_preview_pixel({-0.9f + delta, 0.25f, -0.3f});
    };
    const auto has_face_tint = [&](const QImage &image, float delta) {
      const auto pixel = harness.cutting_preview_pixel({-0.9f + delta, 0.0f, 0.0f});
      const QColor color = image.pixelColor(int(pixel.x), int(pixel.y));
      return color.red() > color.green() + 20 &&
             color.blue() > color.green() + 20;
    };
    const auto capture = [&](const char *phase) {
      QImage image = harness.capture();
      EXPECT_FALSE(image.isNull());
      const QString path = QDir(directory).filePath(
          QStringLiteral("component_stream_%1_%2.png")
              .arg(QString::fromLatin1(label), QString::fromLatin1(phase)));
      EXPECT_TRUE(image.save(path));
      return image;
    };
    const auto expect_current = [&](const QImage &image, float delta) {
      EXPECT_GE(yellow_pixels(image, selected_pixel(delta)), 3)
          << "The selected point or edge must follow the current packed positions.";
      EXPECT_EQ(cyan_pixels(image, rear_pixel(delta)), 0)
          << "The moved source surface must occlude the stationary rear wire.";
      if (context == ViewportOverlaySelectionContext::Face) {
        EXPECT_TRUE(has_face_tint(image, delta))
            << "The selected face fill must move with its retained triangle bindings.";
      } else {
        EXPECT_GE(cyan_pixels(image, wire_pixel(delta)), 3)
            << "The unselected source wire must follow its source vertices.";
      }
    };
    ASSERT_TRUE(present());
    ASSERT_NO_FATAL_FAILURE(expect_current(capture("baseline"), 0.0f));
    auto &modeling = harness.modeling();
    ASSERT_TRUE(modeling.begin_transform_preview(false).ok);
    std::uint64_t retained_builds = 0u;
    std::uint64_t retained_signature = 0u;
    OverlayProxyUploadStats previous_uploads;
    float previous_delta = 0.0f;
    for (const float delta : {0.9f, 1.8f}) {
      SCOPED_TRACE(delta);
      ASSERT_TRUE(modeling.update_transform_translate_preview({delta, 0.0f, 0.0f}).ok);
      ASSERT_TRUE(present());
      const auto overlay_stats = harness.component_overlay_stats();
      EXPECT_GT(overlay_stats.shared_position_proxy_count, 0u);
      if (retained_builds == 0u) {
        retained_builds = harness.selection_frame_builds();
        retained_signature = modeling.selection_overlay_signature();
      } else {
        EXPECT_EQ(harness.selection_frame_builds(), retained_builds);
        EXPECT_EQ(modeling.selection_overlay_signature(), retained_signature);
        EXPECT_FALSE(harness.last_submission().persistent_submitted)
            << "Changing only packed positions must keep the semantic frame retained.";
        EXPECT_EQ(overlay_stats.uploads.position_upload_count, previous_uploads.position_upload_count)
            << "Steady movement must not upload expanded positions for each overlay proxy.";
        EXPECT_EQ(overlay_stats.uploads.shared_position_upload_count,
                  previous_uploads.shared_position_upload_count + 1u);
        EXPECT_EQ(overlay_stats.uploads.shared_position_binding_upload_count,
                  previous_uploads.shared_position_binding_upload_count);
        EXPECT_EQ(overlay_stats.uploads.index_upload_count, previous_uploads.index_upload_count);
        EXPECT_EQ(overlay_stats.uploads.static_upload_count, previous_uploads.static_upload_count);
      }
      previous_uploads = overlay_stats.uploads;
      const QImage image = capture(delta < 1.0f ? "first" : "second");
      ASSERT_NO_FATAL_FAILURE(expect_current(image, delta));
      EXPECT_EQ(yellow_pixels(image, selected_pixel(previous_delta)), 0);
      EXPECT_GE(cyan_pixels(image, rear_pixel(previous_delta)), 3)
          << "The old source surface must no longer occlude the rear wire.";
      if (context == ViewportOverlaySelectionContext::Face) {
        EXPECT_FALSE(has_face_tint(image, previous_delta));
      } else {
        EXPECT_EQ(cyan_pixels(image, wire_pixel(previous_delta)), 0);
      }
      ASSERT_TRUE(present());
      EXPECT_EQ(harness.selection_frame_builds(), retained_builds);
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_EQ(harness.component_overlay_stats().uploads.shared_position_upload_count,
                previous_uploads.shared_position_upload_count);
      previous_delta = delta;
    }
    // A camera rebuild must reapply the same packed positions to all layers.
    ASSERT_TRUE(harness.pan_face_fill_camera(0.1f));
    ASSERT_TRUE(present());
    EXPECT_EQ(harness.selection_frame_builds(), retained_builds);
    EXPECT_FALSE(harness.last_submission().persistent_submitted);
    EXPECT_EQ(harness.component_overlay_stats().uploads.shared_position_upload_count,
              previous_uploads.shared_position_upload_count);
    ASSERT_NO_FATAL_FAILURE(expect_current(capture("camera"), previous_delta));
    ASSERT_TRUE(modeling.cancel_transform_preview().ok);
    ASSERT_TRUE(present());
    EXPECT_GT(harness.selection_frame_builds(), retained_builds);
    const QImage restored = capture("cancel");
    ASSERT_NO_FATAL_FAILURE(expect_current(restored, 0.0f));
    EXPECT_EQ(yellow_pixels(restored, selected_pixel(previous_delta)), 0);
    EXPECT_EQ(harness.component_overlay_stats().shared_position_proxy_count, 0u);
    // A second gesture must rebind retained indices after cancel restored CPU positions.
    ASSERT_TRUE(modeling.begin_transform_preview(false).ok);
    ASSERT_TRUE(modeling.update_transform_translate_preview({0.9f, 0.0f, 0.0f}).ok);
    ASSERT_TRUE(present());
    EXPECT_GT(harness.component_overlay_stats().shared_position_proxy_count, 0u);
    ASSERT_NO_FATAL_FAILURE(expect_current(capture("repeat"), 0.9f));
    const auto before_commit_uploads = harness.component_overlay_stats().uploads;
    const auto before_commit_work = modeling.selection_overlay_geometry_work_counters();
    ASSERT_TRUE(modeling.commit_transform_preview().ok);
    ASSERT_TRUE(present());
    EXPECT_TRUE(modeling.tool_preview_meshes().empty());
    EXPECT_FALSE(modeling.transient_transform_preview_active());
    EXPECT_GT(harness.component_overlay_stats().shared_position_proxy_count, 0u);
    const auto committed_uploads = harness.component_overlay_stats().uploads;
    EXPECT_EQ(committed_uploads.position_upload_count, before_commit_uploads.position_upload_count);
    EXPECT_EQ(committed_uploads.shared_position_upload_count, before_commit_uploads.shared_position_upload_count);
    EXPECT_EQ(committed_uploads.shared_position_binding_upload_count, before_commit_uploads.shared_position_binding_upload_count);
    EXPECT_EQ(committed_uploads.static_upload_count, before_commit_uploads.static_upload_count);
    EXPECT_EQ(committed_uploads.index_upload_count, before_commit_uploads.index_upload_count);
    EXPECT_EQ(modeling.selection_overlay_geometry_work_counters().semantic_aggregate_build_count,
              before_commit_work.semantic_aggregate_build_count);
    EXPECT_EQ(modeling.selection_overlay_geometry_work_counters().world_projection_build_count,
              before_commit_work.world_projection_build_count);
    ASSERT_NO_FATAL_FAILURE(expect_current(capture("commit"), 0.9f));
    ASSERT_TRUE(present());
    EXPECT_EQ(harness.component_overlay_stats().uploads.shared_position_upload_count,
              committed_uploads.shared_position_upload_count);
    ASSERT_TRUE(harness.pan_face_fill_camera(-0.15f));
    ASSERT_TRUE(present());
    ASSERT_NO_FATAL_FAILURE(expect_current(capture("navigation_after_commit"), 0.9f));
    EXPECT_EQ(harness.component_overlay_stats().uploads.shared_position_upload_count,
              committed_uploads.shared_position_upload_count);

    // Accept a candidate whose final sample has never reached the GPU. The
    // handoff must upload that sample, rather than promoting the previous one.
    ASSERT_TRUE(modeling.begin_transform_preview(false).ok);
    ASSERT_TRUE(modeling.update_transform_translate_preview({0.3f, 0.0f, 0.0f}).ok);
    ASSERT_TRUE(present());
    ASSERT_TRUE(modeling.update_transform_translate_preview({0.9f, 0.0f, 0.0f}).ok);
    ASSERT_TRUE(modeling.commit_transform_preview().ok);
    ASSERT_TRUE(present());
    ASSERT_NO_FATAL_FAILURE(expect_current(capture("unsubmitted_commit"), 1.8f));
    EXPECT_EQ(yellow_pixels(harness.capture(), selected_pixel(1.2f)), 0);
    ASSERT_TRUE(modeling.begin_transform_preview(false).ok);
    ASSERT_TRUE(modeling.update_transform_translate_preview({0.3f, 0.0f, 0.0f}).ok);
    ASSERT_TRUE(present());
    ASSERT_TRUE(modeling.cancel_transform_preview().ok);
    ASSERT_TRUE(present());
    ASSERT_NO_FATAL_FAILURE(expect_current(capture("cancel_after_commit"), 1.8f));
  }
}

int loop_preview_green_pixels(const QImage &image, QVec2 center,
                              bool vertex_fill_only) {
  int count = 0;
  for (int y = int(center.y) - 10; y <= int(center.y) + 10; ++y) {
    for (int x = int(center.x) - 10; x <= int(center.x) + 10; ++x) {
      if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) continue;
      const QColor pixel = image.pixelColor(x, y);
      if (pixel.green() > 150 && pixel.green() > pixel.red() + 80 &&
          pixel.green() > pixel.blue() + 80 &&
          (!vertex_fill_only || pixel.blue() > pixel.red())) ++count;
    }
  }
  return count;
}

TEST(SelectionOverlayRuntimeTests,
     VulkanEdgeLoopPreviewVerticesFollowHoverScaleCancelAndCommit) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-edge-loop-preview-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) {
    directory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(directory));
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.create_edge_loop_sample());
  auto &modeling = harness.modeling();
  const auto first_pixels = harness.edge_loop_pixels(-0.25f);
  const auto moved_pixels = harness.edge_loop_pixels(0.25f);
  const auto save = [&](const QImage &image, const char *phase) {
    const QString path = QDir(directory).filePath(
        QStringLiteral("edge_loop_%1.png").arg(QString::fromLatin1(phase)));
    EXPECT_TRUE(image.save(path));
    std::cout << "Edge loop preview GPU png=" << path.toStdString() << '\n';
  };
  const auto expect_visible = [&](const QImage &image,
                                const std::array<QVec2, 3> &pixels) {
    // HoverVertex fill has blue > red; HoverWire has red > blue. Color-space
    // conversion changes channel magnitudes, but preserves this ordering and
    // distinguishes the actual glyph from the line ending at the same pixel.
    for (std::size_t vertex = 0; vertex < 2; ++vertex) {
      EXPECT_GE(loop_preview_green_pixels(image, pixels[vertex], true), 9)
          << "Each split edge must show the standard hover vertex glyph.";
    }
    EXPECT_GE(loop_preview_green_pixels(image, pixels[2], false), 9)
        << "The preview connector must remain visible between its vertices.";
  };
  const auto expect_removed = [&](const QImage &image,
                                const std::array<QVec2, 3> &pixels) {
    for (std::size_t vertex = 0; vertex < 2; ++vertex) {
      EXPECT_EQ(loop_preview_green_pixels(image, pixels[vertex], true), 0)
          << "The previous split-edge vertex must leave no retained glyph.";
    }
    EXPECT_EQ(loop_preview_green_pixels(image, pixels[2], false), 0)
        << "The previous loop connector must leave no retained green line.";
  };

  ASSERT_TRUE(modeling.update_insert_edge_loop_hover(harness.edge_loop_event(-0.25f)).ok);
  ASSERT_TRUE(modeling.tool_status().insert_edge_loop_has_preview);
  const auto preview = modeling.volatile_overlay_snapshot();
  ASSERT_NE(preview.frame, nullptr);
  ASSERT_EQ(preview.frame->points.size(), 2u);
  for (const auto &point : preview.frame->points) {
    EXPECT_EQ(point.layer, ViewportOverlayLayer::HoverVertex);
    EXPECT_EQ(point.style_role, ViewportOverlayStyleRole::HoverVertex);
    EXPECT_EQ(point.source, ViewportOverlaySource::Tool);
  }
  QImage at_100_percent;
  int vertex_pixels_at_100 = 0;
  for (const auto &[scale, phase] :
       std::array<std::pair<float, const char *>, 3>{{
           {1.0f, "hover_100"}, {2.0f, "hover_200"}, {1.0f, "hover_scale_down_100"}}}) {
    SCOPED_TRACE(phase);
    ASSERT_TRUE(harness.present_edge_loop_sample(scale));
    const QImage image = harness.capture();
    ASSERT_FALSE(image.isNull());
    save(image, phase);
    expect_visible(image, first_pixels);
    const int vertex_pixels = loop_preview_green_pixels(image, first_pixels[0], true);
    if (at_100_percent.isNull()) {
      at_100_percent = image;
      vertex_pixels_at_100 = vertex_pixels;
    } else if (scale == 2.0f) {
      EXPECT_GT(vertex_pixels, vertex_pixels_at_100 * 2)
          << "Vertex glyphs must use the shared raster scale.";
    } else {
      EXPECT_EQ(image, at_100_percent)
          << "Scaling down must restore the original vertex and line size.";
    }
    const auto builds = harness.selection_frame_builds();
    ASSERT_TRUE(harness.present_edge_loop_sample(scale));
    EXPECT_EQ(harness.selection_frame_builds(), builds);
    EXPECT_FALSE(harness.last_submission().persistent_submitted);
    EXPECT_FALSE(harness.last_submission().dynamic_submitted);
  }

  ASSERT_TRUE(modeling.update_insert_edge_loop_hover(harness.edge_loop_event(0.25f)).ok);
  ASSERT_TRUE(harness.present_edge_loop_sample());
  const QImage moved = harness.capture();
  ASSERT_FALSE(moved.isNull());
  save(moved, "hover_moved");
  expect_visible(moved, moved_pixels);
  expect_removed(moved, first_pixels);

  // The last grid step lands on the existing endpoint and has no valid split.
  ASSERT_TRUE(modeling.update_insert_edge_loop_hover(harness.edge_loop_event(0.74f)).ok);
  ASSERT_FALSE(modeling.tool_status().insert_edge_loop_has_preview);
  ASSERT_TRUE(harness.present_edge_loop_sample());
  const QImage invalid_hover = harness.capture();
  ASSERT_FALSE(invalid_hover.isNull());
  save(invalid_hover, "hover_endpoint");
  expect_removed(invalid_hover, moved_pixels);

  ASSERT_TRUE(modeling.update_insert_edge_loop_hover(harness.edge_loop_event(0.25f)).ok);
  ASSERT_TRUE(harness.present_edge_loop_sample());
  expect_visible(harness.capture(), moved_pixels);
  ASSERT_TRUE(modeling.cancel_active_modal_tool(false).ok);
  ASSERT_TRUE(harness.present_edge_loop_sample());
  const QImage canceled = harness.capture();
  ASSERT_FALSE(canceled.isNull());
  save(canceled, "canceled");
  expect_removed(canceled, moved_pixels);

  ASSERT_TRUE(modeling.begin_insert_edge_loop_drag(harness.edge_loop_event(-0.25f)).ok);
  ASSERT_TRUE(harness.present_edge_loop_sample());
  expect_visible(harness.capture(), first_pixels);
  ASSERT_TRUE(modeling.commit_insert_edge_loop_drag().ok);
  ASSERT_FALSE(modeling.tool_status().insert_edge_loop_has_preview);
  ASSERT_TRUE(harness.present_edge_loop_sample());
  const QImage committed = harness.capture();
  ASSERT_FALSE(committed.isNull());
  save(committed, "committed");
  expect_removed(committed, first_pixels);
}

TEST(SelectionOverlayRuntimeTests,
     VulkanCuttingToolHoverShowsUnselectedAuthoredTopologyWithoutSelecting) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-cutting-tool-topology-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) {
    directory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(directory));
  constexpr std::array<std::pair<float, const char *>, 3> kScales{{
      {1.0f, "100"}, {2.0f, "200"}, {1.0f, "scale_down_100"}}};
  for (const auto &[tool, label] :
       std::array<std::pair<ViewportToolId, const char *>, 2>{{
           {ViewportToolId::Knife, "knife"},
           {ViewportToolId::InsertEdgeLoop, "loop"}}}) {
    SCOPED_TRACE(label);
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_tool_topology_sample(tool));
    const auto before = harness.tool_topology_selection();
    ASSERT_EQ(before.mode, QuaderSelectionMode::Edge);
    ASSERT_FALSE(before.has_selection);
    ASSERT_EQ(before.selected_object_count, 0u);
    const auto history_before = harness.undo_count();
    const auto probe = harness.tool_topology_internal_edge_pixel();
    const QRect probe_region(int(probe.x) - 10, int(probe.y) - 10, 21, 21);
    std::array<QImage, 3> hovered_regions;
    std::array<QImage, 3> surface_regions;
    QImage hover_at_100;
    const auto save = [&](const QImage &image, const char *scale, const char *phase) {
      const QString path = QDir(directory).filePath(
          QStringLiteral("cutting_topology_%1_%2_%3.png")
              .arg(QString::fromLatin1(label), QString::fromLatin1(scale),
                   QString::fromLatin1(phase)));
      EXPECT_TRUE(image.save(path));
      std::cout << "Cutting topology GPU shared_edge_cyan="
                << cyan_pixels(image, probe) << ", png=" << path.toStdString() << '\n';
    };
    const auto expect_unselected = [&]() {
      const auto current = harness.tool_topology_selection();
      EXPECT_EQ(current.mode, before.mode);
      EXPECT_EQ(current.has_selection, before.has_selection);
      EXPECT_EQ(current.active_kind, before.active_kind);
      EXPECT_EQ(current.active_object_id, before.active_object_id);
      EXPECT_EQ(current.selected_object_count, 0u);
      EXPECT_EQ(current.selected_vertex_count, 0u);
      EXPECT_EQ(current.selected_edge_count, 0u);
      EXPECT_EQ(current.selected_face_count, 0u);
      EXPECT_EQ(current.selection_revision, before.selection_revision);
      EXPECT_EQ(current.content_revision, before.content_revision);
      EXPECT_EQ(harness.undo_count(), history_before);
    };
    const auto expect_retained = [&](float scale) {
      const auto builds = harness.selection_frame_builds();
      EXPECT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge, scale));
      EXPECT_EQ(harness.selection_frame_builds(), builds);
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_FALSE(harness.last_submission().dynamic_submitted);
    };
    // Check broad surface hover before any edge establishes a retained loop
    // seed. Missing the seed later deliberately preserves that cut preview.
    for (std::size_t index = 0; index < kScales.size(); ++index) {
      const auto &[scale, scale_label] = kScales[index];
      SCOPED_TRACE(scale_label);
      ASSERT_TRUE(harness.update_tool_topology_hover({3.0f, 0.0f, 0.0f}));
      ASSERT_TRUE(harness.update_tool_topology_hover({0.45f, 0.15f, 0.0f}));
      if (tool == ViewportToolId::InsertEdgeLoop) {
        EXPECT_FALSE(harness.modeling().tool_status().insert_edge_loop_has_preview);
      }
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge, scale));
      const QImage surface = harness.capture();
      ASSERT_FALSE(surface.isNull());
      save(surface, scale_label, "surface_hover");
      EXPECT_GE(cyan_pixels(surface, probe), 3)
          << "Revealing authored topology must not require a valid insertion seed.";
      surface_regions[index] = surface.copy(probe_region);
      expect_unselected();
      expect_retained(scale);
    }
    for (std::size_t index = 0; index < kScales.size(); ++index) {
      const auto &[scale, scale_label] = kScales[index];
      SCOPED_TRACE(scale_label);
      ASSERT_TRUE(harness.update_tool_topology_hover({3.0f, 0.0f, 0.0f}));
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge, scale));
      const QImage clear = harness.capture();
      ASSERT_FALSE(clear.isNull());
      save(clear, scale_label, "clear");
      EXPECT_EQ(cyan_pixels(clear, probe), 0)
          << "The unselected coplanar internal edge is absent without tool hover.";
      expect_unselected();
      expect_retained(scale);

      ASSERT_TRUE(harness.update_tool_topology_hover({1.0f, -0.25f, 0.0f}));
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge, scale));
      const QImage hovered = harness.capture();
      ASSERT_FALSE(hovered.isNull());
      save(hovered, scale_label, "edge_hover");
      EXPECT_GE(cyan_pixels(hovered, probe), 3)
          << "Hover must expose authored internal edges away from the cut preview.";
      hovered_regions[index] = hovered.copy(probe_region);
      EXPECT_EQ(hovered_regions[index], surface_regions[index]);
      if (index == 0u) hover_at_100 = hovered;
      else if (scale == 1.0f) EXPECT_EQ(hovered, hover_at_100);
      expect_unselected();
      expect_retained(scale);

      ASSERT_TRUE(harness.update_tool_topology_hover({3.0f, 0.0f, 0.0f}));
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge, scale));
      const QImage left = harness.capture();
      ASSERT_FALSE(left.isNull());
      save(left, scale_label, "hover_left");
      EXPECT_EQ(cyan_pixels(left, probe), 0);
      EXPECT_EQ(left.copy(probe_region), clear.copy(probe_region))
          << "Leaving must retire transient topology independently of the retained cut preview.";
      expect_unselected();
      expect_retained(scale);
    }

    ASSERT_TRUE(harness.update_tool_topology_hover({3.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(harness.select_tool_topology_sample());
    ASSERT_EQ(harness.tool_topology_selection().mode, QuaderSelectionMode::Edge);
    ASSERT_EQ(harness.tool_topology_selection().selected_object_count, 1u);
    for (std::size_t index = 0; index < kScales.size(); ++index) {
      const auto &[scale, scale_label] = kScales[index];
      SCOPED_TRACE(scale_label);
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge, scale));
      const QImage selected = harness.capture();
      ASSERT_FALSE(selected.isNull());
      save(selected, scale_label, "selected_reference");
      EXPECT_GE(cyan_pixels(selected, probe), 3);
      EXPECT_EQ(selected.copy(probe_region), hovered_regions[index])
          << "Transient topology must match the real selected mesh's source-wire rendering.";
      expect_retained(scale);
      if (tool == ViewportToolId::InsertEdgeLoop) {
        ASSERT_TRUE(harness.update_tool_topology_hover({1.0f, -0.25f, 0.0f}));
        ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge, scale));
        EXPECT_GE(cyan_pixels(harness.capture(), probe), 3);
        // Explicit preview cleanup must retain the selected mesh's authored
        // topology. Camera navigation instead freezes these transient previews.
        ASSERT_TRUE(harness.modeling().cancel_active_modal_tool(false).ok);
        ASSERT_TRUE(harness.modeling().clear_selection_hover().ok);
        ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge, scale));
        const QImage cleaned = harness.capture();
        ASSERT_FALSE(cleaned.isNull());
        save(cleaned, scale_label, "selected_hover_cleared");
        EXPECT_EQ(cleaned.copy(probe_region), selected.copy(probe_region));
        EXPECT_GE(cyan_pixels(cleaned, probe), 3);
        expect_retained(scale);
      }
    }
    if (tool == ViewportToolId::InsertEdgeLoop) {
      ASSERT_TRUE(harness.occlude_tool_topology_internal_edge());
      ASSERT_TRUE(harness.select_tool_topology_sample());
      ASSERT_TRUE(harness.modeling().clear_selection_hover().ok);
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge));
      const QImage covered_selected = harness.capture();
      ASSERT_EQ(cyan_pixels(covered_selected, probe), 0);
      ASSERT_TRUE(harness.update_tool_topology_hover({1.0f, -0.25f, 0.0f}));
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge));
      const QImage covered_hover = harness.capture();
      save(covered_hover, "100", "occluded_selected_hover");
      EXPECT_EQ(covered_hover.copy(probe_region), covered_selected.copy(probe_region))
          << "Hovering a selected mesh must retain its source-wire depth policy.";
      ASSERT_TRUE(harness.modeling().cancel_active_modal_tool(false).ok);
      ASSERT_TRUE(harness.modeling().clear_selection_hover().ok);
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge));
      const QImage covered_cleared = harness.capture();
      save(covered_cleared, "100", "occluded_selected_hover_cleared");
      EXPECT_EQ(covered_cleared.copy(probe_region), covered_hover.copy(probe_region))
          << "Clearing a tool preview must not change selected topology visibility.";
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanCuttingPreviewsRemainWorldAnchoredDuringCameraOnlyFrames) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-cutting-preview-navigation-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) {
    directory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(directory));

  for (const auto &[tool, label] :
       std::array<std::pair<ViewportToolId, const char *>, 2>{{
           {ViewportToolId::Knife, "knife"},
           {ViewportToolId::InsertEdgeLoop, "loop"}}}) {
    SCOPED_TRACE(label);
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_tool_topology_sample(tool));
    ASSERT_TRUE(harness.pan_face_fill_camera(0.0f));
    auto &modeling = harness.modeling();
    if (tool == ViewportToolId::Knife) {
      ASSERT_TRUE(modeling.accept_knife_point(
          harness.tool_topology_event({-1.0f, -0.25f, 0.0f})).ok);
      ASSERT_TRUE(modeling.accept_knife_point(
          harness.tool_topology_event({0.0f, -0.25f, 0.0f})).ok);
      ASSERT_TRUE(harness.update_tool_topology_hover({1.0f, 0.25f, 0.0f}));
      ASSERT_TRUE(modeling.tool_status().knife_has_current);
    } else {
      ASSERT_TRUE(harness.update_tool_topology_hover({1.0f, -0.25f, 0.0f}));
      ASSERT_TRUE(modeling.tool_status().insert_edge_loop_has_preview);
    }
    ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge));
    const auto frozen = modeling.volatile_overlay_snapshot();
    ASSERT_NE(frozen.frame, nullptr);
    ASSERT_FALSE(frozen.frame->lines.empty());
    ASSERT_FALSE(frozen.frame->points.empty());
    const auto selected = harness.tool_topology_selection();
    const auto undo = harness.undo_count();
    const auto builds = harness.selection_frame_builds();
    const auto volatile_builds = modeling.volatile_overlay_cache_stats().build_count;
    const auto first_probe = harness.cutting_preview_pixel({0.5f, -0.25f, 0.0f});
    QImage initial;

    // The input-controller test covers RMB suspension. These frames exercise
    // its retained rendering contract: only the camera changes, so the authored
    // Knife stroke/current segment and Loop preview must stay in world space.
    for (const auto &[camera_x, phase] :
         std::array<std::pair<float, const char *>, 4>{{
             {0.0f, "before"}, {0.18f, "camera_right"},
             {-0.12f, "camera_left"}, {0.0f, "camera_restored"}}}) {
      SCOPED_TRACE(phase);
      ASSERT_TRUE(harness.pan_face_fill_camera(camera_x));
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge));
      const QImage image = harness.capture();
      ASSERT_FALSE(image.isNull());
      const QString path = QDir(directory).filePath(
          QStringLiteral("cutting_preview_%1_%2.png")
              .arg(QString::fromLatin1(label), QString::fromLatin1(phase)));
      EXPECT_TRUE(image.save(path));
      std::cout << "Frozen cutting preview GPU png=" << path.toStdString() << '\n';
      if (tool == ViewportToolId::Knife) {
        EXPECT_GE(yellow_pixels(image, harness.cutting_preview_pixel(
            {-0.5f, -0.25f, 0.0f})), 3)
            << "The accepted Knife segment must remain visible while navigating.";
        EXPECT_GE(yellow_pixels(image, harness.cutting_preview_pixel(
            {0.5f, 0.0f, 0.0f})), 3)
            << "The current Knife segment must remain visible while navigating.";
      } else {
        for (const float x : {-1.0f, 0.0f, 1.0f}) {
          EXPECT_GE(loop_preview_green_pixels(image,
              harness.cutting_preview_pixel({x, -0.25f, 0.0f}), true), 9)
              << "Loop preview vertices must follow camera projection.";
        }
        EXPECT_GE(loop_preview_green_pixels(image,
            harness.cutting_preview_pixel({0.5f, -0.25f, 0.0f}), false), 9)
            << "The pending loop connector must remain visible while navigating.";
      }
      if (initial.isNull()) initial = image;
      else if (camera_x == 0.0f) EXPECT_EQ(image, initial);
      else {
        EXPECT_NE(image, initial);
        EXPECT_GT(std::abs(harness.cutting_preview_pixel(
            {0.5f, -0.25f, 0.0f}).x - first_probe.x), 10.0f);
      }
      const auto retained = modeling.volatile_overlay_snapshot();
      EXPECT_EQ(retained.frame, frozen.frame);
      EXPECT_EQ(retained.source.publication_revision, frozen.source.publication_revision);
      EXPECT_EQ(modeling.volatile_overlay_cache_stats().build_count, volatile_builds);
      EXPECT_EQ(harness.selection_frame_builds(), builds);
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_FALSE(harness.last_submission().dynamic_submitted);
      EXPECT_EQ(harness.tool_topology_selection().content_revision, selected.content_revision);
      EXPECT_EQ(harness.tool_topology_selection().selection_revision, selected.selection_revision);
      EXPECT_EQ(harness.undo_count(), undo);
    }

    // A new pointer sample after navigation must replace the frozen endpoint.
    // Use the edge midpoint, clear of the fixture's vertex-picking radius.
    ASSERT_TRUE(harness.update_tool_topology_hover({1.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge));
    EXPECT_NE(modeling.volatile_overlay_snapshot().source.publication_revision,
              frozen.source.publication_revision);
    const QImage resumed = harness.capture();
    ASSERT_FALSE(resumed.isNull());
    const QString resumed_path = QDir(directory).filePath(
        QStringLiteral("cutting_preview_%1_hover_resumed.png")
            .arg(QString::fromLatin1(label)));
    EXPECT_TRUE(resumed.save(resumed_path));
    std::cout << "Resumed cutting preview GPU png="
              << resumed_path.toStdString() << '\n';
    if (tool == ViewportToolId::Knife) {
      ASSERT_TRUE(modeling.tool_status().knife_has_current);
      EXPECT_GE(yellow_pixels(resumed,
          harness.cutting_preview_pixel({0.5f, -0.125f, 0.0f})), 3);
    } else {
      ASSERT_TRUE(modeling.tool_status().insert_edge_loop_has_preview);
      EXPECT_GE(loop_preview_green_pixels(resumed,
          harness.cutting_preview_pixel({0.5f, 0.0f, 0.0f}), false), 9);
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanBackFacingFaceTintsPreserveRearWiresAndFrontFacesStillOcclude) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-face-fill-depth-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) {
    directory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(directory));
  constexpr std::array<std::pair<float, const char *>, 3> kScales{{
      {1.0f, "100"}, {2.0f, "200"}, {1.0f, "scale_down_100"}}};
  for (const bool front_facing : {false, true}) {
    SCOPED_TRACE(front_facing ? "front-facing surface" : "culled back-facing surface");
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_face_fill_depth_sample(front_facing));
    const auto rear_wire_pixel = harness.face_fill_probe_pixel(true);
    const auto tint_pixel = harness.face_fill_probe_pixel(false);
    const auto save = [&](const QImage &image, const char *scale, const char *phase) {
      const QString path = QDir(directory).filePath(
          QStringLiteral("face_fill_%1_%2_%3.png")
              .arg(front_facing ? QStringLiteral("front") : QStringLiteral("back"),
                   QString::fromLatin1(scale), QString::fromLatin1(phase)));
      EXPECT_TRUE(image.save(path));
      std::cout << "Face fill GPU rear_cyan=" << cyan_pixels(image, rear_wire_pixel)
                << ", png=" << path.toStdString() << '\n';
    };
    const auto expect_rear_wire_visibility = [&](const QImage &image) {
      if (front_facing) {
        EXPECT_EQ(cyan_pixels(image, rear_wire_pixel), 0)
            << "The genuine front-facing source surface must still occlude rear wires.";
      } else {
        EXPECT_GE(cyan_pixels(image, rear_wire_pixel), 3)
            << "A translucent tint on a culled face must not occlude the rear source wire.";
      }
    };
    const auto expect_retained = [&](float scale) {
      const auto builds = harness.selection_frame_builds();
      EXPECT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, scale));
      EXPECT_EQ(harness.selection_frame_builds(), builds);
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_FALSE(harness.last_submission().dynamic_submitted);
    };
    QImage clear_at_100;
    for (const auto &[scale, label] : kScales) {
      SCOPED_TRACE(label);
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, scale));
      const QImage clear = harness.capture();
      ASSERT_FALSE(clear.isNull());
      save(clear, label, "clear");
      expect_rear_wire_visibility(clear);
      expect_retained(scale);
      if (clear_at_100.isNull()) clear_at_100 = clear;
      else if (scale == 1.0f) EXPECT_EQ(clear, clear_at_100);

      ASSERT_TRUE(harness.set_face_fill_hover(true));
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, scale));
      const QImage hovered = harness.capture();
      ASSERT_FALSE(hovered.isNull());
      save(hovered, label, "hovered");
      expect_rear_wire_visibility(hovered);
      EXPECT_NE(hovered.pixel(int(tint_pixel.x), int(tint_pixel.y)),
                clear.pixel(int(tint_pixel.x), int(tint_pixel.y)))
          << "The two-sided translucent hover fill must remain visible.";
      expect_retained(scale);

      ASSERT_TRUE(harness.set_face_fill_hover(false));
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, scale));
      const QImage restored = harness.capture();
      ASSERT_FALSE(restored.isNull());
      save(restored, label, "hover_left");
      EXPECT_EQ(restored, clear) << "Leaving the face must retire its tint and hover outline.";
      expect_rear_wire_visibility(restored);
      expect_retained(scale);
    }

    ASSERT_TRUE(harness.select_near_face_fill());
    QImage selected_at_100;
    for (const auto &[scale, label] : kScales) {
      SCOPED_TRACE(label);
      ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, scale));
      const QImage selected = harness.capture();
      ASSERT_FALSE(selected.isNull());
      save(selected, label, "selected");
      expect_rear_wire_visibility(selected);
      EXPECT_NE(selected.pixel(int(tint_pixel.x), int(tint_pixel.y)),
                clear_at_100.pixel(int(tint_pixel.x), int(tint_pixel.y)))
          << "The two-sided translucent selected-face fill must remain visible.";
      expect_retained(scale);
      if (selected_at_100.isNull()) selected_at_100 = selected;
      else if (scale == 1.0f) EXPECT_EQ(selected, selected_at_100);
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanOverlappingFaceFillsApplyOneAlphaAtWinningSelectionPriority) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-overlapping-face-fill-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) {
    directory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(directory));
  const auto patch = [](const QImage &image, QVec2 center) {
    return image.copy(int(center.x) - 2, int(center.y) - 2, 5, 5);
  };
  struct FaceFillCase {
    const char *label;
    std::vector<QuaderElementId> faces;
    QuaderElementId active;
    bool hover;
    std::optional<std::size_t> single_fill_reference;
  };
  const std::array<FaceFillCase, 8> cases{{
      {"clear", {3}, 3, false, std::nullopt},
      {"ordinary_single", {1, 3}, 3, false, std::nullopt},
      {"ordinary_overlap", {1, 2, 3}, 3, false, 1u},
      {"active_single", {1}, 1, false, std::nullopt},
      {"active_overlap", {1, 2}, 1, false, 3u},
      {"hover_single", {1, 3}, 3, true, std::nullopt},
      {"hover_overlap", {1, 2, 3}, 3, true, 5u},
      {"hover_active_overlap", {1, 2}, 1, true, 5u},
  }};
  constexpr std::array<std::pair<float, const char *>, 3> kScales{{
      {1.0f, "100"}, {2.0f, "200"}, {1.0f, "scale_down_100"}}};
  for (const auto surface : {ViewportRenderSurface::Swapchain, ViewportRenderSurface::Texture}) {
    const bool texture_surface = surface == ViewportRenderSurface::Texture;
    const auto surface_label = texture_surface ? QStringLiteral("texture") : QStringLiteral("native");
    SCOPED_TRACE(surface_label.toStdString());
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false, surface));
    ASSERT_TRUE(harness.create_overlapping_face_fill_sample());
    QImage default_ordinary_patch;
    QImage default_active_overlap;
    QImage default_hover_overlap;
    for (const bool custom_alpha : {false, true}) {
      SCOPED_TRACE(custom_alpha ? "custom alpha" : "default alpha");
      auto settings = ViewportSettings::defaults();
      if (custom_alpha) {
        auto selection = settings.color(ViewportColorSettingId::SelectionFace);
        auto hover = settings.color(ViewportColorSettingId::HoverFace);
        selection.a = 0.25f;
        hover.a = 0.4f;
        ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionFace, selection));
        ASSERT_TRUE(settings.set_color(ViewportColorSettingId::HoverFace, hover));
      }
      std::array<QImage, 8> at_100_percent;
      for (const auto &[scale, scale_label] : kScales) {
        SCOPED_TRACE(scale_label);
        if (texture_surface) {
          harness.resize_face_fill_sample(
              static_cast<std::uint32_t>(SelectionOverlayRuntimeHarness::kWidth * scale),
              static_cast<std::uint32_t>(SelectionOverlayRuntimeHarness::kHeight * scale));
        }
        const auto tint_pixel = harness.overlapping_face_fill_probe_pixel(false);
        const auto occluded_pixel = harness.overlapping_face_fill_probe_pixel(true);
        std::array<QImage, 8> samples;
        for (std::size_t index = 0; index < cases.size(); ++index) {
          const auto &sample = cases[index];
          SCOPED_TRACE(sample.label);
          ASSERT_TRUE(harness.set_overlapping_face_selection(
              sample.faces, sample.active, sample.hover));
          ASSERT_TRUE(harness.present_component_sample(
              ViewportOverlaySelectionContext::Face, scale, settings));
          QImage &image = samples[index];
          image = harness.capture();
          ASSERT_FALSE(image.isNull());
          const QString path = QDir(directory).filePath(
              QStringLiteral("face_union_%1_%2_%3_%4.png")
                  .arg(surface_label, custom_alpha ? QStringLiteral("custom") : QStringLiteral("default"),
                       QString::fromLatin1(scale_label), QString::fromLatin1(sample.label)));
          ASSERT_TRUE(image.save(path));
          const QColor tint = image.pixelColor(int(tint_pixel.x), int(tint_pixel.y));
          std::cout << "Face union GPU " << sample.label << " tint_rgba="
                    << tint.red() << ',' << tint.green() << ',' << tint.blue()
                    << ',' << tint.alpha()
                    << ", png=" << path.toStdString() << '\n';
          if (texture_surface) {
            // The offscreen target is transparent outside overlays, so its
            // alpha directly proves that the winning fill was applied once.
            const float expected_alpha = index == 0u ? 0.0f
                : settings.color(sample.hover ? ViewportColorSettingId::HoverFace
                                              : ViewportColorSettingId::SelectionFace).a;
            EXPECT_NEAR(tint.alpha(), std::lround(expected_alpha * 255.0f), 1);
          }
          if (sample.single_fill_reference) {
            EXPECT_EQ(patch(image, tint_pixel),
                      patch(samples[*sample.single_fill_reference], tint_pixel))
                << "Overlap must resolve to one authored-alpha fill of the winning class.";
          }
          EXPECT_EQ(patch(image, occluded_pixel), patch(samples[0], occluded_pixel))
              << "A genuine front-facing source surface must occlude every fill class.";
          if (at_100_percent[index].isNull()) {
            at_100_percent[index] = image;
          } else if (scale == 1.0f) {
            EXPECT_EQ(image, at_100_percent[index])
                << "Scale-down must restore the same fill, source wires, and outlines.";
          }
          const auto builds = harness.selection_frame_builds();
          ASSERT_TRUE(harness.present_component_sample(
              ViewportOverlaySelectionContext::Face, scale, settings));
          EXPECT_EQ(harness.selection_frame_builds(), builds);
          EXPECT_FALSE(harness.last_submission().persistent_submitted);
          EXPECT_FALSE(harness.last_submission().dynamic_submitted);
          if (texture_surface) {
            EXPECT_EQ(harness.capture(), image)
                << "The custom target must clear its stencil mask on every unchanged frame.";
          }
        }
        EXPECT_NE(patch(samples[1], tint_pixel), patch(samples[0], tint_pixel))
            << "The ordinary-selection reference must contain a visible fill.";
        EXPECT_NE(patch(samples[3], tint_pixel), patch(samples[1], tint_pixel))
            << "Active selection must retain its distinct fill color.";
        EXPECT_NE(patch(samples[5], tint_pixel), patch(samples[1], tint_pixel))
            << "The hover reference must retain its own fill color and alpha.";
        if (!custom_alpha && scale == 1.0f && default_active_overlap.isNull()) {
          default_active_overlap = samples[4];
          default_hover_overlap = samples[7];
        }
        if (default_ordinary_patch.isNull()) {
          default_ordinary_patch = patch(samples[1], tint_pixel);
        } else if (custom_alpha) {
          EXPECT_NE(patch(samples[1], tint_pixel), default_ordinary_patch)
              << "The authored selection alpha must remain configurable.";
        }
        if (texture_surface) {
          ASSERT_TRUE(harness.pan_face_fill_camera(0.15f));
          ASSERT_TRUE(harness.present_component_sample(
              ViewportOverlaySelectionContext::Face, scale, settings));
          const QImage panned = harness.capture();
          ASSERT_FALSE(panned.isNull());
          EXPECT_NE(panned, samples.back());
          EXPECT_EQ(patch(panned, harness.overlapping_face_fill_probe_pixel(false)),
                    patch(samples.back(), tint_pixel))
              << "Camera motion must not inherit a previous frame's stencil coverage.";
          EXPECT_EQ(patch(panned, harness.overlapping_face_fill_probe_pixel(true)),
                    patch(samples[0], occluded_pixel));
          ASSERT_TRUE(panned.save(QDir(directory).filePath(
              QStringLiteral("face_union_texture_%1_%2_camera_pan.png")
                  .arg(custom_alpha ? QStringLiteral("custom") : QStringLiteral("default"),
                       QString::fromLatin1(scale_label)))));
          ASSERT_TRUE(harness.pan_face_fill_camera(0.0f));
          ASSERT_TRUE(harness.present_component_sample(
              ViewportOverlaySelectionContext::Face, scale, settings));
          EXPECT_EQ(harness.capture(), samples.back())
              << "Restoring the camera must restore the complete offscreen overlay.";
        }
      }
    }
    const auto tint_pixel = harness.overlapping_face_fill_probe_pixel(false);
    const auto occluded_pixel = harness.overlapping_face_fill_probe_pixel(true);
    for (const bool transparent_hover : {true, false}) {
      SCOPED_TRACE(transparent_hover ? "transparent hover" : "transparent selection");
      auto settings = ViewportSettings::defaults();
      const auto transparent_setting = transparent_hover
          ? ViewportColorSettingId::HoverFace : ViewportColorSettingId::SelectionFace;
      auto transparent_color = settings.color(transparent_setting);
      transparent_color.a = 0.0f;
      ASSERT_TRUE(settings.set_color(transparent_setting, transparent_color));
      ASSERT_TRUE(harness.set_overlapping_face_selection({1, 2}, 1, true));
      ASSERT_TRUE(harness.present_component_sample(
          ViewportOverlaySelectionContext::Face, 1.0f, settings));
      const QImage image = harness.capture();
      ASSERT_FALSE(image.isNull());
      const auto &reference = transparent_hover ? default_active_overlap : default_hover_overlap;
      EXPECT_EQ(patch(image, tint_pixel), patch(reference, tint_pixel))
          << "A zero-alpha class must leave the underlying visible fill unchanged.";
      EXPECT_EQ(patch(image, occluded_pixel), patch(reference, occluded_pixel));
      ASSERT_TRUE(image.save(QDir(directory).filePath(
          QStringLiteral("face_union_%1_zero_%2.png")
              .arg(surface_label, transparent_hover ? QStringLiteral("hover")
                                                    : QStringLiteral("selection")))));
      const auto builds = harness.selection_frame_builds();
      ASSERT_TRUE(harness.present_component_sample(
          ViewportOverlaySelectionContext::Face, 1.0f, settings));
      EXPECT_EQ(harness.selection_frame_builds(), builds);
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_FALSE(harness.last_submission().dynamic_submitted);
      EXPECT_EQ(harness.capture(), image);
    }
  }
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

TEST(SelectionOverlayRuntimeTests, VulkanActiveObjectsGroupsAndInstancesSwitchBrightnessAt100And200Percent) {
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
  for (const int variant : {0, 1, 2}) {
    const bool groups = variant == 1;
    const bool instances = variant == 2;
    const QString label = groups ? QStringLiteral("groups")
        : instances ? QStringLiteral("instances") : QStringLiteral("objects");
    SCOPED_TRACE(label.toStdString());
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_active_selection_sample(groups, instances));
    for (const float scale : {1.0f, 2.0f}) {
      const QString stem = QStringLiteral("active-%1-%2")
          .arg(label).arg(int(scale * 100));
      QImage unselected;
      if (instances) {
        ASSERT_TRUE(harness.present_active_selection_sample(false, 0u, scale, false));
        unselected = harness.capture();
        ASSERT_FALSE(unselected.isNull());
        ASSERT_TRUE(unselected.save(QDir(directory).filePath(stem + QStringLiteral("-unselected.png"))));
      }
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
      if (instances) {
        const auto pixels = harness.active_face_pixels();
        const auto face_brightness = [](const QImage &image, QVec2 pixel) {
          int total = 0;
          for (int y = int(pixel.y) - 2; y <= int(pixel.y) + 2; ++y) {
            for (int x = int(pixel.x) - 2; x <= int(pixel.x) + 2; ++x) {
              const QColor color = image.pixelColor(x, y);
              total += color.red() + color.green() + color.blue();
            }
          }
          return total;
        };
        for (const auto pixel : pixels) {
          EXPECT_NE(face_brightness(left_active, pixel), face_brightness(unselected, pixel))
              << "Selected instance faces must contain an actual fill away from their outlines.";
          EXPECT_NE(face_brightness(right_active, pixel), face_brightness(unselected, pixel));
        }
        EXPECT_GT(face_brightness(left_active, pixels[0]), face_brightness(right_active, pixels[0]))
            << "The previous active instance face interior must dim.";
        EXPECT_LT(face_brightness(left_active, pixels[1]), face_brightness(right_active, pixels[1]))
            << "The new active instance face interior must brighten.";
      }
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
