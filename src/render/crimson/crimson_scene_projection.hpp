////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/render_scene_snapshot.hpp"

#include <crimson/renderer/Scene.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace quader {

/** Pins one complete translation, including original Quader stream owners. */
struct CrimsonSceneProjection final {
  std::uint64_t generation = 0u;
  std::shared_ptr<const RenderSceneSnapshot> source;
  std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview;
  std::vector<crimson::shared::MeshUpdate> meshes;
  std::vector<crimson::shared::SurfaceUpdate> surfaces;
  std::vector<crimson::shared::InstanceUpdate> instances;

  [[nodiscard]] crimson::shared::SceneCandidate candidate() const noexcept {
    return {generation, meshes, surfaces, instances};
  }
};

/** Observable conversion work; unchanged publications must leave these fixed. */
struct CrimsonSceneProjectionStats final {
  std::uint64_t position_packs = 0u;
  std::uint64_t attribute_packs = 0u;
  std::uint64_t index_packs = 0u;
  std::uint64_t image_decodes = 0u;
};

/**
 * Translates immutable rendering inputs. It owns no document/modeling state and
 * changes no accepted coverage; the host acknowledges GPU publication separately.
 */
class CrimsonSceneProjectionCache final {
public:
  explicit CrimsonSceneProjectionCache(std::filesystem::path resources);
  ~CrimsonSceneProjectionCache();
  CrimsonSceneProjectionCache(const CrimsonSceneProjectionCache &) = delete;
  CrimsonSceneProjectionCache &operator=(const CrimsonSceneProjectionCache &) = delete;

  [[nodiscard]] bool prepare(
      std::shared_ptr<const RenderSceneSnapshot> source,
      std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview,
      std::uint64_t generation,
      std::shared_ptr<const CrimsonSceneProjection> &projection,
      std::string &error);
  [[nodiscard]] CrimsonSceneProjectionStats stats() const noexcept;
  void clear();

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace quader
