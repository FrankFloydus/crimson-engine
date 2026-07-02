////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/texture_registry.hpp"

namespace quader {

RenderTextureHandle
TextureRegistry::register_texture(filament::Texture *texture,
                                  TextureSampleColorSpace sample_color_space) {
  const RenderTextureHandle id = next_id_++;
  update_texture(id, texture, sample_color_space);
  return id;
}

void TextureRegistry::update_texture(
    RenderTextureHandle id, filament::Texture *texture,
    TextureSampleColorSpace sample_color_space) {
  if (id == kInvalidRenderTextureHandle) {
    return;
  }
  textures_[id] = TextureRecord{
      .texture = texture,
      .sample_color_space = sample_color_space,
  };
}

filament::Texture* TextureRegistry::texture(RenderTextureHandle id) const {
    const auto it = textures_.find(id);
    return it == textures_.end() ? nullptr : it->second.texture;
}

TextureSampleColorSpace
TextureRegistry::sample_color_space(RenderTextureHandle id) const {
  const auto it = textures_.find(id);
  return it == textures_.end() ? TextureSampleColorSpace::DisplaySrgb
                               : it->second.sample_color_space;
}

void TextureRegistry::unregister_texture(RenderTextureHandle id) {
  textures_.erase(id);
}

void TextureRegistry::clear() {
    textures_.clear();
}

} // namespace quader
