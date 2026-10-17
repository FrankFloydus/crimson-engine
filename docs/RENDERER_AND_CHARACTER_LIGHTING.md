# Renderer and Character lighting implementation guide

Updated 2026-10-17. This is a source-oriented companion for agents changing Crimson's current renderer or Character lighting. [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md) remains authoritative for ownership, implementation status and the next agreed milestone. The implementation described here exists in the checkout; the future work at the end does not.

Read the relevant sections before editing, then verify the source rather than treating a historical screenshot or test count as the current contract. [D2R lighting findings](D2R_CHARACTER_LIGHTING_FINDINGS.md) records the reference evidence and discarded iterations. It does not establish Diablo II: Resurrected's unpublished equations or require copying its complete pipeline.

The Windows renderer defaults to Direct3D 12. **Edit → Preferences → Rendering → Renderer (restart required)** selects Direct3D 12 or Direct3D 11 for the next launch; the tooltip reports the active backend. `--renderer dx11|dx12` overrides that preference for one process without persisting it. The engine-owned `GraphicsBackend` value is selected before bgfx initialization and does not belong to scene or Environment data. The renderer validates the requested backend and checks native blendable RGBA32F support using the matching D3D11/D3D12 device. Both backends retain the same linear HDR lighting, final sRGB encoding, effects and SM5/DXBC shader/cache/preflight contracts. Switching APIs alone adds no rendering effects and does not require material recompilation. Vulkan is not enabled. Historical D3D11 measurements below retain their original scope; they are not DX12 measurements or cross-backend speed claims.

## Feature map and entry points

DX12 uses the same pinned bgfx version with [project-owned patches](../cmake/vcpkg-ports/bgfx/README.md) for valid small-mip footprints, staging-buffer written ranges and timestamp publication after the resolving command list completes. Native platform-window retirement uses a queue marker after `Present`; the focused GPU validation check covers collapse, resize, close/reopen, captured pixels and shutdown.

| Concern | Current owner and entry points |
| --- | --- |
| Backend selection and application preferences | [GraphicsBackend.h](../src/crimson-renderer/GraphicsBackend.h), [EditorPreferences.cpp](../src/editor/EditorPreferences.cpp), [Options.cpp](../src/app/Options.cpp), [App.cpp](../src/App.cpp) |
| Authored Character and light data | [Scene.h](../src/scene/Scene.h), [LightingSettings.h](../src/crimson-renderer/LightingSettings.h), [RenderTypes.h](../src/crimson-renderer/RenderTypes.h) |
| Validation, transactions, persistence and conversion | [Scene.cpp](../src/scene/Scene.cpp), [SceneDocument.cpp](../src/scene/SceneDocument.cpp) |
| Actor Inspector and shared lighting widgets | [ScenePanels.cpp](../src/tools/ScenePanels.cpp), [LightingControls.cpp](../src/tools/LightingControls.cpp) |
| Pure actor rig evaluation | [ActorLightRig.cpp](../src/runtime/ActorLightRig.cpp) |
| Presented scene categories, light arrays and reveal | [SceneExtraction.cpp](../src/scene/SceneExtraction.cpp) |
| Prepared world/camera/resource ownership | [EditorRenderFrame.cpp](../src/editor/EditorRenderFrame.cpp), [RenderPassPlan.h](../src/crimson-renderer/RenderPassPlan.h) |
| Submission, HDR targets and shared appearance | [Renderer.cpp](../src/crimson-renderer/Renderer.cpp) |
| Forward+ records and camera assignment | [ClusteredLightingGpu.cpp](../src/crimson-renderer/ClusteredLightingGpu.cpp), [clustered_lighting.sh](../src/crimson-renderer/shaders/clustered_lighting.sh) |
| Point, directional and dedicated Key shadow ownership | [PointShadowRenderer.cpp](../src/crimson-renderer/PointShadowRenderer.cpp), [DirectionalShadowMath.cpp](../src/crimson-renderer/DirectionalShadowMath.cpp) |
| Darkness depth/evaluation/publication | [AmbientVisibilityGpu.cpp](../src/crimson-renderer/AmbientVisibilityGpu.cpp), [ambient_visibility.sh](../src/crimson-renderer/shaders/ambient_visibility.sh) |
| Dynamic GI settings and Environment persistence | [Ssgi.h](../src/crimson-renderer/Ssgi.h), [Ssao.h](../src/crimson-renderer/Ssao.h), [EnvironmentSerialization.h](../src/scene/EnvironmentSerialization.h), [ScenePanels.cpp](../src/tools/ScenePanels.cpp) |
| Scene/Game rig visualization | [ActorLightRigOverlay.cpp](../src/editor/ActorLightRigOverlay.cpp) |
| Material publication and startup | [MaterialGraphGpu.cpp](../src/crimson-renderer/MaterialGraphGpu.cpp), [material startup contract](MATERIAL_SHADER_STARTUP.md) |
| Diagnostics and exact counter meanings | [SceneDiagnostics.cpp](../src/tools/SceneDiagnostics.cpp), [shadow diagnostics guide](SHADOW_DIAGNOSTICS.md) |

## Actor authoring workflow

Create a composed actor with **Add → Player** or **Add → Enemy**. Each actor appears as one Outliner entity. Selecting it exposes all applicable Inspector sections, including Light Rig and Darkness Reveal; Player also supplies a Camera section and Visuals authoring. Owned camera/model/particle spatial records retain their IDs and local transforms internally. Nearest-actor lighting ownership stops at nested actors. Each actor’s rig is independent of Motor, Character Controller, Navigation Agent and Player Input. `SceneProperties::playerCharacter` selects the input owner and player-focused camera/fading context; it does not select the only valid rig or reveal.

Select the actor entity and expand **Inspector → Light Rig** to edit its lighting. Components are Inspector subsections rather than separate Outliner selection targets. **Visuals** contains owned model/particle settings, and **Camera** edits the supplied camera without altering the controller transform. Darkness Reveal remains a separate optional capability. Edit mode affects Scene and Game; Play changes remain temporary. Component/history/storage ownership stays with SceneDocument; shared widgets own no renderer-global settings.

