////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/TextureRegistry.h"

namespace quader {

RenderTextureHandle TextureRegistry::registerTexture(filament::Texture* texture, TextureSampleColorSpace sampleColorSpace) {
    const RenderTextureHandle id = nextId_++;
    updateTexture(id, texture, sampleColorSpace);
    return id;
}

void TextureRegistry::updateTexture(
        RenderTextureHandle id,
        filament::Texture* texture,
        TextureSampleColorSpace sampleColorSpace) {
    if (id == InvalidRenderTextureHandle) {
        return;
    }
    textures_[id] = TextureRecord{
            .texture = texture,
            .sampleColorSpace = sampleColorSpace,
    };
}

filament::Texture* TextureRegistry::texture(RenderTextureHandle id) const {
    const auto it = textures_.find(id);
    return it == textures_.end() ? nullptr : it->second.texture;
}

TextureSampleColorSpace TextureRegistry::sampleColorSpace(RenderTextureHandle id) const {
    const auto it = textures_.find(id);
    return it == textures_.end() ? TextureSampleColorSpace::DisplaySrgb : it->second.sampleColorSpace;
}

void TextureRegistry::unregisterTexture(RenderTextureHandle id) {
    textures_.erase(id);
}

void TextureRegistry::clear() {
    textures_.clear();
}

} // namespace quader
