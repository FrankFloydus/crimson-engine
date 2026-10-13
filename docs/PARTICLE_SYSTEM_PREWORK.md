# Crimson Engine particle system: pre-work specification

Status: agreed design; engine implementation has not started. Prepared 13 October 2026.

This document specifies the particle system's functionality, authoring workflow, engineering boundaries, research rationale, and implementation sequence. It supplements [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md); that guide remains authoritative for engine ownership and implementation status. [README.md](../README.md) remains the entry point for controls that have actually landed.

**Every implementation step must leave a working, usable, saveable particle system.** Build the smallest complete feature at each handoff. Infrastructure belongs inside the feature that needs it, rather than in a series of invisible foundation milestones. Complete each authorized step, run its required checks, report the result, and leave visual feature acceptance to the user.

All steps below are planned. Their numbering defines the particle implementation sequence; it does not replace the architecture guide's current **Next** milestone. This document does not authorize implementing the complete system in one change.

## 1. Product direction and scope

The target is a comprehensive CPU particle system for reusable game effects. Common authoring operations arrive first; specialized behavior arrives after the everyday system is useful.

The agreed capabilities are:

- Native particle-effect assets containing one or more emitters, created in the shared Asset Browser and opened by double-clicking.
- A dedicated particle document/editor with independent save state, history, and isolated preview.
- Built-in module stacks, emitter timing, curves, gradients, and exposed parameters.
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
| Initial velocity | `(0, 6, 0)` m/s |
| Gravity | `(0, -9.81, 0)` m/s² |
| Size | 0.08 m |
| Color / intensity | White / 1 |
| Renderer | Procedural soft sprite, unlit additive |
| Capacity | 256 particles |
| Space / preview seed | World / retained seed 1 |

The isolated preview places the origin where the rise and fall are visible. Its grid is a visual aid, not a collider. The starter must work before imported textures, sophisticated modules, or Jolt exist.

Creation failure leaves existing documents and the browser intact. It must not publish a broken catalog entry or overwrite another asset.

### 2.2 Edit without unnecessary setup

Show controls that the current implementation supports. Introduce advanced controls with their feature. The first document exposes rate, lifetime, initial velocity, gravity, size, color, and capacity; it does not require learning all later modules.

Use readable property names and units: particles/s, seconds, meters, m/s, m/s², and degrees. Place a diagnostic beside its field or emitter. Do not expose persistence IDs, GPU terminology, or resource-cache details as ordinary authoring controls.

Use the engine's adjacent picker, thumbnail/name, clear, and drag-and-drop conventions. Typed fields filter compatible assets and assign immediately. A cleared optional sprite texture uses the documented procedural sprite. An unresolved assigned reference retains its identity and a visible diagnostic; retain a last valid prepared resource when possible, otherwise suspend the affected renderer/emitter until it resolves or is cleared/replaced. A missing root effect retains its scene component and overrides while playback is unavailable.

For compatible scalar properties, offer **Constant**, **Random Range**, and **Curve** beside the property when each mode is implemented. Changing the mode reveals the corresponding controls. Color uses a gradient with opacity. Curve/gradient edits operate next to the property and use ordinary field editing conventions.

Module authoring groups work by purpose and stage: scheduling, initialization, motion/appearance updates, collision, events, and rendering. **Add Module** lists implemented compatible modules with short descriptions; add search when the list warrants it. Enable, remove, duplicate, and reorder modules only where dependencies remain valid. Explain rejected operations rather than silently changing the requested order.

An emitter can feed multiple compatible renderers when that capability lands, such as a sprite plus its trail. Users should not need to duplicate a simulation to draw the same particles in two supported ways.

Emitter duplication and later starter presets produce ordinary editable definitions. Presets are copies, not hidden behavior or a new linked-template system. Keep Dear ImGui's default colors, font, sizing, and the user's docking state. This specification defines interactions without prescribing a panel arrangement.

### 2.3 Playback

| Action | Required behavior |
| --- | --- |
| Play / Resume | Start from the stopped state or continue a paused state without clearing it |
| Restart | Clear particles, trail history, pending events, and child effects; reset time and replay with the retained seed |
| Pause | Retain live state and stop advancing simulation |
| Stop | Clear the effect and return to time zero; do not synthesize death events |
| Play Effect in scene Inspector | Restart the selected scene instance using its resolved seed |
| Step, from Step 5 | Advance one fixed tick while paused |
| Seek, from Step 5 | Reset/replay the isolated preview toward the requested time with bounded work and cancellation |

