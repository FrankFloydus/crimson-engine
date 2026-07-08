////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/payloads.hpp>

namespace quader::modeling {

/**
 * Declares the lifetime contract for renderer-facing snapshot payloads.
 */
enum class SnapshotLifetime {
  Owned,
  Pinned,
  BorrowedSameFrame,
};

/**
 * Stores renderer-neutral scene data produced by the SDK or host adapter.
 */
struct RenderSnapshotPayload {
  SnapshotLifetime lifetime = SnapshotLifetime::Owned;
  std::vector<MeshPayload> meshes;
  std::vector<MaterialPayload> materials;
  std::vector<SemanticOverlayPayload> overlays;
  std::uint64_t scene_revision = 0;
};

} // namespace quader::modeling
