////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/crimson/crimson_render_host.hpp"
#include "render/crimson/crimson_editor_projection.hpp"
#include "render/crimson/crimson_native_queue_handoff.hpp"
#include "render/crimson/crimson_material_preview.hpp"
#include "render/crimson/crimson_material_thumbnails.hpp"
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
#include "render/crimson/crimson_rich_scene.hpp"
#include "integration/ModelingWorldSnapshot.h"
#include "integration/native_scene_render_bindings.hpp"
#endif

#include "core/color_space.hpp"
#include "core/logger.hpp"
#include "render/instance_edit_style.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/viewport_grid_placement_policy.hpp"
#include "viewport/viewport_settings_defaults.hpp"

#include <crimson/renderer/Scene.hpp>
#include <crimson/renderer/BorrowedVulkan.hpp>
#include <crimson/renderer/BorrowedD3D12.hpp>
#include <crimson/renderer/BorrowedD3D11.hpp>
#include "render/crimson/crimson_copy_identity.hpp"
#include "render/crimson/crimson_d3d11_context_state.hpp"
#include <bgfx/d3d11.h>
#include <crimson/renderer/forward/RenderFrame.h>
#include "crimson-renderer/Environment.h"
#include <crimson/renderer/ShaderFamily.hpp>
#include <crimson/renderer/BuildIdentity.hpp>

#include <bgfx/bgfx.h>
#include <bgfx/d3d12.h>

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
#include <unordered_set>

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
  bool gpu_instancing_enabled = true;
  crimson::ModelingSurfaceGrid mesh_grid;
  bool direct12 = false;
  bool direct11 = false;
  bool d3d11_external = false;
  std::uint32_t d3d11_external_depth=0;
  CrimsonD3D11ContextState d3d11_context_state;
  RenderCompositionApi requested_api=RenderCompositionApi::Direct3D12;
  bool native_forward()const noexcept{return direct12||direct11;}
  void *borrowed_device = nullptr, *borrowed_queue = nullptr;
  CrimsonNativeQueueHandoff queue_handoff;
  std::shared_ptr<const crimson::PreparedRenderFrame> engine_frame;
  std::shared_ptr<const crimson::integration::NativeSceneRenderBindings> native_bindings;
  std::uint64_t engine_revision = 0, engine_submitted_revision = 0;
  std::uint32_t completed_frame = 0;
  std::array<crimson::shared::Submission,2> engine_copies{};
  struct EnginePreviewEntry {
    std::weak_ptr<const void> lifetime;
    std::function<bool()> record;
    std::uint64_t revision=0,submitted_revision=0,source_owner=0;
  };
  static constexpr auto kEnginePreviewCount=static_cast<std::size_t>(EnginePreviewKind::Count);
  std::array<EnginePreviewEntry,kEnginePreviewCount> engine_previews{};
  // Clearing a document callback must not discard the completion marker that
  // still protects the destination used by its last native copy.
  std::array<crimson::shared::Submission,kEnginePreviewCount> engine_preview_copies{};
  std::uint64_t engine_preview_revision=0;
  struct PreviewEntry {CrimsonMaterialPreview* view=nullptr;std::weak_ptr<void> lifetime;};
  std::vector<PreviewEntry> material_previews;
  PreviewEntry selected_preview,submitted_preview;
  std::unique_ptr<CrimsonMaterialThumbnails> material_thumbnails;
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
  std::unique_ptr<CrimsonRichScene> rich_scene;
  std::unique_ptr<CrimsonSceneProjectionCache> authoring_cache;
  std::shared_ptr<const RenderSceneSnapshot> authoring_source;
  std::shared_ptr<const CrimsonSceneProjection> authoring_projection;
  std::uint64_t authoring_generation=0;
#endif

  CrimsonMaterialPreview* material_preview() const {
    return selected_preview.view&&!selected_preview.lifetime.expired()&&selected_preview.view->initialized()
        ?selected_preview.view:nullptr;
  }

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

