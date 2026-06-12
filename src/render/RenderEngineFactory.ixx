module;

#include <memory>

export module quader.render.engine_factory;

export import quader.render.viewport_renderer;

export namespace quader {

[[nodiscard]] std::unique_ptr<IViewportRenderer> create_default_viewport_renderer();

} // namespace quader
