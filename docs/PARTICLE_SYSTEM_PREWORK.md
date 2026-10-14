# Crimson Engine particle system: pre-work specification

Status: Steps 1–4 implemented in the main checkout. The initial Step 4 handoff and widget follow-up passed Release builds and 24 selected focused checks. The HDR/Environment Glow checkpoint passed Release and 28 selected checks. Effect pre-process for persistent fire/smoke is also implemented and passed Release plus 21 selected checks. Broader authoring acceptance remains with the user. The next particle implementation milestone is Step 5, Layered effects and timeline. Steps 5–14 and GPU simulation remain planned. Prepared 13 October 2026; implementation checkpoint updated 14 October 2026.

This document specifies the particle system's functionality, authoring workflow, engineering boundaries, research rationale, and implementation sequence. It supplements [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md); that guide remains authoritative for engine ownership and implementation status. [README.md](../README.md) remains the entry point for controls that have actually landed.

**Every implementation step must leave a working, usable, saveable particle system.** Build the smallest complete feature at each handoff. Infrastructure belongs inside the feature that needs it, rather than in a series of invisible foundation milestones. Complete each authorized step, run its required checks, report the result, and leave visual feature acceptance to the user.

Steps 1–4 below are the implemented baseline; the remaining steps are planned. Their numbering defines the particle implementation sequence; it does not replace the architecture guide's current **Next** milestone. This document does not authorize implementing the complete system in one change.

## Current checkpoint — 14 October 2026

### What is implemented

The usable baseline is the complete **asset -> editor -> preview -> scene -> save/reopen** workflow from Step 1, material and transparency work from Step 2, common emission/motion from Step 3, and appearance over lifetime from Step 4:

- Create a saved particle effect in the shared Asset Browser, open its floating non-dockable editor, and place independently playing instances in a scene. Effect, material and scene documents have separate save state and transactional undo/redo. Both asset editors support native windows on other monitors.
- Simulate one emitter on the CPU at 60 Hz, with Point, Cone, Sphere or Box emission. Cone starts on a local XZ disk and uses launch direction/spread; Sphere fills its volume; Box uses full XYZ dimensions. Direction XYZ, Speed, Spread and Flatness appear together; an optional Outward checkbox uses the sampled radial direction. Constant/Random Range lifetime and speed and seeded launch variation are implemented. The starter remains a point emitter at rest, with zero spread/flatness and negative Y gravity.
- Author Continuous, One shot or Looping emission with a delay applied once, a finite cycle duration when needed, and up to 64 uniquely identified bursts. A burst's time is relative to the emission cycle and must precede its finite end. One shots stop emitting and drain before becoming complete; Play Effect replays a selected instance.
- Add, enable, disable and remove **Gravity**, **Acceleration** and **Drag** through simple behavior controls. Required work and order stay inside the engine; there are no stage names or user ordering controls. Removing or disabling a behavior preserves its configured values for history and later use.
- Author Size and Rotation as absolute Constant, Random Range or Curve values, and HDR Color/Opacity as a gradient with intensity on every stop. The retained overall multiplier remains under Color → Advanced. Render contains collapsible Material, Color & opacity, Size and Rotation sections; Material starts open and appearance sections start collapsed. ImPlot curve plots support pan/zoom, Frame, double-click insertion, key selection/dragging and numeric values; the Coollab-based RGBA widget supplies gradient-stop interaction. The adapter retains stable identities, Linear/Constant interpolation, Add/Remove/Reset, one undo transaction per gesture and Escape cancellation. All key/stop ages, including the first and last, move within [0,1]; any can be removed while more than two remain. Values hold outside their authored age interval. Curves evaluate normalized particle age; appearance edits update existing particles even while paused. Gradient mode replaces effect Tint/Opacity, while material/texture appearance still multiplies its output.
- Play, Pause/Resume, Restart and Stop in the isolated preview. Moving an emitter retains playback: world-space particles remain detached after birth; local-space particles follow the emitter and its hierarchy. Forces use the selected simulation space, affine transforms retain position/velocity effects, and sprite size stays in world meters. Appearance changes retain particles and clocks, spawn inputs affect later births, and forces apply on the next tick. Seed, space, timing or burst edits replay from zero while retaining Pause/Stop state; capacity changes clear live records without resetting the clock.
- Assign an ordinary compatible material asset through the effect's picker or drag-and-drop field; clear restores the protected procedural default. Create materials in the Asset Browser and open them with Edit Material or by opening the asset. The material owns its texture, sampler, color/opacity and Alpha/Additive blend mode. Definitions and prepared texture leases remain coherent across replacement, deletion and preparation failures.
- Edit native materials in a floating window with File/Edit menus, a properties sidebar and live Sphere/Cube/Rounded Cube/Cylinder/Plane previews. The Environment menu controls an independent transient HDRI, intensity, rotation and background. Quader's material-preview tumble, pan, zoom and environment-rotation gestures are implemented. Filtering exposes Linear/Nearest beside Wrap U/V and preserves older sampler settings until explicitly changed.
- Render procedural or textured unlit Alpha/Additive sprites, with premultiplied output and stable back-to-front ordering across effects. Native Lit PBR Opaque/Mask materials can also be authored and previewed; **particle materials remain Unlit Alpha/Additive**. Imported originals remain read-only, and Save As creates an editable native material.

The five material-preview meshes are presentation choices, separate from the four implemented particle emission shapes; they do not imply mesh-particle support. Each effect still has exactly one emitter. Native v7 effect files retain v1–v6 reading and unchanged earlier constant appearance, timing, launch settings and material references. Files before v4 receive Flatness zero, and v3 Outward's formerly ignored spread migrates to zero; loading does not rewrite authored files. Multiple emitters, timeline/Step/seek, exposed parameters, flipbooks, stretched sprites, soft intersections, particle lighting, collision, trails/ribbons/beams, mesh particles, events and GPU simulation remain later milestones. Master/child materials and material graphs also remain planned. The engine's existing Play/static-Box Jolt world does not simulate particles; particle gravity and drag remain CPU equations, and particles pass through colliders.

### Validation and current limits

The initial Step 4 handoff passed one incremental Release integration build and all 24 selected focused checks on 14 October 2026. These included scalar/gradient evaluation and boundaries, sRGB-to-linear interpolation and linear alpha, stable random appearance lanes, interpolated normalized age, conservative bounds, live scene appearance edits without restart, v1–v4 migration, v5 validation/persistence, inactive data retention and failed saves. A private CPU ImGui context exercised key/stop drags, single-command undo/redo and Escape cancellation without controlling the user's applications. Native D3D11 rendering verified textured sprite rotation and restoration alongside existing particle transparency/resource checks; shader, material, scene, reference/deletion, Play and input regressions also passed. All 544 existing authored asset/layout file hashes were unchanged across that build/check run, before application launch. The previous Step 3/Flatness handoff passed Release and 16 focused checks; Steps 1–2 passed their own Release build and 22 checks. Those counts remain historical evidence and do not certify the new widgets or movable-edge follow-up. The documented authoring exercises belong to user acceptance; automated checks do not claim to have exercised every effect or monitor arrangement.

**Authoring follow-up verification: passed on 14 October 2026.** Release configure/build passed with the pinned ImPlot dependency and vendored gradient widget. All 24 selected focused checks passed, including the repaired widget check after input-ownership fixes. Private ImGui/ImPlot checks exercise actual first/interior/last point drags, one-command undo/redo and Escape, insertion/removal, a narrow 280-pixel window, curve zoom/Frame without authored edits, cross-widget Delete isolation, document keyboard focus, retained stop selection and opening the RGBA picker. Direct library/runtime comparisons verify linear-light RGB, independent alpha, left-held Constant interpolation and held values outside moved edge ages; CPU/asset/scene checks cover native v5 compatibility and live retiming without restart. The other selected material, resource/reference, scene, Play/input, shader and native-rendering regressions passed. All 564 existing authored asset/layout file hashes were unchanged across this build/check run before launch. Visual authoring and monitor acceptance remain with the user.

The limits and omission counters in Steps 1–4 are implemented bounds, not measured particle-count performance promises. An emitter has at most 64 burst records; each count is 1–65,536. Runtime work is capped at 4,096 scheduled births and 32 scheduling segments per fixed tick; clipped births/segments are counted and never become a future backlog. Each authored appearance curve/gradient has 2–32 stable keys/stops, freely movable ordered ages within [0,1] and minimum age spacing 0.0001. Values hold before the first and after the last authored age. Size tracks support 0–10,000 m, intensity 0–100,000 and rotation ±360,000 degrees. Existing capacity, scene, catch-up and render bounds still apply. CPU profiling at 1k/10k/50k particles remains part of the later profiling work. Center-depth sorting cannot exactly resolve intersecting transparent geometry, and the single HDR resolve can saturate intense transparent emission at display white; Section 3.6 records those rendering limits. The implementation retains existing project assets and docking state.

### Step 3 handoff — implemented, acceptance pending

The implemented user outcome is a **fountain, a one-shot impact and a directional spray** through ordinary controls. Each capability uses the existing effect document, isolated preview and scene instances, including saving/reopening and transactional undo/redo.

| Substep | Working result and necessary actions | Focused acceptance |
| --- | --- | --- |
| 3a. Shapes and variation | Point/Cone/Sphere/Box, Constant/Random Range lifetime and speed, combined Direction/Speed/Spread/Flatness controls, optional Outward, authored seed and deterministic spawn channels. Earlier effects retain original point/vector settings until deliberately edited. | Tune a fountain and cone spray, compare 3D spread with a flat fan, and vary spread around radial launch. Samples stay within authored bounds, saved settings reopen, and Restart repeats the same seed. |
| 3b. Bursts and emitter timing | Continuous/One shot/Looping, delay once, finite duration, bounded stable burst records, crossing handling and completion after draining. Numeric timing is available; the timeline remains Step 5. | Make an impact with rate zero and a burst at time zero. Pause/Resume does not duplicate it; delayed starts, loop crossings and draining behave at different render rates. |
| 3c. Forces and simulation space | Gravity/Acceleration/Drag in World or Local space. Shape positions and launch velocity honor full affine transforms; local particles follow the hierarchy and sprite size remains in world meters. | Tune a spray with drag and compare moving/parented emitters in local and world space. Save/reopen and undo preserve the selected behavior without affecting another instance. |
| 3d. Simple behavior authoring | Add behavior offers Gravity, Acceleration and Drag. Enable/remove controls preserve settings; stable typed identities and engine-owned execution order keep the effect valid. No stage or reorder interface is needed. | Add acceleration and drag, disable them to compare the result, remove and undo them, then save/reopen. Steps 1–2 effects continue to work. |

Simulation remains in the focused CPU subsystem. Shared force/drag and spawn inputs resolve outside per-particle loops; burst order prepares on definition publication, fractional newborn integration remains, and catch-up/scheduling work is bounded. Use one integration build after implementation owners finish and relevant focused checks for sampling, deterministic restart, timing crossings, ranges, transforms, history and format migration.

### Step 4 handoff — implemented, acceptance pending

The implemented user outcome is **smoke that grows and fades, and sparks that shrink, rotate or change color over their lifetime**. Appearance modes use inline controls and leave the small starter and fixed behavior order understandable.

| Substep | Working result and necessary actions | Focused acceptance |
| --- | --- | --- |
| 4a. Scalar lifetime appearance | Size/Intensity/Rotation offer Constant, Random Range and Curve with absolute values and normalized age 0–1. ImPlot supplies curve navigation/Frame and double-click insertion; bounded immutable preparation supports Linear/Constant evaluation and stable random identity lanes. | Make smoke expand, sparks shrink and sprites rotate. Move/remove first, interior and last keys; values hold outside the remaining interval. Every intermediate edit previews, each drag is one undo command, Escape cancels, and saved curves reopen correctly. |
| 4b. Color and opacity gradients | HDR RGBA stops replace effect Tint/Opacity and interpolate linear RGB while retaining independent linear opacity. Earlier SDR stops decode once on load. A vendored Coollab stop widget and adjacent numeric fields retain Linear/Constant interpolation and Crimson's color contract. | Fade smoke to zero opacity and change spark color over lifetime. Move/remove edge or interior stops while retaining two; held edge values, undo and save/reopen are predictable. Constants remain available when switching back. |
| 4c. Workflow and compatibility | v5 retains v1–v4 constant appearance and references. Inactive ranges/curves/gradients remain stored and validated. Appearance applies live without clearing records/clocks; scalar rotation reaches the camera-facing renderer and conservative size bounds. | Earlier gravity/spray/burst assets reopen unchanged. Two placements retain independent clocks while appearance edits update them, including while paused. |

