module;

#include <filament/Engine.h>
#include <filament/Scene.h>

#include <memory>

export module quader.filament.debug_draw_renderer;

export import quader.render.debug_draw;

export namespace quader {

class FilamentDebugDrawRenderer final {
public:
    FilamentDebugDrawRenderer(filament::Engine& engine, filament::Scene& scene);

    FilamentDebugDrawRenderer(const FilamentDebugDrawRenderer&) = delete;
    FilamentDebugDrawRenderer& operator=(const FilamentDebugDrawRenderer&) = delete;

    ~FilamentDebugDrawRenderer();

    void render(const DebugDrawFrame& frame);

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace quader