**Light Rig → Enabled** defaults off. Key, Fill and Local World each retain their own enabled settings beneath it. **Darkness Reveal** is a sibling section and works independently of the rig master. This separation is intentional: revealing existing global light is different from emitting a local light.

The lighting roles are deliberately separate:

| Contribution | Receivers | Casters/occlusion | Purpose |
| --- | --- | --- | --- |
| Character Key | Owning actor visual subtree only | World geometry does not block it | Stable readable actor shading and highlights |
| Character Fill | Owning actor visual subtree only | No shadow map | Additional actor shading; never lights architecture |
| Local World Light | World only | World geometry casts into its six point faces; actor surfaces excluded | Actor-following omni illumination and pillar/doorway shadows |
| Key silhouette | World with Receive Shadows | Only eligible player-subtree models | Strong player silhouette without Key illumination on the environment |
| Darkness Reveal | Eligible ambient receivers | Distance only; no map | Restores existing illumination near the player |

### Character Key and Fill

Both are directional irradiance values, not scene entities or ordinary point slots. **Direction** is the world-space direction in which light travels. It remains fixed when the actor turns. Directions normalize during evaluation; zero/invalid directions are rejected. Character lights cannot be blocked by a nearby pillar.

| Control | Key default | Fill default | Authored range |
| --- | --- | --- | --- |
| Enabled | On | On | Boolean |
| Direction | `(-.5, -.8, -.6)` | `(.7, -.4, .6)` | Inspector components -1…1; finite nonzero vector |
| Color, sRGB | `(1, .96, .9)` | `(.55, .65, 1)` | Each channel 0…1 |
| Intensity | 1.5 | .35 | 0…10,000 |
| Advanced → Diffuse | 1 | 1 | 0…1 |
| Advanced → Specular | 1 | 1 | 0…1 |

The Key additionally owns **Cast Shadow** (on), **Shadow Strength** (.8, range 0…1) and **Shadow Range (m)** (20, range .01…100,000). Advanced **Shadow Depth Bias (m)** defaults .005; **Shadow Normal Bias (texels)** defaults 1. Both accept finite nonnegative values. Fill has no shadow controls.

The silhouette derives its direction and enable state from Key. Rig/Key/Cast Shadow off, zero Key intensity, zero Strength or no eligible drawable player caster suppresses it. It uses a bounded, camera-independent orthographic projection fitted around eligible player bounds, quantized in extent and snapped to texels. Range extends downstream receiver depth; it is not an omni radius.

This is an artistic reflected-light mask: `mix(1, keyVisibility, strength)` multiplies all reflected World lighting, including torch light and IBL. That keeps the player silhouette readable near a bright torch. It adds no illumination to World and does not make pillars cast Key shadows. It leaves emission, Unlit and fog independent. This is Crimson's explicit behavior, not a claim about D2R internals.

### Local World Light

Each actor evaluator emits at most one ordinary `PointLight` at `presentedActorPosition + (0, Height, 0)`. Enabled sources join the common point-light/shadow budgets and retain a deterministic transient identity derived from owner EntityId. Height, radius and Key/Fill directions remain world aligned and independent of actor rotation/scale.

| Control | Default | Range and effect |
| --- | --- | --- |
| Enabled | On | Emits the point only while the rig master is on |
| Height (m) | 2 | .01…100,000; moves the real source and shadow origin |
| Color | `(1, .96, .9)` | sRGB channels 0…1 |
| Intensity | `64/9` ≈ 7.111 | 0…1,000,000; HDR radiance multiplier |
| GI Energy | 1 | 0…16; diffuse donor energy for SSGI, independent of direct brightness |
| Radius (m) | 8 | .01…100,000; finite influence and point-shadow far coverage |
| Attenuation | 2 | -10…10; independent distance-decay exponent |
| Near Falloff Radius (m) | 1.5 | 0…100,000; smooths close-range intensity peak |
| Range Falloff | 1 | .25…8; shapes the outer window |
| Edge Softness | 1 | .05…1; fraction of Radius used for the outer fade |
| Source Radius (m) | 0 | 0…100,000; specular extent and point-shadow penumbra |
| Specular Scale | 0 | 0…1; reflective highlights, independent of diffuse/shadows |
| Cast Shadows | On | Ordinary World-only point-map participation |
| Advanced → Depth Bias (m) | .005 | Finite nonnegative comparison offset |
| Advanced → Normal Bias (texels) | 1 | Finite nonnegative geometric-normal offset |

This is normal-dependent PBR illumination. A wall facing the source can become brighter; a circular/elliptical footprint on a bare floor follows finite influence and distance decay. No claim is made that a point light eliminates that shape. Tune Radius, Intensity, Attenuation and the range window for the scene, rather than reintroducing a hidden normal-independent fill or doorway leakage workaround.

The Local World light has fixed World receiver/caster masks and `ambientReveal=false`. Thus pillars block it, the player does not receive/cast its omni shadows, and the light does not reveal its own darkness footprint. The independent player reveal restores global illumination. Open doors can physically pass point light; the distance reveal can restore IBL across a wall because it is not room visibility.

### Shared point response and source-size shadows

Ordinary points expose the same response fields. Their defaults differ: Near Falloff 0, Specular Scale 1, Range Falloff 4, Edge Softness 1, Source Radius 0, Attenuation 2, Radius 20 m, Intensity 35 and Cast Shadows off. Ordinary authored point intensity is bounded to 0…10,000. Preserve these defaults when adding imports or adapters.

For distance `d`, near radius `n`, influence radius `R`, exponent `p`, softness `s` and attenuation `a`, the response is:

```text
t = clamp((d/R - (1-s))/s, 0, 1)
window = 1 - t^p
response = window² * max(d²+n², .01)^(-a/2)
```

The ordinary `p=4, s=1, a=2` branch keeps its original arithmetic exactly. `a=2` is inverse square; 1 is inverse distance; 0 retains only the range window. Negative exponents intentionally grow with distance until the range fade. General exponent evaluation is bounded for finite HDR output. This is Godot-style power decay with Crimson's existing near softening/numerical floor; it is not Godot's exact close-distance clamp.

See [point_light.sh](../src/crimson-renderer/shaders/point_light.sh). Source Radius does not change that equation or Lambert diffuse orientation. It broadens GGX/Smith specular response using angular emitter size. **Specular Scale 0 does not disable soft shadows.**

