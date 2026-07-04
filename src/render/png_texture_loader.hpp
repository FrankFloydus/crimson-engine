////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>

namespace filament {
class Engine;
class Texture;
} // namespace filament

namespace quader {

/**
 * Enumerates color-space upload modes for app-owned PNG textures.
 */
enum class PngTextureColorSpace {
  Linear,
  Srgb,
};

/**
 * Stores options for decoding and uploading a PNG into a Filament texture.
 */
struct PngTextureLoadOptions {
  PngTextureColorSpace color_space = PngTextureColorSpace::Linear;
  bool flip_y = false;
  bool generate_mipmaps = false;
  const char *label = "PNG texture";
};

[[nodiscard]] filament::Texture *
load_png_texture(filament::Engine &engine, const std::filesystem::path &path,
                 PngTextureLoadOptions options = {});

} // namespace quader
