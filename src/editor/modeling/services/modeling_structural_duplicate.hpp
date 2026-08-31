////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"

#include <functional>
#include <string_view>
#include <vector>

namespace quader {

class EditorTransaction;

/** Identifies one deterministic source-to-duplicate scene mapping. */
struct ModelingStructuralEntityClone final {
  EntityId source = kInvalidEntityId;
  EntityId duplicate = kInvalidEntityId;
  int source_modeling_object_id = kInvalidModelingObjectId;
  int duplicate_modeling_object_id = kInvalidModelingObjectId;
};

/**
 * Reports the complete scene-root and modeled-descendant mappings from one
 * structural duplicate appended to a caller-owned editor transaction.
 */
struct ModelingStructuralDuplicateResult final : ModelingCommandResult {
  std::vector<ModelingStructuralEntityClone> roots;
  std::vector<ModelingStructuralEntityClone> modeling_objects;
};

/**
 * Appends one scene-aware hierarchy duplicate to an empty caller transaction.
 * The callback is borrowed only for the synchronous operation that invokes it.
 */
using ModelingStructuralDuplicate =
    std::function<ModelingStructuralDuplicateResult(
        EditorTransaction &, ModelingSelectionMode, std::string_view)>;

} // namespace quader