[point_shadow.sh](../src/crimson-renderer/shaders/point_shadow.sh) evaluates the source-zero 3×3 PCF footprint as 16 unique raw-D32 comparisons. Each texel uses its own receiver-plane reference and the equivalent separable bilinear weights. Numerical comparison tolerance is separate from the authored offsets: an IEEE single-precision `gamma(8)` allowance covers projection/reference arithmetic, and absolute-world coordinate roundoff propagates through the receiver-plane perspective-depth derivative. A fixed two-D32-unit tolerance was insufficient on translated walls near the point source. Point-shadow casters apply light view and projection separately to reduce that cancellation; the readable scene prepass retains the color pass's combined projection for depth parity. PCF and PCSS normal offsets scale with `sin(theta)` rather than `1-cos(theta)`, preserving protection near normal incidence.

At mixed contact edges, a coherent 2×2 patch of genuine blocker depths can reconstruct a missed central blocker, extrapolating at most one shadow texel. Its depth plane must differ from the receiver, and it is rejected if it predicts blocking any observed sample that matches the original, unbiased receiver plane. Contact sharpening is measured against the projected plane intersection and returns to the full filter between two and four shadow pixels. This is bounded coverage completion, not mesh adjacency recovery; finite geometry with no contradictory receiver samples remains ambiguous. The fallback uses the closest confirmed blocker/receiver physical separation, restoring full filtering between two and four receiver texels. Brightening either path additionally requires the central depth to match the receiver plane; an empty or mismatched central depth retains the filtered result. Fully lit or blocked footprints skip reconstruction, allowing for the numerical accumulation bound of the 16 weights.

Keep one terminal PCF call in `pointShadowVisibility`: `tryPointShadowVisibilitySoft` reports whether PCSS supplied a result, and all fallback conditions reach that shared call. SM5 inlines the substantial contact filter at each call site; duplicating it in individual PCSS returns increases shader size, temporary registers and compilation time.

The receiver plane itself is not a blocker. Authored depth/normal offsets still apply and can independently separate shadows from zero-thickness walls; the filter does not silently cap those values. For contact-sensitive joined walls, use **Depth Bias 0 m / Normal Bias 1 texel**; zero authored Depth Bias still includes the numerical comparison tolerance. There is no global conversion of authored settings or defaults. Positive source radius uses a bounded deterministic PCSS search/filter: 12 blocker samples, radial blocker-distance reconstruction from perspective face depth, and 16 visibility taps. Penumbra radius is proportional to `sourceRadius * (receiverDistance-blockerDistance)/blockerDistance`. Contact stays sharper than distant shadow edges.

Wide samples remap to their proper cube face rather than crossing an atlas tile into another light. Receiver-plane correction and a receiver-normal-based stable kernel preserve sloped surfaces and cube seams. It uses the existing comparison atlas/raw depth access, with no extra sampler, shadow map or depth-render pass. Source-size/attenuation edits can change shading immediately while all six depth faces remain cached. Positive size increases query work; zero depth draws is not zero shadow cost. Key and ordinary directional filtering remain separate and retain their sharpness.

## Darkness Reveal and Environment Ambient Darkness

**Darkness Reveal → Enabled** defaults on when this independent component is added; Spread defaults to 8 m and accepts .01–100,000 m. A controller contributes its half-height to the source center; otherwise the owner origin is used. Every enabled reveal is extracted and packed into the shared Forward+ atlas after light records. Source visibility combines by maximum, with no extra material sampler. Adding a Light Rig does not implicitly reveal darkness.

The player weight is `exp2(-(distance/spread)²)`, with an already negligible tail smoothly tapered to zero over six to eight Spreads. It emits no light, consumes no point record and casts no shadow. It is not an occlusion query or fog of war.

**Environment → Ambient Darkness** owns the global response:

| Control | Default | Meaning |
| --- | --- | --- |
| Enabled | Off | Enables the additional readable-depth/darkness path |
| Darkness Floor | 0 | Remaining reflected light outside reveal; range 0…1 |
| Softness | .25 | Scene-light energy transition width; UI .001…1 |
| Advanced → Strength | 1 | Mix original and darkened lighting; 0…1 |
| Advanced → Threshold | .1 | Scene-light energy needed for reveal; 0…10,000 |
| Advanced → Respect Shadows | On | Uses available scene-light shadow visibility for energy reveal |
| Advanced → Affect Emission | Off | Also attenuates Lit emission with darkness |

Ordinary scene lights independently expose **Affect Ambient Darkness**, reveal strength and reveal receiver scope. Points default to reveal enabled; directionals default off. Reveal eligibility does not depend on direct receiver acceptance. Respect Shadows uses available maps under their existing admission/update budgets; an unshadowed/nonresident source can reveal through an occluder.

Scene-light energy passes through `smoothstep(threshold, threshold+softness, energy)`, then Darkness Floor/Strength. Player visibility uses the Gaussian with the same floor/strength. The two are combined by **maximum**, so a torch can reveal an independent island without double-applying the player fade. Source influence, not final shaded pixel brightness, supplies energy; there is no lighting-feedback loop.

Composition is `reflected * keySilhouette * darkness`, where reflected includes directional/point lighting, Character Key/Fill, custom ambient and diffuse/specular IBL after their ordinary material/scope/shadow response. Affect Emission optionally applies darkness to Lit emission. Unlit, fog, background and post-processing remain independent. An ambient receiver mask of zero explicitly opts out (`darkness=1`); disabling every reveal source does not opt eligible surfaces out.

Opaque/masked surfaces produce separate readable D24S8 depth and RGBA8 receiver data with matching coverage and clocks. Receiver R stores scope; the remaining channels pack 12-bit octahedral normal coordinates. [AmbientVisibilityGpu](../src/crimson-renderer/AmbientVisibilityGpu.cpp) reconstructs receivers, writes R32F scratch, then publishes four visibility values per RGBA32F texel in the existing cluster atlas. Evaluate/Publish are separate dispatches. The color depth attachment is not sampled while bound.

