# Material shader startup and cache contract

Updated 2026-10-20. This describes the implemented editor path, the relevant UE 5.8 source findings and the measured cache workload. It is not a full editor startup or gameplay FPS benchmark.

## Presentation and ownership

`MaterialGraphPrograms` first tries the optimized persistent stages through `ShaderCacheReadSession`. The cache key includes generated source, compiler contents, build/recipe contract, optimization policy and discovered dependency contents. All required stages pass structure, reflection, ABI and DXBC preflight before a coherent GPU generation publishes. A valid warm bundle does not invoke shaderc or wait for a resource worker. Unchanged requests reuse the in-memory generation. A partial hit retains each valid vertex/fragment pair and requests only missing work; cache snapshot validation prevents mixing results from different source epochs.

Direct3D 12 is the default runtime backend; Direct3D 11 remains selectable in application Preferences or through the nonpersistent `--renderer dx11|dx12` launch override. Both backends consume the same Windows Shader Model 5 / DXBC binaries through the pinned bgfx implementation. Engine embedding, generated materials, hot reload and the CPU-only preparation commandlet retain one shared compiler/cache family. The existing `dx11` token in `ShaderCompileConfig::buildContract` identifies that established compilation family; it does not restrict runtime selection to DX11. Changing only the runtime backend does not invalidate the source baseline or trigger shader compilation. New source, topology, compiler or ABI changes still do. DXBC reflection and all binding, output-target and compute-group checks remain enabled for both backends. Vulkan/SPIR-V compilation and validation remain unimplemented for this rich generated-material path. The shared renderer's modeling recipe has its own SPIR-V shader family; that does not establish rich Forward+/PBR Vulkan support.

A true runtime miss compiles expensive Model/Particle surface fragments on the existing ResourceManager workers with an interactive policy that skips FXC optimization; cheap coverage and vertex stages retain full optimization. It then prepares the full O3 surface result in the background after 750 ms of quiet. A new foreground miss cancels obsolete background optimization; sibling owners share pending optimization work. The two policies have distinct cache identities; Release build preparation produces O3 results. Scene/Game rendering continues; there is no whole-world preparing-materials gate. Existing prepared graph entries retain their last complete packet, including programs, uniforms, textures, coverage and render state, until a coherent replacement is ready. Failed compilation retains that packet and reports the diagnostic. The first usable shader is temporary: full optimization replaces it atomically, with no authored-material changes.

Individual stage names derive from their own generated source rather than the complete material topology key. Coverage has an independent postorder traversal and the first uniform/sampler slots. Surface-only graph rewiring therefore cannot rename or move unchanged coverage. Depth source contains only the expressions consumed by that pass: Mask opacity for shadows, Mask or transparent opacity for picking. Identical compiler inputs share an artifact; PointShadow additionally consumes the prior-world varying and emits the optional motion MRT, so its interface differs from DirectionalShadow. The complete material retains a whole-bundle identity for coherent publication. Material and node-preview owners share their cache read session and source-family dependency memo.

`materialGraphFixedSurface` certifies an exact subset for the embedded model program. It accepts opaque standard PBR with conventional color, ORM, normal, AO and emission slots, uniform factors/arithmetic and compatible shared mapping. Texture roles, channels and samplers must match. Normal UV must also match each primitive's tangent UV, except the built-in box's equivalent sets. Certified model graphs use that program as their complete renderer: no generated source is built and no shader jobs are queued. Normal texture streaming keeps its demand, priority and resource leases. These packets are complete rather than graph-pending, and a topology change outside the certified subset selects the ordinary generated-program path.

This is deliberately separate from `materialGraphLegacySurface`, which is compatibility/Inspector transport and can approximate unsupported graph expressions. The certified path rejects independent coverage, animated expressions, textured Power/Add/Lerp, incompatible packed channels, different transforms and other unsupported branches. Those use the domain fallback until their generated program is ready, or retain a previous coherent model packet. Particle and node-preview behavior is not inferred from the fixed model slots. No authored graph, factor, assignment or image is rewritten by presentation.

