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
#include "editor/render/standard_material_render_snapshot_adapter.hpp"
#include "editor/session/project_session.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "editor/tools/extrude_handle_layout.hpp"
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
#include "support/modeling_test_events.hpp"
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
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {
/// Restores the explicitly captured set after fixture-only scene construction.
bool restore_edit_participants(QuaderEditorApi &editor,
                               const QuaderEditorModeState &previous) {
  if (previous.mode != QuaderEditorMode::Edit) return true;
  std::vector<EntityId> participants;
  for (const auto &participant : previous.participants) participants.push_back(participant.id);
  return editor.selection().select_objects(participants, previous.active_entity.id).ok() &&
      editor.mode().set(QuaderEditorMode::Edit).ok() &&
      editor.selection().set_mode(previous.component_kind).ok();
}

/// Creates an occluder through Object Mode while retaining the original Edit set.
bool add_nonparticipant_occluder(QuaderEditorApi &editor,
                                 QuaderBounds bounds, std::string name) {
  const auto previous = editor.mode().state();
  if (!editor.mode().set(QuaderEditorMode::Object).ok()) return false;
  if (!editor.modeling().create_box(bounds, std::move(name)).ok()) return false;
  return restore_edit_participants(editor, previous);
}

/// Makes native fixture mode transitions explicit before changing selection kind.
quader::QuaderApiVoidResult enter_selection_context(
    quader::QuaderEditorApi &editor, quader::QuaderSelectionMode kind) {
  const bool component = kind == quader::QuaderSelectionMode::Vertex ||
      kind == quader::QuaderSelectionMode::Edge ||
      kind == quader::QuaderSelectionMode::Face;
  const auto transition = editor.mode().set(component
      ? quader::QuaderEditorMode::Edit : quader::QuaderEditorMode::Object);
  if (!transition.ok()) return transition;
  return editor.selection().set_mode(kind);
}


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

/** Counts production tool-frame construction behind the retained pane gate. */
class SelectionRuntimeToolFrame final
    : public ViewportOverlayDynamicToolFrameProvider {
public:
  SelectionRuntimeToolFrame(const ViewportToolOverlaySource &source,
                            std::uint64_t &builds)
      : source_(source), builds_(builds) {}

  ViewportOverlayFrame build_dynamic_tool_overlay_frame() const override {
    ++builds_;
    return source_.build_dynamic_tool_overlay_frame();
  }

private:
  const ViewportToolOverlaySource &source_;
  std::uint64_t &builds_;
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
    for (auto &pane : sibling_views_) {
      if (pane) pane->shutdown();
    }
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
                            filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER |
                            (surface == ViewportRenderSurface::SrgbSwapchain
                                 ? filament::SwapChain::CONFIG_SRGB_COLORSPACE : 0u));
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

  QVec2 overlay_audit_image_point(QVec3 world) const {
    auto pixel = camera_.project_world_point(world).position;
    pixel.y = static_cast<float>(kHeight - 1u) - pixel.y;
    return pixel;
  }

  bool create_outline_depth_quad(const std::array<MeshVertex, 4u> &points) {
    QuaderEditorApi api(QuaderApiSessionInternalAccess::from_scene_session(session_));
    const auto previous = api.mode().state();
    if (!api.mode().set(QuaderEditorMode::Object).ok()) return false;
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
        !api.assets().assign_material_to_entity(object.value().entity, material.value().id).ok() ||
        !restore_edit_participants(api, previous)) return false;
    bridge_.sync(session_.scene(), session_.assets());
    return true;
  }

  bool create_diagonal_inside_corner_sample(
      ViewportOverlayFrame &frame, ViewportProjection projection, float phase,
      const QuaderTransform3 *support_transform = nullptr, bool reverse_first_face = false) {
    const QVec2 start_pixel{160.0f + phase, 100.0f};
    const QVec2 end_pixel{450.0f + phase, 350.0f};
    const float length = std::hypot(end_pixel.x - start_pixel.x, end_pixel.y - start_pixel.y);
    const QVec2 normal{-(end_pixel.y - start_pixel.y) / length,
                       (end_pixel.x - start_pixel.x) / length};
    const auto incident_corner = [&](QVec2 pixel, float z, float side) {
      pixel.x += normal.x * 80.0f * side;
      pixel.y += normal.y * 80.0f * side;
      // An affine reciprocal-depth plane is planar after perspective
      // projection. Both faces approach the camera away from the crease.
      const float outer_z = projection == ViewportProjection::Perspective
          ? 5.0f - 1.0f / (1.0f / (5.0f - z) + 0.04f) : z + 4.0f;
      return overlay_audit_world_point(pixel, outer_z);
    };
    std::array points{
        overlay_audit_world_point(start_pixel, 0.0f),
        overlay_audit_world_point(end_pixel, -20.0f),
        incident_corner(end_pixel, -20.0f, 1.0f),
        incident_corner(start_pixel, 0.0f, 1.0f),
        incident_corner(start_pixel, 0.0f, -1.0f),
        incident_corner(end_pixel, -20.0f, -1.0f)};
    if (support_transform != nullptr) {
      for (std::size_t index = 2u; index < points.size(); ++index) {
        const QVec3 offset = points[index] - points[0];
        points[index] = points[0] + support_transform->x_axis * offset.x +
            support_transform->y_axis * offset.y + support_transform->z_axis * offset.z;
      }
    }
    QuaderApi api(QuaderApiSessionInternalAccess::from_scene_session(session_));
    quader_poly::Document document;
    std::array<quader_poly::ElementId, 6u> vertices;
    for (std::size_t index = 0u; index < points.size(); ++index) {
      vertices[index] = quader_poly::add_vertex(document, points[index]);
    }
    const auto positive_face = std::array{vertices[0], vertices[1], vertices[2], vertices[3]};
    const auto negative_face = std::array{vertices[1], vertices[0], vertices[4], vertices[5]};
    const auto first_face = quader_poly::add_face(document, reverse_first_face ? negative_face : positive_face);
    const auto second_face = quader_poly::add_face(document, reverse_first_face ? positive_face : negative_face);
    const std::array faces{reverse_first_face ? second_face : first_face,
                           reverse_first_face ? first_face : second_face};
    const auto object = api.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Diagonal inward corner wire regression"});
    const auto material = api.assets().add_material(QuaderMaterialAsset{
        .name = "Diagonal inward corner neutral gray",
        .base_color = {0.18f, 0.18f, 0.18f}, .roughness = 1.0f});
    if (!object.ok() || !material.ok() ||
        !api.assets().assign_material_to_entity(object.value().entity, material.value().id).ok()) return false;
    inside_corner_object_ = object.value();
    for (std::size_t index = 0u; index < inside_corner_support_vertices_.size(); ++index) {
      inside_corner_support_vertices_[index] = static_cast<QuaderElementId>(vertices[index + 2u]);
    }
    bridge_.sync(session_.scene(), session_.assets());
    frame.lines = {{
        .layer = ViewportOverlayLayer::SourceWire, .start = points[0], .end = points[1],
        .color = rgba8(40, 225, 255), .width_pixels = kOverlaySourceWireLineWidthPixels,
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::ComponentSelection,
        .element = {.object_id = object.value().modeling_object_id,
                    .edge_a = static_cast<int>(vertices[0]), .edge_b = static_cast<int>(vertices[1])}}};
    const std::array<std::array<std::size_t, 3u>, 4u> triangles{{
        {0u, 1u, 2u}, {0u, 2u, 3u}, {0u, 4u, 5u}, {0u, 5u, 1u}}};
    for (std::size_t index = 0u; index < triangles.size(); ++index) {
      const auto &triangle = triangles[index];
      frame.triangles.push_back({
          .layer = ViewportOverlayLayer::SourceWireDepthStamp,
          .a = points[triangle[0]], .b = points[triangle[1]], .c = points[triangle[2]],
          .source = ViewportOverlaySource::ComponentSelection,
          .element = {.object_id = object.value().modeling_object_id,
                      .vertex_id = static_cast<int>(vertices[triangle[0]]),
                      .edge_a = static_cast<int>(vertices[triangle[1]]),
                      .edge_b = static_cast<int>(vertices[triangle[2]]),
                      .face_id = static_cast<int>(faces[index / 2u])}});
    }
    return true;
  }

  bool select_inside_corner_native_wire() {
    auto &editor = array_editor();
    return editor.mode().set(QuaderEditorMode::Object).ok() &&
        editor.selection().select_object(inside_corner_object_).ok() &&
        enter_selection_context(editor, QuaderSelectionMode::Edge).ok();
  }

  bool open_native_project_sample(const std::filesystem::path &manifest) {
    captured_project_ = std::make_unique<ProjectSession>(manifest.parent_path() / "runtime-recent-scenes.json");
    captured_project_->set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) / "resources/modeling_assets");
    array_editor_ = std::make_unique<QuaderEditorApi>(*captured_project_);
    const auto opened = array_editor_->project().open(manifest);
    if (!opened.ok()) {
      std::cout << "PROJECT_OVERLAY_OPEN_FAILED " << opened.error_message() << std::endl;
      return false;
    }
    const auto *active = captured_project_->active_scene();
    if (active == nullptr || active->session == nullptr) return false;
    captured_project_scene_ = active->session.get();
    const auto objects = array_editor_->query().modeling_objects();
    if (objects.empty()) return false;
    const auto saved_selection = array_editor_->query().selection();
    std::cout << "PROJECT_OVERLAY_SAVED_SELECTION mode=" << static_cast<int>(saved_selection.mode)
              << " object=" << saved_selection.active_object_id << " active_face=" << saved_selection.active_face_id
              << " selected_faces=" << saved_selection.selected_face_count << std::endl;
    for (const auto &record : objects) {
      std::cout << "PROJECT_OVERLAY_OBJECT id=" << record.modeling_object_id
                << " entity=" << record.entity << " name=" << record.name << std::endl;
    }
    const auto object = array_editor_->query().modeling_object_for_id(objects.front().modeling_object_id);
    if (!object || !array_editor_->selection().select_object(*object).ok() ||
        !enter_selection_context(*array_editor_, QuaderSelectionMode::Edge).ok()) return false;
    captured_project_materials_ = std::make_unique<ProjectSessionRenderMaterialResolver>(*captured_project_);
    bridge_.set_project_material_resolver(captured_project_materials_.get());
    camera_ = captured_project_scene_->viewport_workspace().camera(ViewportPaneId::Perspective);
    camera_.set_viewport_size(kWidth, kHeight);
    const auto &scene_settings = captured_project_scene_->scene().scene_settings();
    camera_.set_clip_planes(scene_settings.camera.near_plane, scene_settings.camera.far_plane);
    auto settings = ViewportSettings::defaults();
    (void)settings.set_bool_value(ViewportBoolSettingId::ShowMeshGrid, true);
    const float grid_size = viewport_grid_world_size_from_contract(array_editor_->viewports().grid_preset());
    view_.set_material_presentation(ViewportMaterialDebugMode::None, false,
        settings.bool_value(ViewportBoolSettingId::ShowMeshGrid));
    view_.set_scene_layer_mask(kViewportShadedSceneLayerMask);
    view_.apply_scene_settings(scene_settings, false, settings);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, grid_size, settings,
        settings.bool_value(ViewportBoolSettingId::ShowGrid));
    bridge_.update_mesh_surface_grid(grid_size, settings);
    const auto pose = camera_.pose();
    std::cout << "PROJECT_OVERLAY_CAMERA eye=" << pose.eye.x << ',' << pose.eye.y << ',' << pose.eye.z
              << " target=" << pose.target.x << ',' << pose.target.y << ',' << pose.target.z
              << " near=" << camera_.state().near_plane << " far=" << camera_.state().far_plane << std::endl;
    return QuaderRenderApiRuntimeAccess::sync_scene(array_editor_->render(), bridge_).ok();
  }

  bool select_native_project_faces(int object_id, const std::vector<QuaderElementId> &faces,
                                   QuaderElementId active_face) {
    if (captured_project_scene_ == nullptr) return false;
    const auto object = array_editor_->query().modeling_object_for_id(object_id);
    return object && array_editor_->mode().set(QuaderEditorMode::Object).ok() &&
        array_editor_->selection().select_object(*object).ok() &&
        enter_selection_context(*array_editor_, QuaderSelectionMode::Face).ok() &&
        array_editor_->selection().select_components({
            .object = *object, .kind = QuaderComponentKind::Face, .faces = faces,
            .active = {.object = *object, .kind = QuaderComponentKind::Face, .face = active_face}}).ok();
  }

  ViewportCameraPose native_project_camera_pose() const { return camera_.pose(); }

  bool set_native_project_camera_pose(const ViewportCameraPose &pose) {
    if (!camera_.restore_perspective_pose(pose)) return false;
    view_.update_camera(camera_);
    return true;
  }

  bool present_native_project_sample(float scale = 1.0f, bool overlays_visible = true,
                                     bool dimensions_visible = false,
                                     const ViewportOverlaySelectionFrameProvider *reference = nullptr) {
    if (captured_project_scene_ == nullptr ||
        !QuaderRenderApiRuntimeAccess::sync_scene(array_editor_->render(), bridge_).ok() ||
        !QuaderRenderApiRuntimeAccess::sync_tool_previews(array_editor_->render(), bridge_).ok()) return false;
    const auto &modeling = captured_project_scene_->modeling();
    const auto volatile_overlay = modeling.volatile_overlay_snapshot();
    const auto context = array_editor_->query().selection().mode == QuaderSelectionMode::Face
        ? ViewportOverlaySelectionContext::Face : ViewportOverlaySelectionContext::Edge;
    last_submission_ = bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u), .raster_scale = scale,
         .camera = ViewportCameraAdapter::overlay_camera(camera_, {kWidth, kHeight}),
         .selection_source = {.source_identity = reference != nullptr
                 ? reinterpret_cast<std::uintptr_t>(reference) : reinterpret_cast<std::uintptr_t>(&modeling),
             .content_revision = modeling.content_revision(), .selection_revision = modeling.selection_revision(),
             .scene_revision = captured_project_scene_->scene().revision(),
             .asset_revision = captured_project_scene_->assets().revision(),
             .overlay_signature = modeling.selection_overlay_signature(),
             .selection_context = context},
         .selection_frame_provider = reference != nullptr ? reference : this,
         .volatile_modeling_overlay_frame = volatile_overlay.frame.get(),
         .volatile_modeling_source = {.source_identity = volatile_overlay.source.source_identity,
                                     .publication_revision = volatile_overlay.source.publication_revision},
         .scene_selection_mode = RenderOverlaySelectionMode::Component,
         .style_selection_context = context,
         .overlays_visible = overlays_visible, .selection_dimensions_visible = dimensions_visible},
        ViewportSettings::defaults());
    return true;
  }

  bool native_project_line_is_visible(const ViewportOverlayLine &line) const {
    if (captured_project_scene_ == nullptr) return false;
    for (int index = 1; index < 32; ++index) {
      const QVec3 point = line.start + (line.end - line.start) * (static_cast<float>(index) / 32.0f);
      const auto projected = camera_.project_world_point(point);
      if (!projected.visible || projected.position.x < 8.0f || projected.position.x >= kWidth - 8.0f ||
          projected.position.y < 8.0f || projected.position.y >= kHeight - 8.0f) return false;
      const auto ray = camera_.ray_from_viewport_point(projected.position);
      const auto surface = array_editor_->query().mesh_surface_distance({.origin = ray.origin, .direction = ray.direction});
      const float expected_distance = dot(point - ray.origin, ray.direction);
      if (!surface || std::abs(*surface - expected_distance) > 0.0001 * std::max(1.0f, expected_distance)) return false;
    }
    return true;
  }

  std::optional<ViewportOverlayLine> native_project_visible_line_segment(
      const ViewportOverlayLine &line) const {
    if (captured_project_scene_ == nullptr) return std::nullopt;
    constexpr int kSamples = 128;
    int run_start = 0;
    int best_start = 0;
    int best_end = 0;
    for (int index = 0; index <= kSamples; ++index) {
      const auto point = line.start + (line.end - line.start) * (static_cast<float>(index) / kSamples);
      const auto projected = camera_.project_world_point(point);
      bool visible = projected.visible && projected.position.x >= 8.0f && projected.position.x < kWidth - 8.0f &&
          projected.position.y >= 8.0f && projected.position.y < kHeight - 8.0f;
      if (visible) {
        const auto ray = camera_.ray_from_viewport_point(projected.position);
        const auto surface = array_editor_->query().mesh_surface_distance({.origin = ray.origin, .direction = ray.direction});
        const float distance = dot(point - ray.origin, ray.direction);
        visible = surface && std::abs(*surface - distance) <= 0.0001 * std::max(1.0f, distance);
      }
      if (!visible) run_start = index + 1;
      else if (index - run_start > best_end - best_start) {
        best_start = run_start;
        best_end = index;
      }
    }
    if (best_end <= best_start) return std::nullopt;
    auto result = line;
    result.start = line.start + (line.end - line.start) * (static_cast<float>(best_start) / kSamples);
    result.end = line.start + (line.end - line.start) * (static_cast<float>(best_end) / kSamples);
    return result;
  }

  bool begin_inside_corner_support_preview() {
    auto &editor = array_editor();
    return editor.mode().set(QuaderEditorMode::Object).ok() &&
        editor.selection().select_object(inside_corner_object_).ok() &&
        enter_selection_context(editor, QuaderSelectionMode::Vertex).ok() &&
        editor.selection().select_vertices(inside_corner_object_,
            {inside_corner_support_vertices_[0], inside_corner_support_vertices_[1],
             inside_corner_support_vertices_[2], inside_corner_support_vertices_[3]}).ok() &&
        editor.modeling().begin_transform_preview().ok();
  }

  bool sync_inside_corner_support_preview() {
    auto &editor = array_editor();
    return QuaderRenderApiRuntimeAccess::sync_scene(editor.render(), bridge_).ok() &&
        QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(), bridge_).ok();
  }

  bool update_inside_corner_support_preview(const QuaderTransform3 &transform, QVec3 pivot) {
    return array_editor().modeling().update_transform_matrix(transform, pivot).ok() &&
        sync_inside_corner_support_preview();
  }

  bool cancel_inside_corner_support_preview() {
    return array_editor().modeling().cancel_transform_preview().ok() &&
        sync_inside_corner_support_preview();
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
    if (!object.ok() || !enter_selection_context(editor, QuaderSelectionMode::Face).ok() ||
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

  void initialize_extrusion_panes(bool preserve_camera = false) {
    // Identical cameras expose any accidental active-pane styling difference.
    // The modest oblique angle keeps the complete disk on the 200% surface.
    if (!preserve_camera) camera_.set_view_direction({0.0f, 0.5f, -1.0f});
    view_.update_camera(camera_);
    for (std::size_t pane = 1u; pane <= sibling_views_.size(); ++pane) {
      auto &view = sibling_views_[pane - 1u];
      view = std::make_unique<ViewportRenderer>();
      view->initialize(*engine_, *scene_, registry_, materials_.ground_grid(),
          materials_.overlay_stencil_reset(),
          viewport_grid_layer_mask(static_cast<std::uint8_t>(pane)), surface_);
      sibling_cameras_[pane - 1u] = camera_;
      view->resize(kWidth, kHeight);
      view->update_camera(sibling_cameras_[pane - 1u]);
      view->set_overlay_visible(true);
      view->set_scene_layer_mask(kViewportRenderedSceneLayerMask);
      view->set_shadowing_enabled(false);
      view->set_material_debug_mode(ViewportMaterialDebugMode::Albedo);
      SceneSettings settings;
      settings.environment.enabled = false;
      settings.environment.show_background = false;
      settings.environment.clear_color = {};
      settings.post_process.anti_aliasing = false;
      settings.post_process.dithering = false;
      settings.post_process.ambient_occlusion = false;
      settings.post_process.bloom = false;
      settings.post_process.tone_mapping = SceneToneMapping::Linear;
      view->apply_scene_settings(settings, true, ViewportSettings::defaults());
      view->update_ground_grid(sibling_cameras_[pane - 1u], 1.0f,
                              ViewportSettings::defaults(), false);
    }
  }

  bool present_extrusion_pane(std::size_t pane, bool interaction_pane,
                             bool handle_visible, float scale) {
    capture_pane_ = pane;
    auto &camera = extrusion_pane_camera(pane);
    auto &view = extrusion_pane_view(pane);
    auto &editor = array_editor();
    if (!QuaderRenderApiRuntimeAccess::sync_scene(editor.render(), bridge_).ok() ||
        !QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(), bridge_).ok()) {
      return false;
    }
    const auto overlay_camera = ViewportCameraAdapter::overlay_camera(
        camera, {static_cast<float>(view.width()),
                 static_cast<float>(view.height())});
    const auto &handle = session_.extrude().handle_state();
    const ViewportToolOverlaySource source({
        .source_identity = reinterpret_cast<std::uintptr_t>(&session_.extrude()),
        .raster_scale = scale,
        .overlay_camera = &overlay_camera,
        .viewport_camera = &camera,
        .extrusion_handle = handle_visible ? &handle : nullptr,
        .interaction_pane = interaction_pane,
        .gizmos_visible = true,
        .overlays_visible = true});
    const SelectionRuntimeToolFrame provider(source, tool_frame_builds_);
    const auto &modeling = session_.modeling();
    last_submission_ = bridge_.submit_viewport_overlay(
        {.viewport_index = pane,
         .layer_mask = viewport_grid_layer_mask(static_cast<std::uint8_t>(pane)),
         .raster_scale = scale,
         .camera = overlay_camera,
         .selection_source = {
             .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
             .content_revision = modeling.content_revision(),
             .selection_revision = modeling.selection_revision(),
             .scene_revision = session_.scene().revision(),
             .asset_revision = session_.assets().revision(),
             .overlay_signature = modeling.selection_overlay_signature(),
             .selection_context = ViewportOverlaySelectionContext::Face},
         .selection_frame_provider = this,
         .dynamic_tool_source = source.source_stamp(),
         .dynamic_tool_frame_provider = &provider,
         .scene_selection_mode = RenderOverlaySelectionMode::Component,
         .style_selection_context = ViewportOverlaySelectionContext::Face,
         .overlays_visible = true,
         .selection_dimensions_visible = false}, ViewportSettings::defaults());
    return true;
  }

  std::uint64_t tool_frame_builds() const { return tool_frame_builds_; }

  std::optional<QVec2> extrusion_handle_pixel(std::size_t pane, float scale) {
    const auto layout = build_extrude_handle_layout(
        extrusion_pane_camera(pane), session_.extrude().handle_state(), scale);
    if (!layout.valid) return std::nullopt;
    auto pixel = layout.button_center_screen;
    pixel.y = static_cast<float>(extrusion_pane_view(pane).height() - 1u) - pixel.y;
    return pixel;
  }

  void resize_extrusion_pane(std::size_t pane, std::uint32_t width,
                             std::uint32_t height) {
    auto &camera = extrusion_pane_camera(pane);
    auto &view = extrusion_pane_view(pane);
    camera.set_viewport_size(width, height);
    view.resize(width, height);
    view.update_camera(camera);
  }

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
    if (!object.ok() || !editor.selection().select_object(object.value()).ok() ||
        !editor.mode().set(QuaderEditorMode::Edit).ok() ||
        !editor.tools().activate(ViewportToolId::InsertEdgeLoop).ok()) return false;
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.focus_origin(3.0f);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  bool create_closed_edge_loop_sample() {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    const auto object = editor.modeling().create_box(
        {.min = {-1.0f, -0.75f, -0.75f}, .max = {1.0f, 0.75f, 0.75f}},
        "Self-occluded edge loop pixel regression");
    return object.ok() && editor.mode().set(QuaderEditorMode::Edit).ok() &&
        editor.tools().activate(ViewportToolId::InsertEdgeLoop).ok();
  }

  bool edge_loop_point_is_scene_visible(QVec3 world) {
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session_));
    const auto projected = camera_.project_world_point(world);
    const auto ray = camera_.ray_from_viewport_point(projected.position);
    const auto surface = editor.query().mesh_surface_distance(
        {.origin = ray.origin, .direction = ray.direction});
    const float distance = dot(world - ray.origin, ray.direction);
    return !surface || *surface >= distance - 0.001f;
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
        !enter_selection_context(editor, QuaderSelectionMode::Edge).ok() ||
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
    return editor.mode().set(QuaderEditorMode::Object).ok() &&
           editor.selection().select_object(tool_topology_object_).ok() &&
           enter_selection_context(editor, QuaderSelectionMode::Edge).ok() &&
           editor.tools().activate(tool_topology_tool_).ok();
  }

  bool occlude_tool_topology_internal_edge() {
    auto &editor = array_editor();
    return add_nonparticipant_occluder(editor,
        {.min = {-0.2f, 0.2f, 0.2f}, .max = {0.2f, 0.6f, 0.4f}},
        "Cutting topology foreground occluder");
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
    if (!object.ok() || !enter_selection_context(editor, QuaderSelectionMode::Face).ok()) {
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
    // pixels. Offscreen face 3 controls semantic selection count and primary
    // identity without adding coverage to the fill probe.
    quad(-1.0f, 0.3f, -0.65f, 0.65f, 0.0f, false);
    quad(-0.3f, 1.0f, -0.65f, 0.65f, 0.0f, false);
    quad(10.0f, 11.0f, 0.0f, 0.5f, 0.0f, false);
    // This unselected opaque surface must still block every tint behind it.
    quad(-0.2f, 0.2f, 0.2f, 0.5f, 0.25f, true);
    const auto object = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Overlapping component face fill regression"});
    if (!object.ok() || !enter_selection_context(editor, QuaderSelectionMode::Face).ok()) {
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

  bool create_shared_face_edge_sample(bool partial_overlap) {
    auto &editor = array_editor();
    const std::array<QVec3, 2u> edge{{{-1.0f, -2.0f, 0.0f}, {1.0f, 2.0f, -20.0f}}};
    const float normal_length = std::sqrt(420.0f);
    const QVec3 inward{20.0f / normal_length, 40.0f / normal_length, 10.0f / normal_length};
    std::array<QuaderModelingObjectRef, 2u> objects;
    std::array<QuaderElementId, 2u> faces;
    // Separate modeling objects deliberately keep distinct edge identities at
    // the coincident world-space boundary. The two planes meet in an inward
    // corner, so their actual source surfaces also exercise wire depth tests.
    for (std::size_t index = 0u; index < 2u; ++index) {
      const bool active = index == 1u;
      const float begin = !active && partial_overlap ? 0.2f : 0.0f;
      const float end = !active && partial_overlap ? 0.8f : 1.0f;
      const QVec3 a = edge[0] + (edge[1] - edge[0]) * begin;
      const QVec3 b = edge[0] + (edge[1] - edge[0]) * end;
      const QVec3 outward = inward + (active ? QVec3{2.0f, -1.0f, 0.0f}
                                           : QVec3{-2.0f, 1.0f, 0.0f});
      const std::array points = active ? std::array{b, a, a + outward, b + outward}
                                      : std::array{a, b, b + outward, a + outward};
      quader_poly::Document document;
      std::array<quader_poly::ElementId, 4u> vertices;
      for (std::size_t vertex = 0u; vertex < points.size(); ++vertex) {
        vertices[vertex] = quader_poly::add_vertex(document, points[vertex]);
      }
      const auto face = quader_poly::add_face(document, vertices);
      const auto object = editor.modeling().create_mesh(
          QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
          {.name = active ? "Active shared face edge" : "Ordinary shared face edge"});
      if (!object.ok()) return false;
      objects[index] = object.value();
      faces[index] = static_cast<QuaderElementId>(face);
    }
    if (!editor.selection().select_objects(
            {objects[0].entity, objects[1].entity}, objects[1].entity).ok() ||
        !enter_selection_context(editor, QuaderSelectionMode::Face).ok()) return false;
    for (std::size_t index = 0u; index < objects.size(); ++index) {
      if (!editor.selection().select_components({
              .object = objects[index], .kind = QuaderComponentKind::Face,
              .faces = {faces[index]},
              .active = {.object = objects[index], .kind = QuaderComponentKind::Face,
                         .face = faces[index]},
              .edit = index == 0u ? QuaderSelectionEdit::Replace : QuaderSelectionEdit::Add}).ok()) {
        return false;
      }
    }
    return enter_selection_context(editor, QuaderSelectionMode::Face).ok();
  }

  bool create_shared_face_edge_occluder() {
    return add_nonparticipant_occluder(array_editor(),
        {.min = {-0.4f, -0.4f, 1.0f}, .max = {0.4f, 0.4f, 1.2f}},
        "Shared face edge genuine occluder");
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

  bool create_component_transform_sample(ViewportOverlaySelectionContext context,
                                         bool single_vertex = false) {
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
      if (single_vertex) selection.vertices.resize(1u);
    } else if (context == ViewportOverlaySelectionContext::Edge) {
      mode = QuaderSelectionMode::Edge;
      selection.kind = QuaderComponentKind::Edge;
      selection.edges = {{front[0], front[3]}, {front[1], front[2]}};
    } else {
      selection.kind = QuaderComponentKind::Face;
      selection.faces = {static_cast<QuaderElementId>(face)};
    }
    if (!enter_selection_context(editor, mode).ok() ||
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

  bool create_closed_edge_extrusion_sample(bool below) {
    auto &editor = array_editor();
    auto document = quader_poly::make_cube({});
    std::array<QuaderElementId, 2> endpoints{};
    for (const auto &vertex : document.vertices) {
      if (vertex.position.x == 1.0f && vertex.position.y == -1.0f) {
        endpoints[vertex.position.z < 0.0f ? 0u : 1u] = vertex.id;
      }
    }
    if (endpoints[0] == 0u || endpoints[1] == 0u) return false;
    if (below) {
      // This cyan boundary lies behind the future underside and in front of the cube.
      const std::array witness{
          quader_poly::add_vertex(document, {1.25f, -5.0f / 6.0f, -0.35f}),
          quader_poly::add_vertex(document, {1.25f, -0.55f, -0.35f}),
          quader_poly::add_vertex(document, {1.25f, -0.55f, 0.35f}),
          quader_poly::add_vertex(document, {1.25f, -5.0f / 6.0f, 0.35f})};
      (void)quader_poly::add_face(document, witness);
    }
    const auto object = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Closed cube edge extrusion pixel regression"});
    if (!object.ok() || !enter_selection_context(editor, QuaderSelectionMode::Edge).ok() ||
        !editor.selection().select_components({
            .object = object.value(), .kind = QuaderComponentKind::Edge,
            .edges = {{endpoints[0], endpoints[1]}}}).ok() ||
        !camera_.restore_perspective_pose({
            .eye = {6.0f, below ? -4.0f : 4.0f, 0.0f},
            .target = {0.5f, -0.25f, 0.0f}, .up = {0.0f, 1.0f, 0.0f}})) {
      return false;
    }
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    return true;
  }

  OverlayRenderableCacheStats component_overlay_stats() const {
    return bridge_.overlay_stats();
  }

  bool present_component_sample(ViewportOverlaySelectionContext context,
                                float scale = 1.0f,
                                const ViewportSettings &settings = ViewportSettings::defaults(),
                                bool sync_preview = false,
                                const ViewportOverlaySelectionFrameProvider *reference = nullptr,
                                bool dimensions_visible = false) {
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
             .source_identity = reference != nullptr
                 ? reinterpret_cast<std::uintptr_t>(reference)
                 : reinterpret_cast<std::uintptr_t>(&modeling),
             .content_revision = modeling.content_revision(),
             .selection_revision = modeling.selection_revision(),
             .scene_revision = session_.scene().revision(),
             .asset_revision = session_.assets().revision(),
             .overlay_signature = modeling.selection_overlay_signature(),
             .selection_context = context},
         .selection_frame_provider = reference != nullptr ? reference : this,
         .volatile_modeling_overlay_frame = volatile_overlay.frame.get(),
         .volatile_modeling_source = {
             .source_identity = volatile_overlay.source.source_identity,
             .publication_revision = volatile_overlay.source.publication_revision},
         .scene_selection_mode = RenderOverlaySelectionMode::Component,
         .style_selection_context = context,
         .overlays_visible = true,
         .selection_dimensions_visible = dimensions_visible},
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
    if (!object.ok() || !enter_selection_context(editor, QuaderSelectionMode::Face).ok() ||
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
    if (!enter_selection_context(editor, groups ? QuaderSelectionMode::Group : QuaderSelectionMode::Object).ok()) return false;
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
                                       bool selected = true,
                                       const ViewportSettings *appearance = nullptr,
                                       bool singleton = false) {
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session_));
    const auto selection = selected
        ? (singleton
               ? editor.selection().select_objects({active_entities_[active]}, active_entities_[active])
               : editor.selection().select_objects(active_entities_, active_entities_[active]))
        : editor.selection().clear();
    if (!selection.ok() ||
        !QuaderRenderApiRuntimeAccess::sync_scene(editor.render(), bridge_).ok()) return false;
    const auto settings = appearance != nullptr ? *appearance : ViewportSettings::defaults();
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

  std::uint64_t tool_preview_outline_build_count() const {
    return bridge_.tool_preview_outline_build_count();
  }

  QuaderEditorApi &workflow_editor() { return array_editor(); }

  bool present_workflow_pane(std::size_t pane = 0u,
                             const ViewportSettings *appearance = nullptr,
                             bool dimensions_visible = false) {
    auto &editor = array_editor();
    auto &modeling = session_.modeling();
    auto &view = extrusion_pane_view(pane);
    auto &camera = extrusion_pane_camera(pane);
    capture_pane_ = pane;
    const auto mode = modeling.selection_mode();
    const bool selection_visible =
        modeling.active_viewport_tool() != ModelingViewportTool::Cut;
    const bool outline_only = selection_visible &&
        (mode == ModelingSelectionMode::Object || mode == ModelingSelectionMode::Group);
    const auto context = mode == ModelingSelectionMode::Vertex ? ViewportOverlaySelectionContext::Vertex
        : mode == ModelingSelectionMode::Edge ? ViewportOverlaySelectionContext::Edge
        : mode == ModelingSelectionMode::Face ? ViewportOverlaySelectionContext::Face
        : mode == ModelingSelectionMode::Group ? ViewportOverlaySelectionContext::Group
                                              : ViewportOverlaySelectionContext::Object;
    const std::array panes{ViewportPaneId::Perspective, ViewportPaneId::Top};
    std::uint8_t wire_mask = 0u;
    const auto settings = appearance != nullptr ? *appearance : ViewportSettings::defaults();
    const auto style = ViewportOverlayStyleResolver::persistent_style_snapshot(settings);
    ViewportPersistentOverlayRasterGroupSet groups;
    for (std::size_t index = 0u; index < panes.size(); ++index) {
      const auto mask = viewport_grid_layer_mask(static_cast<std::uint8_t>(index));
      if (editor.viewports().shading_mode(panes[index]) == QuaderShadingMode::Wireframe)
        wire_mask = static_cast<std::uint8_t>(wire_mask | mask);
      if (!groups.add(mask, style)) return false;
    }
    const bool wireframe = editor.viewports().shading_mode(panes[pane]) == QuaderShadingMode::Wireframe;
    view.set_scene_visible(!wireframe);
    if (!QuaderRenderApiRuntimeAccess::sync_scene(editor.render(), bridge_).ok() ||
        !QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(), bridge_).ok()) return false;
    // Match Qt's shared persistent routing, including selected-wire demotion.
    if (!QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays(
            editor.render(), bridge_, active_snapshots_,
            {.selected_object_overlay_visible = outline_only && selection_visible,
             .scene_wireframe_layer_mask = wire_mask,
             .selected_scene_wire_visible = false,
             .selected_mesh_outline_only = outline_only,
             .style = style, .raster_groups = groups}).ok()) return false;
    const auto &scene = QuaderRenderApiRuntimeAccess::build_scene_overlay_snapshot(
        editor.render(), active_snapshots_);
    selection_frame_options_.suppress_persistent_object_selection =
        mode == ModelingSelectionMode::Object;
    const auto volatile_overlay = modeling.volatile_overlay_snapshot();
    const bool retained_outlines = bridge_.tool_preview_suppression().empty();
    const bool gpu_outlines = view.update_selected_mesh_outlines(
        outline_only ? active_snapshots_.selected_object_snapshot() : nullptr,
        outline_only ? &scene : nullptr, style,
        outline_only && retained_outlines && selection_visible) && retained_outlines;
    last_submission_ = bridge_.submit_viewport_overlay(
        {.viewport_index = pane,
         .layer_mask = viewport_grid_layer_mask(static_cast<std::uint8_t>(pane)),
         .camera = ViewportCameraAdapter::overlay_camera(camera, {kWidth, kHeight}),
         .selection_source = {
             .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
             .content_revision = modeling.content_revision(),
             .selection_revision = modeling.selection_revision(),
             .scene_revision = session_.scene().revision(),
             .asset_revision = session_.assets().revision(),
             .overlay_signature = modeling.selection_overlay_signature(selection_frame_options_),
             .selection_context = context},
         .selection_frame_provider = this,
         .scene_overlay_snapshot = &scene,
         .selected_mesh_outline_snapshot = outline_only ? active_snapshots_.selected_object_snapshot() : nullptr,
         .gpu_mesh_outlines = gpu_outlines,
         .volatile_modeling_overlay_frame = volatile_overlay.frame.get(),
         .volatile_modeling_source = {.source_identity = volatile_overlay.source.source_identity,
             .publication_revision = volatile_overlay.source.publication_revision},
         .scene_selection_mode = ViewportToolOverlayProjection::selection_mode(mode),
         .style_selection_context = context,
         .overlays_visible = true,
         .selection_overlays_visible = selection_visible,
         .scene_overlay_requested = true,
         .wireframe_pane = wireframe,
         .selection_dimensions_visible = dimensions_visible}, settings);
    return true;
  }

  void sync_selected_object_snapshot(
      const RenderOverlaySelectedObjectSnapshot &snapshot) {
    bridge_.sync_scene_asset_overlays(snapshot, {}, {});
  }

  void sync_scene_wireframe_snapshot(
      const RenderOverlaySceneWireframeSnapshot &snapshot) {
    bridge_.sync_scene_asset_overlays({}, snapshot, {});
  }

  void sync_render_scene(const SceneSession &session) {
    bridge_.sync(session.scene(), session.assets());
  }

  bool present_mesh_outline_snapshot(const RenderOverlaySelectedObjectSnapshot &snapshot,
                                     float scale = 1.0f, bool gpu = true,
                                     bool visible = true,
                                     const ViewportSettings *appearance = nullptr) {
    const auto settings = appearance != nullptr ? *appearance : ViewportSettings::defaults();
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
    return (captured_project_scene_ != nullptr ? *captured_project_scene_ : session_)
        .modeling().selection_overlay_render_frame(selection_frame_options_);
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
    auto &view = extrusion_pane_view(capture_pane_);
    const auto destroy_target = [this](filament::RenderTarget *target) {
      if (target != nullptr) engine_->destroy(target);
    };
    std::unique_ptr<filament::RenderTarget, decltype(destroy_target)>
        texture_read_target(nullptr, destroy_target);
    if (surface_ == ViewportRenderSurface::Texture) {
      auto *texture = registry_.texture(view.overlay_texture_handle());
      if (texture == nullptr) return {};
      texture_read_target.reset(filament::RenderTarget::Builder()
                                    .texture(filament::RenderTarget::AttachmentPoint::COLOR, texture)
                                    .build(*engine_));
      if (!texture_read_target) return {};
    }
    for (int frame = 0; frame < (warm_up ? 5 : 0); ++frame) {
      if (!begin_frame()) return {};
      view.render(*renderer_);
      renderer_->endFrame();
    }
    if (!begin_frame()) return {};
    view.render(*renderer_);
    auto readback = std::make_shared<Readback>();
    const auto width = view.width();
    const auto height = view.height();
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
  ViewportRenderer &extrusion_pane_view(std::size_t pane) {
    return pane == 0u ? view_ : *sibling_views_[pane - 1u];
  }

  ViewportCameraController &extrusion_pane_camera(std::size_t pane) {
    return pane == 0u ? camera_ : sibling_cameras_[pane - 1u];
  }

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
  std::unique_ptr<ProjectSession> captured_project_;
  std::unique_ptr<ProjectSessionRenderMaterialResolver> captured_project_materials_;
  SceneSession *captured_project_scene_ = nullptr;
  std::unique_ptr<QuaderEditorApi> array_editor_;
  ViewportRenderSurface surface_ = ViewportRenderSurface::Swapchain;
  QuaderModelingObjectRef face_fill_object_;
  QuaderModelingObjectRef tool_topology_object_;
  QuaderModelingObjectRef inside_corner_object_;
  std::array<QuaderElementId, 4u> inside_corner_support_vertices_{};
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
  std::array<std::unique_ptr<ViewportRenderer>, 3> sibling_views_;
  std::array<ViewportCameraController, 3> sibling_cameras_;
  std::size_t capture_pane_ = 0u;
  std::uint64_t tool_frame_builds_ = 0u;
  SelectionRuntimeFrame selected_vertex_frame_;
  SelectionRuntimeFrame overlay_audit_frame_;
  ModelingSelectionOverlayOptions selection_frame_options_;
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

QImage strongest_single_wire_coverage(const QImage &first, const QImage &second,
                                     const QImage &background) {
  QImage result = first.copy();
  const auto color_delta = [](QColor color, QColor base) {
    return std::max({std::abs(color.red() - base.red()),
        std::abs(color.green() - base.green()), std::abs(color.blue() - base.blue())});
  };
  for (int y = 0; y < result.height(); ++y) {
    for (int x = 0; x < result.width(); ++x) {
      const auto base = background.pixelColor(x, y);
      const auto alternate = second.pixelColor(x, y);
      if (color_delta(alternate, base) > color_delta(first.pixelColor(x, y), base)) {
        result.setPixelColor(x, y, alternate);
      }
    }
  }
  return result;
}

void expect_native_wire_coverage(const QImage &actual, const QImage &reference,
    const QImage &backdrop, QVec2 start, QVec2 end, float scale, const char *label) {
  ASSERT_FALSE(actual.isNull());
  ASSERT_FALSE(reference.isNull());
  ASSERT_FALSE(backdrop.isNull());
  const float length = std::hypot(end.x - start.x, end.y - start.y);
  ASSERT_GT(length, 20.0f);
  const QVec2 direction{(end.x - start.x) / length, (end.y - start.y) / length};
  std::vector<int> expected_bins(static_cast<std::size_t>(std::ceil(length)), 0);
  std::vector<int> actual_bins(expected_bins.size(), 0);
  int expected_pixels = 0;
  int missing_pixels = 0;
  int feather_pixels = 0;
  int maximum_error = 0;
  int maximum_reference_delta = 0;
  std::array<int, 256u> reference_delta_counts{};
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const float px = x + 0.5f - start.x;
      const float py = y + 0.5f - start.y;
      const float along = px * direction.x + py * direction.y;
      const float across = -px * direction.y + py * direction.x;
      if (along < 8.0f || along > length - 8.0f || std::abs(across) > 3.0f * scale) continue;
      const auto base = backdrop.pixelColor(x, y);
      const auto expected = reference.pixelColor(x, y);
      const auto observed = actual.pixelColor(x, y);
      const int expected_delta = std::max({std::abs(expected.red() - base.red()),
          std::abs(expected.green() - base.green()), std::abs(expected.blue() - base.blue())});
      if (expected_delta < 4) continue;
      const int actual_delta = std::max({std::abs(observed.red() - base.red()),
          std::abs(observed.green() - base.green()), std::abs(observed.blue() - base.blue())});
      ++expected_pixels;
      missing_pixels += actual_delta * 2 < expected_delta ? 1 : 0;
      ++reference_delta_counts[static_cast<std::size_t>(expected_delta)];
      maximum_reference_delta = std::max(maximum_reference_delta, expected_delta);
      expected_bins[static_cast<std::size_t>(along)] += expected_delta;
      actual_bins[static_cast<std::size_t>(along)] += actual_delta;
      maximum_error = std::max({maximum_error, std::abs(expected.red() - observed.red()),
          std::abs(expected.green() - observed.green()), std::abs(expected.blue() - observed.blue())});
    }
  }
  // Feather is partial coverage relative to this stroke's strongest coverage.
  // An absolute RGB cutoff misclassifies saturated strokes over a bright surface.
  for (int delta = 4; delta + 3 < maximum_reference_delta; ++delta) {
    feather_pixels += reference_delta_counts[static_cast<std::size_t>(delta)];
  }
  int gap = 0;
  int longest_gap = 0;
  for (std::size_t index = 0u; index < expected_bins.size(); ++index) {
    gap = expected_bins[index] > 4 && actual_bins[index] * 2 < expected_bins[index] ? gap + 1 : 0;
    longest_gap = std::max(longest_gap, gap);
  }
  std::cout << "NATIVE_SOURCE_WIRE sample=" << label << " scale=" << scale
            << " expected_pixels=" << expected_pixels << " missing_pixels=" << missing_pixels
            << " feather_pixels=" << feather_pixels << " longest_gap=" << longest_gap
            << " maximum_error=" << maximum_error << std::endl;
  // Endpoint guards exclude sixteen pixels of length from the sampled strip.
  EXPECT_GT(expected_pixels, static_cast<int>((length - 16.0f) * 0.8f));
  EXPECT_GT(feather_pixels, 0);
  EXPECT_EQ(missing_pixels, 0);
  EXPECT_EQ(longest_gap, 0) << "Native traversal must preserve continuous diagonal coverage.";
  EXPECT_LE(maximum_error, 3);
}

TEST(SelectionOverlayRuntimeTests,
     VulkanSequentialBevelCornerChainRetainsPreviewCommitAndHistoryPixels) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for sequential bevel captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-sequential-bevel-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  QString output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (output.isEmpty()) {
    output = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(output));
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.set_native_project_camera_pose(
      {.eye = {6.0f, 5.0f, 6.0f}, .target = {}, .up = {0.0f, 1.0f, 0.0f}}));
  auto &editor = harness.workflow_editor();
  const auto created = editor.modeling().create_box(
      {.min = {-2.0f, -2.0f, -2.0f}, .max = {2.0f, 2.0f, 2.0f}},
      "Sequential bevel GPU corner");
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto object = created.value();
  ASSERT_TRUE(editor.selection().select_object(object).ok());
  ASSERT_TRUE(enter_selection_context(editor, QuaderSelectionMode::Edge).ok());
  const auto select_edges = [&](bool top_perimeter) {
    std::vector<std::pair<QuaderElementId, QuaderElementId>> edges;
    for (const auto &line : editor.query().selection_overlay().lines) {
      if (line.element.object_id != object.modeling_object_id ||
          (line.layer != QuaderOverlayLayer::SourceWire &&
           line.layer != QuaderOverlayLayer::SelectedEdge)) continue;
      const auto eligible = [&](const QVec3 &position) {
        return top_perimeter ? std::abs(position.y - 2.0f) < 1.0e-5f
            : position.x > 0.0f && position.z > 0.0f &&
                  std::abs(position.x - position.z) < 1.0e-5f;
      };
      if (eligible(line.start) && eligible(line.end)) {
        edges.emplace_back(std::min(line.element.edge_a, line.element.edge_b),
                           std::max(line.element.edge_a, line.element.edge_b));
      }
    }
    std::ranges::sort(edges);
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    ASSERT_EQ(edges.size(), top_perimeter ? 4u : 3u);
    ASSERT_TRUE(editor.selection().clear().ok());
    for (const auto &[a, b] : edges) {
      ASSERT_TRUE(editor.selection().select_edge(
          object, a, b, QuaderSelectionEdit::Add).ok());
    }
  };
  const auto begin_bevel = [&](double width) {
    ASSERT_TRUE(editor.commands().execute(
        editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
    ASSERT_TRUE(editor.tools().set_setting(
        "edge_bevel", "operation.edge_bevel.width", width).ok());
    ASSERT_TRUE(editor.tools().set_setting(
        "edge_bevel", "operation.edge_bevel.segments", 2.0).ok());
    ASSERT_TRUE(editor.tools().set_setting(
        "edge_bevel", "operation.edge_bevel.profile", 0.5).ok());
  };
  const auto capture = [&](const char *phase) {
    EXPECT_TRUE(harness.present_component_sample(
        ViewportOverlaySelectionContext::Edge, 1.0f,
        ViewportSettings::defaults(), true));
    const QImage image = harness.capture();
    EXPECT_FALSE(image.isNull());
    const QString path = QDir(output).filePath(
        QStringLiteral("sequential_bevel_%1.png").arg(QString::fromLatin1(phase)));
    EXPECT_TRUE(image.save(path));
    std::cout << "Sequential bevel GPU png=" << path.toStdString() << '\n';
    const auto builds = harness.selection_frame_builds();
    EXPECT_TRUE(harness.present_component_sample(
        ViewportOverlaySelectionContext::Edge, 1.0f,
        ViewportSettings::defaults(), true));
    EXPECT_EQ(harness.selection_frame_builds(), builds);
    EXPECT_FALSE(harness.last_submission().persistent_submitted);
    EXPECT_FALSE(harness.last_submission().dynamic_submitted);
    EXPECT_EQ(harness.capture(), image);
    return image;
  };
  const auto changed_pixels = [](const QImage &first, const QImage &second) {
    std::size_t changed = 0u;
    if (first.isNull() || second.isNull() || first.size() != second.size()) return changed;
    for (int y = 0; y < first.height(); ++y) {
      for (int x = 0; x < first.width(); ++x) {
        const auto a = first.pixelColor(x, y);
        const auto b = second.pixelColor(x, y);
        if (std::abs(a.red() - b.red()) > 2 ||
            std::abs(a.green() - b.green()) > 2 ||
            std::abs(a.blue() - b.blue()) > 2) ++changed;
      }
    }
    return changed;
  };
  ASSERT_NO_FATAL_FAILURE(select_edges(true));
  ASSERT_NO_FATAL_FAILURE(begin_bevel(0.5));
  ASSERT_TRUE(editor.commands().execute(
      quader_command::kModelingApplyActivePreview).ok());
  (void)capture("first_bevel");
  ASSERT_NO_FATAL_FAILURE(select_edges(false));
  const auto source = capture("selected_corner_chain");
  const auto initial_history = harness.undo_count();
  ASSERT_NO_FATAL_FAILURE(begin_bevel(0.2));
  const auto preview = capture("second_preview");
  EXPECT_GT(changed_pixels(source, preview), 32u);
  EXPECT_EQ(harness.undo_count(), initial_history);
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.width", 0.25).ok());
  const auto widened = capture("wider_preview");
  EXPECT_GT(changed_pixels(preview, widened), 32u);
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.profile", 0.25).ok());
  const auto profiled = capture("profile_preview");
  EXPECT_GT(changed_pixels(widened, profiled), 8u);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(capture("canceled"), source);
  EXPECT_EQ(harness.undo_count(), initial_history);

  ASSERT_NO_FATAL_FAILURE(begin_bevel(0.2));
  EXPECT_EQ(capture("restored_preview"), preview);
  ASSERT_TRUE(editor.commands().execute(
      quader_command::kModelingApplyActivePreview).ok());
  const auto committed = capture("committed");
  EXPECT_EQ(committed, preview);
  EXPECT_EQ(harness.undo_count(), initial_history + 1u);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(capture("undone"), source);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(capture("redone"), committed);
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
        EXPECT_GT(active_boundary.blue(), active_boundary.green())
            << "The active selected boundary uses pink over the same backdrop.";
        EXPECT_GT(ordinary_boundary.green(), ordinary_boundary.blue());
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

TEST(SelectionOverlayRuntimeTests,
     VulkanSlopedComponentSourceWirePreservesSurfaceDepth) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-sloped-component-wire-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    for (const float far_z : {-20.0f, -145.0f}) {
      SCOPED_TRACE(static_cast<int>(projection));
      SCOPED_TRACE(far_z);
      SelectionOverlayRuntimeHarness harness;
      ASSERT_TRUE(harness.initialize(0.0f, false));
      ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, projection));
      std::array<MeshVertex, 4u> surface;
      const std::array<QVec2, 4u> corners{{
          {80.0f, 140.0f}, {180.0f, 140.0f},
          {180.0f, 340.0f}, {80.0f, 340.0f}}};
      for (std::size_t index = 0u; index < surface.size(); ++index) {
        const float z = index == 0u || index == 3u ? 0.0f : far_z;
        surface[index].position = harness.overlay_audit_world_point(corners[index], z);
      }
      ASSERT_TRUE(harness.create_outline_depth_quad(surface));
      harness.set_overlay_audit_frame({});
      harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex);
      const auto background = harness.capture();
      ASSERT_FALSE(background.isNull());
      ASSERT_NE(background.pixelColor(130, 240), background.pixelColor(320, 400));

      ViewportOverlayFrame frame;
      for (int index = 0; index < 2; ++index) {
        const float y = index == 0 ? 200.0f : 290.0f;
        const float behind_surface = index == 0 ? 0.0f : -0.1f;
        frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = harness.overlay_audit_world_point({80.0f, y}, behind_surface),
            .end = harness.overlay_audit_world_point({180.0f, y}, far_z + behind_surface),
            .color = rgba8(40, 225, 255), .width_pixels = 2.5f,
            .style_role = ViewportOverlayStyleRole::ExplicitColor,
            .source = ViewportOverlaySource::ComponentSelection,
            .element = {.object_id = 1, .edge_a = index * 2, .edge_b = index * 2 + 1}});
      }
      const auto expect_visibility = [&](const QImage &image) {
        ASSERT_FALSE(image.isNull());
        // Square caps extend past the authored endpoints. Their projected depth
        // must extend by the same distance: stretching endpoint depths across
        // the wider ribbon hides the near half of this coplanar internal edge.
        for (const float projected_fraction : {0.1f, 0.25f, 0.5f, 0.7f, 0.9f}) {
          SCOPED_TRACE(projected_fraction);
          const float fraction = projection == ViewportProjection::Perspective
              ? projected_fraction * 5.0f /
                  ((1.0f - projected_fraction) * (5.0f - far_z) + projected_fraction * 5.0f)
              : projected_fraction;
          const auto &visible = frame.lines[0];
          const auto visible_probe = harness.overlay_audit_image_point(
              visible.start + (visible.end - visible.start) * fraction);
          EXPECT_GE(cyan_pixels(image, visible_probe), 3)
              << "A visible source edge must remain continuous on its own sloped surface.";
          const auto &hidden = frame.lines[1];
          const auto hidden_probe = harness.overlay_audit_image_point(
              hidden.start + (hidden.end - hidden.start) * fraction);
          EXPECT_EQ(cyan_pixels(image, hidden_probe), 0)
              << "Correcting the ribbon depth must preserve real surface occlusion.";
        }
      };
      const auto expect_retained_uploads = [&](const OverlayProxyUploadStats &before) {
        const auto after = harness.component_overlay_stats().uploads;
        EXPECT_EQ(after.geometry_creation_count, before.geometry_creation_count);
        EXPECT_EQ(after.position_upload_count, before.position_upload_count);
        EXPECT_EQ(after.index_upload_count, before.index_upload_count);
        EXPECT_EQ(after.static_upload_count, before.static_upload_count);
      };
      harness.set_overlay_audit_frame(frame);
      QImage initial;
      for (const float scale : {1.0f, 2.0f, 1.0f}) {
        SCOPED_TRACE(scale);
        harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
        const auto image = harness.capture();
        expect_visibility(image);
        if (scale == 1.0f) {
          if (initial.isNull()) initial = image;
          else EXPECT_EQ(image, initial) << "Scale-down must restore the source-wire presentation.";
        }
        const auto uploads = harness.component_overlay_stats().uploads;
        EXPECT_EQ(harness.component_overlay_stats().retained_source_wire_edges, 2u);
        harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
        EXPECT_FALSE(harness.last_submission().persistent_submitted);
        EXPECT_FALSE(harness.last_submission().dynamic_submitted);
        expect_retained_uploads(uploads);
        EXPECT_EQ(harness.capture(false), image);

        // Camera movement updates shader projection without rebuilding the
        // semantic edge population or uploading unchanged endpoint geometry.
        ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.2f, projection));
        harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
        EXPECT_FALSE(harness.last_submission().persistent_submitted);
        EXPECT_FALSE(harness.last_submission().dynamic_submitted);
        expect_retained_uploads(uploads);
        expect_visibility(harness.capture());
        ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, projection));
        harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
        expect_retained_uploads(uploads);
        EXPECT_EQ(harness.capture(), image);
        const auto output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
        if (!output.isEmpty()) {
          ASSERT_TRUE(QDir().mkpath(output));
          ASSERT_TRUE(image.save(QDir(output).filePath(
              QStringLiteral("sloped-component-source-wire-%1-%2-%3.png")
                  .arg(static_cast<int>(projection)).arg(int(5.0f - far_z)).arg(int(scale * 100)))));
        }
      }
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanDiagonalInsideCornerWireKeepsContinuousCoverage) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-diagonal-inside-corner-wire-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    for (const float phase : {0.0f, 0.25f, 0.5f, 0.75f}) {
      SCOPED_TRACE(static_cast<int>(projection));
      SCOPED_TRACE(phase);
      SelectionOverlayRuntimeHarness harness;
      ASSERT_TRUE(harness.initialize(0.0f, false));
      ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, projection));
      ViewportOverlayFrame source;
      ASSERT_TRUE(harness.create_diagonal_inside_corner_sample(source, projection, phase));
      auto reference_frame = source;
      reference_frame.lines.front().layer = ViewportOverlayLayer::ToolHandle;
      reference_frame.lines.front().source = ViewportOverlaySource::Tool;
      reference_frame.lines.front().element = {};
      harness.set_overlay_audit_frame({});
      harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex);
      const auto background = harness.capture();
      ASSERT_FALSE(background.isNull());
      ASSERT_NE(background.pixelColor(250, 170), background.pixelColor(600, 50));

      const auto expect_coverage = [&](const QImage &actual, const QImage &reference,
                                       const QImage &backdrop, float scale, const char *label) {
        ASSERT_FALSE(actual.isNull());
        ASSERT_FALSE(reference.isNull());
        ASSERT_FALSE(backdrop.isNull());
        const auto start = harness.overlay_audit_image_point(source.lines.front().start);
        const auto end = harness.overlay_audit_image_point(source.lines.front().end);
        const float length = std::hypot(end.x - start.x, end.y - start.y);
        const QVec2 direction{(end.x - start.x) / length, (end.y - start.y) / length};
        std::vector<double> expected_bins(static_cast<std::size_t>(std::ceil(length)), 0.0);
        std::vector<double> actual_bins(expected_bins.size(), 0.0);
        double expected_signal = 0.0;
        double actual_signal = 0.0;
        int expected_pixels = 0;
        int missing_pixels = 0;
        int partial_pixels = 0;
        int missing_partial_pixels = 0;
        int max_color_error = 0;
        for (int y = 0; y < reference.height(); ++y) {
          for (int x = 0; x < reference.width(); ++x) {
            const float px = x + 0.5f - start.x;
            const float py = y + 0.5f - start.y;
            const float along = px * direction.x + py * direction.y;
            const float across = -px * direction.y + py * direction.x;
            if (along < 8.0f || along > length - 8.0f || std::abs(across) > 3.0f * scale) continue;
            const auto base = backdrop.pixelColor(x, y);
            const auto expected = reference.pixelColor(x, y);
            const auto observed = actual.pixelColor(x, y);
            const int reference_delta = std::max({std::abs(expected.red() - base.red()),
                std::abs(expected.green() - base.green()), std::abs(expected.blue() - base.blue())});
            if (reference_delta < 4) continue;
            const int actual_delta = std::max({std::abs(observed.red() - base.red()),
                std::abs(observed.green() - base.green()), std::abs(observed.blue() - base.blue())});
            const auto bin = static_cast<std::size_t>(along);
            expected_bins[bin] += reference_delta;
            actual_bins[bin] += actual_delta;
            expected_signal += reference_delta;
            actual_signal += actual_delta;
            ++expected_pixels;
            const bool missing = actual_delta * 2 < reference_delta;
            missing_pixels += missing ? 1 : 0;
            // Partially blended pixels exercise the analytic feather as well
            // as the opaque core. Losing just the fringe still fails coverage.
            if (reference_delta < 100) {
              ++partial_pixels;
              missing_partial_pixels += missing ? 1 : 0;
            }
            max_color_error = std::max({max_color_error,
                std::abs(expected.red() - observed.red()), std::abs(expected.green() - observed.green()),
                std::abs(expected.blue() - observed.blue())});
          }
        }
        int gap = 0;
        int longest_gap = 0;
        for (std::size_t bin = 0u; bin < expected_bins.size(); ++bin) {
          gap = expected_bins[bin] > 4.0 && actual_bins[bin] * 2.0 < expected_bins[bin] ? gap + 1 : 0;
          longest_gap = std::max(longest_gap, gap);
        }
        const double ratio = actual_signal / std::max(1.0, expected_signal);
        std::cout << "DIAGONAL_SOURCE_WIRE projection=" << static_cast<int>(projection)
                  << " phase=" << phase << " scale=" << scale << " sample=" << label
                  << " pixels=" << expected_pixels << " missing_pixels=" << missing_pixels
                  << " partial_pixels=" << partial_pixels << " missing_partial_pixels=" << missing_partial_pixels
                  << " signal_ratio=" << ratio << " longest_gap=" << longest_gap
                  << " max_color_error=" << max_color_error << std::endl;
        EXPECT_GT(expected_pixels, 300);
        EXPECT_GT(partial_pixels, 50);
        EXPECT_EQ(missing_pixels, 0) << "The inward surfaces must not erase the diagonal stroke.";
        EXPECT_EQ(missing_partial_pixels, 0) << "The complete antialias feather must survive scene depth.";
        EXPECT_GE(ratio, 0.97);
        EXPECT_LE(ratio, 1.03);
        EXPECT_EQ(longest_gap, 0) << "Interior stroke coverage must not become a dashed line.";
        EXPECT_LE(max_color_error, 3);
      };
      const auto expect_uploads_retained = [&](const OverlayProxyUploadStats &before) {
        const auto after = harness.component_overlay_stats().uploads;
        EXPECT_EQ(after.geometry_creation_count, before.geometry_creation_count);
        EXPECT_EQ(after.position_upload_count, before.position_upload_count);
        EXPECT_EQ(after.index_upload_count, before.index_upload_count);
        EXPECT_EQ(after.static_upload_count, before.static_upload_count);
      };
      QImage initial;
      for (const float scale : {1.0f, 2.0f, 1.0f}) {
        SCOPED_TRACE(scale);
        harness.set_overlay_audit_frame(reference_frame);
        harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
        const auto reference = harness.capture(false);
        harness.set_overlay_audit_frame(source);
        harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
        const auto image = harness.capture(false);
        expect_coverage(image, reference, background, scale, "stationary");
        if (scale == 1.0f) {
          if (initial.isNull()) initial = image;
          else EXPECT_EQ(image, initial);
        }
        const auto uploads = harness.component_overlay_stats().uploads;
        harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
        EXPECT_FALSE(harness.last_submission().persistent_submitted);
        EXPECT_FALSE(harness.last_submission().dynamic_submitted);
        expect_uploads_retained(uploads);
        EXPECT_EQ(harness.capture(false), image);
        const auto output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
        if (!output.isEmpty()) {
          ASSERT_TRUE(QDir().mkpath(output));
          const auto stem = QStringLiteral("diagonal-inside-corner-%1-%2-%3")
              .arg(static_cast<int>(projection)).arg(int(phase * 100)).arg(int(scale * 100));
          ASSERT_TRUE(image.save(QDir(output).filePath(stem + QStringLiteral(".png"))));
          ASSERT_TRUE(reference.save(QDir(output).filePath(stem + QStringLiteral("-reference.png"))));
        }
      }

      const auto uploads = harness.component_overlay_stats().uploads;
      ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.2f, projection));
      harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex);
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_FALSE(harness.last_submission().dynamic_submitted);
      expect_uploads_retained(uploads);
      const auto moved = harness.capture(false);
      harness.set_overlay_audit_frame({});
      harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex);
      const auto moved_background = harness.capture(false);
      harness.set_overlay_audit_frame(reference_frame);
      harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex);
      const auto moved_reference = harness.capture(false);
      expect_coverage(moved, moved_reference, moved_background, 1.0f, "camera");
      ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, projection));

      std::array<MeshVertex, 4u> occluder;
      const std::array<QVec2, 4u> occluder_pixels{{
          {300.0f, 190.0f}, {340.0f, 190.0f}, {340.0f, 290.0f}, {300.0f, 290.0f}}};
      for (std::size_t index = 0u; index < occluder.size(); ++index) {
        occluder[index].position = harness.overlay_audit_world_point(occluder_pixels[index], 2.0f);
      }
      ASSERT_TRUE(harness.create_outline_depth_quad(occluder));
      harness.set_overlay_audit_frame({});
      harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex);
      const auto occluded_background = harness.capture(false);
      harness.set_overlay_audit_frame(source);
      harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex);
      const auto occluded = harness.capture(false);
      ASSERT_FALSE(occluded_background.isNull());
      ASSERT_FALSE(occluded.isNull());
      EXPECT_EQ(occluded.copy({305, 195, 30, 90}), occluded_background.copy({305, 195, 30, 90}))
          << "A real foreground surface must still occlude the continuous wire.";
      const auto output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
      if (!output.isEmpty()) {
        ASSERT_TRUE(occluded.save(QDir(output).filePath(
            QStringLiteral("diagonal-inside-corner-%1-%2-occluded.png")
                .arg(static_cast<int>(projection)).arg(int(phase * 100)))));
      }
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanInsideCornerSupportOnlyPreviewMatchesRebuiltGeometryAndCancel) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-inside-corner-support-preview-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const auto output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!output.isEmpty()) ASSERT_TRUE(QDir().mkpath(output));
  for (const float scale : {1.0f, 2.0f}) {
    SCOPED_TRACE(scale);
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, ViewportProjection::Perspective));
    ViewportOverlayFrame source;
    ASSERT_TRUE(harness.create_diagonal_inside_corner_sample(
        source, ViewportProjection::Perspective, 0.25f));
    const QVec3 pivot = source.lines.front().start;
    const QVec3 axis = normalize(source.lines.front().end - pivot);
    // Both endpoints lie on this axis. Rotating only the four outer vertices
    // changes both incident depth planes while leaving the cyan edge fixed.
    const auto rotation = [&](float angle) {
      const float cosine = std::cos(angle);
      const float sine = std::sin(angle);
      const auto rotate = [&](QVec3 vector) {
        return vector * cosine + cross(axis, vector) * sine +
            axis * (dot(axis, vector) * (1.0f - cosine));
      };
      return QuaderTransform3{.x_axis = rotate({1.0f, 0.0f, 0.0f}),
          .y_axis = rotate({0.0f, 1.0f, 0.0f}), .z_axis = rotate({0.0f, 0.0f, 1.0f})};
    };
    const auto add_occluder = [](SelectionOverlayRuntimeHarness &target) {
      std::array<MeshVertex, 4u> vertices;
      const std::array<QVec2, 4u> pixels{{
          {300.0f, 190.0f}, {340.0f, 190.0f}, {340.0f, 290.0f}, {300.0f, 290.0f}}};
      for (std::size_t index = 0u; index < vertices.size(); ++index) {
        vertices[index].position = target.overlay_audit_world_point(pixels[index], 2.0f);
      }
      return target.create_outline_depth_quad(vertices);
    };
    ASSERT_TRUE(add_occluder(harness));
    harness.set_overlay_audit_frame(source);
    harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
    const auto baseline = harness.capture();
    ASSERT_FALSE(baseline.isNull());
    ASSERT_TRUE(harness.begin_inside_corner_support_preview());
    OverlayProxyUploadStats previous_uploads;
    bool have_previous_uploads = false;
    for (const float angle : {0.15f, 0.35f}) {
      SCOPED_TRACE(angle);
      const auto transform = rotation(angle);
      ASSERT_TRUE(harness.update_inside_corner_support_preview(transform, pivot));
      harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
      const auto stats = harness.component_overlay_stats();
      EXPECT_GT(stats.shared_position_proxy_count, 0u);
      if (have_previous_uploads) {
        EXPECT_FALSE(harness.last_submission().persistent_submitted);
        EXPECT_EQ(stats.uploads.geometry_creation_count, previous_uploads.geometry_creation_count);
        EXPECT_EQ(stats.uploads.position_upload_count, previous_uploads.position_upload_count);
        EXPECT_EQ(stats.uploads.shared_position_upload_count, previous_uploads.shared_position_upload_count + 1u);
        EXPECT_EQ(stats.uploads.shared_position_binding_upload_count, previous_uploads.shared_position_binding_upload_count);
        EXPECT_EQ(stats.uploads.static_upload_count, previous_uploads.static_upload_count);
        EXPECT_EQ(stats.uploads.index_upload_count, previous_uploads.index_upload_count);
      }
      const auto preview = harness.capture(false);
      ASSERT_FALSE(preview.isNull());
      EXPECT_NE(preview, baseline) << "The public preview must actually move the incident surfaces.";
      SelectionOverlayRuntimeHarness rebuilt;
      ASSERT_TRUE(rebuilt.initialize(0.0f, false));
      ASSERT_TRUE(rebuilt.prepare_overlay_audit_camera(5.0f, ViewportProjection::Perspective));
      ViewportOverlayFrame effective;
      ASSERT_TRUE(rebuilt.create_diagonal_inside_corner_sample(
          effective, ViewportProjection::Perspective, 0.25f, &transform));
      ASSERT_TRUE(add_occluder(rebuilt));
      rebuilt.set_overlay_audit_frame(effective);
      rebuilt.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
      const auto reference = rebuilt.capture();
      ASSERT_FALSE(reference.isNull());
      const auto start = harness.overlay_audit_image_point(pivot);
      const auto end = harness.overlay_audit_image_point(source.lines.front().end);
      const float length = std::hypot(end.x - start.x, end.y - start.y);
      const QVec2 direction{(end.x - start.x) / length, (end.y - start.y) / length};
      int expected_pixels = 0;
      int mismatched_pixels = 0;
      int maximum_error = 0;
      for (int y = 0; y < reference.height(); ++y) {
        for (int x = 0; x < reference.width(); ++x) {
          const float px = x + 0.5f - start.x;
          const float py = y + 0.5f - start.y;
          const float along = px * direction.x + py * direction.y;
          const float across = -px * direction.y + py * direction.x;
          if (along < 8.0f || along > length - 8.0f || std::abs(across) > 3.0f * scale) continue;
          const auto expected = reference.pixelColor(x, y);
          const auto actual = preview.pixelColor(x, y);
          const int error = std::max({std::abs(expected.red() - actual.red()),
              std::abs(expected.green() - actual.green()), std::abs(expected.blue() - actual.blue())});
          expected_pixels += std::min(expected.green(), expected.blue()) > expected.red() + 4 ? 1 : 0;
          mismatched_pixels += error > 3 ? 1 : 0;
          maximum_error = std::max(maximum_error, error);
        }
      }
      std::cout << "INSIDE_CORNER_SUPPORT_PREVIEW scale=" << scale << " angle=" << angle
                << " expected_pixels=" << expected_pixels << " mismatched_pixels=" << mismatched_pixels
                << " maximum_error=" << maximum_error << std::endl;
      EXPECT_GT(expected_pixels, 300);
      EXPECT_EQ(mismatched_pixels, 0)
          << "Retained support positions must match rebuilt effective geometry across the complete stroke.";
      EXPECT_LE(maximum_error, 3);
      EXPECT_EQ(preview.copy({305, 195, 30, 90}), reference.copy({305, 195, 30, 90}))
          << "The support-only preview must preserve true foreground occlusion.";
      if (!output.isEmpty()) {
        const auto stem = QStringLiteral("inside-corner-support-preview-%1-%2")
            .arg(int(scale * 100)).arg(int(angle * 100));
        ASSERT_TRUE(preview.save(QDir(output).filePath(stem + QStringLiteral(".png"))));
        ASSERT_TRUE(reference.save(QDir(output).filePath(stem + QStringLiteral("-rebuilt.png"))));
      }
      ASSERT_TRUE(harness.sync_inside_corner_support_preview());
      harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_EQ(harness.component_overlay_stats().uploads.shared_position_upload_count,
                stats.uploads.shared_position_upload_count);
      EXPECT_EQ(harness.capture(false), preview);
      previous_uploads = stats.uploads;
      have_previous_uploads = true;
    }
    ASSERT_TRUE(harness.cancel_inside_corner_support_preview());
    harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
    EXPECT_EQ(harness.component_overlay_stats().shared_position_proxy_count, 0u);
    EXPECT_EQ(harness.capture(false), baseline)
        << "Cancel must restore the original support planes as well as the unchanged endpoints.";
  }
}