Step 1 displays elapsed time and live counts without a scrub control. Seeking arrives with its implementation. Changing the seed is an explicit way to vary an effect; ordinary Restart repeats it.

Scene effects start on successful placement and scene opening. Looping effects continue; one-shot effects finish and remain selectable for replay. Selection changes do not restart effects. Moving, parenting, or editing an unrelated entity must not restart the scene's particle system.

World space is the default: newly spawned particles use the updated emitter transform, while existing particles remain detached. Local space, introduced in Step 3, makes existing particles follow the emitter. Document force-space and size-inheritance choices beside their supported controls; preserve the full affine authored matrix rather than decomposing and replacing it with TRS.

Preview playback is transient. Play, Pause, Restart, camera navigation, and seeking do not dirty a particle asset or scene. Camera-dependent draw culling initially does not suspend simulation or change event/completion behavior. Pausing, focus loss, and minimized-window handling clear accumulated wall time so resuming does not replay a catch-up burst.

### 2.4 Document editing, publication, and saving

Particle assets have independent dirty state and undo/redo from scene instances. **Ctrl+S**, **Ctrl+Z**, **Ctrl+Y**, the equivalent redo shortcut, and menu actions target the document that owns focus. Editing an effect must not accidentally save or undo its scene.

A property drag, curve edit, or timeline drag forms one transaction. Escape restores the prior values. Temporarily incomplete text is editable; constrain ordinary numeric fields where practical and show invalid values locally.

Keep these three operations distinct:

1. During an active transaction, valid intermediate values update the isolated preview. They do not publish incomplete definitions to scene instances.
2. A successful commit publishes a complete valid in-memory definition revision and restarts scene instances using that effect. Cancel restores the previous preview from its retained seed. Invalid commits retain the last valid runtime definition.
3. Save atomically writes the committed revision. Save does not cause another restart. Failure retains the previous disk file, working revision, and dirty state.

Closing a dirty particle document, replacing its contents, or exiting uses Save/Discard/Cancel. Discard restores the saved definition and updates affected instances. Preserve the repository's external-change/conflict behavior when wiring the resource manager and file watcher; do not silently overwrite a changed asset.

The committed working definition is authoritative for open dirty documents until Save, Discard, or an explicitly accepted external reload. A disk watcher or background load must not replace it with an older saved payload. Publish its complete immutable snapshot through the existing resource mechanism and retain the last valid published payload on preparation failure.

Scene-instance exposed parameters apply by stage: spawn inputs affect newborns, update inputs apply on the next tick, and rendering inputs apply on the next extraction. Structural definition changes require preparation/publication and restart. Parameter renaming preserves bindings through stable IDs; type incompatibility is diagnosed without discarding authored overrides.

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

These names describe the planned contracts, not existing particle implementations:

| Boundary / interface | Responsibility |
| --- | --- |
| `ParticleEffectAsset` | Versioned authored emitters, built-in module settings, renderer settings, parameters, stable identities, and managed dependencies |
| `ParticleEffectDocument` | Working definition, transactions, history, dirty state, validation, and atomic save/reload |
| Prepared emitter/module data | Validated native stage lists, immutable curve data, resolved parameter bindings, and required storage streams |
| `CpuParticleSystem` / effect instances | Scheduling, seed/run identity, pooled live state, motion, events, completion, and work limits |
| `SceneParticleEmitter` | Typed effect reference, seed/playback defaults, exposed overrides, and authored attachment through the scene transform |
| `ParticleRenderFrame` | Copied presentation data and render indices, with no ECS pointers or authoritative simulation mutation |
| Editor preview/session | Isolated instance/clock, document operations, preview collision fixtures, and scene reconciliation |
| Renderer particle resources | Programs, quad/mesh/ribbon buffers, bindings, targets, ordering, and explicit GPU release |
| `SceneCollider` / `SceneCollisionWorld` | Authored static collision settings and an explicit Jolt owner with Crimson query values |

