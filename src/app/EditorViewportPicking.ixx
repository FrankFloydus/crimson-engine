module;

#include <optional>

export module quader.app.editor_viewport_picking;

import quader.app.transform_gizmo;
import quader.scene.scene;
import quader.ui.viewport_surface;

export namespace quader {

struct EditorViewportPickResult {
    std::optional<SceneEntityId> entity_id;
    std::optional<DebugDrawPickPayload> debug_payload;
};

[[nodiscard]] EditorViewportPickResult pick_editor_viewport_surface(
    const Scene& scene,
    ViewportCamera camera,
    ViewportProjection projection,
    const ViewportSurfaceResult& surface,
    const std::optional<TransformGizmoRequest>& transform_gizmo);

} // namespace quader
