# ARPG shadow architecture checkpoint

Recorded 15 October 2026. This checkpoint covers the second renderer migration
step: bounded stabilized Game directional coverage, focused character shadow
detail and budgeted local-light shadow updates. The fixed gameplay camera is
the primary design constraint. Scene inspection retains its own appropriate
coverage policy. Ambient attenuation, player light rigs, lighting channels,
new transparency composition and scene antialiasing remain separate steps.

For the frame-input and pass-planning foundation, see
[the frame architecture checkpoint](FRAME_ARCHITECTURE_CHECKPOINT.md).

The candidate recorded below subsequently failed bright, plain-surface quality
acceptance: oblique directional lights produced widespread acne in both views,
and the single Game projection spent resolution on empty frustum space. The
dark-room comparisons below did not establish correctness for those cases.
[The shadow quality correction](SHADOW_QUALITY_CORRECTION.md) records the causes,
repair and stronger regression checks. Retained historical measurements and
binary identities below describe the original Step 2 candidate.

## Retained baseline

The baseline is the completed Step 1 Release build, retained before shadow
implementation changes:

- Renderer SHA256:
  `3428B124FC1DE1BA48469047E1DCD8D931E2107595440BF50545FA592E8AF130`
- Production benchmark SHA256:
  `22F5B9A4B0FFC1348C9E229FBD4AC8A79A5889CD7E25864586FEAF1DFEA8BF36`
- Renderer build time: 15 October 2026, 13:52:37 local time.
- Snapshot: `build/verification/shadow-architecture-baseline/`.

The snapshot includes the application/benchmark executables, production object
files and libraries, source, shader source, pinned compiler/includes and native
reader. `baseline-identity.json` and `sha256-files.json` identify the actual
artifacts. The repository has extensive existing uncommitted work; Git HEAD
alone does not reproduce this build.

The diagnostic uses the retained managed project at
`build/verification/frame-architecture-baseline/engine-original86` and native
Quader fixture at `frame-architecture-baseline/frozen-native/q-test`. It never
reads or modifies the current authored assets for measurement. Managed sources
and derived caches are copied into a temporary project for each run.

## Workload and measurement

The maintained `forward_plus_room_benchmark` exercises production
`prepareEditorRenderFrame` and `Renderer::renderFrame`. It renders hidden D3D11
Scene and Game targets, each 909 × 684, with vsync disabled and an explicit
immediate-context Flush after submission. Only this diagnostic owned the GPU
during its run; desktop background activity and GPU clocks are uncontrolled.

The frozen combined scene contains 86 model instances, fourteen point lights
and one directional light. The phases are:

1. Authored non-Quader Main records.
2. Combined room with all shadow flags disabled.
3. Combined room with static, fully prepared cached shadows.
4. The same room, moving the player subtree and following Game camera through
   a deterministic sixty-frame horizontal translation sequence.

Each phase waits for resource readiness and at least 300 warmup frames, then
records 2,000 frames. Captures replay sample 1,999 after timing; the image
readback frames are excluded. The fixture's twenty material graphs contain no
Time or Panner nodes, and the submitted material clock is fixed at zero.

This is a renderer/resource workload. It excludes editor widgets, browser work,
Quader watching, Play physics/AI and animated crowds. Wall time includes
resource pumping, shadow/receiver preparation, frame construction, submission
and `bgfx::frame()` backpressure. GPU results are asynchronous phase samples;
individual CPU/GPU CSV rows are not synchronized measurements of one frame.

## Before measurements

The NVIDIA GeForce RTX 4090 used driver 610.74. Before the run it reported P8,
405 MHz graphics/405 MHz memory and 16% utilization; afterward P5,
705 MHz graphics/810 MHz memory and 9% utilization. These snapshots do not
represent controlled clocks throughout the run.

All values below are phase means except the explicitly labeled wall p95.

