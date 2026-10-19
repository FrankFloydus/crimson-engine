////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/crimson/crimson_render_host.hpp"
#include "render/crimson/crimson_editor_projection.hpp"

#include "core/color_space.hpp"
#include "core/logger.hpp"
#include "render/instance_edit_style.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/viewport_grid_placement_policy.hpp"
#include "viewport/viewport_settings_defaults.hpp"

#include <crimson/renderer/Scene.hpp>
#include <crimson/renderer/BorrowedVulkan.hpp>
#include <crimson/renderer/ShaderFamily.hpp>
#include <crimson/renderer/BuildIdentity.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

struct CrimsonViewState;

struct CrimsonSurfaceState final {
  crimson::shared::TargetId target;
  RenderVulkanCompositionCapacity capacity;
  crimson::shared::Submission last_submission;
  bool initial = true;
  bool rendered = false;
};

struct CrimsonHostState final {
  crimson::shared::Renderer renderer;
  std::unique_ptr<CrimsonSceneProjectionCache> projection_cache;
  CrimsonEditorProjection editor;
  std::shared_ptr<const RenderSceneSnapshot> source;
  std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview;
  std::shared_ptr<const CrimsonSceneProjection> pending;
  std::shared_ptr<const CrimsonSceneProjection> accepted;
  RenderToolPreviewSuppressionSnapshot accepted_suppression;
  std::vector<std::weak_ptr<CrimsonViewState>> views;
  std::vector<std::weak_ptr<CrimsonSurfaceState>> surfaces;
  std::uint64_t next_generation = 1u;
  std::uint64_t preview_submission_revision = 0u;
  std::uint64_t frame_submissions = 0u;
  std::uint64_t accepted_editor_scene_generation = 0u;
  std::uint64_t preparation_epoch = 0u;
  std::uint64_t submitted_epoch = 0u;
  std::uint32_t preparation_depth = 0u;
  bool scene_dirty = true;
  bool candidate_rejected = false;
  bool initialized = false;
  bool vsync = true;
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  std::filesystem::path resources;
  std::string error;
  RenderFramePerformanceStats performance;
  RenderPresentationFrameTracker presentation;
  std::size_t copy_count = 0u;
  bool presentation_open = false;

  bool accept(crimson::shared::Status status) {
    if (status.ok()) return true;
    error = std::string(status.error_message());
    LOG_ERROR("Crimson renderer: {}", error);
    return false;
  }

  bool prepare_scene() {
    if (!initialized || !projection_cache) return false;
    if (!scene_dirty) return !candidate_rejected;
    if (!source) source = std::make_shared<const RenderSceneSnapshot>();
    std::shared_ptr<const CrimsonSceneProjection> candidate;
    std::string diagnostic;
    if (!projection_cache->prepare(source, preview, next_generation, candidate, diagnostic)) {
      error = std::move(diagnostic);
      LOG_ERROR("Crimson scene candidate rejected: {}", error);
      scene_dirty = false;
      candidate_rejected = true;
      return false;
    }
    if (!accept(renderer.stage_scene(candidate->candidate()))) {
      scene_dirty = false;
      candidate_rejected = true;
      return false;
    }
    ++next_generation;
    pending = std::move(candidate);
    editor.set_preview(pending->preview);
    scene_dirty = false;
    candidate_rejected = false;
    error.clear();
    return true;
  }

  void acknowledge_scene() {
    if (!pending || renderer.retention_stats().accepted_generation != pending->generation) return;
    const bool preview_changed = !accepted || accepted->preview != pending->preview;
    accepted = std::move(pending);
    if (preview_changed) {
      accepted_suppression = accepted->preview
                                ? accepted->preview->suppression
                                : RenderToolPreviewSuppressionSnapshot{};
      ++preview_submission_revision;
    }
  }
};

struct CrimsonViewState final {
  std::weak_ptr<CrimsonSurfaceState> surface;
  crimson::shared::ViewFrame frame;
  std::shared_ptr<const crimson::shared::EditorFrame> accepted_editor;
  ViewportCameraState camera;
  ViewportCameraPose pose{{0.0f, 0.0f, 10.0f}, {}, {0.0f, 1.0f, 0.0f}};
  std::uint32_t width = 1u;
  std::uint32_t height = 1u;
  std::uint32_t backing_width = 1u;
  std::uint32_t backing_height = 1u;
  std::uint32_t crop_left = 0u;
  std::uint32_t crop_top = 0u;
  std::uint64_t camera_revision = 1u;
  std::uint64_t settings_apply_count = 0u;
  std::uint8_t layer_mask = 0u;
  std::uint8_t scene_layer_mask = 0u;
  bool active = false;
  bool dirty = true;
  bool scene_visible = true;
  bool overlay_visible = true;
  bool solid = false;
  bool instance_context = false;
  bool configured_settings = false;
  bool unsupported_presentation_reported = false;
  InstanceEditStyle instance_style;
  std::shared_ptr<const crimson::shared::EditorGrid> grid;
};

