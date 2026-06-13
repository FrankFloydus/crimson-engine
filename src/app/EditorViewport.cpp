module;

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

module quader.app.editor_viewport;

import quader.render.viewport_gizmos;
import quader.app.editor_viewport_picking;
import quader.ui.viewport_host_panel;

namespace quader {
namespace {

constexpr uint32_t kMinimumRenderableViewportDimension = 64;

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
    const ResolvedEditorInput& resolved_input,
    bool text_input_active,
    bool viewport_hovered)
{
    const ViewportNavigationInputActions navigation_actions =
        viewport_navigation_input_actions_from(resolved_input);
    return ViewportNavigationSnapshot {
        .window_has_keyboard_focus = input.window_has_keyboard_focus(),
        .window_has_mouse_focus = input.window_has_mouse_focus(),
        .text_input_active = text_input_active,
        .hovered = viewport_hovered,
        .navigation_activate_pressed = navigation_actions.activate_pressed,
        .navigation_activate_released = navigation_actions.activate_released,
        .navigation_activate_down = navigation_actions.activate_down,
        .fly_forward = navigation_actions.fly_forward_down,
        .fly_backward = navigation_actions.fly_backward_down,
        .fly_left = navigation_actions.fly_left_down,
        .fly_right = navigation_actions.fly_right_down,
        .boost = navigation_actions.boost_down,
        .mouse_delta = input.mouse_delta(),
        .mouse_wheel_delta = input.mouse_wheel_delta(),
    };
}

[[nodiscard]] bool is_transform_gizmo_payload(DebugDrawPickPayload payload)
{
    return payload.role == DebugDrawPickRole::transform_move_axis ||
        payload.role == DebugDrawPickRole::transform_move_plane ||
        payload.role == DebugDrawPickRole::transform_rotate_axis ||
        payload.role == DebugDrawPickRole::transform_scale_axis ||
        payload.role == DebugDrawPickRole::transform_scale_plane ||
        payload.role == DebugDrawPickRole::transform_uniform_scale;
}

} // namespace

EditorViewport::~EditorViewport()
{
    release_viewports();
}

EditorViewportFrameResult EditorViewport::render_frame(
    IViewportRenderer& renderer,
    IViewportFramePresenter& viewport_presenter,
    Scene& scene,
    const Input& input,
    EditorSelectionModel& selection,
    EditorHoverModel& hover,
    EditorCommandStack& commands,
    float delta_seconds)
{
    ensure_viewports(renderer);

    upload_completed_viewport_frames(renderer, viewport_presenter);
    if (hover.validate_against(scene)) {
        mark_scene_viewports_dirty();
    }

    EditorViewportFrameResult frame_result;
    std::vector<ViewportId> visible_viewport_ids;
    bool hover_payload_updated = false;
    const bool text_input_active = viewport_presenter.text_input_active();
    const EditorInputEnvironment tool_input_environment =
        editor_input_environment_from_input(
            input,
            text_input_active,
            false,
            mouse_locked_viewport_.has_value(),
            transform_gizmo_.is_dragging());
    const ResolvedEditorInput tool_input = editor_input_.resolve(input, tool_input_environment);
    if (transform_gizmo_.update_tool_hotkeys(transform_tool_input_actions_from(tool_input))) {
        mark_scene_viewports_dirty();
    }
    render_scene_viewport_host(
        viewport_presenter,
        scene,
        input,
        selection,
        hover,
        commands,
        text_input_active,
        delta_seconds,
        visible_viewport_ids,
        hover_payload_updated,
        frame_result);
    if (!hover_payload_updated && hover.clear()) {
        (void)transform_gizmo_.set_hovered_payload(std::nullopt);
        mark_scene_viewports_dirty();
    }
    release_navigation_capture_if_hidden(visible_viewport_ids);

    request_visible_viewport_frames(renderer, scene, selection, visible_viewport_ids);

    std::vector<ViewportId> owned_viewport_ids;
    append_owned_viewport_ids(owned_viewport_ids);
    viewport_presenter.prune_viewport_textures(owned_viewport_ids);
    return frame_result;
}

bool EditorViewport::wants_relative_mouse_mode() const noexcept
{
    return mouse_locked_viewport_.has_value();
}

