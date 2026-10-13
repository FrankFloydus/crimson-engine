# Crimson Engine: ARPG engine architecture

This is the authoritative engineering guide for the native Crimson Engine ARPG engine. Consult the sections relevant to changes in rendering, assets, scene/runtime boundaries, input, or resource lifetime. Keep it current when an agreed implementation changes these contracts.

The foundation is the native renderer and editor in this project. Work progresses through small slices with a working application at each handoff. Features described as planned are architectural commitments, not instructions to build every subsystem before the next milestone.

Project root: `C:\Users\Drako\Desktop\_CRIMSON_ENGINE\crimson-engine`. The official renderer name, CMake target, and executable are `crimson-renderer`, `crimson-renderer`, and `crimson-renderer.exe`. Renderer implementation, shaders, the ImGui/bgfx adapter, and rendering diagnostics belong in `src/crimson-renderer/`. Editor panels remain in `src/tools/`; asset import/storage remains in `src/assets/`.

## 1. Direction and implementation status

### Agreed foundation

| Area | Decision | Status |
| --- | --- | --- |
| Platform | Windows x64; offline single-player ARPG | Agreed target |
| Language and build | C++20, MSVC, CMake, project-local vcpkg manifest | Implemented |
| Window and input | SDL3 | Implemented |
| Graphics | `crimson-renderer`: our PBR renderer through bgfx with Direct3D 11 | Implemented direct lighting, IBL and normal maps |
| Editor | Dear ImGui 1.92.8 docking inside the SDL window | Implemented |
| Entities | EnTT storage with explicitly ordered Crimson systems | Agreed; integrate for the runtime |
| Physics | Jolt rigid-body world; controlled actors and dynamic props | Agreed; integrate with the character milestone |
| Models | Static glTF 2.0 / GLB first | Next: milestone 2 |
| Game UI | Custom UI authored in C++ initially | Future work |
| Scale | Design for several hundred active actors | Future profiling target |

Keep the manifest baseline `d0c191d142b7bc048141ddfb618aa9ae5dba0233`. EnTT and Jolt are agreed dependencies but are not yet present in the application or manifest. Add them when their implementation slice needs them. Preserve the existing incremental build and fast launch workflow.

### Implemented today

- A procedural PBR sphere at `(0, 1, 0)`, one directional light, two point lights, material controls, and exposure.
- A docked Viewport, Settings panel, and Asset Browser with saved layout.
- A flat crimson application emblem: transparent 512×512 source PNG and a multi-resolution ICO embedded in the Windows executable. SDL uses the executable icon for its native window class and owns the native icon lifetime. Successful links and desktop-shortcut updates send a targeted Windows Shell icon refresh so cached artwork can be reloaded.
- Quader-derived perspective orbit, pan, fly, zoom, and an XZ ground grid.
- Managed PNG, JPEG, TGA, BMP and Radiance HDR imports with thumbnails and adjacent asset pickers. HDR sources are environment-only.
- Albedo, roughness, metalness and tangent-space normal maps, material factors/normal strength, and project-relative persistence in `assets/material.json`.
- Scene-level environment lighting with a built-in studio or imported equirectangular panorama, diffuse irradiance, roughness-prefiltered specular lighting and a two-channel BRDF lookup.
- Environment enable/intensity/Y rotation and separate project-relative persistence in `assets/environment.json`.
- Offscreen HDR scene rendering, exposure/Reinhard tone mapping, grid compositing, final sRGB encoding, and ImGui presentation.

Model importing, EnTT worlds, Jolt, Play/Stop, and a player controller are not implemented yet.

### Immediate progression

| Milestone | Status | Result |
| --- | --- | --- |
| 1. IBL and normal maps | Implemented | Environment lighting and normal maps in the working sphere editor |
| 2. Actual glTF/GLB import | **Next** | Managed model assets instantiated and rendered with their own materials and hierarchy |
| 3. Imported player visual and controller | Planned | Static imported character, controlled collision, WASD movement, mouse aiming, and isometric Play camera |

The first controllable character uses a static model. Define a focused skeletal-animation slice after milestone 3 works. The architecture does not prescribe ability, combat, inventory, or progression behavior.

## 2. Engine boundaries and interfaces

Introduce these boundaries incrementally as their features land. Start with concrete classes and clear source ownership. Keep ordinary headers and `.cpp` files; a plugin framework, generic service locator, DLL ecosystem, or general job graph is not required.

