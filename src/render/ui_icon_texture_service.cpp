////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/ui_icon_texture_service.hpp"

#include "core/logger.hpp"
#include "render/png_texture_loader.hpp"

#include <filament/Engine.h>
#include <filament/Texture.h>

#include <filesystem>
#include <initializer_list>

namespace quader {

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
  filament::Texture *texture = load_png_texture(
      *engine_, icon_path,
      PngTextureLoadOptions{
          .color_space = PngTextureColorSpace::Linear,
          .flip_y = true,
          .label = "UI icon",
      });
  if (texture == nullptr) {
    return kInvalidRenderTextureHandle;
  }

  const RenderTextureHandle handle = textures_->register_texture(texture);
  icon_textures_.push_back(IconTextureRecord{
      .texture = texture,
      .handle = handle,
  });
  return handle;
}

} // namespace quader
