# Crimson Engine: ARPG engine architecture

This is the authoritative engineering guide for the native Crimson Engine ARPG engine. Consult the sections relevant to changes in rendering, assets, scene/runtime boundaries, input, or resource lifetime. Keep it current when an agreed implementation changes these contracts.

The foundation is the native renderer and editor in this project. Work progresses through small slices with a working application at each handoff. Features described as planned are architectural commitments, not instructions to build every subsystem before the next milestone.

Project root: `C:\Users\Drako\Desktop\_CRIMSON_ENGINE\crimson-engine`. The official renderer name, CMake target, and executable are `crimson-renderer`, `crimson-renderer`, and `crimson-renderer.exe`. Renderer implementation, shaders, the ImGui/bgfx adapter, and rendering diagnostics belong in `src/crimson-renderer/`. Editor orchestration belongs in `src/editor/`, docked views in `src/tools/`, CPU scene/history/storage in `src/scene/`, and managed imports in `src/assets/`. `src/math/` owns the public math boundary. Keep `App` focused on composition, native lifetime and the frame loop.

## 1. Direction and implementation status

### Agreed foundation

| Area | Decision | Status |
| --- | --- | --- |
| Platform | Windows x64; offline single-player ARPG | Agreed target |
| Language and build | C++20, MSVC, CMake, project-local vcpkg manifest | Implemented |
| Window and input | SDL3 | Implemented |
| Graphics | `crimson-renderer`: our PBR renderer through bgfx with Direct3D 11 | Implemented direct lighting, IBL and normal maps |
| Editor | Dear ImGui 1.92.8 docking inside the SDL window | Implemented |
| Entities | EnTT storage with explicitly ordered Crimson systems | Implemented scene authoring; runtime simulation remains planned |
| Math | Engine-owned packed values and helpers; GLM private implementation | Implemented |
| Physics | Jolt rigid-body world; controlled actors and dynamic props | Agreed; integrate with the character milestone |
| Models | Static glTF 2.0 / GLB first | Managed import and independent scene instances implemented |
| Game UI | Custom UI authored in C++ initially | Future work |
| Scale | Design for several hundred active actors | Future profiling target |

Keep the manifest baseline `d0c191d142b7bc048141ddfb618aa9ae5dba0233`. EnTT, ImGuizmo and GLM support the scene-authoring slice. Jolt remains planned for the character milestone. Preserve the incremental build and fast launch workflow.

### Implemented today

- Native scene assets with independent model entities, empty entities and directional/point-light entities; environment and exposure belong to the scene.
- A docked Viewport, rootless Outliner, Inspector, Settings panel and Asset Browser with saved layout and Dear ImGui's default colors, font and sizing.
- A flat crimson application emblem: transparent 512×512 source PNG and a multi-resolution ICO embedded in the Windows executable. SDL owns the native window-class icons; the application also assigns those borrowed handles explicitly to the window with `WM_SETICON`. The window is destroyed before SDL releases the class icons. Successful links and desktop-shortcut updates send a targeted Windows Shell icon refresh so cached artwork can be reloaded.
- Quader-derived perspective orbit, pan, fly, zoom, and an XZ ground grid.
- Managed PNG, JPEG, TGA, BMP and Radiance HDR imports with thumbnails and adjacent asset pickers. HDR sources are environment-only.
- One Asset Browser catalog and one Import button for supported images, environments and models, including mixed multi-file selections and independent per-file diagnostics. Native scenes are saved assets in the same catalog and open on double-click.
- Managed static glTF/GLB imports with node hierarchy and opaque/cutout PBR materials. Browser-to-viewport placement adds entities sharing their model's geometry and materials.
- EnTT entity storage, full affine transform parenting, multiple selection, ImGuizmo T/R/E manipulation, F focus and transactional undo/redo.
- Albedo, roughness, metalness and tangent-space normal maps, material factors/normal strength, and project-relative persistence in `assets/material.json`.
- Scene-level environment lighting with a built-in studio or imported equirectangular panorama, diffuse irradiance, roughness-prefiltered specular lighting and a two-channel BRDF lookup.
- Environment enable/intensity/Y rotation and separate project-relative persistence in `assets/environment.json`.
- Offscreen HDR scene rendering, exposure/Reinhard tone mapping, grid compositing, final sRGB encoding, and ImGui presentation.