### Pre-Step-5 checkpoint: HDR particle colors and Environment Glow

The user outcome is **fire, sparks and color ramps with independently bright stops, plus an environment-controlled halo**. This extends Step 4 without introducing multiple emitters, a material graph or particle-generated lights.

| Working result | Implementation and compatibility | Acceptance |
| --- | --- | --- |
| HDR constant color and every gradient stop | A shared picker edits an ordinary hue, Intensity in EV (with its multiplier), and independent opacity. Canonical RGB is linear HDR; intensity is multiplied before ramp interpolation. RGB supports 0–1,000,000 and opacity 0–1. HDR values remain intact when the gradient bar displays readable swatches. | Author a vivid blue stop at +5 EV and green at +1 EV, then move and edit both edge stops. Brightness and opacity remain independent. Each gesture is one undo command, Escape cancels, and live particles retain their clocks. |
| Existing effect preservation | Particle v6 writes `linear-hdr`; readers retain v1–v5 SDR validation and decode RGB exactly once without rewriting the source. Existing constant/range/curve intensity stays as **Color → Advanced → Overall Color Multiplier**, default 1 for new effects. Non-neutral active multipliers are disclosed. Animated/random multipliers are retained rather than approximated by baking ramp stops. | Reopen older gravity/spray/ramp effects with unchanged appearance, save/reopen v6, and compare undo/cancel. No ordinary material assignment changes are needed. |
| Environment Glow | Enabled, Strength and Size are basic controls; Threshold is advanced. Glow is independent of HDRI illumination. Scene/Game share the active authored Environment; the particle popup and material Environment menu have independent transient preview settings. Existing scenes and new default settings have Glow off. Scenes write v5 and read v1–v4; legacy environment storage remains compatible. | Enable Glow for bright particles, disable environment lighting and confirm the halo remains. Save/reopen the scene and undo environment edits. Compare Scene/Game and independent previews; changing preview settings must not dirty documents. |
| Shared HDR post-processing | Renderer-owned half-resolution float32 ping-pong targets extract exposed visible HDR radiance with a soft threshold and apply contiguous separable Gaussian blur before the single tone map. Preserve source coverage, including additive particles over coverage zero. Grid, tools and the empty display matte never enter extraction. Thumbnails explicitly omit Glow. Size/Strength zero and disabled Glow retain the old resolve. | Compare Glow off/on, Alpha/Additive overlap, empty backgrounds and models, bright versus dim colors, viewport resizing and independent preview contexts. A white matte may clip a positive halo at display white; Glow does not illuminate nearby geometry. |

**Verification passed on 14 October 2026:** the incremental Release integration build and the targeted fixes rebuild passed. All 28 selected checks passed, including old-format output preservation, HDR interpolation/round-trip and failed-save retention, EV/opacity/stop undo and Escape, bounded picker-state churn, scene and Play environment history/migration, legacy environment storage, materials/references/deletion, shader compilation/preflight, point shadows and model-material rendering. Actual hidden D3D11 readbacks verified additive zero-coverage halos, exact disabled/zero-Strength/zero-Size paths, threshold/exposure/matte handling, Scene/Game/particle/material contexts, extreme supported radiance, thumbnail bypass, resize/release accounting, shader reload and a continuous maximum-Size halo around a tiny isolated spark. The blur uses 17 point fetches per half-resolution pass at Size 1 and at most 129 at Size 8; scene-scale performance remains unprofiled. All 578 protected authored asset/layout file hashes remained unchanged before launch. Visual authoring and monitor acceptance remain with the user. This checkpoint does not change the engine's **Next** milestone or the particle Step 5 sequence.

### Pre-Step-5 checkpoint: pre-process for persistent effects

The user outcome is **a fireplace or smoke plume whose first visible state is already populated**. Timing exposes **Pre-process (s)** on the effect asset, with 0 disabling it and a supported range of 0–60 s. A useful starting value for continuous emission is the particle lifetime. Earlier v1–v6 effects load with zero; schema v7 saves this field while retaining v6's linear HDR representation and earlier color migrations. Loading never rewrites older effects.

Restart completes the requested time synchronously before its first draw, rounded up to a 60 Hz tick, using the normal seeded simulation and the instance's initial affine transform. Delay, bursts, loops, gravity, drag, lifetimes and appearance retain their ordinary meaning; no earlier emitter movement is invented. The completed result is published as the first visible state, including on initial Play and every Stop/Play cycle. Existing per-tick scheduling/spawn limits, particle capacity and the 60 s bound remain; live wall-clock catch-up has its own unchanged budget. Larger times/counts can add startup work. A finite one shot may already have drained by the chosen time. The earlier across-frame queue was removed because it left the Game view without the effect while preparing it.

Scene opening/placement, entering Play, isolated preview, Restart and Play Effect all complete the saved pre-process before first presentation. Pause retains the prepared state; Stop returns to zero, and Play from Stop repeats preparation. Changing pre-process time uses the timing-edit replay policy, including undo/cancel and retained Pause/Stop state. Edits to a stopped instance do not run discarded warm-up work. Transform changes, saving and ordinary appearance edits preserve playback. No live arrays or clocks enter effect/scene files or history.

Acceptance: save a continuous fireplace with a 2 s pre-process, reopen and place it, then enter Play. The very first Game presentation must already contain mixed particle ages and gravity displacement, with no empty warm-up frames. Repeat Stop/Play and Restart: each first frame must reproduce the same warmed seed/state. Disabling pre-process restores ordinary startup. Compare paused editing, scene duplication and undo, and several simultaneous emitters. Earlier effects and HDR ramps must reopen unchanged. This startup feature does not implement Step 5's timeline or general seeking.

The initial across-frame revision passed Release and 21 selected checks, but user acceptance exposed an empty interval on initial Play and Stop/Play. That behavior was corrected to complete pre-process before first draw, matching the inspected Godot CPU startup order. The correction keeps asset schema v7 unchanged and replaces the queued-work tests with first-presentation assertions.

**Startup correction verification passed on 14 October 2026:** one incremental Release integration build and all 21 selected checks passed. CPU/Scene/Play checks require a two-second effect to contain all 120 pre-process ticks before restart/start/reconcile returns, expose the final warmed presentation immediately, reproduce the first image after Stop/Play, and ready all 16 test emitters together. They also cover seeded ordinary-tick equivalence, full affine World/Local transforms, paused/stopped editing, zero-time legacy behavior, early one-shot completion, maximum duration and decimal quantization. Existing appearance/HDR, persistence/history, material, input, scene, reference/deletion and import checks passed. The remaining cost is upfront simulation for larger time/count settings; no startup-time performance guarantee is claimed. Visual Game-frame acceptance remains with the user.

### Next particle handoff: Step 5 — Layered effects and timeline

The next user outcome is **one reusable impact containing flash, sparks and smoke**, with understandable emitter selection/timing and a way to inspect paused moments. Each substep must keep the effect usable, saveable and undoable; the editor continues handling execution order.

| Substep | Working result and necessary actions | Focused acceptance |
| --- | --- | --- |
| 5a. Multiple emitters | Extend the versioned effect to an ordered collection with stable emitter identities. Add selection, create, duplicate, rename, enable and remove. Give each emitter its existing material, shape, timing, forces and appearance controls; migrate older one-emitter files unchanged. Bound aggregate storage/work and keep scene/preview effect-instance ownership intact. | Build flash, sparks and smoke inside one effect; select/tune each independently, duplicate/remove/undo, then save/reopen and place two scene instances. Older effects keep their appearance. |
| 5b. Emitter timing tracks | Add readable delay/duration tracks and burst markers beside the numeric controls. Dragging a track/marker edits the same authored values in one transaction; retain engine-owned behavior order and explicit Continuous/One shot/Looping semantics. | Delay smoke after the flash, retime the spark burst, undo each drag and compare preview/scene playback. Timeline and numeric values agree; unchanged emitter definitions stay intact. |
| 5c. Paused fixed-tick Step | Add Step to the isolated preview, advancing one 60 Hz tick across the effect's emitters while remaining paused. Keep presentation interpolation, time-zero bursts and drain/completion coherent. | Pause a layered impact, advance tick by tick through its burst and draining phases, then resume without duplicate emission. Step changes no authored dirty state. |
| 5d. Bounded preview seeking | Replay a frozen definition/seed/transform from zero toward a selected time using bounded work per frame, progress and cancellation. Tag replay requests so older work cannot publish into a changed or closed document; retain usable preview state on cancellation. Keep arbitrary scene-history reconstruction outside scope. | Seek to the same moment twice and obtain repeatable results. Long seeks remain responsive/cancellable; edits, restart or closing the effect discard stale replay safely. |

Supporting work belongs in these features: expand material dependency scans/replacement/deletion across all emitters, preserve key/stop identities and independent effect/scene history, reconcile instances by document generation/persistent entity ID, and report aggregate/per-emitter limits. After owners finish, run one integration build and focused checks for migration, stable identities, scheduling, bounded replay, cancellation, history and reference/resource lifetime. Flipbooks/stretch remain Step 6, exposed parameters Step 7 and particle collision queries Step 9. This sequence does not change the architecture's **Next** milestone, **2c: imported-material editing and selected model-material assignment**.

## 1. Product direction and scope

The target is a comprehensive CPU particle system for reusable game effects. Common authoring operations arrive first; specialized behavior arrives after the everyday system is useful.

The agreed capabilities are:

- Native particle-effect assets containing one or more emitters, created in the shared Asset Browser and opened by double-clicking.
- A dedicated particle document/editor with independent save state, history, and isolated preview.
- Simple typed behavior controls with engine-owned ordering, emitter timing, curves, gradients, and exposed parameters.
- Scene placement, attachments through the existing hierarchy, automatic playback, and selected-instance replay.
- Textured sprites, flipbooks, stretched and constrained sprites, trails, ribbons, beams, and static mesh particles.
- Common emission shapes, variation, forces, local/world simulation, collision responses, and child emitters.
- CPU-first simulation with explicit storage and rendering boundaries suitable for a later GPU backend.
- Jolt static collision queries introduced together with usable particle collisions and authored colliders.
- Bounded memory/work, reference diagnostics, and basic performance counters from the first step.

Dynamic rigid bodies, character movement, a gameplay Play world, skeletal emission, a node graph or scripting VM, distortion, decals, effect-generated lights, shadows, and volumetric/fluid simulation have separate scopes. Optional particle lighting uses the renderer's existing lights and IBL. The visual fidelity of an authored effect also depends on its textures and parameters; a feature checklist is not an art-quality guarantee.

### Priorities

1. Finish the asset -> editor -> preview -> scene -> save/reopen loop with simple gravity particles.
2. Make ordinary smoke, sparks, impacts, fire, and layered effects easy to author.
3. Add reuse, surface interaction, and connected geometry while preserving earlier effects.
4. Add specialized motion, emission, and measured optimization last.

There is no standalone framework, render graph, job scheduler, or physics-foundation step. GPU simulation is a separate later implementation, guided by actual CPU and rendering measurements.

## 2. Workflow and ease of use

### 2.1 Create and open

Add **Create -> Particle Effect** to the shared Asset Browser, with the same action available in its existing context-menu workflow. Preserve the single Import button and shared catalog. Creation inserts a valid saved asset into the current virtual browser folder, selects it, and opens its editor. No creation wizard or external texture is required.