Scalar/color values, texture identities, sampler settings and UV transforms are parameter data when graph topology stays compatible. They do not require a new shader permutation. Renderer ABI/source changes and actual topology changes can require compilation; persistent cache reuse cannot remove a genuine first compilation of different code.

Grunge is a generated texture source within this same publication contract. ResourceManager workers synthesize one content-keyed canonical 1024×1024 linear RGBA8 image and its data mips; AssetLibrary's TextureStreamer publishes and leases the GPU allocations on the main thread. Runtime material sampling and the node's embedded/explicit flat 2D previews derive from the same pixels; Value previews display raw numerical grayscale, while RGB previews use sRGB display copies. Independent dirt specks, stains, scratches and leaks opacity controls combine those layers with noise in a single image, sampled through one of the shared twelve Texture 2D/Grunge slots. Changing Seed, noise controls or Layers regenerates image data asynchronously while keeping compatible programs and the last complete material/image visible. Tiling and Mapping only change sampling, without rebaking the canonical image. Adding or rewiring a node can still require its first generated shader compilation.

The generated-image key includes generation settings, resolution and generator revision; bump that revision when pixel equations change. Settings live in graph schema 13 within native material v6 (Grunge was introduced in schema8), with graph schemas 1–13 readable and older Grunge style settings converted to explicit layer values on load. The current representation stores Stains Opacity and Leaks Opacity alongside the other independent layers and writes no Style field. Grunge needs no external input bitmap and creates no separate Asset Browser entry or authored image file. Its layered controls are informed by the readable local Adobe `grunge_concrete.sbs` graph; Crimson's generator is independent and does not claim identical pixels. Shader cache preparation and generated-image preparation are separate work: a warm shader cache does not certify that a requested Grunge image is already resident.

## Per-mesh shading interface

Material ABI/generator 32 adds `u_meshShading` and a separate `TEXCOORD5` normal input. The primary vertex/cache layout remains 108 bytes. Ordinary modes bind a zero-copy alias of its original normal; Smooth binds an optional 12-byte-per-vertex stream. Native tessellation control points keep displacement and shading normals separate. The generated domain reconstructs Smooth height normals without changing the selected geometry or material texture bindings. The domain contract includes `shading-stream1-texcoord5`; generated shader/cache identities include `mesh-shading-v1`, preventing older families from being reused.

The embedded and hot-reload program family includes `cs_model_smooth_normals`, which computes animated Smooth normals after deformation. Shader preflight, compiler recipes, atomic reload and source-baseline staging publish it with the same executable family. Mesh shading is per occurrence and does not add a material graph schema or material-file migration.

## Material catalog startup

Model manifests with explicit shared-material IDs do not need background material metadata loads just to rediscover native assets. Native catalog scanning already discovers those assets. Implicit imported material slots and explicit references to a package's own imported sidecars still run migration. A promotion callback records only files actually created; resource publication refreshes the material catalog once when new/discovered sidecars require it. Unchanged model completions do not repeatedly load and serialize every native material. External material notifications still refresh their source.

The inspected project contained 121 model manifests, with 120 explicitly referencing shared native materials. The previous path queued metadata for all of them and rescanned the complete native material library after each completion. This work is independent of shader cache hits and could recur on every launch.

## Current edit and startup measurements

The 2026-10-16 integration is recorded under `build/verification/material-latency-current/`. `snapshot/build/edit-after.json` uses the saved scene and its real four-texture Lit character material, `tripo_mat_e2d9edc9-a284-40c0-a603-a929041cabe3`. It adds a BaseColor multiply expression in memory. Both comparison caches contain the same shared vertices and coverage stages; the two changed surface stages start cold. Both paths use two workers and require complete five-pass Noop GPU publication with preflight.