Keep particle asset/storage work in `src/assets/`, document orchestration in `src/editor/`, docked controls in `src/tools/`, and scene components/history/extraction in `src/scene/`. Put simulation in a focused CPU boundary such as `src/particles/`; put the Jolt adapter in `src/physics/` when Step 9 needs it. Rendering and shaders remain in `src/crimson-renderer/`. `App` composes lifetimes and forwards time/events; it does not accumulate these implementations.

Use explicit concrete dependencies and ordinary native code. Reuse the checkout's typed `AssetRef`, resource snapshots/publications, atomic-file helper, and reference-management seams. Do not add a second loader framework, service locator, generic message bus, VM, or job graph.

Public math uses Crimson vectors, matrices, bounds, and rays. Serialize authored values and references, never live particles, playback clocks, GLM values, EnTT handles, Jolt objects, bgfx handles, or ImGui state. GPU owners release before bgfx shutdown. CPU jobs publish tagged prepared results; they do not mutate the scene or create GPU resources.

### 3.2 Current-checkout integration baseline

The architecture guide and current working tree are not identical snapshots. At preparation time, the tree contains typed references, resource workers/publications, material assets, asset replacement/deletion machinery, and streaming geometry. Their completed integration must be rechecked before implementation; this document does not claim all in-progress changes have landed.

The current scene writer produces version 2 with environment entities; its reader accepts versions 1 and 2. Extend that actual versioned path for particle/collider components and retain older inputs. Do not implement particle persistence against the guide's historical version-1 shape or assume old synchronous asset preparation remains authoritative.

`SceneDocument::documentGeneration()` changes on document replacement. `Scene::generation()` also changes during ordinary registry rebuilding. Own scene particle instances by document generation plus persistent entity ID, and reconcile changed values. Resetting on every registry generation would restart effects during Inspector edits and gizmo motion. The same rule prevents unrelated collider recooking.

### 3.3 Assets, references, and persistence

Use native versioned effect files under `assets/particles`, with a `.crimson-particle` extension and project-relative managed identity, following native asset storage conventions. Browser names/folders do not become runtime identity. Store stable emitter/module/parameter IDs from their introduction; new optional feature fields receive compatible defaults as steps land.

Loading validates a complete candidate before replacing a working definition. Saving atomically replaces the selected file. Save As creates a distinct identity and preserves the original. A failed load/save leaves active documents and previous files intact.

Scene history contains the effect reference and authored instance settings. Effect history contains definitions. Neither stores live particles. Restoring a deleted instance through scene undo creates a fresh transient instance from its authored settings; undo does not resurrect an old simulation buffer.

Add dependency support with each feature: scene -> effect in Step 1, effect -> texture in Step 2, effect -> model in Step 11, and event source/target emitter validation in Step 12. Include active documents, saved assets, and history in reference scanning as required by the existing policy. Replacement is typed and transactional. Deleting a particle instance never deletes its shared asset or dependencies.

### 3.4 CPU storage and module execution

Start with compact contiguous records per emitter: stable spawn ordinal, previous/current position, velocity, age, and lifetime. Use explicitly typed optional arrays for enabled appearance, rotation, flipbook, trail, and collision state. Reserve to configured capacity; no heap object or ECS entity per particle.

Removal fills a dead slot from the last live slot and moves every enabled associated stream together. Events, attachments, and trail links use stable run/spawn identity, never a dense slot index. Rendering sorts independent indices without permuting simulation storage. Reuse extraction and sorting scratch capacity.

Prepare module definitions into native stage lists outside tick execution. Required streams and dependencies are known before advancing particles. Dispatch modules over particle batches; do not interpret user schemas or perform name lookup/virtual dispatch for each particle. Evaluate shared parameters once per emitter step. Curves are immutable functions of effect time or normalized particle age; they do not own mutable clocks in shared assets.

The contiguous record plus optional-stream layout is the chosen baseline. Fixed typed structure-of-arrays kernels remain a measured optimization candidate. No claim of SoA, SIMD, or threading speedup is made before representative Crimson measurements.

### 3.5 Time, randomness, and lifecycle

Use 60 Hz simulation with previous/current presentation interpolation. Keep remainder time, bound live catch-up, and advance effect time only for executed ticks. A scheduled burst crosses its time boundary once. Integrate a newborn only for the portion of its birth tick during which it exists.

