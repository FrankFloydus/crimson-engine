////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

/** Exposes only the active-tool gates required by floor placement. */
class SnapToFloorToolStatePort {
public:
  virtual ~SnapToFloorToolStatePort() = default;
  [[nodiscard]] virtual bool has_pending_tool() const = 0;
  [[nodiscard]] virtual bool blocks_modeling_content_mutation() const = 0;
};

} // namespace quader
