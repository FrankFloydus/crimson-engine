# Material shader startup and cache contract

Updated 2026-10-16. This describes the implemented editor path, the relevant UE 5.8 source findings and the measured cache workload. It is not a full editor startup or gameplay FPS benchmark.

## Presentation and ownership

`MaterialGraphPrograms` first tries a complete persistent bundle through `ShaderCacheReadSession`. The cache key includes generated source, compiler contents, build/recipe contract and discovered dependency contents. All required stages pass structure, reflection, ABI and DXBC preflight before a coherent GPU generation publishes. A valid warm bundle does not invoke shaderc or wait for a resource worker. Unchanged requests reuse the in-memory generation.

A true miss compiles on the existing ResourceManager workers. Scene/Game rendering continues; there is no whole-world preparing-materials gate. Existing models retain their last complete packet, including programs, uniforms, textures, coverage and render state, until a coherent replacement is ready. Failed compilation retains that packet and reports the diagnostic.

`materialGraphFixedSurface` certifies an exact subset for the embedded model program. It accepts opaque standard PBR with conventional color, ORM, normal, AO and emission slots, uniform factors/arithmetic and compatible shared mapping. Texture roles, channels and samplers must match. Normal UV must also match each primitive's tangent UV, except the built-in box's equivalent sets. Certified model graphs use that program as their complete renderer: no generated source is built and no shader jobs are queued. Normal texture streaming keeps its demand, priority and resource leases. These packets are complete rather than graph-pending, and a topology change outside the certified subset selects the ordinary generated-program path.

This is deliberately separate from `materialGraphLegacySurface`, which is compatibility/Inspector transport and can approximate unsupported graph expressions. The certified path rejects independent coverage, animated expressions, textured Power/Add/Lerp, incompatible packed channels, different transforms and other unsupported branches. Those use the domain fallback until their generated program is ready, or retain a previous coherent model packet. Particle and node-preview behavior is not inferred from the fixed model slots. No authored graph, factor, assignment or image is rewritten by presentation.

Scalar/color values, texture identities, sampler settings and UV transforms are parameter data when graph topology stays compatible. They do not require a new shader permutation. Renderer ABI/source changes and actual topology changes can require compilation; persistent cache reuse cannot remove a genuine first compilation of different code.

## Embedded engine programs and build preparation

The executable already contains its engine shaders. `ShaderReloadSession` compares the selected runtime source/include/compiler family against a build-generated SHA256 baseline before requesting any engine jobs. Equal bytes reuse embedded programs even after timestamp-only staging or relocation. No engine shader compilation or duplicate program publication is queued. The file watcher retains its dependency baseline; actual edits use the existing coherent hot-reload path. Debug source edits made before launch also differ from the embedded baseline and therefore still reload. The baseline lives in the executable rather than in a mutable sidecar that could falsely certify different source bytes.

`Build.cmd Release` runs the CPU-only `crimson-material-cache` utility after the executable and paired `shader-tools` payload have been staged. It scans native material assets and the two protected defaults, coalesces equal graph topologies, compiles each missing shared vertex only once, prepares complete five-pass material families and preflights them against the build's embedded vertex interface and current generated manifest. Files are written only below `assets/cache/shaders`; materials, textures, scenes and layout remain authored inputs. The normal build report is `build/Release/material-cache-preparation.json`.

This preparation pays the cost of genuinely changed custom shader code during the explicit engine build, rather than the user's next editor launch. Repeated builds reuse content-addressed artifacts. It does not reuse old code across a changed renderer interface or approximate the character's independent roughness-texture expression. The active character material in the 2026-10-16 fixture requires generated shaders; Quader color-texture materials and the ordinary default qualify for the embedded model program. Opening an external project or creating a new unsupported graph topology can still require first compilation at runtime, with the existing asynchronous/last-good behavior.

The utility is also callable for an explicitly selected project:

```powershell
& './build/Release/bin/crimson-material-cache.exe' --project-root 'C:/path/to/project' --renderer-bin './build/Release/bin' --report './build/material-cache-report.json' --strict
```

An invalid unused material emits a warning while valid graph families continue preparing; `--strict` returns failure if any preparation failed. Structurally valid cached code that fails interface preflight is discarded and rebuilt once. Only artifacts actually read from the derived cache are eligible for removal. Fresh compilation failures report their diagnostic without a retry loop. No partial GPU generation publishes. Focused checks cover duplicate-topology reuse, zero-process repeated preparation, corrupted cached interfaces and authored-file preservation.

## Diagnosing a reported 30-second launch

The 2026-10-16 prechange probes are under `build/verification/ambient-startup-results/`. `startup-before.json` found three active scene topologies, with every active vertex/fragment lookup cached and roughly 150 ms combined main-thread program publication. `engine-before.json` found all 27 engine stages cached, zero compiler invocations and a 238.78 ms worker batch in the former unconditional startup path. Existing cache-write timestamps showed actual compilation over approximately 46 seconds following the preceding renderer-family update. This supports a cold invalidation after changed shader code; it does not establish a persistent warm-cache failure on every unchanged launch. The current fixes remove the redundant engine batch, avoid generated code for certified model graphs and prepare genuinely changed native material families during Release builds. Cache/tool measurements are not total editor-startup or gameplay FPS measurements.

The v15 Release integration prepared all four native/builtin topologies during the build: 20 cold compiler processes, zero failures and 57,358.8 ms. The current catalog contains 19 materials including the two protected defaults; its complete prepared cache has five shared vertex stages and twenty fragment stages. A strict repeat completed in 201.30 ms with 25/25 stage hits and zero compiler processes. A later incremental build reused those same stages in 139.86 ms. `startup-after.json` hydrated all four complete generated bundles in 187.17 ms, with 40 cache lookups, zero worker requests, zero queued jobs and zero compiler invocations; the authored input hashes remained unchanged. The runtime's certified embedded model path avoids even those generated requests for qualifying world/default materials. These values measure different shader-family inputs from the older v14 probe and must not be reported as an isolated warm-cache speedup or total application boot time.

All 16 selected integration checks passed, combining the initial run with the targeted cache-test repair. The initial cache fixture used a nested material path that the existing managed-asset format rejects; the repaired fixture and product scanner follow the direct material catalog. The full generated-material compilation matrix, Noop material preview/streaming resources, source-baseline invalidation, corrupted-cache repair and focused D3D11 lighting/reload tests pass. All 56 staged shader source files match the current executable family; all 59 embedded source/include/compiler content entries match the deployed payload. `results.json` records the final renderer hash, checks, measurements and their scope.

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

Evidence is under `build/verification/`:

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
