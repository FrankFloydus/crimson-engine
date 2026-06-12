module;

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

module quader.app.editor_viewport;

import quader.ui.viewport_host_panel;

namespace quader {
namespace {

constexpr uint32_t kMinimumRenderableViewportDimension = 64;

bool is_viewport_movement_key_down(const Input& input)
{
    return input.is_key_down(AppKey::w) ||
        input.is_key_down(AppKey::a) ||
        input.is_key_down(AppKey::s) ||
        input.is_key_down(AppKey::d) ||
        input.is_key_down(AppKey::q) ||
        input.is_key_down(AppKey::e);
}

ViewportId create_viewport_or_throw(IViewportRenderer& renderer, const char* viewport_name)
{
    std::optional<ViewportId> viewport_id = renderer.try_create_viewport();
    if (!viewport_id.has_value() || !viewport_id->is_valid()) {
        throw std::runtime_error(std::string("Renderer failed to create viewport: ") + viewport_name);
    }

    return *viewport_id;
}

ViewportNavigationSnapshot navigation_snapshot_from(
    const Input& input,
    bool viewport_hovered)
{
    return ViewportNavigationSnapshot {
        .window_has_keyboard_focus = input.window_has_keyboard_focus(),
        .window_has_mouse_focus = input.window_has_mouse_focus(),
        .text_input_active = ImGui::GetIO().WantTextInput,
        .hovered = viewport_hovered,
        .right_mouse_pressed = input.was_mouse_button_pressed(AppMouseButton::right),
        .right_mouse_released = input.was_mouse_button_released(AppMouseButton::right),
        .right_mouse_down = input.is_mouse_button_down(AppMouseButton::right),
        .move_forward = input.is_key_down(AppKey::w),
        .move_backward = input.is_key_down(AppKey::s),
        .move_left = input.is_key_down(AppKey::a),
        .move_right = input.is_key_down(AppKey::d),
        .move_up = input.is_key_down(AppKey::e),
        .move_down = input.is_key_down(AppKey::q),
        .boost = input.is_key_down(AppKey::left_shift) || input.is_key_down(AppKey::right_shift),
        .mouse_delta = input.mouse_delta(),
        .mouse_wheel_delta = input.mouse_wheel_delta(),
    };
}

void upload_readback_frame_if_available(
    const ViewportRenderFrame& frame,
    ImGuiLayer& imgui)
{
    if (!frame.payload.temporary_readback.has_value() || !frame.payload.temporary_readback->has_pixels()) {
        return;
    }

    const TemporaryRgba8ReadbackFrame& readback = *frame.payload.temporary_readback;
    imgui.upload_viewport_texture(
        frame.viewport_id,
        readback.extent.width,
        readback.extent.height,
        readback.rgba.data());
}

} // namespace

EditorViewport::~EditorViewport()
{
    release_viewports();
}

void EditorViewport::render_frame(
    IViewportRenderer& renderer,
    ImGuiLayer& imgui,
    const Scene& scene,
    const Input& input,
    float delta_seconds)
{
    ensure_viewports(renderer);

    upload_completed_viewport_frames(renderer, imgui);

    std::vector<ViewportId> visible_viewport_ids;
    render_scene_viewport_host(imgui, input, delta_seconds, visible_viewport_ids);
    render_preview_viewport_panel(imgui, input, delta_seconds, visible_viewport_ids);
    release_navigation_capture_if_hidden(visible_viewport_ids);

    request_visible_viewport_frames(renderer, scene, visible_viewport_ids);

    std::vector<ViewportId> owned_viewport_ids;
    append_owned_viewport_ids(owned_viewport_ids);
    imgui.prune_viewport_textures(owned_viewport_ids);
}

bool EditorViewport::wants_relative_mouse_mode() const noexcept
{
    return mouse_locked_viewport_.has_value();
}

void EditorViewport::mark_render_state_dirty()
{
    mark_all_viewports_dirty();
}

void EditorViewport::ensure_viewports(IViewportRenderer& renderer)
{
    if (renderer_ != nullptr) {
        return;
    }

    renderer_ = &renderer;

    scene_viewports_.reserve(4);
    scene_viewports_.push_back(ViewportInstance::scene_viewport(
        create_viewport_or_throw(renderer, "Perspective"),
        "Perspective",
        ViewportCameraPreset::perspective));
    scene_viewports_.push_back(ViewportInstance::scene_viewport(
        create_viewport_or_throw(renderer, "Top"),
        "Top",
        ViewportCameraPreset::top));
    scene_viewports_.push_back(ViewportInstance::scene_viewport(
        create_viewport_or_throw(renderer, "Side"),
        "Side",
        ViewportCameraPreset::side));
    scene_viewports_.push_back(ViewportInstance::scene_viewport(
        create_viewport_or_throw(renderer, "Front"),
        "Front",
        ViewportCameraPreset::front));
    preview_viewport_ = ViewportInstance::preview_viewport(
        create_viewport_or_throw(renderer, "Material Preview"),
        "Material Preview");

    if (!scene_viewports_.empty()) {
        active_viewport_ = scene_viewports_.front().id();
    }
}

void EditorViewport::release_viewports()
{
    if (renderer_ == nullptr) {
        return;
    }

    for (const ViewportInstance& viewport : scene_viewports_) {
        renderer_->release_viewport(viewport.id());
    }

    if (preview_viewport_.has_value()) {
        renderer_->release_viewport(preview_viewport_->id());
    }

    scene_viewports_.clear();
    preview_viewport_.reset();
    active_viewport_.reset();
    mouse_locked_viewport_.reset();
    renderer_ = nullptr;
}

void EditorViewport::append_owned_viewport_ids(std::vector<ViewportId>& viewport_ids) const
{
    viewport_ids.reserve(viewport_ids.size() + scene_viewports_.size() + (preview_viewport_.has_value() ? 1U : 0U));
    for (const ViewportInstance& viewport : scene_viewports_) {
        viewport_ids.push_back(viewport.id());
    }
    if (preview_viewport_.has_value()) {
        viewport_ids.push_back(preview_viewport_->id());
    }
}

void EditorViewport::render_scene_viewport_host(
    ImGuiLayer& imgui,
    const Input& input,
    float delta_seconds,
    std::vector<ViewportId>& visible_viewport_ids)
{
    std::vector<ViewportPaneInput> panes;
    panes.reserve(scene_viewports_.size());
    for (const ViewportInstance& viewport : scene_viewports_) {
        panes.push_back(ViewportPaneInput {
            .id = viewport.id(),
            .title = viewport.title(),
            .texture = imgui.viewport_texture(viewport.id()),
            .performance = performance_overlay_for(viewport, delta_seconds),
            .camera_preset = viewport.camera_preset(),
            .default_camera_preset = viewport.default_camera_preset(),
            .active = active_viewport_.has_value() && *active_viewport_ == viewport.id(),
            .camera_dropdown_enabled = true,
        });
    }

    ViewportHostPanelResult result = draw_viewport_host_panel(layout_state_, panes);
    if (result.active_viewport.has_value()) {
        active_viewport_ = *result.active_viewport;
    }

    for (const ViewportPaneResult& pane : result.panes) {
        ViewportInstance* viewport = find_viewport(pane.id);
        if (viewport == nullptr) {
            continue;
        }

        if (pane.requested_camera_preset.has_value()) {
            viewport->set_camera_preset(*pane.requested_camera_preset);
        }

        update_viewport_from_surface(*viewport, pane.surface, input, delta_seconds, visible_viewport_ids);
    }
}

void EditorViewport::render_preview_viewport_panel(
    ImGuiLayer& imgui,
    const Input& input,
    float delta_seconds,
    std::vector<ViewportId>& visible_viewport_ids)
{
    if (!preview_viewport_.has_value()) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(888.0f, 56.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 260.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Preview Viewport")) {
        ImGui::TextUnformatted(preview_viewport_->title().data(), preview_viewport_->title().data() + preview_viewport_->title().size());
        ImGui::Separator();

        ViewportSurfaceResult surface = draw_viewport_surface(ViewportSurfaceInput {
            .id = preview_viewport_->id(),
            .texture = imgui.viewport_texture(preview_viewport_->id()),
            .performance = performance_overlay_for(*preview_viewport_, delta_seconds),
            .empty_text = "Waiting for preview pixels.",
            .active = active_viewport_.has_value() && *active_viewport_ == preview_viewport_->id(),
        });

        update_viewport_from_surface(*preview_viewport_, surface, input, delta_seconds, visible_viewport_ids);
    }

    ImGui::End();
}

void EditorViewport::update_viewport_from_surface(
    ViewportInstance& viewport,
    const ViewportSurfaceResult& surface,
    const Input& input,
    float delta_seconds,
    std::vector<ViewportId>& visible_viewport_ids)
{
    if (!surface.visible) {
        return;
    }

    if (surface.requested_extent.width < kMinimumRenderableViewportDimension ||
        surface.requested_extent.height < kMinimumRenderableViewportDimension) {
        return;
    }

    visible_viewport_ids.push_back(viewport.id());
    viewport.set_requested_extent(surface.requested_extent);

    if (surface.hovered || surface.clicked || (surface.hovered && is_viewport_movement_key_down(input))) {
        active_viewport_ = viewport.id();
    }

    const ViewportNavigationSnapshot navigation_snapshot = navigation_snapshot_from(input, surface.hovered);
    if (mouse_locked_viewport_ == viewport.id() &&
        !viewport.should_keep_navigation_mouse_lock(navigation_snapshot)) {
        mouse_locked_viewport_.reset();
    }

    if (!mouse_locked_viewport_.has_value() &&
        viewport.should_start_navigation_mouse_lock(navigation_snapshot)) {
        mouse_locked_viewport_ = viewport.id();
    }

    const bool navigation_mouse_locked =
        mouse_locked_viewport_.has_value() &&
        *mouse_locked_viewport_ == viewport.id();
    const bool navigation_changed =
        viewport.update_navigation(navigation_snapshot, navigation_mouse_locked, delta_seconds);
    (void)navigation_changed;
    if (navigation_mouse_locked) {
        active_viewport_ = viewport.id();
    }
}

void EditorViewport::release_navigation_capture_if_hidden(const std::vector<ViewportId>& visible_viewport_ids)
{
    if (!mouse_locked_viewport_.has_value()) {
        return;
    }

    const bool captured_viewport_is_visible =
        std::find(visible_viewport_ids.begin(), visible_viewport_ids.end(), *mouse_locked_viewport_) !=
        visible_viewport_ids.end();
    if (!captured_viewport_is_visible) {
        mouse_locked_viewport_.reset();
    }
}

void EditorViewport::request_visible_viewport_frames(
    IViewportRenderer& renderer,
    const Scene& scene,
    const std::vector<ViewportId>& visible_viewport_ids)
{
    const PbrMaterialSettings preview_material = preview_material_from_scene(scene);
    const DirectionalLightSettings preview_light = preview_light_from_scene(scene);

    for (ViewportId viewport_id : visible_viewport_ids) {
        ViewportInstance* viewport = find_viewport(viewport_id);
        if (viewport == nullptr) {
            continue;
        }

        std::optional<ViewportRenderRequest> request =
            viewport->try_begin_render_request(scene, preview_material, preview_light);
        if (!request.has_value()) {
            continue;
        }

        if (!renderer.request_viewport_frame(*request)) {
            viewport->fail_pending_request();
        }
    }
}

void EditorViewport::upload_completed_viewport_frames(IViewportRenderer& renderer, ImGuiLayer& imgui)
{
    auto consume_viewport = [&renderer, &imgui](ViewportInstance& viewport) {
        std::optional<ViewportRenderFrame> frame = renderer.try_consume_viewport_frame(viewport.id());
        if (!frame.has_value()) {
            return;
        }

        const ViewportFrameAcceptance acceptance = viewport.accept_frame(*frame);
        if (!acceptance.accepted) {
            return;
        }

        upload_readback_frame_if_available(*frame, imgui);
    };

    for (ViewportInstance& viewport : scene_viewports_) {
        consume_viewport(viewport);
    }
    if (preview_viewport_.has_value()) {
        consume_viewport(*preview_viewport_);
    }
}

ViewportInstance* EditorViewport::find_viewport(ViewportId viewport_id)
{
    for (ViewportInstance& viewport : scene_viewports_) {
        if (viewport.id() == viewport_id) {
            return &viewport;
        }
    }

    if (preview_viewport_.has_value() && preview_viewport_->id() == viewport_id) {
        return &*preview_viewport_;
    }

    return nullptr;
}

const ViewportInstance* EditorViewport::find_viewport(ViewportId viewport_id) const
{
    for (const ViewportInstance& viewport : scene_viewports_) {
        if (viewport.id() == viewport_id) {
            return &viewport;
        }
    }

    if (preview_viewport_.has_value() && preview_viewport_->id() == viewport_id) {
        return &*preview_viewport_;
    }

    return nullptr;
}

ViewportPerformanceOverlay EditorViewport::performance_overlay_for(
    const ViewportInstance& viewport,
    float delta_seconds) const
{
    return ViewportPerformanceOverlay {
        .enabled = true,
        .frame_delta_seconds = delta_seconds,
        .render_request_pending = viewport.request_pending(),
        .dirty = viewport.is_dirty(),
    };
}

PbrMaterialSettings EditorViewport::preview_material_from_scene(const Scene& scene) const
{
    if (const SceneMeshComponent* mesh = first_mesh_component(scene)) {
        return mesh->material;
    }

    return {};
}

DirectionalLightSettings EditorViewport::preview_light_from_scene(const Scene& scene) const
{
    if (const SceneDirectionalLightComponent* light = first_directional_light_component(scene)) {
        return light->settings;
    }

    return {};
}

void EditorViewport::mark_scene_viewports_dirty()
{
    for (ViewportInstance& viewport : scene_viewports_) {
        viewport.mark_dirty();
    }
}

void EditorViewport::mark_all_viewports_dirty()
{
    mark_scene_viewports_dirty();
    if (preview_viewport_.has_value()) {
        preview_viewport_->mark_dirty();
    }
}

} // namespace quader