Use **New Particle Effect** with collision-free naming consistent with the browser. The asset has one enabled emitter, **Emitter 1**, and produces visible particles immediately. Double-clicking an existing asset opens its particle document; opening an already-open asset focuses its document rather than creating conflicting copies.

The initial starter values are engineering defaults, not performance limits:

| Property | Starter value |
| --- | --- |
| Shape | Point |
| Emission | Continuous, 30 particles/s |
| Lifetime | 1.25 seconds |
| Initial velocity | `(0, 0, 0)` m/s; start at rest |
| Gravity | `(0, -9.81, 0)` m/s² |
| Size | 0.08 m |
| Color / intensity | White / 1 |
| Renderer | Procedural soft sprite, unlit additive |
| Capacity | 256 particles |
| Space / preview seed | World / retained seed 1 |

The isolated preview frames the constant-velocity/gravity trajectory over the authored lifetime, including the emitter origin and any turning point. The falling starter begins at rest; an explicitly positive initial Y velocity produces an upward launch before gravity reverses it. Its grid is a visual aid, not a collider. The starter must work before imported textures, sophisticated modules, or Jolt exist.

Creation failure leaves existing documents and the browser intact. It must not publish a broken catalog entry or overwrite another asset.

### 2.2 Edit without unnecessary setup

Show controls that the current implementation supports. Introduce advanced controls with their feature. The first document exposes rate, lifetime, initial velocity, gravity, size, color, and capacity; it does not require learning all later modules.

Initial velocity now shows **Direction XYZ**, **Speed**, **Spread (degrees)** and **Flatness** together. Spread zero means straight launch; Flatness zero preserves full 3D spread and one produces a fan in the emitter-local plane through the launch axis and its perpendicular tangent. It changes launch directions rather than sprite geometry. An optional **Outward from shape** checkbox centers spread/flatness around the sampled radial direction. No Vector/Direction or Fixed/Random-spread mode selector is exposed. For old Vector/Fixed effects, a pure view helper derives equivalent displayed controls without altering the authored definition; a launch-control edit intentionally publishes the converted settings. The scalar speed bound is 200,000 m/s, covering the norm of older vectors with ±100,000 m/s per component.

Use readable property names and units: particles/s, seconds, meters, m/s, m/s², and degrees. Place a diagnostic beside its field or emitter. Do not expose persistence IDs, GPU terminology, or resource-cache details as ordinary authoring controls.

Use the engine's adjacent picker, thumbnail/name, clear, and drag-and-drop conventions. Typed fields filter compatible assets and assign immediately. A cleared optional sprite texture uses the documented procedural sprite. An unresolved assigned reference retains its identity and a visible diagnostic; retain a last valid prepared resource when possible, otherwise suspend the affected renderer/emitter until it resolves or is cleared/replaced. A missing root effect retains its scene component while playback is unavailable; retain instance overrides as well when Step 7 introduces them.

The sprite renderer assigns an ordinary material asset through a typed `AssetRef<MaterialAsset>` slot. The material owns its texture/sampler, linear base-color RGBA, and Alpha/Additive mode; emission, motion, size and per-particle tint/intensity/opacity remain on the effect. Step 2 extends the existing `.crimson-material` system with unlit sprite-compatible settings. It does not introduce a separate particle-material asset format. A protected white procedural additive material keeps creation immediately useful. Pick or drop an existing compatible material, clear to the protected default, and use Edit Material to open its independent document. Create materials through the shared Asset Browser's Create → Material action, then assign them. Material commits publish valid complete definitions to referencing effects without restarting playback.

The material editor is a floating, non-dockable document with a replaceable properties sidebar and live 3D preview. Its File menu provides Save, Save As and Close; Edit provides material Undo/Redo. Native Unlit Alpha/Additive and Lit PBR Opaque/Mask materials are editable. Imported originals are read-only; explicit Save As creates a native material with independent identity and no imported origin. Sprite assignment still filters Unlit Alpha/Additive profiles. Imported-original editing and model-material assignment remain the architecture's Next milestone.

The preview offers Sphere by default, Cube, Rounded Cube, Cylinder and Plane. Its Environment panel has HDRI pick/drop/clear, enable, intensity, rotation and Show background. Preview camera, mesh and environment remain transient and independent of scene lighting and authored material/effect files. Quader material mini-viewport gestures are MMB object tumble, Shift+MMB pan, Ctrl+MMB environment rotation, wheel zoom and MMB double-click to reset the view and environment rotation. Texture fields keep their adjacent picker, thumbnail/name, clear and drag-and-drop. Wrap U/V remain explicit beside one Filtering selector, Nearest or Linear; existing sampler values stay preserved until that selector is deliberately used.

Material graphs, master materials and child material parameter overrides remain planned. The current property view is a replaceable authoring surface; it does not implement graph nodes, a graph compiler or material inheritance.

Size and Rotation offer **Constant**, **Random Range** and **Curve** beside the property; the retained overall intensity function is under Color → Advanced → Overall Color Multiplier. Values are absolute: Size in world meters, Intensity as an authored factor and Rotation in degrees in the camera-facing billboard plane. Curves use normalized particle age 0–1, not emitter seconds. Random appearance values remain stable for each spawn identity and do not flicker while time advances. Color offers Constant or Gradient with RGBA stops; Gradient replaces the effect's constant Tint/Opacity rather than multiplying hidden constants, and material/texture appearance still multiplies its output. Authored RGB is canonical linear HDR and interpolates directly; v1–v5 decode SDR sRGB once on load. Opacity stays linear.

Render groups Material, Color & opacity, Size, Rotation and Intensity into collapsible sections. Material starts open; appearance sections start collapsed. Scalar curves use ImPlot 1.0 for pan/zoom, Frame, double-click insertion, selection/dragging and adjacent numeric editing. Plot navigation changes transient view state only. RGBA stops use the vendored Coollab widget through Crimson's adapter, with numeric color/opacity/age fields and Add/Remove/Reset. Both support Linear/Constant interpolation. All ages, including the first and last, may move within [0,1] while retaining order and minimum spacing 0.0001. Before the first authored age, hold its value; after the last, hold the last value. Any selected key/stop is removable when more than two remain. Each track retains 2–32 stable keys/stops. Switching modes retains inactive data. A drag is one undo transaction, Escape restores the prior authored settings, and valid appearance edits immediately affect live particles without resetting their clocks. Zero Size hides presentation without ending a particle's lifetime.

These are editor-only dependencies: ImPlot comes from the pinned vcpkg manifest; Coollab `imgui_gradient` is vendored with its license and patch notes at the commit recorded in R1d below. The adapter keeps document transactions, stable key identities, constraints and input routing inside Crimson. Neither dependency enters serialized effects, CPU particle records or the simulation backend.

Keep essential controls in a fixed readable order. **Add behavior** lists the implemented optional Gravity, Acceleration and Drag behaviors; enable/remove actions retain their values. Users do not need stage names, dependencies or ordering controls. The engine resolves required initialization, motion and presentation work in a valid order. Later behaviors extend this same workflow when their functionality lands; introduce user ordering only if a real authored operation needs it and valid dependencies can be maintained.

An emitter can feed multiple compatible renderers when that capability lands, such as a sprite plus its trail. Users should not need to duplicate a simulation to draw the same particles in two supported ways.

Emitter duplication and later starter presets produce ordinary editable definitions. Presets are copies, not hidden behavior or a new linked-template system. Keep Dear ImGui's default colors, font, sizing, and the user's docking state. The particle editor is a floating, non-dockable window: dragging its title bar outside the main window creates a native platform window that can move to another monitor. Preserve the other panels' docking state, route input to the owning window, and use the host monitor's built-in DPI scaling. This specification defines interactions without prescribing a panel arrangement.

### 2.3 Playback

| Action | Required behavior |
| --- | --- |
| Play / Resume | Start from the stopped state or continue a paused state without clearing it |
| Restart | Clear particles, trail history, pending events, and child effects; reset and run configured pre-process before replay with the retained seed |
| Pause | Retain the prepared live state and stop advancing simulation |
| Stop | Clear the effect and return to time zero; do not synthesize death events |
| Play Effect in scene Inspector | Restart the selected scene instance using its resolved seed |
| Step, from Step 5 | Advance one fixed tick while paused |
| Seek, from Step 5 | Reset/replay the isolated preview toward the requested time with bounded work and cancellation |

The editor displays elapsed time and live counts without a scrub control. Seeking arrives in Step 5. Step 3 exposes the authored Seed for shape, lifetime, speed and direction variation; Restart repeats it, and a seed/pre-process-time edit replays the configured startup while retaining Pause/Stop state.

Scene effects start on successful placement and scene opening. Continuous effects keep emitting until stopped. One shot emits for its finite duration, then drains; Looping repeats its finite cycle. Delay occurs once before the first cycle and repeats only on Restart. Burst times are cycle-relative, including time zero, and occur strictly before a finite duration ends. Completed one shots remain selectable, and Play Effect restarts the chosen instance. Selection changes, moving/parenting the emitter and unrelated entity edits preserve playback.

World space is the default: birth position and launch velocity use the current full affine emitter transform, existing particles remain detached, and gravity/acceleration use world axes. Local space simulates positions, launch velocity and forces in emitter coordinates, then applies the current full affine world matrix during presentation, so live particles follow hierarchy changes. During Play, simulation births use fixed-tick transforms while local rendering/bounds use copied interpolated presentation transforms; attached particles therefore follow the actor's displayed pose independently of its authoritative simulation transform. Sprite size stays in world meters in both modes; emitter scale/shear affects positions and launch directions rather than billboard size. Changing Space replays the effect and retains Pause/Stop state. Preserve the authored matrix rather than decomposing and replacing it with TRS.

Preview playback is transient. Play, Pause, Restart, camera navigation, and seeking do not dirty a particle asset or scene. Camera-dependent draw culling initially does not suspend simulation or change event/completion behavior. Pausing, focus loss, and minimized-window handling clear accumulated wall time so resuming does not replay a catch-up burst.

### 2.4 Document editing, publication, and saving

Particle assets, native materials and scenes have independent dirty state and undo/redo. **Ctrl+S**, **Ctrl+Z**, **Ctrl+Y**, the equivalent redo shortcut, and menu actions target the document that owns focus. Material assignment belongs to effect history; material property edits belong to material history. Editing either asset must not accidentally save or undo its scene. Exit resolves material, effect and scene dirty-document prompts in sequence.

A property drag, curve edit, or timeline drag forms one transaction. Escape restores the prior values. Temporarily incomplete text is editable; constrain ordinary numeric fields where practical and show invalid values locally.

Keep these three operations distinct:

1. During an active transaction, valid intermediate values update the isolated preview. They do not publish incomplete definitions to scene instances.
2. A successful commit publishes a complete valid in-memory definition revision and updates scene instances according to the field policy below. Ordinary appearance, spawn and force edits stay live; seed/space/timing/burst edits intentionally replay to define a coherent schedule. Cancel restores previous authored values under the same policy. Invalid commits retain the last valid runtime definition.
3. Save atomically writes the committed revision. Save does not cause another restart. Failure retains the previous disk file, working revision, and dirty state.

Closing a dirty particle document, replacing its contents, or exiting uses Save/Discard/Cancel. Discard restores the saved definition and updates affected instances. Preserve the repository's external-change/conflict behavior when wiring the resource manager and file watcher; do not silently overwrite a changed asset.

The committed working definition is authoritative for open dirty documents until Save, Discard, or an explicitly accepted external reload. A disk watcher or background load must not replace it with an older saved payload. Publish its complete immutable snapshot through the existing resource mechanism and retain the last valid published payload on preparation failure.

Steps 1–4 apply the following edit policy in both preview and scene instances. Undo/redo and cancellation follow the same policy; document history stores definitions rather than live particles or clocks.