TEST(SelectionOverlayRuntimeTests, VulkanNativeReversedSourceWireKeepsContinuousCoverage) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-native-reversed-wire-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, ViewportProjection::Perspective));
  ViewportOverlayFrame authored;
  ASSERT_TRUE(harness.create_diagonal_inside_corner_sample(
      authored, ViewportProjection::Perspective, 0.25f, nullptr, true));
  ASSERT_TRUE(harness.select_inside_corner_native_wire());
  const auto native = harness.modeling().selection_overlay_render_frame();
  const auto found = std::ranges::find_if(native.lines, [](const ViewportOverlayLine &line) {
    return line.layer == ViewportOverlayLayer::SourceWire &&
        line.element.edge_a == 1 && line.element.edge_b == 2;
  });
  ASSERT_NE(found, native.lines.end());
  EXPECT_FLOAT_EQ(found->start.x, authored.lines.front().end.x);
  EXPECT_FLOAT_EQ(found->start.y, authored.lines.front().end.y);
  EXPECT_FLOAT_EQ(found->start.z, authored.lines.front().end.z);
  EXPECT_FLOAT_EQ(found->end.x, authored.lines.front().start.x);
  EXPECT_FLOAT_EQ(found->end.y, authored.lines.front().start.y);
  EXPECT_FLOAT_EQ(found->end.z, authored.lines.front().start.z);
  auto supported = *found;
  OverlayWireSurfaceSupportCache support_cache;
  support_cache.prepare(native, OverlayRenderableBuilder::frame_signatures(native));
  support_cache.apply(supported);
  std::cout << "NATIVE_REVERSED_ENDPOINTS edge_a=" << found->element.edge_a
            << " edge_b=" << found->element.edge_b
            << " support0=" << supported.surface_support[0].vertex_id
            << " support1=" << supported.surface_support[1].vertex_id << std::endl;
  auto reference_frame = native;
  const auto line_index = static_cast<std::size_t>(found - native.lines.begin());
  reference_frame.lines[line_index].layer = ViewportOverlayLayer::ToolHandle;
  reference_frame.lines[line_index].source = ViewportOverlaySource::Tool;
  reference_frame.lines[line_index].element = {};
  const auto start = harness.overlay_audit_image_point(found->start);
  const auto end = harness.overlay_audit_image_point(found->end);
  harness.set_overlay_audit_frame({});
  harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex);
  const auto backdrop = harness.capture();
  const auto output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!output.isEmpty()) ASSERT_TRUE(QDir().mkpath(output));
  for (const float scale : {1.0f, 2.0f, 1.0f}) {
    SCOPED_TRACE(scale);
    ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge, scale));
    const auto actual = harness.capture(false);
    harness.set_overlay_audit_frame(reference_frame);
    harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
    const auto reference = harness.capture(false);
    if (!output.isEmpty()) {
      const auto stem = QStringLiteral("native-reversed-source-wire-%1").arg(int(scale * 100));
      ASSERT_TRUE(actual.save(QDir(output).filePath(stem + QStringLiteral(".png"))));
      ASSERT_TRUE(reference.save(QDir(output).filePath(stem + QStringLiteral("-reference.png"))));
    }
    expect_native_wire_coverage(actual, reference, backdrop, start, end, scale, "reversed-api-frame");
    ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge, scale));
    EXPECT_EQ(harness.capture(false), actual);
    const auto uploads = harness.component_overlay_stats().uploads;
    ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge, scale));
    EXPECT_FALSE(harness.last_submission().persistent_submitted);
    EXPECT_EQ(harness.component_overlay_stats().uploads.geometry_creation_count, uploads.geometry_creation_count);
    EXPECT_EQ(harness.component_overlay_stats().uploads.position_upload_count, uploads.position_upload_count);
    EXPECT_EQ(harness.component_overlay_stats().uploads.static_upload_count, uploads.static_upload_count);
    EXPECT_EQ(harness.component_overlay_stats().uploads.index_upload_count, uploads.index_upload_count);
  }
  std::array<MeshVertex, 4u> occluder;
  const std::array<QVec2, 4u> pixels{{
      {300.0f, 190.0f}, {340.0f, 190.0f}, {340.0f, 290.0f}, {300.0f, 290.0f}}};
  for (std::size_t index = 0u; index < pixels.size(); ++index) {
    occluder[index].position = harness.overlay_audit_world_point(pixels[index], 2.0f);
  }
  ASSERT_TRUE(harness.create_outline_depth_quad(occluder));
  ASSERT_TRUE(harness.select_inside_corner_native_wire());
  harness.set_overlay_audit_frame({});
  harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex);
  const auto hidden_backdrop = harness.capture(false);
  ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Edge));
  const auto occluded = harness.capture(false);
  EXPECT_EQ(occluded.copy({305, 195, 30, 90}), hidden_backdrop.copy({305, 195, 30, 90}));
}

