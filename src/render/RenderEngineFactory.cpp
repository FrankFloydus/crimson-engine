module;

#include <memory>

module quader.render.engine_factory;

import quader.filament.render_engine;

namespace quader {

std::unique_ptr<ISceneViewportRenderer> create_default_scene_viewport_renderer()
{
    return std::make_unique<FilamentRenderEngine>();
}

} // namespace quader