| Phase | Wall mean | Wall p95 | GPU mean | Shadow preparation | Receiver preparation | Renderer submission |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Main | 0.7557 ms | 0.8291 ms | 0.2890 ms | 0.2065 ms | 0.3103 ms | 0.1005 ms |
| Room, shadows off | 2.1648 ms | 2.4914 ms | 0.6114 ms | 0.0001 ms | 1.7118 ms | 0.1477 ms |
| Room, cached shadows | 5.5393 ms | 5.9125 ms | 1.1672 ms | 2.1035 ms | 1.7187 ms | 1.3573 ms |
| Room, moving player/camera | 6.8302 ms | 7.4334 ms | 2.1310 ms | 2.0278 ms | 1.6789 ms | 1.9428 ms |

| Phase | Draws | Shadow draws | Point faces written | Directional projections written | Shadow bytes | Texture/program bytes | Model bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Main | 34 | 0 | 0 | 0 | 101,451,264 | 10,761,674 | 1,289,016 |
| Room, shadows off | 101 | 0 | 0 | 0 | 0 | 13,794,830 | 1,476,948 |
| Room, cached shadows | 101 | 0 | 0 | 0 | 507,256,320 | 41,101,494 | 2,045,094 |
| Room, moving player/camera | 1,028.089 | 888 | 36 | 8 | 507,256,320 | 41,101,494 | 2,045,094 |

The baseline's static phase asserts that fully prepared cached shadows are not
rewritten. The room's shadow allocation is approximately 483.76 MiB and is
dominated by existing point-shadow storage. A directional projection-count
reduction alone must not be presented as fixing the complete shadow allocation.

All eight fresh baseline captures, Scene/Game for every phase, exactly match
the completed Step 1 candidate in decoded RGBA bytes. The comparison is retained
as `comparison-step1-baseline.json`. This confirms that the shadow migration
starts from the previously verified image.

## Candidate implementation and results

The final Release build succeeded, including the measured metadata correction
described below. The candidate is retained at
`build/verification/shadow-architecture-candidate-final/`, with binary/source
identity in `candidate-identity.json` and `sha256-files.json`. Its renderer
SHA256 is:

`41DB0BD0DF45ECE0CF04C983DF2CDB67CB8F7DE0525AAB514DA82E951B0A1F4E`

Implemented behavior:

- Game uses one stabilized world projection per shadowed directional light.
  Valid assigned-player caster bounds enable one additional focused character
  projection for that light.
- The focused subtree is excluded from the Game world map while its valid
  focus map supplies those shadows. Without valid focus coverage, world
  casting includes it again. World/focus map contents, matrices and cache
  signatures are independent, so a character-only pose edit with a stationary
  camera does not rewrite the Game world map.
- Each dirty directional tile is cleared explicitly without clearing its
  cached neighbors. These clear submissions are included in shadow draw counts.
- Active Scene/Game directional blocks use their actual four-plus-two tile
  policy instead of reserving two four-tile cascade blocks. Point-light
  residency remains sixteen slots. Isolated scene thumbnails retain their
  separate shadow owner and update their admitted set completely for a capture,
  without consuming the active world's per-frame update budget.
- Spatial admission chooses sixteen eligible point shadow lights using one
  frozen world-frame origin and squared distance normalized by squared radius.
  Selection is shared by resource preparation, GPU lighting mapping and shadow
  rendering; stable identities resolve ties. Additional lights still illuminate.

The benchmark supplies the player's three subtree IDs, including one managed
demon mesh with Cast Shadows enabled. The mesh and Game camera translate;
frozen light positions remain unchanged, matching the baseline motion workload.
The demon is a static mesh in this diagnostic, not an animated crowd. Enabling
the new focus path is an intentional difference from the baseline workload.

The local-shadow update budget is separate from residency and illumination:
four complete six-face maps may update in one active-world frame, while sixteen
point-shadow allocations can remain resident and up to 4,096 point lights can
illuminate through Forward+. Scene/Game share that update decision. A deferred
caster-only edit can use the previous coherent light projection temporarily;
a newly allocated or moved light must remain unshadowed until its complete map
is submitted. This prevents stale projection sampling and partial cube faces.