TEST(SelectionOverlayRuntimeTests, VulkanSavedProjectNativeSourceWireKeepsContinuousCoverage) {
  const auto source_path = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_PROJECT");
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1") || source_path.isEmpty()) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 and QUADER_SELECTION_OVERLAY_PROJECT to capture a copied project.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-saved-project-source-wire-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const std::filesystem::path source_manifest(source_path.toStdWString());
  ASSERT_TRUE(std::filesystem::is_regular_file(source_manifest));
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto copy_root = std::filesystem::path(temporary.path().toStdWString()) / source_manifest.stem();
  std::error_code error;
  ASSERT_TRUE(std::filesystem::create_directories(copy_root, error));
  ASSERT_FALSE(error) << error.message();
  // Project opening may provision assets or restore workspace state. Clone
  // every authored root entry while excluding autosaves and bridge records.
  for (const auto &entry : std::filesystem::directory_iterator(source_manifest.parent_path())) {
    if (entry.path().filename() == ".quader" || entry.path().filename() == ".git") continue;
    std::filesystem::copy(entry.path(), copy_root / entry.path().filename(),
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_symlinks, error);
    ASSERT_FALSE(error) << error.message();
  }
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false, ViewportRenderSurface::SrgbSwapchain));
  ASSERT_TRUE(harness.open_native_project_sample(copy_root / source_manifest.filename()));
  const auto native = harness.build_selection_overlay_frame();
  OverlayWireSurfaceSupportCache support_cache;
  support_cache.prepare(native, OverlayRenderableBuilder::frame_signatures(native));
  const auto same_position = [](QVec3 first, QVec3 second) {
    return first.x == second.x && first.y == second.y && first.z == second.z;
  };
  std::size_t source_edges = 0u;
  std::size_t boundary_edges = 0u;
  std::size_t reversed_edges = 0u;
  std::size_t supported_endpoints = 0u;
  std::optional<std::size_t> selected_line;
  float selected_length = 0.0f;
  for (std::size_t index = 0u; index < native.lines.size(); ++index) {
    const auto &line = native.lines[index];
    if (line.layer != ViewportOverlayLayer::SourceWire) continue;
    ++source_edges;
    std::array<std::optional<QVec3>, 2u> endpoints;
    std::unordered_set<int> incident_faces;
    for (const auto &triangle : native.triangles) {
      if (triangle.layer != ViewportOverlayLayer::SourceWireDepthStamp ||
          triangle.element.object_id != line.element.object_id) continue;
      const std::array ids{triangle.element.vertex_id, triangle.element.edge_a, triangle.element.edge_b};
      const std::array positions{triangle.a, triangle.b, triangle.c};
      bool first_found = false;
      bool second_found = false;
      for (std::size_t corner = 0u; corner < ids.size(); ++corner) {
        if (ids[corner] == line.element.edge_a) {
          endpoints[0] = positions[corner];
          first_found = true;
        }
        if (ids[corner] == line.element.edge_b) {
          endpoints[1] = positions[corner];
          second_found = true;
        }
      }
      if (first_found && second_found) incident_faces.insert(triangle.element.face_id);
    }
    boundary_edges += incident_faces.size() == 1u ? 1u : 0u;
    auto supported = line;
    support_cache.apply(supported);
    supported_endpoints += supported.surface_support[0].vertex_id >= 0 ? 1u : 0u;
    supported_endpoints += supported.surface_support[1].vertex_id >= 0 ? 1u : 0u;
    const bool reversed = endpoints[0] && endpoints[1] &&
        same_position(line.start, *endpoints[1]) && same_position(line.end, *endpoints[0]);
    reversed_edges += reversed ? 1u : 0u;
    if (!reversed || incident_faces.size() != 2u) continue;
    const auto start = harness.overlay_audit_image_point(line.start);
    const auto end = harness.overlay_audit_image_point(line.end);
    const float length = std::hypot(end.x - start.x, end.y - start.y);
    if (length > selected_length && std::abs(end.x - start.x) > 4.0f &&
        std::abs(end.y - start.y) > 4.0f && harness.native_project_line_is_visible(line)) {
      selected_line = index;
      selected_length = length;
    }
  }
  std::cout << "PROJECT_NATIVE_SOURCE_WIRE edges=" << source_edges << " endpoint_count=" << source_edges * 2u
            << " boundary_edges=" << boundary_edges << " reversed_edges=" << reversed_edges
            << " resolved_supports=" << supported_endpoints << std::endl;
  ASSERT_TRUE(selected_line.has_value()) << "The saved camera must expose a reversed shared diagonal crease.";
  const auto &crease = native.lines[*selected_line];
  std::cout << "PROJECT_NATIVE_CREASE object=" << crease.element.object_id << " edge_a=" << crease.element.edge_a
            << " edge_b=" << crease.element.edge_b << " length=" << selected_length << std::endl;
  auto reference_frame = native;
  reference_frame.lines[*selected_line].layer = ViewportOverlayLayer::ToolHandle;
  reference_frame.lines[*selected_line].source = ViewportOverlaySource::Tool;
  reference_frame.lines[*selected_line].element = {};
  ASSERT_TRUE(harness.present_native_project_sample(1.0f, false));
  const auto backdrop = harness.capture();
  const auto start = harness.overlay_audit_image_point(crease.start);
  const auto end = harness.overlay_audit_image_point(crease.end);
  const auto output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!output.isEmpty()) ASSERT_TRUE(QDir().mkpath(output));
  for (const float scale : {1.0f, 2.0f}) {
    SCOPED_TRACE(scale);
    ASSERT_TRUE(harness.present_native_project_sample(scale));
    const auto actual = harness.capture(false);
    harness.set_overlay_audit_frame(reference_frame);
    harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Vertex);
    const auto reference = harness.capture(false);
    if (!output.isEmpty()) {
      const auto stem = QStringLiteral("saved-project-native-source-wire-%1").arg(int(scale * 100));
      ASSERT_TRUE(actual.save(QDir(output).filePath(stem + QStringLiteral(".png"))));
      ASSERT_TRUE(reference.save(QDir(output).filePath(stem + QStringLiteral("-reference.png"))));
    }
    expect_native_wire_coverage(actual, reference, backdrop, start, end, scale, "saved-project");
    ASSERT_TRUE(harness.present_native_project_sample(scale));
    const auto uploads = harness.component_overlay_stats().uploads;
    ASSERT_TRUE(harness.present_native_project_sample(scale));
    EXPECT_FALSE(harness.last_submission().persistent_submitted);
    EXPECT_EQ(harness.component_overlay_stats().uploads.geometry_creation_count, uploads.geometry_creation_count);
    EXPECT_EQ(harness.component_overlay_stats().uploads.position_upload_count, uploads.position_upload_count);
    EXPECT_EQ(harness.capture(false), actual);
  }
}