| Field or operation | Effect on an existing instance |
| --- | --- |
| Color/opacity, Size/Intensity/Rotation modes and values, appearance ranges, curve keys and gradient stops | Apply to existing and new particles on the next presentation extraction at interpolated normalized age, even while paused; stable random lanes retain each particle's sample |
| Material assignment or committed material appearance | Prepare coherent bindings and update rendering without clearing particle records or changing playback time |
| Gravity, acceleration, drag, behavior enable/presence | Apply to existing particles on the next fixed simulation tick |
| Rate, emitter enabled | Apply on subsequent emission scheduling; preserve phase and allow existing particles to drain |
| Shape, launch direction/spread/flatness/outward, speed/range, lifetime/range | Apply at birth; existing particles retain their cached birth values |
| Capacity | Prepare replacement storage and clear live particles, retaining clock, emission phase, spawn identities and playback state; count cleared particles |
| Seed, pre-process time, simulation space, delay/duration/playback mode, burst records | Clear and replay the configured startup with the authored definition, retaining Pause/Stop state; Restart repeats the retained seed and pre-process |
| New asset/emitter identity, document replacement, explicit Restart or Play Effect | Clear live state and restart from zero |
| Save | Atomically store the revision without changing simulation state |

Scene-instance exposed parameters later follow these same stages: spawn inputs affect newborns, update inputs apply on the next tick, and rendering inputs apply on the next extraction. Future structural changes require prepared publication and an explicit compatibility policy; identity changes restart, while capacity replacement clears storage without resetting time. Do not make every new definition revision an implicit restart. Parameter renaming preserves bindings through stable IDs; type incompatibility is diagnosed without discarding authored overrides.

### 2.5 Usability exercises

Use short authoring tasks as feature acceptance, with ordinary controls rather than hidden debug options:

- Create an effect and make its particles fall faster by changing gravity: Step 1.
- Assign smoke imagery and clear it back to a procedural sprite: Step 2.
- Make a one-shot impact with a cone burst: Step 3.
- Make smoke grow and fade over life: Step 4.
- Add delayed smoke to flash and sparks: Step 5.
- Animate a sprite sheet and stretch sparks along velocity: Step 6.
- Reuse one effect with a different tint on one scene entity: Step 7.
- Soften a wall intersection, then separately enable existing-light response: Steps 8a/8b.
- Bounce sparks off an explicitly authored collider: Step 9.
- Add a trail, then make an impact spawn a child puff: Steps 10/12.

Preview navigation owns its accepted mouse gestures and focus commands. Property controls, asset dragging, timeline/curve dragging, and the main scene viewport must not consume the same gesture. Reuse familiar navigation conventions; this slice does not justify redesigning editor input.

## 3. Architecture and compatibility contracts

### 3.1 Concrete owners and interfaces

These contracts cover the implemented baseline and later extensions. The milestone descriptions distinguish delivered capabilities from planned ones:

| Boundary / interface | Responsibility |
| --- | --- |
| `ParticleEffectAsset` | Versioned authored emitters, built-in module settings, renderer settings, parameters, stable identities, and managed dependencies |
| `MaterialAsset` / `MaterialDocument` | Native material appearance, compatible sprite/PBR profiles, material-owned image dependencies, atomic storage and independent authored history |
| `ParticleEffectDocument` | Working definition, transactions, history, dirty state, validation, and atomic save/reload |
| Prepared emitter/module data | Validated native stage lists, immutable curve data, resolved parameter bindings, and required storage streams |
| `CpuParticleSystem` / effect instances | Scheduling, seed/run identity, pooled live state, motion, events, completion, and work limits |
| `SceneParticleEmitter` | Typed effect reference, seed/playback defaults, exposed overrides, and authored attachment through the scene transform |
| `ParticleRenderFrame` | Copied presentation data and render indices, with no ECS pointers or authoritative simulation mutation |
| Editor preview/session | Isolated instance/clock, document operations, material preview camera/HDRI, preview collision fixtures when implemented, and scene reconciliation |
| Renderer particle resources | Programs, quad/mesh/ribbon buffers, bindings, targets, ordering, and explicit GPU release |
| `SceneCollider` / `SceneCollisionWorld` | Authored static collision settings and an explicit Jolt owner with Crimson query values |

Keep particle asset/storage work in `src/assets/`, document orchestration in `src/editor/`, controls in `src/tools/`, and scene components/history/extraction in `src/scene/`. Simulation lives in `src/particles/`; the existing `src/physics/` Jolt boundary will gain particle query support when Step 9 needs it. Rendering and shaders remain in `src/crimson-renderer/`. `App` composes lifetimes and forwards time/events; it does not accumulate these implementations.

Use explicit concrete dependencies and ordinary native code. Reuse the checkout's typed `AssetRef`, resource snapshots/publications, atomic-file helper, and reference-management seams. Do not add a second loader framework, service locator, generic message bus, VM, or job graph.

Public math uses Crimson vectors, matrices, bounds, and rays. Serialize authored values and references, never live particles, playback clocks, GLM values, EnTT handles, Jolt objects, bgfx handles, or ImGui state. GPU owners release before bgfx shutdown. CPU jobs publish tagged prepared results; they do not mutate the scene or create GPU resources.

### 3.2 Current-checkout integration baseline

At preparation time, the working tree included typed references, resource workers/publications, material assets, asset replacement/deletion machinery and streaming geometry under active development. Step 1 was implemented against an isolated snapshot after rechecking those owners and their existing APIs, then integrated into the main checkout after the concurrent work was committed. The integration retains the current affine Inspector controls, model import identity/reuse, material previews, surviving-owner deletion warnings and nonmodal Preferences input routing. Later steps must recheck the actual resource/editor owners and APIs they start from.

The planning baseline wrote scene version 2. Step 1 introduced typed particle references in scene version 3; the actual checkout now writes scene version 5 with optional Glow and reads versions 1–4. Step 4 introduced particle v5; the HDR follow-up introduced particle v6 with linear HDR colors; pre-process extends the current writer to v7 while retaining older readers. Extend the actual versioned owners for later features and retain older inputs. Do not implement persistence against historical scene shapes or assume old synchronous model/image preparation remains authoritative.

`SceneDocument::documentGeneration()` changes on document replacement. `Scene::generation()` also changes during ordinary registry rebuilding. Own scene particle instances by document generation plus persistent entity ID, and reconcile changed values. Resetting on every registry generation would restart effects during Inspector edits and gizmo motion. The same rule prevents unrelated collider recooking.

### 3.3 Assets, references, and persistence

Use native versioned effect files under `assets/particles`, with a `.crimson-particle` extension and project-relative managed identity, following native asset storage conventions. Browser names/folders do not become runtime identity. Store stable emitter/module/parameter IDs from their introduction; new optional feature fields receive compatible defaults as steps land.

Current writers use effect schema v7 with linear HDR RGB and effect-level pre-process time; readers accept v1–v7. v1–v6 default pre-process to zero. v1–v5 SDR colors decode exactly once in memory while existing intensity modes/curves remain unchanged. v1–v4 retain earlier constant appearance with rotation zero, Constant modes and empty inactive curves/gradients, along with existing timing, launch behavior and references. v1/v2 retain original point/world/continuous behavior; files before v4 receive Flatness zero. v3 RandomCone preserves exact spread, Fixed retains inactive spread, and Outward replaces formerly ignored spread with zero so radial appearance remains unchanged. Migrations stay in memory until explicit Save. Bursts and appearance keys/stops carry stable nonzero IDs unique within their authored scope. Gravity, Acceleration and Drag have stable typed identities by name, explicit presence/enable flags and preserved settings; no authored stage list is serialized. All present data, including inactive ranges/curves/gradients, remains finite, bounded and validated. Active Random Range requires minimum/maximum values; active Curve/Gradient requires valid keys/stops. Unknown modes/behaviors, inverted ranges and invalid key/burst times/counts are rejected before publication. Merely displaying legacy launch controls does not convert the definition or dirty the document. No new external reference type is introduced by appearance tracks.

Loading validates a complete candidate before replacing a working definition. Saving atomically replaces the selected file. Save As creates a distinct identity and preserves the original. A failed load/save leaves active documents and previous files intact.

Scene history contains the effect reference and authored instance settings. Effect history contains definitions. Neither stores live particles. Restoring a deleted instance through scene undo creates a fresh transient instance from its authored settings; undo does not resurrect an old simulation buffer.

Add dependency support with each feature: scene -> effect in Step 1, effect -> material -> texture in Step 2, effect -> model in Step 11, and event source/target emitter validation in Step 12. Include active documents, saved assets, and history in reference scanning as required by the existing policy. Replacement is typed and transactional. Particle material deletion falls back to the protected procedural particle material; model material deletion keeps its PBR default. Sprite texture clearing/deletion uses the procedural fallback. An unresolved unexpected reference retains its identity and diagnostics. Deleting a particle instance never deletes its shared asset or dependencies.

### 3.4 CPU storage and module execution

Start with compact contiguous records per emitter: stable spawn ordinal, previous/current position, velocity, age, and lifetime. Use explicitly typed optional arrays for enabled appearance, rotation, flipbook, trail, and collision state. Reserve to configured capacity; no heap object or ECS entity per particle.

Removal fills a dead slot from the last live slot and moves every enabled associated stream together. Events, attachments, and trail links use stable run/spawn identity, never a dense slot index. Rendering sorts independent indices without permuting simulation storage. Reuse extraction and sorting scratch capacity.

Required streams and dependencies are known before advancing particles. Step 3 prepares burst time order on definition publication and resolves typed force/drag and spawn inputs once per emitter tick. The particle loop reads resolved values with fixed engine-owned ordering; it does not interpret schemas, perform name lookup or dispatch virtual modules per particle. Step 4 prepares bounded fixed scalar/gradient segment arrays as immutable per-definition values. Appearance evaluates interpolated normalized age and independent seed/emitter/spawn identity lanes during extraction; no extra per-particle appearance arrays or clocks are needed. Later native behaviors retain this boundary.

The contiguous record plus optional-stream layout is the chosen baseline. Fixed typed structure-of-arrays kernels remain a measured optimization candidate. No claim of SoA, SIMD, or threading speedup is made before representative Crimson measurements.

### 3.5 Time, randomness, and lifecycle

Use 60 Hz simulation with previous/current presentation interpolation. Keep remainder time, bound live catch-up, and advance effect time only for executed ticks. A scheduled burst crosses its time boundary once. Integrate a newborn only for the portion of its birth tick during which it exists.

Derive random samples from retained effect seed, stable emitter identity, spawn ordinal, and independent property/module channels. Compaction, rendering order, and an unrelated module must not change another particle's random samples. Repeatability is within the same implementation, validated definition, parameters, and external inputs; it is not a cross-platform bitwise or CPU/GPU guarantee.

Track running, paused, draining, and completed behavior. Step 3 one-shot emission ends at its finite duration, and completion waits for its live particles to drain; the editor shows the result and Play can replay it. The editor's Stop clears immediately. Completion will additionally account for pending events and children when those features arrive. Forced clear/destruction does not generate death events.

Long prewarm/seek work replays fixed ticks incrementally with progress and cancellation. The isolated preview holds its definition, parameter values, transforms, and collision inputs fixed for a replay. Seeking does not promise to reconstruct an arbitrarily edited scene history.

Budgets are separate: live particles, spawning, effect instances, replay ticks, collision queries/impacts, trail samples, event records, and child generation. Clip or reject excess work deterministically, retain existing valid state, and report the reason. Introduce each bound with the feature that creates the cost, rather than postponing correctness to Step 14.

### 3.6 Rendering and color

CPU simulation does not imply CPU-expanded sprite geometry. Use shared static quads and extracted per-particle instance attributes through the pinned bgfx API. Step 4 uses a 48-byte GPU instance layout containing position/size, linear RGBA and rotation radians in `i_data2`; the vertex shader rotates the quad in its camera-facing plane. Size/rotation stay outside CPU motion. Conservative selection/culling bounds expand by maximum authored size's half-diagonal, and preview Frame retains a safe appearance extent. Check instancing and buffer availability, chunk submissions within limits, and report omitted work. Ribbons/beams use bounded variable geometry; mesh particles reuse managed geometry/material resources.