| Scoped measurement | Result |
| --- | ---: |
| Same edit, full optimization before first publication | 11,157.64 ms |
| Same edit, first complete interactive publication | 2,580.82 ms |
| Final optimized publication, measured from edit start | 14,450.23 ms |
| Parameter-only program request | 0.0005 ms; same programs, zero compiler processes |
| Reopen the edited optimized bundle from disk | 58.35 ms; zero jobs/compiler processes |
| Hydrate all eight cached catalog topologies | 324.30 ms; 80/80 cache lookups, zero jobs/compiler processes |
| Strict project cache preparation repeat | 205.50 ms; 24/24 unique stages cached, zero compiler processes |

The interactive policy reduces this edit's first-publication wait by 76.9%; it does not make genuinely new shader code instantaneous. Its temporary bytecode skips optimization, and background O3 replaces it. Parameter-request timing measures reuse of existing programs, not an entire material-editor frame. The complete catalog includes three topologies not active in the scene. Noop timings cover CPU compilation, cache reads, preflight and publication, not native GPU execution or total editor boot time. The authored snapshot hashes remained unchanged.

`catalog-after.json` measures a separate preserved snapshot of the 121-model, 21-native-material catalog: construction plus background publication took 234.38 ms, with one metadata load, no catalog revision change and no authored writes/created/removed files. Input-preservation hashing warmed the filesystem cache before timing. This isolates catalog work; it excludes the window, renderer, shaders and geometry streaming and has no equivalent prechange elapsed-time baseline.

The final Release executable built successfully. Twenty-six distinct focused CPU/headless checks passed across the integration and targeted repair runs, including generated shader passes, interactive/full cache separation, coherent publication/cancellation, source invalidation, model-slot persistence/deletion, Quader remapping, viewport drop intent and preview readiness. The two test-fixture repairs initialized a legacy-empty default graph and accounted for the importer's appended unused default slot. Native viewport interaction remains user acceptance.

## Embedded engine programs and build preparation

The canonical renderer submodule owns shader sources, compilation commands and
embedded headers. `Crimson::HostShaders` exports the generated interface to
Engine adapters and tests; canonical headers precede the Engine's host-generated
resource directory so stale local shader headers cannot satisfy these includes.
Runtime shader tools stage the same canonical sources and compiler family.

The executable already contains its engine shaders. `ShaderReloadSession` compares the selected runtime source/include/compiler family against a build-generated SHA256 baseline before requesting any engine jobs. Equal bytes reuse embedded programs even after timestamp-only staging or relocation. No engine shader compilation or duplicate program publication is queued. The file watcher retains its dependency baseline; actual edits use the existing coherent hot-reload path. Debug source edits made before launch also differ from the embedded baseline and therefore still reload. The baseline lives in the executable rather than in a mutable sidecar that could falsely certify different source bytes.

`Build.cmd Release` runs the CPU-only `crimson-material-cache` utility after the executable and paired `shader-tools` payload have been staged. It scans native material assets and the two protected defaults, coalesces equal graph topologies, compiles each missing shared vertex only once, prepares complete five-pass material families and preflights them against the build's embedded vertex interface and current generated manifest. Files are written only below `.crimson/cache/shaders`; materials, textures, scenes and layout remain authored inputs. The normal build report is `build/Release/material-cache-preparation.json`.

This preparation pays the cost of genuinely changed custom shader code during the explicit engine build, rather than the user's next editor launch. Repeated builds reuse content-addressed artifacts. It does not reuse old code across a changed renderer interface or approximate the character's independent roughness-texture expression. The active character material in the 2026-10-16 fixture requires generated shaders; Quader color-texture materials and the ordinary default qualify for the embedded model program. Opening an external project or creating a new unsupported graph topology can still require first compilation at runtime, with the existing asynchronous/last-good behavior.

The utility is also callable for an explicitly selected project:

```powershell
& './build/Release/bin/crimson-material-cache.exe' --project-root 'C:/path/to/project' --renderer-bin './build/Release/bin' --report './build/material-cache-report.json' --strict
```

An invalid unused material emits a warning while valid graph families continue preparing; `--strict` returns failure if any preparation failed. Structurally valid cached code that fails interface preflight is discarded and rebuilt once. Only artifacts actually read from the derived cache are eligible for removal. Fresh compilation failures report their diagnostic without a retry loop. No partial GPU generation publishes. Focused checks cover duplicate-topology reuse, zero-process repeated preparation, corrupted cached interfaces and authored-file preservation.