bool EditorViewport::has_pending_render_requests() const noexcept
{
    return std::any_of(scene_viewports_.begin(), scene_viewports_.end(), [](const ViewportInstance& viewport) {
        return viewport.request_pending();
    });
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

    scene_viewports_.clear();
    active_viewport_.reset();
    mouse_locked_viewport_.reset();
    renderer_ = nullptr;
}

void EditorViewport::append_owned_viewport_ids(std::vector<ViewportId>& viewport_ids) const
{
    viewport_ids.reserve(viewport_ids.size() + scene_viewports_.size());
    for (const ViewportInstance& viewport : scene_viewports_) {
        viewport_ids.push_back(viewport.id());
    }
}

void EditorViewport::render_scene_viewport_host(
    IViewportFramePresenter& viewport_presenter,
    Scene& scene,
    const Input& input,
    EditorSelectionModel& selection,
    EditorHoverModel& hover,
    EditorCommandStack& commands,
    bool text_input_active,
    float delta_seconds,
    std::vector<ViewportId>& visible_viewport_ids,
    bool& hover_payload_updated,
    EditorViewportFrameResult& frame_result)
{
    std::vector<ViewportPaneInput> panes;
    panes.reserve(scene_viewports_.size());
    for (const ViewportInstance& viewport : scene_viewports_) {
        panes.push_back(ViewportPaneInput {
            .id = viewport.id(),
            .title = viewport_camera_preset_name(viewport.camera_preset()),
            .texture = viewport_presenter.viewport_texture(viewport.id()),
            .performance = performance_overlay_for(viewport, delta_seconds),
            .camera_preset = viewport.camera_preset(),
            .default_camera_preset = viewport.default_camera_preset(),
            .active = active_viewport_.has_value() && *active_viewport_ == viewport.id(),
            .camera_dropdown_enabled = true,
        });
    }

    EnvironmentLightingSettings environment_settings = scene.environment_settings();
    ViewportHostPanelResult result = draw_viewport_host_panel(layout_state_, environment_settings, panes);
    if (result.environment_changed) {
        scene.set_environment_settings(environment_settings);
        frame_result.scene_changed = true;
        mark_scene_viewports_dirty();
    }
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

        update_viewport_from_surface(
            *viewport,
            scene,
            pane.surface,
            input,
            selection,
            hover,
            commands,
            text_input_active,
            delta_seconds,
            visible_viewport_ids,
            hover_payload_updated,
            frame_result);
    }
}