#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
struct CrimsonComposedFrame {
  std::shared_ptr<const CrimsonRichFrame> native;
  std::shared_ptr<const crimson::PreparedRenderFrame> engine;
  std::vector<crimson::RenderModelInstance> instances,shadows;
  crimson::SceneSettings settings;
  bool engine_lighting{};
};
#endif
struct CrimsonViewState final {
  std::weak_ptr<CrimsonSurfaceState> surface;
  crimson::shared::ViewFrame frame;
  std::shared_ptr<const crimson::shared::EditorFrame> accepted_editor;
  ViewportCameraState camera;
  QVec2 logical_size{1,1};
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
  bool rendered = false,shadowing = true,mesh_grid_visible=false,force_rich_pick=false;
  ViewportMaterialDebugMode material_debug=ViewportMaterialDebugMode::None;
  SceneSettings rendered_settings;
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
  std::shared_ptr<const CrimsonRichFrame> rich_frame;
  std::shared_ptr<const CrimsonComposedFrame> composed;
#endif
  bool instance_context = false;
  bool configured_settings = false;
  bool unsupported_presentation_reported = false;
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
    state_->camera = controller.state();state_->logical_size=controller.viewport_size();
    state_->pose = controller.pose();
    update_projection(*state_);
    if (before != state_->frame.view_from_world || projection_before != state_->frame.clip_from_view) {
      state_->dirty = true;
      state_->frame.camera_revision = ++state_->camera_revision;
    }
  }
  void apply_scene_settings(const SceneSettings &scene_settings, bool rendered,
                            const ViewportSettings &settings) override {
    if(state_->rendered!=rendered||state_->rendered_settings!=scene_settings){
      state_->rendered=rendered;state_->rendered_settings=scene_settings;state_->dirty=true;
    }
#if !defined(QUADER_HAS_CRIMSON_RICH_SCENE)
    if(rendered)LOG_ERROR("Rendered presentation requires the Crimson Engine application integration");
#endif
    const auto style=InstanceEditStyle::from_settings(settings);
    const auto background_setting=settings.color(ViewportColorSettingId::Background);
    const auto source_color=rendered&&state_->material_debug==ViewportMaterialDebugMode::None?scene_settings.environment.clear_color:QVec3{srgb_to_linear(background_setting.r),srgb_to_linear(background_setting.g),srgb_to_linear(background_setting.b)};
    const auto background=style.resolve_background_color({source_color.x,source_color.y,source_color.z,1},state_->instance_context);
    const ViewportSettingColor color{linear_to_srgb(background.r),linear_to_srgb(background.g),linear_to_srgb(background.b),background.a};
    crimson::ModelingContextStyle context;
    context.color={srgb_to_linear(style.context.r),srgb_to_linear(style.context.g),srgb_to_linear(style.context.b),style.context.a};
    context.strength=state_->instance_context?style.context_strength:0.f;context.tintAmount=style.context_tint_amount;context.desaturation=style.context_desaturation;
    const bool context_changed=!state_->frame.modeling_presentation||state_->frame.modeling_presentation->context!=context;
    const auto light = crimson::shared::ModelingLight{
        {settings.float_value(ViewportFloatSettingId::ShadedKeyDirectionX),
         settings.float_value(ViewportFloatSettingId::ShadedKeyDirectionY),
         settings.float_value(ViewportFloatSettingId::ShadedKeyDirectionZ)},
        settings.float_value(ViewportFloatSettingId::ShadedKeyIntensity),
        settings.float_value(ViewportFloatSettingId::ShadedDiffuseWrap),
        settings.float_value(ViewportFloatSettingId::ShadedGroundFill),
        settings.float_value(ViewportFloatSettingId::ShadedSkyFill), 1.0f};
    const auto &old = state_->frame.light;
    const auto anti_aliasing = settings.anti_aliasing() == ViewportAntiAliasing::Smaa
        ? crimson::AntiAliasing::Smaa : crimson::AntiAliasing::Off;
    if (state_->configured_settings && !context_changed && old.key_direction_view == light.key_direction_view &&
        old.key_intensity == light.key_intensity && old.diffuse_wrap == light.diffuse_wrap &&
        old.ground_fill == light.ground_fill && old.sky_fill == light.sky_fill &&
        state_->frame.clear_color.red == color.r && state_->frame.clear_color.green == color.g &&
        state_->frame.clear_color.blue == color.b &&
        state_->frame.anti_aliasing == anti_aliasing) return;
    state_->frame.clear_color = {color.r,color.g,color.b,color.a};
    if(context_changed){auto packet=std::make_shared<crimson::ModelingPresentation>();packet->context=context;state_->frame.modeling_presentation=std::move(packet);}
    state_->frame.light = light;
    state_->frame.anti_aliasing = anti_aliasing;
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
  void set_instance_edit_context_active(bool active,const ViewportSettings &settings) override {
    if(state_->instance_context!=active){state_->instance_context=active;state_->dirty=true;}
    apply_scene_settings(state_->rendered_settings,state_->rendered,settings);
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
  void set_shadowing_enabled(bool enabled) override {
    if(state_->shadowing!=enabled){state_->shadowing=enabled;state_->dirty=true;}
  }
  void set_material_presentation(ViewportMaterialDebugMode mode, bool solid, bool mesh_grid) override {
    if (state_->solid != solid||state_->material_debug!=mode||state_->mesh_grid_visible!=mesh_grid) {
      state_->solid=solid;state_->material_debug=mode;state_->mesh_grid_visible=mesh_grid;state_->dirty=true;state_->configured_settings=false;
    }
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
  void update_mesh_surface_grid(float spacing,const ViewportSettings &settings) override {
    crimson::ModelingSurfaceGrid grid;
    grid.enabled=settings.bool_value(ViewportBoolSettingId::ShowMeshGrid);
    grid.size=std::max(spacing,.0001f);grid.majorSize=grid.size*4.f;
    const auto minor=settings.color(ViewportColorSettingId::MeshGridMinor),major=settings.color(ViewportColorSettingId::MeshGridMajor);
    grid.minorColor={srgb_to_linear(minor.r),srgb_to_linear(minor.g),srgb_to_linear(minor.b),minor.a};
    grid.majorColor={srgb_to_linear(major.r),srgb_to_linear(major.g),srgb_to_linear(major.b),major.a};
    grid.minorLineThickness=settings.float_value(ViewportFloatSettingId::GridMinorLineSize);
    grid.majorLineThickness=settings.float_value(ViewportFloatSettingId::GridMajorLineSize);
    const auto& old=owner_->mesh_grid;
    if(old.enabled==grid.enabled&&old.size==grid.size&&old.majorSize==grid.majorSize&&old.minorColor==grid.minorColor&&old.majorColor==grid.majorColor&&old.minorLineThickness==grid.minorLineThickness&&old.majorLineThickness==grid.majorLineThickness)return;
    owner_->mesh_grid=grid;for(const auto& weak:owner_->views)if(const auto view=weak.lock())view->dirty=true;
  }
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

  static bool same_preview(const RenderToolPreviewSceneSnapshot& left,const RenderToolPreviewSceneSnapshot& right){return crimson_tool_preview_snapshots_equal(left,right);}

};

class CrimsonRenderHost final : public RenderHost {
public:
  CrimsonRenderHost() : state_(std::make_shared<CrimsonHostState>()) {}
  ~CrimsonRenderHost() override { shutdown(); }
  bool initialize(void *, const std::filesystem::path &resources, RenderConfig,
                   const RenderVulkanSharedContext *context) override {
    if (state_->initialized) {
      if(state_->direct11&&(!context||context->api!=RenderCompositionApi::Direct3D11||context->d3d11_device!=state_->borrowed_device||context->d3d11_context!=state_->borrowed_queue)){
        state_->error="Qt renderer device/context generation changed before prior ownership retired";LOG_ERROR("{}",state_->error);return false;
      }
      if (state_->direct12 && (!context || context->api != RenderCompositionApi::Direct3D12
          || context->d3d12_device != state_->borrowed_device || context->d3d12_queue != state_->borrowed_queue)) {
        state_->error = "Qt renderer device/queue generation changed before prior ownership retired";
        LOG_ERROR("{}",state_->error); return false;
      }
      return true;
    }
    if(context&&context->api==RenderCompositionApi::Direct3D11){
      if(!context->d3d11_device||!context->d3d11_context)return false;
      if(!state_->d3d11_context_state.initialize(static_cast<ID3D11Device*>(context->d3d11_device),static_cast<ID3D11DeviceContext*>(context->d3d11_context))){state_->error="Qt Direct3D11 native context-state isolation is unavailable";return false;}
      auto native_scope=state_->d3d11_context_state.scope();
      if(!native_scope)return false;
      auto services=std::make_shared<crimson::ForwardServices>();services->prepare_environment=crimson::prepareEnvironmentCpu;
      crimson::shared::BorrowedD3D11Context borrowed;borrowed.device=static_cast<ID3D11Device*>(context->d3d11_device);borrowed.context=static_cast<ID3D11DeviceContext*>(context->d3d11_context);borrowed.forward_services=std::move(services);
      if(!state_->accept(state_->renderer.initialize(borrowed)))return false;
      state_->resources=resources;state_->projection_cache=std::make_unique<CrimsonSceneProjectionCache>(resources);
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
      state_->rich_scene=std::make_unique<CrimsonRichScene>(state_->renderer,resources);
#endif
      state_->initialized=true;state_->direct11=true;state_->scene_dirty=true;state_->borrowed_device=context->d3d11_device;state_->borrowed_queue=context->d3d11_context;
      LOG_INFO("Crimson modeling and Engine initialized on Qt-owned Direct3D 11 device");return true;
    }
    if (context != nullptr && context->api == RenderCompositionApi::Direct3D12) {
      if (!context->d3d12_device || !context->d3d12_queue) return false;
      auto services = std::make_shared<crimson::ForwardServices>();
      services->prepare_environment = crimson::prepareEnvironmentCpu;
      crimson::shared::BorrowedD3D12Context borrowed;
      borrowed.device = static_cast<ID3D12Device *>(context->d3d12_device);
      borrowed.queue = static_cast<ID3D12CommandQueue *>(context->d3d12_queue);
      borrowed.forward_services = std::move(services);
      if (!state_->queue_handoff.initialize(borrowed.device,borrowed.queue)) return false;
      if (!state_->accept(state_->renderer.initialize(borrowed))) return false;
      state_->resources = resources;
      state_->projection_cache = std::make_unique<CrimsonSceneProjectionCache>(resources);
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
      state_->rich_scene=std::make_unique<CrimsonRichScene>(state_->renderer,resources);
#endif
      state_->initialized = true; state_->direct12 = true; state_->scene_dirty = true;
      state_->borrowed_device = context->d3d12_device; state_->borrowed_queue = context->d3d12_queue;
      LOG_INFO("Crimson modeling and Engine initialized on Qt-owned Direct3D 12 device");
      return true;
    }
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
    auto native_scope=state_->direct11?state_->d3d11_context_state.scope():CrimsonD3D11ContextState::Scope{};
    if(state_->direct11&&!native_scope)std::terminate();
    for(const auto& entry:state_->material_previews)if(!entry.lifetime.expired())entry.view->release_gpu();
    state_->selected_preview={};state_->submitted_preview={};
    state_->engine_frame.reset();state_->native_bindings.reset();
    state_->engine_previews={};
    if(state_->material_thumbnails)state_->material_thumbnails->shutdown();
    state_->material_thumbnails.reset();
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
    for(const auto& entry:state_->views)if(auto view=entry.lock())view->rich_frame.reset();
    if(state_->rich_scene)state_->rich_scene->clear();
    state_->rich_scene.reset();state_->authoring_cache.reset();state_->authoring_projection.reset();state_->authoring_source.reset();
#endif
    if(state_->direct12&&!state_->queue_handoff.prepare_shutdown())std::terminate();
    if (!state_->accept(state_->renderer.shutdown())) {
      // Qt's void releaseResources callback must not return and release its
      // device while an operational queue has unknown in-flight ownership.
      LOG_ERROR("Cannot release borrowed Qt Vulkan device after failed completion quarantine");
      std::terminate();
    }
    state_->initialized = false;
    state_->queue_handoff.reset();
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
    state_->engine_copies = {}; state_->engine_revision = state_->engine_submitted_revision = 0;
    state_->engine_preview_copies={};state_->engine_preview_revision=0;
    state_->direct12 = false;
    state_->direct11=false;state_->d3d11_external=false;
    state_->borrowed_device = state_->borrowed_queue = nullptr;
    native_scope.reset();state_->d3d11_context_state.request_reset();
  }
  RenderCompositionApi composition_api() const override { return state_->requested_api; }
  bool set_composition_api(RenderCompositionApi api)override{if(state_->initialized)return api==composition_api();if(api!=RenderCompositionApi::Direct3D11&&api!=RenderCompositionApi::Direct3D12&&api!=RenderCompositionApi::Vulkan)return false;state_->requested_api=api;return true;}
  crimson::shared::Renderer *crimson_renderer() override { return state_->initialized ? &state_->renderer : nullptr; }
  void publish_modeling_authoring_source(std::shared_ptr<const RenderSceneSnapshot> source) override {
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
    if(state_->authoring_source==source)return;
    state_->authoring_source=std::move(source);state_->authoring_projection.reset();if(!state_->authoring_source&&state_->rich_scene)state_->rich_scene->clear_authoring();
#endif
  }
  bool modeling_world_ready() const override {
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
    if(!state_->authoring_source)return true;
    const auto publication=modeling_world_snapshot();return publication&&state_->authoring_projection&&publication->generation==state_->authoring_projection->generation;
#else
    return true;
#endif
  }
  std::shared_ptr<const crimson::integration::ModelingWorldSnapshot> modeling_world_snapshot() const override {
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
    try{return state_->rich_scene?state_->rich_scene->snapshot(true):nullptr;}catch(const std::exception& error){state_->error=error.what();return {};}
#else
    return {};
#endif
  }
  std::uint32_t completed_frame() const override { return state_->completed_frame; }
  bool request_native_pick(RenderView& native,const crimson::PickRequest& request) override {
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
    auto* view=dynamic_cast<CrimsonRenderView*>(&native);if(!view||!state_->initialized||!state_->native_forward())return false;const auto source=view->state();const auto surface=source->surface.lock();if(!surface)return false;source->force_rich_pick=true;source->dirty=true;if(!source->rich_frame)return false;
    auto physical=request;physical.x=source->crop_left+static_cast<std::uint32_t>(std::floor(float(request.x)*float(source->width)/std::max(1.f,source->logical_size.x)));physical.y=source->crop_top+static_cast<std::uint32_t>(std::floor(float(request.y)*float(source->height)/std::max(1.f,source->logical_size.y)));
    return state_->accept(state_->renderer.request_modeling_pick(surface->target,physical,source->backing_width,source->backing_height));
#else
    (void)native;(void)request;return false;
#endif
  }
  void publish_native_scene_bindings(std::shared_ptr<const crimson::integration::NativeSceneRenderBindings> bindings) override {
    if(state_->native_bindings==bindings)return;state_->native_bindings=std::move(bindings);for(const auto& weak:state_->views)if(const auto view=weak.lock())view->dirty=true;
  }
  void publish_engine_frame(std::shared_ptr<const crimson::PreparedRenderFrame> frame) override {
    if (state_->engine_frame == frame) return;
    state_->engine_frame = std::move(frame); ++state_->engine_revision;
  }
  void publish_engine_preview(EnginePreviewKind kind,std::weak_ptr<const void> lifetime,std::function<bool()> record) override {
    const auto index=static_cast<std::size_t>(kind);
    if(index>=state_->engine_previews.size())return;
    if(lifetime.expired()||!record){clear_engine_preview(kind);return;}
    const auto& previous=state_->engine_previews[index];const bool same_owner=!lifetime.owner_before(previous.lifetime)&&!previous.lifetime.owner_before(lifetime);
    const auto owner=same_owner&&previous.source_owner?previous.source_owner:next_crimson_copy_identity();
    state_->engine_previews[index]={std::move(lifetime),std::move(record),++state_->engine_preview_revision,0,owner};
  }
  void clear_engine_preview(EnginePreviewKind kind) override {
    const auto index=static_cast<std::size_t>(kind);
    if(index<state_->engine_previews.size())state_->engine_previews[index]={};
  }
  bool render_engine_preview(EnginePreviewKind kind,const RenderVulkanCompositionTarget& target) override {
    const auto index=static_cast<std::size_t>(kind);
    if(index>=state_->engine_previews.size()||!state_->initialized||!state_->native_forward()||!native_target(target))return false;
    (void)submit_views();const auto& entry=state_->engine_previews[index];const auto binding=entry.lifetime.lock();
    if(!binding||!entry.revision||entry.submitted_revision!=entry.revision)return false;
    bgfx::TextureHandle source=BGFX_INVALID_HANDLE;
    switch(kind){
      case EnginePreviewKind::Ragdoll:source=state_->renderer.ragdollPreviewTexture();break;
      case EnginePreviewKind::Animation:source=state_->renderer.animationPreviewTexture();break;
      case EnginePreviewKind::Particle:source=state_->renderer.particlePreviewTexture();break;
      case EnginePreviewKind::Decal:source=state_->renderer.decalPreviewTexture();break;
      case EnginePreviewKind::Count:return false;
    }
    crimson::shared::Submission candidate;const bool copied=copy_native(source,target,candidate,0,0,entry.source_owner);
    if(candidate.serial)state_->engine_preview_copies[index]=candidate;
    if(!copied)return false;++state_->copy_count;return true;
  }
  bool render_engine_viewport(bool game,const RenderVulkanCompositionTarget& target) override {
    if(!state_->initialized||!state_->native_forward()||!state_->engine_frame||!native_target(target)||!submit_views())return false;
    const auto source=game?state_->renderer.gameViewportTexture():state_->renderer.viewportTexture();
    const auto owner=state_->engine_frame->world?state_->engine_frame->world->shadowWorldId:0;
    crimson::shared::Submission candidate;const bool copied=copy_native(source,target,candidate,0,0,owner);
    if(candidate.serial)state_->engine_copies[game?1:0]=candidate;
    if(!copied)return false;++state_->copy_count;return true;
  }
  bool initialized() const override { return state_->initialized; }
  bool supports_hidden_native_pixels() const override { return true; }
  bool uses_composited_vulkan() const override { return true; }
  std::unique_ptr<RenderSceneSink> create_scene_sink(const std::filesystem::path &) override { return std::make_unique<CrimsonSceneSink>(state_); }
  std::unique_ptr<RenderView> create_view(std::uint8_t mask, ViewportRenderSurface) override {
    auto view = std::make_unique<CrimsonRenderView>(mask,state_);
    view->state()->frame.enable_gpu_instancing = state_->gpu_instancing_enabled;
    state_->views.push_back(view->state());
    return view;
  }
  std::unique_ptr<RenderMaterialPreviewView> create_material_preview_view() override {
    if(!state_->initialized||!state_->native_forward())return {};
    auto view=std::make_unique<CrimsonMaterialPreview>(state_->resources);
    std::erase_if(state_->material_previews,[](const auto& entry){return entry.lifetime.expired();});
    state_->material_previews.push_back({view.get(),view->lifetime()});return view;
  }
  bool set_material_preview_recorder(RenderMaterialPreviewView& view,RenderMaterialPreviewRecorder recorder) override {
    auto* preview=dynamic_cast<CrimsonMaterialPreview*>(&view);if(!preview)return false;
    preview->set_recorder(std::move(recorder));return true;
  }
  bool prepare_material_preview(RenderMaterialPreviewView& view,bool active_window) override {
    auto* preview=dynamic_cast<CrimsonMaterialPreview*>(&view);
    if(!state_->initialized||!state_->native_forward()||!preview||!preview->initialized())return false;
    if(active_window||!state_->material_preview())state_->selected_preview={preview,preview->lifetime()};
    return true;
  }
  void release_material_preview(RenderMaterialPreviewView& view) override {
    auto* preview=dynamic_cast<CrimsonMaterialPreview*>(&view);if(!preview)return;
    if(state_->selected_preview.view==preview)state_->selected_preview={};
    if(state_->submitted_preview.view==preview) {
      state_->submitted_preview={};
      if(state_->initialized)state_->renderer.releaseMaterialPreview();
    }
  }
  bool render_composited_material_preview(RenderMaterialPreviewView& view,const RenderVulkanCompositionTarget& target) override {
    auto* preview=dynamic_cast<CrimsonMaterialPreview*>(&view);
    if(!state_->initialized||!state_->native_forward()||!preview||!native_target(target))return false;
    const auto destination=target.api==RenderCompositionApi::Direct3D11?target.d3d11_texture:target.d3d12_resource;
    const auto retained=[&]{return preview->last_destination==destination&&preview->last_destination_width==target.width&&preview->last_destination_height==target.height&&preview->last_destination_generation==target.destination_generation;};
    if(state_->material_preview()!=preview)return retained();
    if(!submit_views()||state_->submitted_preview.view!=preview||state_->submitted_preview.lifetime.expired())return retained();
    crimson::shared::Submission candidate;const bool copied=copy_native(state_->renderer.materialPreviewTexture(),target,candidate,0,0,preview->source_owner());
    if(candidate.serial)preview->last_copy=candidate;
    if(!copied)return false;
    preview->last_destination=destination;preview->last_destination_width=target.width;preview->last_destination_height=target.height;preview->last_destination_generation=target.destination_generation;
    ++state_->copy_count;return true;
  }
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
  bool render_composited_viewport(RenderView& view,RenderSurface& surface,const RenderVulkanCompositionTarget& target) override {
    if(!state_->initialized||state_->preparation_depth!=0u||!surface.valid())return false;
    bind_view_surface(view,surface);auto* crimson_view=dynamic_cast<CrimsonRenderView*>(&view);auto* crimson_surface=dynamic_cast<CrimsonRenderSurface*>(&surface);
    if(!crimson_view||!crimson_surface||!submit_views())return false;
    const auto view_state=crimson_view->state();const auto surface_state=crimson_surface->state();crimson::shared::Submission candidate;bool copied{};
    if(target.api!=RenderCompositionApi::Vulkan)copied=copy_native(surface_state->target,target,candidate,view_state->crop_left,view_state->crop_top,0);
    else {
      const crimson::shared::VulkanImageTarget destination{.image=target.image,.format=VK_FORMAT_R8G8B8A8_UNORM,.oldLayout=target.old_layout,.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,.width=target.width,.height=target.height,.sourceX=view_state->crop_left,.sourceY=view_state->crop_top};
      copied=state_->accept(state_->renderer.copy_to_vulkan(surface_state->target,destination,candidate));
    }
    if(candidate.serial)surface_state->last_submission=candidate;
    if(!copied)return false;surface_state->initial=false;++state_->copy_count;return true;
  }
  void render_material_preview(RenderMaterialPreviewView &, RenderSurface &) override {}
  void begin_application_frame() override {
    if(state_->initialized&&state_->native_forward())state_->renderer.advanceStreaming();
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
    if(state_->initialized&&state_->rich_scene&&state_->authoring_source){
      if(!state_->authoring_cache)state_->authoring_cache=std::make_unique<CrimsonSceneProjectionCache>(state_->resources);
      if(!state_->authoring_projection){std::string error;if(!state_->authoring_cache->prepare(state_->authoring_source,{},++state_->authoring_generation,state_->authoring_projection,error)){state_->error=std::move(error);return;}}
      if(!state_->rich_scene->prepare(state_->authoring_projection,true))state_->error=state_->rich_scene->status();
    }
#endif
  }
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
  bool finish_external_queue_submission() override {
    if(state_->initialized&&state_->direct11)return bgfx::isD3D11ContextHealthy();
    return !state_->initialized || !state_->direct12
        || (state_->queue_handoff.healthy() && bgfx::isD3D12ContextHealthy());
  }
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
    for(const auto& entry:state_->material_previews)if(!entry.lifetime.expired()&&entry.view->last_copy.serial&&!state_->renderer.is_complete(entry.view->last_copy))return false;
    for (const auto &copy : state_->engine_copies)
      if (copy.serial && !state_->renderer.is_complete(copy)) return false;
    for (const auto &copy : state_->engine_preview_copies)
      if (copy.serial && !state_->renderer.is_complete(copy)) return false;
    for (const auto &entry : state_->surfaces) {
      if (const auto surface = entry.lock(); surface && surface->last_submission.serial &&
          !state_->renderer.is_complete(surface->last_submission)) return false;
    }
    return true;
  }
  RenderFramePerformanceStats frame_performance_stats() const override { return state_->performance; }
  void apply_scene_settings(const SceneSettings &) override {}
  std::shared_ptr<const MaterialThumbnailReadback> submit_material_thumbnail(RenderMaterialEvaluationSnapshot snapshot,std::uint32_t size) override {
    if(!state_->initialized||!state_->native_forward())return {};
    if(!state_->material_thumbnails)state_->material_thumbnails=std::make_unique<CrimsonMaterialThumbnails>(state_->resources);
    return state_->material_thumbnails->submit(std::move(snapshot),size);
  }
  void poll_material_thumbnails() override {
    if(state_->initialized&&state_->material_thumbnails)state_->material_thumbnails->poll(state_->renderer,state_->completed_frame);
  }
  bool submit_prepared_offscreen_work() override {
    if(state_->direct11)return state_->initialized&&state_->d3d11_external&&state_->preparation_depth==0&&submit_views();
    return state_->initialized&&state_->preparation_depth==0&&state_->material_thumbnails&&
        state_->material_thumbnails->needs_recording()&&submit_views();
  }

  std::uint64_t frame_submissions() const noexcept { return state_->frame_submissions; }
  bool begin_native_external_content(void* context)override{
    if(!state_->initialized||!state_->direct11||!context||!state_->d3d11_context_state.begin())return false;
    ++state_->d3d11_external_depth;state_->d3d11_external=true;return true;
  }
  void end_native_external_content()noexcept override{
    if(!state_->d3d11_external_depth)return;
    --state_->d3d11_external_depth;state_->d3d11_external=state_->d3d11_external_depth!=0;
    state_->d3d11_context_state.end();
  }

  void set_gpu_instancing_enabled(bool enabled) {
    if (state_->gpu_instancing_enabled == enabled) return;
    state_->gpu_instancing_enabled = enabled;
    for (const auto &entry : state_->views) if (const auto view = entry.lock()) {
      view->frame.enable_gpu_instancing = enabled;
      view->dirty = true;
    }
  }

  CrimsonRenderDiagnostics diagnostics() const {
    CrimsonRenderDiagnostics value;
    value.frame_submissions = state_->frame_submissions;
    value.accepted_scene_generation = state_->accepted ? state_->accepted->generation : 0u;
    value.pending_scene_generation = state_->pending ? state_->pending->generation : 0u;
    value.accepted_editor_scene_generation = state_->accepted_editor_scene_generation;
    value.translation = state_->projection_cache ? state_->projection_cache->stats() : CrimsonSceneProjectionStats{};
    value.scene_gpu = state_->renderer.retention_stats();
    value.scene_meshes = state_->accepted ? state_->accepted->meshes.size() : 0u;
    value.scene_instances = state_->accepted ? state_->accepted->instances.size() : 0u;
    if (state_->accepted) {
      value.scene_instance_identity_hash = 14695981039346656037u;
      for (const auto &instance : state_->accepted->instances) {
        value.scene_instance_identity_hash ^= instance.id;
        value.scene_instance_identity_hash *= 1099511628211u;
      }
    }
    if (state_->initialized) {
      if (const auto *stats = bgfx::getStats()) value.bgfx_draws = stats->numDraw;
    }
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
  bool native_target(const RenderVulkanCompositionTarget& target)const {
    return (state_->direct12&&target.api==RenderCompositionApi::Direct3D12&&target.d3d12_resource)
        ||(state_->direct11&&target.api==RenderCompositionApi::Direct3D11&&target.d3d11_texture&&target.d3d11_context);
  }
  template<class Source> bool copy_native(Source source,const RenderVulkanCompositionTarget& target,crimson::shared::Submission& receipt,std::uint32_t x,std::uint32_t y,std::uint64_t source_owner){
    if(target.api==RenderCompositionApi::Direct3D11){
      if(!state_->direct11||!state_->d3d11_external)return false;
      crimson::shared::D3D11ImageTarget destination;destination.texture=static_cast<ID3D11Texture2D*>(target.d3d11_texture);destination.context=static_cast<ID3D11DeviceContext*>(target.d3d11_context);destination.width=target.width;destination.height=target.height;destination.sourceX=x;destination.sourceY=y;destination.destinationGeneration=target.destination_generation;destination.sourceOwner=source_owner;
      const auto copied=state_->renderer.copy_to_d3d11(source,destination,receipt);
      // Healthy busy is an ordinary staged retry, never a repeated error log.
      if(!copied.ok()){if(!bgfx::isD3D11ContextHealthy())state_->accept(copied);return false;}
      return true;
    }
    if(!state_->direct12||target.api!=RenderCompositionApi::Direct3D12)return false;
    crimson::shared::D3D12ImageTarget destination;destination.resource=static_cast<ID3D12Resource*>(target.d3d12_resource);destination.state=static_cast<D3D12_RESOURCE_STATES>(target.d3d12_state);destination.width=target.width;destination.height=target.height;destination.sourceX=x;destination.sourceY=y;
    if(!state_->queue_handoff.acquire(target.d3d12_queue))return false;
    const auto copied=state_->renderer.copy_to_d3d12(source,destination,receipt);
    if(receipt.serial&&!state_->queue_handoff.handoff(target.d3d12_queue))return false;
    return state_->accept(copied);
  }

#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
  const crimson::PreparedRenderView* engine_editor_view()const {
    if(!state_->engine_frame)return nullptr;const auto found=std::ranges::find_if(state_->engine_frame->views,[](const auto& view){return view.identity.purpose==crimson::RenderViewPurpose::Scene;});return found==state_->engine_frame->views.end()?nullptr:&*found;
  }
  bool engine_editor_geometry()const {
    if(engine_scene_lighting())return true;
    if(state_->native_bindings&&!state_->native_bindings->material_entities.empty())return true;
    const auto* view=engine_editor_view();return view&&(!view->instances.empty()||!view->collisionOverlays.empty()||(state_->engine_frame->world&&(!state_->engine_frame->world->particles.empty()||!state_->engine_frame->world->decals.empty())));
  }
  bool engine_scene_lighting()const {return state_->native_bindings&&state_->native_bindings->scene_lighting&&engine_editor_view();}
  void compose_editor_view(CrimsonViewState& view){
    const auto* editor=engine_editor_view();if(!view.rich_frame||!editor||!state_->engine_frame->world){view.composed.reset();return;}
    auto composed=std::make_shared<CrimsonComposedFrame>();composed->native=view.rich_frame;composed->engine=state_->engine_frame;const auto native=view.rich_frame->scene();composed->instances.assign(native.instances.begin(),native.instances.end());composed->instances.insert(composed->instances.end(),editor->instances.begin(),editor->instances.end());composed->shadows.assign(native.shadowInstances.begin(),native.shadowInstances.end());
    composed->engine_lighting=engine_scene_lighting()||view.rendered;composed->settings=composed->engine_lighting?editor->settings:native.settings;composed->settings.camera=native.settings.camera;
    std::unordered_set<std::uint64_t> native_geometry;if(state_->native_bindings)for(const auto& [uuid,binding]:state_->native_bindings->entities){(void)uuid;if(binding.native_geometry)native_geometry.insert(binding.entity);}
    for(const auto& instance:state_->engine_frame->world->shadowInstances)if(!native_geometry.contains(instance.entityId))composed->shadows.push_back(instance);view.composed=std::move(composed);
  }
  crimson::RenderScene rich_scene(const CrimsonViewState& view) const {
    const auto native=view.rich_frame->scene();const bool engine_lighting=view.composed&&view.composed->engine_lighting;
    auto scene=engine_lighting?crimson::RenderScene{view.composed->settings}:native;
    if(engine_lighting){scene.instances=native.instances;scene.shadowInstances=native.shadowInstances;scene.shadowWorldId=native.shadowWorldId;scene.meshPopulationRevision=native.meshPopulationRevision;scene.giWorldId=native.giWorldId;scene.giCameraId=native.giCameraId;scene.giEnvironmentId=native.giEnvironmentId;scene.sceneMode=true;}
    scene.pinned=view.rich_frame;
    if(view.composed){const auto& engine=*view.composed->engine->world;scene.instances=view.composed->instances;scene.shadowInstances=view.composed->shadows;scene.directionalLights=engine.directional;scene.pointLights=engine.points;scene.particles=engine.particles;scene.particleMaterials=engine.particleMaterials;scene.decals=engine.decals;scene.materialContext=engine.materialContext;scene.shadowWorldId^=engine.shadowWorldId;scene.shadowResolution=engine.shadowResolution;scene.shadowPriorityOrigin=engine.shadowPriorityOrigin;scene.meshPopulationRevision^=view.composed->engine->resourceSignature;scene.pinned=view.composed;if(const auto* editor=engine_editor_view()){scene.collisionOverlays=editor->collisionOverlays;if(engine_lighting)scene.giEnvironmentId=editor->giEnvironmentId;}}
    if(!engine_lighting&&(!view.shadowing||!view.rendered)){scene.shadowWorldId=0;scene.shadowInstances={};}
    if(!engine_lighting&&!view.rendered&&view.material_debug==ViewportMaterialDebugMode::None){
      scene.previewShadedLighting.enabled=true;
      const auto& light=view.frame.light;scene.previewShadedLighting.keyDirectionView={light.key_direction_view[0],light.key_direction_view[1],light.key_direction_view[2]};
      scene.previewShadedLighting.keyIntensity=light.key_intensity;scene.previewShadedLighting.diffuseWrap=light.diffuse_wrap;scene.previewShadedLighting.groundFill=light.ground_fill;scene.previewShadedLighting.skyFill=light.sky_fill;
    }
    switch(engine_lighting?ViewportMaterialDebugMode::None:view.material_debug){
      case ViewportMaterialDebugMode::Albedo:scene.debugView=crimson::RenderDebugView::Albedo;break;
      case ViewportMaterialDebugMode::Roughness:scene.debugView=crimson::RenderDebugView::Roughness;break;
      case ViewportMaterialDebugMode::Metalness:scene.debugView=crimson::RenderDebugView::Metal;break;
      case ViewportMaterialDebugMode::Normal:scene.debugView=crimson::RenderDebugView::Normal;break;
      default:break;
    }
    return scene;
  }
#endif
  void render_engine_views(){
    if(!state_->engine_frame)return;auto frame=std::make_shared<crimson::PreparedRenderFrame>();frame->world=state_->engine_frame->world;frame->appearance=state_->engine_frame->appearance;frame->gridVisible=false;frame->resourceSignature=state_->engine_frame->resourceSignature;
    for(const auto& view:state_->engine_frame->views)if(view.identity.purpose==crimson::RenderViewPurpose::Game)frame->views.push_back(view);if(!frame->views.empty())state_->renderer.renderFrame(std::move(frame));
  }
  bool submit_views() {
    if(state_->direct11&&!state_->d3d11_external)return false;
    std::vector<std::shared_ptr<CrimsonViewState>> active;
    std::erase_if(state_->views, [](const auto &entry) { return entry.expired(); });
    for (const auto &entry : state_->views) {
      auto view = entry.lock();
      if (!view || !view->active) continue;
      const auto surface = view->surface.lock();
      if (!surface || !surface->target.value) continue;
      active.push_back(std::move(view));
    }
    for(const auto& view:active)if(view->force_rich_pick)if(const auto surface=view->surface.lock();surface&&!state_->renderer.modeling_pick_active(surface->target)&&!view->dirty){view->force_rich_pick=false;view->dirty=true;}
    const auto accepted_surfaces_available = [&] {
      return !active.empty() && std::ranges::all_of(active, [](const auto &view) {
        const auto surface = view->surface.lock();
        return surface && surface->rendered;
      });
    };
    const bool engine_dirty = state_->native_forward() && state_->engine_frame && state_->engine_revision != state_->engine_submitted_revision;
    auto* material_preview=state_->material_preview();
    const bool preview_dirty=material_preview&&(material_preview->dirty()||
        state_->submitted_preview.view!=material_preview||state_->submitted_preview.lifetime.expired());
    const bool engine_previews_dirty=state_->native_forward()&&std::ranges::any_of(state_->engine_previews,[](const auto& entry){
      return entry.record&&!entry.lifetime.expired()&&entry.revision!=entry.submitted_revision;
    });
    const bool thumbnails_dirty=state_->native_forward()&&state_->material_thumbnails&&state_->material_thumbnails->needs_recording();
    const auto record_thumbnail=[&] {return thumbnails_dirty&&state_->material_thumbnails->record(state_->renderer);};
    const auto acknowledge_thumbnail=[&](bool recorded){if(recorded)state_->material_thumbnails->submitted();};
    std::array<std::uint64_t,CrimsonHostState::kEnginePreviewCount> recorded_engine_previews{};
    const auto record_engine_previews=[&] {
      bool recorded=false;
      if(!state_->native_forward())return recorded;
      for(std::size_t index=0;index<state_->engine_previews.size();++index){
        auto& entry=state_->engine_previews[index];
        const auto binding=entry.lifetime.lock();
        if(!binding||!entry.record||entry.revision==entry.submitted_revision)continue;
        if(entry.record()){recorded_engine_previews[index]=entry.revision;recorded=true;}
      }
      return recorded;
    };
    const auto acknowledge_engine_previews=[&] {
      for(std::size_t index=0;index<state_->engine_previews.size();++index){
        auto& entry=state_->engine_previews[index];
        if(recorded_engine_previews[index]&&entry.revision==recorded_engine_previews[index]&&!entry.lifetime.expired())
          entry.submitted_revision=entry.revision;
      }
    };
    const auto record_preview=[&] {return preview_dirty&&material_preview->record(state_->renderer);};
    const auto acknowledge_preview=[&](bool recorded) {
      if(recorded){material_preview->submitted();state_->submitted_preview=state_->selected_preview;}
    };
    const auto submit_engine_only = [&] {
      if (!engine_dirty&&!preview_dirty&&!engine_previews_dirty&&!thumbnails_dirty) return false;
      if(engine_dirty)render_engine_views();
      const bool preview_recorded=record_preview();
      const bool engine_previews_recorded=record_engine_previews();
      const bool thumbnail_recorded=record_thumbnail();
      if(!engine_dirty&&!preview_recorded&&!engine_previews_recorded&&!thumbnail_recorded)return false;
      if (!state_->accept(state_->renderer.submit(&state_->completed_frame))) return false;
      if(engine_dirty)state_->engine_submitted_revision = state_->engine_revision;
      acknowledge_preview(preview_recorded);
      acknowledge_engine_previews();
      acknowledge_thumbnail(thumbnail_recorded);
      ++state_->frame_submissions;
      return true;
    };
    if (!state_->prepare_scene()) {
      const bool engine_submitted = submit_engine_only();
      return engine_submitted || accepted_surfaces_available();
    }
    const auto projection = state_->pending ? state_->pending : state_->accepted;
    if (!projection) return submit_engine_only();
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
    const bool rich_visible=state_->rich_scene&&std::ranges::any_of(active,[this](const auto& view){
      return (view->force_rich_pick||engine_editor_geometry()||view->rendered||view->material_debug!=ViewportMaterialDebugMode::None||view->mesh_grid_visible)&&(!view->solid||view->force_rich_pick||engine_editor_geometry())&&view->scene_visible;
    });
    if(rich_visible&&!state_->rich_scene->prepare(projection)){
      state_->error=state_->rich_scene->status();
      const bool engine_submitted=submit_engine_only();
      return engine_submitted||accepted_surfaces_available();
    }
#endif
    bool dirty = state_->pending != nullptr || engine_dirty || preview_dirty || engine_previews_dirty || thumbnails_dirty;
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
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
      if((view->force_rich_pick||engine_editor_geometry()||view->rendered||view->material_debug!=ViewportMaterialDebugMode::None||view->mesh_grid_visible)&&(!view->solid||view->force_rich_pick||engine_editor_geometry())&&view->scene_visible&&state_->rich_scene){
        crimson::ModelingPresentation controls;
        if(view->frame.modeling_presentation)controls.context=view->frame.modeling_presentation->context;
        controls.materialDebug=static_cast<crimson::ModelingMaterialDebug>(view->material_debug);
        controls.surfaceGrid=state_->mesh_grid;controls.surfaceGrid.enabled=controls.surfaceGrid.enabled&&view->mesh_grid_visible;
        auto rendered_settings=view->rendered_settings;
        {const auto& color=view->frame.clear_color;rendered_settings.environment.clear_color={srgb_to_linear(color.red),srgb_to_linear(color.green),srgb_to_linear(color.blue)};}
        auto prepared=state_->rich_scene->view(rendered_settings,view->camera,view->pose,view->frame.anti_aliasing,surface->target.value,controls,view->rendered||engine_scene_lighting(),state_->native_bindings);
        if(!prepared||prepared->source_generation!=projection->generation){
          state_->error=state_->rich_scene->status();
          const bool engine_submitted=submit_engine_only();
          return engine_submitted||accepted_surfaces_available();
        }
        if(!view->rich_frame||view->rich_frame->source_generation!=prepared->source_generation||
            view->rich_frame->scene().meshPopulationRevision!=prepared->scene().meshPopulationRevision)view->dirty=true;
        view->rich_frame=std::move(prepared);compose_editor_view(*view);
      }else {view->rich_frame.reset();view->composed.reset();}
#endif
      dirty = dirty || view->dirty || !surface->rendered;
    }
    if (!dirty) return true;
    if (active.empty() && !engine_dirty&&!preview_dirty&&!engine_previews_dirty&&!thumbnails_dirty) return false;
    for (const auto &view : active) {
      const auto surface=view->surface.lock();
      auto status=crimson::shared::Status{};
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
      if(view->rich_frame){const auto scene=rich_scene(*view);status=state_->renderer.prepare_rendered_view(surface->target,view->frame,scene);}
      else
#endif
      status=state_->renderer.prepare_view(surface->target,view->frame);
      if (!state_->accept(status)) {
        const bool engine_submitted = submit_engine_only();
        return engine_submitted || accepted_surfaces_available();
      }
    }
    if (engine_dirty) render_engine_views();
    const bool preview_recorded=record_preview();
    const bool engine_previews_recorded=record_engine_previews();
    const bool thumbnail_recorded=record_thumbnail();
    if(active.empty()&&!engine_dirty&&!preview_recorded&&!engine_previews_recorded&&!thumbnail_recorded)return false;
    for (const auto &view : active) {
      const auto surface = view->surface.lock();
      auto status=crimson::shared::Status{};
#if defined(QUADER_HAS_CRIMSON_RICH_SCENE)
      if(view->rich_frame){const auto scene=rich_scene(*view);status=state_->renderer.render_rendered_view(surface->target,view->frame,scene);}
      else
#endif
      status=state_->renderer.render_view(surface->target,view->frame);
      if(!state_->accept(status))return false;
    }
    if (!state_->accept(state_->renderer.submit(&state_->completed_frame))) return false;
    if (engine_dirty) state_->engine_submitted_revision = state_->engine_revision;
    acknowledge_preview(preview_recorded);
    acknowledge_engine_previews();
    acknowledge_thumbnail(thumbnail_recorded);
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

bool crimson_set_gpu_instancing_enabled(RenderHost &host, bool enabled) {
  auto *crimson_host = dynamic_cast<CrimsonRenderHost *>(&host);
  if (!crimson_host) return false;
  crimson_host->set_gpu_instancing_enabled(enabled);
  return true;
}

bool crimson_render_frame_submissions(const RenderHost &host, std::uint64_t &submissions) {
  const auto *crimson_host = dynamic_cast<const CrimsonRenderHost *>(&host);
  if (!crimson_host) return false;
  submissions = crimson_host->frame_submissions();
  return true;
}

} // namespace quader