void update_projection(CrimsonViewState &view) {
  const auto forward = normalize(view.pose.target - view.pose.eye);
  const auto right = normalize(cross(forward, view.pose.up));
  const auto up = cross(right, forward);
  auto &matrix = view.frame.view_from_world.values;
  matrix = {right.x, up.x, -forward.x, 0.0f,
      right.y, up.y, -forward.y, 0.0f,
      right.z, up.z, -forward.z, 0.0f,
      -dot(right, view.pose.eye), -dot(up, view.pose.eye), dot(forward, view.pose.eye), 1.0f};
  const float aspect = static_cast<float>(view.width) / static_cast<float>(view.height);
  const float near_plane = view.camera.near_plane;
  const float far_plane = view.camera.far_plane;
  auto &projection = view.frame.clip_from_view.values;
  projection.fill(0.0f);
  if (view.camera.projection == ViewportProjection::Orthographic) {
    const float half_height = std::max(0.0001f, view.camera.orthographic_size * 0.5f);
    projection[0] = 1.0f / (half_height * aspect);
    projection[5] = 1.0f / half_height;
    projection[10] = 1.0f / (far_plane - near_plane);
    projection[14] = far_plane / (far_plane - near_plane);
    projection[15] = 1.0f;
    view.frame.projection = crimson::shared::ProjectionKind::Orthographic;
  } else {
    const float cotangent = 1.0f / std::tan(view.camera.fov_degrees * std::numbers::pi_v<float> / 360.0f);
    projection[0] = cotangent / aspect;
    projection[5] = cotangent;
    projection[10] = near_plane / (far_plane - near_plane);
    projection[11] = -1.0f;
    projection[14] = near_plane * far_plane / (far_plane - near_plane);
    view.frame.projection = crimson::shared::ProjectionKind::Perspective;
  }
  const float scale_x = static_cast<float>(view.width) / view.backing_width;
  const float scale_y = static_cast<float>(view.height) / view.backing_height;
  const float shift_x = (2.0f * view.crop_left + view.width) / view.backing_width - 1.0f;
  const float shift_y = 1.0f - (2.0f * view.crop_top + view.height) / view.backing_height;
  for (std::size_t column = 0u; column < 4u; ++column) {
    projection[column * 4u] = projection[column * 4u] * scale_x + projection[column * 4u + 3u] * shift_x;
    projection[column * 4u + 1u] = projection[column * 4u + 1u] * scale_y + projection[column * 4u + 3u] * shift_y;
  }
  view.frame.eye_world = {view.pose.eye.x, view.pose.eye.y, view.pose.eye.z};
  view.frame.forward_world = {forward.x, forward.y, forward.z};
  view.frame.depth = crimson::shared::DepthConvention::Reverse;
  view.frame.camera_revision = view.camera_revision;
}

class CrimsonSelectionDepth final : public RenderSelectionDepth {
public:
  bool wants_geometry(const ViewportSelectionDepthCamera &, bool) override { return false; }
  void prepare(std::shared_ptr<const ViewportSelectionDepthGeometry>, const ViewportSelectionDepthCamera &) override {}
  std::shared_ptr<const ViewportSelectionDepthSource> source() const override { return {}; }
  ComponentSelectionRendererStats stats() const override { return {}; }
};