The scene-authoring implementation awaits user acceptance. Imported-material editing, Jolt, Play/Stop and a player controller remain unimplemented. The procedural sphere, its saved material and the older model-preview persistence remain available to legacy diagnostics without becoming mandatory content in every authored scene.

### Immediate progression

| Milestone | Status | Result |
| --- | --- | --- |
| 1. IBL and normal maps | Implemented | Environment lighting and normal maps in the working sphere editor |
| 2a. Managed glTF/GLB import and preview | Implemented | Unified import, managed dependencies, authored materials/hierarchy, single model replacement and reopening |
| 2b. Scene authoring | Implemented; user acceptance pending | Native scene assets, EnTT entities, lights, hierarchy, multiple selection, transform gizmos and undo/redo |
| 2c. Imported-material editing | **Next** | Editable material inspection with explicit shared-material versus per-instance behavior |
| 3. Imported player visual and controller | Planned | Static imported character, controlled collision, WASD movement, mouse aiming, and isometric Play camera |

The first controllable character uses a static model. Define a focused skeletal-animation slice after milestone 3 works. The architecture does not prescribe ability, combat, inventory, or progression behavior.

## 2. Engine boundaries and interfaces

Introduce these boundaries incrementally as their features land. Start with concrete classes and clear source ownership. Keep ordinary headers and `.cpp` files; a plugin framework, generic service locator, DLL ecosystem, or general job graph is not required.

| Boundary | Responsibility |
| --- | --- |
| Application / Platform | Thin SDL window/events, explicit service lifetime, project paths and frame loop |
| Assets | Managed source files and dependencies, import/decoding, shared CPU data, material definitions, identity, and persistence |
| Scene / World | EnTT entities, affine hierarchy, model references and lights; document/history/storage and CPU render extraction |
| Math | Packed Crimson vectors, matrices, quaternions, rays and bounds; backend conversion stays private |
| Renderer | bgfx resources, GPU caches, PBR/IBL, targets, and rendering scene instances |
| Physics / Character Motor | Jolt resources, collision queries, controlled movement, and collision-resolved actor poses |
| Player Controller | Accepted movement and aiming intentions, independent of the selected visual model |
| Editor | Session/document orchestration, selection, interaction and panel commands; Edit/Play/Stop remains future work |
| Future runtime services | Animation, audio, gameplay systems, and C++ game UI when their slices need them |

### Target interfaces

`ModelAsset`, `ModelInstance`, `RenderScene`, `Scene`, `SceneDocument` and the scene undo manager now serve authored scenes. `ModelMaterial` supplies read-only imported material definitions; editable material assets and runtime gameplay services remain planned.

| Interface | Contract |
| --- | --- |
| `ModelAsset` | Shared imported primitives, node hierarchy, source transforms, and material references |
| `ModelInstance` | A reference to a model asset plus an independent root transform; later instance animation state belongs here or in related components |
| `MaterialAsset` | Material factors and typed texture references; mesh primitives reference their assigned materials |
| `Scene` / `World` | Owns instances/entities and their state; references shared assets |
| `SceneDocument` | Owns the current scene, selection, command history, dirty state and native scene storage |
| `RenderScene` / `RenderFrame` | Renderer input containing instances, transforms, materials, camera, and lighting |
| `PlayerController` | Consumes accepted input and produces movement/facing intentions |
| `CharacterMotor` | Resolves desired movement against the world and supplies the authoritative actor pose |

Importing an asset and placing an instance in a scene are separate operations. Instances share CPU asset data and GPU resources while keeping independent transforms. Imported geometry does not automatically become a character collision shape.