Derive random samples from retained effect seed, stable emitter identity, spawn ordinal, and independent property/module channels. Compaction, rendering order, and an unrelated module must not change another particle's random samples. Repeatability is within the same implementation, validated definition, parameters, and external inputs; it is not a cross-platform bitwise or CPU/GPU guarantee.

Track running, paused, draining, and completed behavior. Scheduled emission can stop while live particles/children finish; the editor's Stop command clears immediately. Completion accounts for pending events and children. Forced clear/destruction does not generate death events.

Long prewarm/seek work replays fixed ticks incrementally with progress and cancellation. The isolated preview holds its definition, parameter values, transforms, and collision inputs fixed for a replay. Seeking does not promise to reconstruct an arbitrarily edited scene history.

Budgets are separate: live particles, spawning, effect instances, replay ticks, collision queries/impacts, trail samples, event records, and child generation. Clip or reject excess work deterministically, retain existing valid state, and report the reason. Introduce each bound with the feature that creates the cost, rather than postponing correctness to Step 14.

### 3.6 Rendering and color

CPU simulation does not imply CPU-expanded sprite geometry. Use shared static quads and extracted per-particle instance attributes through the pinned bgfx API. Check instancing and buffer availability, chunk submissions within limits, and report omitted work. Ribbons/beams use bounded variable geometry; mesh particles reuse managed geometry/material resources.

Draw opaque/cutout mesh particles with scene depth writes. Transparent particles depth-test against opaque geometry without writing depth. Sort their visible presentation packets back to front across emitters and renderer types, with stable ties. Alpha and additive packets interleave when foreground smoke must cover emission. Batch only consecutive compatible packets; regrouping by emitter or texture must not destroy ordering. Center/segment sorting cannot perfectly order crossing meshes or strips; record that limit.

Keep the sequence **linear HDR scene/particles -> exposure/Reinhard -> neutral grid -> sRGB viewport -> ImGui**. The current `fs_present.sc` uses a binary alpha threshold for empty background, which would hide low-alpha and additive effects. Step 1 must correct that integration, not defer it as polish.

Use unexposed premultiplied color `C` and continuous covered fraction `c` in a validated, blend-capable `RGBA32F` target. Clear both to zero; opaque/cutout writes coverage one. Keep `RGBA16F` for the tone-mapped linear display/grid target. For source radiance `L` and final opacity `a`:

```text
Alpha:    C_next = a * L + (1 - a) * C
          c_next = a + (1 - a) * c
Additive: C_next = a * L + C
          c_next = c

E = scene exposure
B = linear editor background, clamped per channel to [0, 1 - 1e-6]
H = E * C + (1 - clamp(c, 0, 1)) * B / (1 - B)
Display = 1 - 1 / (1 + max(H, 0))
```

Alpha blending uses `ONE / INV_SRC_ALPHA` for RGB and coverage. Additive uses `ONE / ONE` for RGB and `ZERO / ONE` for coverage. Apply exposure once in resolve. The finite background clamp keeps exact white inputs representable and must preserve the final 8-bit white result. Float32 accumulation prevents ordinary supported high-overlap effects from exceeding half-float headroom; validate target support and finite supported input ranges.

The inverse-Reinhard background term is a Crimson-specific derivation for its editor contract, not a technique attributed to the reference engines. Verify empty background at exposures 0/1/100, opacity approaching zero, additive emission, opaque-only regression, alpha over emission, high overlap, and preview/scene consistency.

This is nonlinear background integration, not conventional display-matte alpha blending. Its white endpoint has a concrete limitation: with zero particle radiance and background clamped to `1 - 1e-6`, opacity 0.5 or 0.99 still resolves to the final 8-bit white value. The clamp prevents undefined/overflowing input; it does not provide ordinary dark-smoke attenuation over white. Treat this formula as the Step 1 additive integration candidate, not as proof of the Step 2 alpha result. Step 2 must test dark particles against dark, mid-grey, near-white, and white backgrounds and make partial opacity visibly attenuate the matte. Revisit the resolve convention within that usable step if needed; retain linear scene accumulation, one tone map/sRGB encode, and exposure-independent uncovered background/grid/tools.

Step 8a makes opaque depth readable and copies positive camera-space depth into a separate `R32F` texture while the source depth is detached from depth-stencil output. Transparent rendering then attaches the original depth for hardware testing and samples the copy. Merely removing `RT_WRITE_ONLY` is not sufficient to sample a depth attachment simultaneously through the pinned D3D11 path. Reconstruct depth using the actual projection conventions, and multiply both opacity and premultiplied color by the soft fade. Each preview/viewport owns compatible targets and ordered views.

