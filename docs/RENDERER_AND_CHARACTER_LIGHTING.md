# Renderer and Character lighting implementation guide

Updated 2026-10-18. This is a source-oriented companion for agents changing Crimson's current renderer or Character lighting. [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md) remains authoritative for ownership, implementation status and the next agreed milestone. The implementation described here exists in the checkout; deferred proposals at the end require their own requested slice.

Crimson's current game direction is first-person action with an editable eye camera, WASD movement, mouse look, jumping, strafe tilt and smooth stair traversal. DUSK and Quake inform locomotion and camera feel only. Scene and Game use the same perspective-aware directional shadow policy; the former elevated-camera projection and player-occluder fading are removed. Evaluate future appearance changes from the authored first-person view while preserving current rendering and asset contracts.

Read the relevant sections before editing, then verify the source rather than treating a historical screenshot or test count as the current contract. [D2R lighting findings](D2R_CHARACTER_LIGHTING_FINDINGS.md) records the reference evidence and discarded iterations. It does not establish Diablo II: Resurrected's unpublished equations or require copying its complete pipeline.

The Windows renderer defaults to Direct3D 12. **Edit → Preferences → Rendering → Renderer (restart required)** selects Direct3D 12 or Direct3D 11 for the next launch; the tooltip reports the active backend. `--renderer dx11|dx12` overrides that preference for one process without persisting it. The engine-owned `GraphicsBackend` value is selected before bgfx initialization and does not belong to scene or Environment data. The renderer validates the requested backend and checks native blendable RGBA32F support using the matching D3D11/D3D12 device. Both backends retain the same linear HDR lighting, final sRGB encoding, effects and SM5/DXBC shader/cache/preflight contracts. Switching APIs alone adds no rendering effects and does not require material recompilation. Vulkan is not enabled for the Engine rich Forward+/PBR path. The shared renderer separately implements borrowed Vulkan for Quader's modeling/editor path. Historical D3D11 measurements below retain their original scope; they are not DX12 measurements or cross-backend speed claims.

## Actor lighting and removed features

Actors, players and enemies receive ordinary scene point/directional lights and environment illumination. `SceneActor` defines the nearest visual ownership boundary and World/Character classification; it supplies no private lighting. Character Light Rigs, their Key/Fill and Local World sources, dedicated rig silhouettes, Darkness Reveal and Environment Ambient Darkness have been removed. Existing scenes load while discarding those obsolete settings in memory; saving writes only the current settings. Player-occluder fading and its nearest-wall compositor are also removed. Ordinary scene lights, character shadow casting from those lights, authored transparency, generic instance opacity, SSGI and SSAO remain independent. Legacy model fade participation and application fade keys are ignored on read and omitted on Save.

## Feature map and entry points

DX12 uses the same pinned bgfx version with [project-owned patches](../third_party/crimson-renderer/cmake/vcpkg-ports/bgfx/README.md) for valid small-mip footprints, staging-buffer written ranges and timestamp publication after the resolving command list completes. Native platform-window retirement uses a queue marker after `Present`; the focused GPU validation check covers collapse, resize, close/reopen, captured pixels and shutdown.

