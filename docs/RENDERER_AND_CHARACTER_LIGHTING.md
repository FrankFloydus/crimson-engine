# Renderer and Character lighting implementation guide

Updated 2026-10-16. This is a source-oriented companion for agents changing Crimson's current renderer or Character lighting. [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md) remains authoritative for ownership, implementation status and the next agreed milestone. The implementation described here exists in the checkout; the future work at the end does not.

Read the relevant sections before editing, then verify the source rather than treating a historical screenshot or test count as the current contract. [D2R lighting findings](D2R_CHARACTER_LIGHTING_FINDINGS.md) records the reference evidence and discarded iterations. It does not establish Diablo II: Resurrected's unpublished equations or require copying its complete pipeline.

## Feature map and entry points

| Concern | Current owner and entry points |
| --- | --- |
| Authored Character and light data | [Scene.h](../src/scene/Scene.h), [LightingSettings.h](../src/crimson-renderer/LightingSettings.h), [RenderTypes.h](../src/crimson-renderer/RenderTypes.h) |
| Validation, transactions, persistence and conversion | [Scene.cpp](../src/scene/Scene.cpp), [SceneDocument.cpp](../src/scene/SceneDocument.cpp) |
| Character Inspector and shared lighting widgets | [ScenePanels.cpp](../src/tools/ScenePanels.cpp), [LightingControls.cpp](../src/tools/LightingControls.cpp) |
| Pure player rig evaluation | [PlayerLightRig.cpp](../src/runtime/PlayerLightRig.cpp) |
| Presented scene categories, light arrays and reveal | [SceneExtraction.cpp](../src/scene/SceneExtraction.cpp) |
| Prepared world/camera/resource ownership | [EditorRenderFrame.cpp](../src/editor/EditorRenderFrame.cpp), [RenderPassPlan.h](../src/crimson-renderer/RenderPassPlan.h) |
| Submission, HDR targets and shared appearance | [Renderer.cpp](../src/crimson-renderer/Renderer.cpp) |
| Forward+ records and camera assignment | [ClusteredLightingGpu.cpp](../src/crimson-renderer/ClusteredLightingGpu.cpp), [clustered_lighting.sh](../src/crimson-renderer/shaders/clustered_lighting.sh) |
| Point, directional and dedicated Key shadow ownership | [PointShadowRenderer.cpp](../src/crimson-renderer/PointShadowRenderer.cpp), [DirectionalShadowMath.cpp](../src/crimson-renderer/DirectionalShadowMath.cpp) |
| Darkness depth/evaluation/publication | [AmbientVisibilityGpu.cpp](../src/crimson-renderer/AmbientVisibilityGpu.cpp), [ambient_visibility.sh](../src/crimson-renderer/shaders/ambient_visibility.sh) |
| Scene/Game rig visualization | [PlayerLightRigOverlay.cpp](../src/editor/PlayerLightRigOverlay.cpp) |
| Material publication and startup | [MaterialGraphGpu.cpp](../src/crimson-renderer/MaterialGraphGpu.cpp), [material startup contract](MATERIAL_SHADER_STARTUP.md) |
| Diagnostics and exact counter meanings | [SceneDiagnostics.cpp](../src/tools/SceneDiagnostics.cpp), [shadow diagnostics guide](SHADOW_DIAGNOSTICS.md) |

## Character authoring workflow

Create/select a Character and designate it with **Make Player**. Only `SceneProperties::playerCharacter` supplies the active rig and distance reveal. Parenting a visual model beneath that Character gives its subtree Character lighting membership. An unrelated entity with a Character component does not automatically receive that membership.

Keep all rig controls in the **Character Inspector**. There is no separate rig panel or renderer-global copy of these values. `tools/LightingControls` draws controls; `SceneDocument` owns changes. Edit-mode controls affect both Scene and Game. Play uses a separate document, so Stop discards Play edits. Save desired settings in Edit mode.

**Light Rig → Enabled** defaults off. Key, Fill and Local World each retain their own enabled settings beneath it. **Darkness Reveal** is a sibling section and works independently of the rig master. This separation is intentional: revealing existing global light is different from emitting a local light.