Keep environment, exposure, lights and editor camera settings separate from individual materials. Scene extraction produces independent model instances and directional/point-light values for `RenderScene`; imported primitives use their own linear `ModelMaterial` factors. `SceneSettings` retains the older sphere material only for the legacy preview/diagnostic path. Future material editing must target a selected material asset or explicit instance override rather than applying the sphere material globally.

### Concrete editor and math boundaries

`App` constructs owners, initializes the native window/renderer, forwards events and drives frames. `EditorSession` handles scene/session persistence and document actions. `SceneEditor` coordinates editor commands with the document and asset library; `SceneViewport` owns picking, gizmos, focus and navigation input arbitration. `ScenePanels` contains docked Outliner/Inspector/Settings views and applies document commands. `AssetBrowserPanel` owns browser interaction state; its CPU `BrowserCatalog` stores virtual folder/display-name metadata while preserving managed asset IDs and paths. Keep these ordinary concrete classes with explicit dependencies; do not move their implementation back into `App` or introduce a service locator, plugin framework, generic message bus or speculative scheduler.

Engine public interfaces use `Vec2/3/4`, `Quat`, `Mat3/4`, `Ray` and `Aabb` from `src/math`. Matrices are column-major, multiply column vectors and default to identity; accumulation uses explicit zero matrices. GLM is private to the math implementation. Serialization writes packed scalar arrays, while renderer and ImGuizmo adapters use explicit `.data()` boundaries. No GLM, bx, ImGui or GPU-specific math type becomes persistent scene data.

### Dependencies and resource ownership

- The application constructs services and passes explicit dependencies. Controller and gameplay code do not call ImGui or bgfx.
- Scene components carry asset references and runtime handles, not ownership of shared GPU resources.
- The rendering boundary owns GPU meshes, material textures, environment resources, and targets. The tools adapter owns its ImGui GPU resources under the same bgfx lifetime.
- Physics owns Jolt worlds, bodies, shapes, constraints, and body-to-entity mappings. Actor/controller code uses the engine-owned adapter.
- Background decoding returns completed CPU results through publication queues. It does not create GPU resources or modify the world directly.
- The main thread initially owns authoritative world mutation and application-side render submission. Jolt may execute internal jobs; its callbacks collect facts rather than changing EnTT.

Current ownership is smaller and partly combined: `AssetLibrary` owns material texture/thumbnail GPU resources and managed source/persistence data, `Renderer` owns scene meshes/targets/programs and derived environment GPU resources, and `ImGuiBgfx` owns tool rendering resources. The renderer-owned environment object releases its resources before bgfx shutdown. Environment CPU preprocessing/cache preparation is invoked explicitly when a source changes, outside ordinary rendering. Separate asset decoding/storage from GPU caches when the relevant slice needs it; do not present that full separation as already implemented.

Model parsing, validation and package creation are CPU-only, using cgltf and MikkTSpace from the pinned manifest. The file-dialog callback queues paths; AssetLibrary processes imports on the main thread. Model images use AssetLibrary's existing texture ownership, with color/data image resources shared independently of per-binding sampler flags. Renderer prepares and caches GPU model resources outside frame submission, keyed by shared model identity, and reuses primitive buffers across scene instances and imported node references. Failed preparation reports diagnostics while retaining working resources. Scene/history records hold asset IDs rather than GPU ownership.

Every resource has a named owner. Shutdown cancels pending work, waits for outstanding jobs, releases runtime/physics state and GPU-owning services, then shuts down bgfx and SDL. Async results carry a project/world generation so stopped or replaced sessions cannot receive stale results.

### Identity and persistence

Persist stable asset and authored-object identities. Keep source locations readable and project-relative. Runtime entity references include world identity and entity generation. Do not retain component pointers across structural registry changes or queued work.

Never serialize temporary EnTT, Jolt, or bgfx handles. Introduce versioned serialization as new asset/scene data lands. Existing imports, material factors/map paths, and dock layout must remain usable when identity support is added.

