module;

#include <memory>

module quader.render.engine_factory;

import quader.filament.render_engine;

namespace quader {

std::unique_ptr<IDebugPreviewRenderer> create_default_debug_preview_renderer()
{
    return std::make_unique<FilamentRenderEngine>();
}

} // namespace quader
