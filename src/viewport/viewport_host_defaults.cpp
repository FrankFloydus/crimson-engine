////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/host_viewport_contract_adapter.hpp"

#include <algorithm>
#include <cmath>

namespace quader {

ViewportHostDefaults viewport_host_defaults() { return {}; }

int viewport_default_grid_preset() { return 6; }

int viewport_clamp_grid_preset(int preset) {
  return std::clamp(preset, 1, 10);
}

float viewport_grid_world_size_from_contract(int preset) {
  const int clamped_preset = viewport_clamp_grid_preset(preset);
  const float map_units =
      std::pow(2.0f, static_cast<float>(clamped_preset - 1));
  return map_units /
         static_cast<float>(viewport_host_defaults().map_units_per_world_unit);
}

} // namespace quader
