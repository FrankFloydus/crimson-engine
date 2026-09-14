////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"

#include <cstdint>
#include <string>

namespace quader {

/** Identifies the viewport presentation of one settings operation. */
struct QuaderSettingsPresentationOrigin {
  std::uint64_t serial = 0;
  std::string target_id;
  ViewportPaneId pane = ViewportPaneId::Perspective;

  [[nodiscard]] bool
  operator==(const QuaderSettingsPresentationOrigin &) const = default;
};

} // namespace quader