class CrimsonRenderView final : public RenderView {
public:
  CrimsonRenderView(std::uint8_t mask, std::shared_ptr<CrimsonHostState> owner)
      : state_(std::make_shared<CrimsonViewState>()), owner_(std::move(owner)) {
    state_->layer_mask = mask;
  }
  std::shared_ptr<CrimsonViewState> state() const { return state_; }
  void shutdown() override { state_->active = false; state_->surface.reset(); }
  void set_active(bool active) override {
    if (state_->active != active) { state_->active = active; if (active) state_->dirty = true; }
  }
  void resize(std::uint32_t width, std::uint32_t height) override {
    width = std::max(1u, width);
    height = std::max(1u, height);
    if (state_->width == width && state_->height == height) return;
    state_->width = width;
    state_->height = height;
    state_->dirty = true;
    ++state_->camera_revision;
  }
  void set_presentation_bounds(std::uint32_t width, std::uint32_t height,
      std::uint32_t left, std::uint32_t top) override {
    width = std::max(width, state_->width);
    height = std::max(height, state_->height);
    if (state_->backing_width == width && state_->backing_height == height &&
        state_->crop_left == left && state_->crop_top == top) return;
    state_->backing_width = width;
    state_->backing_height = height;
    state_->crop_left = left;
    state_->crop_top = top;
    state_->dirty = true;
    ++state_->camera_revision;
  }
  void update_camera(const ViewportCameraController &controller) override {
    const auto before = state_->frame.view_from_world;
    const auto projection_before = state_->frame.clip_from_view;
    state_->camera = controller.state();
    state_->pose = controller.pose();
    update_projection(*state_);
    if (before != state_->frame.view_from_world || projection_before != state_->frame.clip_from_view) {
      state_->dirty = true;
      state_->frame.camera_revision = ++state_->camera_revision;
    }
  }
  void apply_scene_settings(const SceneSettings &, bool rendered,
                            const ViewportSettings &settings) override {
    if (rendered) LOG_ERROR("Crimson public renderer received unsupported Rendered presentation");
    const auto color = settings.color(ViewportColorSettingId::Background);
    const auto light = crimson::shared::ModelingLight{
        {settings.float_value(ViewportFloatSettingId::ShadedKeyDirectionX),
         settings.float_value(ViewportFloatSettingId::ShadedKeyDirectionY),
         settings.float_value(ViewportFloatSettingId::ShadedKeyDirectionZ)},
        settings.float_value(ViewportFloatSettingId::ShadedKeyIntensity),
        settings.float_value(ViewportFloatSettingId::ShadedDiffuseWrap),
        settings.float_value(ViewportFloatSettingId::ShadedGroundFill),
        settings.float_value(ViewportFloatSettingId::ShadedSkyFill), 1.0f};
    const auto &old = state_->frame.light;
    if (state_->configured_settings && old.key_direction_view == light.key_direction_view &&
        old.key_intensity == light.key_intensity && old.diffuse_wrap == light.diffuse_wrap &&
        old.ground_fill == light.ground_fill && old.sky_fill == light.sky_fill &&
        state_->frame.clear_color.red == color.r && state_->frame.clear_color.green == color.g &&
        state_->frame.clear_color.blue == color.b) return;
    state_->instance_style = InstanceEditStyle::from_settings(settings);
    const auto background = state_->instance_style.resolve_background_color(color, state_->instance_context);
    state_->frame.clear_color = {background.r, background.g, background.b, 1.0f};
    state_->frame.light = light;
    state_->dirty = true;
    state_->configured_settings = true;
    ++state_->settings_apply_count;
  }
  void update_ground_grid(const ViewportCameraController &controller, float spacing,
                           const ViewportSettings &settings, bool visible) override {
    if (!visible) {
      if (state_->grid) { state_->grid.reset(); state_->dirty=true; }
      return;
    }
    auto placement=ViewportGridPlacementPolicy::placement(controller,state_->width,state_->height);
    const bool analytic=controller.state().projection==ViewportProjection::Perspective;
    if (!analytic) {
      const float scale=std::max({1.0f,static_cast<float>(state_->backing_width)/state_->width,static_cast<float>(state_->backing_height)/state_->height});
      placement.plane_size.x*=scale; placement.plane_size.y*=scale;
    }
    crimson::shared::EditorGrid grid;
    grid.world_from_local.values[12]=placement.origin.x;
    grid.world_from_local.values[13]=placement.origin.y;
    grid.world_from_local.values[14]=placement.origin.z;
    if (placement.rotation==ViewportGridRotation::NegativeHalfPiAroundZ)
      grid.world_from_local.values={0,-1,0,0, 1,0,0,0, 0,0,1,0, placement.origin.x,placement.origin.y,placement.origin.z,1};
    else if (placement.rotation==ViewportGridRotation::PositiveHalfPiAroundX)
      grid.world_from_local.values={1,0,0,0, 0,0,1,0, 0,-1,0,0, placement.origin.x,placement.origin.y,placement.origin.z,1};
    const auto style=InstanceEditStyle::from_settings(settings);
    const auto color=[&](ViewportColorSettingId id) -> crimson::shared::Float4 {
      const auto value=style.resolve_grid_color(settings.color(id),state_->instance_context);
      return {srgb_to_linear(value.r),srgb_to_linear(value.g),srgb_to_linear(value.b),value.a};
    };
    grid.minor_color=color(ViewportColorSettingId::GridMinor);
    grid.major_color=color(ViewportColorSettingId::GridMajor);
    grid.origin_u_color=color(placement.axes.u_color);
    grid.origin_v_color=color(placement.axes.v_color);
    const auto eye=controller.pose().eye;
    grid.camera_position={eye.x,eye.y,eye.z};
    grid.u_axis={placement.axes.u_axis.x,placement.axes.u_axis.y,placement.axes.u_axis.z};
    grid.v_axis={placement.axes.v_axis.x,placement.axes.v_axis.y,placement.axes.v_axis.z};
    grid.plane_size={placement.plane_size.x,placement.plane_size.y};
    grid.spacing=std::max(spacing,0.0001f);
    grid.parent_spacing=std::max(2.0f,grid.spacing);
    grid.analytic=analytic;
    grid.intensity=settings.float_value(analytic ? ViewportFloatSettingId::Grid3DIntensity : ViewportFloatSettingId::Grid2DIntensity);
    grid.minor_width=settings.float_value(ViewportFloatSettingId::GridMinorLineSize);
    grid.major_width=settings.float_value(ViewportFloatSettingId::GridMajorLineSize);
    grid.axis_width=settings.float_value(ViewportFloatSettingId::GridAxisLineSize);
    grid.analytic_widths={grid.minor_width/kDefaultViewportGridMinorLineSize,grid.major_width/kDefaultViewportGridMajorLineSize,grid.axis_width/kDefaultViewportGridAxisLineSize};
    if (state_->grid && *state_->grid==grid) return;
    state_->grid=std::make_shared<const crimson::shared::EditorGrid>(grid);
    state_->dirty=true;
  }
  void set_instance_edit_context_active(bool active, const ViewportSettings &) override {
    if (state_->instance_context != active) { state_->instance_context = active; state_->dirty = true; }
  }
  void set_overlay_visible(bool visible) override {
    if (state_->overlay_visible != visible) { state_->overlay_visible = visible; state_->dirty = true; }
  }
  void set_scene_visible(bool visible) override {
    if (state_->scene_visible != visible) { state_->scene_visible = visible; state_->dirty = true; }
  }
  void set_scene_layer_mask(std::uint8_t mask) override {
    if (state_->scene_layer_mask!=mask) { state_->scene_layer_mask=mask; state_->dirty=true; }
  }
  void set_shadowing_enabled(bool) override {}
  void set_material_presentation(ViewportMaterialDebugMode mode, bool solid, bool mesh_grid) override {
    if ((mode != ViewportMaterialDebugMode::None || mesh_grid) && !state_->unsupported_presentation_reported) {
      LOG_WARN("Crimson advanced material diagnostics and mesh-grid presentation remain pending");
      state_->unsupported_presentation_reported = true;
    }
    if (state_->solid != solid) { state_->solid = solid; state_->dirty = true; }
  }
  bool update_selected_mesh_outlines(const RenderOverlaySelectedObjectSnapshot *snapshot,
      const RenderOverlaySelectionSnapshot *hierarchy, const ViewportPersistentOverlayStyleSnapshot &style, bool visible) override {
    const auto projection=owner_->pending ? owner_->pending : owner_->accepted;
    const bool changed=owner_->editor.update_selected(state_->layer_mask,snapshot,hierarchy,style,visible,
        projection ? projection->preview : nullptr);
    state_->dirty=state_->dirty || changed;
    return changed;
  }
  RenderSelectionDepth &component_selection() override { return selection_depth_; }
  std::uint32_t width() const override { return state_->width; }
  std::uint32_t height() const override { return state_->height; }
  std::uint64_t scene_settings_apply_count() const override { return state_->settings_apply_count; }

private:
  std::shared_ptr<CrimsonViewState> state_;
  std::shared_ptr<CrimsonHostState> owner_;
  CrimsonSelectionDepth selection_depth_;
};

