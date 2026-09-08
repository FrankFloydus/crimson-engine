////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/components.hpp"

#include <filament/ToneMapper.h>

namespace quader {

/// Returns an immutable mapper with Filament's linear Rec.2020 input/output contract.
/// Retired serialized modes use modern ACES without rewriting their authored identity.
[[nodiscard]] const filament::ToneMapper &
scene_tone_mapper(SceneToneMapping tone_mapping) noexcept;

} // namespace quader
