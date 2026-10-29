#pragma once

#include <crimson/renderer/Renderer.hpp>
#include "GraphicsBackend.h"

namespace crimson {

// Engine resource adapters borrow Quader's single canonical renderer owner.
using Renderer = shared::Renderer;

} // namespace crimson