Opaque materials read published visibility; transparent meshes and Lit sprites evaluate the same response at their own world position. They must not receive a screen-space mask reconstructed from background geometry. Disabled darkness avoids these extra passes/initial allocations; previously allocated reusable resources can remain resident.

Ambient Darkness's computed mask bounds the uncertainty from its D24 readable depth and float inverse-projection arithmetic. If that uncertainty exceeds an eligible resident shadow's authored metre bias, the compute evaluation publishes a negative sentinel before querying that shadow. The opaque material then uses the existing exact-fragment `ambientVisibilityAt` path at its original interpolated surface position. Valid receivers rounded to the clear depth use the same fallback. Raw R32F scratch and RGBA32F atlas publication preserve the sentinel; it never becomes a negative lighting multiplier. This removes camera-angle bands caused by very small editor near planes without altering camera clipping or shadow bias, allocating another target, or performing both complete shadow evaluations. Reliable samples retain the computed mask, while fallback samples pay the existing per-surface light/shadow evaluation cost. A focused ceiling-free fixture covers Scene/Game, three angles, small/normal near planes, Respect Shadows off/on and real blocker preservation.

## Multiple-actor rig resources

Immutable extraction/frame records contain `actorLighting`, `actorShadows` and `reveals` lists. Render models carry their stable actor owner through visible, shadow, preview and thumbnail preparation. Each draw binds its actor's two Key/Fill values, retaining shared material assets and per-instance ownership. Local World points use the existing shared clustered/light-shadow limits. Dedicated actor silhouettes have eight atlas slots with deterministic owner-sorted admission, cache identity including the owner, and shadowless fallback for unadmitted requests. Requested/admitted/unadmitted counters expose that limit. Disabling or reordering rigs resets their draw data and cannot redirect another actor's lights or cached shadow. Independent reveal contributions use maximum visibility.

## Scope masks versus transient categories

`lighting_scope::World=1`, `Character=2`, `All=3`. These are two current bits, not an arbitrary general-purpose layer system.

Models author independent **Advanced Lighting → Direct Lighting**, **Shadow Casting** and **Ambient Darkness** masks. Lights author **Advanced Lighting → Affect**, **Shadow Casters**, **Affect Ambient Darkness**, **Ambient Receivers** and **Ambient Strength**. Removing direct acceptance must not implicitly remove casting or darkness participation. Model **Cast Shadows** and **Receive Shadows** remain separate controls.

Extraction assigns Character to models owned by their nearest Visual Root, and World to unowned models. Authored surface masks intersect that broad category. A separate actor-owner identity selects private Key/Fill data for each prepared draw, preventing one enemy's rig from lighting another. Disabled/missing rigs bind neutral light values. Dedicated silhouettes accept World/Receive Shadows surfaces and select only their owner's shadow casters.

Transient categories, actor-owner indices, evaluated sources, shadow slots and GPU handles are never serialized. Visual ownership is independent of player designation and of rig enablement. A stationary actor can receive a rig; a moving controller does not automatically establish visual ownership.

## Environment Ambient Light, sky and fog

One persistent active-Environment entity selects scene settings. Its transform is not a local lighting volume. The selected HDRI/studio or procedural sky supplies irradiance and roughness-prefiltered specular lighting. Procedural sun linking is explicit; the directional entity remains the direct-light source.

**Ambient Light** defaults to Source Sky, Color black, Sky Contribution 1 and Energy 1, preserving previous sky lighting. The shared Inspector controls are Source Disabled/Color/Sky, Color, Sky Contribution (0…1, Sky only), and Energy (UI 0…16). Stored Energy accepts finite nonnegative values; rendering clamps it to the finite HDR envelope 0…60,000 without rewriting data.

For decoded linear color `C`, Energy `E`, contribution `S`, diffuse sky irradiance `I` and existing environment intensity `J`:

```text
Disabled: diffuseAmbient = 0
Color:    diffuseAmbient = C * E
Sky:      diffuseAmbient = C * E * (1-S) + I * J * S
```

Energy scales the **custom color**, not the entire blend. At S=1 it has no effect; Environment Intensity controls the sky. Environment lighting Enabled/Intensity controls sky/reflections, while custom-color ambient remains independent. Ambient Source Disabled removes diffuse ambient only; reflections keep their existing environment response. This separation follows the inspected Godot renderer semantics.

[EnvironmentPresentation.h](../src/crimson-renderer/EnvironmentPresentation.h) converts/weights custom color once on the CPU. [environment_ambient.sh](../src/crimson-renderer/shaders/environment_ambient.sh) is shared by fallback/generated meshes and Lit particles. The shader applies normal material diffuse response; ambient color is not an emissive overlay. It participates in Ambient Darkness and World Key silhouette. Independent material/particle previews retain their own environment, rather than inheriting the authored scene.

Fog supports Exponential and Depth response, height density and Light Energy. **Sky Affect** (default 1) scales background fog opacity; **Aerial Perspective** (default 0) blends fog color toward the sky along the viewing ray, and **Sun Scatter** (default 0) adds Godot's broad forward scattering toward enabled directional lights. All three range from 0 to 1. Sky Affect controls only the background; Aerial Perspective and Sun Scatter also affect fog on surfaces/particles, while additive materials retain attenuation only. The existing environment sampler supplies aerial perspective without consuming material texture slots, independently of environment illumination Enabled.

Background opacity evaluates distance/height fog at the camera's far-distance proxy, scaled by Sky Affect. Exponential Density approaches zero continuously; Density 0 and Height Density 0 restore the clear sky or exposure-independent matte, and Sky Affect 0 leaves it clear with finite-surface fog still active. The provided Godot checkout's background-opacity policy is independent of density; Crimson deliberately uses density-aware transmission to satisfy these controls. Hidden backgrounds retain premultiplied fog coverage over the unexposed display matte; visible skies mix in HDR. Color/scattering precede exposure and tone mapping. The material's **Receive Fog** toggle permits black cutaway materials to remain unaffected by fog; opaque excluded surfaces still cover the fog background. It does not change shadow casting, ambient masks or occluder visibility. Grid and editor overlays keep their later exposure-independent composition.

## Tone mapping controls and lighting inspection