Draw opaque/cutout mesh particles with scene depth writes. Transparent particles depth-test against opaque geometry without writing depth. Sort their visible presentation packets back to front across emitters and renderer types, with stable ties. Alpha and additive packets interleave when foreground smoke must cover emission. Batch only consecutive compatible packets; regrouping by emitter or texture must not destroy ordering. Center/segment sorting cannot perfectly order crossing meshes or strips; record that limit.

Keep the sequence **linear HDR scene/particles -> exposure and optional Glow -> selected tone mapper and display-matte composition -> neutral grid -> sRGB viewport -> ImGui**. The planning baseline's `fs_present.sc` used a binary alpha threshold for empty background. Step 1 introduced continuous coverage with an inverse-Reinhard background term. Step 2 replaces that term with an exposure-independent display matte so partially transparent dark smoke also attenuates white backgrounds.

Use unexposed premultiplied color `C` and continuous covered fraction `c` in a validated, blend-capable `RGBA32F` target. Clear both to zero; opaque/cutout writes coverage one. Keep `RGBA16F` for the tone-mapped linear display/grid target. For source radiance `L` and final opacity `a`:

```text
Alpha:    C_next = a * L + (1 - a) * C
          c_next = a + (1 - a) * c
Additive: C_next = a * L + C
          c_next = c

E = scene exposure
B = linear editor background in [0, 1], including exact white
H = E * max(C, 0)
G = GlowEnabled ? Strength * blur(softThreshold(H)) : 0
Display = toneMap(H + G) + (1 - clamp(c, 0, 1)) * B
```

Alpha blending uses `ONE / INV_SRC_ALPHA` for RGB and coverage. Additive uses `ONE / ONE` for RGB and `ZERO / ONE` for coverage. Apply exposure once before optional Glow extraction, tone map once, then add uncovered display-linear background. With Glow off and the legacy Reinhard selector, `toneMap(H) = 1 - 1 / (1 + H)`. Exact white requires no inverse or epsilon convention. Float32 accumulation and Glow scratch preserve supported HDR headroom; validate target support and finite supported input ranges. Clamp the display gamut during final encoding.

This compositor is Crimson's chosen adaptation for its editor contract, not an equation attributed to the reference engines. Verify empty background at exposures 0/1/100, opacity approaching zero, additive emission, opaque-only regression, alpha over emission, high overlap, and preview/scene consistency.

The retired inverse-Reinhard background convention kept 0.5/0.99-opacity black particles white over a white matte. The Step 2 convention resolves zero-radiance smoke to `(1 - opacity) * background`. Tone mapping the combined premultiplied HDR color once is an approximation to independently tone-mapping and compositing each layer: intense transparent emission can saturate at display white. It does not promise exact per-surface display blending. That limitation is explicit; retain linear scene accumulation, one tone map/sRGB encode, and exposure-independent uncovered background/grid/tools.

Step 8a makes opaque depth readable and copies positive camera-space depth into a separate `R32F` texture while the source depth is detached from depth-stencil output. Transparent rendering then attaches the original depth for hardware testing and samples the copy. Merely removing `RT_WRITE_ONLY` is not sufficient to sample a depth attachment simultaneously through the pinned D3D11 path. Reconstruct depth using the actual projection conventions, and multiply both opacity and premultiplied color by the soft fade. Each preview/viewport owns compatible targets and ordered views.

Soft intersections are a visual effect against opaque scene depth. They are not physical collision or transparent-particle interaction. Start with full-resolution rendering. Reduced-resolution smoke, weighted blended OIT, tiled rendering, and GPU sorting require later evidence and their own image-quality checks.

### 3.7 Jolt collision foundation

The current engine already has a pinned Jolt dependency and a concrete Play/static-Box CPU physics boundary. Step 9 extends or reuses that owner for particle static queries and the required collider shapes rather than introducing a second dependency/world implicitly. Recheck the pinned port, CMake target and current physics contracts when that step starts. Keep Jolt types private behind engine-owned queries and explicit lifetime; gravity/motion stay in the particle subsystem.

Entities opt in through static collider components and layers. Support simple static primitives and opted-in model triangle geometry. Synchronize affected colliders at a defined tick boundary; rendering all models does not automatically make them physical surfaces. The particle editor's floor/wall fixtures belong to its isolated query world.

Sweep a sphere from previous world position to candidate world position, with radius separate from sprite size. Handle initial overlap, earliest hit, separation, and bounded remaining-step impacts. Expose bounce, stop, and kill responses, restitution/friction, and filtering. World-space query results convert explicitly when simulation is local. Particles and mesh visuals do not acquire one Jolt rigid body each.

Primitive collider transforms must be representable by their actual Jolt shape. Validate them before publication: an orthogonal box transform may use supported nonuniform scale, while sphere/capsule dimensions require their supported uniform-scale representation. Shear or unsupported scaling/reflection must produce an adjacent diagnostic and suspend that collider; never silently discard the affine part or replace it with an approximate TRS shape. Preserve the entity matrix and allow correction or switching to a suitable model collider.

For static models, preserve entity and imported-node affine transforms. Bake their linear transforms into original triangle vertices, retain body translation, and correct winding under reflection. Degenerate/unusable transforms receive a diagnostic. Translation-only changes can move a body; changed baked geometry/linear transform recooks the affected shape. Request and pin original CPU geometry independently of camera/render LOD; visual LOD changes never change collision.

Tag prepared results by project/document/source revision and discard stale publications. Retain working collision resources during failed replacement where possible. Physics/query work must not mutate EnTT through callbacks. Dynamic bodies and character movement remain separate milestones.

### 3.8 Future GPU boundary

Keep definitions as typed behavior/settings/curves and bindings independent of CPU storage. Do not encode C++ callbacks, Jolt objects, bgfx handles, or generated shader source as persistent effects.

A later backend may use counter reset, emission, simulation/compaction, alive/dead indices, explicit synchronization, GPU sorting, and indirect drawing. The pinned bgfx compute/instance example establishes API feasibility, not completed particle infrastructure or measured speed. Extend shader compilation/reload stage support only when compute is actually implemented.

Publish a supported-module/capability matrix. Jolt queries, CPU events, child spawning, seeking, and repeatability need explicit GPU implementations or CPU retention. Camera-dependent depth collision is an optional visual approximation, not a transparent replacement for world collision. No automatic CPU/GPU feature or seeded-result parity is promised.

## 4. Detailed plan of action

All handoffs include relevant CPU/persistence checks, one integration build after owners finish, and a short feature exercise for the user. Update implemented status and README controls only as functionality lands. Each later step retains the earlier assets, scenes, and workflow.

| Step | Useful result at handoff |
| --- | --- |
| 1 | Create, edit, preview, save, reopen, and place a gravity effect |
| 2 | Author textured smoke and additive embers |
| 3 | Author bursts, sprays, and varied motion through simple behavior controls |
| 4 | Shape appearance over lifetime |
| 5 | Compose and retime layered effects; inspect moments through replay |
| 6 | Author animated sprites and fast stretched sparks |
| 7 | Reuse one effect through exposed instance controls |
| 8a / 8b | Separately soften intersections and enable existing-light response |
| 9 | Bounce, stop, or kill particles on authored static colliders |
| 10a / 10b / 10c | Separately add trails, ribbons, and endpoint beams |
| 11 | Emit static mesh debris |
| 12 | Trigger secondary emitters from particle events |
| 13a-13e | Independently add specialized motion and emission |
| 14 | Measure dense scenes and improve authoring diagnostics/presets |

### Step 1: Complete gravity-effect workflow

Implemented in Step 1: native version-1 effect assets, independent effect documents/history, one constant point emitter, procedural unlit additive sprites, world-space 60 Hz CPU playback, isolated preview in a floating multi-viewport editor, scene placement/markers/live focus bounds, scene version-3 persistence and typed reference/deletion handling. Later steps retain this workflow while extending the saved formats; current effects write v7 and scenes v5, with older readers preserved. The starter still begins at rest and falls. Current limits are 65,536 configured particles per emitter, 256 scene instances, 262,144 aggregate configured scene slots (plus at most one 65,536-slot transactional restart/capacity replacement reservation and separate reusable presentation buffers), eight catch-up ticks per frame, and 65,536 rendering packets per view plus available GPU transient capacity. Diagnostics report omitted work and particles cleared by capacity changes. The application suspends simulation when all editor windows lose focus or none can draw, and skips elapsed suspension time on resume. Close the effect document before deleting that asset through the global deletion transaction. These are implementation limits and workflow behavior, not measured performance guarantees. The acceptance exercise below remains for the user.

**Goal:** a small, complete particle authoring feature, usable without any later step.

Implementation actions:

- Add native effect identity/load/validate/save and browser create/open actions; begin with one enabled point emitter and the starter constants in Section 2.1.
- Add the dedicated effect document, transactions, dirty handling, Save/Save As, reopen, and focused Save/Undo/Redo routing.
- Add the bounded CPU instance/storage/tick owner and procedural unlit additive instanced sprites. Land required programs, HDR/background correction, targets, and resource release here.
- Add an isolated preview with Play/Pause/Restart/Stop, retained seed, elapsed time, live/capacity counts, and basic failure/drop diagnostics.
- Add typed scene references, placement, an emitter marker/bounds for selection/focus, transforms/hierarchy participation, duplicate/delete/undo, scene save/reopen, automatic start, and selected **Play Effect**.
- Add scene -> effect reference handling and document-generation reconciliation. Do not include external textures, shape libraries, curves, multiple emitters, seeking, or Jolt.

Acceptance: create the effect, change gravity and observe the fall, undo the edit, save/reopen, place two independently playing instances, save/reopen the scene, and duplicate/delete/undo an instance. Moving one instance and editing an unrelated entity preserve playback. Change color/size while paused and confirm immediate appearance changes with unchanged time/live identities. Resume and confirm gravity changes affect live motion without restart; verify velocity/lifetime changes only affect later births. Capacity replacement clears live particles while retaining time and Pause/Play state. Drag the floating editor onto another monitor, navigate its preview without moving the scene camera, and close it through the normal dirty-document flow. Preview actions remain transient. Saving and failure paths retain prior files and working state. Opaque-only rendering, additive particles over empty space, exposure extremes, resize, and shutdown remain correct.

Focused checks: seeded restart, emission/lifetime integration, fractional birth time, compaction identity, bounded capacity/catch-up, asset round-trip/failure, scene round-trip/history, and resolve equations. This step's broad integration serves one deliberately small emitter, not the whole particle system.

### Step 2: Particle materials, textured sprites and transparency

**Goal:** author recognizable smoke, dust, and embers with ordinary managed images.

Implemented baseline: material-version-2 unlit Alpha/Additive settings with version-1 PBR compatibility; effect-version-2 material references and opacity with Step 1 migration; independent native material documents; ordinary compatible material pick/drop/clear assignment and Edit Material; creation in the shared Asset Browser; material-owned texture/sampler/color controls and a floating live 3D editor; pinned coherent texture bindings; global stable back-to-front alpha/additive ordering and consecutive compatible batches. The material editor also authors native Lit PBR Opaque/Mask profiles and creates native assets through explicit Save As from read-only imported originals. Particle compatibility remains Unlit Alpha/Additive; imported-original editing, particle lighting, shader graphs, flipbooks and additional emission shapes remain outside this step. Per view, material entries are limited to 512 and particle submissions to 1,024 draw calls in addition to the existing packet/transient limits; omissions are diagnosed.

Implementation actions: extend the existing material envelope and dependency/resource owners; add minimal sprite material authoring and independent document actions; prepare material texture slots without fabricating model assets; add effect -> material -> texture scanning/replacement/deletion; preserve live simulation under appearance changes; implement premultiplied output and ordered batching; replace the old background convention with the display matte described in Section 3.6. Clearing a texture returns to procedural sprites, and clearing a material selects the protected default.

