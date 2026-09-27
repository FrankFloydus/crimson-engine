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
#include <filament/RenderTarget.h>
#include <filament/SwapChain.h>

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QRect>

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

  bool present_component_sample(ViewportOverlaySelectionContext context,
                                float scale = 1.0f,
                                const ViewportSettings &settings = ViewportSettings::defaults()) {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    if (!QuaderRenderApiRuntimeAccess::sync_scene(editor.render(), bridge_).ok()) {
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
    for (int frame = 0; frame < 5; ++frame) {
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
  ViewportRenderSurface surface_ = ViewportRenderSurface::Swapchain;
  QuaderModelingObjectRef face_fill_object_;
  QuaderModelingObjectRef tool_topology_object_;
  ViewportToolId tool_topology_tool_ = ViewportToolId::Select;
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
