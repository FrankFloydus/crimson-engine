////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filament/Texture.h>
#include <imgui.h>

#include <cstdint>
#include <unordered_map>

namespace quader {

/**
 * Enumerates TextureSampleColorSpace values used by the render layer.
 */
enum class TextureSampleColorSpace {
    DisplaySrgb,
    LinearSrgb
};

/**
 * Owns Texture Registry lookup and registration for the render extraction and Filament resource layer.
 */
class TextureRegistry {
public:
    ImTextureID registerTexture(filament::Texture* texture, TextureSampleColorSpace sampleColorSpace = TextureSampleColorSpace::DisplaySrgb);
    filament::Texture* texture(ImTextureID id) const;
    TextureSampleColorSpace sampleColorSpace(ImTextureID id) const;
    void unregisterTexture(ImTextureID id);
    void clear();

private:
    /**
     * Stores the Texture Record data contract used by the render extraction and Filament resource layer.
     */
    struct TextureRecord {
        filament::Texture* texture = nullptr;
        TextureSampleColorSpace sampleColorSpace = TextureSampleColorSpace::DisplaySrgb;
    };

    std::uint64_t nextId_ = 1;
    std::unordered_map<ImTextureID, TextureRecord> textures_;
};

} // namespace quader
