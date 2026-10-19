////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#include "render/crimson/crimson_render_host.hpp"

namespace quader {

std::unique_ptr<RenderHost> create_app_render_host() {
  return create_crimson_render_host();
}

} // namespace quader
