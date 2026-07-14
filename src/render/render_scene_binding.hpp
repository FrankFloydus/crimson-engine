////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>

namespace quader {

/**
 * Identifies one activation of an open scene at renderer-neutral cache
 * boundaries. The session identity is the packed SceneSessionId supplied by
 * the editor host; activation and binding revisions prevent records retained
 * by the single shared viewport from crossing tab activations.
 */
struct RenderSceneBindingKey {
  std::uint64_t scene_session_id = 0;
  std::uint64_t activation_generation = 0;
  std::uint64_t binding_revision = 0;

  friend bool operator==(const RenderSceneBindingKey &,
                         const RenderSceneBindingKey &) = default;
};

} // namespace quader