class CrimsonRenderSurface final : public RenderSurface {
public:
  CrimsonRenderSurface(std::shared_ptr<CrimsonHostState> owner,
                       std::shared_ptr<CrimsonSurfaceState> state)
      : owner_(std::move(owner)), state_(std::move(state)) {}
  ~CrimsonRenderSurface() override { reset(); }
  std::shared_ptr<CrimsonSurfaceState> state() const { return state_; }
  bool valid() const override { return owner_->initialized && state_ && state_->target.value; }
  bool needs_initial_frame() const override { return state_ && state_->initial; }
  RenderVulkanCompositionCapacity capacity() const override { return state_ ? state_->capacity : RenderVulkanCompositionCapacity{}; }
  void reset() override {
    if (state_ && state_->target.value && owner_->initialized)
      (void)owner_->accept(owner_->renderer.release_target(state_->target));
    if (state_) state_->target = {};
    state_.reset();
  }

private:
  std::shared_ptr<CrimsonHostState> owner_;
  std::shared_ptr<CrimsonSurfaceState> state_;
};

class CrimsonSceneSink final : public RenderSceneSink {
public:
  explicit CrimsonSceneSink(std::shared_ptr<CrimsonHostState> owner) : owner_(std::move(owner)) {}
  void shutdown() override {
    owner_->preview.reset();
    owner_->source = std::make_shared<const RenderSceneSnapshot>();
    owner_->scene_dirty = true;
  }
  void set_project_material_resolver(const RenderProjectMaterialResolver *resolver) override { resolver_ = resolver; }
  const RenderProjectMaterialResolver *project_material_resolver() const override { return resolver_; }
  void bind_scene(RenderSceneBindingKey binding) override {
    if (binding_ == binding) return;
    binding_ = binding;
    owner_->preview.reset();
    owner_->source.reset();
    owner_->scene_dirty = true;
  }
  void set_backface_culling_enabled(bool enabled) override {
    if (culling_ == enabled) return;
    culling_ = enabled;
    if (owner_->preview) {
        auto preview = std::make_shared<RenderToolPreviewSceneSnapshot>(*owner_->preview);
        for (auto &mesh : preview->meshes) mesh.double_sided = !enabled;
        for (auto &mesh : preview->accepted_overlay_coordinates) mesh.double_sided = !enabled;
        owner_->preview = std::move(preview);
    }
    owner_->scene_dirty = true;
  }
  void sync(const SceneDocument &document, const AssetRegistry &assets,
      std::uint64_t frame_index, RenderSceneBindingKey binding,
      RenderInstanceEditScope scope,
      const ViewportLocalVisibilitySnapshot *visibility) override {
    bind_scene(binding);
    const auto material_revision = resolver_ ? resolver_->revision() : 0u;
    if (owner_->source && render_scene_snapshot_matches_sources(*owner_->source,
        document, assets, binding, material_revision, scope, visibility, culling_)) return;
    sync(build_render_scene_snapshot(document, assets, {
        .previous = owner_->source.get(), .frame_index = frame_index,
        .scene_binding = binding, .project_material_resolver = resolver_,
        .project_material_revision = material_revision, .instance_edit_scope = scope,
        .local_visibility = visibility, .backface_culling_enabled = culling_}));
  }
  void sync(RenderSceneSnapshot snapshot) override {
    owner_->source = std::make_shared<const RenderSceneSnapshot>(std::move(snapshot));
    owner_->scene_dirty = true;
  }
  void update_mesh_surface_grid(float, const ViewportSettings &) override {}
  void sync_scene_asset_overlays(const RenderOverlaySelectedObjectSnapshot &selected,
      const RenderOverlaySceneWireframeSnapshot &wireframe, const RenderOverlayCollisionSnapshot &collision) override {
    const auto projection=owner_->pending ? owner_->pending : owner_->accepted;
    owner_->editor.sync_wireframe(wireframe,projection ? projection->preview : nullptr);
    owner_->editor.sync_collision(collision,selected,projection ? projection->preview : nullptr);
  }
  RenderToolPreviewSceneSnapshot build_tool_preview_scene_snapshot(ToolPreviewScene preview,
      const AssetRegistry &assets, const ViewportLocalVisibilitySnapshot *visibility) override {
    return build_render_tool_preview_scene_snapshot(std::move(preview), assets,
        visibility, resolver_, nullptr, culling_);
  }
  ToolPreviewSubmissionResult set_tool_preview_scene(RenderToolPreviewSceneSnapshot preview) override {
    if (!preview.complete) return ToolPreviewSubmissionResult::Failed;
    for (auto &mesh : preview.meshes) mesh.double_sided = !culling_;
    if (owner_->preview && same_preview(preview, *owner_->preview)) return ToolPreviewSubmissionResult::Unchanged;
    auto previous = owner_->preview;
    owner_->preview = std::make_shared<const RenderToolPreviewSceneSnapshot>(std::move(preview));
    owner_->scene_dirty = true;
    if (!owner_->prepare_scene()) {
      owner_->preview = std::move(previous);
      owner_->scene_dirty = true;
      return ToolPreviewSubmissionResult::Failed;
    }
    return ToolPreviewSubmissionResult::Published;
  }
  const RenderToolPreviewSuppressionSnapshot &tool_preview_suppression() const noexcept override { return owner_->accepted_suppression; }
  std::uint64_t tool_preview_submission_revision() const override { return owner_->preview_submission_revision; }
  bool scene_asset_overlay_sync_pending() const override { return owner_->scene_dirty || owner_->pending != nullptr; }
  void perform_idle_resource_maintenance() override {}
  void set_active_overlay_viewports(std::uint8_t mask) override { owner_->editor.set_active_viewports(mask); }
  ViewportOverlaySubmissionResult submit_viewport_overlay(ViewportOverlaySubmissionRequest request,
      const ViewportSettings &settings) override {
    request.selection_dimension_scene_snapshot = request.scene_overlay_snapshot;
    const auto projection = owner_->pending ? owner_->pending : owner_->accepted;
    if (projection && projection->preview) {
      const auto &preview = *projection->preview;
      request.selection_dimension_preview_meshes = preview.accepted_overlay_coordinates.empty()
          ? std::span<const RenderToolPreviewMeshRecord>(preview.meshes)
          : std::span<const RenderToolPreviewMeshRecord>(preview.accepted_overlay_coordinates);
      request.selection_dimension_preview_suppression = &preview.suppression;
    }
    request.array_dimension_scene_snapshot = projection ? projection->source.get() : nullptr;
    return owner_->editor.submit(std::move(request),settings);
  }

private:
  std::shared_ptr<CrimsonHostState> owner_;
  const RenderProjectMaterialResolver *resolver_ = nullptr;
  RenderSceneBindingKey binding_;
  bool culling_ = true;

