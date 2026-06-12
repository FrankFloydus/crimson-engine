module;

#include <memory>

export module quader.render.engine_factory;

export import quader.render.debug_preview_renderer;

export namespace quader {

[[nodiscard]] std::unique_ptr<IDebugPreviewRenderer> create_default_debug_preview_renderer();

} // namespace quader
