////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"

namespace quader {

class AssetRegistry;
class EditorHistoryService;
class EditorSelectionService;
class ModelingSessionService;
class SceneDocument;

/// Chooses the authored object channels incorporated into mesh coordinates.
enum class MeshTransformBakeMode {
  Transforms,
  Location,
  Rotation,
  Scale,
};

/// Coordinates authored mesh baking through native scene/modeling history.
class MeshTransformBakeService final {
public:
  MeshTransformBakeService(SceneDocument &scene, AssetRegistry &assets,
                           EditorHistoryService &history,
                           ModelingSessionService &modeling);

  [[nodiscard]] bool can_bake(const EditorSelectionService &selection,
                             MeshTransformBakeMode mode) const;
  CommandResult bake(const EditorSelectionService &selection,
                     MeshTransformBakeMode mode);

private:
  SceneDocument &scene_;
  AssetRegistry &assets_;
  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
};

} // namespace quader