## Diagnosing a reported 30-second launch

This section preserves the earlier v15 integration checkpoint. Its catalog, executable identity and check counts describe that run, not the current v16 stage-sharing/interactive compilation results in [Current edit and startup measurements](#current-edit-and-startup-measurements).

The 2026-10-16 prechange probes are under `build/verification/ambient-startup-results/`. `startup-before.json` found three active scene topologies, with every active vertex/fragment lookup cached and roughly 150 ms combined main-thread program publication. `engine-before.json` found all 27 engine stages cached, zero compiler invocations and a 238.78 ms worker batch in the former unconditional startup path. Existing cache-write timestamps showed actual compilation over approximately 46 seconds following the preceding renderer-family update. This supports a cold invalidation after changed shader code; it does not establish a persistent warm-cache failure on every unchanged launch. The current fixes remove the redundant engine batch, avoid generated code for certified model graphs and prepare genuinely changed native material families during Release builds. Cache/tool measurements are not total editor-startup or gameplay FPS measurements.

The v15 Release integration prepared all four native/builtin topologies during the build: 20 cold compiler processes, zero failures and 57,358.8 ms. That checkpoint's catalog contained 19 materials including the two protected defaults; its complete prepared cache had five shared vertex stages and twenty fragment stages. A strict repeat completed in 201.30 ms with 25/25 stage hits and zero compiler processes. A later incremental build reused those same stages in 139.86 ms. `startup-after.json` hydrated all four complete generated bundles in 187.17 ms, with 40 cache lookups, zero worker requests, zero queued jobs and zero compiler invocations; the authored input hashes remained unchanged. The runtime's certified embedded model path avoids even those generated requests for qualifying world/default materials. These values measure different shader-family inputs from the older v14 probe and must not be reported as an isolated warm-cache speedup or total application boot time.

All 16 selected integration checks passed in that run, combining the initial run with the targeted cache-test repair. The initial cache fixture used a nested material path that the existing managed-asset format rejects; the repaired fixture and product scanner follow the direct material catalog. The full generated-material compilation matrix, Noop material preview/streaming resources, source-baseline invalidation, corrupted-cache repair and focused D3D11 lighting/reload tests passed. All 56 staged shader source files matched that executable family; all 59 embedded source/include/compiler content entries matched the deployed payload. `results.json` records that renderer hash, checks, measurements and their scope.

## UE 5.8 source findings

Reference root: `C:\Program Files\Epic Games\UE_5.8\Engine`. These files were read locally; Unreal was not modified.

| Source | Relevant behavior |
| --- | --- |
| `Source/Runtime/Engine/Private/Materials/MaterialRenderProxy.cpp`, `GetMaterialWithFallback`, around 865 | Resolves a complete material map or walks per-material fallback proxies and requests missing work. |
| `Source/Runtime/Renderer/Private/BasePassRendering.cpp`, around 1922 | Chooses material/fallback for a mesh; it does not hide the whole world while one graph compiles. |
| `Source/Runtime/Engine/Private/Materials/Material.cpp`, around 555 | Ordinary fallback can be the domain default material. This does not preserve arbitrary graph textures. Crimson's certified textured subset is an additional local presentation policy. |
| `Source/Runtime/Engine/Private/Materials/MaterialShared.cpp`, around 3003–3206 | Uses inlined/in-process maps, shares identical pending work, checks available derived data and compiles missing shader work asynchronously. |
| `Source/Runtime/Engine/Private/ShaderCompiler/ShaderCompilerJobCache.cpp`, around 77, 181, 1386–1582 | Per-job derived-data caching is enabled; the editor can skip eager full-map PostLoad work. Compiler-input hashes identify cached results and identical in-flight jobs. Do not assume every editor asset eagerly loads a complete map from DDC. |
| `Source/Runtime/RenderCore/Private/ShaderCore.cpp`, around 4612 | Compiler input identity includes format, target, entry point, environment and preprocessed source. |
| `Source/Runtime/Engine/Private/Materials/MaterialInstance.cpp`, around 4224–4334 | Ordinary scalar/texture parameter changes update render data/uniforms; static permutations follow a separate recache path. |

The useful principles are precise persistent identities, reuse of identical work, asynchronous misses, parameter updates without recompilation and per-material presentation. They do not imply that UE always retains the old graph: some recompilation paths release its old shader maps and use a default proxy. Crimson keeps its own complete last-good packet policy.

## Avoiding redundant validation

`compileShader` owns the exact content identity and cold-compilation before/after content validation. Its ResourceManager wrappers use file-state guards around the loader rather than hashing all inputs again on both sides. Named shader compilation reuses its initial fingerprint instead of immediately repeating it. Warm reads check file states before and after content-key lookup. Main-thread cache sessions share content-hash prefixes across bundles and check that the observed source epoch remains coherent before publication.

Shaderc's dependency output can remove inactive lexical includes. A strict subset does not require another compiler process. A newly discovered macro include does require another validation compilation against the expanded content identity. Tests cover both cases, changed compiler/source/contract, malformed binaries, multi-pass coherence, failure recovery and retirement.

Do not replace content-addressed shader identity with timestamps, ignore the compiler version, reuse a partially valid pass family or run bgfx from a worker. Release must use the shader/include family staged with its executable; a live checkout with a different uniform ABI must never become its fallback source.

## Recorded workload

This is an earlier worker-cache/finite-omni checkpoint, before the v15 build-preparation work and current v16 per-stage compilation policy. Keep its before/after comparison scoped to its identical engine inputs; do not combine its catalog counts or shader timings with newer runs. Evidence is under `build/verification/`:

- `material-startup-before.json`: actual saved scene, 12 native materials plus two builtins, six unique topology keys; four used by the scene.
- `engine-shader-startup-before.json` and `engine-shader-startup-after.json`: the same 27 staged engine shader inputs, identical fingerprints, byte sizes and cache hits, with disposable cache copies.

| Measurement | Before | After |
| --- | ---: | ---: |
| First cached engine resource batch, 27 stages | 660.33 ms | 209.05 ms |
| Repeated cached engine resource batch | 637.07 ms | 217.01 ms |
| Compiler processes for either cached batch | 0 | 0 |
| Shared cache-only engine lookup | 81.01 ms | 79.20 ms |

This is a 68.3% reduction in the measured first worker batch, not a claim about total application startup. It removes repeated hashing of the same 7.3 MB compiler and common dependencies.

Before the change, the four active-scene material topologies already hydrated from cache in 164.15 ms combined, without compiler work. Repeated requests across all six keys took 0.016 ms. A deliberately cold disposable cache took 23.07 seconds and 25 compiler processes: five shared vertex stages plus twenty fragments. Each fragment ran once, so the inactive-include subset defect did not explain this scene's baseline. Later cold timings include the changed lighting shaders/ABI and must not be presented as an isolated cache-optimization comparison.

The probes are modes of `material_graph_publication_tests`: `--profile-project projectRoot rendererBin output.json [--cold]` and `--profile-engine projectRoot rendererBin output.json`. They use the actual staged compiler/source/include pair. Cache copies and cold compilation stay disposable; authored project files are read only. Integration logs and focused correctness results are recorded separately. Gameplay performance measurements remain deferred at the user's request.

The final v13 lighting-family probe is `material-startup-after.json`: the same saved scene still uses four topologies, deliberate cold compilation takes 21.00 seconds/25 processes, and repeated worker cache reads take 489.75 ms/zero compiler processes. Because this includes different lighting code, the cold result is recorded separately from the identical-input engine comparison. One earlier attempt was discarded when the editor-session/layout snapshot changed during measurement; the completed report confirms its protected project files remained unchanged. Release integration, the migration repair and 18 distinct focused checks passed; `build/verification/finite-omni-startup-results/results.json` lists the checks and evidence. All 55 shader files match the staged executable family.

## Shared material lighting interface

The current generated source identity is v34 and material ABI is 33. The mesh vertex layout remains unchanged. Schema11 image atoms add cached image boundaries, point helpers and processed-normal decoding without changing the twelve texture slots or 208 parameter rows. Surface programs retain four lighting color
outputs. Full-scene Glow reads the existing HDR frame; its superseded emission
capture attachment, fifth surface output and `u_glowSource` uniform are removed.
PointShadow vertex/fragment programs retain a
prior-world-position/validity varying through the existing `v_tangent` semantic;
that depth-only semantic does not change surface tangents. `CC_GRAPH_POINT_SHADOW`
selects the matching generated coverage interface and second readable-prepass MRT.
The vertex program consumes `u_ssgiPreviousModel` for rigid draws, while the
instanced variant reads each occurrence's previous transform from its retained
palette. Both the embedded family and generated cache identity advance together;
earlier PointShadow binaries cannot be mixed with this executable. Surface
materials retain twelve texture slots and four lighting MRT outputs, and `ModelVertex`
remains 27 floats/108 bytes. The lighting attachment/layout contracts remain unchanged; optional Height/tessellation values use the graph schema described below.

Generator/ABI v32 adds authored Height and a DX12 tessellation program family while retaining the ordinary vertex/fragment pair for DX11 and CPU preparation. Each eligible mesh variant includes a self-contained SM5/DXBC domain recipe and reflected manifest, linked on DX12 with the embedded patch vertex/hull stages and its fragment stage. Model color, picking, PointShadow/readable-depth and DirectionalShadow variants share the Height expression. Particle and node-preview variants retain ordinary geometry. All required stages participate in exact source/compiler/build/dependency identities and preflight; publication remains a complete last-good generation, including native domain compilation failures. The material cache commandlet prepares the domain stages alongside the ordinary fallback stages.

Height uses existing graph parameter rows and the shared twelve texture slots, plus `u_materialTessellation` and geometry-only picking controls. Enable changes topology, while Amount, Midpoint, Factor, LOD and compatible graph value/texture edits update packet data. Runtime Height participates in dynamic shadow/history invalidation. Graph schema 13 writes Resize, atomic image/channel nodes, optional Height/tessellation and refraction settings within native material v6, accepts graph schemas 1–13, and leaves older graphs disabled without writes on load. The current source generator is v34, with material ABI 33 and the maintained bgfx port 14; earlier generated binaries cannot satisfy the new contract.

Lit/Translucent refraction adds optional `ModelTransmission` and `ParticleTransmission` programs to the existing five-pass material family. All seven publish together; ordinary materials retain five and node previews retain one. Transmission binds graph textures 0–11, immutable opaque-plus-sky HDR color at 12 and detached readable depth at 14; environment slot 13 remains available. Its OVER draw immediately precedes the additive lit surface draw in the existing sorted queue. Transmissive sprite runs are singletons so overlapping particles preserve that adjacency. DX12 mesh transmission shares Model's Height domain semantics; DX11 and sprites retain ordinary geometry.

IOR drives dielectric F0 and bending, while coverage remains independent of transmission. Both fragments share Fresnel/Beer throughput and the surface subtracts the transmitted part of foreground fog already present in the snapshot. Equal dielectric media remove the direct grazing term, split-sum lookup bias and refraction blur. The source is current linear FP32 HDR before transparent draws, Glow, tone mapping and SMAA. FP32 bilinear/trilinear reconstruction uses explicit texel loads, avoiding adapter-dependent floating-point filtering support. Depth rejects candidate samples in front of the glass front surface; authored Thickness never turns the virtual slab exit into an occlusion barrier. Roughness projects its cone footprint at the sampled background depth, with a bounded distance fraction for distant surfaces and sky. There is no ray traversal, backface thickness reconstruction or refraction of another transparent layer.

Authored finite optical values remain unchanged. Fragment evaluation bounds IOR to 1–10000, Thickness to 0–1000000 meters, Attenuation Distance to at least 0.000001 meters, and Beer optical depth to 80 to keep extreme finite inputs stable. These are runtime numerical limits, independent of the usual authoring slider ranges. Missing or failed source preparation uses destination-color transmission multiplication followed by the same additive surface response; it never samples a stale or partially prepared snapshot.

Views using refraction also own an RGBA16F RGB matte-throughput target, initialized from `1 - snapshot.alpha`. Transmission publishes HDR radiance and `coverage * throughput * (1 - filteredSource.alpha)` to two targets with matching OVER coverage. The additive surface writes zero matte radiance; ordinary Alpha/faded draws attenuate the matte through their coverage, and Additive preserves it. Presentation samples this RGB weight at slot 2 and multiplies the editor matte after tone mapping. This retains index-matched empty-background identity and colored attenuation without treating the display matte as HDR lighting, even with partially fogged backgrounds. The material sampler budget and two geometry draws are unchanged; the extra target adds about 15.8 MiB at 1080p per admitted view, bringing snapshot, scratch and matte storage to about 68.5 MiB before optional readable-depth storage.

The None/Circular/Quad UV0 interpolation contract introduced in v27 remains.
Embedded and generated surface, depth, picking and shadow stages reconstruct UV0
before material transforms; normal
mapping for nonlinear UVs derives its frame from final material UV derivatives
after scale/rotation and skips a second normal rotation/sign adjustment. Shared
derivative normalization keeps frame conditioning stable with camera distance.
Its UV reconstruction adds no material samplers or mesh subdivision.

Generator v33 retains explicit None/Circular/Quad UV0 attributes/varyings and the shared Scene/Game four-cascade directional-shadow interface, including `u_directionalShadowSoftness[4]` and `u_directionalShadowDepthRange[16]`. The former player-fade partition uniforms, nearest-layer compositor and private actor Key/Fill, dedicated actor shadows, darkness/reveal and Ambient Darkness interfaces remain removed. Source identity and reflected manifests reject earlier generated programs. Fixed PBR/model and generated model/particle paths share ordinary scene light/shadow evaluation. The six-texel point stride, twelve material texture slots, four lighting MRT outputs, fog uniforms and independent GI/AO atlas publication remain intact. Generic model instance opacity and material transparency remain supported. GI reconstruction consumes separate raw luminance-moment histories and early spatial feedback, without feeding displayed GI into material donor capture. Release regenerates embedded shaders and prepares matching material cache entries through the existing commandlet; generated headers are never edited manually.


Tone-mapping White Point and AgX Contrast are presentation parameters, separate from this material interface. Renderer computes their coefficients on the CPU and binds `u_toneCurve` / `u_toneShoulder` to the presentation shader alongside the existing operator/exposure settings. Parameter edits do not compile materials, rebake environment maps or change generator v33. The present shader's reflected uniform contract and staged binary must change together when this presentation interface changes. Default parameters retain the existing output; authored parameters persist through the shared Environment serializer, while preview environments retain independent settings. Direct/Indirect debug views use the authored presentation curve, so weak isolated GI can be clipped by ACES even with correct diffuse source capture; Albedo and other data views continue to bypass exposure and tone mapping.


## Cached Resize images

[Resize](MATERIAL_RESIZE.md) forms a sampler boundary for a supported static UV0 subtree. Its recipe identity includes upstream authored values and the host adds source fingerprints. Numeric edits change content signatures while compatible generated program source remains stable. CPU workers evaluate and resample a float image; TextureStreamer publishes linear RGBA16F mip chains on the main thread. Warm shader cache and warm image residency remain separate conditions. The current `material-image-v3` key also includes authored extent and `material_procedural::algorithm_revision` (2), invalidating the current Clouds and broad-Blur pixel changes together. The separate `material-node-image-v4` key identifies flat numeric preview presentation: Scalar/Vector2 display raw clamped values, while Color/Color4 use one sRGB conversion. This display policy does not advance the procedural algorithm or material shader identity.