  static bool same_preview(const RenderToolPreviewSceneSnapshot &left,
                            const RenderToolPreviewSceneSnapshot &right) {
    if (left.revision != right.revision || left.suppression.signature != right.suppression.signature ||
        left.suppression.entities != right.suppression.entities ||
        left.suppression.occurrences != right.suppression.occurrences ||
        left.meshes.size() != right.meshes.size() ||
        left.accepted_overlay_coordinates.size() != right.accepted_overlay_coordinates.size() ||
        !left.curves.empty() || !right.curves.empty() || !left.lights.empty() || !right.lights.empty() ||
        !left.group_markers.empty() || !right.group_markers.empty()) return false;
    const auto same_matrix = [](const QMat4 &first, const QMat4 &second) { return first.values == second.values; };
    const auto same_mesh = [&](const RenderToolPreviewMeshRecord &first, const RenderToolPreviewMeshRecord &second) {
      if (first.source_entity != second.source_entity || first.source_object_id != second.source_object_id ||
          first.visibility_entity != second.visibility_entity || first.layout_key != second.layout_key ||
          first.position_revision != second.position_revision || first.material_revision != second.material_revision ||
          first.mesh.geometry_payload() != second.mesh.geometry_payload() ||
          first.geometry_projection != second.geometry_projection ||
          first.dynamic_position_payload != second.dynamic_position_payload ||
          first.double_sided != second.double_sided || first.suppress_source != second.suppress_source ||
          !same_matrix(first.transform, second.transform) ||
          !same_matrix(first.sampling_from_local, second.sampling_from_local) ||
          !std::ranges::equal(first.instance_transforms, second.instance_transforms, same_matrix) ||
          !std::ranges::equal(first.instance_sampling_from_local, second.instance_sampling_from_local, same_matrix) ||
          !render_material_payloads_equal(&first.material.primary_material, &second.material.primary_material) ||
          first.material.material_slots.size() != second.material.material_slots.size()) return false;
      for (std::size_t index = 0u; index < first.material.material_slots.size(); ++index) {
        const auto &first_slot = first.material.material_slots[index];
        const auto &second_slot = second.material.material_slots[index];
        if (first_slot.first != second_slot.first || !render_material_payloads_equal(&first_slot.second, &second_slot.second)) return false;
      }
      return true;
    };
    return std::ranges::equal(left.meshes, right.meshes, same_mesh) &&
           std::ranges::equal(left.accepted_overlay_coordinates, right.accepted_overlay_coordinates, same_mesh);
  }
};