TEST(SelectionOverlayRuntimeTests, VulkanHallwaySelectionDimensionsPreserveActiveFaceEdges) {
  const auto source_path = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_HALLWAY_PROJECT");
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1") || source_path.isEmpty()) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 and QUADER_SELECTION_OVERLAY_HALLWAY_PROJECT to inspect a copied hallway project.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-hallway-active-face-dimensions-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const std::filesystem::path source_manifest(source_path.toStdWString());
  ASSERT_TRUE(std::filesystem::is_regular_file(source_manifest));
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto copy_root = std::filesystem::path(temporary.path().toStdWString()) / source_manifest.stem();
  std::error_code error;
  ASSERT_TRUE(std::filesystem::create_directories(copy_root, error));
  ASSERT_FALSE(error) << error.message();
  // Opening a project can restore/provision local state. Only this disposable
  // copy is opened; the user's authored project and scenes remain read-only.
  for (const auto &entry : std::filesystem::directory_iterator(source_manifest.parent_path())) {
    if (entry.path().filename() == ".quader" || entry.path().filename() == ".git") continue;
    std::filesystem::copy(entry.path(), copy_root / entry.path().filename(),
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_symlinks, error);
    ASSERT_FALSE(error) << error.message();
  }
  const auto output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!output.isEmpty()) ASSERT_TRUE(QDir().mkpath(output));
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false, ViewportRenderSurface::SrgbSwapchain));
  ASSERT_TRUE(harness.open_native_project_sample(copy_root / source_manifest.filename()));
  // The reported hallway is one Box. These three actual authored faces form
  // bounds 57 units long and 17 units high: the reported 1824 / 544 dimensions.
  // The lower/upper left-wall faces share the end wall at z=-82.
  constexpr int kHallwayObject = 3;
  const std::vector<QuaderElementId> faces{226, 227, 240};
  struct HallwayCameraSample {
    const char *label;
    ViewportCameraPose pose;
    bool center_active_height = false;
  };
  const std::array<HallwayCameraSample, 3u> cameras{{
      {"far", {.eye = {-12.0f, 9.0f, -18.0f}, .target = {-20.0f, 10.0f, -65.0f}, .up = {0.0f, 1.0f, 0.0f}}},
      {"near", {.eye = {-12.0f, 9.0f, -58.0f}, .target = {-20.0f, 10.0f, -82.0f}, .up = {0.0f, 1.0f, 0.0f}}, true},
      {"oblique", {.eye = {-13.0f, 10.0f, -45.0f}, .target = {-20.0f, 10.0f, -65.0f}, .up = {0.0f, 1.0f, 0.0f}}}}};
  for (const QuaderElementId active_face : {226, 227}) {
    SCOPED_TRACE(active_face);
    ASSERT_TRUE(harness.select_native_project_faces(kHallwayObject, faces, active_face));
    const auto native = harness.build_selection_overlay_frame();
    float active_height_sum = 0.0f;
    std::size_t active_edge_count = 0u;
    for (const auto &line : native.lines) {
      if (line.style_role != ViewportOverlayStyleRole::ActiveSelectionWire) continue;
      active_height_sum += (line.start.y + line.end.y) * 0.5f;
      ++active_edge_count;
      std::cout << "HALLWAY_AUTHORED_ACTIVE_EDGE face=" << active_face
                << " edge=" << line.element.edge_a << ',' << line.element.edge_b
                << " start=" << line.start.x << ',' << line.start.y << ',' << line.start.z
                << " end=" << line.end.x << ',' << line.end.y << ',' << line.end.z << std::endl;
    }
    ASSERT_GT(active_edge_count, 0u);
    SelectionRuntimeFrame backdrop_frame;
    backdrop_frame.frame = native;
    std::erase_if(backdrop_frame.frame.lines, [](const ViewportOverlayLine &line) {
      return line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire;
    });
    for (const auto &sample : cameras) {
      SCOPED_TRACE(sample.label);
      auto pose = sample.pose;
      if (sample.center_active_height) {
        pose.eye.y = pose.target.y = active_height_sum / static_cast<float>(active_edge_count);
      }
      ASSERT_TRUE(harness.set_native_project_camera_pose(pose));
      std::vector<ViewportOverlayLine> visible_edges;
      for (const auto &line : native.lines) {
        if (line.style_role != ViewportOverlayStyleRole::ActiveSelectionWire) continue;
        // Clip only the measurement segment. Production still receives the
        // complete authored edge, including parts extending outside the pane.
        const auto segment = harness.native_project_visible_line_segment(line);
        if (!segment) continue;
        const auto start = harness.overlay_audit_image_point(segment->start);
        const auto end = harness.overlay_audit_image_point(segment->end);
        if (std::hypot(end.x - start.x, end.y - start.y) > 25.0f) visible_edges.push_back(*segment);
      }
      ASSERT_FALSE(visible_edges.empty()) << "The real hallway camera must expose an active face boundary.";
      for (const float scale : {1.0f, 2.0f}) {
        SCOPED_TRACE(scale);
        ASSERT_TRUE(harness.present_native_project_sample(scale, true, false, &backdrop_frame));
        const auto backdrop = harness.capture();
        ASSERT_TRUE(harness.present_native_project_sample(scale, true, false));
        const auto reference = harness.capture();
        ASSERT_TRUE(harness.present_native_project_sample(scale, true, true));
        ASSERT_TRUE(harness.last_submission().selection_dimension_bounds.has_value());
        const auto actual = harness.capture();
        ASSERT_FALSE(actual.isNull());
        ASSERT_FALSE(reference.isNull());
        EXPECT_NE(actual, reference) << "Dimension guides must actually be present in this capture.";
        if (!output.isEmpty()) {
          const auto stem = QStringLiteral("hallway-active-face-%1-%2-%3")
              .arg(active_face).arg(QString::fromLatin1(sample.label)).arg(static_cast<int>(scale * 100.0f));
          ASSERT_TRUE(actual.save(QDir(output).filePath(stem + QStringLiteral("-dimensions.png"))));
          ASSERT_TRUE(reference.save(QDir(output).filePath(stem + QStringLiteral("-reference.png"))));
          ASSERT_TRUE(backdrop.save(QDir(output).filePath(stem + QStringLiteral("-backdrop.png"))));
        }
        for (const auto &line : visible_edges) {
          const auto start = harness.overlay_audit_image_point(line.start);
          const auto end = harness.overlay_audit_image_point(line.end);
          std::cout << "HALLWAY_ACTIVE_DIMENSIONS active_face=" << active_face
                    << " camera=" << sample.label << " scale=" << scale
                    << " edge=" << line.element.edge_a << ',' << line.element.edge_b
                    << " start=" << start.x << ',' << start.y << " end=" << end.x << ',' << end.y << std::endl;
          expect_native_wire_coverage(actual, reference, backdrop, start, end, scale, "hallway-active-dimensions");
        }
        ASSERT_TRUE(harness.present_native_project_sample(scale, true, true));
        EXPECT_FALSE(harness.last_submission().persistent_submitted);
        EXPECT_FALSE(harness.last_submission().dynamic_submitted);
        EXPECT_EQ(harness.capture(), actual);
        ASSERT_TRUE(harness.present_native_project_sample(scale, true, false));
        EXPECT_EQ(harness.capture(), reference);
      }
    }
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
     VulkanWireframePreservesTopologyAndAppearanceThroughObjectAndEditWorkflows) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-wireframe-workflow-runtime-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, ViewportProjection::Perspective));
  harness.initialize_extrusion_panes(true);
  auto &editor = harness.workflow_editor();
  ASSERT_TRUE(editor.viewports().set_layout(ViewportLayoutId::TwoVertical).ok());
  ASSERT_TRUE(editor.viewports().set_shading_mode(ViewportPaneId::Perspective, QuaderShadingMode::Wireframe).ok());
  ASSERT_TRUE(editor.viewports().set_shading_mode(ViewportPaneId::Top, QuaderShadingMode::Solid).ok());
  std::array<QuaderElementId, 8u> vertex_ids{};
  std::array<QuaderElementId, 3u> face_ids{};
  const auto create_strip = [&](float scale, QVec3 origin, bool target) {
    quader_poly::Document document;
    std::array<quader_poly::ElementId, 8u> vertices{};
    for (std::size_t row = 0u; row < 2u; ++row) {
      for (std::size_t column = 0u; column < 4u; ++column) {
        const std::size_t index = row * 4u + column;
        vertices[index] = quader_poly::add_vertex(document, origin + QVec3{
            (static_cast<float>(column) - 1.5f) * scale,
            (row == 0u ? -0.75f : 0.75f) * scale, 0.0f});
        if (target) vertex_ids[index] = static_cast<QuaderElementId>(vertices[index]);
      }
    }
    for (std::size_t column = 0u; column < 3u; ++column) {
      const auto face = quader_poly::add_face(document, std::array{
          vertices[column], vertices[column + 1u], vertices[column + 5u], vertices[column + 4u]});
      if (target) face_ids[column] = static_cast<QuaderElementId>(face);
    }
    return editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = target ? "Wireframe editable strip" : "Wireframe unselected peer"});
  };
  const auto target = create_strip(1.0f, {}, true);
  ASSERT_TRUE(target.ok()) << target.error_message();
  const auto peer = create_strip(0.35f, {0.0f, 1.5f, 0.0f}, false);
  ASSERT_TRUE(peer.ok()) << peer.error_message();
  ASSERT_TRUE(editor.selection().select_object(target.value()).ok());
  const auto layer = editor.layers().create_from_selection("Wireframe authored tint", {1.0f, 0.3f, 0.1f});
  ASSERT_TRUE(layer.ok()) << layer.error_message();
  ASSERT_TRUE(editor.selection().clear().ok());
  const auto region = [&](QVec3 world, int radius = 4) {
    const auto pixel = harness.overlay_audit_image_point(world);
    return QRect(static_cast<int>(std::lround(pixel.x)) - radius,
                 static_cast<int>(std::lround(pixel.y)) - radius,
                 radius * 2 + 1, radius * 2 + 1);
  };
  const QRect interior = region({0.5f, 0.0f, 0.0f});
  const QRect peer_interior = region({0.175f, 1.5f, 0.0f});
  const QRect diagonal = region({0.0f, 0.0f, 0.0f});
  const auto bright_pixels = [](const QImage &image, QRect bounds) {
    int count = 0;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y)
      for (int x = bounds.left(); x <= bounds.right(); ++x)
        if (image.pixelColor(x, y).lightness() > 30) ++count;
    return count;
  };
  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  const auto save = [&](const QImage &image, const QString &name) {
    if (!directory.isEmpty()) {
      EXPECT_TRUE(QDir().mkpath(directory));
      EXPECT_TRUE(image.save(QDir(directory).filePath("wireframe_workflow_" + name + ".png")));
    }
  };
  ASSERT_TRUE(harness.present_workflow_pane());
  const QImage unselected = harness.capture();
  ASSERT_FALSE(unselected.isNull());
  ASSERT_GE(bright_pixels(unselected, interior), 7);
  ASSERT_GE(bright_pixels(unselected, peer_interior), 7);
  EXPECT_EQ(bright_pixels(unselected, diagonal), 0) << "Face triangulation is not authored topology.";
  save(unselected, "object_unselected");
  ASSERT_TRUE(editor.selection().select_object(target.value()).ok());
  ASSERT_TRUE(harness.present_workflow_pane());
  const QImage object_selected = harness.capture();
  EXPECT_EQ(object_selected.copy(interior), unselected.copy(interior))
      << "Object selection must retain interior authored topology and its ordinary Wireframe tint.";
  EXPECT_EQ(object_selected.copy(peer_interior), unselected.copy(peer_interior));
  save(object_selected, "object_selected");

  for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge, QuaderSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ASSERT_TRUE(enter_selection_context(editor, mode).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_TRUE(harness.present_workflow_pane());
    const QImage empty = harness.capture();
    EXPECT_EQ(empty.copy(interior), unselected.copy(interior))
        << "Empty Edit selection must preserve ordinary Wireframe, without cyan SourceWire.";
    EXPECT_EQ(empty.copy(peer_interior), unselected.copy(peer_interior));
    EXPECT_EQ(bright_pixels(empty, diagonal), 0);
    save(empty, "edit_empty_" + QString::number(static_cast<int>(mode)));
    ASSERT_TRUE(harness.present_workflow_pane(1u));
    const QImage shaded = harness.capture();
    EXPECT_GT(cyan_pixels(shaded, harness.overlay_audit_image_point({0.5f, 0.0f, 0.0f})), 0)
        << "The shaded sibling must retain its cyan editing source wire.";
    save(shaded, "shaded_edit_" + QString::number(static_cast<int>(mode)));
    QVec3 highlight;
    if (mode == QuaderSelectionMode::Vertex) {
      ASSERT_TRUE(editor.selection().select_vertex(target.value(), vertex_ids[0]).ok());
      highlight = {-1.5f, -0.75f, 0.0f};
    } else if (mode == QuaderSelectionMode::Edge) {
      ASSERT_TRUE(editor.selection().select_edge(target.value(), vertex_ids[0], vertex_ids[1]).ok());
      highlight = {-1.0f, -0.75f, 0.0f};
    } else {
      ASSERT_TRUE(editor.selection().select_face(target.value(), face_ids[0]).ok());
      highlight = {-1.0f, -0.75f, 0.0f};
    }
    ASSERT_TRUE(harness.present_workflow_pane());
    const QImage selected = harness.capture();
    EXPECT_EQ(selected.copy(interior), unselected.copy(interior));
    EXPECT_EQ(selected.copy(peer_interior), unselected.copy(peer_interior));
    EXPECT_NE(selected.copy(region(highlight, 6)), empty.copy(region(highlight, 6)))
        << "Component selection must still highlight the selected vertex or edge.";
    EXPECT_EQ(bright_pixels(selected, diagonal), 0);
    save(selected, "edit_selected_" + QString::number(static_cast<int>(mode)));
    const auto resources = harness.render_diagnostics();
    ASSERT_TRUE(harness.present_workflow_pane());
    EXPECT_EQ(harness.capture(), selected);
    EXPECT_FALSE(harness.last_submission().persistent_submitted);
    EXPECT_EQ(harness.render_diagnostics().gpu_geometry_upload_count, resources.gpu_geometry_upload_count);
    ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Object).ok());
    ASSERT_TRUE(harness.present_workflow_pane());
    EXPECT_EQ(harness.capture().copy(interior), unselected.copy(interior));
  }

  ASSERT_TRUE(enter_selection_context(editor, QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  // Move one side of an unselected authored edge through the real transform preview owner.
  ASSERT_TRUE(editor.selection().select_vertices(target.value(), {vertex_ids[2], vertex_ids[6]}).ok());
  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(harness.present_workflow_pane());
  EXPECT_EQ(harness.capture().copy(interior), unselected.copy(interior))
      << "Beginning a preview must retain the authored layer tint.";
  constexpr int kPreviewOffsetPixels = 24;
  const QVec2 original_pixel = harness.overlay_audit_image_point({0.5f, 0.0f, 0.0f});
  const QVec3 moved_world = harness.overlay_audit_world_point(
      {original_pixel.x + kPreviewOffsetPixels, original_pixel.y}, 0.0f);
  const QuaderTransform3 preview_transform{.origin = {moved_world.x - 0.5f, 0.0f, 0.0f}};
  ASSERT_TRUE(editor.modeling().update_transform_matrix(preview_transform, {}).ok());
  ASSERT_TRUE(harness.present_workflow_pane());
  const QImage preview = harness.capture();
  EXPECT_EQ(bright_pixels(preview, interior), 0) << "Preview must retire the committed interior edge.";
  const QRect moved_interior = region({moved_world.x, 0.0f, 0.0f});
  ASSERT_EQ(moved_interior, interior.translated(kPreviewOffsetPixels, 0));
  EXPECT_GE(bright_pixels(preview, moved_interior), 7);
  EXPECT_EQ(cyan_pixels(preview, harness.overlay_audit_image_point({moved_world.x, 0.0f, 0.0f})), 0)
      << "Live effective topology must keep ordinary Wireframe appearance.";
  const auto expect_authored_wire_profile = [&](const QImage &image) {
    int maximum_difference = 0;
    for (int y = interior.top(); y <= interior.bottom(); ++y) {
      for (int x = interior.left(); x <= interior.right(); ++x) {
        const QColor expected = unselected.pixelColor(x, y);
        const QColor actual = image.pixelColor(x + kPreviewOffsetPixels, y);
        maximum_difference = std::max({maximum_difference,
            std::abs(actual.red() - expected.red()),
            std::abs(actual.green() - expected.green()),
            std::abs(actual.blue() - expected.blue())});
      }
    }
    // Whole-pixel translation preserves AA phase; allow only byte-rounding noise.
    EXPECT_LE(maximum_difference, 2)
        << "Effective preview wires must retain the ordinary authored layer tint and coverage.";
  };
  expect_authored_wire_profile(preview);
  EXPECT_EQ(preview.copy(peer_interior), unselected.copy(peer_interior));
  save(preview, "vertex_preview");
  ASSERT_TRUE(editor.modeling().cancel_transform_preview().ok());
  ASSERT_TRUE(harness.present_workflow_pane());
  EXPECT_EQ(harness.capture().copy(interior), unselected.copy(interior))
      << "Cancel must restore the committed edge with its authored layer tint.";
  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor.modeling().update_transform_matrix(preview_transform, {}).ok());
  ASSERT_TRUE(harness.present_workflow_pane());
  expect_authored_wire_profile(harness.capture());
  ASSERT_TRUE(editor.modeling().commit_transform_preview("Wireframe tint preview").ok());
  ASSERT_TRUE(harness.present_workflow_pane());
  const QImage committed = harness.capture();
  expect_authored_wire_profile(committed);
  EXPECT_EQ(bright_pixels(committed, interior), 0);
  save(committed, "vertex_preview_committed");
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(harness.present_workflow_pane());
  EXPECT_EQ(harness.capture().copy(interior), unselected.copy(interior));
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(harness.present_workflow_pane());
  EXPECT_EQ(harness.capture(), unselected);
}

