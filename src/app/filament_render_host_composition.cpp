////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#include "render/render_host.hpp"
#include "render/render_system.hpp"

#include <memory>

namespace quader {

std::unique_ptr<RenderHost> create_app_render_host() {
  return std::make_unique<RenderSystem>();
}

} // namespace quader