The shared tone controls sit beside the existing selector in the Environment Inspector's Post-processing section and Material/Particle Editor Environment menus. `EnvironmentSettings::toneMappingParameters` retains all operator values while the selected operator determines which fields are visible. Reinhard White Point defaults 0 (range 0…64), preserving the unlimited legacy curve; a positive value selects extended Reinhard with that exposed HDR input reaching display white. ACES White Point defaults 1 (1…64). AgX White Point defaults 16.29 (2…64) and Contrast defaults 1.25 (1…4). Linear exposes no curve parameters. Exposure remains an independent multiplier before tone mapping, and White Point is measured in that exposed HDR domain. Preview environments keep their own values.

The optional `toneMappingParameters` JSON object writes `reinhardWhite`, `acesWhite`, `agxWhite` and `agxContrast`. Shared scene/legacy-environment serialization validates finite numbers and each range, including inactive operators; missing fields preserve previous defaults without rewriting or dirtying the source. Positive Reinhard White Point values below 0.01 use 0.01 for numerical stability; zero remains the unlimited-curve sentinel. Existing ToneMapping enum values and scene versions remain unchanged. Whole-Environment transactions preserve undo/redo, cancellation, duplication and Edit/Play isolation. Saved-scene thumbnails follow the authored curve.

`ToneMapping.cpp` is the CPU reference for `tone_mapping.sh`. The CPU computes normalization and curve coefficients through `toneMappingUniforms`; Renderer binds `u_toneCurve` and `u_toneShoulder` for every presentation, including alternating Scene/Game views. These uniforms do not enter material shaders or the generated-material ABI. Curve edits require no material compilation or environment preprocessing. Default parameters preserve the existing Reinhard/ACES/AgX output and the single final sRGB encoding.

## Dynamic global illumination

The Environment entity's collapsed **Global Illumination** section enables SSGI (default off). Strength defaults 1 (0…4), Bounces 1 (integer 1…4), Max Distance 3 m per bounce (.1…20 m), Quality Medium, Advanced Thickness .1 m (.01…2 m) and Temporal Stability .85 (0….98). Low/Medium/High trace 2/4/8 rays with 12/16/24 steps per segment at half resolution. Custom supports Samples Per Pixel 1…16 and Ray Steps 8…64; editing either budget selects Custom while preserving the other resolved preset budget. Advanced exposes Ray Bias .02 m (.001….5 m), Denoise Radius 3 half-resolution pixels (0…4), Denoise Strength 1 (0…1) and Radiance Clamp 10 HDR units (0…100; zero disables it). There is no lighting bake. Open levels do not require ceilings, but offscreen and hidden donors remain unavailable.

The independent collapsed **Screen Space Ambient Occlusion** section defaults off and exposes Strength 1 (0…2), Radius .5 m (.05…5 m) and Quality Medium (Low/Medium/High). SSAO adds contact shading to indirect light from nearby visible depth. It does not supply bounce lighting and is separate from artistic Ambient Darkness. Zero Strength bypasses either effect independently. Low/Medium/High use 4/8/12 directions with 3/4/6 horizon steps, followed by a depth/normal-aware filter and upsampling. There is no SSAO history or activation warmup. Direct light and emission remain independent; transparent and partially faded surfaces bypass SSAO.

The shared serializer writes flat optional `ssgi` and `ssao` objects. Current `ssgi` takes precedence; otherwise, the removed `gi` wrapper converts once in memory from its common and `screenSpace` values. Writers discard removed method/world-volume settings. Missing effects stay off, and missing earlier Bounces stays one. Loading does not rewrite or dirty authored files. Whole-Environment transactions preserve disabled tuning, duplication, undo/redo and Play isolation/reset. Material/Particle previews and browser thumbnails omit both effects.

`SsgiGpu` owns three full-resolution donor attachments (World/Character RGBA16F and geometry RGBA32F), half-resolution tracing, temporal histories and bilateral filter targets. Direct diffuse follows the light receiver masks; environment diffuse and eligible Lit emission supply both lanes. Private Key/Fill, specular, fog and previous GI are excluded from donors. Full-opacity opaque/masked donors and receivers participate; Alpha/Additive retain opaque source attachments, while Unlit and fully metallic receivers gain no diffuse bounce. Full-resolution publication reuses the Forward+ data atlas rather than adding a material sampler. Strength and receiver diffuse response apply once before the normal silhouette/darkness/fog composition.

Ordinary directional/point lights and Local World Light expose **GI Energy**, stored as finite `indirectEnergy` in `[0,16]`, default 1. It multiplies only that source's shadowed diffuse donor contribution before World/Character lane accumulation. Zero removes the source's reflected contribution, while higher values artistically boost it; direct/specular lighting, source range, shadow tests and ambient reveal remain independent. Material emission and environment donors are not multiplied by a light's setting. The control does not bypass SSGI visibility or the Environment's Radiance Clamp. Private Character Key/Fill still supply no GI. Missing fields retain 1 without load-time writes; shared scene transactions, duplication, runtime copies and render extraction retain the authored value.

Geometry A stores validity/reactivity plus seven bits per RGB diffuse-response channel as an exact 23-bit integer in RGBA32F. Additional path segments multiply the decoded, bounded response at intermediate hits, keeping material color and energy loss separate from Strength. Each segment has its own Max Distance bound. Captured radiance remains free of prior GI; changing Bounces extends the path rather than increasing a feedback multiplier.

Tracing samples the full cosine hemisphere over time, refines perspective-correct depth crossings and retains accepted-hit energy throughout Max Distance. Radiance second-moment history clipping preserves sparse indirect samples while rejecting inconsistent history. The first Screen Space frame after enabling/resetting contains zero GI. Camera cuts, projection/size changes, frame gaps, identity/settings/program changes and invalid geometry reject old history. Moving/deformed geometry avoids stale accumulation. Separate Scene/Game owners account for their active, pending and retired resources and release them before bgfx shutdown. Unsupported capabilities/allocation errors keep ordinary lighting and expose a status in the active Environment Inspector without switching authored Enabled off.

