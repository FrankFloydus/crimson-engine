# Large-scene performance implementation guide

**Status: clustered Forward+ lighting implemented; user acceptance pending. Other scalability stages remain planned. Last audited: 2026-10-15.**

The [Forward+ checkpoint](FORWARD_PLUS_CHECKPOINT.md) records the completed migration's correctness checks, measured costs and gains, frozen-room replay, raw artifact locations and reproduction commands.

The user initially deferred further scalability implementation after the room regression fixes, then explicitly requested clustered Forward+ as the first renderer migration. Stage C below now records that implementation and its limits. This document preserves the earlier investigation and specifies the remaining performance work when it is requested. It does not declare the other planned systems implemented, change the architecture's **Next** milestone, or authorize background implementation.

Read [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md) and the repository's [AGENTS.md](../AGENTS.md) first. The architecture remains authoritative for ownership and current behavior. The named new types below are proposed interfaces; search before assuming they exist. Paths and function names are the durable navigation aids; source line numbers and external checkout versions can change.

Navigation: [recorded evidence](#2-recorded-evidence-and-its-limits), [source research](#3-source-research-what-to-reuse), [measurement procedure](#5-required-beforeafter-measurement-procedure), [implementation sequence](#6-implementation-sequence-and-stop-conditions), [spatial index](#7-incremental-extraction-and-a-persistent-spatial-index), [light selection](#8-separate-authored-lights-from-draw-and-shadow-budgets), [bounded shadows](#9-bounded-shadow-storage-and-a-frozen-frame-plan), [caster caching](#10-persistent-caster-packets-and-exact-invalidation), [static/moving shadows](#11-static-and-moving-shadow-separation), [budgets](#12-hardware-aware-budgets-and-scalable-resource-bookkeeping), [compressed textures](#13-gpu-block-compressed-mip-caches), [world streaming](#14-world-cells-quader-identity-physics-and-navigation), [delivery checks](#15-verification-and-delivery-checklist-for-the-implementation-agent).

The renderer frame architecture now separates immutable presented-world inputs, camera-local prepared receivers, resource leases and validated pass execution. This establishes the boundaries for the D2R-inspired pipeline without changing shadow projection, adding effects or completing the scaling stages below. See [the frame architecture checkpoint](FRAME_ARCHITECTURE_CHECKPOINT.md) for current matched measurements and ownership checks.

## 1. Decisions and facts to carry forward

- The target is a large ARPG, including moving characters, many assets, local lights, level traversal and an editor that can display Scene and Game together.
- Measure **before and after** every performance slice on the same frozen workload. The user explicitly requested this. Preserve raw results, source/build identity, fixture identity and quality settings.
- A small room running quickly is a regression check, not evidence of large-world readiness. The original report of approximately 2 FPS was not reproduced as a 500 ms steady frame in the isolated rendering harness.
- The current 512 MiB GPU value is a **soft streamable-asset retention target**, not total GPU memory, an allocation ceiling or the amount of VRAM the engine is allowed to use. It is still a fixed default.
- Shadows, environment lighting and post processing have separate explicit owners. Subtracting their fixed allocations from the asset cache caused texture starvation. Keep that repair; future budgeting must coordinate the owners instead of asking texture eviction to reclaim a shadow atlas.
- The immediate CPU preparation improvements are implemented. Spatial world indexing, bounded demand-driven shadow storage, static/moving shadow separation, hardware-aware budgets, GPU block compression and world-cell streaming are not.
- GPU clustered Forward+ local lighting is implemented: 4,096 source point lights, initially 32-pixel tiles and 24 logarithmic depth slices per camera. Tile size doubles for very large viewports to keep at most 262,144 cells. The directional-light limit remains four and the independently selected point-shadow budget remains sixteen. This is a renderer/light-capacity slice, not completion of the other world-scaling stages.
- Preserve the six perspective faces used for point shadows initially. Godot's dual-paraboloid representation is a separate projection/filtering change, not a prerequisite for adopting its allocation policy.
- Preserve the shared shadow texture binding. Generated materials use texture stages 0–11, IBL cubes 12–13, a combined BRDF/Forward+ atlas at 14 and shadows 15. A second material-visible shadow sampler cannot simply be added. The data atlas preserves all twelve material texture bindings and the original BRDF lookup response.
- Preserve saved assets and authored identities. Visibility, residency and simulation activation must not delete scene entities or rewrite the Quader source.

### Work already completed

| Implemented behavior | Entry points | Contract to preserve |
|---|---|---|
| One full resource preparation per relevant shadow caster | `EditorResources::prepareShadows`, `point_shadow::casterTexelWorld` | Aggregate the finest required footprint across lights, transformed primitive bounds and both cameras' directional cascades before preparing. An enclosing light or unknown bounds must remain conservative. |
| Cheap prepared-material signatures | `src/editor/PreparedMaterialSignature.h`, `AssetLibrary::preparedGraphBindings` | Hash prepared programs/uniforms/sampler/source versions and explicit draw state; do not serialize authored graph JSON once per instance/view/light. |
| Immutable material compatibility and UV metadata caches | `AssetLibrary::materialCompatible`, `graphCache`, `requiredGraphUvs` | Weak source-version identity; validate replacement versions. Never cache away texture readiness, source coherence or GPU lifetime checks. |
| Correct runtime graph replacement | `MaterialGraphShader::runtimeDefinitionSignature`, `materialGraphValueHash` | Time period/pause and Panner edits can have identical time-zero uniforms and shader topology. Their authored runtime identity must still replace the packet. Disconnected node previews count; layout edits do not. |
| Material-preview recovery from texture pressure | `TextureStreamer::request`, `MaterialPreview` | Selected requests clear inherited pressure. Coherent coarse resources can draw while detail refines; invalid replacements retain a working packet. Reset View remains independent camera behavior. |
| Display-sized retained thumbnails | `ThumbnailCache`, `ThumbnailDemand`, `AssetPreviews`, `TextureStreamer` | Default 128-pixel RGBA cards cost 64 KiB rather than 1 MiB at 512 pixels. Completed rendered images and raw texture thumbnails have bounded LRU retention; disk PNGs survive GPU eviction. Count pending/readback/retired allocations too. |
| Idempotent shared derived-cache publication | `src/resources/CacheIO.h`, `StreamingModel`, `ModelMetadataCache`, `TextureMipCache`, `ShaderCompiler` | Reuse byte-identical files, accept an identical concurrent winner, repair damaged contents, close readers promptly and preserve real errors. Mip-cache publication must use this helper too: replacing an identical file during a Windows mip read can fail with error 32 and leave the shared descriptor in Failed state. Authored-file atomic replacement remains a separate operation. |
| Play metrics | `FrameMetrics`, `GameView`, `App::run` | F1 in focused Game; main-thread work, whole-editor frame/GPU time, driver memory and explicit owner bytes have different meanings. |
| GPU clustered Forward+ local lighting | `ClusteredLightingGpu`, `cs_cluster_lights.sc`, `clustered_lighting.sh`, `ClusteredLighting`, `ShadowLightSelection` | Camera-local GPU sphere/cell bitsets preserve every influencing source light within capacity, including transparent paths; 4,096 authored/imported points and sixteen independently admitted point shadows use different mappings. The CPU builder and all-lights rendering path are correctness references. |

## 2. Recorded evidence and its limits

The original room investigation was performed on the working tree based on Git HEAD `86b3fc7c64be4ab6ad1426fa83fe29febe7b9008`. There were substantial uncommitted feature/fix changes. **That commit alone does not reproduce the measured implementation.** The room measurements below predate the clustered Forward+ migration and must not be presented as its after measurements. A future benchmark must record its own full source state and binary hashes before editing.

### Matched original-room rendering replay

The valid comparison used a frozen managed Crimson scene, with `world_origin=saved_main`, not a new import of the subsequently edited Quader project:

| Fixture property | Recorded value |
|---|---|
| Saved scene | `assets/scenes/9d9791ec48beb7022d3a7e925edbb4f1.crimson-scene` in the frozen copy |
| Scene SHA-256 | `B8A447E51FF73338B240CABEAF70A20096883D550C925FF3D619E6DDFB3EAE63` |
| Combined models / point lights / directional lights | 86 / 14 / 1 |
| Static visible models | Scene 32; Game 49 |
| Prepared shadow casters | 86 |
| Viewports | Two 909 × 684 render views |
| Maximum/current shadow face resolution | 1024 interior pixels |
| Adapter | Logged vendor 4318 (`0x10DE`), device 9860 (`0x2684`); separately identified as NVIDIA RTX 4090 |
| Measured samples per phase | 30 |
| Static submitted draws | 101 |
| Static shadow work | 13 point-light cache hits, 2 directional camera cache hits; zero shadow writes |
| Moving shadow work | 36 point faces, 8 cascades, 888 shadow draws |

| Mean CPU/wall measurement | Before material-cache optimization | Final verified build |
|---|---:|---:|
| Static total replay frame | 18.55888 ms | 6.38865 ms |
| Moving total replay frame | 19.98330 ms | 7.49510 ms |
| Static shadow preparation | 9.00853 ms | 2.26957 ms |
| Static visible-material preparation | 7.39459 ms | 1.91346 ms |
| Moving shadow preparation | 9.02057 ms | 2.14563 ms |
| Moving visible-material preparation | 7.56968 ms | 1.80110 ms |

Final wall p95 was 8.2749 ms static and 8.5987 ms moving. Combined preparation fell approximately 74.5% static and 76.2% moving. These measurements establish the material-preparation problem; they are not a promised desktop-editor FPS.

Allocations were identical in the matched runs:

| Owner | Bytes | MiB |
|---|---:|---:|
| Material textures and graph programs | 41,056,470 | 39.15 |
| Model buffers | 2,137,830 | 2.04 |
| Shared point/directional shadow allocation | 507,256,320 | 483.76 |

The shadow allocation, not this room's texture working set, is the obvious memory-scaling problem. GPU time varied despite matching submitted work: approximately 2.630 → 1.291 ms static and 5.921 → 2.114 ms moving. GPU clocks/load were not controlled sufficiently to attribute those differences to the CPU cache changes. Do not claim a GPU algorithm speedup from them.

An earlier 52,164,822-byte texture/program observation used a different workload with 46 Scene-visible models, 67 Game-visible models and 150 draws. It is not the baseline for the table above. The live Quader room changed during the investigation; its later 23-mesh/5-point-light export is also not interchangeable with this frozen 86-model world.

### Local diagnostic artifacts

The following are **ignored scratch artifacts under `build/`**, not permanent test infrastructure. The facts above remain useful if the artifacts have been removed. Do not make production or CI depend on their availability.

```text
build/diagnostics/quader-performance/
  room_gpu.cpp
  room_cpu.cpp
  compile_gpu.cmd
  gpu_compile.rsp
  engine-original86/
  frozen-native/q-test/q-test.qproj
  room_gpu_original86_before_cache.log
  room_gpu_original86_final.log
  room_gpu_original86_cold_final.log
```

The frozen native metadata was used for reader testing too; its manifest has a modified minimum-product-release value. Treat it as a fixture, not an untouched copy of the user's Quader source. In `--saved` mode the managed saved scene supplies the measured world. Native mesh/light counts reported alongside it describe a different input.

The GPU harness exercises production `AssetLibrary`, `EditorResources` and `Renderer`, with a hidden D3D11 window. It uses a tiny synthetic diffuse IBL, disables the grid and VSync, and moves the player's model subtree plus follow camera by a deterministic per-frame offset. It excludes editor widgets, browser work, the live Quader watcher, the normal event loop, actual Play physics/AI and a representative animated crowd. It records means and wall p95, not a durable raw series or p50/p99. It does not preserve GPU query frame IDs in its output, so its CPU/GPU samples must not be treated as synchronized per-frame pairs.

The cold run passed all four preparation phases without copying derived mesh/texture/shader caches. `--cold` does **not** flush OS/driver caches. Its 1974/604/30/30 warm-up frame counts are not import durations: pending iterations included sleeps and asynchronous work. Measure wall duration explicitly in future runs.

Separate CPU probing found approximately 0.29 ms steady Play CPU work in that small fixture and a roughly 337 ms one-time navigation bake. These are triage observations, not a crowd or large-navigation benchmark.

### Measurement mistakes already discovered

1. The first moving-player probe moved the character but not its visual subtree. That result is invalid for moving shadow casters.
2. Hidden offscreen D3D11 views do not necessarily call `Present`. Unflushed command/query work produced misleading long GPU timestamps. The repaired probe explicitly flushed the borrowed immediate context after submitting the frame; this was diagnostic-only, with no production wait/flush change. Real-editor runs must retain their real presentation path.
3. `BGFX_DEBUG_PROFILER` enables per-view timings. Whole-frame D3D11 GPU timing is available without it; `gpuFrameNum == 0` is an uninitialized sample and must be rejected.
4. `--legacy` in the scratch harness does not restore an older algorithm. It only changes diagnostic behavior. Old runs used separately retained objects/executables; those are not a reproducible source baseline.
5. Linking a scratch harness against new libraries with an old object after a header layout change is unsafe. Recompile the harness and every affected object.
6. `VsDevCmd.bat` can change `VCPKG_ROOT`. A bare CMake command once selected the wrong toolchain. Use `Build.cmd`, which restores the chosen root; use preset environment when adding any focused helper. Do not alter the pinned dependency baseline to fix that shell mistake.

## 3. Source research: what to reuse

External source roots inspected, read-only:

```text
Godot: C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/godot-master
       version.py: 4.7.0 beta
Unreal: C:/Program Files/Epic Games/UE_5.8/Engine
        Build/Build.version: 5.8.0, changelist 55116800
Quader: C:/Users/Drako/Desktop/quader-windows
```

### Godot

| Question | Source to inspect | Finding and implication |
|---|---|---|
| How are positional shadow memory and resolution controlled? | `servers/rendering/renderer_rd/storage_rd/light_storage.{h,cpp}`, especially `shadow_atlas_update_light` and slot allocation/reallocation; `scene/main/scene_tree.cpp` defaults | A bounded atlas is subdivided into slots; screen coverage influences resolution. Ownership is stable and reallocation has hysteresis. Adopt that policy, not Crimson's current grow-by-light-count rectangle. |
| How are visible lights selected? | `servers/rendering/renderer_scene_cull.cpp`, shadow/light culling and atlas update paths; `core/math/dynamic_bvh.h` | Spatial queries and visible-light demand precede expensive shadow work. Offscreen lights/casters are separate concepts: an offscreen caster can shadow a visible receiver. |
| What is the default memory scale? | `servers/rendering/rendering_server.cpp`, `scene/main/scene_tree.cpp`, `light_storage.cpp` | Positional and directional defaults are each 4096-square D16 base atlases, approximately 32 MiB apiece. Intermediate maps, additional viewports and backend differences add memory. This is not an equal-quality comparison with six 1024-square D32F faces per light. |
| How does an omni light fit? | `light_storage.cpp`, scene renderer omni-shadow conversion paths | Persistent omni allocation uses dual-paraboloid tiles, with cube-map rendering intermediates where applicable. Do not describe this as six full-resolution persistent faces per light. Keep Crimson's current projection initially. |
| Is there a universal 512 MiB streaming pool? | `editor/import/resource_importer_texture.cpp`, `scene/resources/compressed_texture.cpp`, `servers/rendering/renderer_rd/storage_rd/texture_storage.cpp` | The inspected imported-texture path explicitly disables ordinary texture streaming and creates complete imported images/mip chains. Sharing/refcounted resources and VRAM compression matter. Godot's behavior does not justify a fixed 512 MiB budget or prove that ordinary textures are streamed on demand. |

Useful recorded anchors in that checkout: texture streaming comment near importer line 757; compressed-texture load near line 132 and destruction near 462; texture allocation near 967/1012; positional atlas setup around `scene_tree.cpp:2161`; directional/positional format defaults around `rendering_server.cpp:3673–3681`; atlas allocation/update around `light_storage.cpp:2352–2649`; 500 ms reallocation tolerance in `light_storage.h` near 395. Verify symbols if lines move.

### Unreal

| Question | Source to inspect | Finding and implication |
|---|---|---|
| Texture pool sizes | `Config/BaseScalability.ini`, texture-quality sections | Inspected presets use 400/600/800/1000/3000 MB. These are configuration choices, not one universal engine budget. |
| VRAM percentage policy | Windows engine configuration, `PoolSizeVRAMPercentage` | The inspected configuration includes 70%; quality/device/project overrides affect the effective setting. Do not present 70% as an unconditional allocation policy. |
| Conventional shadow caching | `Source/Runtime/Renderer/Private/ShadowSetup.cpp`, `r.Shadow.WholeSceneShadowCacheMb` and static/movable cache setup | Separate shadow memory accounting and static/moving work are useful references. The conventional cache's inspected default is 150 MiB; it is not Unreal's total shadow/renderer memory or a description of Virtual Shadow Maps. |

Read the algorithms and ownership rules, then implement within Crimson's boundaries. Do not copy Unreal's renderer architecture, introduce a plugin system or bring a service locator into Crimson for this work.

## 4. Current Crimson path and integration map

The current normal frame is broadly:

```text
App frame / completed timing
  EditorSession: simulation and presentation scene
    extractScene: sorted ECS traversal + world transforms
    EditorResources::prepareShadows: full extracted model set
    Scene receiver visibility + resource preparation
    Game receiver visibility + resource preparation
  Renderer: caster construction, cache signatures, depth passes, receivers
  ImGui/platform presentation; completed frame metrics
```

| Responsibility | Existing location | Implication for the next change |
|---|---|---|
| Persistent identities, hierarchy, affine transforms | `src/scene/Scene.{h,cpp}` | A scene revision is currently broad. Spatial caching needs targeted changes or a measured change-diff stage; a global revision must not rebuild a world index whenever one actor moves. |
| Copied render extraction | `src/scene/SceneExtraction.{h,cpp}` | `extractScene` gathers/sorts entities and resolves transforms on each call. Add persistent CPU extraction/index state outside `App`; do not leak EnTT handles into rendering. |
| Frame composition and both cameras | `src/editor/EditorSession.cpp` | Scene and Game currently scan `extracted.models` separately. Build view demand once, then pass explicit results to resource preparation and renderer. |
| Geometry/material/mip preparation | `src/editor/EditorResources.{h,cpp}` | Consume actual allocated shadow detail; preserve immutable packet pins and readiness. Current one-per-caster aggregation is the starting point, not the final spatial solution. |
| Shadow draw construction and dispatch | `src/crimson-renderer/Renderer.cpp` | Avoid rebuilding/sorting every candidate draw and hashing it per light when cached content is unchanged. Keep draw geometry and shader coverage semantics here. |
| Shadow allocation/cache owner | `src/crimson-renderer/PointShadowRenderer.{h,cpp}` | Currently owns the combined point/directional D32F atlas, stable slots, uniforms and retired allocations. Introduce a tested CPU allocator/planner behind this owner. |
| Projection, fitting, bounds and LOD | `PointShadowMath`, `DirectionalShadowMath` | Preserve expanded cube guards, receiver-plane PCF, affine bounds, cascade upstream coverage and orthographic LOD demand. |
| Texture CPU caches and GPU owner | `TextureMipCache`, `TextureStreamer`, `AssetLibrary` | CPU workers decode/build caches; GPU publication remains main-thread AssetLibrary-owned. |
| Native presentation/device access | `src/crimson-renderer/PlatformSurface.cpp` | Existing borrowed bgfx D3D11-device pattern is the reference for adapter-budget queries. Do not create a second renderer device. |
| Physics/navigation | `CollisionResources`, `PhysicsWorld`, `NavigationMesh`, `PlaySession` | Render culling must not remove required collisions or trigger global navigation rebuilding every time a visual changes. |
| Import identities and Quader synchronization | `src/quader/`, `QuaderSceneIntegration`, managed model imports | Preserve source-key mapping, completed-save checks, transactional subtree replacement and user edits. Runtime cell residency is separate from authored import. |

### Current hard limits and the Forward+ change

`Scene.h`, `Scene::validate` and `QuaderImport.cpp` now support four directional lights and 4,096 point lights together with the clustered GPU renderer. This replaces the former whole-scene sixteen-point-light ceiling. Creation, duplicate overflow, save/load and Quader conversion/publication use the same source capacity. Existing scene format fields and stable light identities remain compatible.

Distinguish **authored world light count**, **lights selected for a camera**, **lights affecting a draw/cluster**, and **resident shadow maps**. Per-cluster source bitsets have no additional sixteen-light cutoff; a dense cluster can retain all 4,096 lights, with corresponding fragment cost. Shadow Step 2 admits sixteen eligible lights using squared distance from the frozen player focus normalized by light radius squared, with persistent-ID ties; without character geometry it uses the editor camera's target. Preparation and Scene/Game shader mapping consume the same origin. Legacy isolated callers without an origin retain source-order admission. Four complete dirty point maps may update per shared world frame, with deferred-age fairness and projection-coherent sampling. Larger capacities, admission hysteresis, variable map resolution and persistent spatial light queries remain later work. A scene with more than sixteen shadow-enabled lights retains their direct lighting but does not shadow all of them. See [the shadow checkpoint](SHADOW_ARCHITECTURE_CHECKPOINT.md) for implemented projection/cache policy and measurements.

Numeric bgfx view ordering is also a resource limit. Current IDs are compute 0; world point faces 1–96; Scene directionals 97–100; Game directionals 101–104; Scene 105–111; particle preview 112–118; reserved 119; picking/readback 120–121; thumbnail points 122–217; thumbnail directionals 218–221; thumbnail rendering/readback 222–226; material preview 227–233; Game 234–239; tools 240; platform windows 241 upward. See `RenderTypes.h`. The pinned D3D11 blit path must retain numeric execution order. Do not solve atlas packing by blindly adding one view per cascade or applying a nonidentity view remapping.

## 5. Required before/after measurement procedure

### Freeze the baseline before implementation

1. Record source revision, dirty diff/untracked source manifest, Release build settings, dependency baseline, shader compiler identity and binary hashes. Preserve a rebuildable baseline checkout or patch set; an old `.obj` is insufficient.
2. Copy the required managed project into a disposable benchmark root. Include referenced assets, scene settings, materials, camera paths and deterministic movement seeds. Never benchmark against the live user project while it can change.
3. Record SHA-256 hashes of scene/asset/config inputs. Record viewport pixel sizes, DPI, camera projections, exposure/IBL, shadow quality, lights, selected entities, visible panels, VSync/frame caps and driver/adapter IDs. Selection forces higher detail, so it must match too.
4. Run baseline and candidate separately, without concurrent builds, tests, shader compilation from unrelated work or another benchmark. Record unavoidable background GPU load.
5. Keep renderer-only and full-editor measurements as separate series. For full-editor acceptance, follow the repository's permission rule for PC interaction. Do not automate the user's applications without permission.

### Workload matrix to build when the slice starts

These are proposed deterministic fixtures, not tests already present:

| Fixture | Purpose | Required variants |
|---|---|---|
| Frozen original room | Regression continuity | No shadows; static shadows; moving visual subtree and camera; Scene + Game |
| 1k / 10k / 50k repeated mesh instances | Extraction, transform/index cost, resource sharing and submission scaling | Fixed visible subset while offscreen world size grows; shared versus several materials; deep hierarchy; a small moving subset |
| Distributed local lights | Authored-light versus active-light scaling | Use 16/64/256/1024 and the 4096 source boundary; most outside either camera; disjoint Scene/Game demand. Separate GPU cluster-assignment, shading and shadow costs. |
| Dense visible-light overlap | Detect silent lighting loss and shadow-budget behavior | Compare clustered rendering with the all-lights reference, including bit boundaries and more than sixteen overlapping lights; report the independent shadow admission count. |
| Directional outdoor level | Conservative upstream caster queries | Low-angle sun; tall offscreen occluders; long shadow distance; camera translation/rotation and cascade boundary traversal |
| Moving crowd | Static/moving shadow cost | Deterministic 1/32/128/512 moving mesh proxies at first; later real skeletal poses when animation exists; identical static environment |
| Asset pressure and traversal | Memory, mip promotion, eviction and uploads | Synthetic low budget; repeated route; teleports; source hot reload; selection/native-detail requests; cold and warm caches |
| Large Asset Browser | Editor interference and cache reuse | Thousands of entries, fixed card/DPI sizes, scroll down/back, GPU eviction and disk reload |
| Collision/navigation streaming | Simulation correctness and hitching | Crossing cell boundaries, path across adjacent tiles, pending collision, reimport and eviction near the player |

A fixture generator must preserve stable IDs, fixed seeds and asset hashes. Begin with shared simple geometry to isolate scene overhead, then repeat with representative assets; duplicated high-poly geometry alone obscures the problem being tested.

### Capture meaningful counters

For a durable benchmark harness, store raw per-frame CSV/JSON plus a manifest. Record at least:

- Wall frame, main-thread extraction/index update, visibility, shadow planning, material/LOD preparation, submission, simulation, navigation and upload/publication times.
- Whole-frame GPU time and per-view groups for point depth, directional depth, Scene, Game, post processing and editor rendering when available. Keep query frame numbers/latency; do not assign an old query to a new workload phase.
- Resident/pending/retired bytes by owner, physical atlas dimensions, used tile area, budget, cache hits/misses, evictions, promotions, upload bytes and outstanding jobs. A single total cannot explain pressure.
- Total versus queried/tested/visible models and lights; spatial nodes visited; caster candidates and accepted draws per light/cascade; prepared packets; unchanged-packet reuse; dirty-shadow reasons; draw calls and triangles.
- Geometry/material readiness, requested versus resident mip/LOD, degraded or missing shadow allocation, fallback draws and resource diagnostics. A fast incomplete frame is not an optimization.

Use at least 300 warm-up frames, then measure for at least 2000 frames and 30 seconds per steady phase, whichever takes longer, as an initial proposed protocol; repeat at least three times and alternate baseline/candidate order. Adjust duration to the phenomenon, recording the chosen stopping rule. Report median, mean, p95, p99, max and cold-load duration. The old 30-sample probe is too short for p99 or hitch conclusions.

Define warm completion from valid/coherent requested resources, completed uploads and the intended shadow-cache state, not an arbitrary delay. Track cold preparation separately. Re-entering a previously visited region must be a separate retention test. For pressure tests, report detail changes alongside time/memory savings.

If old scratch artifacts still exist, their positional CLI is:

```text
room_gpu_probe.exe <managed-project-root> <native-qproj> <native-scene-id>
                   <shaderc-exe> <shader-source-dir> <bgfx-include-dir>
                   --saved [--cold]
```

Use the exact actual executable name/path from the local compile response file. The old compile script has machine-specific MSVC/workspace paths; it is not an official portable build target. A future maintained harness should be an opt-in CMake executable built through `Build.cmd`, with fixture/output paths as arguments. Preserve its CPU/GPU lifetime and isolated-project cleanup; never recursively delete a computed path without checking that it lies under the disposable root.

The verified local invocation below is useful only while those artifacts exist. It builds the current implementation; it does not reconstruct the historical before build. Run from the repository root after satisfying the renderer-close rule, and inspect the machine-specific compile script before using it:

```powershell
.\Build.cmd Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
& .\build\diagnostics\quader-performance\compile_gpu.cmd
if ($LASTEXITCODE -ne 0) { throw 'Performance probe compilation failed.' }

$probeRoot = Join-Path (Get-Location) 'build\diagnostics\quader-performance'
$probeExe = Join-Path (Get-Location) 'build\Release\bin\room_gpu_probe.exe'
$probeArguments = @(
    (Join-Path $probeRoot 'engine-original86')
    (Join-Path $probeRoot 'frozen-native\q-test\q-test.qproj')
    '6aeb7097-5046-450e-a451-a8b4518aa095'
    (Join-Path (Get-Location) 'build\vcpkg_installed\x64-windows\tools\bgfx\shaderc.exe')
    (Join-Path (Get-Location) 'src\crimson-renderer\shaders')
    (Join-Path (Get-Location) 'build\vcpkg_installed\x64-windows\include\bgfx')
    '--saved'
)
& $probeExe @probeArguments 2>&1 |
    Tee-Object (Join-Path $probeRoot 'new-baseline-warm.log')
if ($LASTEXITCODE -ne 0) { throw 'Warm performance probe failed.' }
& $probeExe @probeArguments '--cold' 2>&1 |
    Tee-Object (Join-Path $probeRoot 'new-baseline-cold.log')
if ($LASTEXITCODE -ne 0) { throw 'Cold performance probe failed.' }
```

Preserved artifact SHA-256 values for provenance, not future pass/fail expectations:

| Artifact | SHA-256 |
|---|---|
| `room_gpu.cpp` | `2803DF13B4FF0440AA5CB01743270E48C7E4B78027A809E322825E3FFFEDF774` |
| Frozen original `editor-session.json` | `A355D4726803BD53F35A1ADCB591B62FD39A9D43D416E0ECCCAB1195AE1E9CD6` |
| Frozen native fixture `.qproj` | `8935C29FCCC59FDF36B2D97A5972267B6DF94C2B3B93E991C914182B997726C2` |
| `room_gpu_original86_final.log` | `C0ED08FA06ECAC0A406CF2848056D44068189611BD10A76E218F340634B8F21C` |
| `room_gpu_original86_cold_final.log` | `7CEE66B1CC51D361A3633CEC88BAF272320389A0F1FD6CF4E1127C7AA6B8AF3A` |

### Interpretation of the current F1 overlay

`FrameMetrics.h` publishes an averaged readout roughly every 250 ms. `App::run` supplies completed wall time and elapsed main-thread preparation/submission time before `bgfx::frame`; CPU work excludes the explicit bgfx/native-presentation wait. It is not thread busy-cycle measurement or process CPU utilization and does not sum worker CPU time. Timings and draw counts cover the editor's complete frame, not only Game.

Pinned D3D11 whole-frame queries run when timer support is available without `BGFX_DEBUG_PROFILER`; that flag is needed for separate per-view profiling. Reject unknown frequencies/timestamps and frame zero. The displayed device-memory denominator comes from backend driver availability information, not installed physical total or the WDDM process budget. The texture/program line includes graph programs; the explicit model/shadow lines do not constitute complete engine GPU usage. Use the overlay to locate a problem; use raw scoped measurements to prove a change.

## 6. Implementation sequence and stop conditions

Implement each stage as a reviewable slice with its own baseline, correctness checks and results. The original order below followed the measured CPU-preparation and shadow-allocation problems. The user subsequently requested Stage C's clustered Forward+ migration first. That does not complete the spatial-index or shadow-allocation stages.

| Stage | Deliverable | Complete when |
|---|---|---|
| A | Durable benchmark and scoped counters | Matched before/after input identity and raw results can be reproduced without the old ignored objects. |
| B | Incremental world extraction and spatial queries | Fixed visible demand stops scaling preparation with the entire offscreen world; randomized queries remain conservative. |
| C | GPU clustered Forward+ and world-light-count separation — **implemented; user acceptance pending** | 4,096 authored/imported source lights, GPU sphere/cell bitsets, camera-local data and independently mapped sixteen-light shadow admission. Dense-overlap correctness uses an all-lights reference. Persistent world/light spatial queries remain Stage B. |
| D | Bounded stable shadow allocation and allocated-detail planning | Shadow physical memory stays within a defined budget, with reported resolution changes and stable ownership. |
| E | Persistent caster packets and targeted invalidation | Static cache hits avoid rebuilding draw/material data; one actor only dirties relevant light/cascade relationships. |
| F | Separate static and moving shadow work | Crowd motion no longer resubmits the static level per affected light, with measured composition/budget cost. |
| G | Hardware-aware residency policy | Adapter budget changes cause bounded, stable degradation and recovery without losing coherent drawable resources. |
| H | GPU block-compressed mip caches | GPU bytes fall with verified color/normal/alpha behavior and bounded worker/upload work. |
| I | Spatial asset, collision and navigation streaming | Traversal has bounded working sets and publication work without missing collisions, lost authored state or global navigation stalls. |

The following sections define the implementation contracts for those stages. No stage is complete merely because an interface or configuration field exists.

## 7. Incremental extraction and a persistent spatial index

### Ownership and data

Implement a CPU-only dynamic AABB tree in a concrete location such as `src/scene/SpatialIndex.{h,cpp}`. Use Crimson math and persistent `EntityId` values. Start with a dynamic tree, conservative fat bounds, incremental refits/rotations and reused query scratch vectors; do not leave the initial implementation undecided between multiple spatial structures.

Keep geometry proxies and positional-light influence volumes in separate trees or explicitly separate categories. Each model occurrence gets a proxy, even when several entities reference the same managed asset. Geometry/material/GPU versions remain owned by the existing asset and rendering systems, not the tree.

Proposed proxy contents:

```text
world epoch + persistent entity ID
current full affine world transform and conservative world AABB
asset identity + metadata/bounds generation
category/cast/receive flags
transform/bounds/coverage generations
```

Proposed operations: `reset(worldEpoch)`, `upsert(proxy)`, `erase(entity)`, `queryAabb`, `querySphere` and `queryConvex`. Return IDs or stable proxy handles with generations, never persistent pointers into a movable vector. Missing/invalid bounds go into a conservative fallback list; do not put infinite AABBs into a balanced tree or treat missing metadata as invisible.

`EditorSession` owns session reconciliation and the query results shared by resource preparation. Renderer-specific draw matrices, buffers and leases stay in `src/crimson-renderer/`. Keep `App` a lifetime/frame boundary. Do not create a general engine service registry to distribute the index.

### Change feed

`Scene` already has authored revision and a separate runtime `poseRevision`. Neither is a per-entity dirty feed. `SceneDocument` command differences/history application, `Scene::applyRuntimePoses`, `PlaySession` presentation interpolation and resource publication are the boundaries to extend.

1. Emit or accumulate changed persistent IDs for create/delete/component/transform/reparent operations. A parent transform dirties descendants' world transforms; process the hierarchy in dependency order.
2. Track metadata-bound replacement independently of entity transforms. A model can grow after hot reload with its scene transform unchanged.
3. Update from the interpolated **presentation** world used for rendering. Fixed-tick-only bounds can incorrectly cull fast-moving actors and their child visuals.
4. Use a world epoch that distinguishes document replacement, Play world replacement and restored editor presentation. Clear derived memberships when the epoch changes.
5. Cache unchanged world transforms/extracted proxies. A full scan/diff is acceptable as an initial correctness fallback, but record its cost and replace it before claiming incremental scene scaling.
6. Keep transient editor placement previews and independent saved-scene thumbnail worlds outside the persistent active-world tree, or give them separate explicit contexts.

Initially keep the brute-force path available in CPU tests as an oracle. Query results may contain conservative extras but may not omit any object the old exact/conservative tests would include.

### Receiver and caster queries

- Query Scene and Game receiver frusta independently. Preserve selection behavior and picking's exact frame/resource identity. A selected offscreen model can require resource residency without becoming a visible draw.
- Find positional lights whose **influence volume** intersects visible receivers or a conservative receiver volume. Testing only the light's center against the camera is incorrect.
- For an admitted point shadow, query the light sphere, then refine candidates against each guarded face frustum using the current math helpers.
- For a directional cascade, derive its receiver footprint first. Query a conservative volume extruded upstream toward possible occluders; use scene bounds or the tree to determine a safe finite search extent. A camera frustum query alone misses trees/buildings outside the image that cast into it.
- Preserve the current cascade depth fitting from candidate bounds. Avoid the circular shortcut of fitting depth from already-clipped casters that the same fit excluded.
- Combine Scene/Game demand before selecting shared point shadow work. Keep their directional cascade fits separate. Hidden Game or unavailable cameras must stop generating unnecessary demand without changing logical camera identity.

### Relationships and dirty propagation

Maintain caster-to-light and light-to-caster relationships, or derive changes with old/new overlap queries. On motion, query the union of old and new bounds and invalidate both departed and entered lights. Deletion must remove previous depth as well as remove the proxy. A light move/radius edit similarly affects previous and new memberships.

Directional lights do not have finite positional influence proxies. Caster changes must invalidate affected upstream cascade volumes in both camera contexts, or conservatively all active directional contexts until tighter relationships are proven. Newly entered upstream casters must trigger depth refitting before the final clipping decision; otherwise a previously cached depth range can incorrectly exclude them.

Godot's geometry/light pair **and unpair** callbacks dirty shadows. Its `DynamicBVH` and renderer culler are useful references:

- `renderer_scene_cull.h`: geometry/volume indexers and per-light cache state.
- `renderer_scene_cull.cpp`: `_instance_pair`/unpair paths near 176/310; bounds updates near 1605–1745; shadow convex queries near 2417–2634; incremental tree optimization near 4367.
- `core/math/dynamic_bvh.h`: insert/update/remove/optimization and AABB/convex queries near 292–308.
- `rendering_light_culler.cpp`: camera/light intersection and shadow-caster volume construction.

**Acceptance:** grow an offscreen world from 1k to 10k to 50k instances while keeping camera-visible content and local changes fixed. Report extraction/update cost separately from query/preparation cost. Static frames must not rebuild the tree; one moving actor must update its affected proxies/relationships rather than all lights. Randomized affine, reparent, deletion and metadata-replacement cases must match a brute-force oracle conservatively.

## 8. Separate authored lights from draw and shadow budgets

### Implemented Stage C contract

`ClusteredLightingGpu` replaces the fixed sixteen-point-light shader arrays with GPU light records and conservative cluster bitsets. Keep these rules when extending it:

1. Preserve the 4,096 source-point-light capacity shared by `RenderTypes.h`, `Scene.h`, scene validation and Quader conversion. A capacity increase must update GPU storage, bitset addressing, adapter checks, scene/import validation and source/shadow mapping together. Do not silently accept and truncate an oversized source.
2. Each camera owns a separate GPU atlas and upload buffer. The grid starts with 32-pixel top-left tiles and uses 24 logarithmic positive-view-depth slices over the camera's sanitized near/far range. Double tile size until `tilesX * tilesY * 24 <= 262144`; a 5120×2880 viewport therefore uses 64-pixel tiles. The actual tile size is supplied to assignment and fragment lookup together. The cell index is `(slice * tilesY + tileY) * tilesX + tileX`. Partial edge tiles use the actual viewport boundary. Coarser clusters preserve coverage while increasing false-positive work. CPU-oracle comparisons at these resolutions must use the same actual tile size through `Config::tileSize`.
3. `cs_cluster_lights.sc` tests finite influence spheres against cell planes. A light center outside the view can still illuminate a visible wall. Near-plane intersections and a camera inside the volume remain conservative. The production assignment runs on GPU; `ClusteredLighting` is the separate reusable pure CPU oracle with compact lists, source indices and explicit capacity failures.
4. Active valid sources compact into GPU records that retain original source and selected shadow indices. Bitsets retain one bit per compact GPU record and enough words for the active count. Dense overlap has no separate per-cluster cap and no nearest-sixteen approximation. Fragment cost remains proportional to the influencing lights and assignment cost depends on the grid/source workload. This slice does not make overlapping lights free. The CPU oracle's compact lists use original source indices directly; do not compare those numbers to GPU bit positions without the record mapping.
5. Do not reduce assignment using only opaque depth. Transparent meshes and lit particles use the same conservative three-dimensional grid and must retain lights affecting surfaces in front of, behind or outside opaque-depth coverage.
6. The RGBA32F atlas combines the unchanged BRDF lookup, source light records and cluster masks at generated-material stage 14. Keep material stages 0–11, environment cubes 12–13 and shadow comparison stage 15 available. `clustered_lighting.sh` reconstructs the existing BRDF sampling coordinates and supplies one light iteration contract to generated/fallback mesh and particle paths. HDR, material factors, normal maps, fog, coverage and presentation remain independent.
7. `ShadowLightSelection` now admits sixteen eligible shadow-enabled lights using the frozen world priority origin and normalized spatial influence; legacy isolated callers without an origin retain source order. Source index, selected shadow index, persistent light identity and the shadow owner's atlas slot are distinct. Apply the same selection and origin to caster preparation and surface metadata; a shadow source at index 63 must work when the preceding 63 lights do not cast shadows. Unadmitted lights retain direct illumination. Preserve the explicitly linked procedural Sun and the four directional-light limit.
8. Atlas resources and upload storage remain renderer-owned, include pending/retired bytes, and release before bgfx shutdown. Source capacity and adapter texture/storage failures must be diagnosed without publishing a truncated list. The current per-camera grid limit is 262,144 cells, with adaptive tile growth as above and additional adapter storage capability checks. Exact snapshots retain unchanged records/masks without repeated uploads or compute dispatch. Color/intensity/shadow-only changes can refresh fragment records while preserving valid masks when the influencing sphere sequence stays equivalent. A target retains its atlas capacity until destruction; measure retained and retired bytes as well as the immediate workload.

### Validation and remaining extensions

`clustered_lighting_tests.cpp` checks conservative world-point samples projected by actual camera matrices, camera/frustum edge cases, dense 4,096-light coverage, capacity overflow, allocation reuse and source-to-shadow mapping. `forward_plus_render_tests.cpp` compares hidden D3D11 clustered images with the same renderer's all-lights reference across light counts, depth, multiple cameras, dense overlap, bitset boundaries and a shadow source beyond index sixteen. The all-lights path is a test diagnostic, not a user-facing renderer mode. These tests establish lighting equivalence; matched performance runs are still required for performance claims. Preserve raw before/after measurements separately from the historical room evidence in Section 2.

Future Stage B should query camera/light candidates spatially before upload and avoid rebuilding complete world data for unchanged frames. Stage D should derive shadow requests from the union of Scene/Game demand, apply importance/resolution policy and retain stable allocations. A shadow admission decision must never disable the direct BRDF contribution. Player light rigs, lighting receiver channels, independent caster masks and ambient attenuation are separate future features, not implied by Forward+.

Current fixed bgfx shadow view ranges can update sixteen point lights per world pass. Admitting more resident shadow lights does not automatically provide more update views. Keep an explicit update/admission budget, preserve deterministic priorities and measure latency if updates are deferred. Never present an evicted or stale map as current. Larger active-light algorithms must include the view schedule and shader uniform/capability checks in their design.

The current implementation uses `firstView + slotIndex * 6` and fixed sixteen-element ownership arrays. Extending residency requires separating **world light ID**, **allocation handle/generation**, **draw-local shader index** and **frame-local update ordinal**. With the current view schedule, only update ordinals 0–15 can own the six-face view groups; an arbitrarily numbered persistent allocation cannot become a view ID directly.

**Acceptance:** many spatially separated authored lights survive save/load, Quader import and scene editing; receivers get the expected local lights in both cameras; source order does not change direct illumination; stable IDs retain atlas allocations while their shadow admission remains unchanged; dense overlap retains all influencing lights within capacity. Include a light with its center offscreen but radius intersecting a visible wall. Shadow Step 2 makes finite-origin spatial admission independent of source order when persistent IDs are present. Admission hysteresis and resolution policy remain Stage D work.

## 9. Bounded shadow storage and a frozen frame plan

### Allocator policy

Introduce a CPU-tested rectangle allocator under the existing renderer shadow owner. A deterministic free-rectangle allocator with coalescing is suitable for the first version. Keep contiguous point blocks of **3 × 2 faces** and directional camera/light blocks of **2 × 2 cascades**. This avoids changing projection conventions and the grouped directional vertex transform at the same time as residency policy.

Choose physical atlas dimensions from an explicit shadow-memory budget and device texture limits. Keep D32F initially; D16 is a separate precision/bias tradeoff. Illustrative D32F capacities are 4096² = 64 MiB, 8192 × 4096 = 128 MiB and 8192² = 256 MiB. These are examples to evaluate, **not selected new defaults**. Include the independent thumbnail owner and old/new allocation overlap when reporting peak memory.

Physical guards matter. For inner resolution `R`, pitch is `R + 4`. A 1024 face occupies 1028² texels; a point block is 3084 × 2056; a directional block is 2056 × 2056. Two such directional blocks do not fit in 4096². Do not silently reduce the inner resolution to make power-of-two arithmetic convenient.

Policy order:

1. Derive desired resolution from projected influence/receiver coverage in physical pixels, taking the maximum across Scene and Game for shared point maps. Treat near-plane intersections and a camera inside the light volume conservatively.
2. Quantize to supported resolution classes bounded by the quality setting. Preserve existing 256/512/1024 as maximum choices; adding a smaller class is an explicit quality-policy change with tests.
3. Reuse a stable owner's rectangle when its class remains suitable. Key by world epoch, light identity and logical camera context where applicable.
4. Use time-based resize/eviction hysteresis, beginning with Godot's 500 ms as a reference to measure, not an unexplained constant. Emergency pressure can shrink sooner; improvement should require sustained demand/headroom.
5. Reclaim no-longer-demanded owners, then lower lower-priority requests deterministically until the set fits. Preserve priority stability around thresholds. Report requested versus allocated resolution and allocation failures.
6. Do not repack the entire atlas every frame. Coalesce free regions; handle fragmentation with controlled repacking into a replacement allocation or bounded admission degradation. Repacking invalidates maps and has a measurable memory/render cost.

`steady current atlas <= configured capacity` and `peak including retired/replacement atlases <= capacity plus explicitly reserved transition allowance` are separate assertions. The current owner retires old allocations for queued work; preserve safe lifetime rather than immediately freeing a texture referenced by submitted draws.

### Plan before preparing resources

A proposed immutable `ShadowFramePlan` should include:

```text
world epoch, frame/version, shader generation
stable light identity and logical camera context
allocation identity/generation, block origin and physical extent
requested and allocated inner resolution
camera/cascade fits derived at allocated resolution
caster candidate identity/generations and update decisions
validity/publication state and resource/atlas lifetime pins
```

Build and freeze the plan on the main rendering boundary before `EditorResources::prepareShadows`:

```text
presentation proxy/index update
  -> Scene/Game receiver and relevant-light queries
  -> desired resolution + stable bounded allocation
  -> cascade fits and conservative caster queries
  -> geometry/mip demand using allocated detail
  -> coherent packet preparation
  -> point maps once + camera-specific directional maps
  -> receiver draws consume the same frozen plan
```

The existing `ShadowCamera` contains only camera and aspect. Screen-coverage planning also needs physical viewport width/height and a stable logical camera identifier; introduce an explicit demand value carrying those fields instead of assuming they already exist. Resize/DPI changes must update demand, while allocation hysteresis controls unnecessary churn.

The planner/allocator belongs under `src/crimson-renderer/`; scene spatial data stays CPU-only. Pass concrete bounds/demand values across that boundary, not mutable scene pointers or a service lookup. Add the integration in `EditorSession`, keeping `App` thin. Logical Game context must remain identifiable even if Scene is hidden; do not alias camera contexts merely by their temporary array position.

Newly assigned or moved rectangles must be cleared/rendered before their new owner is sampled. Until publication succeeds, use an explicitly valid previous mapping where compatible or disabled shadow state. Never reuse another light's depth. If allocation fails, report it and preserve coherent remaining state; do not publish half-updated matrices/rectangles.

Keep update/publication units explicit: a point map writes all six faces before becoming valid. Shadow Step 2 budgets complete point maps, retains coherent old content only for unchanged light projections, and disables sampling of new/moved projections until their six faces finish. Directional maps now clear their individual guarded tile with a depth-only draw and cache each projection separately; active Scene blocks are 4×1, Game blocks 2×1 and isolated thumbnails 2×2. Clear-region and independent-cache GPU checks cover this change. Splitting a single cube map across frames still requires inactive storage, completion state and old/new overlap accounting; the current scheduler does not publish partial faces.

### Required shader contract changes

Changing only `ensureAtlas()` is insufficient:

- `point_shadow.sh` currently reconstructs the face tile from a slot number, global columns, global resolution and pitch. Add explicit per-light pixel metadata, for example `{blockOriginX, blockOriginY, innerResolution, pitch}`. Compute the existing face offset inside that 3 × 2 block. Keep the one shared comparison sampler.
- Use the allocated per-light resolution for normal bias, receiver-plane gradients, face projection expansion and clamping. The quality maximum must not remain hidden in any of those calculations.
- `directional_shadow.sh` already has normalized inner rectangles, but still reads global resolution/pitch. Derive square-map inner resolution from `rect.z / atlasInverseWidth`, with pitch `R + 4`, or provide explicit equivalent metadata. Use it consistently for bias and PCF.
- Initially retain one equal-resolution 2 × 2 cascade block per light/camera. Independent cascade sizes require a further change to `directional_shadow::casterTile` and its vertex mapping.
- Update C++ uniform creation, disabled state, bindings, named shader manifests, generated graph manifests, shader preflight and cache contract versions together. Relevant files include `PointShadowRenderer.cpp`, `ShaderPreflight.cpp`, `MaterialGraphShader.cpp` and both shared shadow includes.
- Preserve the two guard texels, face-axis tie rules, overlapping expanded cube projections, derivatives evaluated before discard, receiver-plane correction and exactly one final sRGB encoding. Bounded allocation is not a reason to remove those correctness mechanisms.

**Acceptance:** allocator non-overlap and byte arithmetic; stable owner reuse; guarded edge sampling; bounded pressure behavior; resolution hysteresis; world/Play switching; both cameras and thumbnails; alpha masks; low sun/upstream casters; source/shader reload; camera-hidden cases; unsupported allocation/format fallback. Readback tests should detect an old owner's shadow being sampled after slot reuse. Performance results must include image/detail changes if budget pressure lowers resolution.

## 10. Persistent caster packets and exact invalidation

The current GPU shadow cache checks its signature **after** CPU caster traversal, matrix/LOD construction and hashing. A cache hit must eventually avoid that repeated CPU work too.

Cache instance-independent model draw recipes, then instance world/normal matrices and resolved geometry/material versions. Maintain per-light/cascade candidate membership and dirty generations. Keep texture projection demand separate from the immutable recipe: each camera/light can require different mip/LOD detail.

`textureDemand()` still traverses active graph textures/mappings inside primitive traversal. A proposed immutable `GraphDemandPlan` can hold active texture nodes, UV requirements, constant mappings and flags for runtime-dependent terms. Compute it once per immutable graph; evaluate dynamic terms with the current world material context. Do not reuse one camera's projected footprint globally.

Cache keys must include model/metadata revision, material revision, reference/deletion-policy revision, shader recipe generation and the relevant allocated detail. Current GPU source versions/readiness remain dynamic inputs. A metadata cache cannot certify a newly uploaded texture coherent.

Distinguish the full packet/binding key from the narrower **depth-content key**. Broad material/runtime-definition revisions must replace prepared packets, but only geometry, culling state, coverage and relevant depth program changes need invalidate shadow pixels. The current `coverageSignature()` conservatively starts from the whole prepared packet signature and adds active animated coverage. It already avoids invalidation from an opaque animation's clock alone; avoiding redraw after unrelated authored color edits requires the finer key proposed here. Do not claim that authored-edit precision already exists.

| Change | Required shadow/cache action |
|---|---|
| Entity creation/deletion or cast flag | Update membership and invalidate previous/new affected lights. |
| Actor/parent/presentation transform | Recompute affected descendants' bounds/matrices; dirty lights overlapping old or new bounds. |
| Geometry/metadata/LOD replacement | Refit bounds if needed; replace draw recipe and dirty affected depth. |
| Mask mode/cutoff, alpha texture/sampler/mip/source | Invalidate affected coverage and depth even when geometry is stationary. |
| Active masked Time/Panner coverage | Use current `coverageSignature(context)` or a dynamic classification; regenerate affected depth. |
| Opaque color-only Time/Panner, emissive, exposure | Update surface appearance without invalidating unchanged depth. |
| Point position/radius or allocated resolution | Refit/reallocate and regenerate affected faces. |
| Directional projection/camera context | Recompute cascade fit; regenerate changed projections. |
| Shader replacement | Invalidate the affected depth program generation atomically. |
| Material graph edit with identical time-zero values | Preserve `runtimeDefinitionSignature`; replace future runtime evaluation correctly. |

Long-lived cached packets cannot pin every historical mip/LOD forever. Retain exact versions for submitted frames, transfer to new coherent packets and release obsolete versions after queued use. Evict inactive CPU packets/relationships with a bounded policy separate from authored identity.

**Acceptance:** after pending LOD/mip/shader publication has drained, a fully prepared frame with unchanged sources, coverage and projections has zero caster recipe rebuilds and zero shadow writes. Legitimate asynchronous refinement must still invalidate affected depth. Moving one actor dirties only related lights; moving it out of range clears its old shadow; masked animation invalidates while opaque color animation does not; hot reload and deletion cannot keep stale resources alive indefinitely. Compare result images and candidate membership to the existing path before removing the oracle.

## 11. Static and moving shadow separation

Do this after CPU packet reuse is measured. It adds GPU memory and composition work and should be justified by the moving-crowd profile.

Classify depth contributors by actual transform/geometry/coverage changes, including character visual descendants and animated masks. An unchanging transform is insufficient to call a masked material static. A moved authored prop must leave the old static set and invalidate its previous cached depth.

Maintain separately budgeted static depth, then rebuild an affected final tile from valid static depth and current dynamic casters. Drawing today's actor over yesterday's combined depth leaves the old actor's shadow behind. When the dynamic set becomes empty, the final tile still needs restoration.

Nearest depth must be composed **before PCF**. Multiplying or taking the minimum of two already filtered visibility values is not equivalent to filtering the combined occluders. Preserve one final surface-visible comparison atlas to fit the current material sampler contract.

D3D11 cannot bind the same depth subresource for reading and writing simultaneously, even for disjoint rectangles. Use a separately owned static-depth/scratch resource and a validated depth-copy/composition pass into the final atlas, or prove another backend-supported path. Do not assume bgfx supports partial depth blits without checking capability and behavior. A dedicated copy pass may sample a texture without adding a sampler to material surface shaders, but its resources, view order, depth writes and bandwidth still need tests.

Include static storage, final storage, scratch, retirement and replacement overlap in the shadow budget. Camera movement can change directional cascade projections even when every object is static; cache projected depth only while its projection remains valid. Reusing draw recipes is more general than reusing depth pixels.

**Acceptance:** an actor moves, leaves the light and disappears without residual shadow; static occluders remain; intersecting static/dynamic edges match a combined reference; alpha masks and cascade transitions agree. Report static/dynamic submissions, copy/composition GPU time and peak bytes alongside frame improvement. A split that doubles memory and slows the real workload should not become the default merely because Unreal has analogous modes.

## 12. Hardware-aware budgets and scalable resource bookkeeping

### Finish accounting first

`ResourceConfig` currently uses two workers, a 128-job queue, 512 MiB CPU retention, 512 MiB streamable GPU retention, 64 MiB thumbnail configuration and five-second ordinary idle grace. These are soft targets and estimates. For example, `AssetLibrary` currently reports `sizeof(MaterialAsset)` for loaded material payloads, excluding heap graph nodes, links and strings. Do not label that CPU counter as total process memory.

Define a plain CPU budget snapshot with separate categories for CPU payload/staging estimates; current/pending/retired GPU geometry and textures; graph programs; shadow/environment/post-process targets; thumbnails and readbacks. Track wanted detail and upload queues separately. Shared allocations must be counted once even if many leases reference them. Driver-reported usage overlaps engine accounting; do not add the two totals.

`ResourceManager::pump()` currently copies/sorts headers; `trim()` constructs/sorts candidates; invalidation scans dependencies transitively. With large catalogs, measure these costs independently of visible rendering. Replace measured full-catalog hot work with amortized priority/LRU structures and reverse dependency indexes while preserving epochs, cancellation and immutable snapshots. A metadata cache's pruning threshold is not a hard capacity bound.

### Renderer-owned adapter budget provider

Use the actual bgfx D3D11 device, following `PlatformSurface::Impl::ensureDevice`. Query its `IDXGIDevice`/adapter and then `IDXGIAdapter3::QueryVideoMemoryInfo` for local and nonlocal segments. Do not create a second D3D device or expose COM pointers outside the renderer. Release references/events before bgfx shutdown.

Publish adapter identity, timestamp/validity, budget, current process usage and reservation values. Poll modestly, such as roughly once per second, or use budget-change notifications. Never query the driver per material/draw. Unsupported API, UMA, device removal and stale snapshots require an explicit conservative fallback.

Primary references: Windows SDK `dxgi1_4.h` (`DXGI_QUERY_VIDEO_MEMORY_INFO`, adapter query/notification APIs); Unreal `Source/Runtime/RHICore/Private/DXGIUtilities.cpp`, `UE::DXGIUtilities::GetD3DMemoryStats`; `Source/Runtime/Windows/D3D11RHI/Private/Windows/WindowsD3D11Device.cpp`. The current bgfx NVAPI readout is not a substitute for these process-budget values.

### One policy coordinator, existing resource owners

Introduce a coordinator that sets category targets/admission limits. Keep texture/model/shadow/etc. allocation ownership in their existing RAII owners. Multiple owners must not independently spend the same free headroom.

A starting policy computes a candidate streamable target from known resident streamable bytes plus the **signed** surplus/deficit `processBudget - processUsage - safetyReserve`, then safely clamps the resulting target to zero and platform/user policy. Preserve a negative deficit until that combination is complete; clamping headroom to zero first would fail to reclaim memory when already over budget. For example, 500 MiB resident streamable assets, 1200 MiB process usage, a 1000 MiB process budget and a 100 MiB reserve imply a roughly 200 MiB target. Use checked arithmetic and record every category. `processUsage` already contains fixed and managed allocations; subtracting fixed targets again without accounting for that overlap is wrong. Pending driver visibility and allocations about to be submitted need a conservative burst reserve.

Shrink new admission promptly under pressure; grow detail only after sustained headroom. Include old/new texture or atlas overlap and independently cap upload bytes/time. Preserve coherent coarse resources while sharper ones wait. Selected/native-detail leases may exceed a soft retention target; define that behavior explicitly instead of advertising an impossible hard ceiling.

Unreal's `FRenderAssetStreamingMipCalcTask::UpdateBudgetedMips_Async` in `Source/Runtime/Engine/Private/Streaming/AsyncTextureStreaming.cpp` is the concrete reference for separating nonstreaming memory, margins, temporary budget and asymmetric shrink/grow decisions. Copy the reasoning, not its constants.

**Acceptance:** inject deterministic unavailable/UMA/stale/shrinking-budget snapshots; simulate external pressure; verify no oscillating promotion/eviction or invalid handles; check shared leases, cancellation, source reload and readback shutdown. Real-adapter testing supplements policy tests. Memory reports must distinguish steady targets, admitted work and unavoidable live/retired overage.

## 13. GPU block-compressed mip caches

The current `TextureMipCache` is role-aware RGBA8 storage (`CRMIP001`, version 1); `TextureStreamer` assumes four-byte pixels and RGBA8 uploads. File compression or PNG size does not reduce that GPU allocation.

### Derived format and worker pipeline

1. Introduce a new disposable cache format with storage format, block dimensions/bytes, channel/normal encoding, per-mip dimensions/offsets/payload lengths/checksums and recipe version.
2. Include source fingerprint, role, encoder/version/options, channel policy and normal convention in the cache identity. Different roles or encodings must not alias one file.
3. Generate correct mip values before compression on workers. Keep managed source images intact. Do not perform compression in frame submission.
4. Select a backend-supported format with an RGBA8 fallback. For the first version, BC7 is a possible RGB/RGBA-capable path; BC1/BC4/BC5 require explicit channel/alpha rules and quality evaluation. Encoder availability/licensing and pinned dependency changes need review before choosing one.
5. Upload block-aligned regions and correct small mip tails. Account compressed bytes rather than `width * height * 4`; use `ceil(width/4) * ceil(height/4) * blockBytes` for conventional 4 × 4 blocks, including sub-block levels.
6. Preserve coarse-first complete textures, chunked uploads and atomic complete-version publication, source readiness and old-version leases. Do not expose a texture whose advertised mip tail has not been uploaded.
7. Use idempotent atomic derived publication and checksum repair. A new encoder/cache version must not reinterpret existing RGBA caches as compressed data.

### Material correctness

- **Color:** filter RGB in linear light, retain alpha as linear data and sample with the appropriate sRGB behavior. Do not apply sRGB twice or use a format that lacks required decoding without shader support.
- **Data:** preserve all channels that any consumer may read. glTF metallic/roughness use packed G/B; Quader's independent maps use red. Choosing BC4 because today's visible material reads red can break another graph sharing the image.
- **Normals:** the current shared `ccDecodeNormal()` consumes RGB. BC5 RG cannot silently replace it. Initially retain RGB-capable storage, or add explicit XY normal-encoding metadata and consistent reconstruction before normal strength, UV and triplanar transformations. Do not discard authored blue/sign behavior without a defined conversion and tests.
- **Coverage:** inspect cutout edges at multiple mips and thresholds. Compression and mip filtering can change shadow/picking coverage. Any coverage-preserving mip policy needs a versioned recipe and agreement across passes.
- **HDR/environment:** keep the existing float preprocessing/cache contract separate. BC6H or environment compression is a later evaluated change, not an automatic consequence of ordinary color texture compression.

Godot references: `ResourceImporterTexture::_save_ctex` in `editor/import/resource_importer_texture.cpp`; `Image::detect_used_channels` in `core/io/image.cpp`; `modules/etcpak/image_compress_etcpak.cpp`. Its channel selection and sRGB-format constraints matter more than the name of a codec. Normal-variance-driven roughness mip generation is another possible later feature; Crimson does not currently implement it.

**Acceptance:** NPOT, 1 × N and sub-block tails; corrupted cache descriptors/payloads; concurrent publication; encoder-version changes; unsupported-format fallback; transparent edges; packed channels; normal strength, mirrored UVs, triplanar and double-sided surfaces. Compare both GPU bytes and image error/appearance. Keep decoded/staging peaks and compression wall time in the results.

## 14. World cells, Quader identity, physics and navigation

GPU mip/LOD streaming already exists. Spatial world activation and unloading do not. Add an authored descriptor layer before using runtime ECS residency to represent a large world.

### Authored descriptors and runtime residency

Define world/cell manifests containing stable logical entity IDs, bounds, asset dependencies, generation and cross-cell references. An unloaded entity must remain authored and discoverable for saving, Undo, references and deletion transactions. Runtime absence must not mean deletion.

Cell packages are detached CPU values produced by workers, then published on the main thread after checking document/world/cell generations. Preserve full affine parent transforms when a parent is in another unloaded cell; define which ancestor descriptors stay resident. Do not serialize EnTT handles, GPU objects or the current camera's active set.

Demand combines Scene/Game cameras, player movement, predicted traversal, route corridors and offscreen shadow occluders. Use separate residency radii/priorities and hysteresis for rendering, physics and navigation. A camera teleport must cancel obsolete requests without letting their late completion activate the wrong generation.

### Quader integration

`SceneDocument::replaceQuaderImport` preserves entity IDs using `QuaderGenerated::sourceKey` in one transaction. Current keys include mesh occurrence/geometry/definition identities and light identities. Preserve those mappings through partitioning; do not derive persistent IDs from cell coordinates that change whenever a bounds policy changes.

Quader sync remains an authored source transaction with completion fingerprints, import-generation checks, manual sibling preservation and existing material-refresh policy. A source update can produce new cell descriptions and invalidate affected resident generations. Do not independently reimport the external project once per cell or let runtime unloading write back into the Quader file. Existing scenes and imported package generations must remain readable.

Reference: Unreal's `UWorldPartitionStreamingPolicy::UpdateStreamingSources` and `UpdateStreamingState`, plus persistent `FWorldPartitionActorDesc::GetGuid`. Godot's threaded resource-loading functions are useful loading references but are not themselves a world-partition system.

### Collision and navigation are separate readiness gates

`CollisionResources` prepares original LOD0 triangles, and `PhysicsWorld` publishes accepted shape versions. Continue using those accepted snapshots. Prepare replacement collision before retiring the previous body; keep necessary collision ahead of the player even when the corresponding mesh is offscreen. Failed visual streaming must not create a walk-through gap.

`NavigationMesh::reconcile` currently performs one synchronous Recast build from accepted boxes/meshes. The four-million global horizontal-cell limit can be exceeded by extent even with few triangles; the sixteen-million cumulative box rasterization limit is another guard. Raising those limits is not a streaming design.

Introduce bounded asynchronous navigation tile generation, safe main-thread Detour publication and generation validation. Define tile borders, cross-cell links and route validity explicitly. A route must pin or validate the collision/navigation corridor it uses; unloaded destinations require a staged route/activation policy. Resource/cell changes should invalidate or replan the affected route without rebuilding the entire world every frame. Workers must not mutate live Jolt, Detour, scene documents or registries.

**Acceptance:** cross-cell parents; repeated traversal and reversals; teleports; failed/incomplete collision; stale completions; source refresh during load; Undo and project reopen with unloaded cells; boundary-crossing paths; reflected/sheared collision; removing a cell behind an actor; no lost authored references. Measure cell activation and nav-tile p95/p99 latency, not only steady draw time.

## 15. Verification and delivery checklist for the implementation agent

### Before editing

- Confirm the requested slice and current architecture status. Use this guide's source findings; recheck only changed dependencies/assumptions.
- Inspect working-tree changes and preserve user assets, deleted scenes and `crimson_ui.ini`.
- Establish the new frozen baseline and save measurements before changing runtime paths.
- Write down the intended memory/CPU bound and any quality tradeoff. Choose an acceptance workload that actually exercises it.

### During implementation

- Keep CPU workers detached from bgfx/ImGui/documents. Publish complete generations on the main thread.
- Preserve stable IDs, affine hierarchy, existing material factors/assignments, source-role decoding and linear HDR/final encoding.
- Preserve submitted-frame leases, pending/retired accounting and shutdown ordering.
- Keep new metrics scoped to diagnosis; do not add unsolicited panel explanations, themes or unrelated UI.
- Retain a correctness oracle or reference images while replacing traversal/allocation behavior. Do not benchmark fallback/unfinished output as a success.

### Build and focused checks

Use the normal root PowerShell workflow:

```powershell
.\Build.cmd Release
ctest --test-dir build/Release --output-on-failure -R '^(scene_core|scene_extraction_core|point_shadow_math_core|directional_shadow_math_core|material_graph_publication_core|gpu_streaming_core)$'
```

That expression is an example for spatial/shadow work, not a mandatory exhaustive suite. Add new allocator/index/budget tests and select the existing checks affected by the slice. Texture compression needs streaming/material mapping/shader tests; world activation needs Quader, collision, navigation and Play checks. Use focused D3D readback tests when shader/allocation correctness warrants them; do not run routine native capture suites for unrelated changes.

Existing regression anchors include `point_shadow_math_tests.cpp`, `directional_shadow_math_tests.cpp`, `point_shadow_render_tests.cpp`, `material_mapping_render_tests.cpp`, `material_graph_publication_tests.cpp`, `material_graph_shader_tests.cpp`, `gpu_streaming_tests.cpp`, `streaming_tests.cpp`, `import_identity_tests.cpp`, `shader_compiler_tests.cpp`, `preview_dependencies_tests.cpp`, `scene_extraction_tests.cpp` and `game_metrics_tests.cpp`.

Close the running renderer before replacing its executable. Ask the user only when implementation is ready for the integration build, following AGENTS.md. Use one integration build after implementation owners finish; repeat when failures or new fixes justify it. Documentation-only changes require link/consistency checks, not a build. Keep `docs/VERIFICATION.md` historical.

### Before claiming completion

Deliver the implemented behavior and exact remaining limitations, the matched before/after table, raw result/fixture locations, source/build identities, allocation peaks, and image/detail evidence. Distinguish CPU work from waits, GPU query frames from CPU samples, renderer-only from full-editor results, and derived-cache cold from machine-cold.

Update `ENGINE_ARCHITECTURE.md` current-versus-target status and README controls only for features that actually landed. Update this guide's status tables so the next agent does not implement a completed stage again. Leave interactive feature acceptance to the user. Do not state large-ARPG readiness from a single room or a short average-FPS sample.
