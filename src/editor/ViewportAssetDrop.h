#pragma once

namespace crimson {
struct EditorFrame;

// Call immediately after the viewport image, while its ImGui window is current.
// Model hover publishes a transient preview; model/particle delivery transfers keyboard focus.
void acceptViewportModelDrop(EditorFrame& frame);
}
