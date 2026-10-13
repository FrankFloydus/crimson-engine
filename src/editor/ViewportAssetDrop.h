#pragma once

namespace crimson {
struct EditorFrame;

// Call immediately after the viewport image, while its ImGui window is current.
// Hover publishes a transient preview; delivery transfers keyboard focus.
void acceptViewportModelDrop(EditorFrame& frame);
}
