module;

module quader.filament.renderer_capabilities;

namespace quader {

RendererCapabilities filament_renderer_capabilities()
{
    RendererCapabilities capabilities = default_renderer_capabilities();
    capabilities.engine_name = "Filament";

    capabilities.post_processing.bloom.resolution.description =
        "Minor-axis resolution of the bloom buffer.";
    capabilities.post_processing.bloom.threshold.description =
        "Only blooms HDR values above the renderer's bloom threshold.";
    capabilities.post_processing.ambient_occlusion.resolution.description =
        "Ambient occlusion buffer scale; this renderer supports half or full resolution.";
    capabilities.post_processing.ambient_occlusion.bias.description =
        "World-space self-occlusion bias; ignored by ground-truth ambient occlusion.";
    capabilities.post_processing.ambient_occlusion.min_horizon_angle.description =
        "Minimum horizon angle in radians for scalable ambient occlusion.";
    capabilities.post_processing.ambient_occlusion.cone_tracing.description =
        "Screen-space cone tracing for ambient shadows from a dominant light direction.";
    capabilities.post_processing.ambient_occlusion.ground_truth_detail.description =
        "Ground-truth ambient occlusion integration controls.";
    capabilities.post_processing.ambient_occlusion.visibility_bitmasks.description =
        "Enables visibility bitmasks for ground-truth ambient occlusion; changing this can rebuild internal shaders.";
    capabilities.post_processing.ambient_occlusion.linear_thickness.description =
        "Increases ground-truth ambient occlusion thickness with distance; changing this can rebuild internal shaders.";

    capabilities.material.transparent_shadows.description =
        "Lets material alpha affect shadow opacity; this works best with transparent variance shadows and blur.";
    capabilities.material.refraction.description =
        "Controls transmissive material refraction through the environment or visible scene pixels.";
    capabilities.material.cubemap_refraction.description =
        "Uses the environment cubemap as the refraction source; this does not refract nearby scene objects.";
    capabilities.material.screen_space_refraction.description =
        "Uses visible opaque scene pixels as the refraction source.";

    capabilities.shadows.directional_modes.entries[2].label = "Transparent VSM";
    capabilities.shadows.directional_modes.entries[2].description =
        "Variance-map shadows intended for alpha-influenced shadowing.";
    capabilities.lights.point_light_range.description =
        "Maximum point light influence distance used by the renderer's point-light falloff.";
    capabilities.lights.point_light_falloff_modes.entries[0].description =
        "Keeps the authored radius visually useful for editor lighting.";
    capabilities.lights.point_light_falloff_modes.entries[1].description =
        "Uses the renderer's physically based point-light attenuation.";

    return capabilities;
}

} // namespace quader
