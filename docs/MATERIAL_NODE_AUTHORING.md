# Material node authoring conventions

This guide applies when adding or modifying Material Editor nodes. The CPU material graph is authoritative; renderer preparation and editor presentation are separate consumers. Follow the ownership and history contracts in [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md#editor-ui-conventions).

## Properties and inline controls

- Put the editable node name at the top of the inspector. Do not add a single-field **Node** header. Use `SeparatorText` for a short flat group and unframed `TreeNodeEx` rows for expandable subsections. Reserve framed headers for substantial top-level groups.
- Expose the node's frequently edited value inside the node. The inspector adds detailed settings; it must not become a second source of values. Scalar has a direct value control, Vector2 has X/Y, Constant has a clickable swatch, Texture 2D has its image and Sample Type, and UV has mapping and transforms.
- For an input with an authored fallback, show the fallback editor beside the disconnected pin. Hide that editor when connected, retaining its authored value so disconnecting restores it. This includes Multiply operands, Output defaults and UV Scale XY/Offset XY.
- Inactive Output pins remain visible and dimmed, with their connections retained. Reject new connections to inactive pins. Use central shader-model definitions for activation, labels and types rather than repeating rules in widgets.
- Inline controls, property controls, asset assignment and graph commands use the existing `MaterialDocument` transaction API. A drag is one command. Escape restores its exact starting values. Selection, navigation, expansion and node-preview targets are transient.
- Draw popup contents after suspending the node canvas. Convert the originating control's bounds to screen coordinates once, anchor dropdown lists beside that control, and keep them within the viewport work rectangle. Do not call a raw `ImGui::Combo` inside the transformed canvas. Opening or cancelling a dropdown does not start a document transaction; accepting a value creates one undo command.
- Preserve ImGui's default global theme, font and sizing. Never put tutorials, implementation explanations or redundant action buttons in the inspector body. Use a short tooltip for an operational constraint.

## Color and numeric controls

- Reuse `HdrColorEditor`: a disclosure row and full clickable swatch, with optional expanded linear channel values. Do not show a second RGB editor beside a picker or add an EV slider/derived multiplier label.
- Stored RGB is linear HDR. Merely viewing, expanding or opening the picker must not alter it. Ordinary swatches use linear-to-sRGB conversion; HDR swatches normalize display brightness only and show an HDR marker. Popup hue edits preserve existing HDR strength, while numeric channels allow direct HDR entry.
- Material emission strength is explicit Scalar/Multiply graph math. Particle Brightness is the existing independent multiplier with Constant/Random Range/Curve modes. Never normalize or rebake old colors into another parameter.
- Particle Color reads the emitter's evaluated color, Brightness and Opacity. These attributes affect the material only through connected Particle Color outputs; never multiply the final material by them again. Its RGB and R/G/B/A outputs are source values, so the node has no color picker or editable color default. Ordinary mesh and node previews supply white for this source.
- Constant alpha is labeled **Alpha** and affects coverage only through an explicit graph connection. Particle coverage controls are labeled **Opacity**. Brightness edits never change alpha.
- Scalar slider ranges are soft gesture ranges: numeric entry outside the range remains available. Enforce finite values and the owning property's actual validation limits separately.

## Typed pins and textures

- A central presentation definition supplies names, category accents, pin colors and preview capability. Scalar/math and radial masks use green, constants/vectors ochre, textures blue, mapping and runtime inputs teal, and Output neutral brown. Use literal R/G/B accents for channel pins and retain readable type/semantic labels.
- Keep custom node names and search aliases when display names change. Use **Scalar (1D)**, **Vector2**, **Constant**, **Multiply**, **Saturate**, **Texture 2D**, **UV**, **Particle Color**, **Radial Gradient** and the model-specific Output name.
- Texture **Sample Type** is Color, Linear Data or Normal. Filtering/wrapping belong to Sampler properties, not Sample Type. Roughness/metallic/AO are linear channel data; normal decoding and strength retain their specialized semantics.
- RGB/Color expressions can connect to scalar inputs such as Roughness, Metallic or Saturate. The conversion takes the first component (R), not luminance or alpha; keep the authored source pin and perform conversion at the receiving input. Explicit R/G/B/A connections retain their selected channel. Mapping and Normal remain strict types, and Vector2 compatibility and Multiply inference are unchanged.
- Texture fields include adjacent picker, thumbnail/name, clear and drop behavior. Borrow AssetLibrary thumbnail resources only for UI submission. Never serialize GPU or ImGui handles.
- Keep node titles and texture names within the node's content width. Ellipsize long text on one line without splitting UTF-8 characters, expose the full name on hover, and keep pin columns aligned to the node edges. Asset names must not resize the node independently of its pin layout.
- Canvas drops convert screen position to graph position exactly once. A batch creates its first node at that position and offsets later nodes by 32 graph units. Validate the entire operation before mutation; one drop is one undo command.
- Vector2 multiplies with Scalar or Vector2, not RGB, Mapping or Normal. Mapping remains a distinct type. UV Scale XY/Offset XY accept uniform constants and Multiply expressions; texture-driven transforms are invalid. Use the shared mapping evaluator for shader packets, fallbacks and streaming demand.
- Radial Gradient has an optional UV mapping input and a scalar Value output. Disconnected UV uses identity UV0. It evaluates `max(1 - dot(uv * 2 - 1, uv * 2 - 1), 0)^2`, preserving the former default sprite's soft-disc shape as explicit graph content. It has no hidden radius, strength or fallback color controls; use UV transforms and ordinary Multiply nodes. It is available to every material.
- Saturate clamps one scalar input to 0–1 and exposes a scalar Value output. Its disconnected Input editor accepts any finite value, preserves that authored value while connected, and reappears after disconnection. Use the ordinary math accent, scalar preview and transaction behavior. It remains a uniform expression when its source is uniform. Legacy particle migration uses this visible node before particle-alpha and radial modulation to preserve the original order of operations, including old coverage values above one.

## Common materials and particle attributes

- Materials have Shader Model and Blend Mode, without an exclusive Surface/Particle usage selector. Mesh and particle fields select from the same material catalog. Renderer capabilities and diagnostics must not rewrite the assigned graph or infer a material domain from its blending, name or texture.
- Output coverage activation depends on blend mode: Opaque has neither coverage input active, Masked activates Opacity Mask, and Translucent/Additive activate Opacity. Keep the inactive values and links. A constant color alone leaves the full sprite quad covered; color alpha does not implicitly create coverage or a round shape.
- Masked is valid for both Lit and Unlit particle sprites. For animated cutout coverage, multiply the authored mask by Particle Color A before Opacity Mask. The cutoff discards the entire surface, including emission. Do not change blend mode to repair an unconnected particle alpha path.
- The protected default sprite material uses explicit Particle Color and Radial Gradient graph nodes. It is an ordinary authored graph with a protected asset identity. Save As creates an editable material. Loading an older material materializes its former implicit modulation and shape as visible nodes; do not keep an extra hidden shader branch.
- Particle Editor color, Brightness and Opacity remain emitter attributes. Keep their existing tracks and labels; a material must connect Particle Color to consume those attributes. Put any short explanation in a tooltip, never a new explanatory panel section.

## Interaction and preview

- RMB drag pans; a stationary RMB click opens context actions. Held 1/2/3/M/T plus LMB creates Scalar/Vector2/Constant/Multiply/Texture on empty canvas. Respect text entry, active widgets, popups and drag payload ownership.
- Keep Alt-click disconnection, Q straightening, F/Home framing and clipboard/group commands consistent for new nodes. Output remains unique and protected.
- Start/Stop Previewing and graph-scoped Ctrl+P operate on an explicit transient node/output target. Selection changes do not retarget preview. Deleting the target, changing documents or stopping preview restores the full material.
- Preview Scalars as grayscale, Vector2 as XY color, RGB directly, reconstructed normals in displayable 0–1 form, and Mapping through a neutral checker. Reuse expression and mapping code. Preview recipes/resources must not publish material changes to scenes or thumbnails.
- Material preview geometry is an explicit editor choice, including Sprite alongside the mesh primitives. Never lock geometry or select a material category from connected pins, opacity, shader model or a legacy Usage field. Node previews remain transient; changing preview geometry must not dirty the material.

## References and verification

The local Unreal material graph and color-struct customization informed the swatch/disclosure pattern, inline disconnected defaults, semantic node categories and drop coordinates. Unreal's `MaterialExpressionParticleColor` is a regular source expression; its material domains do not include a separate particle domain, and Niagara selects an authored default sprite material. Its preview primitive is an explicit editor choice. Crimson's Radial Gradient retains Crimson's original soft-disc equation rather than substituting Unreal's different SphereMask function. Godot's material editor informed explicit emission energy and transparent-material expectations. Crimson retains its own shader equations, normal mapping and resource ownership.

For each new node, cover CPU typing/validation, persistence and undo/redo, uniform versus structural changes, generated shader behavior, and headless ImGui interaction. Exercise inline and inspector edits against the same authored field, cancellation, connected-default restoration, pan/zoom coordinates and popup suspension. Native feature acceptance remains user-driven.