TEST(SelectionOverlayRuntimeTests,
     VulkanShadedWireTogglesPreserveOcclusionAuthoredEdgesAndEditingWire) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-shaded-wire-runtime-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.prepare_overlay_audit_camera(3.0f, ViewportProjection::Perspective));

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
  const auto target = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
      {.name = "Shaded wire authored internal edge"});
  ASSERT_TRUE(target.ok()) << target.error_message();
  const auto occluder = editor.modeling().create_box(
      {.min = {-0.25f, 0.15f, 0.2f}, .max = {0.25f, 0.65f, 0.4f}},
      "Shaded wire opaque foreground");
  ASSERT_TRUE(occluder.ok()) << occluder.error_message();
  const auto material = editor.assets().add_material(QuaderMaterialAsset{
      .name = "Shaded wire neutral gray", .base_color = {0.18f, 0.18f, 0.18f},
      .roughness = 1.0f});
  ASSERT_TRUE(material.ok());
  for (const EntityId entity : {target.value().entity, occluder.value().entity}) {
    ASSERT_TRUE(editor.assets().assign_material_to_entity(entity, material.value().id).ok());
  }
  const auto target_entity = editor.query().entity(target.value().entity);
  const auto occluder_entity = editor.query().entity(occluder.value().entity);
  ASSERT_TRUE(target_entity.has_value());
  ASSERT_TRUE(occluder_entity.has_value());
  ASSERT_TRUE(editor.scene().set_mesh_component(occluder.value().entity,
      {.mesh = occluder_entity->mesh, .wireframe_on_shaded = false}).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  harness.sync_render_scene(session);

  ViewportSettings appearance = ViewportSettings::defaults();
  const auto present_wires = [&](bool master_enabled, bool ordinary_wireframe = false) {
    const auto snapshot = build_render_overlay_scene_wireframe_snapshot(
        session.scene(), session.assets(),
        ordinary_wireframe ? viewport_grid_layer_mask(0u) : 0u,
        ViewportOverlayStyleResolver::persistent_style_snapshot(appearance),
        {}, nullptr, false, {}, false,
        master_enabled ? viewport_grid_layer_mask(0u) : 0u);
    harness.sync_scene_wireframe_snapshot(snapshot);
  };
  const auto probe_region = [&](QVec3 world) {
    const QVec2 pixel = harness.overlay_audit_image_point(world);
    return QRect(static_cast<int>(std::lround(pixel.x)) - 3,
                 static_cast<int>(std::lround(pixel.y)) - 8, 7, 17);
  };
  const auto changed_pixels = [](const QImage &image, const QImage &baseline,
                                 QRect region) {
    int count = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
      for (int x = region.left(); x <= region.right(); ++x) {
        const QColor actual = image.pixelColor(x, y);
        const QColor original = baseline.pixelColor(x, y);
        if (std::max({std::abs(actual.red() - original.red()),
                      std::abs(actual.green() - original.green()),
                      std::abs(actual.blue() - original.blue())}) > 3) {
          ++count;
        }
      }
    }
    return count;
  };
  const QRect visible_region = probe_region({0.0f, -0.4f, 0.0f});
  const QRect occluded_region = probe_region({0.0f, 0.4f, 0.0f});
  const QRect triangulation_region = probe_region({0.5f, 0.0f, 0.0f});
  const auto colored_pixels = [](const QImage &image, QRect region, bool blue) {
    int count = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
      for (int x = region.left(); x <= region.right(); ++x) {
        const QColor pixel = image.pixelColor(x, y);
        const int channel = blue ? pixel.blue() : pixel.red();
        const int other = blue ? pixel.red() : pixel.blue();
        if (channel > 100 && channel > other + 45 &&
            channel > pixel.green() + 45) ++count;
      }
    }
    return count;
  };
  present_wires(false);
  const QImage master_off = harness.capture();
  ASSERT_FALSE(master_off.isNull());
  present_wires(true);
  const QImage master_on = harness.capture();
  ASSERT_FALSE(master_on.isNull());
  EXPECT_GE(changed_pixels(master_on, master_off, visible_region), 12)
      << "Enabling shaded wire must draw the authored internal edge on its opaque surface.";
  EXPECT_EQ(changed_pixels(master_on, master_off, occluded_region), 0)
      << "The foreground surface must hide the rear topology wire.";
  EXPECT_EQ(changed_pixels(master_on, master_off, triangulation_region), 0)
      << "Compiled triangulation diagonals are not authored topology edges.";
  int black_pixels = 0;
  for (int y = visible_region.top(); y <= visible_region.bottom(); ++y) {
    for (int x = visible_region.left(); x <= visible_region.right(); ++x) {
      const QColor pixel = master_on.pixelColor(x, y);
      const QColor baseline = master_off.pixelColor(x, y);
      if (pixel.lightness() < 80 && pixel.lightness() + 15 < baseline.lightness() &&
          std::abs(pixel.red() - pixel.green()) < 4 &&
          std::abs(pixel.red() - pixel.blue()) < 4) ++black_pixels;
    }
  }
  EXPECT_GE(black_pixels, 12) << "The default shaded wire must render black over the gray surface.";
  const auto retained_resources = harness.render_diagnostics();
  present_wires(true);
  EXPECT_EQ(harness.capture(), master_on);
  EXPECT_EQ(harness.render_diagnostics().gpu_geometry_upload_count,
            retained_resources.gpu_geometry_upload_count);
  present_wires(false);
  EXPECT_EQ(harness.capture(), master_off)
      << "Disabling the master toggle must retire the currently visible wire.";
  present_wires(true);
  EXPECT_EQ(harness.capture(), master_on);

  ASSERT_TRUE(editor.scene().set_mesh_component(target.value().entity,
      {.mesh = target_entity->mesh, .wireframe_on_shaded = false}).ok());
  present_wires(true);
  const QImage mesh_off = harness.capture();
  EXPECT_EQ(mesh_off, master_off) << "A per-mesh opt-out must remove its shaded wire.";
  ASSERT_TRUE(editor.scene().set_mesh_component(target.value().entity,
      {.mesh = target_entity->mesh, .wireframe_on_shaded = true}).ok());
  present_wires(false);
  EXPECT_EQ(harness.capture(), master_off)
      << "The master toggle must remove retained shaded wires.";

  present_wires(false, true);
  const QImage ordinary_wireframe = harness.capture();
  EXPECT_GE(changed_pixels(ordinary_wireframe, master_off, visible_region), 12);
  EXPECT_GE(changed_pixels(ordinary_wireframe, master_off, occluded_region), 12)
      << "Ordinary Wireframe keeps its existing X-ray coverage.";
  ASSERT_TRUE(editor.scene().set_mesh_component(target.value().entity,
      {.mesh = target_entity->mesh, .wireframe_on_shaded = false}).ok());
  present_wires(false, true);
  EXPECT_EQ(harness.capture(), ordinary_wireframe)
      << "The per-mesh shaded-wire toggle must not change ordinary Wireframe coverage.";
  ASSERT_TRUE(editor.scene().set_mesh_component(target.value().entity,
      {.mesh = target_entity->mesh, .wireframe_on_shaded = true}).ok());

  ViewportOverlayFrame editing_frame;
  editing_frame.lines.push_back({
      .layer = ViewportOverlayLayer::SourceWire,
      .start = {-0.5f, -0.65f, 0.001f}, .end = {-0.5f, 0.65f, 0.001f},
      .color = kOverlaySourceWireColor, .width_pixels = 3.0f,
      .source = ViewportOverlaySource::ComponentSelection,
      .element = {.object_id = target.value().modeling_object_id}});
  editing_frame.lines.push_back({
      .layer = ViewportOverlayLayer::SelectedEdge,
      .start = {-0.75f, -0.65f, 0.001f}, .end = {-0.75f, 0.65f, 0.001f},
      .color = kOverlaySelectionWireColor, .width_pixels = 3.0f,
      .source = ViewportOverlaySource::ComponentSelection,
      .element = {.object_id = target.value().modeling_object_id}});
  harness.set_overlay_audit_frame(std::move(editing_frame));
  harness.present_overlay_audit_frame(1.0f, ViewportOverlaySelectionContext::Vertex);
  present_wires(false);
  const QImage editing_master_off = harness.capture();
  const QVec2 editing_pixel = harness.overlay_audit_image_point({-0.5f, 0.0f, 0.001f});
  EXPECT_GE(cyan_pixels(editing_master_off, editing_pixel), 3);
  present_wires(true);
  const QImage editing_master_on = harness.capture();
  EXPECT_EQ(editing_master_on.copy(probe_region({-0.5f, 0.0f, 0.001f})),
            editing_master_off.copy(probe_region({-0.5f, 0.0f, 0.001f})))
      << "Independent component editing wire must survive shaded-wire toggles.";
  ASSERT_TRUE(editor.scene().set_mesh_component(target.value().entity,
      {.mesh = target_entity->mesh, .wireframe_on_shaded = false}).ok());
  present_wires(true);
  EXPECT_EQ(harness.capture(), editing_master_off);

  ASSERT_TRUE(editor.scene().set_mesh_component(target.value().entity,
      {.mesh = target_entity->mesh, .wireframe_on_shaded = true}).ok());
  present_wires(false, true);
  const QImage ordinary_before_colors = harness.capture();
  ASSERT_TRUE(appearance.set_color(ViewportColorSettingId::WireframeOnShaded,
      {1.0f, 0.0f, 0.0f, 1.0f}));
  present_wires(true);
  const QImage global_red = harness.capture();
  EXPECT_GE(colored_pixels(global_red, visible_region, false), 12);
  EXPECT_EQ(changed_pixels(global_red, editing_master_off, occluded_region), 0);
  ASSERT_TRUE(editor.scene().set_mesh_component(target.value().entity,
      {.mesh = target_entity->mesh, .wireframe_on_shaded = true,
       .wireframe_on_shaded_color = QVec3{0.0f, 0.0f, 1.0f}}).ok());
  present_wires(true);
  const QImage custom_blue = harness.capture();
  EXPECT_GE(colored_pixels(custom_blue, visible_region, true), 12);
  EXPECT_EQ(changed_pixels(custom_blue, editing_master_off, occluded_region), 0);
  for (const QImage &image : {global_red, custom_blue}) {
    for (const float x : {-0.5f, -0.75f}) {
      EXPECT_EQ(image.copy(probe_region({x, 0.0f, 0.001f})),
                editing_master_off.copy(probe_region({x, 0.0f, 0.001f})))
          << "Shaded-wire colors must not recolor cyan editing or selected edges.";
    }
  }
  present_wires(false, true);
  EXPECT_EQ(harness.capture(), ordinary_before_colors)
      << "Global and per-mesh shaded-wire colors must not recolor ordinary Wireframe.";
  ASSERT_TRUE(appearance.set_color(ViewportColorSettingId::WireframeOnShaded,
      {1.0f, 0.0f, 0.0f, 0.0f}));
  present_wires(true);
  EXPECT_EQ(harness.capture(), editing_master_off)
      << "A custom mesh RGB color must inherit the global wire alpha.";
  ASSERT_TRUE(appearance.set_color(ViewportColorSettingId::WireframeOnShaded,
      {1.0f, 0.0f, 0.0f, 1.0f}));
  ASSERT_TRUE(editor.scene().set_mesh_component(target.value().entity,
      {.mesh = target_entity->mesh, .wireframe_on_shaded = true,
       .wireframe_on_shaded_color = std::nullopt}).ok());
  present_wires(true);
  EXPECT_EQ(harness.capture(), global_red)
      << "Resetting a mesh color must restore the current global color.";

  const auto shared_mesh_entity = editor.scene().create_empty();
  ASSERT_TRUE(shared_mesh_entity.ok()) << shared_mesh_entity.error_message();
  // Seed a second render placement of the same asset before editing its color.
  ASSERT_TRUE(session.scene().set_mesh(shared_mesh_entity.value().id,
      {.mesh = target_entity->mesh}));
  const auto shared_mesh_color = editor.scene().set_mesh_component(shared_mesh_entity.value().id,
      {.mesh = target_entity->mesh, .wireframe_on_shaded = true,
       .wireframe_on_shaded_color = QVec3{0.0f, 0.0f, 1.0f}});
  ASSERT_TRUE(shared_mesh_color.ok()) << shared_mesh_color.error_message();
  ASSERT_TRUE(editor.scene().set_transform(shared_mesh_entity.value().id,
      {.location = {1.3f, 0.0f, 0.0f}, .scale = {0.25f, 0.25f, 0.25f}}).ok());
  ASSERT_TRUE(editor.assets().assign_material_to_entity(
      shared_mesh_entity.value().id, material.value().id).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  harness.sync_render_scene(session);
  present_wires(true);
  const QImage shared_geometry_colors = harness.capture();
  EXPECT_GE(colored_pixels(shared_geometry_colors, visible_region, false), 12);
  EXPECT_GE(colored_pixels(shared_geometry_colors,
      probe_region({1.3f, 0.0f, 0.0f}), true), 12)
      << "Two placements of one mesh asset must retain distinct shaded-wire colors.";
  const auto color_resources = harness.render_diagnostics();
  present_wires(true);
  EXPECT_EQ(harness.capture(), shared_geometry_colors);
  EXPECT_EQ(harness.render_diagnostics().gpu_geometry_upload_count,
            color_resources.gpu_geometry_upload_count);
  EXPECT_EQ(harness.render_diagnostics().gpu_geometry_reuse_count,
            color_resources.gpu_geometry_reuse_count);

  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(directory));
    for (const auto &[image, name] : std::array{
             std::pair{master_off, "shaded_wire_master_off"},
             std::pair{master_on, "shaded_wire_master_on"},
             std::pair{mesh_off, "shaded_wire_mesh_off"},
             std::pair{ordinary_wireframe, "shaded_wire_ordinary_xray"},
             std::pair{editing_master_on, "shaded_wire_editing_wire"},
             std::pair{global_red, "shaded_wire_global_red"},
             std::pair{custom_blue, "shaded_wire_custom_blue"},
             std::pair{shared_geometry_colors, "shaded_wire_shared_geometry_colors"}}) {
      ASSERT_TRUE(image.save(QDir(directory).filePath(
          QString::fromLatin1(name) + QStringLiteral(".png"))));
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanShadedWireRemainsContinuousOnObliqueSurfaces) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-shaded-wire-continuity-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(directory));

  for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    for (const float depth_scale : {1.0f, 6.0f}) {
      for (const float phase : {0.0f, 0.25f, 0.5f, 0.75f}) {
        SCOPED_TRACE(static_cast<int>(projection));
        SCOPED_TRACE(depth_scale);
        SCOPED_TRACE(phase);
        SceneSession session;
        QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
        SelectionOverlayRuntimeHarness harness;
        ASSERT_TRUE(harness.initialize(0.0f, false));
        ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, projection));
        const auto material = editor.assets().add_material(QuaderMaterialAsset{
            .name = "Oblique wire neutral gray", .base_color = {0.18f, 0.18f, 0.18f},
            .roughness = 1.0f});
        ASSERT_TRUE(material.ok());
        const std::array edges{
            std::array<QVec2, 2>{{{190.0f + phase, 80.0f}, {197.0f + phase, 400.0f}}},
            std::array<QVec2, 2>{{{390.0f + phase, 80.0f}, {550.0f + phase, 400.0f}}}};
        for (std::size_t panel = 0u; panel < edges.size(); ++panel) {
          const float left = (panel == 0u ? 125.0f : 335.0f) + phase;
          const float right = (panel == 0u ? 265.0f : 605.0f) + phase;
          const float center = (panel == 0u ? 190.0f : 450.0f) + phase;
          const std::array<QVec2, 6> pixels{{
              {left, 80.0f}, edges[panel][0], {right, 80.0f},
              {right, 400.0f}, edges[panel][1], {left, 400.0f}}};
          quader_poly::Document document;
          std::array<quader_poly::ElementId, 6> vertices;
          for (std::size_t index = 0u; index < pixels.size(); ++index) {
            const QVec2 pixel = pixels[index];
            const float along = (pixel.y - 80.0f) / 320.0f;
            // Affine reciprocal depth makes all perspective samples coplanar;
            // affine depth does the same in an orthographic projection. The
            // cross-edge gradient makes a centerline-only ribbon lose pixels.
            const float depth = projection == ViewportProjection::Perspective
                ? 1.0f / ((1.0f - along) / (5.0f * depth_scale) +
                          along / (25.0f * depth_scale) +
                          (pixel.x - center) * (panel == 0u ? 0.00012f : 0.00006f) /
                              depth_scale)
                : depth_scale * (5.0f + 20.0f * along - 0.03f * (pixel.x - center));
            vertices[index] = quader_poly::add_vertex(document,
                harness.overlay_audit_world_point(pixel, 5.0f - depth));
          }
          (void)quader_poly::add_face(document,
              std::array{vertices[0], vertices[1], vertices[4], vertices[5]});
          (void)quader_poly::add_face(document,
              std::array{vertices[1], vertices[2], vertices[3], vertices[4]});
          const auto mesh = editor.modeling().create_mesh(
              QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
              {.name = "Oblique shaded wire continuity"});
          ASSERT_TRUE(mesh.ok()) << mesh.error_message();
          ASSERT_TRUE(editor.assets().assign_material_to_entity(
              mesh.value().entity, material.value().id).ok());
        }
        ASSERT_TRUE(editor.selection().clear().ok());
        harness.sync_render_scene(session);
        ViewportSettings appearance = ViewportSettings::defaults();
        ASSERT_TRUE(appearance.set_color(ViewportColorSettingId::WireframeOnShaded,
            {1.0f, 1.0f, 1.0f, 1.0f}));
        const auto present_wires = [&](bool enabled) {
          harness.sync_scene_wireframe_snapshot(build_render_overlay_scene_wireframe_snapshot(
              session.scene(), session.assets(), 0u,
              ViewportOverlayStyleResolver::persistent_style_snapshot(appearance),
              {}, nullptr, false, {}, false,
              enabled ? viewport_grid_layer_mask(0u) : 0u));
        };
        const auto expect_continuity = [&](const QImage &image, const QImage &baseline,
                                            bool occluded) {
          for (std::size_t edge = 0u; edge < edges.size(); ++edge) {
            int missing_rows = 0;
            int tested_rows = 0;
            for (int y = 88; y <= 392; ++y) {
              if (occluded && y >= 195 && y <= 285) continue;
              const float fraction = (static_cast<float>(y) + 0.5f - edges[edge][0].y) /
                  (edges[edge][1].y - edges[edge][0].y);
              const float center = edges[edge][0].x +
                  (edges[edge][1].x - edges[edge][0].x) * fraction;
              int strongest = 0;
              for (int x = static_cast<int>(std::floor(center)) - 2;
                   x <= static_cast<int>(std::floor(center)) + 2; ++x) {
                const QColor pixel = image.pixelColor(x, y);
                const QColor base = baseline.pixelColor(x, y);
                strongest = std::max(strongest, std::min({pixel.red() - base.red(),
                    pixel.green() - base.green(), pixel.blue() - base.blue()}));
              }
              // White wire on this neutral gray fixture needs a visible core, not only a faint fringe.
              if (strongest <= 60) ++missing_rows;
              ++tested_rows;
            }
            EXPECT_GT(tested_rows, 200);
            EXPECT_EQ(missing_rows, 0)
                << "Shaded wire must cover every interior scanline; edge=" << edge
                << ", foreground=" << occluded;
          }
        };
        present_wires(false);
        const QImage baseline = harness.capture();
        ASSERT_FALSE(baseline.isNull());
        EXPECT_GT(baseline.pixelColor(193, 240).lightness(), 20);
        EXPECT_GT(baseline.pixelColor(470, 240).lightness(), 20);
        present_wires(true);
        const QImage visible = harness.capture();
        ASSERT_FALSE(visible.isNull());
        expect_continuity(visible, baseline, false);
        EXPECT_EQ(visible.copy(QRect(153, 225, 13, 30)),
                  baseline.copy(QRect(153, 225, 13, 30)))
            << "The quad's triangulation diagonal must remain invisible.";
        const auto retained_resources = harness.render_diagnostics();
        present_wires(true);
        EXPECT_EQ(harness.capture(), visible);
        EXPECT_EQ(harness.render_diagnostics().gpu_geometry_upload_count,
                  retained_resources.gpu_geometry_upload_count);

        quader_poly::Document occluder_document;
        std::array<quader_poly::ElementId, 4> occluder_vertices;
        const std::array<QVec2, 4> occluder_pixels{{
            {70.0f, 205.0f}, {615.0f, 205.0f}, {615.0f, 275.0f}, {70.0f, 275.0f}}};
        for (std::size_t index = 0u; index < occluder_vertices.size(); ++index) {
          occluder_vertices[index] = quader_poly::add_vertex(occluder_document,
              harness.overlay_audit_world_point(occluder_pixels[index], 4.9f));
        }
        (void)quader_poly::add_face(occluder_document, occluder_vertices);
        const auto occluder = editor.modeling().create_mesh(
            QuaderPolygonDocumentInternalAccess::from_backend(std::move(occluder_document)),
            {.name = "Oblique wire foreground occluder"});
        ASSERT_TRUE(occluder.ok()) << occluder.error_message();
        ASSERT_TRUE(editor.assets().assign_material_to_entity(
            occluder.value().entity, material.value().id).ok());
        ASSERT_TRUE(editor.scene().set_mesh_component(occluder.value().entity,
            {.mesh = occluder.value().mesh, .wireframe_on_shaded = false}).ok());
        ASSERT_TRUE(editor.selection().clear().ok());
        harness.sync_render_scene(session);
        present_wires(false);
        const QImage occluded_baseline = harness.capture();
        ASSERT_FALSE(occluded_baseline.isNull());
        present_wires(true);
        const QImage occluded = harness.capture();
        ASSERT_FALSE(occluded.isNull());
        expect_continuity(occluded, occluded_baseline, true);
        EXPECT_EQ(occluded.copy(QRect(80, 215, 525, 50)),
                  occluded_baseline.copy(QRect(80, 215, 525, 50)))
            << "A real foreground face must hide every interior shaded-wire fragment.";
        if (!directory.isEmpty()) {
          const QString prefix = QStringLiteral("shaded_wire_continuity_%1_%2_%3")
              .arg(static_cast<int>(projection)).arg(static_cast<int>(depth_scale))
              .arg(static_cast<int>(phase * 100.0f));
          for (const auto &[image, suffix] : std::array{
                   std::pair{baseline, "_baseline"}, std::pair{visible, "_visible"},
                   std::pair{occluded_baseline, "_occluded_baseline"},
                   std::pair{occluded, "_occluded"}}) {
            ASSERT_TRUE(image.save(QDir(directory).filePath(
                prefix + QString::fromLatin1(suffix) + QStringLiteral(".png"))));
          }
        }
      }
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanGrazingShadedWireCannotCrossForegroundDepth) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-shaded-wire-grazing-occlusion-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(directory));
  for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    SCOPED_TRACE(static_cast<int>(projection));
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, projection));
    const auto material = editor.assets().add_material(QuaderMaterialAsset{
        .name = "Grazing wire neutral gray", .base_color = {0.18f, 0.18f, 0.18f},
        .roughness = 1.0f});
    ASSERT_TRUE(material.ok());
    const auto create_quad = [&](const std::array<QVec2, 4> &pixels,
                                  const std::array<float, 4> &depths,
                                  const char *name) {
      quader_poly::Document document;
      std::array<quader_poly::ElementId, 4> vertices;
      for (std::size_t index = 0u; index < vertices.size(); ++index) {
        vertices[index] = quader_poly::add_vertex(document,
            harness.overlay_audit_world_point(pixels[index], 5.0f - depths[index]));
      }
      (void)quader_poly::add_face(document, vertices);
      return editor.modeling().create_mesh(
          QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
          {.name = name});
    };
    // The surface is only 0.032 pixels wide. Extending its depth plane toward
    // pixel center 320.5 can put a ribbon in front of the depth-10 occluder,
    // even though every authored point is at depth 30 or farther.
    const auto target = create_quad(
        {{{320.625f, 100.0f}, {320.657f, 100.0f},
          {320.657f, 380.0f}, {320.625f, 380.0f}}},
        {{30.0f, 90.0f, 90.0f, 30.0f}}, "Grazing shaded wire");
    ASSERT_TRUE(target.ok()) << target.error_message();
    ASSERT_TRUE(editor.assets().assign_material_to_entity(
        target.value().entity, material.value().id).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    harness.sync_render_scene(session);
    ViewportSettings appearance = ViewportSettings::defaults();
    ASSERT_TRUE(appearance.set_color(ViewportColorSettingId::WireframeOnShaded,
        {1.0f, 1.0f, 1.0f, 1.0f}));
    const auto present_wires = [&](bool enabled) {
      harness.sync_scene_wireframe_snapshot(build_render_overlay_scene_wireframe_snapshot(
          session.scene(), session.assets(), 0u,
          ViewportOverlayStyleResolver::persistent_style_snapshot(appearance),
          {}, nullptr, false, {}, false,
          enabled ? viewport_grid_layer_mask(0u) : 0u));
    };
    present_wires(false);
    const QImage unoccluded_baseline = harness.capture();
    ASSERT_FALSE(unoccluded_baseline.isNull());
    present_wires(true);
    const QImage unoccluded = harness.capture();
    ASSERT_FALSE(unoccluded.isNull());
    int visible_wire_pixels = 0;
    for (int y = 108; y <= 372; ++y) {
      for (int x = 318; x <= 323; ++x) {
        if (unoccluded.pixelColor(x, y).lightness() >
            unoccluded_baseline.pixelColor(x, y).lightness() + 12) ++visible_wire_pixels;
      }
    }
    EXPECT_GT(visible_wire_pixels, 100) << "The grazing fixture must draw an unoccluded wire.";
    const auto occluder = create_quad(
        {{{300.0f, 80.0f}, {340.0f, 80.0f}, {340.0f, 400.0f}, {300.0f, 400.0f}}},
        {{10.0f, 10.0f, 10.0f, 10.0f}}, "Grazing wire foreground occluder");
    ASSERT_TRUE(occluder.ok()) << occluder.error_message();
    ASSERT_TRUE(editor.assets().assign_material_to_entity(
        occluder.value().entity, material.value().id).ok());
    ASSERT_TRUE(editor.scene().set_mesh_component(occluder.value().entity,
        {.mesh = occluder.value().mesh, .wireframe_on_shaded = false}).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    harness.sync_render_scene(session);
    present_wires(false);
    const QImage occluded_baseline = harness.capture();
    ASSERT_FALSE(occluded_baseline.isNull());
    EXPECT_GT(occluded_baseline.pixelColor(320, 240).lightness(), 20);
    present_wires(true);
    const QImage occluded = harness.capture();
    ASSERT_FALSE(occluded.isNull());
    EXPECT_EQ(occluded, occluded_baseline)
        << "A grazing support plane must not pull any hidden ribbon in front of a real occluder.";
    if (!directory.isEmpty()) {
      const QString prefix = QStringLiteral("shaded_wire_grazing_%1")
          .arg(static_cast<int>(projection));
      for (const auto &[image, suffix] : std::array{
               std::pair{unoccluded_baseline, "_baseline"},
               std::pair{unoccluded, "_visible"},
               std::pair{occluded_baseline, "_occluded_baseline"},
               std::pair{occluded, "_occluded"}}) {
        ASSERT_TRUE(image.save(QDir(directory).filePath(
            prefix + QString::fromLatin1(suffix) + QStringLiteral(".png"))));
      }
    }
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

int red_pixels(const QImage &image, QVec2 center) {
  int count = 0;
  for (int y = int(center.y) - 3; y <= int(center.y) + 3; ++y) {
    for (int x = int(center.x) - 3; x <= int(center.x) + 3; ++x) {
      if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) continue;
      const QColor pixel = image.pixelColor(x, y);
      if (pixel.red() > 150 && pixel.red() > pixel.green() + 60 &&
          pixel.red() > pixel.blue() + 60) ++count;
    }
  }
  return count;
}

int pink_pixels(const QImage &image, QVec2 center) {
  int count = 0;
  for (int y = int(center.y) - 3; y <= int(center.y) + 3; ++y) {
    for (int x = int(center.x) - 3; x <= int(center.x) + 3; ++x) {
      if (!image.rect().contains(x, y)) continue;
      const QColor pixel = image.pixelColor(x, y);
      if (pixel.red() > 130 && pixel.green() > 30 &&
          pixel.blue() > pixel.green() + 20 &&
          pixel.red() > pixel.blue() + 20) ++count;
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
    const auto selected_pixels = [&](const QImage &image, QVec2 pixel) {
      return context == ViewportOverlaySelectionContext::Face
          ? yellow_pixels(image, pixel) : pink_pixels(image, pixel);
    };
    const auto expect_current = [&](const QImage &image, float delta) {
      EXPECT_GE(selected_pixels(image, selected_pixel(delta)), 3)
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
      EXPECT_EQ(selected_pixels(image, selected_pixel(previous_delta)), 0);
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
    EXPECT_EQ(selected_pixels(restored, selected_pixel(previous_delta)), 0);
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
    EXPECT_EQ(selected_pixels(harness.capture(), selected_pixel(1.2f)), 0);
    ASSERT_TRUE(modeling.begin_transform_preview(false).ok);
    ASSERT_TRUE(modeling.update_transform_translate_preview({0.3f, 0.0f, 0.0f}).ok);
    ASSERT_TRUE(present());
    ASSERT_TRUE(modeling.cancel_transform_preview().ok);
    ASSERT_TRUE(present());
    ASSERT_NO_FATAL_FAILURE(expect_current(capture("cancel_after_commit"), 1.8f));
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanComponentGridCatchupMovesSelectionAndDepthTogether) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-grid-catchup-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(directory));
  const auto settings = ViewportSettings::defaults();
  for (const auto &[context, label] :
       std::array<std::pair<ViewportOverlaySelectionContext, const char *>, 3>{{
           {ViewportOverlaySelectionContext::Vertex, "vertex"},
           {ViewportOverlaySelectionContext::Edge, "edge"},
           {ViewportOverlaySelectionContext::Face, "face"}}}) {
    SCOPED_TRACE(label);
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_component_transform_sample(context));
    auto &modeling = harness.workflow_editor().modeling();
    const auto present = [&] {
      return harness.present_component_transform_sample(context, settings);
    };
    const auto selected_pixels = [&](const QImage &image, float delta) {
      const auto pixel = harness.cutting_preview_pixel({
          -1.2f + delta,
          context == ViewportOverlaySelectionContext::Vertex ? -0.6f : 0.0f,
          0.0f});
      return context == ViewportOverlaySelectionContext::Face
          ? yellow_pixels(image, pixel) : pink_pixels(image, pixel);
    };
    const auto capture = [&](const char *phase) {
      const QImage image = harness.capture();
      EXPECT_FALSE(image.isNull());
      if (!directory.isEmpty()) {
        EXPECT_TRUE(image.save(QDir(directory).filePath(
            QStringLiteral("grid_catchup_%1_%2.png")
                .arg(QString::fromLatin1(label), QString::fromLatin1(phase)))));
      }
      return image;
    };
    ASSERT_TRUE(present());
    const auto baseline = capture("baseline");
    EXPECT_GE(selected_pixels(baseline, 0.0f), 3);
    const QuaderTransformDragBeginRequest begin{
        .tool = QuaderTransformTool::Move, .axis = QuaderTransformAxis::X,
        .pivot = {-0.9f, 0.0f, 0.0f},
        .screen_position = {100, 100}, .screen_pivot = {100, 100},
        .screen_tip = {120, 100}, .has_world_direction = true,
        .world_direction = {1, 0, 0},
        .ray = {.origin = {-0.9f, 0, 5}, .direction = {0, 0, -1}},
        .camera = {.orthographic = true, .origin = {0, 0, 5},
                   .x_axis = {1, 0, 0}, .y_axis = {0, 1, 0}, .z_axis = {0, 0, 1},
                   .viewport_size = {800, 600}, .orthographic_size = 8},
        .grid_size = 0.75, .vertex_snap_radius = 0, .grid_snap_enabled = true};
    const auto begun = modeling.begin_transform_drag(begin);
    ASSERT_TRUE(begun.ok()) << begun.error_message();
    EXPECT_FALSE(begun.changed());
    ASSERT_TRUE(present());
    EXPECT_EQ(capture("press"), baseline);
    for (const auto &[raw_delta, expected_delta] :
         std::array<std::pair<float, float>, 2>{{{0.05f, 0.15f}, {0.8f, 0.9f}}}) {
      const auto moved = modeling.update_transform_drag({
          .screen_position = {100 + raw_delta * 100, 100},
          .ray = {.origin = {-0.9f + raw_delta, 0, 5}, .direction = {0, 0, -1}},
          .camera = begin.camera, .grid_size = begin.grid_size,
          .vertex_snap_radius = 0, .grid_snap_enabled = true});
      ASSERT_TRUE(moved.ok()) << moved.error_message();
      EXPECT_NEAR(moved.value().move_delta.x, expected_delta, 1.0e-5f);
      ASSERT_TRUE(present());
      const auto image = capture(raw_delta < 0.1f ? "catchup" : "grid_step");
      EXPECT_GE(selected_pixels(image, expected_delta), 3);
      EXPECT_EQ(cyan_pixels(image, harness.cutting_preview_pixel(
          {-0.9f + expected_delta, 0.25f, -0.3f})), 0);
      const auto uploads = harness.component_overlay_stats().uploads;
      ASSERT_TRUE(present());
      EXPECT_EQ(harness.component_overlay_stats().uploads.shared_position_upload_count,
                uploads.shared_position_upload_count);
      EXPECT_EQ(harness.capture(), image);
    }
    ASSERT_TRUE(modeling.cancel_transform_drag().ok());
    ASSERT_TRUE(present());
    EXPECT_EQ(capture("cancel"), baseline);
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanKeyboardNudgesPublishSelectionDepthAndUndoTogether) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-keyboard-nudge-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(directory));
  for (const auto &[context, label] :
       std::array<std::pair<ViewportOverlaySelectionContext, const char *>, 3>{{
           {ViewportOverlaySelectionContext::Vertex, "vertex"},
           {ViewportOverlaySelectionContext::Edge, "edge"},
           {ViewportOverlaySelectionContext::Face, "face"}}}) {
    SCOPED_TRACE(label);
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_component_transform_sample(context));
    auto &editor = harness.workflow_editor();
    ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Perspective).ok());
    ASSERT_TRUE(editor.viewports().set_view(ViewportPaneId::Perspective, ViewportView::Back).ok());
    ASSERT_TRUE(editor.viewports().set_transform_space(QuaderTransformSpace::Global).ok());
    ASSERT_TRUE(editor.viewports().set_control(QuaderViewportControl::SnappingEnabled, false).ok());
    const auto present = [&] {
      return harness.present_component_transform_sample(context, ViewportSettings::defaults());
    };
    const auto capture = [&](const char *phase) {
      const QImage image = harness.capture();
      EXPECT_FALSE(image.isNull());
      if (!directory.isEmpty()) {
        EXPECT_TRUE(image.save(QDir(directory).filePath(
            QStringLiteral("keyboard_nudge_%1_%2.png")
                .arg(QString::fromLatin1(label), QString::fromLatin1(phase)))));
      }
      return image;
    };
    const auto expect_selected = [&](const QImage &image, QVec3 position) {
      const auto pixel = harness.cutting_preview_pixel(position);
      EXPECT_GE(context == ViewportOverlaySelectionContext::Face
                    ? yellow_pixels(image, pixel) : pink_pixels(image, pixel), 3);
    };
    const auto objects = editor.query().modeling_objects();
    ASSERT_EQ(objects.size(), 1u);
    const auto authored_state = [&] {
      return harness.modeling().polygon_document_read_snapshot(
          objects.front().modeling_object_id, false);
    };
    const auto expect_authored_state = [&](const ModelingPolygonDocumentReadSnapshot &expected) {
      const auto restored = authored_state();
      ASSERT_TRUE(restored.ok()) << restored.error_message();
      const auto expected_vertices = expected.document.view().vertices;
      const auto restored_vertices = restored.value().document.view().vertices;
      ASSERT_EQ(restored_vertices.size(), expected_vertices.size());
      for (std::size_t index = 0u; index < expected_vertices.size(); ++index) {
        EXPECT_EQ(restored_vertices[index].id, expected_vertices[index].id);
        EXPECT_EQ(restored_vertices[index].position.x, expected_vertices[index].position.x);
        EXPECT_EQ(restored_vertices[index].position.y, expected_vertices[index].position.y);
        EXPECT_EQ(restored_vertices[index].position.z, expected_vertices[index].position.z);
      }
      const auto &selection = restored.value().selection;
      EXPECT_EQ(selection.kind, expected.selection.kind);
      EXPECT_EQ(selection.vertices, expected.selection.vertices);
      EXPECT_EQ(selection.edges, expected.selection.edges);
      EXPECT_EQ(selection.faces, expected.selection.faces);
      EXPECT_EQ(selection.has_active, expected.selection.has_active);
      EXPECT_EQ(selection.active_vertex, expected.selection.active_vertex);
      EXPECT_EQ(selection.active_edge, expected.selection.active_edge);
      EXPECT_EQ(selection.active_face, expected.selection.active_face);
    };
    const auto expect_history_frame = [&](const QImage &restored, const QImage &expected) {
      ASSERT_EQ(restored.size(), expected.size());
      if (context != ViewportOverlaySelectionContext::Edge) {
        EXPECT_EQ(restored, expected);
        return;
      }
      // Accepted transforms retain their coordinate projection; history restores
      // a canonical projection. Their edge-boundary compositing can differ while
      // selection cores, ribbon location, authored coordinates and depth agree.
      const auto pink_core = [](QColor color) {
        return color.red() > 180 && color.green() < 130 && color.blue() > 80;
      };
      const auto cyan_core = [](QColor color) {
        return color.green() > 150 && color.blue() > 150 && color.red() < 100;
      };
      const auto selected_ribbon = [](QColor color) {
        const bool pink = color.red() > 130 && color.green() > 30 &&
            color.blue() > color.green() + 20 && color.red() > color.blue() + 20;
        const bool orange = color.red() > 130 && color.green() > 110 &&
            color.red() > color.blue() + 50 && color.green() > color.blue() + 35;
        return pink || orange;
      };
      const auto near_ribbon = [&](const QImage &image, int x, int y) {
        for (int sample_y = std::max(0, y - 1); sample_y <= std::min(image.height() - 1, y + 1); ++sample_y) {
          for (int sample_x = std::max(0, x - 1); sample_x <= std::min(image.width() - 1, x + 1); ++sample_x) {
            if (selected_ribbon(image.pixelColor(sample_x, sample_y))) return true;
          }
        }
        return false;
      };
      int changed_pink_core = 0;
      int changed_cyan_core = 0;
      int missing_ribbon = 0;
      int added_ribbon = 0;
      int changed_outside_ribbon = 0;
      for (int y = 0; y < expected.height(); ++y) {
        for (int x = 0; x < expected.width(); ++x) {
          const QColor before = expected.pixelColor(x, y);
          const QColor after = restored.pixelColor(x, y);
          changed_pink_core += pink_core(before) != pink_core(after);
          changed_cyan_core += cyan_core(before) != cyan_core(after);
          missing_ribbon += selected_ribbon(before) && !near_ribbon(restored, x, y);
          added_ribbon += selected_ribbon(after) && !near_ribbon(expected, x, y);
          changed_outside_ribbon += before != after &&
              !near_ribbon(expected, x, y) && !near_ribbon(restored, x, y);
        }
      }
      EXPECT_EQ(changed_pink_core, 0);
      EXPECT_EQ(changed_cyan_core, 0);
      EXPECT_EQ(missing_ribbon, 0);
      EXPECT_EQ(added_ribbon, 0);
      EXPECT_EQ(changed_outside_ribbon, 0);
    };
    ASSERT_TRUE(present());
    const auto baseline = capture("baseline");
    const auto baseline_state = authored_state();
    ASSERT_TRUE(baseline_state.ok()) << baseline_state.error_message();
    const auto moved = editor.commands().execute(quader_command::kViewportNudgeMoveRight);
    ASSERT_TRUE(moved.ok()) << moved.error_message();
    ASSERT_TRUE(moved.changed());
    ASSERT_TRUE(present());
    const auto translated = capture("move");
    const auto translated_state = authored_state();
    ASSERT_TRUE(translated_state.ok()) << translated_state.error_message();
    expect_selected(translated, {-0.2f,
        context == ViewportOverlaySelectionContext::Vertex ? -0.6f : 0.0f, 0.0f});
    EXPECT_EQ(cyan_pixels(translated, harness.cutting_preview_pixel({0.1f, 0.25f, -0.3f})), 0);
    const auto uploads = harness.component_overlay_stats().uploads;
    ASSERT_TRUE(present());
    EXPECT_EQ(harness.component_overlay_stats().uploads.shared_position_upload_count,
              uploads.shared_position_upload_count);
    EXPECT_EQ(harness.capture(), translated);

    const auto rotated = editor.commands().execute(quader_command::kViewportNudgeRotateRight);
    ASSERT_TRUE(rotated.ok()) << rotated.error_message();
    ASSERT_TRUE(rotated.changed());
    ASSERT_TRUE(present());
    const auto rotation = capture("rotate");
    const auto rotated_state = authored_state();
    ASSERT_TRUE(rotated_state.ok()) << rotated_state.error_message();
    const float diagonal = std::sqrt(0.5f);
    const float initial_y = context == ViewportOverlaySelectionContext::Vertex ? -0.6f : 0.0f;
    expect_selected(rotation, {0.1f + (-0.3f + initial_y) * diagonal,
                               (0.3f + initial_y) * diagonal, 0.0f});
    EXPECT_EQ(cyan_pixels(rotation, harness.cutting_preview_pixel({0.1f, 0.25f, -0.3f})), 0);
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(present());
    const auto restored_translation = capture("undo_rotate");
    expect_authored_state(translated_state.value());
    expect_history_frame(restored_translation, translated);
    EXPECT_EQ(cyan_pixels(restored_translation, harness.cutting_preview_pixel({0.1f, 0.25f, -0.3f})), 0);
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(present());
    expect_authored_state(baseline_state.value());
    EXPECT_EQ(capture("undo_move"), baseline);
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_TRUE(present());
    const auto restored_rotation = capture("redo");
    expect_authored_state(rotated_state.value());
    expect_history_frame(restored_rotation, rotation);
    EXPECT_EQ(cyan_pixels(restored_rotation, harness.cutting_preview_pixel({0.1f, 0.25f, -0.3f})), 0);
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanShiftNudgesPublishExtrusionSelectionDepthAndRepeatTogether) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-shift-nudge-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(directory));
  for (const auto &[context, label] :
       std::array<std::pair<ViewportOverlaySelectionContext, const char *>, 3>{{
           {ViewportOverlaySelectionContext::Vertex, "vertex"},
           {ViewportOverlaySelectionContext::Edge, "edge"},
           {ViewportOverlaySelectionContext::Face, "face"}}}) {
    SCOPED_TRACE(label);
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_component_transform_sample(context));
    auto &editor = harness.workflow_editor();
    ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Perspective).ok());
    ASSERT_TRUE(editor.viewports().set_view(ViewportPaneId::Perspective, ViewportView::Top).ok());
    ASSERT_TRUE(editor.viewports().set_transform_space(QuaderTransformSpace::Global).ok());
    ASSERT_TRUE(editor.viewports().set_control(QuaderViewportControl::SnappingEnabled, false).ok());
    const auto present = [&] {
      return harness.present_component_transform_sample(context, ViewportSettings::defaults());
    };
    const auto capture = [&](const char *phase) {
      const QImage image = harness.capture();
      EXPECT_FALSE(image.isNull());
      if (!directory.isEmpty()) {
        EXPECT_TRUE(image.save(QDir(directory).filePath(
            QStringLiteral("shift_nudge_%1_%2.png")
                .arg(QString::fromLatin1(label), QString::fromLatin1(phase)))));
      }
      return image;
    };
    const auto objects = editor.query().modeling_objects();
    ASSERT_EQ(objects.size(), 1u);
    const auto topology_size = [&] {
      const auto snapshot = harness.modeling().polygon_document_read_snapshot(
          objects.front().modeling_object_id, false);
      EXPECT_TRUE(snapshot.ok()) << snapshot.error_message();
      if (!snapshot.ok()) return std::pair<std::size_t, std::size_t>{};
      const auto view = snapshot.value().document.view();
      return std::pair{view.vertices.size(), view.faces.size()};
    };
    const auto rear_probe = harness.cutting_preview_pixel({-1.35f, 0.25f, -0.3f});
    const auto expect_extruded = [&](const QImage &image, float distance) {
      const auto selected_probe = harness.cutting_preview_pixel({
          -1.2f, context == ViewportOverlaySelectionContext::Vertex ? -0.6f : 0.0f,
          distance});
      if (context == ViewportOverlaySelectionContext::Edge) {
        // Extrusion preserves left/right order and activates the last copied edge.
        EXPECT_GE(yellow_pixels(image, selected_probe), 3);
        EXPECT_GE(pink_pixels(image, harness.cutting_preview_pixel(
            {-0.6f, 0.0f, distance})), 3);
      } else {
        EXPECT_GE(context == ViewportOverlaySelectionContext::Face
                      ? yellow_pixels(image, selected_probe) : pink_pixels(image, selected_probe), 3);
      }
      EXPECT_EQ(cyan_pixels(image, rear_probe), 0);
    };
    ASSERT_TRUE(present());
    const auto baseline = capture("baseline");
    const auto baseline_size = topology_size();
    EXPECT_GE(cyan_pixels(baseline, rear_probe), 3);
    const auto extruded = editor.commands().execute(
        quader_command::kViewportNudgeDuplicateOrExtrudeMoveDown);
    ASSERT_TRUE(extruded.ok()) << extruded.error_message();
    ASSERT_TRUE(extruded.changed());
    ASSERT_TRUE(present());
    const auto first = capture("extrude");
    const auto first_size = topology_size();
    EXPECT_GT(first_size.first, baseline_size.first);
    EXPECT_GT(first_size.second, baseline_size.second);
    expect_extruded(first, 1.0f);
    const auto uploads = harness.component_overlay_stats().uploads;
    ASSERT_TRUE(present());
    EXPECT_EQ(harness.component_overlay_stats().uploads.shared_position_upload_count,
              uploads.shared_position_upload_count);
    EXPECT_EQ(harness.capture(), first);

    const auto repeated = editor.commands().execute(quader_command::kEditRepeatLastAction);
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    ASSERT_TRUE(repeated.changed());
    ASSERT_TRUE(present());
    const auto second = capture("repeat");
    const auto second_size = topology_size();
    EXPECT_GT(second_size.first, first_size.first);
    EXPECT_GT(second_size.second, first_size.second);
    expect_extruded(second, 2.0f);
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(present());
    EXPECT_EQ(topology_size(), first_size);
    expect_extruded(capture("undo_repeat"), 1.0f);
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(present());
    EXPECT_EQ(topology_size(), baseline_size);
    EXPECT_EQ(capture("undo_extrude"), baseline);
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_TRUE(present());
    EXPECT_EQ(topology_size(), second_size);
    expect_extruded(capture("redo"), 2.0f);
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanClosedEdgeNudgeKeepsBothRidgeSurfacesVisibleThroughHistory) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-closed-edge-nudge-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(directory));
  for (const bool below : {false, true}) {
    SCOPED_TRACE(below ? "below" : "above");
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_closed_edge_extrusion_sample(below));
    auto &editor = harness.workflow_editor();
    ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Perspective).ok());
    ASSERT_TRUE(editor.viewports().set_view(ViewportPaneId::Perspective, ViewportView::Front).ok());
    ASSERT_TRUE(editor.viewports().set_transform_space(QuaderTransformSpace::Global).ok());
    ASSERT_TRUE(editor.viewports().set_grid_preset(6).ok());
    ASSERT_TRUE(editor.viewports().set_control(QuaderViewportControl::SnappingEnabled, false).ok());
    const auto present = [&] {
      return harness.present_component_transform_sample(
          ViewportOverlaySelectionContext::Edge, ViewportSettings::defaults());
    };
    const auto capture = [&](const char *phase) {
      const QImage image = harness.capture();
      EXPECT_FALSE(image.isNull());
      if (!directory.isEmpty()) {
        EXPECT_TRUE(image.save(QDir(directory).filePath(
            QStringLiteral("closed_edge_%1_%2.png")
                .arg(below ? QStringLiteral("below") : QStringLiteral("above"),
                     QString::fromLatin1(phase)))));
      }
      return image;
    };
    const auto expect_first_ridge = [&](const QImage &image) {
      EXPECT_GE(yellow_pixels(image, harness.cutting_preview_pixel({2.0f, -1.0f, 0.0f})), 3);
      // Both sides of the ridge must contain surface pixels away from wire overlays.
      const auto probe = harness.cutting_preview_pixel({1.5f, below ? -1.0f : -0.875f, 0.0f});
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const auto pixel = image.pixelColor(static_cast<int>(probe.x) + dx,
                                             static_cast<int>(probe.y) + dy);
          EXPECT_GT(std::min({pixel.red(), pixel.green(), pixel.blue()}), 100);
        }
      }
      if (below) {
        EXPECT_EQ(cyan_pixels(image, harness.cutting_preview_pixel(
            {1.25f, -5.0f / 6.0f, 0.0f})), 0)
            << "The ridge underside must occlude the boundary in front of the original cube.";
      }
    };
    ASSERT_TRUE(present());
    const auto baseline = capture("baseline");
    if (below) {
      EXPECT_GE(cyan_pixels(baseline, harness.cutting_preview_pixel(
          {1.25f, -5.0f / 6.0f, 0.0f})), 3);
    }
    const auto nudged = editor.commands().execute(
        quader_command::kViewportNudgeDuplicateOrExtrudeMoveLeft);
    ASSERT_TRUE(nudged.ok()) << nudged.error_message();
    ASSERT_TRUE(nudged.changed());
    ASSERT_TRUE(present());
    const auto first = capture("extrude");
    expect_first_ridge(first);
    const auto uploads = harness.component_overlay_stats().uploads;
    ASSERT_TRUE(present());
    EXPECT_EQ(harness.component_overlay_stats().uploads.shared_position_upload_count,
              uploads.shared_position_upload_count);
    EXPECT_EQ(harness.capture(), first);
    const auto repeated = editor.commands().execute(quader_command::kEditRepeatLastAction);
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    ASSERT_TRUE(repeated.changed());
    ASSERT_TRUE(present());
    EXPECT_GE(yellow_pixels(capture("repeat"),
                         harness.cutting_preview_pixel({3.0f, -1.0f, 0.0f})), 3);
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(present());
    expect_first_ridge(capture("undo_repeat"));
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(present());
    EXPECT_EQ(capture("undo_extrude"), baseline);
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_TRUE(present());
    expect_first_ridge(capture("redo"));
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanVertexDragExtrusionPublishesPreviewCancelCommitAndUndo) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-vertex-extrude-drag-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(directory));
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.create_component_transform_sample(ViewportOverlaySelectionContext::Vertex, true));
  auto &editor = harness.workflow_editor();
  auto &modeling = editor.modeling();
  const auto present = [&] {
    return harness.present_component_transform_sample(
        ViewportOverlaySelectionContext::Vertex, ViewportSettings::defaults());
  };
  const auto capture = [&](const char *phase) {
    const QImage image = harness.capture();
    EXPECT_FALSE(image.isNull());
    if (!directory.isEmpty()) {
      EXPECT_TRUE(image.save(QDir(directory).filePath(
          QStringLiteral("vertex_drag_extrude_%1.png").arg(QString::fromLatin1(phase)))));
    }
    return image;
  };
  const QuaderTransformDragBeginRequest begin{
      .tool = QuaderTransformTool::Move, .operation = QuaderTransformOperation::Extrude,
      .axis = QuaderTransformAxis::Z, .pivot = {-1.2f, -0.6f, 0.0f},
      .screen_position = {100, 100}, .screen_pivot = {100, 100}, .screen_tip = {120, 100},
      .has_world_direction = true, .world_direction = {0, 0, 1},
      .ray = {.origin = {5, -0.6f, 0}, .direction = {-1, 0, 0}},
      .camera = {.orthographic = true, .origin = {5, 0, 0},
                 .x_axis = {0, 0, 1}, .y_axis = {0, 1, 0}, .z_axis = {-1, 0, 0},
                 .viewport_size = {800, 600}, .orthographic_size = 8},
      .grid_size = 0.25, .vertex_snap_radius = 0, .grid_snap_enabled = false};
  const auto move = [&] {
    return modeling.update_transform_drag({
        .screen_position = {200, 100},
        .ray = {.origin = {5, -0.6f, 1}, .direction = {-1, 0, 0}},
        .camera = begin.camera, .grid_size = begin.grid_size,
        .vertex_snap_radius = 0, .grid_snap_enabled = false});
  };
  const auto expect_extruded = [&](const QImage &image) {
    const auto tip = harness.cutting_preview_pixel({-1.2f, -0.6f, 1.0f});
    int selected_tip_pixels = 0;
    for (int dy = -3; dy <= 3; ++dy) {
      for (int dx = -3; dx <= 3; ++dx) {
        const auto pixel = image.pixelColor(static_cast<int>(tip.x) + dx,
                                           static_cast<int>(tip.y) + dy);
        selected_tip_pixels += pixel.red() > 160 && pixel.green() > 60 &&
            pixel.red() > pixel.green() + 70 && pixel.green() > pixel.blue() + 35 ? 1 : 0;
      }
    }
    EXPECT_GE(selected_tip_pixels, 3) << "The single selected vertex uses its orange point style.";
    EXPECT_GE(cyan_pixels(image, harness.cutting_preview_pixel({-1.2f, -0.6f, 0.0f})), 3)
        << "Extrusion keeps its original boundary vertex; an ordinary move does not.";
    const auto surface_probe = harness.cutting_preview_pixel({-1.17f, -0.54f, 0.9f});
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        const auto pixel = image.pixelColor(static_cast<int>(surface_probe.x) + dx,
                                           static_cast<int>(surface_probe.y) + dy);
        EXPECT_GT(std::min({pixel.red(), pixel.green(), pixel.blue()}), 100)
            << "The new surface must render outside the original quad's silhouette.";
      }
    }
  };
  ASSERT_TRUE(present());
  const auto baseline = capture("baseline");
  const auto surface_probe = harness.cutting_preview_pixel({-1.17f, -0.54f, 0.9f});
  EXPECT_LT(baseline.pixelColor(static_cast<int>(surface_probe.x),
                               static_cast<int>(surface_probe.y)).red(), 70);
  const auto undo_count = harness.undo_count();
  for (const bool commit : {false, true}) {
    const auto started = modeling.begin_transform_drag(begin);
    ASSERT_TRUE(started.ok()) << started.error_message();
    ASSERT_EQ(started.value().operation, QuaderTransformOperation::Extrude);
    const auto moved = move();
    ASSERT_TRUE(moved.ok()) << moved.error_message();
    EXPECT_NEAR(moved.value().move_delta.z, 1.0f, 1.0e-5f);
    ASSERT_TRUE(present());
    const auto preview = capture(commit ? "accept_preview" : "cancel_preview");
    expect_extruded(preview);
    const auto uploads = harness.component_overlay_stats().uploads;
    ASSERT_TRUE(present());
    EXPECT_EQ(harness.component_overlay_stats().uploads.shared_position_upload_count,
              uploads.shared_position_upload_count);
    EXPECT_EQ(harness.capture(), preview);
    if (!commit) {
      ASSERT_TRUE(modeling.cancel_transform_drag().ok());
      ASSERT_TRUE(present());
      EXPECT_EQ(capture("cancel"), baseline);
      EXPECT_EQ(harness.undo_count(), undo_count);
    } else {
      ASSERT_TRUE(modeling.commit_transform_drag().ok());
      ASSERT_TRUE(present());
      expect_extruded(capture("commit"));
      EXPECT_EQ(harness.undo_count(), undo_count + 1u);
      ASSERT_TRUE(editor.history().undo().ok());
      ASSERT_TRUE(present());
      EXPECT_EQ(capture("undo"), baseline);
      ASSERT_TRUE(editor.history().redo().ok());
      ASSERT_TRUE(present());
      expect_extruded(capture("redo"));
    }
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

struct LoopLinePixelCoverage {
  int samples = 0;
  int colored = 0;
  int transitions = 0;
  int longest_color_run = 0;
  int longest_gap_run = 0;
};

LoopLinePixelCoverage loop_line_pixel_coverage(
    const QImage &image, QVec2 start, QVec2 end, float inset = 0.0f,
    float normal_offset = 0.0f, bool search_normal = true) {
  const float length = std::hypot(end.x - start.x, end.y - start.y);
  if (length <= 2.0f * inset) return {};
  const QVec2 tangent{(end.x - start.x) / length, (end.y - start.y) / length};
  const QVec2 normal{-tangent.y, tangent.x};
  LoopLinePixelCoverage result;
  bool previous = false;
  int run = 0;
  for (float distance = inset; distance <= length - inset; distance += 1.0f) {
    bool colored = false;
    const int search_radius = search_normal ? 1 : 0;
    for (int offset = -search_radius; offset <= search_radius; ++offset) {
      const float side = normal_offset + static_cast<float>(offset);
      const int x = static_cast<int>(std::lround(start.x + tangent.x * distance + normal.x * side));
      const int y = static_cast<int>(std::lround(start.y + tangent.y * distance + normal.y * side));
      if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) continue;
      const QColor pixel = image.pixelColor(x, y);
      colored = colored || (pixel.green() > 140 && pixel.green() > pixel.red() + 80 &&
                            pixel.green() > pixel.blue() + 80);
    }
    if (result.samples != 0 && colored != previous) {
      ++result.transitions;
      run = 0;
    }
    ++run;
    ++result.samples;
    if (colored) {
      ++result.colored;
      result.longest_color_run = std::max(result.longest_color_run, run);
    } else {
      result.longest_gap_run = std::max(result.longest_gap_run, run);
    }
    previous = colored;
  }
  return result;
}