For a clear Screen Space visual comparison, place a colored Lit emissive wall beside a rough neutral, nonmetallic floor, keep both visible and within Max Distance, and temporarily remove other illumination. The floor should gain the wall's color with GI enabled. An ordinary light illuminating one surface can similarly supply bounce to a nearby surface facing it. Light to floor to wall is one indirect bounce; continuing from that wall back onto the floor requires Bounces 2 or higher. Strength scales the result rather than extending propagation. Every added segment remains limited to visible geometry, so higher bounce counts cannot restore offscreen transport. Glow is a separate screen-space effect and does not illuminate nearby geometry.

Point lights supply GI through the diffuse light reflected by opaque/masked Lit models, including generated materials and shadowed lights. Indirect Lighting deliberately omits the point's direct pool. A point-lit flat floor cannot reflect diffuse light back onto the same coplanar floor; visible facing walls or prop sides provide that path. GI can illuminate dimmer or shadowed receivers through those reflected paths, including beyond the source's direct Radius when the GI trace can reach a lit donor. It does not flatten the point's direct distance falloff or uniformly fill its range. Horizontal prop tops mostly reflect toward the upper hemisphere, and hidden sides cannot act as screen-space donors. Ambient Darkness also attenuates captured reflected lighting; Direct Lighting inspection bypasses that final artistic mask, so a bright direct diagnostic does not by itself establish a bright GI donor.

A focused point-only saved-project reproduction disabled model emission, sky/ambient, directionals and private actor lights while preserving scene geometry, ordinary point lights and authored SSGI settings. GI brightened 14,800 Scene pixels and 17,877 Game pixels in Lit at 909×684, while Direct Lighting captures were identical with GI on/off and the no-point control was exactly black. Isolating the weak reflected signal under the saved ACES curve left only 32 detected Scene pixels; presenting the same camera/light/settings with Linear revealed 89,892. The coffin materials were opaque Lit nonmetals with full per-view opacity. This establishes working point-light transport and a display-curve limitation; it does not establish room-wide fill or remove screen-space visibility limits.

The isolated `ssgi_render_core` GPU fixture covers that ceiling-free comparison, changing/removing emission, moving geometry, discarded cutouts, partially faded models, generated/fallback materials, scope separation, private Key exclusion, transparent auxiliary preservation, darkness composition, independent cameras and disabled/zero-strength output. Its point-only case places a finite-range point above the floor so it directly illuminates a wall while excluding the floor: the floor is black in Direct Lighting and receives colored bounce in Indirect Lighting. It checks generated/fallback donors, light removal, World/Character isolation with packed reveal flags and an actual resident point-shadow path. A second fixture requires an emitter-to-wall-to-coplanar-floor path: one bounce leaves the floor black, while two through four transport light through the wall. It checks Unlit, metallic and scope-blocked intermediate surfaces, generated material response, emission removal and unchanged bounce-count allocations. Its executable accepts `--artifacts <directory>` to export off/on, point-only and 1/2/4-bounce PNGs, and `--benchmark --warmup 120 --samples 360` for a bounded comparison; its three required path arguments are supplied by the CTest registration.

On 2026-10-17, the updated D3D11 RTX 4090 fixture at 1920×1080, Medium, measured these whole-frame GPU medians with 160 warmup frames and 300 measured submissions per case (232/269 distinct completed GPU samples for the off cases, 300 for each on case). The interactive editor was closed; CPU material-cache preparation was running. The benchmark scene contains two quads, so many multi-segment paths terminate early; these figures describe the synthetic local workload, not gameplay cost or a lower-end GPU budget. Richer geometry can keep additional segments active and increase cost. GI-on p95 was .973/.647/.866 ms for Game at 1/2/4 bounces, and 1.074/1.233/1.412 ms for Scene+Game.

| Views | GI off | 1 bounce | 2 bounces | 4 bounces | GI owner memory | Additional environment GPU allocations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Game | .033 ms | .368 ms | .388 ms | .434 ms | 110.7 MiB | 142.6 MiB |
| Scene + Game | .058 ms | .647 ms | .724 ms | .829 ms | 221.5 MiB | 301.0 MiB |

The measured median overhead above GI off was .335/.355/.401 ms for Game at 1/2/4 bounces, and .589/.666/.771 ms for Scene+Game. The owner count includes donor/trace/filter/history resources and does not grow with bounce count. The environment allocation difference also includes readable depth and atlas capacity, and counts submitted/retired resources while resident. Disabled GI starts with no GI owner allocations; this MRT/history design has a substantial memory cost even when its small-scene GPU time is low.

The sparse bright-donor check uses an odd 217×163 viewport and uninterrupted rendering during readback. Correct history lookup maps reprojected full-resolution capture centers back to their half-resolution samples; using UV times the rounded-up half extent caused stationary history to drift across odd targets. With that correction and variance-aware clipping, the fixture measured mean linear irradiance .037 without history and .036 with Temporal Stability .9, while temporal standard deviation fell from .012 to .003. These are local fixture measurements, not a guarantee for moving or offscreen geometry.

Scene and Game each expose a top-right render-view dropdown with Lit, Albedo, Roughness, Metal, Normal, Direct Lighting and Indirect Lighting. The selection is transient and independent per viewport; previews stay Lit. Data views bypass exposure, tone mapping and fog and preserve final sRGB encoding. World normals map [-1,1] to [0,1]. Direct/Indirect Lighting isolate their corresponding shader contribution and retain authored exposure, tone operator and curve parameters. ACES's fitted toe can clip weak isolated indirect light to black even when adding it before tone mapping visibly changes Lit. Compare GI on/off in Lit or temporarily inspect with Linear at fixed exposure when diagnosing that case. The toolbar owns its own gestures outside the rendered image input rectangle.

## Renderer data, passes, shadows and lifetime

Current local lighting is GPU clustered Forward+: up to 4,096 point sources, initial 32-pixel tiles and 24 logarithmic depth slices per camera. Very large viewports grow tile size. Dense clusters retain all influencing lights within source capacity. The all-lights reference path is a correctness comparison, not a second authoring mode. Up to four ordinary directionals remain independent.

The generated shader texture budget is fixed: graph textures 0…11, IBL cubes 12…13, combined BRDF/Forward+ data atlas 14 and shared shadow comparison atlas 15. Keep BRDF filtering/coordinates intact when extending the atlas. Do not add a sampled darkness texture or source-size sampler casually.

