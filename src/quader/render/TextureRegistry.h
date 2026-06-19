#pragma once

#include <filament/Texture.h>
#include <imgui.h>

#include <cstdint>
#include <unordered_map>

namespace quader {

enum class TextureSampleColorSpace {
    DisplaySrgb,
    LinearSrgb
};

class TextureRegistry {
public:
    ImTextureID registerTexture(filament::Texture* texture, TextureSampleColorSpace sampleColorSpace = TextureSampleColorSpace::DisplaySrgb);
    filament::Texture* texture(ImTextureID id) const;
    TextureSampleColorSpace sampleColorSpace(ImTextureID id) const;
    void unregisterTexture(ImTextureID id);
    void clear();

private:
    struct TextureRecord {
        filament::Texture* texture = nullptr;
        TextureSampleColorSpace sampleColorSpace = TextureSampleColorSpace::DisplaySrgb;
    };

    std::uint64_t nextId_ = 1;
    std::unordered_map<ImTextureID, TextureRecord> textures_;
};

} // namespace quader