class CrimsonRenderHost final : public RenderHost {
public:
  CrimsonRenderHost() : state_(std::make_shared<CrimsonHostState>()) {}
  ~CrimsonRenderHost() override { shutdown(); }
  bool initialize(void *, const std::filesystem::path &resources, RenderConfig,
                   const RenderVulkanSharedContext *context) override {
    if (state_->initialized) return true;
    if (context == nullptr || !context->enabled_capabilities) {
      state_->error = "Crimson requires a Qt-owned Vulkan context with proven enabled capabilities";
      LOG_ERROR("{}", state_->error);
      return false;
    }
    const auto &capabilities = *context->enabled_capabilities;
    std::vector<const char *> instance_extensions;
    std::vector<const char *> device_extensions;
    for (const auto &extension : capabilities.instance_extensions) instance_extensions.push_back(extension.c_str());
    for (const auto &extension : capabilities.device_extensions) device_extensions.push_back(extension.c_str());
    crimson::shared::BorrowedVulkanContext borrowed{
        .instance = context->instance, .physicalDevice = context->physical_device,
        .device = context->device, .queue = context->graphics_queue,
        .queueFamilyIndex = context->graphics_queue_family, .queueIndex = context->graphics_queue_index,
        .apiVersion = capabilities.api_version, .enabledFeatures = capabilities.features,
        .maxSamplerAnisotropy = capabilities.features.samplerAnisotropy ? 8.0f : 1.0f,
        .enabledInstanceExtensionCount = static_cast<std::uint32_t>(instance_extensions.size()),
        .enabledInstanceExtensions = instance_extensions.data(),
        .enabledDeviceExtensionCount = static_cast<std::uint32_t>(device_extensions.size()),
        .enabledDeviceExtensions = device_extensions.data()};
    if (!state_->accept(state_->renderer.initialize(borrowed))) return false;
    state_->resources = resources;
    state_->projection_cache = std::make_unique<CrimsonSceneProjectionCache>(resources);
    state_->initialized = true;
    state_->scene_dirty = true;
    LOG_INFO("Crimson/bgfx Vulkan initialized on borrowed Qt context device {}; shader family {}",
        static_cast<const void *>(context->device), crimson::shared::shader_family_identity());
    LOG_INFO("Crimson renderer source {}; manifest {}; modified={}",
        crimson::shared::kRendererSourceCommit, crimson::shared::kRendererManifestSha256,
        crimson::shared::kRendererWorkingTreeDirty);
    return true;
  }
  void shutdown() override {
    if (!state_->initialized) return;
    if (!state_->accept(state_->renderer.shutdown())) {
      // Qt's void releaseResources callback must not return and release its
      // device while an operational queue has unknown in-flight ownership.
      LOG_ERROR("Cannot release borrowed Qt Vulkan device after failed completion quarantine");
      std::terminate();
    }
    state_->initialized = false;
    for (const auto &entry : state_->surfaces) if (auto surface = entry.lock()) surface->target = {};
    state_->pending.reset();
    state_->accepted.reset();
    state_->accepted_editor_scene_generation=0u;
    state_->source.reset();
    state_->preview.reset();
    state_->accepted_suppression = {};
    state_->projection_cache.reset();
    state_->editor.clear();
    state_->views.clear();
    state_->surfaces.clear();
    state_->submitted_epoch = 0u;
    state_->preparation_epoch = 0u;
    state_->presentation.reset();
  }
  bool initialized() const override { return state_->initialized; }
  bool supports_hidden_native_pixels() const override { return true; }
  bool uses_composited_vulkan() const override { return true; }
  std::unique_ptr<RenderSceneSink> create_scene_sink(const std::filesystem::path &) override { return std::make_unique<CrimsonSceneSink>(state_); }
  std::unique_ptr<RenderView> create_view(std::uint8_t mask, ViewportRenderSurface) override {
    auto view = std::make_unique<CrimsonRenderView>(mask,state_);
    state_->views.push_back(view->state());
    return view;
  }
  std::unique_ptr<RenderMaterialPreviewView> create_material_preview_view() override { return {}; }
  std::unique_ptr<RenderSurface> create_composited_surface(std::uint32_t width, std::uint32_t height) override {
    if (!state_->initialized) return {};
    auto surface = std::make_shared<CrimsonSurfaceState>();
    surface->capacity = {width, height};
    if (!state_->accept(state_->renderer.create_target({width, height}, surface->target))) return {};
    state_->surfaces.push_back(surface);
    return std::make_unique<CrimsonRenderSurface>(state_, std::move(surface));
  }
  std::unique_ptr<RenderSurface> create_native_surface(void *, SwapChainPresentationColorSpace) override {
    state_->error = "Crimson scene presentation requires Qt Vulkan composition";
    LOG_ERROR("{}", state_->error);
    return {};
  }
  void bind_view_surface(RenderView &view, RenderSurface &surface) override {
    auto *crimson_view = dynamic_cast<CrimsonRenderView *>(&view);
    auto *crimson_surface = dynamic_cast<CrimsonRenderSurface *>(&surface);
    if (!crimson_view || !crimson_surface) return;
    const auto view_state = crimson_view->state();
    const auto surface_state = crimson_surface->state();
    if (view_state->surface.lock() != surface_state) {
      view_state->surface = surface_state;
      view_state->dirty = true;
    }
  }
  bool begin_frame(RenderSurface &surface) override { return state_->initialized && surface.valid(); }
  void end_frame(RenderSurface &) override {}
  void render_viewport(RenderView &, RenderSurface &) override {
    state_->error = "Crimson requires a borrowed Qt destination image for scene presentation";
  }
  bool render_composited_viewport(RenderView &view, RenderSurface &surface,
      const RenderVulkanCompositionTarget &target) override {
    if (!state_->initialized || state_->preparation_depth != 0u || !surface.valid()) return false;
    bind_view_surface(view, surface);
    auto *crimson_view = dynamic_cast<CrimsonRenderView *>(&view);
    auto *crimson_surface = dynamic_cast<CrimsonRenderSurface *>(&surface);
    if (!crimson_view || !crimson_surface || !submit_views()) return false;
    const auto view_state = crimson_view->state();
    const auto surface_state = crimson_surface->state();
    const crimson::shared::VulkanImageTarget destination{
        .image = target.image, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .oldLayout = target.old_layout, .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .width = target.width, .height = target.height,
        .sourceX = view_state->crop_left, .sourceY = view_state->crop_top};
    if (!state_->accept(state_->renderer.copy_to_vulkan(surface_state->target,
        destination, surface_state->last_submission))) return false;
    surface_state->initial = false;
    ++state_->copy_count;
    return true;
  }
  void render_material_preview(RenderMaterialPreviewView &, RenderSurface &) override {}
  void begin_view_preparation() override { ++state_->preparation_depth; }
  void complete_view_preparation() override {
    if (state_->preparation_depth && --state_->preparation_depth == 0u) ++state_->preparation_epoch;
  }
  void begin_presentation_frame() override {
    if (state_->presentation_open) return;
    state_->presentation_open = true;
    state_->copy_count = 0u;
    state_->presentation.begin_presentation_frame(RenderPresentationFrameTracker::Clock::now());
  }
  void complete_presentation_frame(std::size_t submissions) override {
    (void)state_->presentation.complete_presentation_frame(RenderPresentationFrameTracker::Clock::now(), submissions, state_->performance);
    state_->presentation_open = false;
  }
  void complete_composited_presentation_frame() override { complete_presentation_frame(state_->copy_count); }
  bool finish_external_queue_submission() override { return true; }
  void begin_shared_command_batch() override {}
  void end_shared_command_batch() override {}
  void mark_shared_commands_pending() override {}
  void set_composited_present_mode(VkPresentModeKHR mode) override { state_->present_mode = mode; }
  void set_display_refresh_rate_hz(float) override {}
  void set_vsync_enabled(bool enabled) override { state_->vsync = enabled; }
  bool vsync_enabled() const override { return state_->vsync; }
  bool presentation_is_synchronized() const override {
    return state_->present_mode == VK_PRESENT_MODE_FIFO_KHR || state_->present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR;
  }
  bool ready_for_next_frame() const override {
    for (const auto &entry : state_->surfaces) {
      if (const auto surface = entry.lock(); surface && surface->last_submission.serial &&
          !state_->renderer.is_complete(surface->last_submission)) return false;
    }
    return true;
  }
  RenderFramePerformanceStats frame_performance_stats() const override { return state_->performance; }
  void apply_scene_settings(const SceneSettings &) override {}
  std::shared_ptr<const MaterialThumbnailReadback> submit_material_thumbnail(RenderMaterialEvaluationSnapshot, std::uint32_t) override { return {}; }
  void poll_material_thumbnails() override {}