The lighting roles are deliberately separate:

| Contribution | Receivers | Casters/occlusion | Purpose |
| --- | --- | --- | --- |
| Character Key | Player subtree only | World geometry does not block it | Stable readable character shading and highlights |
| Character Fill | Player subtree only | No shadow map | Additional character shading; never lights architecture |
| Local World Light | World only | World geometry casts into its six point faces; player excluded | Player-following omni illumination and pillar/doorway shadows |
| Key silhouette | World with Receive Shadows | Only eligible player-subtree models | Strong player silhouette without Key illumination on the environment |
| Darkness Reveal | Eligible ambient receivers | Distance only; no map | Restores existing illumination near the player |

### Character Key and Fill

Both are directional irradiance values, not scene entities or ordinary point slots. **Direction** is the world-space direction in which light travels. It remains fixed when the player turns. Directions normalize during evaluation; zero/invalid directions are rejected. Character lights cannot be blocked by a nearby pillar.

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

The evaluator emits at most one ordinary `PointLight` at `presentedPlayerPosition + (0, Height, 0)`. Height uses world up. Player rotation/parent scale do not rotate this offset, scale Radius or rotate Key/Fill. No authored lateral-offset controls or World Fill modes remain.

| Control | Default | Range and effect |
| --- | --- | --- |
| Enabled | On | Emits the point only while the rig master is on |
| Height (m) | 2 | .01…100,000; moves the real source and shadow origin |
| Color | `(1, .96, .9)` | sRGB channels 0…1 |
| Intensity | `64/9` ≈ 7.111 | 0…1,000,000; HDR radiance multiplier |
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

**Character → Darkness Reveal → Enabled** defaults on; **Spread (m)** defaults 8 and accepts .01…100,000. The center is player presented position plus half capsule height, with that offset bounded at 100,000 m. Spread is the half-visibility distance, independent of Local World Height/Radius and Environment Softness.

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

## Scope masks versus transient categories

`lighting_scope::World=1`, `Character=2`, `All=3`. These are two current bits, not an arbitrary general-purpose layer system.

Models author independent **Advanced Lighting → Direct Lighting**, **Shadow Casting** and **Ambient Darkness** masks. Lights author **Advanced Lighting → Affect**, **Shadow Casters**, **Affect Ambient Darkness**, **Ambient Receivers** and **Ambient Strength**. Removing direct acceptance must not implicitly remove casting or darkness participation. Model **Cast Shadows** and **Receive Shadows** remain separate controls.

Extraction assigns one transient category: the designated player's subtree is Character; every other model is World. Each authored surface mask is intersected with that category. Choosing both checkboxes does not make the player World or turn arbitrary geometry into Character. Key/Fill additionally require Character category/direct acceptance. The dedicated silhouette requires World category/Receive Shadows even on IBL-only receivers with direct acceptance disabled.

Transient categories, synthetic point IDs, evaluated sources, shadow indices and GPU handles are never serialized. Do not classify monsters/items as Character automatically; that broader classification is future work.

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

Fog supports Exponential and Depth response, height density and Light Energy. The material's **Receive Fog** toggle permits black cutaway materials to remain unaffected by fog. It does not change shadow casting, ambient masks or occluder visibility. A visible sky/background, solid editor matte and fog have separate composition contracts; keep them independent.

## Renderer data, passes, shadows and lifetime

Current local lighting is GPU clustered Forward+: up to 4,096 point sources, initial 32-pixel tiles and 24 logarithmic depth slices per camera. Very large viewports grow tile size. Dense clusters retain all influencing lights within source capacity. The all-lights reference path is a correctness comparison, not a second authoring mode. Up to four ordinary directionals remain independent.

The generated shader texture budget is fixed: graph textures 0…11, IBL cubes 12…13, combined BRDF/Forward+ data atlas 14 and shared shadow comparison atlas 15. Keep BRDF filtering/coordinates intact when extending the atlas. Do not add a sampled darkness texture or source-size sampler casually.

