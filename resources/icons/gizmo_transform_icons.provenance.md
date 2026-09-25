# Gizmo visibility and Local Transforms icon provenance

Created on 2026-09-25 using the built-in ImageGen tool at the user's request.
The user approved both initial previews before installation, then requested a
larger visibility eye after reviewing the native toolbar.

| Asset | Generated source | SHA-256 |
| --- | --- | --- |
| `viewport_gizmos.png` | `exec-0a8ff413-fcfa-4dfe-a01a-d66eb309abbf.png` | `12d40459e6c49700bfed213fdedd028ddab58fb2968e1fee8dc04fbf96bb2d23` |
| `tool_transform_local.png` | `exec-512c2d9d-0850-41f4-9a03-e60155cef5dd.png` | `79d635babc2bbf759230c0955e553928415995933e3d011657a02f16d3af3c6c` |

Both are derived from Quader's existing `viewport_gizmos.png` artwork.
The visibility variant adds an eye at the upper right, using the eye in
`viewport_grid.png` as its style reference. The Local Transforms variant
enlarges the central cube and has no eye. The original green, blue and orange
axis colors are retained. No third-party artwork is included.

The generated PNGs are copied unchanged, preserving their transparent alpha.
Existing Qt resource paths and `qt_multiresolution_icon` supply both native
application builds. Labels, shortcuts and command behavior remain unchanged.

## Toggle Gizmos enlarged-eye revision prompt

Edit source: `exec-e13dd027-b5d1-4a98-8507-543dcbf3bbd6.png` (the initial approved
Toggle Gizmos image). The following built-in ImageGen edit produced the current
asset with a more prominent visibility eye.

Use case: precise-object-edit.
Asset type: Quader Toggle Gizmos toolbar PNG, one narrowly scoped size correction.

EDIT TARGET: the supplied existing icon. Keep the underlying cube and its GREEN up arrow, BLUE down-left arrow and ORANGE down-right arrow exactly as they are: same shapes, positions, overall scale, colors, shading and dark navy outlines.

Change ONLY the eye overlay: make it substantially LARGER, approximately 1.8 TIMES its current width AND height. The current eye is too tiny to read beside the other visibility toolbar icons. Target the eye occupying about 52–56% of the entire square canvas width, approximately 650–700 pixels on this 1254px canvas, rather than its current roughly375px width. Preserve the eye's exact almond shape, pale silver/periwinkle sclera, dark navy outline, blue iris, dark pupil and small pale glint.
Keep the enlarged eye in the UPPER-RIGHT corner, moving its center upward as necessary so it remains wholly inside the canvas with at least4% clear margin. It may overlap the upper green arrow slightly as a foreground visibility overlay if needed. Favor a clearly readable large eye, NOT a tiny badge. Do not shrink the underlying gizmo to make room and do not shrink the overall combined artwork. The eye must be visibly much larger in the final result.
Do not change any other part of the icon, restyle the arrows or cube, add any decorations, or add background shapes behind the eye.

Same square canvas. Genuinely transparent alpha background; no black/charcoal fill, no checkerboard pixels. No labels, text, grid, tile, shadows, glow, halo or added border. One crisp finished icon with the enlarged eye.

## Toggle Gizmos initial generation prompt

Use case: precise-object-edit.
Asset type: ONE edited transparent PNG toolbar icon, approval preview for Quader's Toggle Gizmos control.

Image 1 is the actual current Toggle Gizmos icon and the EDIT TARGET. Image 2 is Quader's Toggle Grid icon, used ONLY as the exact EYE STYLE reference.

Preserve Image 1's existing small solid silver/periwinkle cube, green upward arrow, blue down-left arrow, ORANGE down-right arrow, their proportions, directions, shading, bold dark-navy outlines and compact overall silhouette. Do not substitute the red-axis proposals from earlier; the original right arrow is orange. Faithfully redraw the same existing artwork at high resolution without changing its design.

The ONLY design addition: add a small visibility EYE in the empty UPPER-RIGHT corner of the overall icon. Copy the eye vocabulary exactly from Image 2: horizontal almond-shaped pale silver/periwinkle sclera, pointed eye corners, heavy dark navy outline, circular muted-blue iris, dark pupil and tiny pale glint. Use just the eye; DO NOT include any grid lines. The small eye is approximately 28% of the full icon width. Put it beside the upper part of the green arrow, in the upper-right open space; keep a clear gap so the eye does not cover the green arrow or its outline. It is a compact corner overlay, not a giant central eye. The underlying gizmo must remain recognizable and visually dominant.

Output a single centered standalone icon on a square canvas with genuinely TRANSPARENT alpha background and balanced margins. The combined gizmo and upper-right eye fit within about 86% of canvas width/height. Preserve the source's clean authored toolbar style, not photorealistic or glossy rendering. No labels, letters, title, grid, tile, frame, extra badge background, glow, shadow or checkerboard pixels.

## Local Transforms generation prompt

Use case: precise-object-edit.
Asset type: ONE edited transparent PNG toolbar icon, approval preview for Quader's Local Transforms control.

The supplied image is the actual current Toggle Gizmos icon and the EDIT TARGET. The user explicitly wants to REUSE THIS EXACT DESIGN for Local Transforms, changing ONLY the SIZE OF ITS CENTRAL CUBE.

Preserve the original GREEN upward arrow, BLUE down-left arrow and ORANGE down-right arrow. Preserve their directional arrangement, arrowhead shape, existing gentle rounded shading, thick dark navy outlines, pale highlight style and original compact silhouette. Do NOT substitute a red right arrow and do NOT add an eye.

Enlarge the central solid silver/periwinkle isometric cube to roughly 1.8 times its original linear dimensions, making it about 38% of the entire icon width. Keep the cube centered at the existing common axis origin. It must be geometrically straight and upright: true vertical edges, symmetric top diamond and equally wide side faces. Retain the original pale silver top and muted periwinkle side-face colors. Keep arrow tips at essentially the original locations; shorten the exposed shafts naturally as the larger cube covers their inner ends. Maintain a meaningful visible shaft on all three arrows.
CRITICAL: each arrow shaft emerges from the EXACT CENTER of its corresponding face: green from top face center, blue from left face center, orange from right face center. The face center is the intersection of its diagonals. The underlying three axis centerlines share the cube's 3D center. No off-center attachments near the outer edges, no arrows attached to cube corners, no tilted or skewed cube.

Faithful larger-cube variant of the source artwork, not a new concept or redesign. One centered icon on a square canvas with genuinely TRANSPARENT alpha background and balanced margins, overall design within about 86% of canvas width and height. No eye, no labels, no text, no grid, no decorative frame, no background tile, no glow, no cast shadow and no checkerboard pixels.