### Measured correction during implementation

The initial candidate reduced GPU work and allocation but introduced repeated
CPU preparation for each directional projection. Measurements detected this:
in the back-to-back B bracket, cached-shadow renderer submission increased
from 1.3600 to 1.6978 ms, and cached-shadow wall time from 5.4352 to 6.3490 ms.
The initial A and B runs, images, binaries and source are retained separately;
they are not replaced by final results.

The correction builds common caster bounds, primitive transforms, material
lookup and coverage metadata once per `renderShadows` call for all eligible
point/directional targets. Each target retains its own intersection, LOD,
signature, draw list and exact-version lease. This is a shared preparation
change, not a cross-frame cache that could silently retain stale caster data.
Independent review and the point-shadow/Forward+ GPU checks passed afterward.
All eight final captures exactly match the initial candidate's decoded RGBA.

### Final timing comparison

All entries are means over 2,000 frames per phase. The retained baseline B was
followed immediately by the initial candidate B to investigate the regression;
the final candidate was measured after the correction/build/focused checks.
There is no clock lock or randomized run order, and the final run began at P8,
420 MHz graphics/405 MHz memory with 20% background device utilization and
ended at P3, 930 MHz graphics/5,001 MHz memory with 12% utilization.

| Phase | Before B wall | Final wall | Before B GPU | Final GPU | Before B / final wall p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Main | 0.8324 ms | 0.8336 ms | 0.4128 ms | 0.4440 ms | 1.0596 / 1.1147 ms |
| Room, shadows off | 2.4526 ms | 2.3553 ms | 0.5805 ms | 0.5232 ms | 3.4493 / 2.9703 ms |
| Room, cached shadows | 5.4352 ms | 5.3593 ms | 1.2860 ms | 1.2043 ms | 5.8654 / 7.0799 ms |
| Room, moving player/camera | 7.8247 ms | 6.8910 ms | 2.4302 ms | 2.2009 ms | 9.9927 / 8.8720 ms |

| Moving phase component | Before B | Initial B | Final |
| --- | ---: | ---: | ---: |
| Shadow resource preparation | 2.1786 ms | 2.3215 ms | 2.2991 ms |
| Receiver preparation | 1.8353 ms | 1.9197 ms | 1.9900 ms |
| Renderer submission | 2.2569 ms | 2.3706 ms | 1.2378 ms |
| Point-shadow GPU views | 0.4549 ms | 0.3238 ms | 0.2800 ms |
| Directional-shadow GPU views | 0.2947 ms | 0.2590 ms | 0.2218 ms |
| Scene shading GPU view | 0.4267 ms | 0.4324 ms | 0.4126 ms |
| Game shading GPU view | 0.3847 ms | 0.4206 ms | 0.3984 ms |
| Post/other GPU views | 0.3844 ms | 0.3749 ms | 0.3621 ms |

Cached-shadow renderer submission is 0.7992 ms in the final run versus
1.3600 ms before B (41.2% lower); moving submission is 1.2378 versus 2.2569 ms
(45.2% lower). The final correction alone reduced cached submission from
1.6978 to 0.7992 ms and moving submission from 2.3706 to 1.2378 ms, without
changing the candidate's pixels.

Whole-frame results require restraint: the earlier baseline A moving mean was
6.8302 ms wall / 2.1310 ms GPU, close to the final 6.8910 / 2.2009 ms. Static
wall p95 is worse in the final run despite its improved mean/submission cost.
Uncontrolled clocks, device activity and CPU scheduling are visible in these
runs. The data establishes the deterministic work/allocation reductions and a
substantial measured submission improvement; it does not establish a universal
editor/game FPS gain. Summed individual GPU views and the full-frame GPU query
have different scopes and gaps and must not be treated as interchangeable.

### Work, allocation and image results

