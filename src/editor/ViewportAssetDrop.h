#pragma once

namespace crimson {
struct EditorFrame;

// Call immediately after the viewport image, while its ImGui window is current.
// Model/material hover publishes transient input; delivery transfers keyboard focus.
void acceptViewportModelDrop(EditorFrame& frame);
}
