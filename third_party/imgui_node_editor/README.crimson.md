# imgui-node-editor

Upstream: https://github.com/thedmd/imgui-node-editor

Pinned revision: `1e0e494f1a86885eb332ca518543108ccd88d13e`.

License: MIT, retained in `LICENSE`. Only the core library is vendored; examples
and their application/backend dependencies are omitted. The library uses the
project's existing pinned Dear ImGui target.

Crimson keeps material graph data, stable identities, transactions and node
positions in its own CPU asset/document types. The library's settings file is
disabled. Canvas navigation and selection are transient editor state.

Local additions: CMake target and this provenance note. `imgui_canvas.h/.cpp`
scope ImGui 1.92 font rasterizer density to the canvas zoom, using four density
tiers to bound atlas updates. Font layout size and the default editor font are
unchanged; Suspend/Resume and End restore the original monitor density. This
avoids enlarging the same low-resolution glyphs when zooming into the graph.
