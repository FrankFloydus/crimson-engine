////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_face_texture_settings.hpp"

#include <cstdint>
#include <span>

namespace quader {

/// Owns selection epochs and relative rotation editing, without owning mesh UVs.
class FaceTextureEditContext final {
public:
  [[nodiscard]] ModelingFaceTextureSettings synchronize(
      const ModelingSelectedFaceTextureSnapshot &snapshot,
      std::uint64_t selection_revision, QVec2 texture_extent);
  [[nodiscard]] ModelingCommandResult prepare_edit(
      const ModelingFaceTextureEdit &edit,
      const ModelingSelectedFaceTextureSnapshot &snapshot, QVec2 texture_extent,
      std::vector<ModelingFaceTextureUpdateBatch> &batches) const;
  [[nodiscard]] ModelingFaceTextureEditState capture() const;
  void record_applied_edit(const ModelingFaceTextureEdit &edit);
  void restore(const ModelingFaceTextureEditState &state,
               const ModelingSelectedFaceTextureSnapshot &snapshot,
               std::uint64_t selection_revision);
  void invalidate();
  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
  [[nodiscard]] bool membership_matches(
      const ModelingSelectedFaceTextureSnapshot &snapshot) const;
  void reset_baselines(const ModelingSelectedFaceTextureSnapshot &snapshot,
                       std::uint64_t selection_revision);

  ModelingFaceTextureEditState state_;
  std::uint64_t selection_revision_ = 0;
  std::uint64_t selection_token_ = 0;
  std::uint64_t revision_ = 0;
  bool initialized_ = false;
};

} // namespace quader
