module;

#include <memory>

export module quader.render.engine_factory;

export import quader.render.scene_viewport_renderer;

export namespace quader {

[[nodiscard]] std::unique_ptr<ISceneViewportRenderer> create_default_scene_viewport_renderer();

} // namespace quader