Soft intersections are a visual effect against opaque scene depth. They are not physical collision or transparent-particle interaction. Start with full-resolution rendering. Reduced-resolution smoke, weighted blended OIT, tiled rendering, and GPU sorting require later evidence and their own image-quality checks.

### 3.7 Jolt collision foundation

Step 9 adds the pinned `joltphysics` dependency and a concrete CPU physics/query boundary without changing the manifest baseline. The inspected baseline provides Jolt 5.5.0; recheck the port and CMake target when that step starts. Keep Jolt types private behind engine-owned queries and explicit lifetime.

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
| 3 | Author bursts, sprays, and varied motion through common modules |
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

**Goal:** a small, complete particle authoring feature, usable without any later step.

Implementation actions:

- Add native effect identity/load/validate/save and browser create/open actions; begin with one enabled point emitter and the starter constants in Section 2.1.
- Add the dedicated effect document, transactions, dirty handling, Save/Save As, reopen, and focused Save/Undo/Redo routing.
- Add the bounded CPU instance/storage/tick owner and procedural unlit additive instanced sprites. Land required programs, HDR/background correction, targets, and resource release here.
- Add an isolated preview with Play/Pause/Restart/Stop, retained seed, elapsed time, live/capacity counts, and basic failure/drop diagnostics.
- Add typed scene references, placement, an emitter marker/bounds for selection/focus, transforms/hierarchy participation, duplicate/delete/undo, scene save/reopen, automatic start, and selected **Play Effect**.
- Add scene -> effect reference handling and document-generation reconciliation. Do not include external textures, shape libraries, curves, multiple emitters, seeking, or Jolt.

Acceptance: create the effect, change gravity and observe the fall, undo the edit, save/reopen, place two independently playing instances, save/reopen the scene, and duplicate/delete/undo an instance. Moving one instance and editing an unrelated entity preserve playback. Preview actions remain transient. Saving and failure paths retain prior files and working state. Opaque-only rendering, additive particles over empty space, exposure extremes, resize, and shutdown remain correct.

Focused checks: seeded restart, emission/lifetime integration, fractional birth time, compaction identity, bounded capacity/catch-up, asset round-trip/failure, scene round-trip/history, and resolve equations. This step's broad integration serves one deliberately small emitter, not the whole particle system.

### Step 2: Textured sprites and transparency

**Goal:** author recognizable smoke, dust, and embers with ordinary managed images.

Add typed texture fields, sampler/color-role handling, alpha mode beside additive mode, premultiplied shader output, and globally ordered transparent packets. Keep clear-to-procedural behavior. Add effect -> texture reference scanning/replacement/deletion and unresolved assignments as this reference type arrives.

Acceptance: assign/drop/replace/clear smoke imagery, overlap two effects, view particles over empty background and opaque/cutout models, then save/reopen and undo. Diagnose missing textures without losing the assignment. Smoke can cover sparks behind it, and partial dark opacity visibly attenuates dark/mid-grey/near-white/white editor backgrounds. Complete any necessary resolve adjustment described in Section 3.6 within this handoff. Check sorting order and alpha/coverage equations through CPU data tests; visual acceptance belongs to the user.

### Step 3: Common emission and motion

**Goal:** build fountains, one-shot impacts, and directional sprays without bespoke code.

Add cone/sphere/box emission, random ranges for lifetime/speed/direction, bursts, delay/duration/loop scheduling, acceleration/drag, and explicit local/world simulation. Add supported module creation, enable/remove, and ordering within validated stages. Shape positions and initial directions use the emitter's affine transform; document force-space behavior without rewriting authored matrices.

Acceptance: tune a fountain, create a cone impact burst, and compare local/world behavior under moving/parented emitters. Pause/Resume cannot duplicate a burst. Restart repeats it, disabling a module restores predictable behavior, and earlier effects reopen unchanged. Check sampling bounds, fractional rate/burst scheduling, ranges, stage validation, and transforms.

### Step 4: Curves and gradients

**Goal:** shape effects over lifetime through direct authoring controls.