| Concern | Current owner and entry points |
| --- | --- |
| Backend selection and application preferences | [GraphicsBackend.h](../src/crimson-renderer/GraphicsBackend.h), [EditorPreferences.cpp](../src/editor/EditorPreferences.cpp), [Options.cpp](../src/app/Options.cpp), [App.cpp](../src/App.cpp) |
| Authored Character and light data | [Scene.h](../src/scene/Scene.h), [LightingSettings.h](../src/crimson-renderer/LightingSettings.h), [RenderTypes.h](../src/crimson-renderer/RenderTypes.h) |
| Validation, transactions, persistence and conversion | [Scene.cpp](../src/scene/Scene.cpp), [SceneDocument.cpp](../src/scene/SceneDocument.cpp) |
| Actor Inspector and shared lighting widgets | [ScenePanels.cpp](../src/tools/ScenePanels.cpp), [LightingControls.cpp](../src/tools/LightingControls.cpp) |
| Presented scene categories and light arrays | [SceneExtraction.cpp](../src/scene/SceneExtraction.cpp) |
| Prepared world/camera/resource ownership | [EditorRenderFrame.cpp](../src/editor/EditorRenderFrame.cpp), [RenderPassPlan.h](../src/crimson-renderer/RenderPassPlan.h) |
| Submission, HDR targets and shared appearance | [RendererHost.cpp](../src/crimson-renderer/RendererHost.cpp) and canonical [ForwardPipeline.cpp](../third_party/crimson-renderer/src/forward/ForwardPipeline.cpp) |
| Forward+ records and camera assignment | [ClusteredLightingGpu.cpp](../third_party/crimson-renderer/src/forward/ClusteredLightingGpu.cpp), [clustered_lighting.sh](../third_party/crimson-renderer/src/shaders/clustered_lighting.sh) |
| Point and directional shadow ownership | [PointShadowRenderer.cpp](../third_party/crimson-renderer/src/forward/PointShadowRenderer.cpp), [DirectionalShadowMath.cpp](../third_party/crimson-renderer/src/forward/DirectionalShadowMath.cpp) |
| Dynamic GI settings and Environment persistence | [Ssgi.h](../src/crimson-renderer/Ssgi.h), [Ssao.h](../src/crimson-renderer/Ssao.h), [EnvironmentSerialization.h](../src/scene/EnvironmentSerialization.h), [ScenePanels.cpp](../src/tools/ScenePanels.cpp) |
| Material publication and startup | [MaterialGraphGpu.cpp](../src/crimson-renderer/MaterialGraphGpu.cpp), [material startup contract](MATERIAL_SHADER_STARTUP.md) |
| Diagnostics and exact counter meanings | [SceneDiagnostics.cpp](../src/tools/SceneDiagnostics.cpp), [shadow diagnostics guide](SHADOW_DIAGNOSTICS.md) |

### Shared point response and source-size shadows

Ordinary points expose the same response fields. Their defaults differ: Near Falloff 0, Specular Scale 1, Range Falloff 4, Edge Softness 1, Source Radius 0, Attenuation 2, Radius 20 m, Intensity 35 and Cast Shadows off. Ordinary authored point intensity is bounded to 0…10,000. Preserve these defaults when adding imports or adapters.

For distance `d`, near radius `n`, influence radius `R`, exponent `p`, softness `s` and attenuation `a`, the response is:

```text
t = clamp((d/R - (1-s))/s, 0, 1)
window = 1 - t^p
response = window² * max(d²+n², .01)^(-a/2)
```

The ordinary `p=4, s=1, a=2` branch keeps its original arithmetic exactly. `a=2` is inverse square; 1 is inverse distance; 0 retains only the range window. Negative exponents intentionally grow with distance until the range fade. General exponent evaluation is bounded for finite HDR output. This is Godot-style power decay with Crimson's existing near softening/numerical floor; it is not Godot's exact close-distance clamp.

See [point_light.sh](../third_party/crimson-renderer/src/shaders/point_light.sh). Source Radius does not change that equation or Lambert diffuse orientation. It broadens GGX/Smith specular response using angular emitter size. **Specular Scale 0 does not disable soft shadows.**

[point_shadow.sh](../third_party/crimson-renderer/src/shaders/point_shadow.sh) evaluates the source-zero 3×3 PCF footprint as 16 unique raw-D32 comparisons. Each texel uses its own receiver-plane reference and the equivalent separable bilinear weights. Numerical comparison tolerance is separate from the authored offsets: an IEEE single-precision `gamma(8)` allowance covers projection/reference arithmetic, and absolute-world coordinate roundoff propagates through the receiver-plane perspective-depth derivative. A fixed two-D32-unit tolerance was insufficient on translated walls near the point source. Point-shadow casters apply light view and projection separately to reduce that cancellation; the readable scene prepass retains the color pass's combined projection for depth parity. PCF and PCSS normal offsets scale with `sin(theta)` rather than `1-cos(theta)`, preserving protection near normal incidence.

At mixed contact edges, a coherent 2×2 patch of genuine blocker depths can reconstruct a missed central blocker, extrapolating at most one shadow texel. Its depth plane must differ from the receiver, and it is rejected if it predicts blocking any observed sample that matches the original, unbiased receiver plane. Contact sharpening is measured against the projected plane intersection and returns to the full filter between two and four shadow pixels. This is bounded coverage completion, not mesh adjacency recovery; finite geometry with no contradictory receiver samples remains ambiguous. The fallback uses the closest confirmed blocker/receiver physical separation, restoring full filtering between two and four receiver texels. Brightening either path additionally requires the central depth to match the receiver plane; an empty or mismatched central depth retains the filtered result. Fully lit or blocked footprints skip reconstruction, allowing for the numerical accumulation bound of the 16 weights.