Each point record contains six vec4 texels with the existing attenuation/source-size/material-role response. Zero-based row five carries the attenuation exponent and GI Energy minus one, followed by two reserved zeros. `u_clusterRecordLayoutV6` still guards the six-texel stride. Independent reveal records occupy a subsequent atlas section described by `u_revealAtlas`. Generated-material identity is v23, retaining the actor light/silhouette arrays, shared GI atlas/draw interface, packed donor response and four color outputs. SSAO and viewport diagnostic uniforms belong to the same coherently replaced shader family. Preflight rejects obsolete bindings and render-target signatures; staged source/cache identity and complete last-good publication remain mandatory.

Direct capacity is distinct from shadow residency: **16 admitted point maps**, six guarded D32F projections each, and a normal update budget of **4 maps/24 faces** per world frame. The player omni consumes one source and, when eligible, one map. Key/Fill, reveal and dedicated silhouette consume no ordinary point slots. Scene validation includes the enabled player point before committing edits/loads.

Point admission/update selection uses spatial priority, cache state and fairness. Deferred caster-only updates can retain a coherent previous projection; a changed light projection must not sample obsolete depth. Scene/Game share point caches and a dedicated Key map. Ordinary directional shadows use four Scene/thumbnail cascades; Game uses one stabilized World projection plus an optional focused player-caster projection. That focused ordinary directional map is **different from the Key silhouette**. Do not replace Game policy with cascades by accident.

Per-projection cache keys include relevant caster geometry/deformation, transforms, alpha coverage/time and projection. Receiver biases and source response need not redraw unchanged depth. Upstream World casters are collected before camera receiver culling. Depth range/receiver fitting, texel snapping and receiver-plane correction are correctness requirements; increasing bias is not a substitute.

`EditorRenderFrame` freezes one presented World with independent Scene/Game camera requests and exact resource-version leases. `RenderPassPlan` declares implemented dependencies/owners and validates ordering; it is not a general frame-graph scheduler or backend barrier compiler. Optional darkness uses Depth Prepass → Ambient Evaluate → Publish before Scene Color. Existing shadow/deformation/environment/light assignment precede consumers.

Workers compile/prepare CPU data only. Main-thread concrete GPU owners publish complete replacements, retain submitted frame leases and account for pending/retired allocations. Replaced shadow atlases remain owned through retirement; high-water capacity/padding can keep memory above active maps. Thumbnail shadows have a separate owner. All GPU owners/readbacks retire before bgfx shutdown; ResourceManager is constructed before GPU owners.

Preserve linear HDR accumulation, the existing GGX/Smith/Schlick/Fresnel-Lambert response and roughness floor .045. Exposure and optional Glow precede the selected Linear/ACES/AgX/legacy Reinhard mapper; the solid editor matte/grid remain exposure independent. Apply the final sRGB encode once, then optional display-space Debanding and ImGui. Glow is a screen-space halo, not scene illumination.

## Persistence, materials and startup

Current native scene writer is **v17** (readers v1–v17); Light Rig settings remain **v4**, Darkness Reveal **v2**, native material file **v6**, graph schema **v7**. Old Character fields convert in memory into Actor, Controller, Motor, Navigation Agent, Player Input, Light Rig and Darkness Reveal. Lens-only Camera and separate Camera Follow capture the former follow target/offset and full world basis. Existing IDs, hierarchy, settings and camera roles remain intact. Only explicit Save writes the new schema; loading does not rewrite authored assets. The optional Environment `ssgi` object retains disabled defaults in older files.

Imported glTF/GLB and Quader native material sidecars are editable under their shared assigned identities. Imported origin is provenance, not a read-only flag. Existing glTF sidecars are not overwritten by promotion; exact-content imports preserve their native graphs. Quader Sync Materials governs source overwrite, independently of its Sync Lights setting. Preserve model slot overrides, factors, texture roles/channels, UV/tangent compatibility and reference-aware deletion policy.

An exact conventional opaque PBR subset uses the embedded model program as its complete implementation; it must pass `materialGraphFixedSurface` certification. `materialGraphLegacySurface` is an adapter and cannot certify arbitrary graphs. Unsupported expressions/coverage use generated complete pass bundles. Parameter values, texture identities/samplers and compatible UV transforms are data changes, not new shader permutations.

Warm generated bundles hydrate through content/compiler/build/dependency-aware cache validation and preflight without shaderc/jobs. True misses prepare asynchronously; the scene is never hidden behind a whole-world Preparing Materials gate. Keep the complete last-good packet, including programs, uniforms, textures, coverage and render state, until its coherent replacement publishes. Initial misses use the supported fallback, not arbitrary texture/opacity inference.

Current interactive misses skip costly surface-fragment FXC optimization for first publication, then prepare O3 after 750 ms of quiet. Background work cancels/shares according to current owner revisions. Optimized and interactive policies have distinct identities. Stage names follow their own source, and independent coverage traversal/slots avoid invalidating unchanged coverage merely because surface code changed. Native catalog scanning already discovers explicit shared-material identities; model completion does not repeatedly rediscover/reserialize them. Named engine shaders whose contents match the embedded baseline use embedded programs immediately. Release uses the paired staged shader/include/compiler family, never newer checkout sources with a potentially incompatible manifest.

`Build.cmd Release` also runs project material-cache preparation, coalescing topology/stage work and writing derived cache only. This pays a changed renderer family's custom compile cost before handoff. A new external project or genuinely new graph can still compile at runtime. Cache-only hydration, shader compilation, total startup and gameplay FPS are different measurements. The canonical implementation/limitations and scoped evidence are in [Material shader startup](MATERIAL_SHADER_STARTUP.md); do not copy historical timing tables here.

## Visual tools and focused diagnosis

**Light Rig → Show Rig** displays active Scene/Game arrows, the Local World source sphere and the separate **50% Reveal** sphere using the same presented pose/camera. Turning it off hides these overlays regardless of selection in Edit and Play. The Reveal sphere can display independently of the rig master when Reveal and Show Rig are enabled. These are passive overlays; free Scene camera navigation remains available. They do not create light entities.