void expect_solid_loop_coverage(const LoopLinePixelCoverage &coverage) {
  ASSERT_GE(coverage.samples, 20);
  EXPECT_GE(coverage.colored, static_cast<int>(0.97f * coverage.samples))
      << "A visible connector must stay solid across its projected span.";
  EXPECT_LE(coverage.longest_gap_run, 1);
}

void expect_dashed_loop_coverage(const LoopLinePixelCoverage &coverage) {
  ASSERT_GE(coverage.samples, 30);
  EXPECT_GE(coverage.colored, static_cast<int>(0.2f * coverage.samples));
  EXPECT_LE(coverage.colored, static_cast<int>(0.8f * coverage.samples));
  EXPECT_GE(coverage.transitions, 6)
      << "A hidden connector must contain repeated visible dashes and empty gaps.";
  EXPECT_GE(coverage.longest_color_run, 2);
  EXPECT_GE(coverage.longest_gap_run, 2);
}

void save_loop_occlusion_capture(const QImage &image, const QString &stem) {
  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) return;
  ASSERT_TRUE(QDir().mkpath(directory));
  const QString path = QDir(directory).filePath(stem + QStringLiteral(".png"));
  EXPECT_TRUE(image.save(path));
  std::cout << "Edge loop occlusion GPU png=" << path.toStdString() << '\n';
}

TEST(SelectionOverlayRuntimeTests,
     VulkanEdgeLoopPreviewWallOcclusionChangesOnlyHiddenSpansAt100And200Percent) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-edge-loop-wall-occlusion-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    SCOPED_TRACE(projection == ViewportProjection::Perspective ? "perspective" : "orthographic");
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.create_edge_loop_sample());
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(3.0f, projection));
    const auto pixels = harness.edge_loop_pixels(-0.25f);
    const float split_x = pixels[2].x;
    const auto add_wall = [&](float left, float right, float z) {
      const std::array<QVec2, 4u> corners{{
          {left, pixels[2].y - 35.0f}, {right, pixels[2].y - 35.0f},
          {right, pixels[2].y + 35.0f}, {left, pixels[2].y + 35.0f}}};
      std::array<MeshVertex, 4u> vertices;
      for (std::size_t index = 0u; index < corners.size(); ++index) {
        vertices[index].position = harness.overlay_audit_world_point(corners[index], z);
      }
      return harness.create_outline_depth_quad(vertices);
    };
    ASSERT_TRUE(add_wall(pixels[0].x - 20.0f, split_x, 0.5f));
    ASSERT_TRUE(harness.modeling().activate_viewport_tool(ModelingViewportTool::InsertEdgeLoop).ok);
    ASSERT_TRUE(harness.modeling().update_insert_edge_loop_hover(harness.edge_loop_event(-0.25f)).ok);
    ASSERT_TRUE(harness.modeling().tool_status().insert_edge_loop_has_preview);
    QImage at_100_percent;
    LoopLinePixelCoverage hidden_at_100;
    for (const auto &[scale, label] : std::array<std::pair<float, const char *>, 3>{{
             {1.0f, "100"}, {2.0f, "200"}, {1.0f, "down"}}}) {
      SCOPED_TRACE(label);
      ASSERT_TRUE(harness.present_edge_loop_sample(scale));
      const QImage image = harness.capture();
      ASSERT_FALSE(image.isNull());
      save_loop_occlusion_capture(image, QStringLiteral("loop-wall-%1-%2")
          .arg(projection == ViewportProjection::Perspective ? "perspective" : "orthographic")
          .arg(QString::fromLatin1(label)));
      expect_solid_loop_coverage(loop_line_pixel_coverage(image,
          {split_x + 12.0f, pixels[2].y}, {pixels[1].x - 20.0f, pixels[2].y}));
      const auto hidden = loop_line_pixel_coverage(image,
          {pixels[0].x + 20.0f, pixels[2].y}, {split_x - 12.0f, pixels[2].y});
      expect_dashed_loop_coverage(hidden);
      if (at_100_percent.isNull()) {
        at_100_percent = image;
        hidden_at_100 = hidden;
      } else if (scale == 2.0f) {
        EXPECT_GE(hidden.longest_gap_run, hidden_at_100.longest_gap_run * 1.5f);
        EXPECT_LT(hidden.transitions, hidden_at_100.transitions);
      } else {
        EXPECT_EQ(image, at_100_percent);
      }
      const auto uploads = harness.component_overlay_stats().uploads;
      ASSERT_TRUE(harness.present_edge_loop_sample(scale));
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_FALSE(harness.last_submission().dynamic_submitted);
      EXPECT_EQ(harness.component_overlay_stats().uploads.geometry_creation_count,
                uploads.geometry_creation_count);
      EXPECT_EQ(harness.capture(false), image);
    }
    ASSERT_TRUE(harness.modeling().cancel_active_modal_tool(false).ok);
    ASSERT_TRUE(harness.present_edge_loop_sample());
    EXPECT_EQ(loop_line_pixel_coverage(harness.capture(), pixels[0], pixels[1], 20.0f).colored, 0);

    SelectionOverlayRuntimeHarness fully_hidden;
    ASSERT_TRUE(fully_hidden.initialize(0.0f, false));
    ASSERT_TRUE(fully_hidden.create_edge_loop_sample());
    ASSERT_TRUE(fully_hidden.prepare_overlay_audit_camera(3.0f, projection));
    const auto hidden_pixels = fully_hidden.edge_loop_pixels(-0.25f);
    const std::array<QVec2, 4u> full_wall_corners{{
        {hidden_pixels[0].x - 20.0f, hidden_pixels[2].y - 35.0f},
        {hidden_pixels[1].x + 20.0f, hidden_pixels[2].y - 35.0f},
        {hidden_pixels[1].x + 20.0f, hidden_pixels[2].y + 35.0f},
        {hidden_pixels[0].x - 20.0f, hidden_pixels[2].y + 35.0f}}};
    std::array<MeshVertex, 4u> full_wall;
    for (std::size_t index = 0u; index < full_wall_corners.size(); ++index) {
      full_wall[index].position = fully_hidden.overlay_audit_world_point(full_wall_corners[index], 0.5f);
    }
    ASSERT_TRUE(fully_hidden.create_outline_depth_quad(full_wall));
    ASSERT_TRUE(fully_hidden.modeling().activate_viewport_tool(ModelingViewportTool::InsertEdgeLoop).ok);
    // Start on the uncovered seed edge, then drag its locked factor behind the
    // wall. Scene creation is complete before the retained preview exists.
    ASSERT_TRUE(fully_hidden.modeling().begin_insert_edge_loop_drag(fully_hidden.edge_loop_event(0.5f)).ok);
    ASSERT_TRUE(fully_hidden.modeling().update_insert_edge_loop_drag(fully_hidden.edge_loop_event(-0.25f)).ok);
    ASSERT_TRUE(fully_hidden.modeling().tool_status().insert_edge_loop_has_preview);
    ASSERT_TRUE(fully_hidden.present_edge_loop_sample());
    const QImage hidden = fully_hidden.capture();
    ASSERT_FALSE(hidden.isNull());
    expect_dashed_loop_coverage(loop_line_pixel_coverage(hidden, hidden_pixels[0], hidden_pixels[1], 20.0f));
    save_loop_occlusion_capture(hidden, QStringLiteral("loop-wall-%1-fully-hidden")
        .arg(projection == ViewportProjection::Perspective ? "perspective" : "orthographic"));
    ASSERT_TRUE(fully_hidden.modeling().cancel_active_modal_tool(false).ok);
    ASSERT_TRUE(fully_hidden.present_edge_loop_sample());
    const QImage canceled = fully_hidden.capture();
    EXPECT_EQ(loop_line_pixel_coverage(canceled, hidden_pixels[0], hidden_pixels[1], 20.0f).colored, 0)
        << "Cancellation must also retire the occluded companion.";
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanEdgeLoopSelfOcclusionFollowsCameraWithoutRebuildingWorldGeometry) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-edge-loop-self-occlusion-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.create_closed_edge_loop_sample());
  ViewportCameraController camera;
  camera.set_viewport_size(SelectionOverlayRuntimeHarness::kWidth, SelectionOverlayRuntimeHarness::kHeight);
  camera.set_clip_planes(0.01f, 200.0f);
  ASSERT_TRUE(camera.restore_perspective_pose({
      .eye = {3.0f, 1.5f, 4.0f}, .target = {}, .up = {0.0f, 1.0f, 0.0f}}));
  harness.set_sample_camera(camera);
  ASSERT_TRUE(harness.modeling().update_insert_edge_loop_hover(
      harness.tool_topology_event({1.0f, -0.25f, 0.75f})).ok);
  ASSERT_TRUE(harness.modeling().tool_status().insert_edge_loop_has_preview);
  ASSERT_TRUE(harness.present_edge_loop_sample());
  const auto frozen = harness.modeling().volatile_overlay_snapshot();
  ASSERT_NE(frozen.frame, nullptr);
  std::vector<const ViewportOverlayLine *> connectors;
  int ordinary_hover_lines = 0;
  for (const auto &line : frozen.frame->lines) {
    if (line.occlusion_style == ViewportOverlayLineOcclusionStyle::Dashed) {
      connectors.push_back(&line);
      EXPECT_EQ(line.source, ViewportOverlaySource::Tool);
    } else {
      EXPECT_EQ(line.occlusion_style, ViewportOverlayLineOcclusionStyle::Default);
      EXPECT_EQ(line.layer, ViewportOverlayLayer::HoverEdge);
      ++ordinary_hover_lines;
    }
  }
  ASSERT_EQ(connectors.size(), 4u);
  EXPECT_EQ(ordinary_hover_lines, 1)
      << "The authored seed-edge hover remains outside the connector dash policy.";
  const auto uploads = harness.component_overlay_stats().uploads;
  const auto selection_builds = harness.selection_frame_builds();
  const auto volatile_builds = harness.modeling().volatile_overlay_cache_stats().build_count;
  QImage initial;
  std::array<bool, 4u> initial_visibility{};
  int changed_classifications = 0;
  for (const auto &[eye_x, label] : std::array<std::pair<float, const char *>, 3>{{
           {3.0f, "right"}, {-3.0f, "left"}, {3.0f, "restored"}}}) {
    SCOPED_TRACE(label);
    ASSERT_TRUE(camera.restore_perspective_pose({
        .eye = {eye_x, 1.5f, 4.0f}, .target = {}, .up = {0.0f, 1.0f, 0.0f}}));
    harness.set_sample_camera(camera);
    ASSERT_TRUE(harness.present_edge_loop_sample());
    const QImage image = harness.capture();
    ASSERT_FALSE(image.isNull());
    int visible_segments = 0;
    int hidden_segments = 0;
    for (std::size_t index = 0u; index < connectors.size(); ++index) {
      const auto &line = *connectors[index];
      const bool visible = harness.edge_loop_point_is_scene_visible((line.start + line.end) * 0.5f);
      const auto coverage = loop_line_pixel_coverage(image,
          harness.overlay_audit_image_point(line.start), harness.overlay_audit_image_point(line.end), 8.0f);
      if (visible) {
        ++visible_segments;
        expect_solid_loop_coverage(coverage);
      } else {
        ++hidden_segments;
        expect_dashed_loop_coverage(coverage);
      }
      if (initial.isNull()) initial_visibility[index] = visible;
      else if (eye_x < 0.0f && initial_visibility[index] != visible) ++changed_classifications;
    }
    EXPECT_GE(visible_segments, 1);
    EXPECT_GE(hidden_segments, 1);
    if (initial.isNull()) initial = image;
    else if (eye_x > 0.0f) EXPECT_EQ(image, initial);
    else EXPECT_NE(image, initial);
    save_loop_occlusion_capture(image, QStringLiteral("loop-self-occlusion-%1").arg(QString::fromLatin1(label)));
    const auto retained = harness.modeling().volatile_overlay_snapshot();
    EXPECT_EQ(retained.frame, frozen.frame);
    EXPECT_EQ(retained.source.publication_revision, frozen.source.publication_revision);
    EXPECT_EQ(harness.modeling().volatile_overlay_cache_stats().build_count, volatile_builds);
    EXPECT_EQ(harness.selection_frame_builds(), selection_builds);
    EXPECT_FALSE(harness.last_submission().persistent_submitted);
    EXPECT_FALSE(harness.last_submission().dynamic_submitted);
    const auto retained_uploads = harness.component_overlay_stats().uploads;
    EXPECT_EQ(retained_uploads.geometry_creation_count, uploads.geometry_creation_count);
    EXPECT_EQ(retained_uploads.position_upload_count, uploads.position_upload_count);
    EXPECT_EQ(retained_uploads.static_upload_count, uploads.static_upload_count);
  }
  EXPECT_GE(changed_classifications, 2)
      << "Orbit must reveal the previous hidden side and hide the previous visible side.";
}

