////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/modeling/modeling_result_types.hpp"
#include "math/types.hpp"

#include <quader/modeling/mesh/uv_peel.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace quader {

using ModelingUvPeelAxis = modeling::UvPeelAxis;
using ModelingUvPeelOptions = modeling::UvPeelOptions;

/// Captures settings once; repeat resolves the current guides and frames.
struct ModelingUvPeelRequest final {
  ModelingUvPeelOptions options;
  std::uint64_t selection_token = 0;
};

/// Stable presentation causes for unavailable edge-guided texture editing.
enum class ModelingUvPeelBlockReason {
  None,
  NoSelection,
  PreviewActive,
  TargetUnavailable,
};

/// A narrow owned selection snapshot, without copied mesh topology.
struct ModelingSelectedUvPeelObject final {
  int modeling_object_id = -1;
  EntityId entity = kInvalidEntityId;
  std::vector<modeling::EdgeKey> edges;
  std::optional<modeling::EdgeKey> active_edge;

  friend bool operator==(const ModelingSelectedUvPeelObject &,
                         const ModelingSelectedUvPeelObject &) = default;
};

/// Captures guide identities and the active object without copying topology.
struct ModelingSelectedUvPeelSnapshot final {
  ModelingCommandResult command{.ok = true};
  std::vector<ModelingSelectedUvPeelObject> objects;
  int active_modeling_object_id = -1;
};

/// Host facts needed by portable preparation; geometry and UV policy stay there.
struct ModelingUvPeelTarget final {
  int modeling_object_id = -1;
  std::vector<modeling::EdgeKey> edges;
  std::optional<modeling::EdgeKey> active_edge;
  QMat4 world_from_local;
  QMat4 sampling_from_local;
};

/// Shares scene-local options and selected-guide availability with presentation.
struct ModelingUvPeelSettings final {
  std::size_t selected_edge_count = 0;
  bool editable = false;
  ModelingUvPeelOptions options;
  ModelingUvPeelBlockReason block_reason = ModelingUvPeelBlockReason::NoSelection;
  std::string disabled_reason;
  std::uint64_t selection_token = 0;
};

} // namespace quader