Current material persistence writes version 2 of `assets/material.json`, adding `normalMap` and `normalStrength`; existing unversioned material files load with a neutral missing normal map and strength 1. Version 1 of `assets/environment.json` stores `map`, `enabled`, `intensity` and `rotation` in degrees. An empty environment path selects the built-in studio. Both files use managed project-relative source IDs; missing/unsupported assignments produce a diagnostic and fall back safely. Environment state is independent of material resets. Assets and `crimson_ui.ini` follow the optional `--project-root` location.

Model IDs are project-relative paths to versioned package manifests under `assets/models/<unique-id>/manifest.json`. Packages retain the original document and normalized managed geometry/image dependencies, so reopening never depends on the source folder. The older version-1 `assets/preview.json` remains preserved for the legacy single-model path; ordinary scene authoring uses native scene assets.

Version-1 scene files live at `assets/scenes/<unique-id>.crimson-scene`. They contain a display name, scene environment/exposure and entity records: persistent 64-bit IDs encoded as hexadecimal strings, names, parent IDs, sibling order, full local matrices and optional model/light components. Paths are managed and project-relative. Loading validates the complete candidate before replacing the active scene; malformed data, cycles, missing parents, invalid matrices and light-limit violations leave the current document intact. Missing model/environment asset references remain in the document and receive diagnostics when resolved. Saving writes a complete temporary file and atomically replaces the selected scene asset through the shared CPU file helper; a failed write retains the prior file, identity and dirty state. Scene deletion only removes the selected native scene file, never its shared imports. `assets/editor-session.json` stores the last saved scene, per-scene editor camera and legacy-preview recovery state separately from authored scene content. New scenes start empty with the default environment.

The `Scene` owns the EnTT registry. Persistent entity IDs are independent of registry handles; validated registry replacements invalidate runtime handles through a generation and increment the scene revision. Components contain identity, parent/order, a full affine local matrix, model reference and optional light data. Imported model nodes remain read-only asset internals instead of becoming editable EnTT entities. Light extraction supports up to four directional and sixteen point lights; creation, duplication and loading enforce those limits.

Reparenting computes `inverse(newParentWorld) * oldWorld`, retaining shear, reflection and nonuniform scale. Cycles and singular parents are rejected atomically. Multiple selection operates on topmost selected roots, so selected descendants are transformed, deleted or duplicated once. ImGuizmo manipulates a stable orthonormal proxy; its delta applies to the original world matrices, then converts back through the parent inverse. A single root uses its origin as pivot; a group uses its union-bounds center. The UI must not decompose/recompose persistent matrices through Euler angles or discard shear silently.

Outliner selection follows Quader's retained multi-selection drag and Ctrl/Shift range conventions. F2 edits names inline. Empty background is the unparent target; no common root is displayed. Group/Ungroup are atomic document commands, retaining world matrices and stable IDs. Inspector edits the primary entity's name and 12 affine matrix coefficients without decomposing the matrix. Its bottom row stays 0,0,0,1. Viewport gizmos use T/R/E/F without an operation toolbar or drag-status text.

Directional lights and their viewport arrows both transform the authored local -Z vector through the accumulated world matrix and normalize it. They must not reconstruct forward from cross(X,Y), which loses authored forward under reflection/shear. Quader's direct-axis policy is the reference; its -Y asset convention is not adopted by Crimson.

Browser cards have one common footprint and a letterboxed square thumbnail. Selection borders appear only for selected entries. `assets/browser.json` stores versioned virtual folders and display aliases; moving/renaming browser entries never moves managed model dependencies or breaks scene/material IDs. Mixed folder moves validate atomically; malformed metadata is preserved with diagnostics. Typed drags retain image assignment, and multiple model drags place one selected batch through scene history.

The document history holds up to 256 semantic commands as immutable before/after patches of affected entity values, scene properties and selection IDs. A continuous gizmo/property drag has begin/update/commit/cancel boundaries and contributes one command; cancel restores the exact original values. Delete/duplicate preserve subtree relationships and IDs across undo/redo, with newly allocated IDs reused by redo. Failed and unchanged edits do not clear redo. Unique state tokens track the saved state through undo and branching; failed saves never mark a document clean. Opening/new scenes reset history after success. Selection, camera navigation, focusing and asset import are outside scene history.

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