TEST(SelectionOverlayRuntimeTests,
     VulkanDashedLoopStyleKeepsSlopedCoplanarWidthSolidAndClipsHiddenShortSpans) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-edge-loop-slope-clipping-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  for (const auto projection : {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    SCOPED_TRACE(projection == ViewportProjection::Perspective ? "perspective" : "orthographic");
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(0.0f, false));
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, projection));
    const std::array<QVec2, 4u> corners{{{70.0f, 100.0f}, {570.0f, 100.0f},
                                       {570.0f, 380.0f}, {70.0f, 380.0f}}};
    std::array<MeshVertex, 4u> vertices;
    for (std::size_t index = 0u; index < corners.size(); ++index) {
      vertices[index].position = harness.overlay_audit_world_point(corners[index], index < 2u ? 4.0f : -20.0f);
    }
    ASSERT_TRUE(harness.create_outline_depth_quad(vertices));
    // Each cross-section is on the authored plane. Perspective screen-space
    // midpoints interpolate reciprocal depth; orthographic ones interpolate Z.
    const float connector_z = projection == ViewportProjection::Perspective
        ? 5.0f - 1.0f / (0.5f / 1.0f + 0.5f / 25.0f) : -8.0f;
    ViewportOverlayFrame frame;
    frame.lines = {{
        .layer = ViewportOverlayLayer::HoverEdge,
        .start = harness.overlay_audit_world_point({100.0f, 240.0f}, connector_z),
        .end = harness.overlay_audit_world_point({540.0f, 240.0f}, connector_z),
        .color = rgba8(30, 245, 55), .width_pixels = 6.0f,
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::Tool,
        .surface_support = {{{vertices[0].position, 0}}},
        .occlusion_style = ViewportOverlayLineOcclusionStyle::Dashed}};
    harness.set_overlay_audit_frame(frame);
    QImage at_100_percent;
    for (const auto &[scale, label] : std::array<std::pair<float, const char *>, 3>{{
             {1.0f, "100"}, {2.0f, "200"}, {1.0f, "down"}}}) {
      SCOPED_TRACE(label);
      harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Edge);
      const QImage image = harness.capture();
      ASSERT_FALSE(image.isNull());
      for (const float offset : {-2.0f * scale, 0.0f, 2.0f * scale}) {
        SCOPED_TRACE(offset);
        expect_solid_loop_coverage(loop_line_pixel_coverage(
            image, {100.0f, 240.0f}, {540.0f, 240.0f}, 20.0f, offset, false));
      }
      if (at_100_percent.isNull()) at_100_percent = image;
      else if (scale == 1.0f) EXPECT_EQ(image, at_100_percent);
      save_loop_occlusion_capture(image, QStringLiteral("loop-coplanar-slope-%1-%2")
          .arg(projection == ViewportProjection::Perspective ? "perspective" : "orthographic")
          .arg(QString::fromLatin1(label)));
    }
  }

  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, ViewportProjection::Perspective, 1.0f));
  const std::array<QVec2, 4u> wall_corners{{{320.0f, 210.0f}, {600.0f, 210.0f},
                                         {600.0f, 420.0f}, {320.0f, 420.0f}}};
  std::array<MeshVertex, 4u> wall;
  for (std::size_t index = 0u; index < wall_corners.size(); ++index) {
    wall[index].position = harness.overlay_audit_world_point(wall_corners[index], 2.0f);
  }
  ASSERT_TRUE(harness.create_outline_depth_quad(wall));
  ViewportOverlayFrame frame;
  const auto line = [&](QVec2 start, float start_z, QVec2 end, float end_z,
                        ViewportOverlayLineOcclusionStyle style) {
    return ViewportOverlayLine{
        .layer = ViewportOverlayLayer::HoverEdge,
        .start = harness.overlay_audit_world_point(start, start_z),
        .end = harness.overlay_audit_world_point(end, end_z),
        .color = rgba8(30, 245, 55), .width_pixels = 3.0f,
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::Tool, .occlusion_style = style};
  };
  frame.lines = {
      line({100.0f, 260.0f}, 4.5f, {560.0f, 260.0f}, -20.0f,
           ViewportOverlayLineOcclusionStyle::Dashed),
      line({350.0f, 320.0f}, 0.0f, {570.0f, 320.0f}, 0.0f,
           ViewportOverlayLineOcclusionStyle::Default),
      line({400.0f, 370.0f}, 0.0f, {401.0f, 370.0f}, 0.0f,
           ViewportOverlayLineOcclusionStyle::Dashed)};
  harness.set_overlay_audit_frame(frame);
  for (const float scale : {1.0f, 2.0f}) {
    harness.present_overlay_audit_frame(scale, ViewportOverlaySelectionContext::Edge);
    const QImage image = harness.capture();
    ASSERT_FALSE(image.isNull());
    expect_solid_loop_coverage(loop_line_pixel_coverage(image, {355.0f, 260.0f}, {470.0f, 260.0f}));
    expect_dashed_loop_coverage(loop_line_pixel_coverage(image, {500.0f, 260.0f}, {555.0f, 260.0f}));
    EXPECT_EQ(loop_line_pixel_coverage(image, {105.0f, 260.0f}, {195.0f, 260.0f}).colored, 0)
        << "The near-clipped endpoint must not leave an extrapolated ribbon.";
    // Ordinary Tool/HoverEdge keeps its established draw-on-top behavior even
    // beside a tagged connector in the same layer and source partition.
    expect_solid_loop_coverage(loop_line_pixel_coverage(image, {360.0f, 320.0f}, {560.0f, 320.0f}));
    EXPECT_GE(loop_preview_green_pixels(image, {400.0f, 370.0f}, false), 3)
        << "A hidden connector shorter than one dash must retain its rounded start cap.";
    save_loop_occlusion_capture(image, QStringLiteral("loop-near-clip-short-%1").arg(static_cast<int>(scale * 100.0f)));
  }
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
     VulkanCutKeepModesHideDiscardedSurfaceAndRestoreCancelCommitHistory) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-cut-kept-surface-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(directory));

  SelectionOverlayRuntimeHarness harness;
  // Match the application's sRGB composition target so authored orange/red
  // tool colors are encoded into the readback PNG in their display color space.
  ASSERT_TRUE(harness.initialize(0.0f, false, ViewportRenderSurface::SrgbSwapchain));
  auto &editor = harness.workflow_editor();
  auto &modeling = harness.modeling();
  ViewportCameraController camera;
  camera.view_axis(ViewportViewAxis::Y, 1);
  camera.focus_origin(4.0f);
  harness.set_sample_camera(camera);
  auto settings = ViewportSettings::defaults();
  auto selection_fill = settings.color(ViewportColorSettingId::SelectionFace);
  selection_fill.a = 0.0f;
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionFace, selection_fill));
  // Control the selection tint so exact patch comparisons prove that the
  // source material survives preview, commit and history without conflating
  // ordinary selection fill with source-replacing tool presentation.
  const auto capture = [&](const char *phase) {
    EXPECT_TRUE(harness.present_workflow_pane(0u, &settings, true));
    QImage image = harness.capture();
    EXPECT_FALSE(image.isNull());
    if (!directory.isEmpty()) {
      EXPECT_TRUE(image.save(QDir(directory).filePath(
          QStringLiteral("cut-kept-surface-%1.png").arg(QString::fromLatin1(phase)))));
    }
    return image;
  };
  const QImage background = capture("background");
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Cut kept surface pixel regression");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  const QImage source = capture("source");
  ASSERT_TRUE(harness.last_submission().selection_dimension_bounds.has_value());
  const std::array probes{
      harness.cutting_preview_pixel({0.3f, 1.0f, -0.6f}),
      harness.cutting_preview_pixel({0.3f, 1.0f, 0.6f})};
  const std::array outline_probes{
      harness.cutting_preview_pixel({0.3f, 1.0f, -1.0f}),
      harness.cutting_preview_pixel({0.3f, 1.0f, 1.0f})};
  const auto patch = [](const QImage &image, QVec2 probe) {
    return image.copy(QRect(static_cast<int>(probe.x) - 3,
                            static_cast<int>(probe.y) - 3, 7, 7));
  };
  for (const auto probe : probes) {
    EXPECT_NE(patch(source, probe), patch(background, probe))
        << "Both probes must initially cover actual box surfaces.";
  }
  for (const auto probe : outline_probes) EXPECT_GE(yellow_pixels(source, probe), 3);
  const auto draw_cut = [&](const char *activation_phase) {
    EXPECT_TRUE(editor.tools().activate(ViewportToolId::Cut).ok());
    const QImage activated = capture(activation_phase);
    EXPECT_FALSE(harness.last_submission().selection_dimension_bounds.has_value());
    for (const auto probe : outline_probes) {
      EXPECT_EQ(yellow_pixels(activated, probe), 0)
          << "Ordinary selection disappears immediately, before placing Cut points.";
    }
    auto start = test::drawn_plane_event(-0.5f);
    start.ray.origin.y = 20.0f;
    start.camera.origin = {0.0f, 20.0f, 10.0f};
    auto end = start;
    end.ray.origin.x = 0.5f;
    end.dragging = true;
    EXPECT_TRUE(modeling.click_cut(start).ok);
    EXPECT_TRUE(modeling.update_cut_hover(end).ok);
    EXPECT_TRUE(modeling.end_cut_drag().ok);
    EXPECT_TRUE(modeling.tool_status().cut_preview_valid);
  };
  const auto undo_count = harness.undo_count();
  draw_cut("activated");
  ASSERT_TRUE(modeling.set_cut_mode(ModelingCutMode::KeepBoth).ok);
  const QImage both = capture("keep-both");
  for (const auto probe : probes) {
    EXPECT_EQ(patch(both, probe), patch(source, probe));
  }
  const auto contour_probe = harness.cutting_preview_pixel({0.75f, 1.0f, 0.0f});
  EXPECT_GE(yellow_pixels(both, contour_probe), 3);
  for (const auto probe : outline_probes) {
    EXPECT_EQ(yellow_pixels(both, probe), 0);
    EXPECT_EQ(red_pixels(both, probe), 0);
  }
  EXPECT_FALSE(harness.last_submission().selection_dimension_bounds.has_value());
  ASSERT_TRUE(modeling.set_cut_mode(ModelingCutMode::DiscardLeft).ok);
  const QImage first_half = capture("discard-left");
  const std::array retained{
      patch(first_half, probes[0]) == patch(source, probes[0]),
      patch(first_half, probes[1]) == patch(source, probes[1])};
  ASSERT_NE(retained[0], retained[1])
      << "A one-sided preview must retain exactly one original surface.";
  for (std::size_t index = 0; index < probes.size(); ++index) {
    EXPECT_EQ(patch(first_half, probes[index]),
              patch(retained[index] ? source : background, probes[index]))
        << "Discarded geometry must disappear rather than receive an overlay tint.";
    EXPECT_EQ(yellow_pixels(first_half, outline_probes[index]), 0);
    if (retained[index]) {
      EXPECT_EQ(red_pixels(first_half, outline_probes[index]), 0);
    } else {
      EXPECT_GE(red_pixels(first_half, outline_probes[index]), 3);
    }
  }
  EXPECT_GE(yellow_pixels(first_half, contour_probe), 3);
  const auto outline_build_count = harness.tool_preview_outline_build_count();
  EXPECT_EQ(capture("discard-left-retained"), first_half);
  ASSERT_TRUE(harness.present_workflow_pane(0u, &settings, true));
  EXPECT_EQ(harness.tool_preview_outline_build_count(), outline_build_count)
      << "Unchanged frames must retain the accepted preview outline projection.";
  ASSERT_TRUE(modeling.set_cut_mode(ModelingCutMode::DiscardRight).ok);
  const QImage second_half = capture("discard-right");
  for (std::size_t index = 0; index < probes.size(); ++index) {
    EXPECT_EQ(patch(second_half, probes[index]),
              patch(retained[index] ? background : source, probes[index]));
    EXPECT_EQ(yellow_pixels(second_half, outline_probes[index]), 0);
    if (retained[index]) {
      EXPECT_GE(red_pixels(second_half, outline_probes[index]), 3);
    } else {
      EXPECT_EQ(red_pixels(second_half, outline_probes[index]), 0);
    }
  }
  EXPECT_GE(yellow_pixels(second_half, contour_probe), 3);
  EXPECT_EQ(harness.undo_count(), undo_count);
  ASSERT_TRUE(editor.tools().cancel_modal_tool().ok());
  EXPECT_EQ(capture("cancel"), source);
  EXPECT_EQ(harness.undo_count(), undo_count);

  draw_cut("activated-before-commit");
  ASSERT_TRUE(modeling.set_cut_mode(ModelingCutMode::DiscardRight).ok);
  const QImage accepted_preview = capture("accepted-preview");
  for (const auto probe : probes) {
    EXPECT_EQ(patch(accepted_preview, probe), patch(second_half, probe));
  }
  ASSERT_TRUE(editor.tools().commit_modeling_tool(ViewportToolId::Cut).ok());
  const QImage committed = capture("commit");
  for (const auto probe : probes) {
    EXPECT_EQ(patch(committed, probe), patch(accepted_preview, probe));
  }
  EXPECT_EQ(harness.undo_count(), undo_count + 1u);
  ASSERT_TRUE(editor.history().undo().ok());
  const QImage undone = capture("undo");
  for (const auto probe : probes) EXPECT_EQ(patch(undone, probe), patch(source, probe));
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(capture("redo"), committed);
}

TEST(SelectionOverlayRuntimeTests,
     VulkanCutPerspectiveCubeKeepsContourDiscardWirePlaneAndHandles) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-cut-perspective-reference-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(directory));
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false, ViewportRenderSurface::SrgbSwapchain));
  ASSERT_TRUE(harness.set_native_project_camera_pose(
      {.eye = {4.0f, 3.5f, 5.0f}, .target = {}, .up = {0.0f, 1.0f, 0.0f}}));
  auto &editor = harness.workflow_editor();
  auto &modeling = harness.modeling();
  const auto object = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Cut perspective cube reference");
  ASSERT_TRUE(object.ok());
  ASSERT_TRUE(editor.selection().select_object(object.value()).ok());
  auto settings = ViewportSettings::defaults();
  auto fill = settings.color(ViewportColorSettingId::SelectionFace);
  fill.a = 0.0f;
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionFace, fill));
  const auto capture = [&](const char *phase) {
    EXPECT_TRUE(harness.present_workflow_pane(0u, &settings, true));
    const auto image = harness.capture();
    EXPECT_FALSE(image.isNull());
    if (!directory.isEmpty()) {
      EXPECT_TRUE(image.save(QDir(directory).filePath(
          QStringLiteral("cut-perspective-%1.png").arg(QString::fromLatin1(phase)))));
    }
    return image;
  };
  const auto brightness = [&](const QImage &image, QVec3 world) {
    const auto probe = harness.cutting_preview_pixel(world);
    int total = 0;
    for (int y = int(probe.y) - 1; y <= int(probe.y) + 1; ++y) {
      for (int x = int(probe.x) - 1; x <= int(probe.x) + 1; ++x) {
        const auto color = image.pixelColor(x, y);
        total += std::min({color.red(), color.green(), color.blue()});
      }
    }
    return total / 9;
  };
  const std::array surface_points{QVec3{0.7f, 1.0f, 0.7f}, QVec3{-0.4f, 1.0f, -0.4f}};
  const std::array wire_points{QVec3{1.0f, 0.2f, 1.0f}, QVec3{-0.5f, 1.0f, -1.0f}};
  const std::array contour_points{QVec3{0.5f, 1.0f, 0.5f}, QVec3{0.0f, 0.6f, 1.0f}};
  const QImage source = capture("source");
  EXPECT_GE(yellow_pixels(source, harness.cutting_preview_pixel(wire_points[1])), 3);
  EXPECT_TRUE(harness.last_submission().selection_dimension_bounds.has_value());
  for (const auto point : surface_points) EXPECT_GT(brightness(source, point), 170);
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Cut).ok());
  EXPECT_FALSE(editor.query().tool_status().cut_cap);
  const QImage activated = capture("activated");
  EXPECT_FALSE(harness.last_submission().selection_dimension_bounds.has_value());
  for (const auto point : wire_points) {
    EXPECT_EQ(yellow_pixels(activated, harness.cutting_preview_pixel(point)), 0);
  }
  auto first = test::drawn_plane_event(-0.25f);
  first.ray.origin = {-0.25f, 20.0f, 1.25f};
  first.camera.origin = {0.0f, 20.0f, 10.0f};
  first.snap_enabled = false;
  auto second = first;
  second.ray.origin = {1.25f, 20.0f, -0.25f};
  second.dragging = true;
  ASSERT_TRUE(modeling.click_cut(first).ok);
  ASSERT_TRUE(modeling.update_cut_hover(second).ok);
  ASSERT_TRUE(modeling.end_cut_drag().ok);
  ASSERT_TRUE(modeling.tool_status().cut_preview_valid);
  ASSERT_TRUE(modeling.set_cut_mode(ModelingCutMode::KeepBoth).ok);
  const auto expect_tool_presentation = [&](const QImage &image) {
    for (const auto point : contour_points) {
      EXPECT_GE(yellow_pixels(image, harness.cutting_preview_pixel(point)), 3)
          << "Orange identifies the plane intersection, independently of the keep mode.";
    }
    const auto status = modeling.tool_status();
    ASSERT_EQ(status.cut_point_count, 2);
    for (int index = 0; index < status.cut_point_count; ++index) {
      EXPECT_GE(cyan_pixels(image, harness.cutting_preview_pixel(status.cut_points[index])), 3);
    }
    EXPECT_FALSE(harness.last_submission().selection_dimension_bounds.has_value());
  };
  const QImage both = capture("keep-both");
  expect_tool_presentation(both);
  for (const auto point : surface_points) EXPECT_GT(brightness(both, point), 170);
  for (const auto point : wire_points) {
    EXPECT_EQ(red_pixels(both, harness.cutting_preview_pixel(point)), 0);
  }
  // Probe the translucent plane outside the cube's original raster footprint.
  // These candidates follow the authored finite plane rather than its infinite support.
  bool visible_plane_fill = false;
  const auto overlay = modeling.volatile_overlay_snapshot();
  ASSERT_NE(overlay.frame, nullptr);
  for (const auto &triangle : overlay.frame->triangles) {
    if (triangle.layer != ViewportOverlayLayer::ToolPlane) continue;
    for (const auto weights : std::array{QVec3{0.7f, 0.15f, 0.15f},
             QVec3{0.15f, 0.7f, 0.15f}, QVec3{0.15f, 0.15f, 0.7f}}) {
      const auto point = triangle.a * weights.x + triangle.b * weights.y + triangle.c * weights.z;
      const auto probe = harness.cutting_preview_pixel(point);
      const QPoint pixel(int(probe.x), int(probe.y));
      if (!both.rect().contains(pixel)) continue;
      const auto original = activated.pixelColor(pixel);
      const auto color = both.pixelColor(pixel);
      visible_plane_fill = visible_plane_fill ||
          (std::max({original.red(), original.green(), original.blue()}) < 60 &&
           std::min({color.red() - original.red(), color.green() - original.green(),
                     color.blue() - original.blue()}) > 15 &&
           std::max({color.red(), color.green(), color.blue()}) < 200);
    }
  }
  EXPECT_TRUE(visible_plane_fill) << "The finite translucent Cut plane must remain visible.";
  std::array<bool, 2> first_retained{};
  for (const auto &[mode, phase] : std::array<std::pair<ModelingCutMode, const char *>, 2>{{
           {ModelingCutMode::DiscardLeft, "discard-left"},
           {ModelingCutMode::DiscardRight, "discard-right"}}}) {
    ASSERT_TRUE(modeling.set_cut_mode(mode).ok);
    const QImage image = capture(phase);
    expect_tool_presentation(image);
    const std::array retained{brightness(image, surface_points[0]) > 170,
                              brightness(image, surface_points[1]) > 170};
    EXPECT_NE(retained[0], retained[1]) << "One side remains a solid surface; the other becomes wire.";
    for (std::size_t index = 0; index < retained.size(); ++index) {
      const auto probe = harness.cutting_preview_pixel(wire_points[index]);
      if (retained[index]) {
        EXPECT_EQ(red_pixels(image, probe), 0);
      } else {
        EXPECT_GE(red_pixels(image, probe), 3);
      }
      if (mode == ModelingCutMode::DiscardRight) {
        EXPECT_NE(retained[index], first_retained[index]);
      }
    }
    if (mode == ModelingCutMode::DiscardLeft) first_retained = retained;
    const auto outline_builds = harness.tool_preview_outline_build_count();
    EXPECT_EQ(capture((std::string(phase) + "-retained").c_str()), image);
    EXPECT_EQ(harness.tool_preview_outline_build_count(), outline_builds);
  }
  ASSERT_TRUE(editor.tools().cancel_modal_tool().ok());
  EXPECT_EQ(capture("cancel"), source);
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
    ASSERT_GT(before.selected_object_count, 0u);
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
      EXPECT_EQ(current.selected_object_count, before.selected_object_count);
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
      EXPECT_GE(cyan_pixels(clear, probe), 3)
          << "Edit topology remains visible without tool hover.";
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
      EXPECT_GE(cyan_pixels(left, probe), 3);
      EXPECT_EQ(left.copy(probe_region), clear.copy(probe_region))
          << "Leaving preserves Edit topology independently of the retained cut preview.";
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
          << "Edit topology retains the same source-wire rendering across explicit re-entry.";
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
     VulkanSharedComponentFaceEdgesKeepOneActiveColorNearAndFar) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-shared-active-face-edge-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const QString output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!output.isEmpty()) ASSERT_TRUE(QDir().mkpath(output));
  auto settings = ViewportSettings::defaults();
  auto fill = settings.color(ViewportColorSettingId::SelectionFace);
  fill.a = 0.0f;
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionFace, fill));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionWire, {1.0f, 0.5f, 0.05f, 1.0f}));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::ActiveSelection, {1.0f, 0.3f, 0.7f, 1.0f}));
  constexpr std::array<float, 3u> kDistances{5.0f, 65.0f, 5.0f};
  for (const auto surface : {ViewportRenderSurface::Swapchain, ViewportRenderSurface::Texture}) {
    for (const bool partial_overlap : {false, true}) {
      SCOPED_TRACE(static_cast<int>(surface));
      SCOPED_TRACE(partial_overlap);
      SelectionOverlayRuntimeHarness harness;
      ASSERT_TRUE(harness.initialize(0.0f, false, surface));
      ASSERT_TRUE(harness.create_shared_face_edge_sample(partial_overlap));
      SelectionRuntimeFrame selected;
      selected.frame = harness.modeling().selection_overlay_render_frame();
      SelectionRuntimeFrame reference;
      reference.frame = selected.frame;
      std::erase_if(reference.frame.lines, [](const ViewportOverlayLine &line) {
        return line.style_role != ViewportOverlayStyleRole::ActiveSelectionWire;
      });
      ASSERT_EQ(reference.frame.lines.size(), 4u);
      SelectionRuntimeFrame neighbor_reference;
      neighbor_reference.frame = selected.frame;
      const auto on_shared_edge = [](QVec3 point) {
        const QVec3 direction{2.0f, 4.0f, -20.0f};
        const QVec3 offset = point - QVec3{-1.0f, -2.0f, 0.0f};
        const QVec3 residual = offset - direction * (dot(offset, direction) / dot(direction, direction));
        return dot(residual, residual) < 0.000001f;
      };
      std::erase_if(neighbor_reference.frame.lines, [&](const ViewportOverlayLine &line) {
        return line.style_role != ViewportOverlayStyleRole::SelectionWire ||
            !on_shared_edge(line.start) || !on_shared_edge(line.end);
      });
      ASSERT_EQ(neighbor_reference.frame.lines.size(), 1u);
      neighbor_reference.frame.lines.front().style_role = ViewportOverlayStyleRole::ActiveSelectionWire;
      SelectionRuntimeFrame backdrop;
      backdrop.frame = reference.frame;
      backdrop.frame.lines.clear();
      for (const float scale : {1.0f, 2.0f}) {
        SCOPED_TRACE(scale);
        std::array<QImage, kDistances.size()> actual;
        OverlayProxyUploadStats retained_uploads;
        std::uint64_t retained_builds = 0u;
        std::uint64_t retained_coincident_rebuilds = 0u;
        for (std::size_t sample = 0u; sample < kDistances.size(); ++sample) {
          SCOPED_TRACE(kDistances[sample]);
          ASSERT_TRUE(harness.prepare_overlay_audit_camera(kDistances[sample], ViewportProjection::Perspective));
          ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, scale, settings));
          actual[sample] = harness.capture();
          ASSERT_FALSE(actual[sample].isNull());
          EXPECT_EQ(harness.capture(), actual[sample]);
          const auto uploads = harness.component_overlay_stats().uploads;
          if (sample == 0u) {
            retained_uploads = uploads;
            retained_builds = harness.selection_frame_builds();
            retained_coincident_rebuilds = harness.component_overlay_stats().coincident_wire_rebuild_count;
          } else {
            EXPECT_EQ(uploads.geometry_creation_count, retained_uploads.geometry_creation_count);
            EXPECT_EQ(uploads.position_upload_count, retained_uploads.position_upload_count);
            EXPECT_EQ(uploads.index_upload_count, retained_uploads.index_upload_count);
            EXPECT_EQ(uploads.static_upload_count, retained_uploads.static_upload_count);
            EXPECT_EQ(harness.selection_frame_builds(), retained_builds);
            EXPECT_EQ(harness.component_overlay_stats().coincident_wire_rebuild_count, retained_coincident_rebuilds);
            EXPECT_FALSE(harness.last_submission().persistent_submitted);
          }
        }
        EXPECT_EQ(actual.front(), actual.back()) << "Returning the camera restores identical shared-edge pixels.";
        for (std::size_t sample = 0u; sample < 2u; ++sample) {
          SCOPED_TRACE(kDistances[sample]);
          ASSERT_TRUE(harness.prepare_overlay_audit_camera(kDistances[sample], ViewportProjection::Perspective));
          ASSERT_TRUE(harness.present_component_sample(
              ViewportOverlaySelectionContext::Face, scale, settings, false, &reference));
          const QImage active_only = harness.capture();
          ASSERT_TRUE(harness.present_component_sample(
              ViewportOverlaySelectionContext::Face, scale, settings, false, &neighbor_reference));
          const QImage neighbor_only = harness.capture();
          ASSERT_TRUE(harness.present_component_sample(
              ViewportOverlaySelectionContext::Face, scale, settings, false, &backdrop));
          const QImage background = harness.capture();
          ASSERT_FALSE(active_only.isNull());
          ASSERT_FALSE(neighbor_only.isNull());
          ASSERT_FALSE(background.isNull());
          // Each face can support a different half of the ribbon at an inward
          // corner. Render each support independently in pink, then retain the
          // stronger single-stroke coverage. Rendering both pink references
          // together would accept an incorrect double-alpha blend as the oracle.
          const QImage expected = strongest_single_wire_coverage(active_only, neighbor_only, background);
          const auto color_delta = [](QColor color, QColor base) {
            return std::max({std::abs(color.red() - base.red()),
                std::abs(color.green() - base.green()), std::abs(color.blue() - base.blue())});
          };
          const QVec2 start = harness.overlay_audit_image_point({-1.0f, -2.0f, 0.0f});
          const QVec2 end = harness.overlay_audit_image_point({1.0f, 2.0f, -20.0f});
          expect_native_wire_coverage(actual[sample], expected, background,
              start, end, scale, partial_overlap ? "partial-active-face" : "shared-active-face");
          const float length = std::hypot(end.x - start.x, end.y - start.y);
          const QVec2 direction{(end.x - start.x) / length, (end.y - start.y) / length};
          int orange_pixels = 0;
          for (int y = 0; y < expected.height(); ++y) {
            for (int x = 0; x < expected.width(); ++x) {
              const float px = x + 0.5f - start.x;
              const float py = y + 0.5f - start.y;
              const float along = px * direction.x + py * direction.y;
              const float across = -px * direction.y + py * direction.x;
              if (along < 8.0f || along > length - 8.0f || std::abs(across) > 3.0f * scale ||
                  color_delta(expected.pixelColor(x, y), background.pixelColor(x, y)) < 4) continue;
              const auto color = actual[sample].pixelColor(x, y);
              orange_pixels += color.green() > color.blue() + 4 && color.red() > color.blue() + 8 ? 1 : 0;
            }
          }
          EXPECT_EQ(orange_pixels, 0) << "Both depth-supported halves of the shared edge must use active pink.";
          int missing_samples = 0;
          for (float along = 8.0f; along < length - 8.0f; along += 1.0f) {
            bool pink = false;
            for (float across = -2.0f * scale; across <= 2.0f * scale; across += 0.5f) {
              const int x = static_cast<int>(std::lround(start.x + direction.x * along - direction.y * across));
              const int y = static_cast<int>(std::lround(start.y + direction.y * along + direction.x * across));
              const QColor color = actual[sample].pixelColor(x, y);
              pink = pink || (color.red() > color.green() + 45 && color.blue() > color.green() + 20);
            }
            missing_samples += pink ? 0 : 1;
          }
          EXPECT_EQ(missing_samples, 0) << "The active edge must remain continuous when the camera moves away.";
          if (surface == ViewportRenderSurface::Texture && !partial_overlap && sample == 0u && scale == 1.0f) {
            // Keep dimension-stream interaction in the self-contained fixture
            // as well as the opt-in real hallway regression above.
            ASSERT_TRUE(harness.present_component_sample(
                ViewportOverlaySelectionContext::Face, scale, settings, false, nullptr, true));
            ASSERT_TRUE(harness.last_submission().selection_dimension_bounds.has_value());
            const auto dimensions = harness.capture();
            ASSERT_FALSE(dimensions.isNull());
            EXPECT_NE(dimensions, actual[sample]) << "The separate dimension stream must be visible.";
            expect_native_wire_coverage(dimensions, expected, background,
                start, end, scale, "shared-active-face-with-dimensions");
          }
          if (!output.isEmpty()) {
            ASSERT_TRUE(actual[sample].save(QDir(output).filePath(
                QStringLiteral("shared_face_edge_%1_%2_%3_%4.png")
                    .arg(static_cast<int>(surface)).arg(partial_overlap ? "partial" : "full")
                    .arg(static_cast<int>(scale * 100.0f)).arg(sample == 0u ? "near" : "far"))));
            ASSERT_TRUE(expected.save(QDir(output).filePath(
                QStringLiteral("shared_face_edge_expected_%1_%2_%3_%4.png")
                    .arg(static_cast<int>(surface)).arg(partial_overlap ? "partial" : "full")
                    .arg(static_cast<int>(scale * 100.0f)).arg(sample == 0u ? "near" : "far"))));
          }
        }
      }
      if (surface == ViewportRenderSurface::Texture && !partial_overlap) {
        ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, ViewportProjection::Perspective));
        const QVec2 start = harness.overlay_audit_image_point({-1.0f, -2.0f, 0.0f});
        const QVec2 end = harness.overlay_audit_image_point({1.0f, 2.0f, -20.0f});
        for (const auto &[ordinary_width, active_width] :
             std::array<std::pair<float, float>, 2u>{{{3.0f, 0.75f}, {0.75f, 3.0f}}}) {
          SCOPED_TRACE(ordinary_width);
          SCOPED_TRACE(active_width);
          auto widths = settings;
          ASSERT_TRUE(widths.set_float_value(ViewportFloatSettingId::SelectionFaceWireLineSize, ordinary_width));
          ASSERT_TRUE(widths.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, active_width));
          ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, 1.0f, widths));
          const QImage actual_width = harness.capture();
          ASSERT_TRUE(harness.present_component_sample(
              ViewportOverlaySelectionContext::Face, 1.0f, widths, false, &reference));
          const QImage active_width_image = harness.capture();
          ASSERT_TRUE(harness.present_component_sample(
              ViewportOverlaySelectionContext::Face, 1.0f, widths, false, &neighbor_reference));
          const QImage neighbor_width_image = harness.capture();
          ASSERT_TRUE(harness.present_component_sample(
              ViewportOverlaySelectionContext::Face, 1.0f, widths, false, &backdrop));
          const QImage width_background = harness.capture();
          ASSERT_FALSE(actual_width.isNull());
          ASSERT_FALSE(active_width_image.isNull());
          ASSERT_FALSE(neighbor_width_image.isNull());
          ASSERT_FALSE(width_background.isNull());
          expect_native_wire_coverage(actual_width,
              strongest_single_wire_coverage(active_width_image, neighbor_width_image, width_background),
              width_background, start, end, 1.0f, "unequal-active-face-width");
          if (!output.isEmpty()) {
            ASSERT_TRUE(actual_width.save(QDir(output).filePath(
                QStringLiteral("shared_face_edge_width_%1_%2.png").arg(ordinary_width).arg(active_width))));
          }
        }
        auto transparent = settings;
        auto active_color = transparent.color(ViewportColorSettingId::ActiveSelection);
        active_color.a = 0.0f;
        ASSERT_TRUE(transparent.set_color(ViewportColorSettingId::ActiveSelection, active_color));
        ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, 1.0f, transparent));
        const QImage transparent_active = harness.capture();
        SelectionRuntimeFrame ordinary_only;
        ordinary_only.frame = selected.frame;
        std::erase_if(ordinary_only.frame.lines, [](const ViewportOverlayLine &line) {
          return line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire;
        });
        ASSERT_TRUE(harness.present_component_sample(
            ViewportOverlaySelectionContext::Face, 1.0f, transparent, false, &ordinary_only));
        EXPECT_EQ(transparent_active, harness.capture())
            << "A transparent active edge must neither tint nor mask the ordinary selection.";
      }
      // Priority must never promote a genuinely hidden edge over scene depth.
      ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, ViewportProjection::Perspective));
      ASSERT_TRUE(harness.create_shared_face_edge_occluder());
      ASSERT_TRUE(harness.present_component_sample(
          ViewportOverlaySelectionContext::Face, 1.0f, settings, false, &selected));
      const QImage occluded = harness.capture();
      ASSERT_TRUE(harness.present_component_sample(
          ViewportOverlaySelectionContext::Face, 1.0f, settings, false, &backdrop));
      const QImage occluded_background = harness.capture();
      ASSERT_FALSE(occluded.isNull());
      ASSERT_FALSE(occluded_background.isNull());
      EXPECT_EQ(occluded.copy(317, 236, 7, 7), occluded_background.copy(317, 236, 7, 7));
    }
  }
}

