////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/document/scene_content_document.hpp"

namespace quader {

class SceneSession;

/**
 * Stores the project default asset identifiers created during asset bootstrap.
 */
struct ProjectDefaultAssets final {
  AssetId albedo_texture = kInvalidAssetId;
  AssetId roughness_texture = kInvalidAssetId;
  AssetId default_material = kInvalidAssetId;
};

/**
 * Coordinates project-owned default asset bootstrap for editor sessions.
 */
class ProjectAssetBootstrapService final {
public:
  [[nodiscard]] ProjectDefaultAssets
  bootstrap_default_assets(SceneSession &session) const;
};

} // namespace quader
