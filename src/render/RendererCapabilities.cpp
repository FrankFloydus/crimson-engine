module;

module quader.render.capabilities;

namespace quader {
namespace {

void populate_common_options(RendererCapabilities& capabilities)
{
    capabilities.post_processing.bloom.blend_modes = {
        .entries = {
            BloomBlendModeOption {
                .value = BloomBlendMode::add,
                .label = "Add",
                .description = "Adds bloom back into the rendered scene.",
            },
            BloomBlendModeOption {
                .value = BloomBlendMode::interpolate,
                .label = "Interpolate",
                .description = "Blends between the rendered scene and the bloom contribution.",
            },
        },
        .count = 2,
    };
    capabilities.post_processing.bloom.qualities = {
        .entries = {
            BloomQualityOption {
                .value = BloomQuality::low,
                .label = "Low",
                .description = "Lowest bloom filtering cost.",
            },
            BloomQualityOption {
                .value = BloomQuality::medium,
                .label = "Medium",
                .description = "Balanced bloom filtering quality.",
            },
            BloomQualityOption {
                .value = BloomQuality::high,
                .label = "High",
                .description = "Highest bloom filtering quality.",
            },
        },
        .count = 3,
    };
    capabilities.post_processing.ambient_occlusion.algorithms = {
        .entries = {
            AmbientOcclusionAlgorithmOption {
                .value = AmbientOcclusionAlgorithm::scalable,
                .label = "Scalable",
                .description = "Scalable screen-space ambient occlusion.",
            },
            AmbientOcclusionAlgorithmOption {
                .value = AmbientOcclusionAlgorithm::ground_truth,
                .label = "Ground Truth",
                .description = "Ground-truth-based screen-space ambient occlusion.",
            },
        },
        .count = 2,
    };
    capabilities.post_processing.ambient_occlusion.qualities = {
        .entries = {
            AmbientOcclusionQualityOption {
                .value = AmbientOcclusionQuality::low,
                .label = "Low",
                .description = "Lowest ambient occlusion sampling and filtering cost.",
            },
            AmbientOcclusionQualityOption {
                .value = AmbientOcclusionQuality::medium,
                .label = "Medium",
                .description = "Balanced ambient occlusion sampling and filtering quality.",
            },
            AmbientOcclusionQualityOption {
                .value = AmbientOcclusionQuality::high,
                .label = "High",
                .description = "Highest ambient occlusion sampling and filtering quality.",
            },
        },
        .count = 3,
    };

    capabilities.material.blend_modes = {
        .entries = {
            MaterialBlendModeOption {
                .value = PbrMaterialBlendMode::opaque,
                .label = "Opaque",
                .description = "Renders the material as fully opaque.",
            },
            MaterialBlendModeOption {
                .value = PbrMaterialBlendMode::transparent,
                .label = "Transparent",
                .description = "Alpha blends the material with pixels behind it.",
            },
            MaterialBlendModeOption {
                .value = PbrMaterialBlendMode::fade,
                .label = "Fade",
                .description = "Fades the material without preserving physically based transparency.",
            },
            MaterialBlendModeOption {
                .value = PbrMaterialBlendMode::masked,
                .label = "Masked",
                .description = "Discards pixels below the alpha mask threshold.",
            },
            MaterialBlendModeOption {
                .value = PbrMaterialBlendMode::add,
                .label = "Add",
                .description = "Adds the material contribution to pixels behind it.",
            },
            MaterialBlendModeOption {
                .value = PbrMaterialBlendMode::multiply,
                .label = "Multiply",
                .description = "Multiplies the material with pixels behind it.",
            },
            MaterialBlendModeOption {
                .value = PbrMaterialBlendMode::screen,
                .label = "Screen",
                .description = "Screens the material over pixels behind it.",
            },
        },
        .count = 7,
    };
    capabilities.material.transparency_modes = {
        .entries = {
            MaterialTransparencyModeOption {
                .value = PbrMaterialTransparencyMode::default_sorting,
                .label = "Default",
                .description = "Uses the renderer's default transparent sorting path.",
            },
            MaterialTransparencyModeOption {
                .value = PbrMaterialTransparencyMode::two_passes_one_side,
                .label = "Two Passes One Side",
                .description = "Draws one side through a depth pass and a color pass.",
            },
            MaterialTransparencyModeOption {
                .value = PbrMaterialTransparencyMode::two_passes_two_sides,
                .label = "Two Passes Two Sides",
                .description = "Draws back faces and front faces separately to reduce sorting artifacts.",
            },
        },
        .count = 3,
    };
    capabilities.material.culling_modes = {
        .entries = {
            MaterialCullingModeOption {
                .value = PbrMaterialCullingMode::none,
                .label = "None",
                .description = "Renders both face orientations.",
            },
            MaterialCullingModeOption {
                .value = PbrMaterialCullingMode::front,
                .label = "Front",
                .description = "Skips front-facing triangles.",
            },
            MaterialCullingModeOption {
                .value = PbrMaterialCullingMode::back,
                .label = "Back",
                .description = "Skips back-facing triangles.",
            },
            MaterialCullingModeOption {
                .value = PbrMaterialCullingMode::front_and_back,
                .label = "Front And Back",
                .description = "Skips all rasterized faces.",
            },
        },
        .count = 4,
    };
    capabilities.material.refraction_modes = {
        .entries = {
            MaterialRefractionModeOption {
                .value = PbrMaterialRefractionMode::none,
                .label = "None",
                .description = "Disables material refraction.",
            },
            MaterialRefractionModeOption {
                .value = PbrMaterialRefractionMode::cubemap,
                .label = "Environment",
                .description = "Refracts the distant environment lighting.",
            },
            MaterialRefractionModeOption {
                .value = PbrMaterialRefractionMode::screen_space,
                .label = "Screen Space",
                .description = "Refracts visible opaque scene pixels.",
            },
        },
        .count = 3,
    };
    capabilities.material.refraction_types = {
        .entries = {
            MaterialRefractionTypeOption {
                .value = PbrMaterialRefractionType::solid,
                .label = "Solid",
                .description = "Treats the material as a thicker refractive volume.",
            },
            MaterialRefractionTypeOption {
                .value = PbrMaterialRefractionType::thin,
                .label = "Thin",
                .description = "Treats the material as a thin refractive surface.",
            },
        },
        .count = 2,
    };
    capabilities.material.absorption_modes = {
        .entries = {
            MaterialAbsorptionModeOption {
                .value = PbrMaterialAbsorptionMode::coefficients,
                .label = "Coefficients",
                .description = "Authors direct absorption coefficients.",
            },
            MaterialAbsorptionModeOption {
                .value = PbrMaterialAbsorptionMode::transmittance_color,
                .label = "Transmittance Color",
                .description = "Derives absorption from a target transmitted color and distance.",
            },
        },
        .count = 2,
    };

    capabilities.shadows.directional_modes = {
        .entries = {
            DirectionalShadowModeOption {
                .value = DirectionalShadowMode::crisp,
                .label = "Crisp",
                .description = "Hard directional shadows.",
            },
            DirectionalShadowModeOption {
                .value = DirectionalShadowMode::soft,
                .label = "Soft",
                .description = "Directional shadows with broader penumbra controls.",
            },
            DirectionalShadowModeOption {
                .value = DirectionalShadowMode::vsm_transparent,
                .label = "Transparent",
                .description = "Variance-map shadows suitable for alpha-influenced shadowing.",
            },
        },
        .count = 3,
    };
    capabilities.shadows.qualities = {
        .entries = {
            DirectionalShadowQualityOption {
                .value = DirectionalShadowQuality::low,
                .label = "Low",
                .description = "Lower shadow-map detail.",
            },
            DirectionalShadowQualityOption {
                .value = DirectionalShadowQuality::medium,
                .label = "Medium",
                .description = "Balanced shadow-map detail.",
            },
            DirectionalShadowQualityOption {
                .value = DirectionalShadowQuality::high,
                .label = "High",
                .description = "High shadow-map detail.",
            },
            DirectionalShadowQualityOption {
                .value = DirectionalShadowQuality::ultra,
                .label = "Ultra",
                .description = "Maximum shadow-map detail.",
            },
        },
        .count = 4,
    };

    capabilities.lights.point_light_falloff_modes = {
        .entries = {
            PointLightFalloffModeOption {
                .value = PointLightFalloffMode::linear,
                .label = "Linear",
                .description = "Keeps the authored radius visually useful for editor lighting.",
            },
            PointLightFalloffModeOption {
                .value = PointLightFalloffMode::inverse_squared,
                .label = "Inverse Squared",
                .description = "Uses physically based point-light attenuation.",
            },
        },
        .count = 2,
    };
}

} // namespace

RendererCapabilities default_renderer_capabilities()
{
    RendererCapabilities capabilities;
    capabilities.engine_name = "Renderer";
    capabilities.defaults = RendererDefaults {};

    capabilities.post_processing.feature = RendererControlCapability {
        .description = "Edits post-processing effects applied to rendered viewports.",
    };
    capabilities.post_processing.bloom.feature = RendererControlCapability {
        .description = "Adds a glow around high-luminance pixels, including emissive materials.",
    };
    capabilities.post_processing.bloom.strength = RendererControlCapability {
        .description = "How much bloom is added back into the scene.",
    };
    capabilities.post_processing.bloom.resolution = RendererControlCapability {
        .description = "Minor-axis resolution of the bloom buffer.",
    };
    capabilities.post_processing.bloom.levels = RendererControlCapability {
        .description = "Number of blur levels used to spread the glow.",
    };
    capabilities.post_processing.bloom.blend = RendererControlCapability {
        .description = "Chooses how bloom is blended with the rendered scene.",
    };
    capabilities.post_processing.bloom.quality = RendererControlCapability {
        .description = "Controls the filter quality used by the bloom pass.",
    };
    capabilities.post_processing.bloom.threshold = RendererControlCapability {
        .description = "Only blooms HDR values above the active renderer's bloom threshold.",
    };
    capabilities.post_processing.bloom.highlight = RendererControlCapability {
        .description = "Caps very bright highlights before bloom.",
    };

    capabilities.post_processing.ambient_occlusion.feature = RendererControlCapability {
        .description = "Adds screen-space occlusion to indirect lighting.",
    };
    capabilities.post_processing.ambient_occlusion.radius = RendererControlCapability {
        .description = "World-space occlusion radius in meters.",
    };
    capabilities.post_processing.ambient_occlusion.intensity = RendererControlCapability {
        .description = "Strength of the ambient occlusion pass.",
    };
    capabilities.post_processing.ambient_occlusion.power = RendererControlCapability {
        .description = "Contrast curve applied to the occlusion term.",
    };
    capabilities.post_processing.ambient_occlusion.bias = RendererControlCapability {
        .description = "World-space self-occlusion bias.",
    };
    capabilities.post_processing.ambient_occlusion.resolution = RendererControlCapability {
        .description = "Ambient occlusion buffer scale.",
    };
    capabilities.post_processing.ambient_occlusion.bilateral_threshold = RendererControlCapability {
        .description = "Depth threshold used by the bilateral filter.",
    };
    capabilities.post_processing.ambient_occlusion.quality = RendererControlCapability {
        .description = "Controls ambient occlusion sampling and filtering quality.",
    };
    capabilities.post_processing.ambient_occlusion.low_pass_filter = RendererControlCapability {
        .description = "Controls ambient occlusion smoothness.",
    };
    capabilities.post_processing.ambient_occlusion.upsampling = RendererControlCapability {
        .description = "Controls ambient occlusion buffer upsampling quality.",
    };
    capabilities.post_processing.ambient_occlusion.bent_normals = RendererControlCapability {
        .description = "Computes bent normals for more accurate indirect lighting.",
    };
    capabilities.post_processing.ambient_occlusion.min_horizon_angle = RendererControlCapability {
        .description = "Minimum horizon angle in radians for scalable ambient occlusion.",
    };
    capabilities.post_processing.ambient_occlusion.cone_tracing = RendererControlCapability {
        .description = "Adds cone-traced ambient shadows from a dominant light direction.",
    };
    capabilities.post_processing.ambient_occlusion.ground_truth_detail = RendererControlCapability {
        .description = "Fine controls for ground-truth ambient occlusion integration.",
    };
    capabilities.post_processing.ambient_occlusion.visibility_bitmasks = RendererControlCapability {
        .description = "Enables the renderer's visibility-bitmask ambient occlusion path.",
    };
    capabilities.post_processing.ambient_occlusion.linear_thickness = RendererControlCapability {
        .description = "Increases ambient occlusion thickness with distance.",
    };

    capabilities.environment_lighting.feature = RendererControlCapability {
        .description = "Edits the scene environment lighting used by every viewport.",
    };
    capabilities.environment_lighting.intensity = RendererControlCapability {
        .description = "Scales the indirect light in lux.",
    };
    capabilities.environment_lighting.rotation = RendererControlCapability {
        .description = "Rotates the environment around the vertical axis.",
    };
    capabilities.environment_lighting.skybox = RendererControlCapability {
        .description = "Shows the environment as the viewport background.",
    };
    capabilities.environment_lighting.fallback_color = RendererControlCapability {
        .description = "Viewport background color used when the environment skybox is hidden or unavailable.",
    };

    capabilities.material.feature = RendererControlCapability {
        .description = "Edits physically based material properties.",
    };
    capabilities.material.transparency = RendererControlCapability {
        .description = "Controls how this material combines with pixels behind it.",
    };
    capabilities.material.transparent_shadows = RendererControlCapability {
        .description = "Lets material alpha affect shadow opacity when supported by the active shadow path.",
    };
    capabilities.material.alpha_to_coverage = RendererControlCapability {
        .description = "Uses alpha to coverage for masked edges when MSAA is available.",
    };
    capabilities.material.refraction = RendererControlCapability {
        .description = "Controls transmitted and refracted light for transparent materials.",
    };
    capabilities.material.cubemap_refraction = RendererControlCapability {
        .description = "Uses the environment lighting as the refraction source.",
    };
    capabilities.material.screen_space_refraction = RendererControlCapability {
        .description = "Uses visible opaque scene pixels as the refraction source.",
    };

    capabilities.shadows.directional_shadows = RendererControlCapability {
        .description = "Directional shadow controls.",
    };
    capabilities.shadows.point_shadows = RendererControlCapability {
        .description = "Point light shadow controls.",
    };
    capabilities.shadows.contact_shadows = RendererControlCapability {
        .description = "Adds a small dark touch where objects meet surfaces.",
    };
    capabilities.shadows.advanced_controls = RendererControlCapability {
        .description = "Fine controls for cleaning up shadow speckles, floating edges, shimmering, leaks, and softness.",
    };

    capabilities.lights.directional_lights = RendererControlCapability {
        .description = "Directional light controls.",
    };
    capabilities.lights.point_lights = RendererControlCapability {
        .description = "Point light controls.",
    };
    capabilities.lights.point_light_range = RendererControlCapability {
        .description = "Maximum point light influence distance.",
    };

    populate_common_options(capabilities);
    return capabilities;
}

} // namespace quader
