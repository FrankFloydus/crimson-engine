module;

#include <filament/Engine.h>
#include <filament/IndirectLight.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <image/Ktx1Bundle.h>
#include <ktxreader/Ktx1Reader.h>
#include <math/mat3.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include "diagnostics/LogMacros.h"
#include "generated/QuaderDefaultEnvironmentIbl.inc"
#include "generated/QuaderDefaultEnvironmentSkybox.inc"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

module quader.filament.environment_resources;

import quader.diagnostics.logger;

namespace quader {
namespace {

constexpr float kPi = 3.14159265358979323846f;

template<typename Pointer>
Pointer require_filament_allocation(Pointer pointer, const char* resource_name)
{
    if (pointer == nullptr) {
        throw std::runtime_error("Filament failed to allocate " + std::string(resource_name) + ".");
    }

    return pointer;
}

[[nodiscard]] filament::math::float4 to_filament_color(LinearRgba color)
{
    return filament::math::float4 { color.r, color.g, color.b, color.a };
}

[[nodiscard]] filament::math::mat3f environment_rotation(float degrees)
{
    const float radians = degrees * kPi / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return filament::math::mat3f {
        filament::math::float3 { cosine, 0.0f, -sine },
        filament::math::float3 { 0.0f, 1.0f, 0.0f },
        filament::math::float3 { sine, 0.0f, cosine },
    };
}

[[nodiscard]] bool embedded_default_environment_available() noexcept
{
    return filament_assets::quader_default_environment_ibl_ktx_size > 0U &&
        filament_assets::quader_default_environment_skybox_ktx_size > 0U;
}

struct LoadedKtxTexture {
    filament::Texture* texture = nullptr;
    std::optional<std::array<filament::math::float3, 9>> irradiance_sh;
};

[[nodiscard]] LoadedKtxTexture load_ktx1_texture(
    filament::Engine& engine,
    const unsigned char* bytes,
    unsigned int byte_count,
    bool read_irradiance_sh,
    const char* resource_name)
{
    if (bytes == nullptr || byte_count == 0U) {
        throw std::runtime_error(std::string(resource_name) + " has no embedded KTX data.");
    }

    auto bundle = std::make_unique<image::Ktx1Bundle>(bytes, byte_count);

    LoadedKtxTexture loaded;
    if (read_irradiance_sh) {
        std::array<filament::math::float3, 9> sh {};
        if (bundle->getSphericalHarmonics(sh.data())) {
            loaded.irradiance_sh = sh;
        }
    }

    const bool source_is_srgb = ktxreader::Ktx1Reader::isSrgbTextureFormat(
        ktxreader::Ktx1Reader::toTextureFormat(bundle->getInfo()));
    loaded.texture = ktxreader::Ktx1Reader::createTexture(&engine, bundle.release(), source_is_srgb);
    loaded.texture = require_filament_allocation(loaded.texture, resource_name);
    return loaded;
}

} // namespace

struct FilamentEnvironmentResources::Resources {
    Resources(filament::Engine& engine_ref, filament::Scene& scene_ref)
        : engine(engine_ref)
        , scene(scene_ref)
    {
    }

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        destroy();
    }

    void apply(EnvironmentLightingSettings settings)
    {
        settings = clamp_environment_lighting_settings(settings);

        if (settings.enabled && embedded_default_environment_available()) {
            ensure_default_environment();
            if (indirect_light != nullptr) {
                indirect_light->setIntensity(settings.intensity);
                indirect_light->setRotation(environment_rotation(settings.rotation_degrees));
                scene.setIndirectLight(indirect_light);
            }
            apply_skybox(settings);
            return;
        }

        if (settings.enabled && !missing_assets_warned) {
            QUADER_LOG_WARN(
                "Default environment IBL assets are unavailable; using fallback viewport background color.");
            missing_assets_warned = true;
        }

        scene.setIndirectLight(nullptr);
        ensure_color_skybox(settings.fallback_color);
        scene.setSkybox(color_skybox);
    }