Add immutable scalar curve and color/opacity gradient data, value modes, key/stop editing, and evaluation for size, rotation, and intensity. Keep emitter time in effect seconds and particle lifetime functions in normalized age. Prepare authoring data outside particle loops and keep random curve variation associated with stable spawn identity.

Acceptance: smoke grows/fades and sparks shrink. Dragging a key/stop is one undo command; cancel restores prior values. Constant/range/curve switching, endpoints, saved interpolation, and independent instance playback are correct. Check finite validated keys, endpoints, evaluation, transaction history, and round-trip persistence.

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

Acceptance: light/environment changes affect lit effects predictably; unlit mode restores their authored appearance. Normal assignments are typed linear data. Save/reopen and mode switching remain stable. This step adds no shadows, volumetric scattering, particle-generated lights, or general material editor.

### Step 9: Jolt static collision

**Goal:** make particles interact with explicitly authored static scene surfaces.

Add Jolt and the static query owner together with primitive/model collider authoring, layers, scene persistence/history, and isolated floor/wall fixtures. Add swept collision radius, bounce/stop/kill, restitution/friction, and filtering. Cook original model geometry with affine hierarchy/reflection handling, tag asynchronous preparation, and update only affected colliders.

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

Evidence: contiguous particle records, initialization/update separation, fixed-FPS remainder, seeded restart, and fractional birth integration. Adopt those timing/storage lessons. Crimson adds stable spawn identity under compaction and explicit presentation interpolation. Godot's CPU implementation is hard-coded, not a reorderable module system; its synchronous preprocessing is not the bounded preview replay chosen here. The [process-material generator](https://github.com/godotengine/godot/blob/4.5.2-stable/scene/resources/particle_process_material.cpp) generates GPU shaders and must not be cited as CPU feature parity.

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

Evidence: premultiplied coverage, emissive contributions, ordered OVER, bounded-memory approximate OIT, and crossing-geometry limitations. Adopt premultiplication and honest ordering limits; soften both radiance and coverage. Weighted blended OIT changes the compositing approximation and remains a later evaluated alternative. The inverse-Reinhard editor-background integration is Crimson's derivation, not a claim from this paper.

## 6. Checks, handoff, and completion

### Documentation completion

The pre-work document is complete when each step specifies a visible useful result, its supporting changes, acceptance, compatibility obligations, and its position in the sequence. Check research links/attribution, local links, status labels, and the architecture cross-reference. Documentation-only delivery does not require an application build.

### Implementation checks

Use the repository's PowerShell workflow. After implementation owners finish a step, perform one incremental Release integration build and its focused CPU checks. Dependency/preset changes use the configure build alternative; `test` and `configure` are not combined flags. Reuse prior checks rather than growing a broad matrix without a failure or unresolved concern.

Automated checks should exercise meaningful behavior: native round-trip/migration/failure, entity/document history, seed/timing/curves, bounds/capacity, stable identity, query responses, staged events, bindings, render equations/extraction, and resource teardown. Preserve earlier fixtures as new format versions land. Stress workloads must be reproducible and include actual settings/hardware in measurements.

User acceptance performs the short authoring exercise at each handoff. Report actual checks and limitations. Computer interaction requires permission; routine native input automation or capture suites are not mandatory. Do not create or refresh a verification-workflow document. Update the architecture's implemented status and README's controls as features land, preserving project assets and docking state.

### Known limits and assumptions

- This is the agreed particle roadmap, not a change to the engine's current Next milestone or evidence that particles are implemented.
- CPU simulation is the first backend; GPU simulation is planned separately after profiling.
- Seeded replay assumes fixed definitions/inputs within the same implementation; it cannot reconstruct arbitrary edited scene history.
- Primitive/segment transparency sorting remains approximate for crossing meshes and strips.
- The proposed inverse-Reinhard background integration has a nonlinear white-matte alpha limit; Step 2 includes the required visible-opacity test and resolve adjustment before alpha sprites are considered complete.
- Initial collision is a static Jolt query world with explicit colliders; dynamic actors/props and gameplay authority remain separate.
- Optional lighting reuses existing lights/IBL without new shadows, volumetric scattering, or effect lights.
- The current resource/asset refactor and pinned API/dependency versions must be rechecked before the relevant implementation step.
- Profiling counts and starter defaults are engineering inputs, not promised hardware capacity.
