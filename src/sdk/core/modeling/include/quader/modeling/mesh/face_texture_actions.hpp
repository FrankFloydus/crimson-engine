////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/mesh/polygon_document.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace quader::modeling {

/// Discrete face-texturing operations, independent of a native editor host.
enum class FaceTextureAction {
  AlignToGrid,
  AlignToFace,
  AlignToView,
  ScaleXUp,
  ScaleXDown,
  ScaleYUp,
  ScaleYDown,
  ShiftLeft,
  ShiftRight,
  ShiftUp,
  ShiftDown,
  RotateClockwise,
  RotateCounterClockwise,
  FlipTextureHorizontal,
  FlipTextureVertical,
  FlipFaceHorizontal,
  FlipFaceVertical,
  FitBoth,
  FitX,
  FitY,
  JustifyLeft,
  JustifyRight,
  JustifyTop,
  JustifyBottom,
  JustifyCenter,
};

/// One selected face and its material's decoded pixel dimensions.
struct FaceTextureActionFace {
  FaceId face{};
  std::uint32_t texture_width = 64;
  std::uint32_t texture_height = 64;
};

/**
 * Borrows an immutable document for the duration of preparation. The opaque,
 * unique key associates the prepared document with its host-owned target.
 */
struct FaceTextureActionTarget {
  std::uint64_t key = 0;
  const PolygonDocument *document = nullptr;
  std::vector<FaceTextureActionFace> faces;
  Transform3 world_from_local{};
  Transform3 sampling_from_local{};
};

/**
 * Borrows one face in its displayed occurrence. World placement and texture
 * sampling may differ; the host retains both transforms after publication.
 */
struct FaceTextureTransferFace {
  std::uint64_t key = 0;
  const PolygonDocument *document = nullptr;
  FaceId face{};
  Transform3 world_from_local{};
  Transform3 sampling_from_local{};
};

/// Captured operation context; preparing never queries or changes a host.
struct FaceTextureActionOptions {
  FaceTextureAction action = FaceTextureAction::AlignToGrid;
  bool treat_as_one = false;
  double rotation_degrees = 15.0;
  std::uint64_t active_target_key = 0;
  FaceId active_face{};
  Vec3 view_right{1.0F, 0.0F, 0.0F};
  Vec3 view_up{0.0F, 1.0F, 0.0F};
};

/// Owns a validated replacement, including an unchanged shared copy on no-op.
struct PreparedFaceTextureDocument {
  std::uint64_t key = 0;
  PolygonDocument document;
  OperationResult receipt;
};

/// All replacements become available together, or preparation returns an error.
struct PreparedFaceTextureAction {
  std::vector<PreparedFaceTextureDocument> documents;
  bool changed = false;
};

/**
 * Prepares selection-wide texturing/face reflection without mutating any input.
 * A missing active reference falls back to the smallest target key and face ID.
 * Result documents retain component IDs, selection, and occurrence sampling;
 * the host publishes changed replacements in one atomic transaction.
 */
[[nodiscard]] Result<PreparedFaceTextureAction> prepare_face_texture_action(
    std::span<const FaceTextureActionTarget> targets,
    const FaceTextureActionOptions &options);

/**
 * Continues the source's affine texture chart by rotating it onto the target
 * plane about their intersection. Parallel/opposed planes retain world axes
 * and phase. Copies the source face settings and the host-resolved material
 * slot without changing geometry, component IDs, selection, or sampling.
 * Returns only the immutable target replacement; neither input is modified.
 */
[[nodiscard]] Result<PreparedFaceTextureAction> prepare_face_texture_transfer(
    const FaceTextureTransferFace &source,
    const FaceTextureTransferFace &target, std::uint32_t target_material_slot);

} // namespace quader::modeling