Acceptance: create a material in the Asset Browser, assign it through the effect's picker or drop field, edit its texture/color/blend, and save/reopen/undo through its own document. Switch material preview meshes, move the editor onto another monitor and use its Quader mini-viewport gestures; HDRI/intensity/rotation/background changes remain transient and leave scene lighting intact. Existing sampler settings survive ordinary edits until Filtering is explicitly selected. Assign/drop/replace/clear smoke imagery, overlap two effects, and view particles over empty background and opaque/cutout models. Diagnose missing textures without losing the assignment. Smoke can cover sparks behind it, and partial dark opacity visibly attenuates dark/mid-grey/near-white/white editor backgrounds. Complete any necessary resolve adjustment described in Section 3.6 within this handoff. Check sorting order and alpha/coverage equations through CPU data tests; visual acceptance belongs to the user.

### Step 3: Common emission and motion

**Goal:** build fountains, one-shot impacts, and directional sprays without bespoke code.

Implemented: Cone disk, Sphere volume and Box full-dimension emission alongside Point; Constant/Random Range lifetime and speed; combined Direction/Speed/Spread/Flatness controls and optional Outward launch; editable seeded variation; Continuous/One shot/Looping playback, delay once, finite duration and stable bounded burst records; Gravity/Acceleration/Drag add/enable/remove controls; World/Local simulation. The editor presents fixed sections and simple optional behaviors, while the engine owns valid execution order. Positions and launch velocity honor the full affine emitter transform; gravity/acceleration use the chosen simulation space and sprite size remains in world meters. This step introduced effect v4; Step 4's v5 retains its compatible launch behavior and references without rewriting files on load or converting merely displayed controls.

Supporting work: prepare burst order before execution, resolve shared sampling/forces once per tick, integrate newborns for the remaining fraction of their birth tick, use independent random channels keyed by seed/emitter/spawn identity, drain one shots to completion, and bound each tick to 4,096 scheduled births and 32 scheduling segments. Burst authoring allows at most 64 records with unique nonzero identities, finite nonnegative time and counts 1–65,536; finite cycles require time strictly below duration. Edits to seed, space, timing or bursts replay from zero with Pause/Stop preserved; appearance, birth inputs and force edits follow Section 2.4. Jolt particle queries, curves, multiple emitters and timeline controls remain outside this step.

Acceptance: tune a fountain with Cone, upward Direction and Speed, a small spread and negative Y Gravity. Compare Flatness zero's round spread with Flatness one's fan, then enable Outward and confirm spread/flatness applies around radial launch. Give it Random Range speed/lifetime, Restart twice with the same seed, and save/reopen. Create a One shot impact with rate zero and a time-zero burst; Pause/Resume must not duplicate it, live particles drain, and Play Effect replays it. Make a directional spray with Acceleration and Drag; disable/remove/undo each behavior, then move or parent two instances to compare World/Local motion without affecting the other instance. Space/timing/seed edits replay while paused and retain that paused state; tint/material edits retain clocks and live identities. Earlier assets reopen with their original point/world/continuous values, and viewing legacy launch controls leaves definitions clean until an intentional edit.

Focused checks: shape/direction/range/fan sampling bounds, radial spread, seeded restart, fractional newborn integration, same-time rate/burst ordering, time-zero and delay/loop boundaries, no duplicate paused bursts, one-shot completion, drag/force integration, affine World/Local transforms, stage-specific live edits, scheduling limits/counters, strict v4 validation, v1–v3 migration, unchanged legacy launch display/edit conversion, save/reopen/Save As, cancellation and undo/redo history. These checks and the integration build complete before the handoff; user authoring acceptance follows in the launched app.

### Step 4: Curves and gradients

**Goal:** shape effects over lifetime through direct authoring controls.

Implemented: absolute Constant/Random Range/Curve tracks for Size, retained overall Intensity and Rotation, Constant/Gradient HDR RGBA color/opacity, bounded stable keys/stops, Linear/Constant interpolation, inline editing and coherent renderer/extraction support. Curves use interpolated normalized particle age 0–1; emitter timing stays in seconds. Gradient replaces effect tint/opacity, interpolates canonical linear HDR RGB and retains linear alpha. The HDR follow-up migrates older SDR colors once on load and places intensity inside each color picker. Mode changes retain inactive settings. Existing particles evaluate new appearance without restarting, including while paused; random lanes remain tied to seed/emitter/spawn identity independently of storage order.

Supporting work: effect schema v5 with v1–v4 appearance defaults, strict finite domains and bounded 2–32 keys/stops with ordered ages within [0,1] and minimum spacing; movable/removable edge keys/stops and held values outside the authored interval; immutable prepared segment arrays; editor-only ImPlot and vendored RGBA widgets with one transaction per graph/bar/numeric gesture and Escape cancellation; rotation radians in renderer-owned 48-byte instance data and camera-plane shader rotation; conservative bounds/Frame based on maximum authored size. Earlier v5 tracks using ages 0 and 1 retain their appearance, and loading does not rewrite them. Size curve/range values may be zero to hide a sprite without killing its simulation record. No per-particle appearance arrays, new reference type, node graph, multi-emitter runtime or timeline is introduced.

Acceptance: assign an Alpha smoke material, make Size grow through a Curve and fade RGBA opacity to zero through Gradient. Pan/zoom and Frame the curve plot, double-click to insert a key, and change first, interior and last keys/stops using widget and numeric controls. Move edge ages away from 0/1, confirm held values outside their interval, and remove an edge while more than two remain. Each gesture must undo once, Escape restores prior values, and Save/reopen retains identities, ages and interpolation. Make sparks shrink and rotate; compare Constant and Random Range appearance without flicker. Pause two placed instances, edit appearance and confirm updated sprites with preserved live identities/clocks; resume independent playback. Switch modes and return to confirm retained data. Existing gravity, spray and burst assets reopen unchanged.

Focused checks: scalar/gradient edge holds and Linear/Constant evaluation; widget pan/zoom/Frame/insertion, selection/drag/numeric edits and removal down to two; sRGB decode/linear color interpolation and linear alpha; random lane independence; interpolated age; live edits/cancellation without replay; rotation extraction and shader attributes; bounds; finite limits/stable key identities; v1–v4 migration and earlier/current v5 round-trip; atomic save failures and transaction history. Integration build and focused results are recorded at the checkpoint above before user acceptance.

### Step 5: Layered effects and timeline

**Goal:** combine flash, sparks, and smoke in one reusable timed effect.

Add multiple emitters, duplication/rename/enable/removal, timing tracks, numeric equivalents, and burst markers. Selection consistently identifies the emitter being edited. Track ordering is presentation order, not a hidden simulation priority. Add Step and isolated-preview seeking through bounded replay with progress/cancellation.

Acceptance: layer three emitters, delay smoke, change duration, and save/reopen. Retiming is undoable, unaffected emitters remain intact, and earlier single-emitter assets still work. Restart/seek to a selected moment is repeatable under fixed inputs; long seeks remain responsive. Check schedule boundaries, stable identities, history, and cancellation/stale replay.

### Step 6: Flipbooks and stretched sprites

**Goal:** animated fire/smoke and readable fast sparks using the existing sprite path.

Add sprite-sheet dimensions/frame selection, animation speed, randomized starting frame, and compatible frame interpolation; add velocity-stretched and axis-constrained orientations. Preserve UV/color-role handling and constrain invalid frame/grid values through normal controls.

Acceptance: assign a flipbook and animate it, vary its start frame, then stretch moving sparks. Zero velocity has a stable orientation, render-rate variation does not change simulation, and settings survive save/reopen/undo. Check frame addressing, time/age evaluation, zero-velocity behavior, and extraction bounds.

### Step 7: Exposed parameters and reuse

**Goal:** customize selected instances while sharing one effect definition.

Add **Expose as Parameter** to compatible fields, a typed default/binding definition, and scene overrides with reset-to-default. Support scalar/integer/bool/vector/color fields as needed by delivered modules; avoid a universal property override system. Resolve stable IDs once during instance preparation, validate types, and apply overrides by execution stage.

Acceptance: place two uses of one asset with different tint/strength/emission. Selected-instance edits do not alter the shared definition; shared definition commits update its uses. Save, duplicate, undo, parameter rename, removal/type incompatibility, and reset preserve authored identity with diagnostics. Check binding resolution and override application.

### Step 8a: Soft intersections and near-camera fading

**Goal:** remove hard sprite intersections without introducing physics.

Implement sampleable opaque depth, the detached-source linear-depth copy pass, compatible preview/scene targets, and distance/softness controls. Hardware depth testing remains enabled and particle depth writes remain disabled. Fade both premultiplied radiance and opacity; zero softness returns the ordinary path.

Acceptance: smoke approaches a floor/wall cleanly, remains occluded behind it, and near-camera fading behaves as authored. Save/reopen and undo retain those settings. Vary camera distance, clip planes, and viewport dimensions; no particle path samples its currently attached depth output. Check depth reconstruction/fade equations and resize/resource lifetimes. Jolt is not a dependency of this step.

### Step 8b: Optional lighting

**Goal:** opt an effect into existing scene light response while retaining simple unlit authoring.

Add compatible lit sprite modes using existing directional/point lights and diffuse IBL, plus normal-texture controls where supported. Apply the same lighting path to strips when Step 10 introduces them. Keep intensity/emission separate from opacity and use the existing color contract. Reuse engine PBR conventions for mesh particles when Step 11 lands rather than introducing another BRDF.

Acceptance: light/environment changes affect lit effects predictably; unlit mode restores their authored appearance. Normal assignments are typed linear data. Save/reopen and mode switching remain stable. Shadows, volumetric scattering, particle-generated lights and material graphs retain separate scopes.

### Step 9: Jolt static collision

**Goal:** make particles interact with explicitly authored static scene surfaces.

Extend the existing pinned Jolt/static physics owner with engine-owned particle queries and the required primitive/model collider authoring, layers, scene persistence/history, and isolated floor/wall fixtures. Retain the implemented Play/character collision contract. Add swept collision radius, bounce/stop/kill, restitution/friction, and filtering. Cook original model geometry with affine hierarchy/reflection handling, tag asynchronous preparation, and update only affected colliders.

Acceptance: fast sparks hit thin walls; primitives, filtering, initial overlap, and repeated corner hits stay bounded. Unsupported affine primitive transforms are diagnosed without changing the authored entity matrix. Imported mirrored/sheared hierarchy geometry aligns with model collision; visual LOD has no effect. Collider delete/undo, transform edits, missing sources, failed cooking, and stale completion are safe. Check queries/responses, affine cooking, revision synchronization, and persistence.

### Step 10: Trails, ribbons, and beams

Deliver these as three separate usable handoffs, retaining prior effects:

| Substep | Actions and useful result | Acceptance |
| --- | --- | --- |
| 10a: Particle trails | Add bounded history associated with stable particle identity, width/color/opacity along history, distance UVs, and reset/drain behavior; draw a moving projectile trail | Death and compaction do not reconnect unrelated histories; stationary samples and teleports produce stable/disconnected geometry |
| 10b: Connected ribbons | Add explicit ribbon groups/order and compatible appearance controls; author a connected flowing strip | Birth/death, gaps, local/world motion, save/reopen, and resets preserve intended connections within bounded geometry |
| 10c: Endpoint beams | Add start/end authored parameters and bounded segment rendering through the particle/ribbon path; author a beam between chosen points | Coincident endpoints, moving endpoints, exposed bindings, resize, and reset are stable |

An emitter may feed supported sprite and trail renderers from the same state. Chunk geometry within buffer/index limits, preserve transparent ordering, and diagnose dropped segments. No separate beam-entity system, spline editor, or general VFX graph is introduced.

### Step 11: Static mesh particles

**Goal:** emit recognizable static model debris sharing managed resources.

Add a typed static-model renderer, per-particle orientation/scale, supported imported materials/hierarchy, and model dependency handling. Opaque/cutout draws join the opaque path; transparent particle modes join ordered particle rendering. Share model geometry/resources and preserve inverse-transpose normal/reflection behavior. Collision continues to use simple query shapes.

