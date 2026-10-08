<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Perspective grid coverage

`ground_grid.mat` integrates a line band's area over a screen pixel. A locally linear plane
coordinate has offset `dx*x + dy*y` over the unit pixel square. After normalizing by the gradient
length, its distribution is the convolution of two centered uniform intervals. The difference
of that distribution's cumulative values at the band's boundaries supplies antialiasing coverage.
The vanishing-interval case uses the uniform limit. No neighborhood samples or temporal state
are required. Resolved lines use this isolated-band integral. Dense lines use periodic first and
second primitives, integrating every stripe intersecting the pixel. A complementary-gap branch
and phase reduction keep the arithmetic bounded when the period or line width is large.

Ordinary line widths are bounded to half the projected base-cell pitch, retaining a line and gap
within each cell. Apply the same bound to parent lines so their broader widths cannot consume
the fine grid's palette at distance. Axes retain their independent width. Dense cells therefore
contribute their averaged area instead of vanishing or becoming an opaque parent-colored plane.
Do not fade U/V families independently: that produces directional stripe bands at oblique angles.

The grid uses the configured minor and major colors, plus the origin axes; there is no separate
1024-map-unit color tier. Nested interval subtraction reserves major and origin slots even when
their authored opacity is zero. Sum distinct tier areas within each direction; allocate the maximum directional
total by axis/major/minor priority, bounded to one. This preserves dense contributions
without stacking opacity at crossings. Widths are measured perpendicular to the projected line;
intensity remains a separate final multiplier.

This coverage implementation was independently derived from the material interface and visible
requirements without access to the preceding shader or foreign source/binary research. The
existing material shell, coplanar depth policy, fixed bounds, configured palette, renderer bindings
and orthographic path are retained. Earlier research remains in Git history; this is a scoped
engineering provenance statement, not legal clearance.

`RenderGroundGridRuntimeTests` checks actual Vulkan pixels: configured palette and alpha over
textured surfaces, fixed bounds and distant visibility, hierarchy slots, reflection and translation,
subpixel integrated area and thickness response. The oblique density regression uses the real
default palette and opacity over nine camera/spacing combinations, checking retained neutral
coverage, smooth density transitions and brightness. The low-opacity contrast fixtures alone
did not expose the original density regression. `GroundGridMaterialTests` retains material/pass
constraints without prescribing the numerical implementation. Perspective antialiasing is not
pixel-identical to the previous approximation; controls and authored colors remain the same.
