////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/texture_registry.hpp"

#include <filesystem>
#include <vector>

namespace filament {
class Engine;
class Texture;
} // namespace filament

namespace quader {

/**
 * Stores render texture handles for the native ImGui icon set.
 */
struct RenderUiIconSet {
  RenderTextureHandle logo = kInvalidRenderTextureHandle;
  RenderTextureHandle mesh = kInvalidRenderTextureHandle;
  RenderTextureHandle empty = kInvalidRenderTextureHandle;
  RenderTextureHandle lock = kInvalidRenderTextureHandle;
  RenderTextureHandle unlock = kInvalidRenderTextureHandle;
  RenderTextureHandle hide = kInvalidRenderTextureHandle;
  RenderTextureHandle show = kInvalidRenderTextureHandle;
};

/**
 * Owns native UI icon texture upload, registry handles, and Filament teardown.
 */
class UiIconTextureService {
public:
  UiIconTextureService() = default;
  UiIconTextureService(const UiIconTextureService &) = delete;
  UiIconTextureService &operator=(const UiIconTextureService &) = delete;
  UiIconTextureService(UiIconTextureService &&) = delete;
  UiIconTextureService &operator=(UiIconTextureService &&) = delete;
  ~UiIconTextureService();

  void initialize(filament::Engine &engine, TextureRegistry &textures);
  RenderUiIconSet load_icons(const std::filesystem::path &executable_dir);
  void clear();

private:
  /**
   * Tracks a loaded UI icon texture and its ImGui presentation registry handle.
   */
  struct IconTextureRecord {
    filament::Texture *texture = nullptr;
    RenderTextureHandle handle = kInvalidRenderTextureHandle;
  };

  RenderTextureHandle load_path(const std::filesystem::path &icon_path);

  filament::Engine *engine_ = nullptr;
  TextureRegistry *textures_ = nullptr;
  std::vector<IconTextureRecord> icon_textures_;
};

} // namespace quader
