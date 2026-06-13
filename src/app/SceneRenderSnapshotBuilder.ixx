module;

export module quader.app.scene_render_snapshot_builder;

export import quader.render.viewport_types;
import quader.scene.scene;

export namespace quader {

[[nodiscard]] SceneRenderSnapshot build_scene_render_snapshot(const Scene& scene);

} // namespace quader