Acceptance: many particles share one imported model and preserve hierarchy/material appearance. Save/reopen and undo retain model assignments and renderer settings. Replacement/missing assets, unsupported source features, and failed preparation retain diagnostics/working resources. Check transforms, bounds, dependencies, and bounded primitive/draw multiplication. Skeletal particles and rigid-body debris remain separate work.

### Step 12: Child-emitter events

**Goal:** a particle can trigger a secondary emitter without manual duplicate effects.

Add birth/collision/death sources, target-emitter references, selected position/velocity/color inheritance, and copied event payloads. Consume events after the producing update stage. Validate source/target identities and reject cyclic authored event links; separately cap records, child instances/generation, and resulting spawning. Never recursively mutate storage while iterating it.

Acceptance: collision produces a puff and death produces a secondary burst exactly as authored. Save/reopen and undo preserve triggers, target links, and inheritance settings. Pause/Restart/seek are repeatable under fixed inputs; pending children count toward completion. Disabled targets retain authored links, missing targets are diagnosed, and excessive fan-out is bounded and counted. Check event ordering/payloads, cycle validation, limits, and forced-clear behavior.

### Step 13: Specialized motion and emission

Add the following in order, with each row a separate saveable, demonstrable handoff rather than a combined advanced-system gate:

| Substep | Capability and authoring goal | Acceptance |
| --- | --- | --- |
| 13a | Procedural turbulence/curl noise for irregular smoke and magical motion | Seeded behavior, frequency/strength/time controls, spaces, and disabling the module are predictable and finite |
| 13b | Attraction, vortex, and orbit behavior for focused/rotating effects | Center/axis/radius inputs have clear units; coincident/zero-radius cases stay finite |
| 13c | Inherited emitter velocity and distance-based emission for moving effects | Movement distributes births along the path; teleport/reset does not create an unintended giant burst |
| 13d | Additional shape options such as line, disc/ring, and capsule | Distribution follows its displayed parameters and transforms; invalid extents are diagnosed |
| 13e | Static mesh-surface emission for effects conforming to imported geometry | Sample valid triangles with documented local-area weighting; respect imported hierarchy and manage CPU geometry independently of visual LOD |

Each addition has a small demonstration asset, focused sampling/motion checks, undo, and round-trip persistence. External vector-field assets, skeletal surfaces, and unusual specialized rendering are not prerequisites; extend their scope separately if later effects need them.

### Step 14: Dense-scene profiling and authoring aids

**Goal:** measure and diagnose many complete effects, then improve the actual bottlenecks.

Expand views for effect/emitter state, live/max counts, dropped work/reasons, resolved parameters, bounds, collision queries, simulation time, sort/extraction time, upload bytes, draw calls, and overdraw. Add curated editable starter effects using delivered features. Basic limits and counters already exist from their introduction steps; this milestone improves visibility and capacity rather than retroactively fixing unbounded work.

Measure warmed Release workloads at 1k, 10k, and 50k live particles: simple motion, many curves/forces, collision-heavy sparks, trails, churn/lifetime variance, many small emitters, and a few large smoke layers. These are workloads, not guaranteed capacities. Record hardware, settings, memory, median/tail times, and end-to-end frame contribution.

Compare the adopted layout with fixed typed SoA only on the same real stages, including extraction/conversion costs and matching outputs. Add SIMD/threading/alternative sorting or rendering only for a demonstrated bottleneck and remeasure. Rendering overdraw may dominate even when CPU simulation is cheap.

Acceptance: supported capacity/overflow remains bounded and explainable; old effects remain compatible; scene switching, asset revision changes, and shutdown release transient state. Report actual measurements and residual limits. A GPU backend requires its own implementation plan/capability review after these results.

## 5. Research and decision log

The following primary projects, original articles, conference slides, and papers were inspected during planning. The inspected version/date identifies the evidence; it is not a request to upgrade or add those engines as dependencies. Attribute published results to their original workload. The selected architecture combines evidence with Crimson-specific design decisions.

### R1. Godot CPU particle implementation