| Boundary | Responsibility |
| --- | --- |
| Application / Platform | SDL window/events, service lifetime, input routing, project paths, and frame loop |
| Assets | Managed source files and dependencies, import/decoding, shared CPU data, material definitions, identity, and persistence |
| Scene / World | Entities, transforms, model instances, lights, and optional controller/physics components |
| Renderer | bgfx resources, GPU caches, PBR/IBL, targets, and rendering scene instances |
| Physics / Character Motor | Jolt resources, collision queries, controlled movement, and collision-resolved actor poses |
| Player Controller | Accepted movement and aiming intentions, independent of the selected visual model |
| Editor | ImGui panels, asset fields, selection, inspection, and Edit/Play/Stop |
| Future runtime services | Animation, audio, gameplay systems, and C++ game UI when their slices need them |

### Target interfaces

These names describe the intended responsibilities; they are not existing APIs that future work should assume are already available.

| Interface | Contract |
| --- | --- |
| `ModelAsset` | Shared imported primitives, node hierarchy, source transforms, and material references |
| `ModelInstance` | A reference to a model asset plus an independent root transform; later instance animation state belongs here or in related components |
| `MaterialAsset` | Material factors and typed texture references; mesh primitives reference their assigned materials |
| `Scene` / `World` | Owns instances/entities and their state; references shared assets |
| `RenderScene` / `RenderFrame` | Renderer input containing instances, transforms, materials, camera, and lighting |
| `PlayerController` | Consumes accepted input and produces movement/facing intentions |
| `CharacterMotor` | Resolves desired movement against the world and supplies the authoritative actor pose |

Importing an asset and placing an instance in a scene are separate operations. Instances share CPU asset data and GPU resources while keeping independent transforms. Imported geometry does not automatically become a character collision shape.

Keep environment, exposure, lights, and camera settings separate from individual materials. Today `SceneSettings` includes one preview material; replace that assumption when model support lands. A model can have multiple primitives with different materials. The Inspector must edit the selected object/material rather than applying the sphere's material globally.

### Dependencies and resource ownership

- The application constructs services and passes explicit dependencies. Controller and gameplay code do not call ImGui or bgfx.
- Scene components carry asset references and runtime handles, not ownership of shared GPU resources.
- The rendering boundary owns GPU meshes, material textures, environment resources, and targets. The tools adapter owns its ImGui GPU resources under the same bgfx lifetime.
- Physics owns Jolt worlds, bodies, shapes, constraints, and body-to-entity mappings. Actor/controller code uses the engine-owned adapter.
- Background decoding returns completed CPU results through publication queues. It does not create GPU resources or modify the world directly.
- The main thread initially owns authoritative world mutation and application-side render submission. Jolt may execute internal jobs; its callbacks collect facts rather than changing EnTT.

Current ownership is smaller and partly combined: `AssetLibrary` owns material texture/thumbnail GPU resources and managed source/persistence data, `Renderer` owns scene meshes/targets/programs and derived environment GPU resources, and `ImGuiBgfx` owns tool rendering resources. The renderer-owned environment object releases its resources before bgfx shutdown. Environment CPU preprocessing/cache preparation is invoked explicitly when a source changes, outside ordinary rendering. Separate asset decoding/storage from GPU caches when the relevant slice needs it; do not present that full separation as already implemented.

Every resource has a named owner. Shutdown cancels pending work, waits for outstanding jobs, releases runtime/physics state and GPU-owning services, then shuts down bgfx and SDL. Async results carry a project/world generation so stopped or replaced sessions cannot receive stale results.

### Identity and persistence

Persist stable asset and authored-object identities. Keep source locations readable and project-relative. Runtime entity references include world identity and entity generation. Do not retain component pointers across structural registry changes or queued work.

Never serialize temporary EnTT, Jolt, or bgfx handles. Introduce versioned serialization as new asset/scene data lands. Existing imports, material factors/map paths, and dock layout must remain usable when identity support is added.

Current material persistence writes version 2 of `assets/material.json`, adding `normalMap` and `normalStrength`; existing unversioned material files load with a neutral missing normal map and strength 1. Version 1 of `assets/environment.json` stores `map`, `enabled`, `intensity` and `rotation` in degrees. An empty environment path selects the built-in studio. Both files use managed project-relative source IDs; missing/unsupported assignments produce a diagnostic and fall back safely. Environment state is independent of material resets. Assets and `crimson_ui.ini` follow the optional `--project-root` location.

Transform parenting serves imported nodes and attachments. Spatial queries serve collision, nearby actors, visibility, and ground targeting; the transform hierarchy is not the proximity-query structure.

