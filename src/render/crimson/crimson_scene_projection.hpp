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
#include <unordered_map>

namespace quader {

[[nodiscard]] bool crimson_tool_preview_snapshots_equal(
    const RenderToolPreviewSceneSnapshot&,const RenderToolPreviewSceneSnapshot&);

/** Pins one complete translation, including original Quader stream owners. */
struct CrimsonNativeInstance {std::string entity_uuid;crimson::shared::Matrix4 entity_from_local;std::uint64_t source_instance_id{};std::string member_uuid;std::uint32_t index_offset{},index_count{};};
struct CrimsonSceneProjection final {
  std::uint64_t generation = 0u;
  std::shared_ptr<const RenderSceneSnapshot> source;
  std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview;
  std::vector<crimson::shared::MeshUpdate> meshes;
  std::vector<crimson::shared::SurfaceUpdate> surfaces;
  // Full Standard payloads remain immutable alongside the modeling albedo
  // transport, so Rendered can reuse exact source identities and all four maps.
  std::vector<std::pair<std::uint64_t,RenderMaterialAssetSnapshot>> source_materials;
  std::vector<crimson::shared::InstanceUpdate> instances;
  std::unordered_map<std::uint64_t,CrimsonNativeInstance> native_instances;
  std::vector<std::pair<std::uint64_t,bool>> instance_cast_shadows;
  // Accepted and transient lights use the same immutable publication as mesh
  // coverage. Replaced sources never illuminate alongside their preview.
  std::vector<RenderSceneLightRecord> lights;

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