Focused CPU regressions cover environment preprocessing and math. The user performs visual acceptance of environment-only material response, roughness-dependent reflections, neutral/detailed normals and reopening. Historical captures are retained separately and do not impose a feature-completion workflow.

Single-scattering GGX can darken rough metals through missing microfacet multiple scattering. This remains a known limitation; a later compensation must be a consistent BRDF change rather than an exposure adjustment. Split-sum environment reflections also retain their grazing-angle approximation and do not reflect nearby geometry or provide occlusion/shadows. Area lights, advanced material layering, shadows, and a different light pipeline receive separate scopes.

## 4. Models and the first controllable actor

### Milestone 2: actual glTF/GLB import

Managed import feeds **independent scene entities**. One Import button accepts images and glTF/GLB files together; all usable assets and saved scenes appear in one browser. Importing and placing remain separate. Model placement preserves authored dimensions, orientation and internal node transforms; the scene entity supplies its independent root matrix. F frames the selected entity/subtrees by moving the editor camera. Scene navigation orbits the camera target without querying the old sphere.

Supported geometry includes indexed/nonindexed triangles, converted strips/fans, 8/16/32-bit indices, interleaved/normalized and sparse attributes, UV0/UV1 and vertex colors. Missing normals use flat faces; normal-mapped primitives without tangents use MikkTSpace with corner splits. Selected scene roots use the declared default scene, otherwise the first scene, otherwise parentless nodes. Hierarchy transforms are preserved with inverse-transpose normals, mirrored winding and tangent handedness; singular transformed draws are skipped with diagnostics.

Imported material factors are linear and separate from the sphere's editor-sRGB factors. Core metallic-roughness materials support base color/alpha, packed G/B roughness/metallic, normal scale, emissive, AO applied to indirect light, double-sided rendering and OPAQUE/MASK. Color textures decode sRGB while scalar/normal textures remain linear. Each texture binding retains UV-set selection and wrap/filter flags. Mipmap minification currently uses a warned non-mipmapped nearest/linear fallback. Missing referenced UV sets or sets beyond UV1 are rejected.

BLEND materials, skins, morph targets, points/lines and unsupported required extensions fail import. Animations, imported cameras/lights and optional unsupported extensions produce warnings while core static content remains usable. Managed imports stage and validate complete dependencies before publication; failed files in a mixed batch do not prevent other valid imports. GPU preparation failures retain usable scene resources and report diagnostics.

**Milestone 2b: scene authoring is implemented; user acceptance is pending.** New/save/save-as/open/delete operate on native scene assets. The dockable Outliner supports model, empty and light entities, multiple selection and world-preserving reparenting. The viewport supports model/light picking, T move, R rotate, E scale and F focus; widget/text editing and navigation keep explicit input ownership. Undo/redo groups a drag into one command, and scene persistence restores independent placements and light data.

**Next: milestone 2c, imported-material editing.** Define shared material edits versus per-instance overrides before exposing editable imported-material fields. Continue to retain linear authored factors, typed texture slots and shared GPU geometry. Skeletal deformation, animation playback, physics and advanced import extensions remain later work.

### Milestone 3: imported player visual and controller

Use the existing EnTT scene foundation for an entity with a transform, character motor, player controller and static model instance imported through milestone 2. Introduce Jolt and the runtime components when this slice starts; scene authoring itself does not require physics.

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

## 6. Build checks, user acceptance and future AI workflow

For documentation-only changes, verify consistency, status labels, and entry-point links. No application build is required.

For each implementation slice, use one incremental Release build after implementation owners finish, then focused CPU checks. Repeat or broaden checks only when failures or unresolved concerns justify it. Keep launching separate from building; preserve the user's project assets, assignments and dock layout. Report actual checks in the handoff and leave feature acceptance to the user.

Computer control, including mouse/keyboard automation and interaction with the user's applications, requires the user's permission. Noninteractive screenshots are allowed. Do not run routine native acceptance/capture suites or maintain `VERIFICATION.md` as a completion requirement; that file is historical evidence. The older capture scripts and smoke modes remain optional manual diagnostics.