| Moving phase measurement | Before | Final |
| --- | ---: | ---: |
| Point faces submitted per frame | 36 | 24 |
| Directional maps submitted per frame | 8 | 6: five world, one character |
| Shadow draws, including new explicit directional clears | 888 | 669.384 |
| All draws | 1,028.089 | 795.473 |
| Deferred coherent point updates | Not scheduled | 2 |
| Deferred critical/new-projection updates | Not scheduled | 0 |
| Resident shadow allocation | 507,256,320 bytes | 456,530,688 bytes |

The atlas drops from 483.76 to 435.38 MiB, saving 48.38 MiB (10%). Moving
shadow draw submissions fall by 24.6%. The atlas remains a significant
allocation because point-shadow residency has not become adaptive streaming.
Final texture/program residency is 41,167,294 bytes versus 41,101,494 before;
model residency varies slightly with asynchronous derived LOD retention and is
recorded in each JSON rather than hidden in the atlas figure.

Both static and moving warmups never exceed twenty-four point faces in one
world frame. The static room initially defers up to nine new projections, then
drains to zero. Its measured phase rewrites no point or directional maps.
During continuous movement, two caster-only updates remain deferred while
critical deferrals stay zero. The focused test separately verifies that all
sixteen resident lights eventually refresh and moved projections never sample
their stale depth.

Pixel comparisons against baseline B:

- Shadows-off Scene and Game are exactly identical decoded RGBA controls.
- Main changes eleven Scene pixels and one Game pixel, each by at most 1/255.
- Shadowed Scene changes twenty-seven pixels, each by at most 1/255, in both
  static/moving captures after the atlas/layout/sampling policy change.
- Static Game changes 11,123 pixels (1.789%) by at most 3/255; moving Game changes
  11,066 (1.780%) by at most 4/255. These use the intentional single-world plus
  focused-character policy. The Game images were inspected for coherent room
  coverage and lighting; they are not claimed pixel-identical to four cascades.
- Final versus initial candidate: eight of eight captures exactly identical.

### Focused verification

Twenty-one distinct focused checks ultimately passed: seventeen CPU/Noop
checks (34.60 seconds) and native-resource, point-shadow, Forward+ and isolated
material-mapping D3D11 checks. The new scheduler/selection, directional fitting,
frame ownership/pass planning and existing material/scene paths are included.
The material-mapping check uses the retained smoke fixture with unchanged
candidate shader contents under the isolated verification directory, because
the authored smoke source was previously deleted. No authored file was restored.

The first directional render assertion inherited FOV65/100 m coverage from a
broad floor test, leaving its 1.5 m caster under-resolved in a single 256 px map.
The independent six-metre directional fixture now explicitly uses FOV45 and
15 m coverage, retaining the original greater-than-60 brightness contrast
requirement and adding diagnostic values on failure. The repaired point test
passed in 7.69 seconds. This unit fixture change is separate from the real room,
which retains its authored 100 m coverage.

After the common metadata correction, the two directly affected GPU checks
passed again: point-shadow 7.73 seconds and Forward+ 4.01 seconds. They verify
budgeted admission and shared Scene/Game work, stale moved-projection rejection,
focused-caster filtering, independent world/focus caches, Scene/Game isolation,
masked coverage and reload preservation. The initial failed test log and both
successful rerun logs remain available.

### Design evidence and remaining scope

The provided D2R renderer transcription, PDF page 18, describes a main
directional shadow map, a separate character map, cached static spot-light
results and an omnidirectional player light; its fixed camera/view range avoids
cascaded gameplay shadows. Crimson adopts that camera-specific separation.
The free Scene camera still uses four inspection cascades. This is an explicit
view policy, rather than a claim that cascades were removed from every context.

The local Godot source provides a useful scheduling reference:

- `servers/rendering/renderer_scene_cull.h:60` bounds shadow update work.
- `renderer_scene_cull.cpp:2450` requires all six cube faces to fit before
  starting that light's shadow update.