    void ensure_default_environment()
    {
        if (indirect_light != nullptr && environment_skybox != nullptr) {
            return;
        }

        LoadedKtxTexture ibl;
        LoadedKtxTexture sky;
        filament::IndirectLight* built_indirect_light = nullptr;
        filament::Skybox* built_skybox = nullptr;

        try {
            ibl = load_ktx1_texture(
                engine,
                filament_assets::quader_default_environment_ibl_ktx,
                filament_assets::quader_default_environment_ibl_ktx_size,
                true,
                "default environment IBL texture");
            sky = load_ktx1_texture(
                engine,
                filament_assets::quader_default_environment_skybox_ktx,
                filament_assets::quader_default_environment_skybox_ktx_size,
                false,
                "default environment skybox texture");

            filament::IndirectLight::Builder indirect_builder;
            indirect_builder.reflections(ibl.texture);
            if (ibl.irradiance_sh.has_value()) {
                indirect_builder.irradiance(3, ibl.irradiance_sh->data());
            }
            built_indirect_light = require_filament_allocation(
                indirect_builder.build(engine),
                "default environment indirect light");

            built_skybox = require_filament_allocation(
                filament::Skybox::Builder()
                    .environment(sky.texture)
                    .build(engine),
                "default environment skybox");
        } catch (...) {
            if (built_skybox != nullptr) {
                engine.destroy(built_skybox);
            }
            if (built_indirect_light != nullptr) {
                engine.destroy(built_indirect_light);
            }
            if (sky.texture != nullptr) {
                engine.destroy(sky.texture);
            }
            if (ibl.texture != nullptr) {
                engine.destroy(ibl.texture);
            }
            throw;
        }

        ibl_texture = ibl.texture;
        skybox_texture = sky.texture;
        indirect_light = built_indirect_light;
        environment_skybox = built_skybox;
    }

    void apply_skybox(const EnvironmentLightingSettings& settings)
    {
        if (settings.show_skybox && environment_skybox != nullptr) {
            scene.setSkybox(environment_skybox);
            return;
        }

        ensure_color_skybox(settings.fallback_color);
        scene.setSkybox(color_skybox);
    }

    void ensure_color_skybox(LinearRgba color)
    {
        color = clamp_color(color);
        if (color_skybox == nullptr) {
            color_skybox = filament::Skybox::Builder()
                .color(to_filament_color(color))
                .build(engine);
            color_skybox = require_filament_allocation(color_skybox, "fallback environment skybox");
            return;
        }

        color_skybox->setColor(to_filament_color(color));
    }

    void destroy()
    {
        scene.setIndirectLight(nullptr);
        scene.setSkybox(nullptr);

        if (environment_skybox != nullptr) {
            engine.destroy(environment_skybox);
            environment_skybox = nullptr;
        }
        if (color_skybox != nullptr) {
            engine.destroy(color_skybox);
            color_skybox = nullptr;
        }
        if (indirect_light != nullptr) {
            engine.destroy(indirect_light);
            indirect_light = nullptr;
        }
        if (skybox_texture != nullptr) {
            engine.destroy(skybox_texture);
            skybox_texture = nullptr;
        }
        if (ibl_texture != nullptr) {
            engine.destroy(ibl_texture);
            ibl_texture = nullptr;
        }
    }

    filament::Engine& engine;
    filament::Scene& scene;
    filament::Texture* ibl_texture = nullptr;
    filament::Texture* skybox_texture = nullptr;
    filament::IndirectLight* indirect_light = nullptr;
    filament::Skybox* environment_skybox = nullptr;
    filament::Skybox* color_skybox = nullptr;
    bool missing_assets_warned = false;
};

FilamentEnvironmentResources::FilamentEnvironmentResources(filament::Engine& engine, filament::Scene& scene)
    : resources_(std::make_unique<Resources>(engine, scene))
{
}

FilamentEnvironmentResources::~FilamentEnvironmentResources() = default;

void FilamentEnvironmentResources::apply(EnvironmentLightingSettings settings)
{
    resources_->apply(settings);
}

} // namespace quader