`--project-root PATH` selects assets, persistence, layout and derived cache for isolated diagnostics. `--ibl-smoke` requires an explicit disposable project root and creates/assigns fixtures there; follow it with `--scenario ibl-reopen` in the same root to exercise reopening. Environment-only `ibl-*` scenarios cover metals/dielectrics, roughness, neutral/detail/zero-strength normals and extremes. These are targeted diagnostic modes, not the ordinary project launch path.

`Verify-Models.ps1` retains isolated `--model-smoke`, `model-reopen` and `model-sphere` scenarios for the older preview path. These mutating diagnostics reject the authored project root and are not the ordinary scene-authoring workflow.

| Milestone | Focused acceptance scenarios |
| --- | --- |
| IBL / normal maps | Environment-only metal/dielectric response; roughness-dependent reflections; neutral/detailed normals; seams/poles; stable valid extremes; exposure-independent grid/tools; assignment persistence |
| Model import | `.gltf` and `.glb`; multiple primitives/materials; nested transforms; 16/32-bit index needs; managed texture/buffer dependencies; independent instances; unsupported-feature diagnostics; reopening |
| Scene authoring | Scene save/open/delete and missing references; multiple placement/selection; selectable model/light gizmos; T/R/E/F; affine parenting; grouped undo/redo; dirty-state branches; default ImGui visuals |
| Player controller | Normalized diagonal movement; camera-relative directions; mouse facing while stationary/moving; ground targeting; coincident/missing aim; obstacle collision; camera follow; input capture/focus loss; repeated Play/Stop |

After the controllable static character works, define the skeletal-animation slice. Further ARPG foundations—additional actors, gameplay, audio, shadows, VFX, game UI, and dense-scene profiling—receive their own scopes and acceptance criteria.

Future AI must:

- Consult the relevant sections of this guide for subsystem changes and the repository workflow for build/run commands.
- Follow the current milestone and preserve already-working native features and project data.
- Use familiar engine UI conventions, including adjacent asset fields and immediate picker assignment.
- Keep ownership, data flow, input routing, and resource lifetime explicit.
- Introduce infrastructure when it serves an implemented feature; avoid unrelated settings and speculative systems.
- Update implemented/agreed/future status and README controls as slices land; report build/CPU-test results directly and leave feature acceptance to the user.
- Keep `App` lean, scene/history code CPU-only, editor orchestration explicit and public math independent of backend types.
- Discuss departures from the agreed architecture; proceed autonomously with routine choices inside it.

## 7. Research references

The Khronos Simple Engine tutorial is the architecture reference for clear subsystem boundaries, explicit dependencies, RAII ownership and presentation separation; adapt those ideas to SDL3/bgfx. The EnTT documentation informs registry/component/view usage, not a requirement to adopt every EnTT facility. Their API examples and advanced feature lists do not mandate a service locator, generic framework or additional engine work.

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
- [EnTT: Entity Component System](https://github.com/skypjack/entt/wiki/Entity-Component-System), for entity lifetime, components, registries and views; serialized identity remains engine-owned.
- [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo), for the transform manipulation widget; picking and input ownership remain editor responsibilities.
- [Brian Karis: Real Shading in Unreal Engine 4](https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf), especially pages 3–8 for BRDF conventions and split-sum IBL.
- [GPU Gems 3: GPU-Based Importance Sampling](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling), section 20.4 for filtering high-frequency HDR sources by sample footprint.
- [Filament: CubemapIBL](https://github.com/google/filament/blob/main/libs/ibl/src/CubemapIBL.cpp), for GGX reflection PDF and source-level selection; Crimson uses a twofold footprint overlap validated against its concentrated-emitter regression.
- [Filament PBR reference](https://google.github.io/filament/main/filament.html), for correlated Smith visibility and single-scattering energy-loss context. This is an equation reference, not a renderer dependency.
- [Jolt author's fixed-timestep guidance](https://github.com/jrouwe/JoltPhysics/discussions/968), for the initial 60 Hz physics choice.