## 3. Rendering and material contract

Use a right-handed, Y-up world with meters as the distance unit. Camera, picking, physics, scene transforms, and imports use consistent conventions; backend conversions belong at their adapters.

Retain the current direct-light BRDF: GGX with `alpha = perceptualRoughness * perceptualRoughness`, height-correlated Smith visibility, Schlick Fresnel, and Fresnel-weighted Lambert diffuse. The visibility term already contains the Cook-Torrance denominator; do not divide by it again.

Keep the current roughness floor of `0.045` and numerical safeguards until a measured, explicitly documented shading change replaces them. Avoid changing the material response by copying a different approximation under the same parameter names.

### Color and texture meaning

- Accumulate lighting in linear HDR. Apply exposure and tone mapping once; encode the final image to sRGB once.
- Preserve the editor sequence: **HDR scene -> exposure/Reinhard into linear display color -> grid -> sRGB viewport image -> ImGui tools**. The grid and tools remain independent of scene exposure.
- Texture roles specify decoding and channel selection. Albedo is sRGB color; roughness, metalness, and normal maps are linear data.
- Environment panoramas use linear radiance for Radiance HDR and sRGB decoding for PNG/JPEG/TGA/BMP. HDR thumbnails are tone-mapped display previews; preprocessing reads the original HDR source. HDR sources cannot be assigned to material slots.
- Standalone roughness/metalness maps retain their current red-channel behavior. glTF imports preserve packed roughness in green and metalness in blue through explicit channel metadata.
- Material factors multiply their maps. Manual first assignment to an empty albedo/roughness/metalness slot retains the current neutral-factor behavior; replacement preserves edited factors and clearing removes only the map.
- Importing a model preserves its authored material factors. Import is not a manual picker assignment and must not reset those factors to neutral.
- Material edits update parameters/bindings without recreating geometry or rebuilding shaders.

Future shipping UI composites after scene tone mapping and before final encoding so scene exposure does not change HUD brightness. Its C++ presentation code reads state and emits commands; it does not own simulation rules.

### Milestone 1: IBL and normal maps (implemented)

The sphere editor has scene-level environment lighting: diffuse irradiance, a roughness-prefiltered specular environment, and a two-channel BRDF lookup texture. The Lights tab provides the environment picker/clear/drag-and-drop field, enable toggle, intensity 0–10, and Y rotation −180° to 180°. An empty assignment uses the built-in studio; renderer preparation failures remain visible in the panel.

Preparation/cache loading runs on initialization or source assignment, outside ordinary frame rendering. Intensity and rotation reuse prepared resources. The CPU-derived cache lives at `assets/cache/ibl/<FNV64>.ibl`; its key includes source content, preprocessing version 3, BRDF version 1, dimensions and sample counts. Cache reads validate payload hash, length and finite/range constraints; incompatible or damaged data is regenerated. Authored sources and assignments are independent of this disposable cache.

The default studio is generated as a 512×256 HDR panorama. Derived RGBA32F cubemaps comprise 16×16 diffuse faces with 512 cosine-weighted samples and 128×128 specular faces with a full eight-level mip chain and 256 GGX samples. Specular mip roughness is `0.045 + 0.955 * mip / 7`; runtime selection follows that same range. A 128×128 RG32F BRDF lookup uses 1024 GGX samples with the runtime's correlated Smith visibility, roughness convention and denominator safeguards. Source HDR radiance is bounded to finite values up to 60000 for downstream HDR stability.

Environment convolution uses filtered importance sampling to prevent small, bright HDR emitters from producing scattered reflection copies. A linear source pyramid preserves spherical radiance integrals with exact latitude and cell-overlap weights, including odd image dimensions. Each sample selects and blends source levels by its probability-derived solid angle, with a lower bound from the destination cubemap texel footprint. Longitude wraps across the panorama seam; latitude clamps at the poles. These source levels provide spatial filtering and are distinct from the output cubemap's roughness levels. The filtering change invalidates older derived caches automatically; it does not change the BRDF, material roughness floor, exposure or source assets.

Use split-sum specular recombination:

```text
specularIBL = prefilteredEnvironment * (F0 * LUT.scale + LUT.bias)
```

Store diffuse preprocessing as irradiance divided by pi and apply that normalization once. Sum environment and direct lighting in linear HDR before exposure/tone mapping.

Karis's notes provide the split-sum reference. Its analytic-light roughness adjustment must not be applied to IBL, and its selected visibility approximation must not silently replace our correlated Smith model. The notes also describe the grazing-angle limitations of the prefilter approximation. IBL does not itself supply local geometry reflections or shadowing.

