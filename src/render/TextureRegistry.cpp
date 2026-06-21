#include "render/TextureRegistry.h"

namespace quader {

ImTextureID TextureRegistry::registerTexture(filament::Texture* texture, TextureSampleColorSpace sampleColorSpace) {
    const ImTextureID id = static_cast<ImTextureID>(nextId_++);
    textures_[id] = TextureRecord{
            .texture = texture,
            .sampleColorSpace = sampleColorSpace,
    };
    return id;
}

filament::Texture* TextureRegistry::texture(ImTextureID id) const {
    const auto it = textures_.find(id);
    return it == textures_.end() ? nullptr : it->second.texture;
}

TextureSampleColorSpace TextureRegistry::sampleColorSpace(ImTextureID id) const {
    const auto it = textures_.find(id);
    return it == textures_.end() ? TextureSampleColorSpace::DisplaySrgb : it->second.sampleColorSpace;
}

void TextureRegistry::unregisterTexture(ImTextureID id) {
    textures_.erase(id);
}

void TextureRegistry::clear() {
    textures_.clear();
}

} // namespace quader