- `renderer_scene_cull.cpp:3630–3641` leaves an unrendered required update dirty
  so it can retry.
- `renderer_rd/storage_rd/light_storage.cpp` chooses atlas classes from
  coverage, uses light versions to identify cached contents, and protects
  recently used allocations; `light_storage.h:395` sets a 500 ms reallocation
  tolerance in that checkout.

These files were read in the authorized local Godot source checkout at
`C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/godot-master`.
Crimson's four-light/twenty-four-face budget, normalized distance/radius
admission and age-based fairness are project-specific choices. Godot's dynamic
coverage-based atlas tiers and allocation hysteresis have not been ported.
The current atlas still retains sixteen fixed-resolution point-shadow slots;
changing update scheduling does not stream those allocations out.

## In-game acceptance workflow

These are user-operated checks using existing controls. They do not require a
new renderer mode or a special test scene import.

1. Open the intended dungeon scene and its **Game** panel. Select the intended
   Character entity and use **Make Player** in Inspector if it is not already
   assigned. Keep the visible character mesh parented beneath that Character;
   its subtree supplies the focused caster IDs automatically. Select the Camera
   entity, use **Make Active** if needed, and set **Follow Target** to that
   Character for the ordinary following view.
2. Select the directional light and enable **Cast Shadows**. Use **Shadow
   Distance (m)** to bound coverage to the gameplay area. Start with enough
   distance to contain the actual view and relevant occluders, then reduce
   excess coverage while watching the complete Game view. One map spanning
   100 metres has coarser world detail than the same map spanning 15 metres;
   coverage must still reach the player. The frozen room's follow-camera
   offset is about (30, 24.5, 30) metres and its target depth is about 49 metres:
   keep **100 m** for that setup. The automated 15 m test fixture uses a camera
   only six metres from its receiver and is not a recommended setting for
   the room. The focused character map provides separate local detail.
3. On the character's visible mesh, enable **Cast Shadows**. On the floor and
   other intended receiving meshes, enable **Receive Shadows**. Toggle the
   character's Cast Shadows briefly to identify its actual contribution, then
   restore it. There is no extra character-focus checkbox: a valid assigned
   player's caster bounds enable it automatically.
4. Use **Play** at the right of the top bar, or press **P**, then click walkable
   geometry in Game to move the player. Turn and move through the lit area.
   The directional shadow follows the presented character position; its light
   direction continues to come from the directional light, independently of
   the character's facing. This step does not supply a player-follow light rig
   or preserve dungeon darkness against a global directional light.
5. Inspect **Scene diagnostics → Shadows**. **World projections** and
   **character projections** count maps updated in the current editor frame,
   rather than resident map counts. A static view can therefore show zero.
   Each directional light's Game update uses one world projection plus an
   available focused character projection; the free Scene camera can also
   contribute its four world
   cascades. Simultaneous Scene/Game totals must be interpreted accordingly.
6. To inspect the independent focus cache, temporarily clear the Camera's
   **Follow Target**, leave the Game camera stationary, and move only the
   character. Its focused map should follow while the Game world map remains
   cached. Scene-view caster updates may still appear in the aggregate
   diagnostics. Restore Follow Target after this check.
7. With several point lights' **Cast Shadows** enabled, move the player among
   their ranges. **Lights rendered** and **faces** must stay within four and
   twenty-four per active-world frame. **Deferred** may be nonzero during
   movement. After motion stops and resource preparation finishes, it should
   drain to zero. **awaiting a valid map** describes new/moved projections
   temporarily contributing direct light without stale shadow sampling.
8. Press **F1** while Game is focused to show/hide the existing performance
   overlay. Compare the same camera, scene and window sizes before changing
   quality. Its CPU/GPU counters cover the whole editor frame, and device VRAM
   can include other applications. Use **Shift+Escape** or **Stop** to end Play;
   Play-only scene edits are discarded on Stop.