TEST(SelectionOverlayRuntimeTests,
     VulkanSharedComponentFaceEdgePriorityFollowsExtrusionPreviewAndCancel) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-shared-face-edge-preview-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const QString output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!output.isEmpty()) ASSERT_TRUE(QDir().mkpath(output));
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false, ViewportRenderSurface::Texture));
  ASSERT_TRUE(harness.create_shared_face_edge_sample(false));
  ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, ViewportProjection::Perspective));
  auto settings = ViewportSettings::defaults();
  auto fill = settings.color(ViewportColorSettingId::SelectionFace);
  fill.a = 0.0f;
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionFace, fill));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionWire, {1.0f, 0.5f, 0.05f, 1.0f}));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::ActiveSelection, {1.0f, 0.3f, 0.7f, 1.0f}));
  ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, 2.0f, settings, true));
  const QImage joined = harness.capture();
  ASSERT_FALSE(joined.isNull());
  const auto initial_undo = harness.undo_count();
  ASSERT_TRUE(harness.extrusion().activate(modeling::FaceExtrudeMode::Individual).ok);
  ASSERT_TRUE(harness.extrusion().begin_drag().ok);
  for (const double amount : {-0.35, -0.8}) {
    SCOPED_TRACE(amount);
    ASSERT_TRUE(harness.extrusion().update_drag_world_amount(amount).ok);
    ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, 2.0f, settings, true));
    const QImage separated = harness.capture();
    ASSERT_FALSE(separated.isNull());
    EXPECT_NE(separated, joined);
    const auto rebuilds = harness.component_overlay_stats().coincident_wire_rebuild_count;
    ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, 2.0f, settings, true));
    EXPECT_EQ(harness.component_overlay_stats().coincident_wire_rebuild_count, rebuilds);
    EXPECT_EQ(harness.capture(), separated);

    // Individual extrusion moves these two caps along different face normals.
    // The former shared ordinary edge must regain its own orange appearance;
    // retaining a supplemental active ribbon would incorrectly follow it pink.
    std::optional<ViewportOverlayLine> ordinary_edge;
    float nearest_distance = 100.0f;
    for (const auto &line : harness.modeling().selection_overlay_frame().lines) {
      if (line.style_role != ViewportOverlayStyleRole::SelectionWire ||
          line.layer != ViewportOverlayLayer::SelectedFaceEdge) continue;
      const QVec3 direction = line.end - line.start;
      const QVec3 original_direction{2.0f, 4.0f, -20.0f};
      const QVec3 residual = direction - original_direction *
          (dot(direction, original_direction) / dot(original_direction, original_direction));
      if (dot(residual, residual) > 0.000001f) continue;
      const QVec3 offset = (line.start + line.end) * 0.5f - QVec3{0.0f, 0.0f, -10.0f};
      const float distance = dot(offset, offset);
      if (distance < nearest_distance) {
        nearest_distance = distance;
        ordinary_edge = line;
      }
    }
    ASSERT_TRUE(ordinary_edge.has_value());
    SelectionRuntimeFrame ordinary_reference;
    ordinary_reference.frame = harness.modeling().selection_overlay_render_frame();
    const auto is_ordinary_edge = [&](const ViewportOverlayLine &line) {
      return line.layer == ordinary_edge->layer &&
          line.element.object_id == ordinary_edge->element.object_id &&
          line.element.edge_a == ordinary_edge->element.edge_a &&
          line.element.edge_b == ordinary_edge->element.edge_b;
    };
    // Preserve cyan source wires and ordinary joins behind the target stroke's
    // feather. An isolated orange line on black is not a valid reference where
    // another legitimate source edge crosses it in projection.
    std::erase_if(ordinary_reference.frame.lines, [](const ViewportOverlayLine &line) {
      return line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire;
    });
    ASSERT_EQ(std::count_if(ordinary_reference.frame.lines.begin(),
        ordinary_reference.frame.lines.end(), is_ordinary_edge), 1);
    ASSERT_TRUE(harness.present_component_sample(
        ViewportOverlaySelectionContext::Face, 2.0f, settings, true, &ordinary_reference));
    const QImage orange_only = harness.capture();
    SelectionRuntimeFrame backdrop;
    backdrop.frame = ordinary_reference.frame;
    std::erase_if(backdrop.frame.lines, is_ordinary_edge);
    ASSERT_TRUE(harness.present_component_sample(
        ViewportOverlaySelectionContext::Face, 2.0f, settings, true, &backdrop));
    const QImage background = harness.capture();
    if (!output.isEmpty()) {
      const auto amount_label = static_cast<int>(std::abs(amount) * 100.0);
      ASSERT_TRUE(separated.save(QDir(output).filePath(
          QStringLiteral("shared_face_edge_preview_%1_actual.png").arg(amount_label))));
      ASSERT_TRUE(orange_only.save(QDir(output).filePath(
          QStringLiteral("shared_face_edge_preview_%1_reference.png").arg(amount_label))));
      ASSERT_TRUE(background.save(QDir(output).filePath(
          QStringLiteral("shared_face_edge_preview_%1_background.png").arg(amount_label))));
    }
    expect_native_wire_coverage(separated, orange_only, background,
        harness.overlay_audit_image_point(ordinary_edge->start),
        harness.overlay_audit_image_point(ordinary_edge->end), 2.0f, "separated-ordinary-face-preview");
  }
  ASSERT_TRUE(harness.extrusion().update_drag_world_amount(0.0).ok);
  ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, 2.0f, settings, true));
  EXPECT_EQ(harness.capture(), joined) << "Returning preview caps to coincidence restores the complete pink seam.";
  ASSERT_TRUE(harness.extrusion().cancel_drag().ok);
  ASSERT_TRUE(harness.extrusion().cancel().ok);
  ASSERT_TRUE(harness.present_component_sample(ViewportOverlaySelectionContext::Face, 2.0f, settings, true));
  EXPECT_EQ(harness.capture(), joined) << "Cancel must restore both shared-edge appearance and original source geometry.";
  EXPECT_EQ(harness.undo_count(), initial_undo);
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
  const std::array<FaceFillCase, 9> cases{{
      {"clear", {3}, 3, false, std::nullopt},
      {"ordinary_single", {1, 3}, 3, false, std::nullopt},
      {"ordinary_overlap", {1, 2, 3}, 3, false, 1u},
      {"active_single", {1, 3}, 1, false, std::nullopt},
      {"active_overlap", {1, 2}, 1, false, 3u},
      {"singleton", {1}, 1, false, std::nullopt},
      {"hover_single", {1, 3}, 3, true, std::nullopt},
      {"hover_overlap", {1, 2, 3}, 3, true, 6u},
      {"hover_active_overlap", {1, 2}, 1, true, 6u},
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
      std::array<QImage, 9> at_100_percent;
      for (const auto &[scale, scale_label] : kScales) {
        SCOPED_TRACE(scale_label);
        if (texture_surface) {
          harness.resize_face_fill_sample(
              static_cast<std::uint32_t>(SelectionOverlayRuntimeHarness::kWidth * scale),
              static_cast<std::uint32_t>(SelectionOverlayRuntimeHarness::kHeight * scale));
        }
        const auto tint_pixel = harness.overlapping_face_fill_probe_pixel(false);
        const auto occluded_pixel = harness.overlapping_face_fill_probe_pixel(true);
        std::array<QImage, 9> samples;
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
        EXPECT_EQ(patch(samples[5], tint_pixel), patch(samples[1], tint_pixel))
            << "A singleton face uses the ordinary authored-alpha selection fill.";
        EXPECT_NE(patch(samples[5], tint_pixel), patch(samples[3], tint_pixel))
            << "A second semantic selection enables the distinct primary fill.";
        EXPECT_NE(patch(samples[6], tint_pixel), patch(samples[1], tint_pixel))
            << "The hover reference must retain its own fill color and alpha.";
        if (!custom_alpha && scale == 1.0f && default_active_overlap.isNull()) {
          default_active_overlap = samples[4];
          default_hover_overlap = samples[8];
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
     VulkanSharedExtrusionHandleUsesEveryPaneLayerAndRetainsPreviewUpdates) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-shared-extrusion-pane-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  QString directory = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (directory.isEmpty()) {
    directory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(directory));
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(0.0f, false));
  ASSERT_TRUE(harness.create_extrusion_sample(modeling::FaceExtrudeMode::Region));
  harness.initialize_extrusion_panes();
  const auto initial_undo = harness.undo_count();

  for (const float scale : {1.0f, 2.0f}) {
    SCOPED_TRACE(scale);
    std::array<QImage, 4> initial_images;
    const auto capture_panes = [&](const char *phase, bool remember_initial) {
      QImage first;
      for (std::size_t pane = 0u; pane < initial_images.size(); ++pane) {
        SCOPED_TRACE(pane);
        // Only pane zero owns input. All four layer slots borrow the same
        // operation, including its accepted preview position.
        ASSERT_TRUE(harness.present_extrusion_pane(pane, pane == 0u, true, scale));
        const auto pixel = harness.extrusion_handle_pixel(pane, scale);
        ASSERT_TRUE(pixel.has_value());
        const QImage image = harness.capture(false);
        ASSERT_FALSE(image.isNull());
        ASSERT_GE(pixel->x, 4.0f);
        ASSERT_LT(pixel->x, image.width() - 4.0f);
        ASSERT_GE(pixel->y, 4.0f);
        ASSERT_LT(pixel->y, image.height() - 4.0f);
        EXPECT_GE(yellow_pixels(image, *pixel), 35)
            << "The filled Extrude disk must reach every pane's GPU surface.";
        if (pane == 0u) first = image;
        else EXPECT_EQ(image, first) << "Passive panes must have the same tool presentation.";
        if (remember_initial) initial_images[pane] = image;
        const auto builds = harness.tool_frame_builds();
        const auto selection_builds = harness.selection_frame_builds();
        ASSERT_TRUE(harness.present_extrusion_pane(pane, pane == 0u, true, scale));
        EXPECT_EQ(harness.tool_frame_builds(), builds);
        EXPECT_EQ(harness.selection_frame_builds(), selection_builds);
        EXPECT_FALSE(harness.last_submission().dynamic_submitted);
        EXPECT_FALSE(harness.last_submission().persistent_submitted);
        EXPECT_EQ(harness.capture(false), image);
        const QString path = QDir(directory).filePath(
            QStringLiteral("shared_extrude_%1_pane_%2_%3.png")
                .arg(static_cast<int>(scale * 100.0f)).arg(pane)
                .arg(QString::fromLatin1(phase)));
        EXPECT_TRUE(image.save(path));
        std::cout << "Shared extrusion GPU png=" << path.toStdString() << '\n';
      }
    };
    ASSERT_NO_FATAL_FAILURE(capture_panes("initial", true));
    ASSERT_TRUE(harness.extrusion().begin_drag().ok);
    ASSERT_TRUE(harness.extrusion().update_drag_world_amount(0.35).ok);
    ASSERT_NO_FATAL_FAILURE(capture_panes("preview_first", false));
    ASSERT_TRUE(harness.extrusion().update_drag_world_amount(0.9).ok);
    ASSERT_NO_FATAL_FAILURE(capture_panes("preview_moved", false));
    ASSERT_TRUE(harness.extrusion().update_drag_world_amount(0.0).ok);
    ASSERT_NO_FATAL_FAILURE(capture_panes("zero", false));
    ASSERT_TRUE(harness.extrusion().cancel_drag().ok);
    ASSERT_NO_FATAL_FAILURE(capture_panes("canceled", false));
    EXPECT_EQ(harness.undo_count(), initial_undo);

    // A dock-sized resize invalidates this pane's retained projection even
    // when the operation's revision does not change.
    constexpr std::size_t kResizedPane = 2u;
    harness.resize_extrusion_pane(kResizedPane, 520u, 430u);
    ASSERT_TRUE(harness.present_extrusion_pane(kResizedPane, false, false, scale));
    const QImage hidden = harness.capture(false);
    ASSERT_FALSE(hidden.isNull());
    ASSERT_TRUE(harness.present_extrusion_pane(kResizedPane, false, true, scale));
    const auto resized_pixel = harness.extrusion_handle_pixel(kResizedPane, scale);
    ASSERT_TRUE(resized_pixel.has_value());
    const QImage resized = harness.capture(false);
    ASSERT_FALSE(resized.isNull());
    EXPECT_NE(resized, hidden);
    EXPECT_GE(yellow_pixels(resized, *resized_pixel), 35);
    ASSERT_TRUE(harness.present_extrusion_pane(kResizedPane, false, false, scale));
    EXPECT_EQ(harness.capture(false), hidden) << "Hiding retires the entire retained handle.";
    harness.resize_extrusion_pane(kResizedPane,
        SelectionOverlayRuntimeHarness::kWidth, SelectionOverlayRuntimeHarness::kHeight);
    ASSERT_TRUE(harness.present_extrusion_pane(kResizedPane, false, true, scale));
    EXPECT_EQ(harness.capture(false), initial_images[kResizedPane]);
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

TEST(SelectionOverlayRuntimeTests,
     VulkanActiveOutlinePreferencesChangeOnlyPrimaryPixelsAndRetainGeometry) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 for production Vulkan captures.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-active-outline-preference-test";
  char *argv[] = {name, nullptr};
  QCoreApplication application(argc, argv);
  const auto changed_pixels = [](const QImage &image, const QImage &base, QRect region) {
    int count = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
      for (int x = region.left(); x <= region.right(); ++x) {
        if (image.pixelColor(x, y) != base.pixelColor(x, y)) ++count;
      }
    }
    return count;
  };
  const QString output = qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (!output.isEmpty()) ASSERT_TRUE(QDir().mkpath(output));
  for (const auto surface : {ViewportRenderSurface::Swapchain, ViewportRenderSurface::Texture}) {
    SelectionOverlayRuntimeHarness harness;
    ASSERT_TRUE(harness.initialize(-0.02f, true, surface));
    ASSERT_TRUE(harness.prepare_overlay_audit_camera(5.0f, ViewportProjection::Orthographic));
    RenderOverlaySelectedObjectSnapshot snapshot;
    snapshot.selection_revision = 1u;
    snapshot.selected_mesh_outline_only = true;
    ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot));
    const QImage background = harness.capture(false);
    ASSERT_FALSE(background.isNull());
    std::array<std::array<MeshVertex, 4u>, 2u> vertices;
    const std::array<std::uint32_t, 6u> indices{0u, 1u, 2u, 0u, 2u, 3u};
    for (std::size_t index = 0u; index < vertices.size(); ++index) {
      const float left = index == 0u ? 70.0f : 400.0f;
      const std::array<QVec2, 4u> corners{{{left, 120.0f}, {left + 100.0f, 120.0f},
                                         {left + 100.0f, 240.0f}, {left, 240.0f}}};
      for (std::size_t corner = 0u; corner < corners.size(); ++corner) {
        vertices[index][corner].position = harness.overlay_audit_world_point(corners[corner], 0.1f);
      }
      snapshot.meshes.push_back({.entity = static_cast<EntityId>(index + 1u),
          .mesh_id = static_cast<AssetId>(index + 1u), .mesh_revision = 1u,
          .selected = true, .active_selected = index == 1u,
          .vertices = vertices[index], .indices = indices});
    }
    ++snapshot.selection_revision;
    for (const float scale : {1.0f, 2.0f, 1.0f}) {
      ViewportSettings narrow = ViewportSettings::defaults();
      ASSERT_TRUE(narrow.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, 1.0f));
      ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot, scale, true, true, &narrow));
      const QImage narrow_image = harness.capture(false);
      const auto work = harness.mesh_outline_work();
      ViewportSettings wide = narrow;
      ASSERT_TRUE(wide.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, 7.0f));
      ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot, scale, true, true, &wide));
      const QImage wide_image = harness.capture(false);
      const QRect ordinary_region(58, 155, 25, 50);
      const QRect active_region(382, 155, 37, 50);
      EXPECT_EQ(narrow_image.copy(ordinary_region), wide_image.copy(ordinary_region));
      EXPECT_GT(changed_pixels(wide_image, background, active_region),
                changed_pixels(narrow_image, background, active_region));
      EXPECT_EQ(harness.mesh_outline_work().geometry_builds, work.geometry_builds);
      EXPECT_EQ(harness.mesh_outline_work().uploaded_vertices, work.uploaded_vertices);
      ASSERT_TRUE(wide.set_color(ViewportColorSettingId::ActiveSelection, {0.0f, 1.0f, 1.0f, 1.0f}));
      ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot, scale, true, true, &wide));
      const QImage cyan_image = harness.capture(false);
      EXPECT_EQ(wide_image.copy(ordinary_region), cyan_image.copy(ordinary_region));
      EXPECT_NE(wide_image.copy(active_region), cyan_image.copy(active_region));
      EXPECT_GT(cyan_image.pixelColor(400, 180).green(), cyan_image.pixelColor(400, 180).red());
      EXPECT_EQ(harness.mesh_outline_work().geometry_builds, work.geometry_builds);
      EXPECT_EQ(harness.mesh_outline_work().uploaded_vertices, work.uploaded_vertices);
      ASSERT_TRUE(harness.present_mesh_outline_snapshot(snapshot, scale, true, true, &wide));
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_FALSE(harness.last_submission().dynamic_submitted);
      EXPECT_EQ(harness.capture(false), cyan_image);
      if (!output.isEmpty()) {
        ASSERT_TRUE(wide_image.save(QDir(output).filePath(QStringLiteral("active-width-%1-%2.png")
            .arg(static_cast<int>(surface)).arg(static_cast<int>(100.0f * scale)))));
        ASSERT_TRUE(cyan_image.save(QDir(output).filePath(QStringLiteral("active-custom-color-%1-%2.png")
            .arg(static_cast<int>(surface)).arg(static_cast<int>(100.0f * scale)))));
      }
    }
  }
}

TEST(SelectionOverlayRuntimeTests, VulkanActiveObjectsGroupsAndInstancesSwitchPinkAt100And200Percent) {
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
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 0u, scale, true, nullptr, true));
      const QImage singleton = harness.capture();
      ASSERT_FALSE(singleton.isNull());
      ASSERT_TRUE(singleton.save(QDir(directory).filePath(stem + QStringLiteral("-singleton.png"))));
      ViewportSettings singleton_custom = ViewportSettings::defaults();
      ASSERT_TRUE(singleton_custom.set_color(ViewportColorSettingId::ActiveSelection,
                                             {0.0f, 1.0f, 1.0f, 1.0f}));
      ASSERT_TRUE(singleton_custom.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, 7.0f));
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 0u, scale, true, &singleton_custom, true));
      EXPECT_EQ(harness.capture(), singleton)
          << "One selected object, group or occurrence keeps ordinary color and width.";
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 0u, scale));
      const QImage left_active = harness.capture();
      ASSERT_FALSE(left_active.isNull());
      ASSERT_TRUE(left_active.save(QDir(directory).filePath(stem + QStringLiteral("-left.png"))));
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 1u, scale));
      const QImage right_active = harness.capture();
      ASSERT_FALSE(right_active.isNull());
      ASSERT_TRUE(right_active.save(QDir(directory).filePath(stem + QStringLiteral("-right.png"))));
      std::array<long long, 2> pink_delta{};
      std::array<int, 2> changed{};
      for (int y = 0; y < left_active.height(); ++y) {
        for (int x = 0; x < left_active.width(); ++x) {
          const QColor before = left_active.pixelColor(x, y);
          const QColor after = right_active.pixelColor(x, y);
          const std::size_t side = x < left_active.width() / 2 ? 0u : 1u;
          if (before != after) ++changed[side];
          pink_delta[side] += after.blue() - after.green() -
                              before.blue() + before.green();
        }
      }
      EXPECT_GT(changed[0], 8);
      EXPECT_GT(changed[1], 8);
      EXPECT_LT(pink_delta[0], 0) << "The previous active boundary must lose its pink tint.";
      EXPECT_GT(pink_delta[1], 0) << "The new active boundary must gain its pink tint.";
      long long singleton_to_active_pink_delta = 0;
      for (int y = 0; y < left_active.height(); ++y) {
        for (int x = 0; x < left_active.width() / 2; ++x) {
          const QColor before = singleton.pixelColor(x, y);
          const QColor after = left_active.pixelColor(x, y);
          singleton_to_active_pink_delta += after.blue() - after.green() -
                                             before.blue() + before.green();
        }
      }
      EXPECT_GT(singleton_to_active_pink_delta, 0)
          << "Adding another semantic selection enables the primary pink appearance.";
      if (instances) {
        const auto pixels = harness.active_face_pixels();
        const auto face_tint = [](const QImage &image, QVec2 pixel) {
          int total = 0;
          for (int y = int(pixel.y) - 2; y <= int(pixel.y) + 2; ++y) {
            for (int x = int(pixel.x) - 2; x <= int(pixel.x) + 2; ++x) {
              const QColor color = image.pixelColor(x, y);
              total += color.blue() - color.green();
            }
          }
          return total;
        };
        for (const auto pixel : pixels) {
          EXPECT_NE(face_tint(left_active, pixel), face_tint(unselected, pixel))
              << "Selected instance faces must contain an actual fill away from their outlines.";
          EXPECT_NE(face_tint(right_active, pixel), face_tint(unselected, pixel));
        }
        EXPECT_GT(face_tint(left_active, pixels[0]), face_tint(right_active, pixels[0]))
            << "The previous active instance face interior must lose its pink tint.";
        EXPECT_LT(face_tint(left_active, pixels[1]), face_tint(right_active, pixels[1]))
            << "The new active instance face interior must gain its pink tint.";
      }
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 1u, scale));
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_FALSE(harness.last_submission().dynamic_submitted);
      EXPECT_EQ(harness.capture(), right_active);
      ViewportSettings custom = ViewportSettings::defaults();
      ASSERT_TRUE(custom.set_color(ViewportColorSettingId::ActiveSelection,
                                  {0.0f, 1.0f, 1.0f, 1.0f}));
      ASSERT_TRUE(custom.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, 7.0f));
      const auto retained_work = harness.mesh_outline_work();
      const auto retained_renderables = harness.renderable_count();
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 1u, scale, true, &custom));
      const QImage customized = harness.capture();
      EXPECT_NE(customized, right_active);
      EXPECT_EQ(harness.mesh_outline_work().geometry_builds, retained_work.geometry_builds);
      EXPECT_EQ(harness.mesh_outline_work().uploaded_vertices, retained_work.uploaded_vertices);
      EXPECT_EQ(harness.renderable_count(), retained_renderables);
      if (instances) {
        const auto pixels = harness.active_face_pixels();
        const auto patch = [](const QImage &image, QVec2 pixel) {
          return image.copy(QRect(int(pixel.x) - 2, int(pixel.y) - 2, 5, 5));
        };
        EXPECT_EQ(patch(customized, pixels[0]), patch(right_active, pixels[0]));
        EXPECT_NE(patch(customized, pixels[1]), patch(right_active, pixels[1]));
        ASSERT_TRUE(custom.set_color(ViewportColorSettingId::ActiveSelection,
                                    {0.0f, 1.0f, 1.0f, 0.0f}));
        ASSERT_TRUE(harness.present_active_selection_sample(groups, 1u, scale, true, &custom));
        const QImage transparent = harness.capture();
        EXPECT_EQ(patch(transparent, pixels[0]), patch(right_active, pixels[0]));
        EXPECT_EQ(patch(transparent, pixels[1]), patch(unselected, pixels[1]));
      }
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 1u, scale));
      EXPECT_EQ(harness.capture(), right_active);
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 0u, scale, true, nullptr, true));
      EXPECT_EQ(harness.capture(), singleton);
      ASSERT_TRUE(harness.present_active_selection_sample(groups, 0u, scale, true, nullptr, true));
      EXPECT_FALSE(harness.last_submission().persistent_submitted);
      EXPECT_FALSE(harness.last_submission().dynamic_submitted);
      EXPECT_EQ(harness.capture(), singleton);
      std::cout << "Active selection GPU " << stem.toStdString() << " changed="
                << changed[0] << ',' << changed[1] << " pink_delta="
                << pink_delta[0] << ',' << pink_delta[1] << '\n';
    }
  }
}

} // namespace
} // namespace quader