Material data includes a normal-map reference and strength 0–2 with the existing adjacent picker/clear/drag-and-drop pattern. Normal assignment preserves strength. Tangent XYZ points along increasing U; the sign in tangent W reconstructs the bitangent as `cross(N,T) * W`, oriented along increasing V. Maps use +Y/OpenGL green. The sphere supplies stable tangents at its duplicated seam and poles. The shader orthogonalizes interpolated tangents, provides a safe basis for degenerate input, scales tangent XY, and renormalizes the evaluated normal. Missing maps and zero strength use the original smooth normal; conventional `(128,128,255)` flat maps evaluate neutrally, with both nearest 8-bit encodings of 0.5 treated as zero XY.

Preserve material factors, direct lights, docking, Quader navigation, the grid, existing imports, and persistence. Normal-map and environment assignment data must also survive reopening.

Acceptance checks cover environment-only metal/dielectric response, roughness-dependent reflection sharpness, neutral/detailed normals, stable valid extremes and reopening. Actual executed checks and evidence are recorded in [VERIFICATION.md](VERIFICATION.md).

Single-scattering GGX can darken rough metals through missing microfacet multiple scattering. This remains a known limitation; a later compensation must be a consistent BRDF change rather than an exposure adjustment. Split-sum environment reflections also retain their grazing-angle approximation and do not reflect nearby geometry or provide occlusion/shadows. Area lights, advanced material layering, shadows, and a different light pipeline receive separate scopes.

## 4. Models and the first controllable actor

### Milestone 2: actual glTF/GLB import

Support static glTF 2.0 models in `.gltf` and `.glb` form. Import their required buffers and images into managed project assets so reopening works even if the original source folder moves or disappears.

Load primitive geometry, node transforms/hierarchy, material assignments, UVs, normals, tangents, and appropriate index sizes. Preserve authored material factors, color decoding, and packed texture channels. Render through `ModelAsset`/`ModelInstance` and scene input with actual instance transforms.

Expose imported models in the Asset Browser, support instantiation, and provide selection/material inspection. Display clear diagnostics for unsupported features or failed imports. Skeletal deformation, animation playback, and advanced extensions are outside this first import milestone.

Models retain their own material appearance; they do not inherit the preview sphere's color or scalar settings. Multiple instances share resources and keep independent transforms.

Completion: a real managed model renders with correct geometry, hierarchy, materials, and textures; independent instances work; reopening preserves its dependencies and placement.

### Milestone 3: imported player visual and controller

Create an EnTT entity with a transform, character motor, player controller, and static model instance imported through milestone 2. Introduce the needed EnTT/Jolt dependencies in this slice, not as a prerequisite for IBL.

Use controlled Jolt movement and an explicit collision shape. Attach the visual through a local transform for scale, forward-axis correction, and alignment. The visual follows the collision-resolved player pose; the mesh itself is not the motor or collision shape.

Use normalized, camera-relative WASD movement projected onto the ground plane. Mouse aiming comes from a ray through the Play viewport's camera onto the ground. Character facing follows that aim independently of movement. Preserve the last valid direction for a coincident aim point or missing ground intersection.

Use an isometric Play camera that follows the player. Keep Quader's perspective navigation in Edit mode. Player input is owned by the focused game viewport and respects editor text/widget capture and focus loss. The same keys must not simultaneously drive the player and editor camera.

Start with a small ground-and-obstacle scene. Completion: the imported static character moves/faces correctly, collides predictably, and follows the game camera without interfering with editor controls. Animation and combat are later work.

## 5. Simulation and editor lifecycle

Use 60 Hz fixed simulation and physics as the initial engineering default. Rendering is independent and interpolates presentation between completed states. This is a starting point to measure, not a production hardware-performance guarantee.

Input consists of ordered edges and held state. Consume each edge once even when one rendered frame executes multiple simulation steps. A press/release pair between steps must not disappear. Pause/focus handling clears held input and accumulated wall time so resuming does not replay a catch-up burst.

The character motor owns the controlled actor's collision-resolved pose. Dynamic rigid bodies own their physics poses. Rendering never writes interpolated values back into the world. Apply structural world changes at safe boundaries; collect Jolt callback facts and consume them after its update completes.

Play creates a runtime world from authored scene data. Stop discards transient entities, movement, and simulation state and invalidates pending world-specific work. Deliberate edits to material assets and saved definitions persist. Shared imported assets and GPU caches survive Play/Stop. Editor selection cannot retain destroyed runtime entity references.

