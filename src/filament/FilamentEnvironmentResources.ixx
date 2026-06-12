module;

#include <filament/Engine.h>
#include <filament/Scene.h>

#include <memory>

export module quader.filament.environment_resources;

export import quader.render.types;

export namespace quader {

class FilamentEnvironmentResources final {
public:
    FilamentEnvironmentResources(filament::Engine& engine, filament::Scene& scene);

    FilamentEnvironmentResources(const FilamentEnvironmentResources&) = delete;
    FilamentEnvironmentResources& operator=(const FilamentEnvironmentResources&) = delete;

    ~FilamentEnvironmentResources();

    void apply(EnvironmentLightingSettings settings);

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace quader
