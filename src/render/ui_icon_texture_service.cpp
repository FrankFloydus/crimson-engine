////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/ui_icon_texture_service.hpp"

#include "core/logger.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"

#include <filament/Engine.h>
#include <filament/Texture.h>
#include <lodepng.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <vector>

namespace quader {
namespace {

constexpr unsigned kRgbaChannelCount = 4u;

} // namespace

UiIconTextureService::~UiIconTextureService() {
  clear();
}

void UiIconTextureService::initialize(filament::Engine &engine,
                                      TextureRegistry &textures) {
  clear();
  engine_ = &engine;
  textures_ = &textures;
}

RenderUiIconSet
UiIconTextureService::load_icons(const std::filesystem::path &executable_dir) {
  clear();
  RenderUiIconSet icons;
  if (engine_ == nullptr || textures_ == nullptr) {
    LOG_WARN("Unable to load UI icons before the render texture service is "
             "initialized");
    return icons;
  }

  const auto load = [&](const char *filename) {
    return load_path(executable_dir / "icons" / filename);
  };
  const auto load_first_available =
      [&](std::initializer_list<std::filesystem::path> paths) {
        for (const std::filesystem::path &path : paths) {
          if (!std::filesystem::exists(path)) {
            continue;
          }
          const RenderTextureHandle texture = load_path(path);
          if (texture != kInvalidRenderTextureHandle) {
            return texture;
          }
        }
        return kInvalidRenderTextureHandle;
      };

  icons.logo = load_first_available({
      executable_dir / "icons" / "quader-logo@2x.png",
      executable_dir / "icons" / "logo.png",
  });
  icons.mesh = load("mesh_icon.png");
  icons.empty = load("empty_icon.png");
  icons.lock = load("lock_icon.png");
  icons.unlock = load("unlock_icon.png");
  icons.hide = load("hide_icon.png");
  icons.show = load("show_icon.png");
  return icons;
}

void UiIconTextureService::clear() {
  if (textures_ != nullptr) {
    for (const IconTextureRecord &record : icon_textures_) {
      if (record.handle != kInvalidRenderTextureHandle) {
        textures_->unregister_texture(record.handle);
      }
    }
  }

  if (engine_ != nullptr) {
    for (const IconTextureRecord &record : icon_textures_) {
      if (record.texture != nullptr) {
        engine_->destroy(record.texture);
      }
    }
  }

  icon_textures_.clear();
}

RenderTextureHandle
UiIconTextureService::load_path(const std::filesystem::path &icon_path) {
  std::vector<unsigned char> pixels;
  unsigned width = 0;
  unsigned height = 0;
  const unsigned error = lodepng::decode(pixels, width, height,
                                         icon_path.string());
  if (error != 0u || pixels.empty() || width == 0u || height == 0u) {
    LOG_WARN("Unable to load UI icon {}: {}", icon_path.string(),
             lodepng_error_text(error));
    return kInvalidRenderTextureHandle;
  }

  const std::size_t row_byte_count =
      static_cast<std::size_t>(width) * kRgbaChannelCount;
  std::vector<unsigned char> flipped(pixels.size());
  for (unsigned y = 0; y < height; ++y) {
    const std::size_t source_offset =
        static_cast<std::size_t>(y) * row_byte_count;
    const std::size_t target_offset =
        static_cast<std::size_t>(height - 1u - y) * row_byte_count;
    std::memcpy(flipped.data() + target_offset, pixels.data() + source_offset,
                row_byte_count);
  }

  filament::Texture *texture =
      filament::Texture::Builder()
          .width(width)
          .height(height)
          .levels(1)
          .format(filament::Texture::InternalFormat::RGBA8)
          .usage(filament::Texture::Usage::UPLOADABLE |
                 filament::Texture::Usage::SAMPLEABLE)
          .build(*engine_);
  if (texture == nullptr) {
    LOG_WARN("Unable to create render texture for UI icon {}",
             icon_path.string());
    return kInvalidRenderTextureHandle;
  }

  texture->setImage(*engine_, 0,
                    FilamentBufferDescriptorFactory::rgba_pixels(
                        flipped.data(), flipped.size()));
  const RenderTextureHandle handle = textures_->register_texture(texture);
  icon_textures_.push_back(IconTextureRecord{
      .texture = texture,
      .handle = handle,
  });
  return handle;
}

} // namespace quader
