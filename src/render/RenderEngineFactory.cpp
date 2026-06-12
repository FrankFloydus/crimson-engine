module;

#include <memory>

module quader.render.engine_factory;

import quader.filament.render_engine;

namespace quader {

std::unique_ptr<IViewportRenderer> create_default_viewport_renderer()
{
    return std::make_unique<FilamentRenderEngine>();
}

} // namespace quader
