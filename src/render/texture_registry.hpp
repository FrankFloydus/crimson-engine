////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filament/Texture.h>

#include <cstdint>
#include <unordered_map>

namespace quader {

using RenderTextureHandle = std::uintptr_t;
constexpr RenderTextureHandle kInvalidRenderTextureHandle = 0u;

/**
 * Enumerates TextureSampleColorSpace values used by the render layer.
 */
enum class TextureSampleColorSpace {
    DisplaySrgb,
    DisplaySrgbPremultipliedAlpha,
    LinearSrgb
};

/**
 * Owns Texture Registry lookup and registration for the render extraction and Filament resource layer.
 */
class TextureRegistry {
public:
    TextureRegistry() = default;
    TextureRegistry(const TextureRegistry&) = delete;
    TextureRegistry& operator=(const TextureRegistry&) = delete;
    TextureRegistry(TextureRegistry&&) = delete;
    TextureRegistry& operator=(TextureRegistry&&) = delete;

    RenderTextureHandle
    register_texture(filament::Texture *texture,
                     TextureSampleColorSpace sample_color_space =
                         TextureSampleColorSpace::DisplaySrgb);
    void update_texture(RenderTextureHandle id, filament::Texture *texture,
                        TextureSampleColorSpace sample_color_space =
                            TextureSampleColorSpace::DisplaySrgb);
    filament::Texture* texture(RenderTextureHandle id) const;
    TextureSampleColorSpace sample_color_space(RenderTextureHandle id) const;
    void unregister_texture(RenderTextureHandle id);
    void clear();

private:
    /**
     * Stores the Texture Record data contract used by the render extraction and Filament resource layer.
     */
    struct TextureRecord {
        filament::Texture* texture = nullptr;
        TextureSampleColorSpace sample_color_space =
            TextureSampleColorSpace::DisplaySrgb;
    };

    RenderTextureHandle next_id_ = 1u;
    std::unordered_map<RenderTextureHandle, TextureRecord> textures_;
};

} // namespace quader