void EditorViewport::update_viewport_from_surface(
    ViewportInstance& viewport,
    Scene& scene,
    const ViewportSurfaceResult& surface,
    const Input& input,
    EditorSelectionModel& selection,
    EditorHoverModel& hover,
    EditorCommandStack& commands,
    bool text_input_active,
    float delta_seconds,
    std::vector<ViewportId>& visible_viewport_ids,
    bool& hover_payload_updated,
    EditorViewportFrameResult& frame_result)
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

    const bool navigation_captured =
        mouse_locked_viewport_.has_value() &&
        *mouse_locked_viewport_ == viewport.id();
    const EditorInputEnvironment viewport_input_environment =
        editor_input_environment_from_input(
            input,
            text_input_active,
            surface.hovered,
            navigation_captured,
            transform_gizmo_.is_dragging());
    const ResolvedEditorInput viewport_input = editor_input_.resolve(input, viewport_input_environment);
    const ViewportNavigationInputActions navigation_actions =
        viewport_navigation_input_actions_from(viewport_input);

    if (surface.hovered || surface.clicked || navigation_actions.activate_down) {
        active_viewport_ = viewport.id();
    }

    if (surface.hovered && surface.pointer.has_value() && !transform_gizmo_.is_dragging()) {
        const std::optional<TransformGizmoRequest> transform_gizmo =
            transform_gizmo_.render_request_for_selection(
                scene,
                selection.selected_entity(),
                viewport.current_camera(),
                viewport.current_projection());
        const EditorViewportPickResult hover_pick = pick_editor_viewport_surface(
            scene,
            viewport.current_camera(),
            viewport.current_projection(),
            surface,
            transform_gizmo);
        const std::optional<DebugDrawPickPayload> hover_payload =
            hover_pick.debug_payload.has_value() && is_transform_gizmo_payload(*hover_pick.debug_payload)
            ? hover_pick.debug_payload
            : std::nullopt;
        hover_payload_updated = true;
        const bool hover_changed = hover.set_hovered_pick(hover_pick.entity_id, hover_payload);
        const bool transform_hover_changed = transform_gizmo_.set_hovered_payload(hover.hovered_debug_payload());
        if (hover_changed || transform_hover_changed) {
            mark_scene_viewports_dirty();
        }
    }

    const TransformGizmoController::DragUpdate drag_update =
        transform_gizmo_.update_drag_preview(scene, viewport.current_camera(), viewport.current_projection(), surface);
    if (drag_update.transform_changed && drag_update.entity_id.has_value() && drag_update.preview_transform.has_value()) {
        if (commands.preview_transform_edit(scene, *drag_update.entity_id, *drag_update.preview_transform)) {
            active_viewport_ = viewport.id();
            frame_result.scene_changed = true;
            mark_scene_viewports_dirty();
        }
    }
    if (drag_update.drag_finished) {
        if (commands.finish_transform_edit(scene)) {
            frame_result.scene_changed = true;
            mark_scene_viewports_dirty();
        }
    }

    if (surface.left_clicked && !surface.dragging && !transform_gizmo_.is_dragging()) {
        const std::optional<TransformGizmoRequest> transform_gizmo =
            transform_gizmo_.render_request_for_selection(
                scene,
                selection.selected_entity(),
                viewport.current_camera(),
                viewport.current_projection());
        const EditorViewportPickResult pick = pick_editor_viewport_surface(
            scene,
            viewport.current_camera(),
            viewport.current_projection(),
            surface,
            transform_gizmo);
        if (pick.debug_payload.has_value() &&
            transform_gizmo_.try_begin_drag(
                *pick.debug_payload,
                scene,
                viewport.current_camera(),
            viewport.current_projection(),
            surface)) {
            const SceneEntityId owner { pick.debug_payload->owner_entity_id };
            (void)selection.select_entity(owner);
            (void)commands.begin_transform_edit(scene, owner);
            active_viewport_ = viewport.id();
        } else {
            (void)selection.select_entity(pick.entity_id.value_or(SceneEntityId {}));
            (void)commands.cancel_transform_edit(scene);
        }
        mark_scene_viewports_dirty();
    }

    const ViewportNavigationSnapshot navigation_snapshot =
        navigation_snapshot_from(input, viewport_input, text_input_active, surface.hovered);
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
    const EditorSelectionModel& selection,
    const std::vector<ViewportId>& visible_viewport_ids)
{
    for (ViewportId viewport_id : visible_viewport_ids) {
        ViewportInstance* viewport = find_viewport(viewport_id);
        if (viewport == nullptr) {
            continue;
        }

        const std::optional<TransformGizmoRequest> transform_gizmo =
            transform_gizmo_.render_request_for_selection(
                scene,
                selection.selected_entity(),
                viewport->current_camera(),
                viewport->current_projection());
        std::optional<ViewportRenderRequest> request =
            viewport->try_begin_render_request(scene, transform_gizmo);
        if (!request.has_value()) {
            continue;
        }

        if (!renderer.request_viewport_frame(*request)) {
            viewport->fail_pending_request();
        }
    }
}

void EditorViewport::upload_completed_viewport_frames(
    IViewportRenderer& renderer,
    IViewportFramePresenter& viewport_presenter)
{
    auto consume_viewport = [&renderer, &viewport_presenter](ViewportInstance& viewport) {
        std::optional<ViewportRenderFrame> frame = renderer.try_consume_viewport_frame(viewport.id());
        if (!frame.has_value()) {
            return;
        }

        const ViewportFrameAcceptance acceptance = viewport.accept_frame(*frame);
        if (!acceptance.accepted) {
            return;
        }

        (void)viewport_presenter.present_viewport_frame(*frame);
    };

    for (ViewportInstance& viewport : scene_viewports_) {
        consume_viewport(viewport);
    }
}

ViewportInstance* EditorViewport::find_viewport(ViewportId viewport_id)
{
    for (ViewportInstance& viewport : scene_viewports_) {
        if (viewport.id() == viewport_id) {
            return &viewport;
        }
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

void EditorViewport::mark_scene_viewports_dirty()
{
    for (ViewportInstance& viewport : scene_viewports_) {
        viewport.mark_dirty();
    }
}

void EditorViewport::mark_all_viewports_dirty()
{
    mark_scene_viewports_dirty();
}

} // namespace quader