Each point record contains six vec4 texels: position/intensity; color/inverse-range-squared; conservative cull sphere; shadow/source-size/roles/reveal; near-radius-squared/specular/range/edge response; attenuation with reserved components. CPU packing and shaders share [point_light_record.sh](../src/crimson-renderer/shaders/point_light_record.sh). Reflected `u_clusterRecordLayoutV5` prevents old-layout programs from publishing. Current generated-material identity is v16 and includes Environment `u_ambientLight` and complete Character/silhouette bindings. Update recipe/manifest/cache identity with a true ABI change; do not accept a mismatched old cache.

Direct capacity is distinct from shadow residency: **16 admitted point maps**, six guarded D32F projections each, and a normal update budget of **4 maps/24 faces** per world frame. The player omni consumes one source and, when eligible, one map. Key/Fill, reveal and dedicated silhouette consume no ordinary point slots. Scene validation includes the enabled player point before committing edits/loads.

Point admission/update selection uses spatial priority, cache state and fairness. Deferred caster-only updates can retain a coherent previous projection; a changed light projection must not sample obsolete depth. Scene/Game share point caches and a dedicated Key map. Ordinary directional shadows use four Scene/thumbnail cascades; Game uses one stabilized World projection plus an optional focused player-caster projection. That focused ordinary directional map is **different from the Key silhouette**. Do not replace Game policy with cascades by accident.

Per-projection cache keys include relevant caster geometry/deformation, transforms, alpha coverage/time and projection. Receiver biases and source response need not redraw unchanged depth. Upstream World casters are collected before camera receiver culling. Depth range/receiver fitting, texel snapping and receiver-plane correction are correctness requirements; increasing bias is not a substitute.

`EditorRenderFrame` freezes one presented World with independent Scene/Game camera requests and exact resource-version leases. `RenderPassPlan` declares implemented dependencies/owners and validates ordering; it is not a general frame-graph scheduler or backend barrier compiler. Optional darkness uses Depth Prepass → Ambient Evaluate → Publish before Scene Color. Existing shadow/deformation/environment/light assignment precede consumers.

Workers compile/prepare CPU data only. Main-thread concrete GPU owners publish complete replacements, retain submitted frame leases and account for pending/retired allocations. Replaced shadow atlases remain owned through retirement; high-water capacity/padding can keep memory above active maps. Thumbnail shadows have a separate owner. All GPU owners/readbacks retire before bgfx shutdown; ResourceManager is constructed before GPU owners.

Preserve linear HDR accumulation, the existing GGX/Smith/Schlick/Fresnel-Lambert response and roughness floor .045. Exposure and optional Glow precede the selected Linear/ACES/AgX/legacy Reinhard mapper; the solid editor matte/grid remain exposure independent. Apply the final sRGB encode once, then optional display-space Debanding and ImGui. Glow is a screen-space halo, not scene illumination.

## Persistence, materials and startup

Current native scene writer is **v11**; Character rig is **v4**, Darkness Reveal **v2**, native material file **v6** with graph schema **v7** (readers **v1–v7**). Grunge stores one generated image with independent dirt specks, stains, scratches and leaks layer contributions; older style settings convert to explicit layer values on load. Scene edits use transactional history and atomic selected-file saves. Point source size writes `sourceRadius`; `softSourceRadius` is a read-only conversion key. Missing attenuation reads as 2. Older rig/World Fill/point-offset data converts at load; only Save writes the new representation. No legacy runtime mode or duplicate controls remain. Never rewrite authored files merely because a renderer/cache version changed.

Imported glTF/GLB and Quader native material sidecars are editable under their shared assigned identities. Imported origin is provenance, not a read-only flag. Existing glTF sidecars are not overwritten by promotion; exact-content imports preserve their native graphs. Quader Sync Materials governs source overwrite, independently of its Sync Lights setting. Preserve model slot overrides, factors, texture roles/channels, UV/tangent compatibility and reference-aware deletion policy.

An exact conventional opaque PBR subset uses the embedded model program as its complete implementation; it must pass `materialGraphFixedSurface` certification. `materialGraphLegacySurface` is an adapter and cannot certify arbitrary graphs. Unsupported expressions/coverage use generated complete pass bundles. Parameter values, texture identities/samplers and compatible UV transforms are data changes, not new shader permutations.