During Play, focus Game and press **F1** to toggle the top-right metrics overlay. It reports FPS/frame time, main-thread CPU work, backend GPU time, backend Device VRAM where available, tracked textures/programs/models/shadows and engine draw count. Missing device/timer values show N/A. These are not OS CPU utilization or a complete per-pass GPU profiler. Input guards preserve text fields, popups and drag ownership.

For paired API measurements, the explicit `renderer_backend_benchmark` utility runs the same isolated draw-heavy and effects-heavy fixtures in fresh `--backend dx11` and `--backend dx12` processes. Each covers Game alone and simultaneous Scene/Game, defaulting to 1920×1080 per view and 120 warmup frames. `--samples 360` requires at least 360 actual distinct GPU queries from a fixed 720-frame CPU window on both APIs, covering three complete motion cycles; 16 untimed frames expose delayed results. The pinned bgfx statistics API retains only the latest completed query, so intermediate GPU frame IDs may be unavailable when several queries complete between reads. The report records actual coverage and gaps, counts no duplicate results and fails if the requested number of unique samples is unavailable. Its report and raw CSVs separate CPU preparation/submission, completed GPU query samples, renderer-owned allocations and DXGI process residency. CPU frame timings include the hidden primary-window Present and queue backpressure with VSync off on both APIs; GPU timestamps cover submitted scene/compute work. The fixture excludes authored scene loading, simulation, editor UI and visible desktop composition latency. See [README reproduction commands](../README.md#build-checks-and-user-acceptance); fixture results require their own measured report and do not establish gameplay FPS or a universal API advantage.

The benchmark disables optional per-view GPU profiling and uses the ordinary primary presentation cadence without an extra native DX11 flush. Whole-frame GPU timestamp elapsed time includes any queue-feeding gaps inside its bracket and is not an active-GPU utilization measurement. The [measured backend comparison](RENDERER_BACKEND_COMPARISON.md) records the local RTX 4090 results and the repaired DX12 query-readiness contract; earlier GPU results from the unpatched timer are superseded.

The **Scene diagnostics → Shadows** section reports maps/projections refreshed this frame and cache hits, not just allocations. Zero updates can be correct with visible cached shadows; wider PCSS still costs sampling. The dedicated Key counters differ from ordinary directional Character projections. Read [SHADOW_DIAGNOSTICS.md](SHADOW_DIAGNOSTICS.md) before interpreting allocation/deferred numbers or drawing memory/performance conclusions.

Common symptom routing:

| Symptom | Check first |
| --- | --- |
| Source Radius has no effect | Cast Shadows/admission, radius above zero, actual caster-receiver separation; specular can be disabled |
| Player invisible near a pillar | Player designation/subtree/category, rig master/Key/Fill, ambient mask; World omni intentionally excludes player |
| Torch washes away player silhouette | Key Cast Shadow/Strength/eligible coverage/World Receive Shadows; silhouette must multiply the complete reflected sum once |
| Room behind a wall becomes visible | Separate real point transport from distance-only player reveal/IBL; do not remove shadows to disguise it |
| Wall flickers at grazing movement | Receiver normal/plane consistency, bias sign, cube seam/tile addressing; test reflected/sloped geometry |
| Gray or missing textures at launch | Certified path/fallback and coherent packet state; cache/source-family/preflight diagnostics; do not erase authored textures |
| A light is visible but shadow count is lower | Distinct 4,096 direct-source versus 16-map admission and four-update budget |

## Focused verification map and future boundary

Documentation-only edits need source/link consistency checks, not an application build. Code changes use `Build.cmd Release` after implementation owners finish, followed by focused CTest selection. Ask to save/close a running renderer only immediately before that executable build. Do not run routine capture/acceptance suites or control the user's PC without permission. User visual acceptance is separate from CPU/headless evidence.

| Changed contract | Focused existing CTest names |
| --- | --- |
| Backend selection, persistence and native lifetime | `app_options_core`, `editor_preferences_core`, `native_resource_core`, `native_resource_dx11_core` |
| Rig values and extraction | `player_light_rig_core`, `scene_gameplay_core`, `scene_extraction_core` |
| Darkness math/environment settings | `ambient_visibility_core`, `environment_core`, `environment_presentation_core`, `procedural_sky_core` |
| Point response and shadow geometry | `render_core`, `point_shadow_math_core`, `point_shadow_render_core` |
| Directional policy/precision/scheduler | `directional_shadow_math_core`, `directional_shadow_receiver_plane_core`, `directional_shadow_quality_core`, `shadow_update_scheduler_core` |
| GPU records and cross-path lighting | `clustered_lighting_core`, `clustered_lighting_gpu_core`, `forward_plus_render_core`, `lighting_dependency_render_core` |
| Frame ownership/pass contracts | `render_pass_plan_core`, `editor_render_frame_core` |
| Material ABI/cache/publication | `shader_preflight_core`, `shader_compiler_core`, `material_graph_shader_core`, `material_graph_publication_core`, `project_material_cache_core` |
| Game overlay | `game_metrics_core`, `game_view_input_core` |

Select tests for the actual change. GPU/compilation fixtures use isolated inputs and their existing serial/time limits; do not turn this table into a mandatory full matrix. Record changed visuals and benchmark scope before/after when performance work is requested. The earlier gameplay performance comparison was explicitly deferred; historical appearance tests are not a substitute for paired FPS/GPU measurements.

The shared focused GPU suites default to DX12 and accept `CRIMSON_TEST_RENDERER=dx11` for the same fixture on DX11. CTest also registers `ssgi_render_dx11_core`, `ssao_render_dx11_core` and `lighting_dependency_render_dx11_core`; native lifetime coverage uses its explicit backend argument. API selection changes neither shader content nor acceptance tolerances. Retain strict coverage for Scene/Game histories, shadows, transparent auxiliary outputs, receiver precision and queued cleanup across both backends.

**Still planned:** occluder fading for foreground walls/pillars, room/line-of-sight visibility, broader actor/item categories, AO, time-of-day/volume blending, dynamic bodies and later scalability slices. Occluder fading must eventually agree across visible coverage, readable depth/darkness, picking, transparency and shadow cache invalidation; there is no current automatic proximity fade. Existing Mask/Translucent materials and Receive Fog do not implement that system. Keep the full D2R-inspired pipeline direction in mind while preserving the current contracts, and implement the next slice only when requested.