Keep one terminal PCF call in `pointShadowVisibility`: `tryPointShadowVisibilitySoft` reports whether PCSS supplied a result, and all fallback conditions reach that shared call. SM5 inlines the substantial contact filter at each call site; duplicating it in individual PCSS returns increases shader size, temporary registers and compilation time.

The receiver plane itself is not a blocker. Authored depth/normal offsets still apply and can independently separate shadows from zero-thickness walls; the filter does not silently cap those values. For contact-sensitive joined walls, use **Depth Bias 0 m / Normal Bias 1 texel**; zero authored Depth Bias still includes the numerical comparison tolerance. There is no global conversion of authored settings or defaults. Positive source radius uses a bounded deterministic PCSS search/filter: 12 blocker samples, radial blocker-distance reconstruction from perspective face depth, and 16 visibility taps. Penumbra radius is proportional to `sourceRadius * (receiverDistance-blockerDistance)/blockerDistance`. Contact stays sharper than distant shadow edges.

Wide samples remap to their proper cube face rather than crossing an atlas tile into another light. Receiver-plane correction and a receiver-normal-based stable kernel preserve sloped surfaces and cube seams. It uses the existing comparison atlas/raw depth access, with no extra sampler, shadow map or depth-render pass. Source-size/attenuation edits can change shading immediately while all six depth faces remain cached. Positive size increases query work; zero depth draws is not zero shadow cost. Ordinary directional filtering retains its sharpness.

## Directional shadows for first-person cameras

Scene, Game and saved thumbnails share four stable sphere-fit cascades. Each view uses its own camera projection, position, FOV, near plane and aspect ratio; there is no separate fixed-camera Game world map or player-focus map. Practical PSSM splits blend logarithmic and uniform distances with lambda **0.8**, favoring near-camera detail. Extents are quantized, XY centers snap to texels, the final 10% of each split blends into its neighbor and the final 20% of the effective shadow range fades out. Effective range is min(camera far, authored Shadow Distance). Whole-world caster collection precedes camera receiver culling, so relevant offscreen casters remain visible in the maps.

Directional Inspector controls are **Cast Shadows**, **Shadow Distance (m)** (default 100), **Depth Bias (m)** (0.005), **Normal Bias (texels)** (1), and **Shadow Angular Diameter (°)** (default **0.53°**, range **0–2°**). The angle is the full emitter diameter and affects only directional shadow softness. It does not broaden directional specular response or change the light's energy. Missing angular data defaults to 0.53° without a scene-version bump; current v23 Save writes `shadowAngularDiameter`. Existing cast-shadow flags, distances and biases remain authored values.

At zero angle, weighted 5×5 PCF uses nine bilinear comparison samples with a 6×6 texel footprint. Positive angle adds twelve raw blocker-depth probes and thirty-two bilinear comparison samples for a wide penumbra. Subtexel penumbrae and blocker-search misses fall back to weighted PCF to retain thin caster shadows; the transition to the wider filter blends over **2–3 local shadow texels**. These fixed light-space disks avoid per-frame/random rotation because the engine has no temporal antialiasing. Positive-angle paths therefore still pay blocker search in lit/hard regions, and the transition evaluates both filters. These are bounded sample counts, not evidence of a measured frame-time improvement.

The weighted PCF footprint remains the minimum spatial antialiasing filter, including touching surfaces and a zero angular diameter. A sharp physical contact shadow still needs filtered subtexel coverage. Directional filtering never replaces that coverage with a raw central depth comparison or a reconstructed binary plane result; doing so exposed jagged shadow-map texel steps on stair silhouettes. Wider PCSS penumbrae blend from this filtered baseline. Ordinary finite shadow-map resolution remains a limit.

