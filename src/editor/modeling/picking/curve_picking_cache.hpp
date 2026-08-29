////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/interactions/selection_box.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "math/types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace quader {

namespace modeling {
class CurveDocument;
}

class AssetRegistry;
class SceneDocument;
class ViewportLocalVisibilitySnapshot;
struct CurveSamplePayload;

/// Pins one selectable authored curve for camera-dependent segment picking.
struct ViewportSceneCurvePickCandidate {
  EntityId entity = kInvalidEntityId;
  QMat4 world_transform;
  /// Pins authored anchors so drag-time snap queries never rescan assets.
  std::shared_ptr<const modeling::CurveDocument> document;
  std::shared_ptr<const CurveSamplePayload> sample_payload;
};

/// Owns filtered scene/asset candidates and cached curve click/marquee inputs.
class ViewportCurvePickCache final {
public:
  /// Borrows candidates until the next source change or reset.
  [[nodiscard]] std::span<const ViewportSceneCurvePickCandidate> candidates(
      const SceneDocument &document, const AssetRegistry &assets,
      const ViewportLocalVisibilitySnapshot &local_visibility);
  [[nodiscard]] std::span<const ViewportSceneCurvePickCandidate> candidates(
      const SceneDocument &document, const AssetRegistry &assets,
      std::uint64_t visibility_revision,
      const std::function<bool(EntityId)> &entity_visible,
      bool instance_edit_active = false);
  /// Resolves the same contain/crossing rules as meshes without a polygon proxy.
  [[nodiscard]] std::vector<EntityId> box_pick_entities(
      const SceneDocument &document, const AssetRegistry &assets,
      const ModelingBoxSelectRequest &request, std::uint64_t visibility_revision,
      const std::function<bool(EntityId)> &entity_visible,
      bool instance_edit_active = false);
  void reset();
  [[nodiscard]] std::uint64_t build_count() const noexcept { return build_count_; }
  [[nodiscard]] std::uint64_t rectangle_build_count() const noexcept {
    return rectangle_build_count_;
  }

private:
  const SceneDocument *document_ = nullptr;
  const AssetRegistry *assets_ = nullptr;
  std::uint64_t scene_revision_ = 0;
  std::uint64_t asset_revision_ = 0;
  std::uint64_t visibility_revision_ = 0;
  std::uint64_t build_count_ = 0;
  std::vector<ViewportSceneCurvePickCandidate> candidates_;
  bool instance_edit_active_ = false;
  bool valid_ = false;
  std::uint64_t rectangle_source_revision_ = 0;
  std::uint64_t rectangle_build_count_ = 0;
  ModelingViewportCamera rectangle_camera_;
  quader_editor_core::RectangleProjectedHitsRequest rectangle_request_;
  std::vector<EntityId> rectangle_entities_;
  bool rectangle_valid_ = false;
};

} // namespace quader
