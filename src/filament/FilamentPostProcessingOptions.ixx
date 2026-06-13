module;

#include <filament/View.h>

export module quader.filament.post_processing_options;

export import quader.render.types;

export namespace quader {

[[nodiscard]] filament::View::BloomOptions filament_bloom_options(BloomSettings settings);
[[nodiscard]] filament::View::AmbientOcclusionOptions filament_ambient_occlusion_options(
    AmbientOcclusionSettings settings);

} // namespace quader
