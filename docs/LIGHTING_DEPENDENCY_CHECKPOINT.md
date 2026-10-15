# Lighting dependency checkpoint

Historical Step 3 evidence: the IBL-only composition and nested reveal-light settings
described here were subsequently replaced. Current character Key/Fill, local World
omni and Key silhouette have separate roles, with a separate player darkness range.
See [the current rig and shared darkness
contract](D2R_CHARACTER_LIGHTING_FINDINGS.md#implemented-crimson-contract) and
[the architecture](ENGINE_ARCHITECTURE.md#lighting-dependencies-scopes-and-presented-player-rig)
for current behavior. There is one current rig implementation and no selectable legacy mode.

Recorded 15 October 2026. This is renderer Step 3: coverage-correct readable
depth, camera-local light assignment, ambient visibility, scoped player lighting
and ambient composition. It extends the immutable frame/pass contracts from
[Step 1](FRAME_ARCHITECTURE_CHECKPOINT.md) and the bounded world/character/local
shadow ownership from [Step 2](SHADOW_ARCHITECTURE_CHECKPOINT.md). Preserve the
[bright-surface shadow correction](SHADOW_QUALITY_CORRECTION.md) when changing
lighting or depth; dark dungeon images alone are insufficient quality evidence.

## Status and preservation boundary

Implementation, the Release build and focused checks are complete; user
acceptance remains pending. The evidence sections below retain actual test
outcomes, matched captures and before/after measurements.

Ambient Visibility and the Player Light Rig start disabled, including in older
scene files. All light direct/caster masks start at World + Character. This
keeps the existing BRDF, direct-light response, material coverage, environment,
fog exclusions, shared sorted transparency, Glow, tone mapping, independent
construction grid and one final sRGB encoding. Existing point lights default
to eligible ambient reveal; directional lights do not. That role has no visual
effect while Ambient Visibility is disabled.

The optional readable depth is a separate producer. The color pass keeps its
own existing depth attachment and `LESS` behavior, including coplanar ties;
enabling a prepass does not switch color to `EQUAL`/`LEQUAL` or change which
coincident surface wins the visible color pass.

## Implemented resource and execution contracts

`RenderPassPlan` declares real `DepthPrepass`, `AmbientVisibility` and
`AmbientPublish` work when the authored feature is enabled for Scene, Game or
a saved-scene thumbnail. Isolated material/particle previews and model/material
thumbnails retain their separate studio lighting. Physical bgfx view order
remains numeric and identity-mapped for the pinned D3D11 backend.

The dependencies join as follows:

1. The frozen world supplies models, exact material/texture versions and clocks,
   particle packets, source lights, scopes and world shadow context.
2. The existing conservative sphere/cluster assignment produces camera-local
   point records and masks. It remains valid for surfaces at every depth,
   including blended meshes and lit particles.
3. The optional prepass produces sampleable D24S8 depth and an RGBA8 receiver
   target containing ambient category plus an encoded geometric receiver
   normal. Opaque/masked coverage uses the corresponding material evaluation,
   texture mapping, cutoff, vertex alpha, transforms, winding and frozen clocks.
   Blended surfaces do not overwrite the opaque receiver depth.
4. Ambient evaluation reads those detached targets, clustered sources and
   available shadow maps, reconstructs world position, and writes R32F scratch.
5. A separate publication dispatch packs four visibility values into each
   RGBA32F texel in a reserved tail of the existing BRDF/cluster atlas.
6. Opaque lighting reads published visibility through its existing stage-14
   atlas binding. Blended surfaces evaluate the same response at their own
   world position; an opaque pixel behind glass cannot define that glass's
   ambient visibility.
7. The resulting multiplier affects diffuse/specular environment lighting.
   Direct light, emission, unlit response, fog radiance and background remain
   separate. Existing post-processing follows the HDR scene color as before.

Light assignment does not currently read opaque min/max depth. It is an
independent producer consumed by ambient evaluation and surface lighting.
Opaque-only depth pruning would require its own lists or another conservative
transparent route; pruning the shared list would incorrectly remove lights
from transparent surfaces outside the opaque depth interval.

`AmbientVisibilityGpu` belongs to each concrete viewport target. Scene, Game
and thumbnail scratch/atlas regions never share mutable camera-local storage.
Evaluation samples the atlas while writing a different scratch texture;
publication then reads scratch while writing the atlas. This avoids binding
one texture simultaneously as a sampled resource and writable image. Resource
replacements retire through their GPU owners, and owners die before bgfx.

The material sampler budget stays at twelve material textures, environment
cubes in slots 12–13, combined BRDF/light/mask/visibility data in slot 14 and
the shadow comparison atlas in slot 15. Ambient visibility adds no material
sampler. Its dedicated compute programs have their own bindings.

First-use extra target storage is approximately 16 bytes per viewport pixel:
4 bytes readable depth, 4 bytes receiver data, 4 bytes scratch and 4 bytes
packed visibility. Atlas row padding, retained/replaced allocations and
existing targets are additional. At 909 × 684 this is about 9.49 MiB per view;
at 1920 × 1080 about 31.64 MiB. Initially disabled views do not create these
extra image targets or dispatch their optional passes. A previously enabled
view can retain its reusable allocations until resizing/releasing its owner.

## Ambient response and occlusion policy

For a receiver's ambient mask, each enabled reveal source contributes scalar
energy. Point sources use the same finite-radius attenuation as direct point
lighting, multiplied by source intensity and Reveal Strength. Directional
sources use their intensity and Reveal Strength. RGB tint does not control
this scalar visibility; the dedicated player reveal packet is deliberately
black and has no direct receivers.

The shared CPU/shader response is:

```text
t = clamp((revealEnergy - threshold) / transition, 0, 1)
response = t*t*(3 - 2*t)
visibility = lerp(1, lerp(darknessFloor, 1, response), strength)
finalAmbient = existingAmbient * visibility
```

Global Strength is in [0,1]; zero preserves the previous ambient exactly.
Darkness Floor is the minimum visibility at full strength, also in [0,1].
Reveal Threshold starts at 0.1, Transition at 0.25 and spans a positive energy
interval after the threshold. Increasing threshold suppresses weak reveal;
increasing transition makes its brightening more gradual. This is artistic
ambient attenuation, independent of authored material occlusion and distinct
from SSAO, indirect light transport or gameplay fog of war.

Respect Shadows starts enabled. It multiplies a shadow-casting reveal source's
contribution by its available shadow visibility, using the independent caster
mask. Turning it off gives a deliberate radius/energy reveal that can cross
walls. The renderer's sixteen resident point-shadow influences and four
six-face updates per world frame still apply: a light without a usable map
cannot promise wall occlusion. Enabling a reveal source does not create an
unlimited second shadow budget. Directional reveal is global by nature and
starts off; enabling it can brighten ambient across the whole lit scene.

## Authored scopes, player rig and workflow

`LightingSettings.h` contains concrete value types shared by scene/render data.
World is bit 1, Character bit 2, All is 3, and zero selects neither. Every model
has independent direct-receiver, shadow-caster and ambient-receiver masks.
Every light has independent direct-receiver, shadow-caster and ambient-reveal
masks, plus Reveal Ambient and Reveal Strength. Direct exclusion never removes
a caster or disables ambient participation implicitly.
An empty model ambient-receiver mask opts out of ambient modulation and retains
its original IBL. A receiver with an eligible mask but no contributing reveal
sources still follows the configured darkness floor and global strength.

Character currently means the designated Player Character's complete model
subtree. Other models, including other characters, belong to World. Extraction
intersects each authored surface mask with that one category; the player does
not retain the World bit. A World-only light therefore excludes the hero.
This slice does not introduce arbitrary named lighting layers.

The Inspector's collapsed Advanced Lighting groups contain the independent
light roles and model scopes. Existing Cast Shadows/Receive Shadows retain
their meaning. Select the Player Character and edit Character → Light Rig;
global Ambient Darkness appears only on the Environment. The rig stays in
`SceneCharacter::lightRig`, and all edits use the existing Character Inspector
transaction owner. The former separate Game Settings panel has been removed.
No source values or saved hidden contributions are normalized on opening.

The disabled-by-default rig contains:

| Value | Key | Fill | Dedicated ambient reveal |
| --- | --- | --- | --- |
| Enabled within rig | Yes | No | No |
| World offset (m) | (-3,5,3) | (3,3,-2) | Player origin |
| RGB | (1,0.96,0.9) | (0.55,0.65,1) | Black |
| Intensity | 35 | 12 | 1, with no direct receivers |
| Radius | 12 m | 8 m | 8 m |
| Direct receivers | World + Character | Character | None |
| Cast shadows | Yes | No | Yes |
| Ambient reveal | No | No | Yes |

Key/fill controls include enabled state, world offset, color, intensity, radius,
shadow toggle and Affect choice. Biases, caster masks and saved custom ambient
contributions live under Advanced Lighting. One Ambient Light group exposes
the dedicated source's enabled state, radius and strength. Selecting the player
or its model subtree shows the active rig in the freely navigable Scene view.
Show Rig also displays it when unselected and in Game. The shared passive
overlay draws clipped links, labeled source markers and range rings from the
presented pose, without creating scene entities or taking navigation input.

The evaluator consumes only the interpolated/presented player world position.
Offsets remain in world axes: character turning cannot rotate the light rig,
and parent scale cannot multiply its range or offset. The camera does not
rotate the offsets either. Scene and Game freeze the same evaluated light
packets. Temporary identities deterministically avoid authored light IDs.
Up to three enabled rig packets count against the same 4,096-source capacity;
an over-budget scene edit is rejected rather than truncating lights.

Current authoring workflow:

1. Select the active Player Character, then open Character → Light Rig.
2. Enable the rig and tune Key Light/Fill Light using the freely navigable Scene camera and automatic selected-rig overlay.
3. Use one Character Ambient Light group for the dedicated ambient source.
4. Adjust global Environment → Ambient Darkness only when its IBL response is needed. Darkness Floor and Softness are the normal controls; detailed response and scope settings stay collapsed.

These are the current point-rig controls, not the complete D2R character-lighting
model. A world-affecting moving point can create wall discs and turn pillar
shadows. [The subsequent primary-source findings](D2R_CHARACTER_LIGHTING_FINDINGS.md)
establish D2R's separate directional actor rigs and broader attenuation policy.
A Character-only readability light cannot darken a floor it does not illuminate;
the future replacement must explicitly define its ground-shadow contribution.

Scene storage keeps optional compatible fields in version 9. Missing roles,
scopes, rig or ambient settings use their documented defaults. Validation
rejects invalid masks, nonfinite/out-of-range values and invalid stored fields
even while disabled. Semantic document patches include scopes and nested rig
settings. Live Play edits use the transient document: undo/cancel preserves
runtime poses, and Stop discards Play-only tuning like other scene edits.
Matching Quader source identities retain their Crimson-only model scopes and
same-kind light roles across reimport. Source geometry, transforms, light
energy/range and shadow flags continue following the existing sync policy;
new or changed-kind sources receive their normal role defaults.

## Focused verification and reproduction

Close the running renderer only when implementation is ready for its integration
build. Use `Build.cmd Release`, then focused checks from the project root:

```powershell
ctest --test-dir build/Release -C Release --output-on-failure -R '^(player_light_rig|ambient_visibility|scene_gameplay|scene_extraction|quader_scene|render_pass_plan|editor_render_frame)_core$'
ctest --test-dir build/Release -C Release --output-on-failure -R '^lighting_dependency_render_core$'
```

The second command requires an isolated GPU interval and uses the hidden
diagnostic renderer, rather than controlling the user's editor. Do not run it
concurrently with another GPU workload or mistake it for feature acceptance.

CPU coverage exercises rig evaluation, collision-safe identities, settings
validation, independent role transport, smooth bounded ambient response,
odd-sized atlas packing, legacy/current storage, semantic Play history,
presented-space extraction and pass/resource ownership. The maintained GPU
fixture covers point/directional World/Character direct scopes, role-only
record invalidation, pure-IBL darkness/floor/strength/off behavior, odd 129 × 97
publication, black reveal-only sources with independent ambient masks,
shadow-aware reveal and caster-mask filtering, fallback Alpha meshes and lit
Alpha particles at their own world positions, generated opaque/Alpha materials,
Time-linked masked coverage, and Scene/Game depth/settings isolation. The final
coverage extension also exercises fallback Opaque/Mask and generated Mask
particles through the real prepass, with matching clocks and a shared instance
allocation. Each particle capture retains exactly one requested/submitted
particle and one visible draw, with no invalid/dropped entries. Resize/toggle
checks cover 133 × 101 and 65 × 49 in addition to the initial odd extent.

| Check | Result |
| --- | --- |
| Incremental integration build | Passed, Release integration build on 15 October 2026 |
| Focused CPU checks | Passed, 17 unique checks; final affected 11/11 rerun in 57.78 s, plus six unaffected earlier passes; `focused-cpu-tests.log` and `final-cpu.log` under the results directory |
| Lighting dependency GPU checks | Passed, including final particle extension in 5.81 s; `lighting-particle-coverage.log` (verbose numeric results) and `final-gpu-tests.log` |
| Existing Forward+/shadow regressions | Seven GPU scenarios ultimately passed, including lighting; initial material-mapping invocation lacked the previously deleted `smoke_07.png`, then the same binary passed against an isolated fixture containing the byte-identical tracked image; `final-gpu-tests.log` and `material-mapping-isolated.log` |
| Default-disabled visual comparisons | Eight of eight PNGs exact RGBA matches; `comparison-default.json` |

The later authoring correction removes the separate rig panel, moves properties
into the Character Inspector, exposes the selected rig in Scene through a shared
passive overlay, and consolidates global darkness controls on Environment.
Its Release integration build and 11 focused CPU checks passed. Logs are under
`build/verification/lighting-workflow-correction`. It changes no lighting
equations, stored rig values, material shaders or source-light defaults; the
measurements below identify the original renderer checkpoint, not an additional
performance claim for editor overlays or an implemented directional-rig replacement.

The final lighting fixture measures original IBL at 73 display-code units,
unrevealed IBL at 0 and a 0.25 Darkness Floor at 35. A shadowed reveal sample
changes from 73 through an unoccluded path to 0 behind an eligible blocker,
then returns to 73 when its caster scope excludes that blocker. Independent
direct, emission, Unlit and fog samples remain unchanged when ambient toggles.
These are fixture assertions, not authored appearance presets.

The first build stopped on an HLSL reserved identifier in the new shaders;
the source was corrected before the successful integration build. The final
particle-only rebuild changed only the diagnostic test, leaving the measured
renderer and benchmark hashes below unchanged. Its source, executable and
identity record are retained in `renderer-step3-results/particle-coverage-evidence`.

## Before/after performance evidence

The baseline is the completed directional-shadow correction, frozen before
Step 3 source edits in `build/verification/renderer-step3-baseline`. It retains
copied executables/DLLs, production benchmark objects and libraries, complete
sources/shaders/tests, the pinned shader compiler/include tree and the saved
managed project. `baseline-identity.json`, `fixture-identity.json` and
`sha256-files.json` record provenance and 4,481 file identities. Git HEAD alone
does not reproduce this build because existing uncommitted work was present.

| Baseline artifact | SHA256 |
| --- | --- |
| Release renderer | `0250C91292A0D680B4E4E146DF090A4513B75181559EAFDC08CE9D8B8C69B049` |
| Production benchmark | `F618E4A84B6AAA8DBA82D9E872D137437FC510D8B73CD48B8B6F518463AED843` |
| Saved combined scene | `3EC8B9607CD3F735284175ACA2A53A4931692BEE8A6D6BBEED114B07F1FC0B9E` |

The frozen saved scene contains ten models, eight enabled shadowed point lights
and one shadowed directional light. It already reflects the user's eight-light
edits; it is not the earlier scene without point lights. The source scene's hash
was stable before, during and after copying. The retained project is never
modified by measurement; the harness copies it into its own disposable project.

The production benchmark renders hidden D3D11 Scene and Game targets, each
909 by 684, with vsync off and an immediate-context Flush after submission. It
uses `prepareEditorRenderFrame` and `Renderer::renderFrame`, the same captured
material clock zero, resource readiness and at least 300 warmup frames followed
by 2,000 measured frames per phase. Four phases cover authored non-Quader
records, the full room with shadows disabled, static cached shadows, and the
player subtree plus following Game camera moving through a deterministic
sixty-frame translation sequence. The native Quader arguments supply metadata
in `--saved` mode; no native reimport determines this saved workload.

Eight PNGs replay the last measured pose after timing. Their readback frames
are excluded. CSVs retain 8,000 measured rows per run. The diagnostic excludes
editor widgets/browser work, Quader watching, gameplay physics/AI and animated
crowds. GPU timestamps are asynchronous phase samples; they are not synchronized
to the corresponding CPU row.

The old baseline ran after the user closed Crimson and before integration was
built. No other team diagnostic used the GPU. Desktop activity and CPU/GPU
clocks remain uncontrolled. The RTX 4090 used driver 610.74; point snapshots
before/after reported P8 at 210/405 MHz and P0 at 2,265/10,501 MHz graphics/memory.
These observations are not a fixed-clock experiment.

| Phase | Before wall | Default wall | Before wall p95 | Default wall p95 | Before GPU | Default GPU |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Non-Quader records | 0.3075 ms | 0.2929 ms | 0.4490 ms | 0.3855 ms | 0.2131 ms | 0.1708 ms |
| Full room, shadows off | 0.5823 ms | 0.4498 ms | 0.8805 ms | 0.5123 ms | 0.1972 ms | 0.2053 ms |
| Full room, cached shadows | 0.9533 ms | 0.7211 ms | 1.3585 ms | 0.7902 ms | 0.2973 ms | 0.2269 ms |
| Moving player/camera | 1.4427 ms | 1.0741 ms | 1.8529 ms | 1.2932 ms | 0.6965 ms | 0.5583 ms |

Static warmed point shadows have eight cache hits per frame and no depth writes
or deferred work. The moving phase writes 24 point faces per frame, retains four
cached lights and has zero deferred/critical work. Shadow allocation is
253,628,160 bytes. Full-room shadowed phases retain 1,349,016 model bytes and
5,836,224 texture/program bytes.

The candidate's default-disabled run uses the same frozen project/settings and
sequence for preservation. A separate candidate `--ambient` run enables only
Ambient Visibility, measuring its new depth/evaluate/publish work and retained
storage. This fixture's environment lighting intensity is zero, so that run
establishes enabled-pass cost rather than demonstrating ambient darkening; the
dedicated lighting GPU fixture checks the visible response with environment
lighting. The benchmark does not enable the Player Light Rig or simulate its
full gameplay overhead.

Results live under `build/verification/renderer-step3-results`. Reproduce the
old baseline from the repository root in PowerShell:

```powershell
$step3Base = (Resolve-Path 'build/verification/renderer-step3-baseline').Path
$step3Native = (Resolve-Path 'build/verification/frame-architecture-baseline/frozen-native/q-test/q-test.qproj').Path
& (Join-Path $step3Base 'bin/forward_plus_room_benchmark.exe') `
  (Join-Path $step3Base 'project') $step3Native `
  '6aeb7097-5046-450e-a451-a8b4518aa095' `
  (Join-Path $step3Base 'shader-tools/shaderc.exe') `
  (Join-Path $step3Base 'shaders') (Join-Path $step3Base 'bgfx-includes') `
  --saved --warmup 300 --samples 2000 --capture `
  --output 'build/verification/renderer-step3-results/reproduction-before'
```

For the corrected candidate use its retained executable and shader/compiler
paths, keeping `renderer-step3-baseline/project` and the same native metadata
arguments. Choose separate output directories for default-disabled and
`--ambient` runs. The candidate reports `gpu_depth_views_ms`,
`gpu_ambient_views_ms` and `environment_bytes`; the latter includes cluster,
prepass and scratch owners. Directional GPU timing excludes the new pass slots.

```powershell
$step3Candidate = (Resolve-Path 'build/verification/renderer-step3-candidate').Path
$step3Arguments = @(
  (Join-Path $step3Base 'project'), $step3Native,
  '6aeb7097-5046-450e-a451-a8b4518aa095',
  (Join-Path $step3Candidate 'shader-tools/shaderc.exe'),
  (Join-Path $step3Candidate 'shaders'),
  (Join-Path $step3Candidate 'bgfx-includes'),
  '--saved', '--warmup', '300', '--samples', '2000', '--capture'
)
& (Join-Path $step3Candidate 'bin/forward_plus_room_benchmark.exe') `
  @step3Arguments --output 'build/verification/renderer-step3-results/reproduction-default'
& (Join-Path $step3Candidate 'bin/forward_plus_room_benchmark.exe') `
  @step3Arguments --ambient --output 'build/verification/renderer-step3-results/reproduction-ambient'
```

The completed candidate is retained under
`build/verification/renderer-step3-candidate`, with `candidate-identity.json`
and `sha256-files.json`, complete compiled sources and copied production
objects/libraries. Its Release renderer build time is 16:32:44 local time.

| Candidate artifact | SHA256 |
| --- | --- |
| Release renderer | `F40E0BFA50094FBB6DF27E5EDF367C10DCE6B7DD5A2D8DDD1B52AC5AF736CC41` |
| Production benchmark | `4A2EDC816EE887DAA8319B82F871B74617FAA124A3D5DA2876CCD899899DB8B3` |

The default run preserves all eight PNGs exactly, with zero changed RGBA
channels across every phase/view pair. Shadow writes, cache hits, model
receivers and draw counts also match the baseline. No depth or ambient view
executes in the disabled profile. Full-room model and shadow allocations remain
1,349,016 and 253,628,160 bytes. Tracked texture/program bytes rise from
5,836,224 to 5,982,340 because the extended generated programs are larger.

The default timings above are observations, not an established optimization.
Even the shadows-off control has a 22.75% lower CPU wall mean while its GPU
mean rises 4.09%. CPU preparation time changes across stages that this feature
does not optimize. Uncontrolled clocks and background activity prevent
attributing the lower cached/moving times to this implementation. The exact
images and unchanged submitted work establish preservation more directly.

The same candidate with `--ambient` produces the following separate cost
observations:

| Phase | Enabled wall mean | Enabled wall p95 | Enabled GPU mean | New depth views | New ambient views |
| --- | ---: | ---: | ---: | ---: | ---: |
| Non-Quader records | 0.3477 ms | 0.4356 ms | 0.2108 ms | 0.0059 ms | 0.0423 ms |
| Full room, shadows off | 0.5445 ms | 0.6649 ms | 0.3197 ms | 0.0139 ms | 0.0627 ms |
| Full room, cached shadows | 0.8048 ms | 0.9301 ms | 0.3655 ms | 0.0128 ms | 0.0811 ms |
| Moving player/camera | 1.0426 ms | 1.2549 ms | 0.6284 ms | 0.0105 ms | 0.0838 ms |

Depth plus ambient evaluation/publication account for approximately 0.094 ms
of sampled GPU work in the two shadowed full-room phases. Their total GPU
means rise by 0.1387 and 0.0701 ms relative to the default candidate. The
cached CPU wall mean rises by 0.0837 ms; the moving CPU mean falls despite
additional work, illustrating the remaining measurement variability. These
values characterize this small fixture on this adapter and do not establish
large-world or ARPG gameplay performance.

The enabled full-room phases add seventeen depth draws, from 23 to 40 static
draws and from 140.966 to 157.966 mean moving draws. Shadow draws stay at zero
static and 93.966 moving, with the same 24 point faces and zero deferred/critical
work. The environment/resource-owner total is 1,565,568 bytes disabled and
21,452,064 bytes enabled, an additional 19,886,496 bytes (approximately
18.965 MiB) for the two viewports' readable depth/scopes, R32F scratch and
packed atlas region/padding. This is retained logical owner accounting, not
total driver or peak process VRAM. Shadow, managed texture/program and model
allocations are identical between these two candidate modes.

All eight enabled-run PNGs also match the default candidate exactly, as
expected with this scene's zero environment-lighting intensity. This confirms
that the optional passes do not change its direct lighting or shadows.
It does not demonstrate darkening in a lit-IBL scene; the dedicated
GPU fixture supplies that acceptance coverage. Static Game and Scene captures
were also visually inspected and retain the bright-surface shadow repair.

The default run's pre/post GPU snapshots were P8 at 240/405 MHz and P0 at
2,205/10,501 MHz; the enabled run's were P8 at 225/405 MHz and P0 at
2,685/10,501 MHz. Crimson remained closed, no other team GPU job ran, and no
authored data or UI was controlled during these measurements.

The retained runs are
[`before-saved`](../build/verification/renderer-step3-results/before-saved/room-summary.json),
[`after-default`](../build/verification/renderer-step3-results/after-default/room-summary.json)
and [`after-ambient`](../build/verification/renderer-step3-results/after-ambient/room-summary.json).
Each directory contains raw `room-frames.csv` and eight PNGs.
[`comparison-default.json`](../build/verification/renderer-step3-results/comparison-default.json)
and [`comparison-ambient.json`](../build/verification/renderer-step3-results/comparison-ambient.json)
retain decoded image hashes/differences and CPU/GPU percentiles, generated by
`scripts/Compare-FrameArchitecture.py`. Candidate summaries/CSVs additionally
record depth/ambient query and environment-owner measurements.

## Follow-up: transparency, occluder fading and scaling

Step 4 remains deliberate transparency/ghost composition, shared post and scene
antialiasing. Automatic occluder fading is a separate gameplay policy. It needs
an explicit camera-to-player obstruction query, eligible wall/roof classes,
stable entity identities, hysteresis and fade recovery, followed by immutable
per-view visual packets. The renderer then needs a dedicated ghost path with
frontmost-surface depth/coverage semantics, controlled opacity and defined
shadow/picking behavior. A camera blocking one view must not mutate a shared
material or fade the wall in another view accidentally.

Use the readable-depth/material coverage foundation when implementing that
path, but do not treat its existence as an implemented obstruction query,
automatic wall fading, OIT or gameplay visibility system. Preserve ordinary
sorted transparency until that slice has its own quality/performance evidence.
The [large-scene implementation guide](LARGE_SCENE_PERFORMANCE_GUIDE.md) remains
the scaling reference for spatial indices, resource/geometry streaming and
broader scene submission work; this step does not replace those systems.
