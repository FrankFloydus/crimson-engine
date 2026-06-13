module;

#include <array>
#include <cstddef>
#include <span>

export module quader.render.capabilities;

export import quader.render.types;

export namespace quader {

struct RendererControlCapability {
    bool supported = true;
    const char* description = "";
    const char* unsupported_reason = "";
};

template <typename Value>
struct RendererEnumOption {
    Value value {};
    const char* label = "";
    const char* description = "";
    bool supported = true;
    const char* unsupported_reason = "";
};

template <typename Option, std::size_t Capacity>
struct RendererOptionSet {
    std::array<Option, Capacity> entries {};
    std::size_t count = 0;

    [[nodiscard]] std::span<const Option> options() const
    {
        return std::span<const Option> { entries.data(), count };
    }
};

struct RendererDefaults {
    PbrMaterialSettings material;
    PostProcessingSettings post_processing;
    EnvironmentLightingSettings environment_lighting;
    DirectionalLightSettings directional_light;
    PointLightSettings point_light;
};

using BloomBlendModeOption = RendererEnumOption<BloomBlendMode>;
using BloomQualityOption = RendererEnumOption<BloomQuality>;
using AmbientOcclusionAlgorithmOption = RendererEnumOption<AmbientOcclusionAlgorithm>;
using AmbientOcclusionQualityOption = RendererEnumOption<AmbientOcclusionQuality>;
using MaterialBlendModeOption = RendererEnumOption<PbrMaterialBlendMode>;
using MaterialTransparencyModeOption = RendererEnumOption<PbrMaterialTransparencyMode>;
using MaterialCullingModeOption = RendererEnumOption<PbrMaterialCullingMode>;
using MaterialRefractionModeOption = RendererEnumOption<PbrMaterialRefractionMode>;
using MaterialRefractionTypeOption = RendererEnumOption<PbrMaterialRefractionType>;
using MaterialAbsorptionModeOption = RendererEnumOption<PbrMaterialAbsorptionMode>;
using DirectionalShadowModeOption = RendererEnumOption<DirectionalShadowMode>;
using DirectionalShadowQualityOption = RendererEnumOption<DirectionalShadowQuality>;
using PointLightFalloffModeOption = RendererEnumOption<PointLightFalloffMode>;

struct RendererBloomCapabilities {
    RendererControlCapability feature;
    RendererControlCapability strength;
    RendererControlCapability resolution;
    RendererControlCapability levels;
    RendererControlCapability blend;
    RendererControlCapability quality;
    RendererControlCapability threshold;
    RendererControlCapability highlight;
    RendererOptionSet<BloomBlendModeOption, 2> blend_modes;
    RendererOptionSet<BloomQualityOption, 3> qualities;
};

struct RendererAmbientOcclusionCapabilities {
    RendererControlCapability feature;
    RendererControlCapability radius;
    RendererControlCapability intensity;
    RendererControlCapability power;
    RendererControlCapability bias;
    RendererControlCapability resolution;
    RendererControlCapability bilateral_threshold;
    RendererControlCapability quality;
    RendererControlCapability low_pass_filter;
    RendererControlCapability upsampling;
    RendererControlCapability bent_normals;
    RendererControlCapability min_horizon_angle;
    RendererControlCapability cone_tracing;
    RendererControlCapability ground_truth_detail;
    RendererControlCapability visibility_bitmasks;
    RendererControlCapability linear_thickness;
    RendererOptionSet<AmbientOcclusionAlgorithmOption, 2> algorithms;
    RendererOptionSet<AmbientOcclusionQualityOption, 3> qualities;
};

struct RendererPostProcessingCapabilities {
    RendererControlCapability feature;
    RendererBloomCapabilities bloom;
    RendererAmbientOcclusionCapabilities ambient_occlusion;
};

struct RendererEnvironmentLightingCapabilities {
    RendererControlCapability feature;
    RendererControlCapability intensity;
    RendererControlCapability rotation;
    RendererControlCapability skybox;
    RendererControlCapability fallback_color;
};

struct RendererMaterialCapabilities {
    RendererControlCapability feature;
    RendererControlCapability transparency;
    RendererControlCapability transparent_shadows;
    RendererControlCapability alpha_to_coverage;
    RendererControlCapability refraction;
    RendererControlCapability cubemap_refraction;
    RendererControlCapability screen_space_refraction;
    RendererOptionSet<MaterialBlendModeOption, 7> blend_modes;
    RendererOptionSet<MaterialTransparencyModeOption, 3> transparency_modes;
    RendererOptionSet<MaterialCullingModeOption, 4> culling_modes;
    RendererOptionSet<MaterialRefractionModeOption, 3> refraction_modes;
    RendererOptionSet<MaterialRefractionTypeOption, 2> refraction_types;
    RendererOptionSet<MaterialAbsorptionModeOption, 2> absorption_modes;
};

struct RendererShadowCapabilities {
    RendererControlCapability directional_shadows;
    RendererControlCapability point_shadows;
    RendererControlCapability contact_shadows;
    RendererControlCapability advanced_controls;
    RendererOptionSet<DirectionalShadowModeOption, 3> directional_modes;
    RendererOptionSet<DirectionalShadowQualityOption, 4> qualities;
};

struct RendererLightCapabilities {
    RendererControlCapability directional_lights;
    RendererControlCapability point_lights;
    RendererControlCapability point_light_range;
    RendererOptionSet<PointLightFalloffModeOption, 2> point_light_falloff_modes;
};

struct RendererCapabilities {
    const char* engine_name = "Renderer";
    RendererDefaults defaults;
    RendererPostProcessingCapabilities post_processing;
    RendererEnvironmentLightingCapabilities environment_lighting;
    RendererMaterialCapabilities material;
    RendererShadowCapabilities shadows;
    RendererLightCapabilities lights;
};

[[nodiscard]] RendererCapabilities default_renderer_capabilities();

} // namespace quader