Receiver-plane gradients use world-position derivatives and portable matrix-row extraction. Comparison references account for the bilinear footprint; projection-dependent float tolerance remains separate from authored metre/texel offsets. Normal bias follows the geometric normal with a slope factor, and overlapping cascades share one continuous world-space normal offset. Increasing bias trades acne for detached shadows; it does not increase map resolution.

All cascades reserve the same world-space halo `H = tan(diameter / 2) * authoredShadowDistance`, with the angle converted to radians. PCSS uses `min(H, tan(diameter / 2) * meanBlockerReceiverGap)` as its world-space filter radius. At the defaults, H is about **0.4625 m**. Penumbra growth saturates after one Shadow Distance of light-space separation; farther upstream casters still cast. The cap bounds projection padding/filter support and avoids a different world-space softness limit in each cascade. Long-distance thin casters, sparse blocker sampling, finite map resolution and this deliberate saturation remain quality limits. There is no ray-traced visibility or temporal reconstruction.

The research basis is [Zhang et al.'s practical split scheme](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus), [Microsoft's cascade/stabilization guidance](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps), [Castaño's optimized PCF derivation](https://www.ludicon.com/castano/blog/articles/shadow-mapping-summary-part-1/) and [Fernando's PCSS paper](https://developer.download.nvidia.com/shaderlibrary/docs/shadow_PCSS.pdf). Lambda 0.8, the 12/32 sample budgets and the shared halo are Crimson choices rather than universal prescriptions from those sources. The older [ARPG shadow checkpoint](SHADOW_ARCHITECTURE_CHECKPOINT.md) retains historical fixtures and measurements only.

Point-shadow admission remains separate from direct illumination: sixteen maps, four complete dirty-map updates per frozen world frame, stable light IDs and fairness. Its frozen priority origin is the **visible Game camera eye**, otherwise the **Scene camera eye**. A missing Player mesh must not cause the free Scene camera target to choose Game's shadowed lights. Resource demand, source-to-shadow mapping and update scheduling reuse the same origin.

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


Fog supports Exponential and Depth response, height density and Light Energy. **Sky Affect** (default 1) scales background fog opacity; **Aerial Perspective** (default 0) blends fog color toward the sky along the viewing ray, and **Sun Scatter** (default 0) adds Godot's broad forward scattering toward enabled directional lights. All three range from 0 to 1. Sky Affect controls only the background; Aerial Perspective and Sun Scatter also affect fog on surfaces/particles, while additive materials retain attenuation only. The existing environment sampler supplies aerial perspective without consuming material texture slots, independently of environment illumination Enabled.

Background opacity evaluates distance/height fog at the camera's far-distance proxy, scaled by Sky Affect. Exponential Density approaches zero continuously; Density 0 and Height Density 0 restore the clear sky or exposure-independent matte, and Sky Affect 0 leaves it clear with finite-surface fog still active. The provided Godot checkout's background-opacity policy is independent of density; Crimson deliberately uses density-aware transmission to satisfy these controls. Hidden backgrounds retain premultiplied fog coverage over the unexposed display matte; visible skies mix in HDR. Color/scattering precede exposure and tone mapping. The material's **Receive Fog** toggle permits black cutaway materials to remain unaffected by fog; opaque excluded surfaces still cover the fog background. It does not change shadow casting, ambient masks or occluder visibility. Grid and editor overlays keep their later exposure-independent composition.

## Tone mapping controls and lighting inspection

The shared tone controls sit beside the existing selector in the Environment Inspector's Post-processing section and Material/Particle Editor Environment menus. `EnvironmentSettings::toneMappingParameters` retains all operator values while the selected operator determines which fields are visible. Reinhard White Point defaults 0 (range 0…64), preserving the unlimited legacy curve; a positive value selects extended Reinhard with that exposed HDR input reaching display white. ACES White Point defaults 1 (1…64). AgX White Point defaults 16.29 (2…64) and Contrast defaults 1.25 (1…4). Linear exposes no curve parameters. Exposure remains an independent multiplier before tone mapping, and White Point is measured in that exposed HDR domain. Preview environments keep their own values.

The optional `toneMappingParameters` JSON object writes `reinhardWhite`, `acesWhite`, `agxWhite` and `agxContrast`. Shared scene/legacy-environment serialization validates finite numbers and each range, including inactive operators; missing fields preserve previous defaults without rewriting or dirtying the source. Positive Reinhard White Point values below 0.01 use 0.01 for numerical stability; zero remains the unlimited-curve sentinel. Existing ToneMapping enum values and scene versions remain unchanged. Whole-Environment transactions preserve undo/redo, cancellation, duplication and Edit/Play isolation. Saved-scene thumbnails follow the authored curve.

`ToneMapping.cpp` is the CPU reference for `tone_mapping.sh`. The CPU computes normalization and curve coefficients through `toneMappingUniforms`; Renderer binds `u_toneCurve` and `u_toneShoulder` for every presentation, including alternating Scene/Game views. These uniforms do not enter material shaders or the generated-material ABI. Curve edits require no material compilation or environment preprocessing. Default parameters preserve the existing Reinhard/ACES/AgX output and the single final sRGB encoding.

## Dynamic global illumination

Only ordinary point/directional lights and eligible Lit emission/environment diffuse light supply donors. Private actor light sources and darkness attenuation are absent. **GI Energy** remains a per-scene-light multiplier in [0,16], default 1. It changes diffuse donor energy independently of direct light, specular or shadows.


The Environment entity's collapsed **Global Illumination** section enables SSGI (default off). Strength defaults 1 (0…4), Bounces 1 (integer 1…4), Max Distance 3 m per bounce (.1…20 m), Quality Medium, Advanced Thickness .1 m (.01…2 m) and Temporal Stability .85 (0….98). Low/Medium/High trace 2/4/8 rays with 12/16/24 steps per segment at half resolution. Custom supports Samples Per Pixel 1…16 and Ray Steps 8…64; editing either budget selects Custom while preserving the other resolved preset budget. Advanced exposes Ray Bias .02 m (.001….5 m), Denoise Radius 3 half-resolution pixels (0…4), Denoise Strength 1 (0…1) and Radiance Clamp 10 HDR units (0…100; zero disables it). There is no lighting bake. Open levels do not require ceilings, but offscreen and hidden donors remain unavailable.


The shared serializer writes flat optional `ssgi` and `ssao` objects. Current `ssgi` takes precedence; otherwise, the removed `gi` wrapper converts once in memory from its common and `screenSpace` values. Writers discard removed method/world-volume settings. Missing effects stay off, and missing earlier Bounces stays one. Loading does not rewrite or dirty authored files. Whole-Environment transactions preserve disabled tuning, duplication, undo/redo and Play isolation/reset. Material/Particle previews and browser thumbnails omit both effects.



Geometry A stores validity/reactivity plus seven bits per RGB diffuse-response channel as an exact 23-bit integer in RGBA32F. Additional path segments multiply the decoded, bounded response at intermediate hits, keeping material color and energy loss separate from Strength. Each segment has its own Max Distance bound. Captured radiance remains free of prior GI; changing Bounces extends the path rather than increasing a feedback multiplier.

Tracing samples the full cosine hemisphere over time, refines perspective-correct depth crossings and retains accepted-hit energy throughout Max Distance. Radiance second-moment history clipping preserves sparse indirect samples while rejecting inconsistent history. The first Screen Space frame after enabling/resetting contains zero GI. Camera cuts, projection/size changes, frame gaps, identity/settings/program changes and invalid geometry reject old history. Moving/deformed geometry avoids stale accumulation. Separate Scene/Game owners account for their active, pending and retired resources and release them before bgfx shutdown. Unsupported capabilities/allocation errors keep ordinary lighting and expose a status in the active Environment Inspector without switching authored Enabled off.

For a clear Screen Space visual comparison, place a colored Lit emissive wall beside a rough neutral, nonmetallic floor, keep both visible and within Max Distance, and temporarily remove other illumination. The floor should gain the wall's color with GI enabled. An ordinary light illuminating one surface can similarly supply bounce to a nearby surface facing it. Light to floor to wall is one indirect bounce; continuing from that wall back onto the floor requires Bounces 2 or higher. Strength scales the result rather than extending propagation. Every added segment remains limited to visible geometry, so higher bounce counts cannot restore offscreen transport. Glow is a separate screen-space effect and does not illuminate nearby geometry.


A focused point-only saved-project reproduction disabled model emission, sky/ambient, directionals and private actor lights while preserving scene geometry, ordinary point lights and authored SSGI settings. GI brightened 14,800 Scene pixels and 17,877 Game pixels in Lit at 909×684, while Direct Lighting captures were identical with GI on/off and the no-point control was exactly black. Isolating the weak reflected signal under the saved ACES curve left only 32 detected Scene pixels; presenting the same camera/light/settings with Linear revealed 89,892. The coffin materials were opaque Lit nonmetals with full per-view opacity. This establishes working point-light transport and a display-curve limitation; it does not establish room-wide fill or remove screen-space visibility limits.

The isolated `ssgi_render_core` GPU fixture covers that ceiling-free comparison, changing/removing emission, moving geometry, discarded cutouts, partially faded models, generated/fallback materials, scope separation, transparent auxiliary preservation, independent cameras and disabled/zero-strength output. Its point-only case places a finite-range point above the floor so it directly illuminates a wall while excluding the floor: the floor is black in Direct Lighting and receives colored bounce in Indirect Lighting. It checks generated/fallback donors, light removal, World/Character isolation and an actual resident point-shadow path. A second fixture requires an emitter-to-wall-to-coplanar-floor path: one bounce leaves the floor black, while two through four transport light through the wall. It checks Unlit, metallic and scope-blocked intermediate surfaces, generated material response, emission removal and unchanged bounce-count allocations. Its executable accepts `--artifacts <directory>` to export off/on, point-only and 1/2/4-bounce PNGs, and `--benchmark --warmup 120 --samples 360` for a bounded comparison; its three required path arguments are supplied by the CTest registration.

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

The generated shader texture budget is fixed: graph textures 0…11, IBL cubes 12…13, combined BRDF/Forward+ data atlas 14 and shared shadow comparison atlas 15. Keep BRDF filtering/coordinates intact when extending the atlas. Keep new effects within the existing sampler budget.


Per-projection cache keys include relevant caster geometry/deformation, transforms, alpha coverage/time and projection. Receiver depth bias and point-source response need not redraw unchanged depth; directional angular size or normal bias can alter projection padding and invalidate that projection. Upstream World casters are collected before camera receiver culling. Depth-range/cascade fitting, texel snapping and receiver-plane correction are correctness requirements; increasing bias is not a substitute.

`EditorRenderFrame` freezes one presented World with independent Scene/Game camera requests and exact resource-version leases. `RenderPassPlan` declares implemented dependencies/owners and validates ordering; it is not a general frame-graph scheduler or backend barrier compiler. Optional GI/AO use detached readable depth and compute publication before Scene Color. The removed player-fade layer contributes no target, packed-depth tail or extra composition pass. Existing shadow/deformation/environment/light assignment precede consumers.

Workers compile/prepare CPU data only. Main-thread concrete GPU owners publish complete replacements, retain submitted frame leases and account for pending/retired allocations. Replaced shadow atlases remain owned through retirement; high-water capacity/padding can keep memory above active maps. Thumbnail shadows have a separate owner. All GPU owners/readbacks retire before bgfx shutdown; ResourceManager is constructed before GPU owners.

Preserve linear HDR accumulation, the existing GGX/Smith/Schlick/Fresnel-Lambert response and roughness floor .045. Exposure and optional Glow precede the selected Linear/ACES/AgX/legacy Reinhard mapper; the solid editor matte/grid remain exposure independent. Apply the final sRGB encode once, then optional display-space Debanding and ImGui. Glow is a screen-space halo, not scene illumination.

Smooth shading normals may point away from the eye on a visible triangle. Direct lighting keeps positive `N·L` contributions and clamps signed `N·V` for specular evaluation, avoiding a black cutoff as the camera crosses the shading normal's horizon. Environment diffuse and SSGI receiver weights use the integrated specular response from the existing BRDF LUT for their energy split; they do not use raw view Fresnel to extinguish rough diffuse light. Keep these equations shared across embedded and generated surfaces, including the preview sphere. The LUT data and preprocessing equations are unchanged.

## Persistence, materials and startup

Quader UV interpolation is mesh data: None=0, Circular=1 and Quad=2. Circular mode
preserves radial/angular texture flow; Quad mode preserves ordinary inverse-bilinear
mapping. Coordinates, correction and the explicit mode survive managed packages
and model cache revision 8. Embedded and generated mesh programs reconstruct UV0
before the material UV transform. Nonlinear tangent-space normal mapping derives its
frame from the final material UV after scale/rotation, with no second normal
rotation/sign adjustment. Normalized derivatives keep frame conditioning stable
with camera distance; the bitangent follows `B = cross(N, T) * sign`. Color,
alpha coverage, depth, picking and shadow passes use the same coordinates;
ordinary UV, UV1 and triplanar mappings keep their existing behavior.
Material generation v27 carries this vertex/varying interface with no additional
samplers, uniforms or mesh subdivision. Nonlinear primitives retain their original
LOD geometry until a chart-aware simplifier is available.

Imported glTF/GLB and Quader native material sidecars are editable under their shared assigned identities. Imported origin is provenance, not a read-only flag. Existing glTF sidecars are not overwritten by promotion; exact-content imports preserve their native graphs. Quader Sync Materials governs source overwrite, independently of its Sync Lights setting. Preserve model slot overrides, factors, texture roles/channels, UV/tangent compatibility and reference-aware deletion policy.

An exact conventional opaque PBR subset uses the embedded model program as its complete implementation; it must pass `materialGraphFixedSurface` certification. `materialGraphLegacySurface` is an adapter and cannot certify arbitrary graphs. Unsupported expressions/coverage use generated complete pass bundles. Parameter values, texture identities/samplers and compatible UV transforms are data changes, not new shader permutations.

Warm generated bundles hydrate through content/compiler/build/dependency-aware cache validation and preflight without shaderc/jobs. True misses prepare asynchronously; the scene is never hidden behind a whole-world Preparing Materials gate. Keep the complete last-good packet, including programs, uniforms, textures, coverage and render state, until its coherent replacement publishes. Initial misses use the supported fallback, not arbitrary texture/opacity inference.

Current interactive misses skip costly surface-fragment FXC optimization for first publication, then prepare O3 after 750 ms of quiet. Background work cancels/shares according to current owner revisions. Optimized and interactive policies have distinct identities. Stage names follow their own source, and independent coverage traversal/slots avoid invalidating unchanged coverage merely because surface code changed. Native catalog scanning already discovers explicit shared-material identities; model completion does not repeatedly rediscover/reserialize them. Named engine shaders whose contents match the embedded baseline use embedded programs immediately. Release uses the paired staged shader/include/compiler family, never newer checkout sources with a potentially incompatible manifest.

`Build.cmd Release` also runs project material-cache preparation, coalescing topology/stage work and writing derived cache only. This pays a changed renderer family's custom compile cost before handoff. A new external project or genuinely new graph can still compile at runtime. Cache-only hydration, shader compilation, total startup and gameplay FPS are different measurements. The canonical implementation/limitations and scoped evidence are in [Material shader startup](MATERIAL_SHADER_STARTUP.md); do not copy historical timing tables here.

## Visual tools and focused diagnosis


During Play, focus Game and press **F1** to toggle the top-right metrics overlay. It reports FPS/frame time, main-thread CPU work, backend GPU time, backend Device VRAM where available, tracked textures/programs/models/shadows and engine draw count. Missing device/timer values show N/A. These are not OS CPU utilization or a complete per-pass GPU profiler. Input guards preserve text fields, popups and drag ownership.

For paired API measurements, the explicit `renderer_backend_benchmark` utility runs the same isolated draw-heavy and effects-heavy fixtures in fresh `--backend dx11` and `--backend dx12` processes. Each covers Game alone and simultaneous Scene/Game, defaulting to 1920×1080 per view and 120 warmup frames. `--samples 360` requires at least 360 actual distinct GPU queries from a fixed 720-frame CPU window on both APIs, covering three complete motion cycles; 16 untimed frames expose delayed results. The pinned bgfx statistics API retains only the latest completed query, so intermediate GPU frame IDs may be unavailable when several queries complete between reads. The report records actual coverage and gaps, counts no duplicate results and fails if the requested number of unique samples is unavailable. Its report and raw CSVs separate CPU preparation/submission, completed GPU query samples, renderer-owned allocations and DXGI process residency. CPU frame timings include the hidden primary-window Present and queue backpressure with VSync off on both APIs; GPU timestamps cover submitted scene/compute work. The fixture excludes authored scene loading, simulation, editor UI and visible desktop composition latency. See [README reproduction commands](../README.md#build-checks-and-user-acceptance); fixture results require their own measured report and do not establish gameplay FPS or a universal API advantage.

The benchmark disables optional per-view GPU profiling and uses the ordinary primary presentation cadence without an extra native DX11 flush. Whole-frame GPU timestamp elapsed time includes any queue-feeding gaps inside its bracket and is not an active-GPU utilization measurement. The [measured backend comparison](RENDERER_BACKEND_COMPARISON.md) records the local RTX 4090 results and the repaired DX12 query-readiness contract; earlier GPU results from the unpatched timer are superseded.

The **Scene diagnostics → Shadows** section reports maps/projections refreshed this frame and cache hits, not just allocations. Zero updates can be correct with visible cached shadows; wider PCSS still costs sampling. The dedicated Key counters differ from ordinary directional Character projections. Read [SHADOW_DIAGNOSTICS.md](SHADOW_DIAGNOSTICS.md) before interpreting allocation/deferred numbers or drawing memory/performance conclusions.

Common symptom routing:

| Symptom | Check first |
| --- | --- |
| Source Radius has no effect | Cast Shadows/admission, radius above zero, actual caster-receiver separation; specular can be disabled |
| Wall flickers at grazing movement | Receiver normal/plane consistency, bias sign, cube seam/tile addressing; test reflected/sloped geometry |
| Gray or missing textures at launch | Certified path/fallback and coherent packet state; cache/source-family/preflight diagnostics; do not erase authored textures |
| A light is visible but shadow count is lower | Distinct 4,096 direct-source versus 16-map admission and four-update budget |

## Focused verification map and future boundary

Documentation-only edits need source/link consistency checks, not an application build. Code changes use `Build.cmd Release` after implementation owners finish, followed by focused CTest selection. Ask to save/close a running renderer only immediately before that executable build. Do not run routine capture/acceptance suites or control the user's PC without permission. User visual acceptance is separate from CPU/headless evidence.

| Changed contract | Focused existing CTest names |
| --- | --- |
| Backend selection, persistence and native lifetime | `app_options_core`, `editor_preferences_core`, `native_resource_core`, `native_resource_dx11_core` |
| Point response and shadow geometry | `render_core`, `point_shadow_math_core`, `point_shadow_render_core` |
| Directional policy/precision/scheduler | `directional_shadow_math_core`, `directional_shadow_receiver_plane_core`, `directional_shadow_quality_core`, `shadow_update_scheduler_core` |
| GPU records and cross-path lighting | `clustered_lighting_core`, `clustered_lighting_gpu_core`, `forward_plus_render_core`, `material_mapping_render_core` |
| Frame ownership/pass contracts | `render_pass_plan_core`, `editor_render_frame_core` |
| Material ABI/cache/publication | `shader_preflight_core`, `shader_compiler_core`, `material_graph_shader_core`, `material_graph_publication_core`, `project_material_cache_core` |
| Game overlay | `game_metrics_core`, `game_view_input_core` |

Select tests for the actual change. GPU/compilation fixtures use isolated inputs and their existing serial/time limits; do not turn this table into a mandatory full matrix. Record changed visuals and benchmark scope before/after when performance work is requested. The earlier gameplay performance comparison was explicitly deferred; historical appearance tests are not a substitute for paired FPS/GPU measurements.

The shared focused GPU suites default to DX12 and accept `CRIMSON_TEST_RENDERER=dx11` for the same fixture on DX11. CTest also registers `ssgi_render_dx11_core`, `ssao_render_dx11_core`, `material_mapping_render_dx11_core` and the focused shadow counterparts; native lifetime coverage uses its explicit backend argument. API selection changes neither shader content nor acceptance tolerances. Retain strict coverage for Scene/Game histories, shadows, transparent auxiliary outputs, receiver precision and queued cleanup across both backends.

Environment SSAO retains its authored controls and coverage/lighting contract. Player-occluder fading is removed; preserve ordinary material transparency and generic instance opacity. Room/line-of-sight visibility, broader actor/item categories, time-of-day/volume blending, dynamic bodies and later scalability slices remain deferred proposals. Reassess their relevance to the first-person game when a requested slice needs them. The historical D2R investigation is reference evidence, not authorization to implement its full pipeline.
