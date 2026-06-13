module;

#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>

#include <cstdint>
#include <memory>

export module quader.filament.scene_viewport_materials;

export import quader.render.viewport_types;

export namespace quader {

enum class FilamentPbrMaterialVariant : uint8_t {
    opaque,
    opaque_transparent_shadow,
    transparent,
    transparent_shadow,
    fade,
    fade_shadow,
    masked,
    masked_no_alpha_to_coverage,
    masked_shadow,
    masked_no_alpha_to_coverage_shadow,
    add,
    add_shadow,
    multiply,
    multiply_shadow,
    screen,
    screen_shadow,
    refract_cubemap_solid,
    refract_cubemap_solid_shadow,
    refract_cubemap_thin,
    refract_cubemap_thin_shadow,
    refract_screen_space_solid,
    refract_screen_space_solid_shadow,
    refract_screen_space_thin,
    refract_screen_space_thin_shadow,
    count,
};

[[nodiscard]] bool filament_pbr_material_uses_screen_space_refraction(
    const PbrMaterialSettings& settings);
[[nodiscard]] FilamentPbrMaterialVariant filament_pbr_material_variant_for(
    PbrMaterialSettings settings);

class FilamentPbrMaterialResources final {
public:
    explicit FilamentPbrMaterialResources(filament::Engine& engine);
    FilamentPbrMaterialResources(const FilamentPbrMaterialResources&) = delete;
    FilamentPbrMaterialResources& operator=(const FilamentPbrMaterialResources&) = delete;
    ~FilamentPbrMaterialResources();

    [[nodiscard]] filament::Material* ensure_material(FilamentPbrMaterialVariant variant);
    [[nodiscard]] filament::MaterialInstance* create_mesh_material_instance(
        ViewportMeshKind mesh_kind,
        FilamentPbrMaterialVariant variant);
    void apply_settings(
        filament::MaterialInstance& instance,
        FilamentPbrMaterialVariant variant,
        PbrMaterialSettings settings);
    void destroy();

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace quader
