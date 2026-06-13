module;

#include <optional>

export module quader.app.transform_gizmo;

export import quader.app.editor_input;
export import quader.render.viewport_gizmos;
export import quader.scene.scene;
export import quader.ui.viewport_surface;

export namespace quader {

class TransformGizmoController {
public:
    [[nodiscard]] bool update_tool_hotkeys(const TransformToolInputActions& actions);
    [[nodiscard]] TransformGizmoTool active_tool() const noexcept;
    [[nodiscard]] TransformGizmoSpace active_space() const noexcept;
    [[nodiscard]] bool is_dragging() const noexcept;
    [[nodiscard]] bool set_hovered_payload(std::optional<DebugDrawPickPayload> payload);
    [[nodiscard]] std::optional<TransformGizmoRequest> render_request_for_selection(
        const Scene& scene,
        std::optional<SceneEntityId> selected_entity,
        ViewportCamera camera,
        ViewportProjection projection) const;

    [[nodiscard]] bool try_begin_drag(
        DebugDrawPickPayload payload,
        const Scene& scene,
        ViewportCamera camera,
        ViewportProjection projection,
        const ViewportSurfaceResult& surface);

    struct DragUpdate {
        std::optional<SceneEntityId> entity_id;
        std::optional<TransformComponent> preview_transform;
        bool transform_changed = false;
        bool drag_finished = false;
    };

    [[nodiscard]] DragUpdate update_drag_preview(
        const Scene& scene,
        ViewportCamera camera,
        ViewportProjection projection,
        const ViewportSurfaceResult& surface);
    void cancel_drag();

private:
    struct ActiveDrag {
        SceneEntityId entity_id;
        DebugDrawPickRole role = DebugDrawPickRole::none;
        DebugDrawAxis axis = DebugDrawAxis::none;
        Vec3 axis_direction;
        Vec3 plane_axis_u;
        Vec3 plane_axis_v;
        std::optional<Vec3> start_plane_hit;
        TransformComponent start_transform;
        Vec3 origin;
        ViewportSurfacePointer start_pointer;
    };

    TransformGizmoTool active_tool_ = TransformGizmoTool::move;
    TransformGizmoSpace active_space_ = TransformGizmoSpace::global;
    std::optional<DebugDrawPickPayload> hovered_payload_;
    std::optional<ActiveDrag> active_drag_;
};

[[nodiscard]] bool scene_entity_can_scale(const Scene& scene, SceneEntityId entity_id);

} // namespace quader
