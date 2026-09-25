# Outliner entity icon provenance

Created for Quader on 2026-09-25 using the built-in `image_gen` tool at the user's request. These are original generated project assets. The existing Quader Box icon was supplied as a style reference; no external artwork was imported.

The Outliner displays these icons at 16 logical pixels at 100% UI scale, with the existing Qt multiresolution presenter handling scale and device pixel ratio. Generated alpha is preserved; the tool's PNG output is copied directly without post-processing.

Meshes (and existing Full-build mesh instances) reuse `tool_create_box.png`, generated for the primitive toolbar; see [its prompt and provenance](primitive_icons.provenance.md#box). Empty/group entities use three linked square nodes to convey an object hierarchy.

## curve

Asset: `outliner_curve.png`.

Generation source: `exec-534a40e2-ddef-423c-98e4-37f6f21f9459.png`. Edited with the built-in tool from the original curve source `exec-2f04f01e-10ec-4af1-b89f-47b954a9ae3b.png` to increase stroke weight and endpoint size for 16-pixel legibility.

Prompt:

> Use case: precise-object-edit. Asset type: transparent PNG for a native Qt Outliner curve icon shown at 16 logical pixels. Input image is the edit target. Keep the same rising S-shaped curve from lower left to upper right, two square endpoint nodes, silver/periwinkle color family and transparent background. Make it MUCH CHUNKIER to survive 16px downsampling: approximately TRIPLE the thickness of the pale inner curve, to a uniform 17 percent of canvas width (about 2.7px at 16px); reduce its dark navy outline to a fine edge, maximum 1.5 percent of canvas width. Enlarge both square endpoints to 24 percent of canvas width, with generous bright periwinkle centers and only a thin dark edge. Bold solid almost flat fills, smooth round curve, simplified clear silhouette, uniform stroke with no taper. Centered 84 percent canvas occupancy, balanced margins, exactly one curve and two endpoint nodes. No extra geometry, handles, tangent lines, text, arrows, background tile, shadows, glows, or decorative detail. Background must have genuine transparent alpha. This is the same curve icon redrawn with substantially heavier visual weight, not a different object.

## point light

Asset: `outliner_point_light.png`.

Generation source: `exec-d3b4659b-af66-475b-adbd-adb5c32ca7b6.png`.

Prompt:

> Use case: stylized-concept. Asset type: tiny native Qt Outliner entity icon, displayed at only 16 logical pixels. Make ONE original icon, not a sheet. The supplied Quader Box is STYLE REFERENCE only, not an edit target. Match its pale silver and restrained periwinkle surfaces with dark navy edge definition, but simplify aggressively for tiny sizes: bold clear silhouette, broad strokes, almost flat fills, no tiny internal details, no delicate lines, no glossy effects. Center in a square canvas, occupy about 84% with balanced margins. Background MUST be genuinely transparent alpha. No text, letters, badges, background tile, shadow, glow, gradients, watermark, or extra objects. Subject: one familiar upright LIGHTBULB silhouette, front view: rounded pale silver bulb with a short periwinkle screw base. Clearly separate bulb and base using one dark boundary. No filament, surrounding rays, electrical cord, or stand. Must read instantly as lightbulb at 16px.

## directional light

Asset: `outliner_directional_light.png`.

Generation source: `exec-c5ddc6e9-0ba2-486f-9ac0-214bdcd4f523.png`.

Prompt:

> Use case: stylized-concept. Asset type: tiny native Qt Outliner entity icon, displayed at only 16 logical pixels. Make ONE original icon, not a sheet. The supplied Quader Box is STYLE REFERENCE only, not an edit target. Match its pale silver and restrained periwinkle surfaces with dark navy edge definition, but simplify aggressively for tiny sizes: bold clear silhouette, broad strokes, almost flat fills, no tiny internal details, no delicate lines, no glossy effects. Center in a square canvas, occupy about 84% with balanced margins. Background MUST be genuinely transparent alpha. No text, letters, badges, background tile, shadow, glow, gradients, watermark, or extra objects. Subject: one simple SUN symbol, straight-on front view: solid pale silver/periwinkle circular center, surrounded by exactly EIGHT short thick evenly spaced rays, separated from center by generous transparent gaps. Center and rays have restrained dark edge definition. No face, no horizon. Symmetrical and clearly distinct from a lightbulb at 16px.

## group

Asset: `outliner_group.png`.

Generation source: `exec-652338af-9e73-4ae9-b74d-1169e281ee39.png`. Regenerated with the built-in tool to give all three square nodes the same dimensions.

Prompt:

> Create one extremely simple transparent square PNG icon of an object GROUP/HIERARCHY. Precision is essential: ALL THREE SQUARE NODES MUST HAVE THE EXACT SAME WIDTH AND HEIGHT. Use a flat front view, no perspective. Construct the drawing on a 1000x1000 conceptual canvas with three IDENTICAL 230x230 square tiles. Tile bounding boxes: top tile x=385,y=90,width=230,height=230; lower-left tile x=100,y=680,width=230,height=230; lower-right tile x=670,y=680,width=230,height=230. Thus the TOP TILE IS EXACTLY THE SAME SIZE AS EACH LOWER TILE, NOT bigger. Connect tile centers with a thick simple T-shaped hierarchy connector behind them: stem from top tile bottom center to y=500, horizontal branch from x=215 to x=785 at y=500, vertical branches down to each lower tile. Silver-white square interiors, periwinkle connector, very thin dark navy outer edges. Flat solid colors. All three tiles identical clones with matching thin borders. No gradients, shadows, emboss, perspective, text, letters, icons inside squares, extra boxes, background plate or frame. Actual transparent alpha background. This is a tiny native Qt Outliner icon seen at 16px, so use bold simple silhouettes. The three equally sized squares are the only nodes.