Warm generated bundles hydrate through content/compiler/build/dependency-aware cache validation and preflight without shaderc/jobs. True misses prepare asynchronously; the scene is never hidden behind a whole-world Preparing Materials gate. Keep the complete last-good packet, including programs, uniforms, textures, coverage and render state, until its coherent replacement publishes. Initial misses use the supported fallback, not arbitrary texture/opacity inference.

Current interactive misses skip costly surface-fragment FXC optimization for first publication, then prepare O3 after 750 ms of quiet. Background work cancels/shares according to current owner revisions. Optimized and interactive policies have distinct identities. Stage names follow their own source, and independent coverage traversal/slots avoid invalidating unchanged coverage merely because surface code changed. Native catalog scanning already discovers explicit shared-material identities; model completion does not repeatedly rediscover/reserialize them. Named engine shaders whose contents match the embedded baseline use embedded programs immediately. Release uses the paired staged shader/include/compiler family, never newer checkout sources with a potentially incompatible manifest.

`Build.cmd Release` also runs project material-cache preparation, coalescing topology/stage work and writing derived cache only. This pays a changed renderer family's custom compile cost before handoff. A new external project or genuinely new graph can still compile at runtime. Cache-only hydration, shader compilation, total startup and gameplay FPS are different measurements. The canonical implementation/limitations and scoped evidence are in [Material shader startup](MATERIAL_SHADER_STARTUP.md); do not copy historical timing tables here.

## Visual tools and focused diagnosis

**Light Rig → Show Rig** displays active Scene/Game arrows, the Local World source sphere and the separate **50% Reveal** sphere using the same presented pose/camera. Turning it off hides these overlays regardless of selection in Edit and Play. The Reveal sphere can display independently of the rig master when Reveal and Show Rig are enabled. These are passive overlays; free Scene camera navigation remains available. They do not create light entities.

During Play, focus Game and press **F1** to toggle the top-right metrics overlay. It reports FPS/frame time, main-thread CPU work, backend GPU time, backend Device VRAM where available, tracked textures/programs/models/shadows and engine draw count. Missing device/timer values show N/A. These are not OS CPU utilization or a complete per-pass GPU profiler. Input guards preserve text fields, popups and drag ownership.

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
| Rig values and extraction | `player_light_rig_core`, `scene_gameplay_core`, `scene_extraction_core` |
| Darkness math/environment settings | `ambient_visibility_core`, `environment_core`, `environment_presentation_core`, `procedural_sky_core` |
| Point response and shadow geometry | `render_core`, `point_shadow_math_core`, `point_shadow_render_core` |
| Directional policy/precision/scheduler | `directional_shadow_math_core`, `directional_shadow_receiver_plane_core`, `directional_shadow_quality_core`, `shadow_update_scheduler_core` |
| GPU records and cross-path lighting | `clustered_lighting_core`, `clustered_lighting_gpu_core`, `forward_plus_render_core`, `lighting_dependency_render_core` |
| Frame ownership/pass contracts | `render_pass_plan_core`, `editor_render_frame_core` |
| Material ABI/cache/publication | `shader_preflight_core`, `shader_compiler_core`, `material_graph_shader_core`, `material_graph_publication_core`, `project_material_cache_core` |
| Game overlay | `game_metrics_core`, `game_view_input_core` |

Select tests for the actual change. GPU/compilation fixtures use isolated inputs and their existing serial/time limits; do not turn this table into a mandatory full matrix. Record changed visuals and benchmark scope before/after when performance work is requested. The earlier gameplay performance comparison was explicitly deferred; historical appearance tests are not a substitute for paired FPS/GPU measurements.

**Still planned:** occluder fading for foreground walls/pillars, room/line-of-sight visibility, broader actor/item categories, AO, time-of-day/volume blending, dynamic bodies and later scalability slices. Occluder fading must eventually agree across visible coverage, readable depth/darkness, picking, transparency and shadow cache invalidation; there is no current automatic proximity fade. Existing Mask/Translucent materials and Receive Fog do not implement that system. Keep the full D2R-inspired pipeline direction in mind while preserving the current contracts, and implement the next slice only when requested.
