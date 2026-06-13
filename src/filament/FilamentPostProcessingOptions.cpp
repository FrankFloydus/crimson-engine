module;

#include <filament/View.h>

module quader.filament.post_processing_options;

namespace quader {
namespace {

[[nodiscard]] filament::View::BloomOptions::BlendMode filament_bloom_blend_mode(BloomBlendMode mode)
{
    switch (mode) {
    case BloomBlendMode::add:
        return filament::View::BloomOptions::BlendMode::ADD;
    case BloomBlendMode::interpolate:
        return filament::View::BloomOptions::BlendMode::INTERPOLATE;
    }

    return filament::View::BloomOptions::BlendMode::ADD;
}

[[nodiscard]] filament::QualityLevel filament_quality(BloomQuality quality)
{
    switch (quality) {
    case BloomQuality::low:
        return filament::QualityLevel::LOW;
    case BloomQuality::medium:
        return filament::QualityLevel::MEDIUM;
    case BloomQuality::high:
        return filament::QualityLevel::HIGH;
    }

    return filament::QualityLevel::LOW;
}

[[nodiscard]] filament::QualityLevel filament_quality(AmbientOcclusionQuality quality)
{
    switch (quality) {
    case AmbientOcclusionQuality::low:
        return filament::QualityLevel::LOW;
    case AmbientOcclusionQuality::medium:
        return filament::QualityLevel::MEDIUM;
    case AmbientOcclusionQuality::high:
        return filament::QualityLevel::HIGH;
    }

    return filament::QualityLevel::LOW;
}

[[nodiscard]] filament::View::AmbientOcclusionOptions::AmbientOcclusionType filament_ambient_occlusion_type(
    AmbientOcclusionAlgorithm algorithm)
{
    switch (algorithm) {
    case AmbientOcclusionAlgorithm::scalable:
        return filament::View::AmbientOcclusionOptions::AmbientOcclusionType::SAO;
    case AmbientOcclusionAlgorithm::ground_truth:
        return filament::View::AmbientOcclusionOptions::AmbientOcclusionType::GTAO;
    }

    return filament::View::AmbientOcclusionOptions::AmbientOcclusionType::SAO;
}

} // namespace

filament::View::BloomOptions filament_bloom_options(BloomSettings settings)
{
    settings = clamp_bloom_settings(settings);

    filament::View::BloomOptions options;
    options.enabled = settings.enabled;
    options.strength = settings.strength;
    options.resolution = settings.resolution;
    options.levels = settings.levels;
    options.blendMode = filament_bloom_blend_mode(settings.blend_mode);
    options.quality = filament_quality(settings.quality);
    options.threshold = settings.threshold;
    options.highlight = settings.highlight;
    return options;
}

filament::View::AmbientOcclusionOptions filament_ambient_occlusion_options(AmbientOcclusionSettings settings)
{
    settings = clamp_ambient_occlusion_settings(settings);

    filament::View::AmbientOcclusionOptions options;
    options.aoType = filament_ambient_occlusion_type(settings.algorithm);
    options.radius = settings.radius;
    options.power = settings.power;
    options.bias = settings.bias;
    options.resolution = settings.resolution_scale;
    options.intensity = settings.intensity;
    options.bilateralThreshold = settings.bilateral_threshold;
    options.quality = filament_quality(settings.quality);
    options.lowPassFilter = filament_quality(settings.low_pass_filter);
    options.upsampling = filament_quality(settings.upsampling);
    options.enabled = settings.enabled;
    options.bentNormals = settings.bent_normals;
    options.minHorizonAngleRad = settings.min_horizon_angle_radians;

    options.ssct.lightConeRad = settings.ssct.light_cone_radians;
    options.ssct.shadowDistance = settings.ssct.shadow_distance;
    options.ssct.contactDistanceMax = settings.ssct.contact_distance_max;
    options.ssct.intensity = settings.ssct.intensity;
    options.ssct.lightDirection = {
        settings.ssct.light_direction.x,
        settings.ssct.light_direction.y,
        settings.ssct.light_direction.z,
    };
    options.ssct.depthBias = settings.ssct.depth_bias;
    options.ssct.depthSlopeBias = settings.ssct.depth_slope_bias;
    options.ssct.sampleCount = settings.ssct.sample_count;
    options.ssct.rayCount = settings.ssct.ray_count;
    options.ssct.enabled = settings.ssct.enabled;

    options.gtao.sampleSliceCount = settings.gtao.sample_slice_count;
    options.gtao.sampleStepsPerSlice = settings.gtao.sample_steps_per_slice;
    options.gtao.thicknessHeuristic = settings.gtao.thickness_heuristic;
    options.gtao.useVisibilityBitmasks = settings.gtao.use_visibility_bitmasks;
    options.gtao.constThickness = settings.gtao.const_thickness;
    options.gtao.linearThickness = settings.gtao.linear_thickness;

    return options;
}

} // namespace quader