Use **Depth Bias (m)** and **Normal Bias (texels)** only for actual self-shadow
artifacts. Excessive bias detaches shadows from their casters; it is not a
replacement for suitable coverage. Shadow softness/contact hardening,
automatic room darkness and a configurable player light rig are not added by
this step.

## Evidence and reproduction

The results root is `build/verification/shadow-architecture-results/`:

- `before-saved-a/room-frames.csv`: 8,000 measured frames.
- `before-saved-a/room-summary.json`: timings, counters, memory and GPU views.
- `before-saved-a/*.png`: eight deterministic Scene/Game captures.
- `before-saved-a.log`: resource readiness, phase output and final summary.
- `hardware-before-before-a.csv` and `hardware-after-before-a.csv`.
- `comparison-step1-baseline.json`: comparison with the completed Step 1 image.
- `before-saved-b/`: second retained baseline with 8,000 measured frames and
  eight captures, followed immediately by the initial candidate B.
- `after-saved-a/` and `after-saved-b/`: initial implementation results before
  the measured CPU metadata correction. `comparison-a.json` and
  `comparison-b.json` retain those regressions.
- `after-saved-final/`: final 8,000 measured frames, summary and eight captures.
- `comparison-final.json`: baseline B versus final candidate.
- `comparison-metadata-optimization.json`: initial B versus final, including
  proof of eight exact decoded RGBA image matches.
- `shadow-work-counters.json`: measured work/admission counters for all runs.
- `integration-build.log`, `fixture-build.log`, `final-build.log` and the
  focused CPU/GPU, material fixture and point rerun logs preserve actual checks.

Reproduce the retained baseline from the repository root in PowerShell:

```powershell
$baseline = (Resolve-Path 'build/verification/shadow-architecture-baseline').Path
$fixture = (Resolve-Path 'build/verification/frame-architecture-baseline').Path
& (Join-Path $baseline 'bin/forward_plus_room_benchmark.exe') `
    (Join-Path $fixture 'engine-original86') `
    (Join-Path $fixture 'frozen-native/q-test/q-test.qproj') `
    '6aeb7097-5046-450e-a451-a8b4518aa095' `
    (Join-Path $baseline 'shader-tools/shaderc.exe') `
    (Join-Path $baseline 'shaders') `
    (Join-Path $fixture 'bgfx-includes') `
    --saved --warmup 300 --samples 2000 --capture `
    --output 'build/verification/shadow-architecture-results/before-repeat'
```

To reproduce the retained final candidate, use the same fixture and arguments
with `shadow-architecture-candidate-final` as the executable/shader owner:

```powershell
$candidate = (Resolve-Path 'build/verification/shadow-architecture-candidate-final').Path
$fixture = (Resolve-Path 'build/verification/frame-architecture-baseline').Path
& (Join-Path $candidate 'bin/forward_plus_room_benchmark.exe') `
    (Join-Path $fixture 'engine-original86') `
    (Join-Path $fixture 'frozen-native/q-test/q-test.qproj') `
    '6aeb7097-5046-450e-a451-a8b4518aa095' `
    (Join-Path $candidate 'shader-tools/shaderc.exe') `
    (Join-Path $candidate 'shaders') `
    (Join-Path $fixture 'bgfx-includes') `
    --saved --warmup 300 --samples 2000 --capture `
    --output 'build/verification/shadow-architecture-results/final-repeat'
```

Run only one GPU workload at a time. Compare completed matched runs with the
existing diagnostic helper; Pillow is required for decoded pixel comparison:

```powershell
python scripts/Compare-FrameArchitecture.py BEFORE_DIRECTORY AFTER_DIRECTORY `
    --output 'build/verification/shadow-architecture-results/comparison.json'
```

The helper checks workload identity, reports timing means/percentiles, counters
and tracked resident memory, and compares exact decoded RGBA pixels. Different
shadow pixels require inspection and explanation rather than being accepted
solely because the aggregate difference is small.