Custom game UI remains C++ authored. No UI framework, gameplay screen, animation system, or audio implementation is required to complete the first three milestones.

## 6. Verification and future AI workflow

For documentation-only changes, verify consistency, status labels, and entry-point links. No application build is required.

For each implementation slice, use one incremental Release build after implementation owners finish, focused checks, and one short visual smoke when needed. Repeat or broaden checks only when failures or unresolved concerns justify it. Keep launching separate from building; preserve the user's project assets, assignments, and dock layout.

`--project-root PATH` selects assets, persistence, layout and derived cache for isolated diagnostics. `--ibl-smoke` requires an explicit disposable project root and creates/assigns fixtures there; follow it with `--scenario ibl-reopen` in the same root to exercise reopening. Environment-only `ibl-*` scenarios cover metals/dielectrics, roughness, neutral/detail/zero-strength normals and extremes. These are targeted diagnostic modes, not the ordinary project launch path.

| Milestone | Focused acceptance scenarios |
| --- | --- |
| IBL / normal maps | Environment-only metal/dielectric response; roughness-dependent reflections; neutral/detailed normals; seams/poles; stable valid extremes; exposure-independent grid/tools; assignment persistence |
| Model import | `.gltf` and `.glb`; multiple primitives/materials; nested transforms; 16/32-bit index needs; managed texture/buffer dependencies; independent instances; unsupported-feature diagnostics; reopening |
| Player controller | Normalized diagonal movement; camera-relative directions; mouse facing while stationary/moving; ground targeting; coincident/missing aim; obstacle collision; camera follow; input capture/focus loss; repeated Play/Stop |

After the controllable static character works, define the skeletal-animation slice. Further ARPG foundations—additional actors, gameplay, audio, shadows, VFX, game UI, and dense-scene profiling—receive their own scopes and acceptance criteria.

Future AI must:

- Consult the relevant sections of this guide for subsystem changes and the repository workflow for build/run commands.
- Follow the current milestone and preserve already-working native features and project data.
- Use familiar engine UI conventions, including adjacent asset fields and immediate picker assignment.
- Keep ownership, data flow, input routing, and resource lifetime explicit.
- Introduce infrastructure when it serves an implemented feature; avoid unrelated settings and speculative systems.
- Update implemented/agreed/future status as slices land, with relevant verification evidence in the existing verification document.
- Discuss departures from the agreed architecture; proceed autonomously with routine choices inside it.

## 7. Research references

The Vulkan tutorial is an architecture reference; adapt its ownership and subsystem ideas to SDL3/bgfx. Its Vulkan API examples and advanced feature list do not mandate additional engine work.

The recursive review covered the overview, sections 1–7, section 9 and their linked descendants, including the shared appendix: 69 pages total. Section 8 (mobile) was excluded.

| Tutorial section | Use in this engine |
| --- | --- |
| 1. Engine architecture | Components, explicit dependencies, resource ownership, and presentation separation |
| 2. Camera transformations | Shared coordinate/projection conventions and consistent viewport picking |
| 3. Lighting/materials | Lighting/material separation and a clear PBR/presentation pipeline |
| 4. GUI | Editor integration and input ownership |
| 5. Model loading | Shared assets, independent instances, hierarchy, materials, and later animation |
| 6. Subsystems | Engine-owned physics/audio adapters and measured CPU-first development |
| 7. Tooling | Clear failures, reproducible builds, focused diagnostics, and eventual packaging |
| 9. Advanced topics | Later reference for measured lighting, visibility, streaming, and renderer work |

- [Khronos: Building a Simple Engine](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/introduction.html).
- [Brian Karis: Real Shading in Unreal Engine 4](https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf), especially pages 3–8 for BRDF conventions and split-sum IBL.
- [GPU Gems 3: GPU-Based Importance Sampling](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling), section 20.4 for filtering high-frequency HDR sources by sample footprint.
- [Filament: CubemapIBL](https://github.com/google/filament/blob/main/libs/ibl/src/CubemapIBL.cpp), for GGX reflection PDF and source-level selection; Crimson uses a twofold footprint overlap validated against its concentrated-emitter regression.
- [Filament PBR reference](https://google.github.io/filament/main/filament.html), for correlated Smith visibility and single-scattering energy-loss context. This is an equation reference, not a renderer dependency.
- [Jolt author's fixed-timestep guidance](https://github.com/jrouwe/JoltPhysics/discussions/968), for the initial 60 Hz physics choice.
