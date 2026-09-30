# Modeling toolbar icon provenance

Updated on 2026-09-29 from the user's supplied references, continuing the explicitly approved precise vector workflow. No new ImageGen generations were needed for these six assets.

The SVGs are the editable source of truth, with transparent 96-by-96 viewBoxes. The deployed 384-by-384 PNGs were rendered with Sharp (density 288, resize to 384). No background flattening or checkerboard pixels are present.

World/Local revision: the user's `codex-clipboard-e57f5976-348b-4e2a-b066-19bce37d84e4.png` increases the globe radius from 15 to 25 design units and brightens the green shaft/head to sampled `#73BB46` / `#63A23C`. Globe strokes grow with its size and sit behind the green arrow. The cube faces use the canonical Create Box palette; blue/orange axes match the revised reference. Both states retain the same axis geometry and existing Off/On behavior.

| Runtime icon | SVG source | User reference |
| --- | --- | --- |
| [tool_lasso.png](tool_lasso.png) | [tool_lasso.svg](tool_lasso.svg) | `codex-clipboard-7b5d461c-ce8f-4dc9-8d72-88bd23ba0422.png` |
| [tool_insert_edge_loop.png](tool_insert_edge_loop.png) | [tool_insert_edge_loop.svg](tool_insert_edge_loop.svg) | `codex-clipboard-22ad9426-9811-46e8-9a58-d6c92e221e68.png` |
| [selection_backface.png](selection_backface.png) | [selection_backface.svg](selection_backface.svg) | `codex-clipboard-574104b2-386f-4d10-8a87-42a733b8875c.png (left icon)` |
| [tool_transform_world.png](tool_transform_world.png) | [tool_transform_world.svg](tool_transform_world.svg) | `codex-clipboard-574104b2-386f-4d10-8a87-42a733b8875c.png (right icon)` |
| [tool_transform_local.png](tool_transform_local.png) | [tool_transform_local.svg](tool_transform_local.svg) | `codex-clipboard-bdc88272-4655-405c-8f97-bf5a1bc036ba.png` |
| [tool_knife.png](tool_knife.png) | [tool_knife.svg](tool_knife.svg) | `codex-clipboard-f9f9aa2c-e6d7-4c0b-98b9-fda0ed43678d.png` |

The green edge-loop line is clipped to the cube silhouette. Knife's green segments meet at the square centers. World and Local share the same axis/cube geometry; World adds the globe. The lasso's dashed loop and pointer remain separate shapes.

`QtSnappingToolbar` supplies the World icon in `QIcon::Off` and Local in `QIcon::On`. Independent viewport action check states select the correct artwork; no frame-by-frame icon reconstruction is added. Existing Local Transforms label, commands, bindings, and transformation semantics remain unchanged.

A contact sheet at 96 pixels and 32 pixels was reviewed before the native build. Public documentation describes the globe/plain-axis state indicator in the Viewport Toolbar page.