[Godot 4.5.2 CPU implementation](https://github.com/godotengine/godot/blob/4.5.2-stable/scene/3d/cpu_particles_3d.cpp) and [state definitions](https://github.com/godotengine/godot/blob/4.5.2-stable/scene/3d/cpu_particles_3d.h), inspected at the release tag. The [release announcement](https://godotengine.org/article/maintenance-release-godot-4-5-2/) dates that version to 19 March 2026.

Evidence: contiguous particle records, initialization/update separation, fixed-FPS remainder, seeded restart, and fractional birth integration. Adopt those timing/storage lessons. Crimson adds stable spawn identity under compaction and explicit presentation interpolation. Godot's CPU implementation is hard-coded, not a reorderable module system; its synchronous pre-process informs the ready-before-first-draw startup correction. The later timeline/seek plan retains a separate bounded replay policy. The [process-material generator](https://github.com/godotengine/godot/blob/4.5.2-stable/scene/resources/particle_process_material.cpp) generates GPU shaders and must not be cited as CPU feature parity.

Follow-up inspection on 14 October 2026 used the user's local `godot-master` checkout at `C:\Users\Drako\Desktop\_QUADER_DEV\resources\_SOURCE_CODE\godot-master`, identified by `version.py` as 4.7 beta. In `scene/3d/cpu_particles_3d.cpp`, ordinary color/parameter/gravity/seed setters assign values without calling `restart()`. Color and scale are reevaluated for live records; gravity enters the next update, while initial speed/direction/spawn shape initialize births. `set_amount()` reallocates and marks particles inactive without resetting time/cycle/remainder; explicit `restart()` clears those scheduling values. The editor's Restart Emission command calls restart explicitly rather than restarting every Inspector property change. Godot CPU gravity is integrated directly and does not use a physics server or Jolt. These are verified CPU behaviors, not claims about its GPU backend.

The same local CPU source was rechecked for pre-process. `_set_emitting()` calls `_update_internal()` immediately at time zero specifically to avoid a rendering delay; entering the tree and becoming visible use the same first-update rule. `restart()` resets time, cycle, remainder and live flags, then enters that startup path. `_update_internal()` completes the full pre-process interval in a synchronous loop, using fixed FPS or a 30 Hz fallback and a final shortened step, temporarily ignoring speed scale, before preparing the particle buffer for draw. Crimson now adopts this ready-before-first-draw ordering after user acceptance found the across-frame implementation left an empty interval. Its 60 Hz tick quantization, 60 s authoring bound, explicit retained seed, initial-transform contract and document replay policy remain Crimson choices. Larger values/capacities can have upfront cost. This source does not establish GPU backend behavior or implement the later timeline-seek feature.

Crimson adapts that behavior to its separate particles/s emission rate: lifetime is cached at birth and affects only new particles, whereas Godot also uses emitter lifetime in its global cycle scheduling. Color/size/intensity extraction, transactional immutable revisions, stable IDs under compaction, preserved paused/stopped state and cleared-capacity counters are Crimson choices. Step 3 deliberately replays seed, simulation-space, timing and burst edits to keep its schedule and repeatability coherent; that policy differs from the inspected Godot seed setter and is a Crimson adaptation. The local inspection informs the field policy in Section 2.4; it does not establish feature parity or a shared backend.

### R1c. Materials and particle appearance

[Godot CPU particle color/material documentation](https://docs.godotengine.org/en/stable/classes/class_cpuparticles3d.html#class-cpuparticles3d-property-color) verifies that CPU particle color is consumed by the drawn mesh's BaseMaterial3D or ShaderMaterial. The inspected local `cpu_particles_3d.cpp` selects mesh materials/material overrides; it does not put gravity into the surface material. [Godot's process/draw explanation](https://docs.godotengine.org/en/stable/tutorials/3d/particles/creating_a_3d_particle_system.html) distinguishes GPU simulation process materials from draw-pass surface materials. [Niagara's sprite renderer reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-module-reference-for-niagara-effects-in-unreal-engine?application_version=5.6) verifies a renderer Material assignment with particle color/other attribute bindings.

Crimson adapts that separation through the existing native MaterialAsset envelope: simulation and per-particle values remain effect-owned, textures/blending are material-owned, and resource preparation/render submission stay in their existing owners. The unlit-only Step 2 sprite profile, procedural default, ordinary asset-reference assignment, Asset Browser material creation, separate material history and supported-mode validation are Crimson decisions, not claims of feature parity. A material graph and GPU process-material architecture are not required for this CPU slice.

### R1d. Curve and gradient authoring widgets

[ImPlot 1.0](https://github.com/epezent/implot/tree/v1.0) supplies the Dear ImGui plot surface and interactive axes. Crimson uses the version selected by its pinned vcpkg manifest for editor curve pan/zoom and drawing; key insertion/selection, valid numeric edits, Frame behavior, undo transactions and cancellation remain Crimson responsibilities. The dependency does not evaluate runtime particles or change asset ownership.

[Coollab `imgui_gradient`](https://github.com/Coollab-Art/imgui_gradient/tree/6fcb6b814839e2adb6d41edc9b27e42a02114c83), pinned to commit `6fcb6b814839e2adb6d41edc9b27e42a02114c83` (30 January 2026), supplies the vendored RGBA stop widget under BSL-1.0. `third_party/imgui_gradient` retains its source, license and patch notes. Crimson's patch uses linear RGB interpolation, straight linear alpha and left-held Constant interpolation to match particle appearance; the editor adapter enforces stable IDs, ordered ages, a two-stop minimum and 32-stop maximum. Freely movable edge ages with held outside values, document history and per-gesture Escape are Crimson workflow decisions. These widget choices establish no simulation-capacity or performance guarantee.

### R1e. HDR colors and environment Glow

The inspected local Godot 4.7-beta source stores unbounded floating-point Color values and exposes intensity as a color-picker decomposition: RGB decodes to linear, multiplies by `2^EV`, and returns to the editor color representation while alpha remains independent. Gradient stops retain complete colors; Environment owns Glow. Crimson adopts per-color brightness and environment post-processing but stores canonical linear HDR directly, retains its existing Gradient-replaces-constant contract and adds bounded validation rather than copying Godot's internal storage convention. [Godot's Environment/Glow documentation](https://docs.godotengine.org/en/stable/tutorials/3d/environment_and_post_processing.html#glow) describes that separation.

[Unity's HDR picker reference](https://docs.unity.com/en-us/engine/6000.6/manual/materials-and-shaders/graphics-color/hdr/hdr-color-picker-reference) documents HDR color intensity/exposure. Its inspected [VFX Graph gradient property](https://github.com/Unity-Technologies/Graphics/blob/a7e4c051d256a781ab362c64316b125a1e104694/Packages/com.unity.visualeffectgraph/Editor/GraphView/Views/Properties/GradientPropertyRM.cs) explicitly enables HDR and linear color space. This does not establish identical behavior for the classic Particle System: the inspected [Color Over Lifetime editor](https://github.com/Unity-Technologies/UnityCsReference/blob/5217f03a26ec2bffc0883fb4d2430100c5d2538c/Modules/ParticleSystemEditor/ParticleSystemModules/ColorModuleUI.cs) passes HDR disabled to its gradient field. Crimson deliberately exposes HDR on every stop.

[Unreal's Niagara light-effect example](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-particle-effects-that-emit-light-in-niagara-for-unreal-engine) uses above-one particle RGB and a distinct Light Renderer; its [post-process documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-effects-in-unreal-engine) places bloom in post-processing. Crimson adopts the separation between HDR source color, visible halo and actual scene illumination. Its shared three-pass FP32 Glow implementation, explicit coverage handling, compatibility defaults, preview settings and retained legacy multiplier are Crimson-specific choices, not engine feature-parity claims.

### R2. The project's pinned bgfx CPU example

[Particle helper](https://github.com/bkaradzic/bgfx/blob/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/common/ps/particle_system.cpp) and [example 32](https://github.com/bkaradzic/bgfx/blob/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/32-particles/particles.cpp), at bgfx commit `7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099`. This is the inspected upstream snapshot of installed `1.129.8940-496#1`; the [wrapper release](https://github.com/bkaradzic/bgfx.cmake/releases/tag/v1.129.8940-496) dates to 24 November 2025.

Evidence: bounded emitter arrays, compact death removal, fractional emission, separate update/render entry points, and global particle sorting. Adopt contiguous storage and independent render indices. Use instanced quads/reused scratch rather than inheriting expanded-quad/index limits, one-atlas assumptions, per-render allocations, or the example's global context. This sample does not supply the native asset/editor architecture.

### R3. Production cache traversal

[Joost van Dongen: How we made particles twice as fast through cache optimisation](https://joostdevblog.blogspot.com/2020/03/how-we-made-particles-twice-as-fast.html), 1 March 2020, original author article.

Evidence: pooled allocation did not prevent poor locality when shuffled pointers drove traversal. Adopt sequential value traversal and independent transparency indices. The reported speedup belongs to that engine; ring storage/lifetime assumptions do not establish Crimson performance or prove SoA is always better.

### R4. CPU effect composition and authored values

[Alex Tardif: CPU Particle Systems](https://alextardif.com/Particles.html), original author engineering article.

Evidence: system/emitter/particle separation, bounded pools, keyed values, variation, and shared output handling. Adopt authored curves and explicit limits. Avoid copying singleton/manager hierarchies or thread/pool assumptions. Its emitter-only depth sorting cannot interleave overlapping emitters, so Crimson chooses particle/presentation ordering instead. Historical measurements are context, not acceptance targets.

### R5. Data-oriented module requirements

[Milo Yip: A Data-Oriented Programming Paradigm for Optimal Performance](https://media.gdcvault.com/gdcchina14/presentations/833779_MiloYip_ADataOriented_EN.pdf), GDC China 2014, Tencent. Inspected slides include TAG Prestige's declared particle attribute requirements and SoA tradeoffs.

Adopt declared module data/stages and batch-oriented execution. Do not adopt a generic dynamic table solely because it supports an advanced particle architecture. Crimson starts with a concrete compact record and typed optional streams; SoA/conversion costs require measurement on its actual workloads.

### R6. Batch execution and scripting costs

[Niklas Frykholm: Scripting Particles - Getting Native Speed from a Virtual Machine](https://media.gdcvault.com/GDC2014/Presentations/Frykholm_Niklas_Scripting_Particles_the.pdf), Bitsquid, GDC 2014, original slides.

Evidence: data-wide execution amortizes dispatch but has storage/load tradeoffs. Adopt native staged operations over batches and pre-resolved bindings. This does not require Crimson to implement the presentation's language, bytecode compiler, VM, SIMD backend, or its historical performance assumptions.

### R7. Fixed-step presentation

[Glenn Fiedler: Fix Your Timestep!](https://gafferongames.com/post/fix_your_timestep/), 10 June 2004, original author article.

Evidence: fixed-step accumulation, retained remainder, interpolation, and overload from unlimited catch-up. Adopt the engine's 60 Hz clock and separate live catch-up from preview replay. Fixed steps alone do not guarantee bitwise portability, event limits, or replay of a changing external world.

### R8. Niagara modules, events, and scale

[System and emitter module reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/system-and-emitter-module-reference-for-niagara-effects-in-unreal-engine?application_version=5.6), [events and handlers](https://dev.epicgames.com/documentation/en-us/unreal-engine/events-and-event-handlers-in-niagara-effects-for-unreal-engine?application_version=5.6), and [scalability best practices](https://dev.epicgames.com/documentation/en-us/unreal-engine/scalability-and-best-practices-for-niagara?application_version=5.6), Epic UE 5.6 documentation.

Evidence: ordered execution contexts, lifecycle distinctions, emitter event connections, and separate instance/emitter/particle costs. Adopt stage validation, explicit completion/drain behavior, and cost diagnostics. The documented CPU event mechanism does not imply GPU event support. Crimson's bounded event snapshots and stable IDs are adaptations; a script compiler and data-channel infrastructure are not required.

### R9. Typed instance controls

[Unity Visual Effect component API](https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.0/manual/ComponentAPI.html), VFX Graph 17.x documentation inspected at 17.0.4.

Evidence: typed overrides, play/stop/pause/reinitialization, seed controls, and copied event attributes. Adopt asset defaults with independent instance state and immutable queued payloads. Stable persistent parameter IDs and focused document commands are Crimson decisions. No graph-specific API objects or per-particle string lookup are needed.

### R10. Debugging why an effect is absent

[Dan Bruington and Brittany Hein: Where Are My Particles? Debugging Techniques for VFX in Unreal 4](https://gdcvault.com/play/1027335/Visual-Effects-Summit-Where-Are), FXVille, GDC 2021; [original slide deck](https://media.gdcvault.com/GDC+2021/GDC_WhereAreMyParticles.pdf).

Inspected material covers origins/bounds, counts, parameters, attributes, materials, and scalability causes. Adopt emitter state, counts, bounds, resolved values, and suppression/failure reasons early. It supports useful diagnostics, not a requirement for a large generic capture/debugger system.

### R11. Jolt collision queries and shape constraints

[Jolt 5.5.0 NarrowPhaseQuery](https://jrouwe.github.io/JoltPhysicsDocs/5.5.0/class_narrow_phase_query.html), [shape-cast definitions](https://github.com/jrouwe/JoltPhysics/blob/v5.5.0/Jolt/Physics/Collision/ShapeCast.h), and [mesh shape definitions](https://github.com/jrouwe/JoltPhysics/blob/v5.5.0/Jolt/Physics/Collision/Shape/MeshShape.h).

Evidence: filtered cast/overlap interfaces and triangle/convex shape contracts. Adopt swept query particles and original static triangle geometry. Full-affine baking, persistent entity mapping, bounded impacts, and editor collider synchronization are Crimson integration choices. Mesh particle visuals do not imply swept mesh rigid bodies or dynamic physics.

### R12. GPU feasibility through pinned bgfx

[bgfx example 24-nbody](https://github.com/bkaradzic/bgfx/blob/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/24-nbody/nbody.cpp), at the same pinned snapshot as R2.

Evidence: compute capability checks, ping-pong buffers, direct instance-buffer use, and optional indirect drawing. Keep definitions/state/presentation separate now. Compute tooling, compaction, emission, events, and preview semantics remain future Crimson work; an n-body demonstration is not a complete particle backend.

### R13. Wicked Engine architecture and implementation

[GPU-based particle simulation](https://wickedengine.net/category/devblog/2017/11/07/gpu-based-particle-simulation/index.html), 7 November 2017, original engine article. Compare with the inspected [emitted-particle implementation](https://github.com/turanszkij/WickedEngine/blob/0b4dd9ebe0025a4a6d8f17c52c943c40d96d62a7/WickedEngine/wiEmittedParticle.cpp) and [simulation shader](https://github.com/turanszkij/WickedEngine/blob/0b4dd9ebe0025a4a6d8f17c52c943c40d96d62a7/WickedEngine/shaders/emittedparticle_simulateCS.hlsl) at commit `0b4dd9e` from 17 September 2026.

Evidence: pooled state, dead/alive indices, emission/update phases, counters, and indirect rendering. Adopt explicit state ownership and bounded phases as GPU design guidance. Treat the article as historical; shader generation, skinned emission, and depth-history collision are not CPU prerequisites.

### R14. AMD DirectCompute architecture

[Gareth Thomas: Advanced Visual Effects with DirectX 11 - Compute-Based GPU Particle Systems](https://gdcvault.com/play/1020956/Advanced-Visual-Effects-with-DirectX), AMD, GDC 2014; [original slides](https://media.gdcvault.com/GDC2014/Presentations/Gareth_Thomas_Compute-based_GPU_Particle.pdf). The [GPUOpen reference implementation](https://github.com/GPUOpen-LibrariesAndSDKs/GPUParticles11/blob/4613dc571e9cada8283d06873409ed0ae5f58f9f/gpuparticles11/src/GPUParticleSystem.cpp) is inspected at its 19 January 2016 initial commit.

Evidence: split simulation/render data, emission/update/sort phases, indirect counts, and fill-rate alternatives. Adopt separate cost measurements and vertex-stage sprite expansion. The [collision shader](https://github.com/GPUOpen-LibrariesAndSDKs/GPUParticles11/blob/4613dc571e9cada8283d06873409ed0ae5f58f9f/gpuparticles11/src/Shaders/ParticleSimulation.hlsl) exposes offscreen limitations of depth collision. Do not substitute it for Jolt world queries or assume old hardware timings predict Crimson.

### R15. Fill rate and soft depth

[Iain Cantlay: High-Speed, Off-Screen Particles](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles), NVIDIA GPU Gems 3, 2007.

Evidence: large smoke can be fill-rate limited, lower-resolution rendering trades detail for cost, and depth handling can cause halos/leaking. Adopt overdraw measurement and separate soft-depth treatment. Start full resolution; defer mixed-resolution reconstruction. Historical DirectX restrictions and mixed-blend speculation are not requirements for Crimson's HDR formula.

### R16. Transparency approximation choices

[Morgan McGuire and Louis Bavoil: Weighted Blended Order-Independent Transparency](https://jcgt.org/published/0002/02/09/), JCGT, 19 December 2013; [paper](https://jcgt.org/published/0002/02/09/paper-lowres.pdf).

Evidence: premultiplied coverage, emissive contributions, ordered OVER, bounded-memory approximate OIT, and crossing-geometry limitations. Adopt premultiplication and honest ordering limits; soften both radiance and coverage. Weighted blended OIT changes the compositing approximation and remains a later evaluated alternative. The editor display-matte integration in Step 2 is Crimson's adaptation, not a claim from this paper.

## 6. Checks, handoff, and completion

### Documentation completion

The pre-work document is complete when each step specifies a visible useful result, its supporting changes, acceptance, compatibility obligations, and its position in the sequence. Check research links/attribution, local links, status labels, and the architecture cross-reference. Documentation-only delivery does not require an application build.

### Implementation checks

Use the repository's PowerShell workflow. After implementation owners finish a step, perform one incremental Release integration build and its focused CPU checks. Dependency/preset changes use the configure build alternative; `test` and `configure` are not combined flags. Reuse prior checks rather than growing a broad matrix without a failure or unresolved concern.

Automated checks should exercise meaningful behavior: native round-trip/migration/failure, entity/document history, seed/timing/curves, bounds/capacity, stable identity, query responses, staged events, bindings, render equations/extraction, and resource teardown. Preserve earlier fixtures as new format versions land. Stress workloads must be reproducible and include actual settings/hardware in measurements.

User acceptance performs the short authoring exercise at each handoff. Report actual checks and limitations. Computer interaction requires permission; routine native input automation or capture suites are not mandatory. Do not create or refresh a verification-workflow document. Update the architecture's implemented status and README's controls as features land, preserving project assets and docking state.

### Known limits and assumptions

- Steps 1–4 are implemented with broader user acceptance pending; Step 5 layered effects/timeline is next in the particle roadmap. The particle sequence does not change the engine's current Next milestone.
- CPU simulation is the first backend; GPU simulation is planned separately after profiling.
- Seeded replay assumes fixed definitions/inputs within the same implementation; it cannot reconstruct arbitrary edited scene history.
- Primitive/segment transparency sorting remains approximate for crossing meshes and strips.
- Step 2 replaces the inverse-Reinhard background term with a display matte and tests visible dark-smoke attenuation at exact white. Single-tone-map premultiplied HDR remains an explicit compositing approximation.
- Initial collision is a static Jolt query world with explicit colliders; dynamic actors/props and gameplay authority remain separate.
- Optional lighting reuses existing lights/IBL without new shadows, volumetric scattering, or effect lights.
- The current resource/asset refactor and pinned API/dependency versions must be rechecked before the relevant implementation step.
- Profiling counts and starter defaults are engineering inputs, not promised hardware capacity.