  CrimsonRenderDiagnostics diagnostics() const {
    CrimsonRenderDiagnostics value;
    value.frame_submissions = state_->frame_submissions;
    value.accepted_scene_generation = state_->accepted ? state_->accepted->generation : 0u;
    value.pending_scene_generation = state_->pending ? state_->pending->generation : 0u;
    value.accepted_editor_scene_generation = state_->accepted_editor_scene_generation;
    value.translation = state_->projection_cache ? state_->projection_cache->stats() : CrimsonSceneProjectionStats{};
    value.editor_projection = state_->editor.counters();
    value.editor_gpu = state_->renderer.editor_retention_stats();
    value.error = state_->error;
    value.editor_overlays_pending = !state_->initialized || state_->scene_dirty || state_->candidate_rejected || state_->pending != nullptr;
    for (const auto &entry : state_->views) if (const auto view = entry.lock(); view && view->active) {
      ++value.active_views;
      const auto surface = view->surface.lock();
      value.editor_overlays_pending = value.editor_overlays_pending || view->dirty || !surface || !surface->rendered;
      if (!view->accepted_editor) continue;
      const auto &editor = *view->accepted_editor;
      CrimsonEditorViewDiagnostics pane;
      pane.layer_mask = view->layer_mask;
      pane.frame_identity = reinterpret_cast<std::uintptr_t>(view->accepted_editor.get());
      pane.frame_revision = editor.revision;
      pane.scene_generation = editor.scene_generation;
      if (editor.selection) {
        pane.ordinary_masks = editor.selection->ordinary.size();
        pane.active_masks = editor.selection->active.size();
        pane.ordinary_color = editor.selection->ordinary_color;
        pane.active_color = editor.selection->active_color;
      }
      value.editor_views.push_back(pane);
    }
    return value;
  }

private:
  bool submit_views() {
    std::vector<std::shared_ptr<CrimsonViewState>> active;
    std::erase_if(state_->views, [](const auto &entry) { return entry.expired(); });
    for (const auto &entry : state_->views) {
      auto view = entry.lock();
      if (!view || !view->active) continue;
      const auto surface = view->surface.lock();
      if (!surface || !surface->target.value) continue;
      active.push_back(std::move(view));
    }
    const auto accepted_surfaces_available = [&] {
      return !active.empty() && std::ranges::all_of(active, [](const auto &view) {
        const auto surface = view->surface.lock();
        return surface && surface->rendered;
      });
    };
    if (!state_->prepare_scene()) return accepted_surfaces_available();
    const auto projection = state_->pending ? state_->pending : state_->accepted;
    if (!projection) return false;
    bool dirty = state_->pending != nullptr;
    for (const auto &view : active) {
      const auto surface = view->surface.lock();
      update_projection(*view);
      const auto editor = state_->editor.frame(view->layer_mask,view->frame.clip_from_view,
          {view->crop_left,view->crop_top,view->width,view->height},{view->backing_width,view->backing_height},view->grid,view->overlay_visible,projection->generation,view->scene_visible);
      if (view->frame.editor != editor) { view->frame.editor = editor; view->dirty = true; }
      view->frame.generation = state_->frame_submissions + 1u;
      view->frame.scene_generation = projection->generation;
      view->frame.pass = !view->scene_visible ? crimson::shared::ScenePass::Empty
          : view->solid ? crimson::shared::ScenePass::Solid : crimson::shared::ScenePass::Shaded;
      dirty = dirty || view->dirty || !surface->rendered;
    }
    if (!dirty) return true;
    if (active.empty()) return false;
    for (const auto &view : active) {
      const auto surface=view->surface.lock();
      if (!state_->accept(state_->renderer.prepare_view(surface->target,view->frame)))
        return accepted_surfaces_available();
    }
    for (const auto &view : active) {
      const auto surface = view->surface.lock();
      if (!state_->accept(state_->renderer.render_view(surface->target, view->frame))) return false;
    }
    if (!state_->accept(state_->renderer.submit())) return false;
    state_->error.clear();
    ++state_->frame_submissions;
    state_->accepted_editor_scene_generation=projection->generation;
    state_->submitted_epoch = state_->preparation_epoch;
    state_->acknowledge_scene();
    for (const auto &view : active) {
      view->dirty = false;
      view->accepted_editor = view->frame.editor;
      if (const auto surface = view->surface.lock()) surface->rendered = true;
    }
    return true;
  }

  std::shared_ptr<CrimsonHostState> state_;
};

} // namespace

std::unique_ptr<RenderHost> create_crimson_render_host() {
  return std::make_unique<CrimsonRenderHost>();
}

bool crimson_render_diagnostics(const RenderHost &host, CrimsonRenderDiagnostics &diagnostics) {
  const auto *crimson_host = dynamic_cast<const CrimsonRenderHost *>(&host);
  if (crimson_host == nullptr) return false;
  diagnostics = crimson_host->diagnostics();
  return true;
}

} // namespace quader
