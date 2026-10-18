# Reading the Shadows diagnostics

The **Shadows** section in **Scene diagnostics** reports the active world's shadow allocation and the shadow work submitted in the current renderer frame. It combines Scene and Game. It does not report every light's direct illumination cost, triangle count, total editor draw count or GPU duration. Work counters reset once at `Renderer::advanceStreaming`, and diagnostics draw after that frame's world submissions. They describe CPU submissions, not completed GPU timestamps, simulation ticks or totals since startup. If neither world view submits, allocation fields can retain their last known values while work counters reset to zero.

For current feature ownership, Inspector controls and renderer limits, see [Renderer and character lighting](RENDERER_AND_CHARACTER_LIGHTING.md) and [the architecture](ENGINE_ARCHITECTURE.md). This page describes the diagnostics in the current implementation; historical benchmark evidence is linked separately.

New point lights start with **Cast Shadows off**. Eight point-light entities can illuminate the scene while this panel reports zero active point-shadow lights. A shadow-casting directional light is reported separately. Current limits are six faces per point map, sixteen resident point slots and four complete point-map updates per world frame.

## Point source size and softness

**Point light → Source Radius (m)** controls emitter size independently of the light's influence Radius. Zero uses the original 3×3 point-shadow filter. Positive values add contact-hardening PCSS: a blocker search estimates occluder distance and widens the comparison filter as receiver/blocker separation increases. This works with Specular Scale zero. Source Radius also broadens reflective highlights when specular is enabled; diffuse attenuation remains independent.

Changing Source Radius updates receiver filtering without regenerating cached six-face depth. `Lights rendered` can stay zero and `cached` stay nonzero while the shadow visibly softens. The existing counters report depth submissions, not PCSS tap cost or GPU duration. Positive-radius points add bounded search/filter sampling work; source-zero points retain the original filter cost. Directional lights have their separate Shadow Angular Diameter control and shared four-cascade filter described in the [lighting guide](RENDERER_AND_CHARACTER_LIGHTING.md#directional-shadows-for-first-person-cameras).

## What each row means

### `Lights: A / C allocated; R px per face`

- **A** is the number of admitted, enabled, shadow-casting **point lights** in the active world. Directional lights are excluded. Ordinary lights with Cast Shadows off are excluded.
- **C** is the point-light capacity of the current GPU atlas. Each point-light slot contains six depth faces. It is a storage capacity, not a per-frame update budget or the number of faces.
- **R** is the inner resolution of each point face and directional projection: 256, 512 or 1024 pixels. Each physical atlas tile also has two guard texels on each side, so its stored width and height are `R + 4`.

**Edit → Preferences → Rendering → Shadow resolution** changes R for both point and directional shadows. Higher resolution gives finer shadow texels and increases memory approximately with the square of resolution. Changing it replaces the atlas and requires the maps to be rebuilt.

The active point-shadow limit is sixteen. Eligibility requires an enabled light, Cast Shadows on, finite position, and positive finite radius and intensity. With more than sixteen eligible lights, admission prioritizes squared distance to the frozen world focus divided by squared radius, with persistent light identity resolving ties. The frozen focus is the visible Game camera's eye, otherwise the Scene camera's eye; Player mesh presence and the free editor camera target do not select Game shadows. Lights that are not admitted continue to illuminate normally without a resident shadow map.

Atlas capacity grows in powers of two according to the highest occupied stable slot. It does not automatically shrink when lights are removed. Therefore `8 / 16 allocated` is possible and is not a failure to count the eight lights. Likewise, after disabling all point shadows while keeping the sun active, `0 / 8 allocated` can be legitimate retained storage. A fresh scene with only a shadow-casting sun normally reads `0 / 0 allocated`; the resolution can still read 1024 because the same atlas contains the sun's directional projections.

### `Lights rendered: U; cached: H; faces: F`

These three values concern **point lights only**:

- **U** is the number of complete point-light shadow maps rebuilt this frame.
- **H** is the number of admitted point lights whose stored shadow content matched the current caster/projection signature and required no rebuild.
- **F** is the number of point-shadow faces submitted this frame. One complete point map always updates all six faces, so `F = 6 × U`.

The active-world budget is **four complete point maps / twenty-four faces per renderer frame**, shared by Scene and Game. Opening Game does not grant another four-map budget or render the same point maps twice.

Cached maps still participate in lighting and shadow sampling. `Lights rendered: 0; cached: 8; faces: 0` is the intended settled state for eight stationary shadow-casting point lights in a static scene. Zero rendered faces means that their depth maps are being reused, not that shadows have stopped working.

The cache uses caster membership, transforms, relevant material/coverage signatures and resident geometry bindings, plus the light's position and radius. Changing geometry, importing a replacement mesh, publishing new geometry detail, changing alpha coverage, toggling casting or reloading shaders can trigger refreshes. Material publication can invalidate conservatively through its prepared signature, even when a particular edit does not visibly change its silhouette. Short bursts during resource refinement are therefore possible in an otherwise stationary scene.

### `Deferred: D; awaiting a valid map: V`

- **D** is the number of dirty admitted point maps left pending after this frame's update selection.
- **V** is the subset of those pending maps that cannot currently be sampled because they are new, invalidated, or belong to a changed light projection.

An unchanged light position and radius permit an old coherent map to remain in use while caster-only changes await refresh. Such a light contributes to D, but not V and not H: it is dirty and deferred, even though the old map is still sampled.

A new light, moved light, changed radius or invalidated atlas must not sample depth generated for a different projection. Until its complete replacement is ready, that point light illuminates without shadows. It contributes to V while deferred.

For a normal frame with a valid active atlas, **active = rendered + unchanged cached + deferred**, and **awaiting a valid map ≤ deferred**.

Deferral is expected when more than four admitted maps become dirty at once. It should drain after the scene settles. Persistent dirty maps gain age priority after four deferred frames, so continuously changing nearby lights cannot permanently starve older work. This is a frame-based policy, not a fixed millisecond deadline. At lower frame rates the same number of deferred frames lasts longer in real time.

With eight continuously affected lights, some character shadows can temporarily show older caster positions. The scheduler bounds complete-map updates; separate static/dynamic caster layers and unrestricted per-frame shadow refreshes are not implemented by this budget.

### `Casters: N; draws: B`

- **N** is the number of prepared **model instances** in the world's shadow-caster packet. It covers the caster set prepared for both camera views, all relevant ordinary point/directional lights. It is not the number of triangles, meshes actually visible in the Game image, actors, lights, or per-light caster relationships.
- **B** is the total number of shadow depth draw submissions this frame across ordinary point and directional work in the active world.

One model instance can contain several primitives. The same primitive can draw into several point faces, several lights or several directional projections. A caster can also be outside the visible camera image while still casting a shadow into it. N therefore need not resemble B or the visible object count.

Each updated directional cascade includes **one fullscreen depth-clear draw**, in addition to its caster draws. Point faces use a view depth clear instead, which does not add a draw to B. For example, a fresh empty ordinary directional projection can contribute one draw even when no caster primitive is drawn into it.

Cached shadows submit no depth draws. A static scene can legitimately show many casters and zero draws. Candidate instances later rejected because of blend mode, unavailable geometry, collapsed transforms or per-projection visibility can also make N larger than the number that actually draws.

### `Directional contexts: Q; rendered: U; cached: H`

A **context** is one shadow-casting directional light evaluated for one camera. It is not a unique directional-light entity.

For one sun, Scene submitted alone normally gives Q = 1; Game alone gives Q = 1; Scene and Game together give Q = 2.

**Rendered** counts contexts with at least one directional projection refreshed this frame. **Cached** counts contexts where all required projections could be reused. A context with one dirty cascade and three reusable cascades still counts as rendered.

Scene and Game use the same four stable perspective-aware cascades per light/camera. Each has quantized sphere-fit extents, texel-snapped centers, blended split transitions and bounded Angular Diameter softness. Scene/Game camera poses and lenses remain independent; they share the immutable world caster snapshot and one shadow owner.

### `Directional cascades written: W`

W counts individual directional cascade maps refreshed this frame, summed across submitted cameras and lights. One sun with both views can initially write eight cascades: four for Scene and four for Game. After caching, W can be zero while every shadow remains visible. A moving actor invalidates every relevant cascade containing its changed caster content; there is no separate focused character projection. Camera movement, shadow range/angular size, geometry refinement or changed caster membership can also change the projection/signature.

The former focused Game and dedicated Character Key counters are removed. Angular Diameter changes can alter the world halo and fitted projection, so they can redraw directional maps even though a point light's Source Radius changes only receiver filtering.

### `Atlas allocations: M MiB`

M estimates the active-world renderer's D32F texture storage: **width × height × 4 bytes**, divided by **1,048,576** for MiB.

It includes point tiles, directional tiles, guard texels, unused packing space, retained capacity and old atlas allocations waiting for retirement. It excludes ordinary material textures, model buffers, scene-color targets and the separate saved-scene thumbnail shadow owner. Scene and Game directional contexts are reserved even when Game is closed, so closing Game need not reduce M. Removing a light does not promise immediate shrinkage.

Replaced atlases remain explicitly owned for two renderer-frame boundaries before their GPU handles are destroyed. A resolution or capacity change can temporarily show old-plus-new storage. Driver/device memory counters may retire allocations later and include other resources; M is engine allocation accounting, not a complete hardware VRAM reading. The Game F1 overlay's **Shadows** value includes the separate thumbnail owner and can exceed this panel's M.

## What eight point lights and one sun should show

These examples use eight ordinary shadow-casting point lights and one sun. Removed private actor lights and Key silhouettes contribute no maps or counters.

With all eight point lights' **Cast Shadows off**, they still illuminate the scene. On a fresh allocation expect `Lights: 0 / 0 allocated`, with the global resolution still visible. Point rendered/cached/faces and deferred/awaiting counts are all zero. The sun gives one directional context per submitted view, initially rendered and then cached. Casters, draws and memory can remain nonzero because the sun needs shadows. Enable Cast Shadows on the point lights before judging their shadow caching or update budget.

The inspected saved scene had this default configuration. The [eight-point-light follow-up](SHADOW_QUALITY_CORRECTION.md#eight-point-light-follow-up) records the as-saved check and isolated copies with all eight shadows enabled. Its enabled static case reached eight cached maps, zero faces and zero deferral. During the tested moving-character path, four maps refreshed and four stayed cached, with twenty-four faces and zero deferral throughout 2,000 measured samples. That path affected four maps; it does not establish that all eight can refresh in one frame.

### Enable shadows on all eight, then leave everything still

Assuming a fresh allocation, valid resident resources and unchanged light/caster data, the point counters normally settle as follows:

| Frame after activation | Active | Rendered | Cached | Faces | Deferred | Awaiting valid map |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| First | 8 | 4 | 0 | 24 | 4 | 4 |
| Second | 8 | 4 | 4 | 24 | 0 | 0 |
| Settled | 8 | 0 | 8 | 0 | 0 | 0 |

These are expected scheduling states, not a promise about exact editor startup frame numbers. Asset refinement, shader publication, additional moving casters or a changed world can introduce further refreshes. Atlas growth also invalidates existing directional maps, so the sun can refresh during the transition.

Once settled, N can remain nonzero while B and W are zero. With both views and one sun, Q normally stays at two and its cached count stays at two.

### Expected memory for a fresh eight-point-light allocation

Eight point lights require forty-eight faces. One sun reserves two four-cascade camera contexts. The point region uses twelve tile columns and four rows; the directional 2×2 blocks add two rows across that atlas width. Unused packing space is allocated too.

| Inner resolution | Point storage only | Eight points + one sun, Scene/Game allocation |
| --- | ---: | ---: |
| 256 | 12.4 MiB | 18.6 MiB |
| 512 | 48.8 MiB | 73.1 MiB |
| 1024 | 193.5 MiB | 290.3 MiB |

At 1024: pitch = **1028**, width = **12,336**, height = **6,168**, and storage = **12,336 × 6,168 × 4 = 304,353,792 bytes = 290.2544 MiB**. These are layout-derived D32F allocation values, not device-memory measurements.

This table assumes eight-slot point capacity, one-slot directional capacity and no retired allocation. A previously larger atlas, more directional lights, atlas replacement or different retained slot placement changes the result. Depth targets remain separate from the streamable asset-cache retention target.

## Practical checks

1. **Establish the static state.** Enable shadows on the intended eight point lights, allow materials/geometry to finish preparing, then stop camera and character movement. Expect eight cached point maps, no point faces, no deferred maps and no shadow depth draws once the directional maps also settle.
2. **Move the character through the light volumes.** Point maps whose caster data changes become dirty. If all eight need refreshes, expect up to four updates/twenty-four faces and some deferred caster-only work. Awaiting a valid map should normally stay zero because the stationary lights retain coherent projections. Relevant Scene/Game cascades can refresh, and a following Game camera can change their projections.
3. **Stop moving.** Pending point work should drain and return to eight cached maps. A stable nonzero deferred count after resources and the scene settle warrants investigation. Stopping Play may switch the presented world and cause a one-time cache warmup; it is different from merely stopping character movement.
4. **Move one point light.** Its position/radius projection changes. Expect one whole-map update/six faces when scheduled. If it is deferred, awaiting a valid map increases and that light temporarily renders without shadows. It must not sample the map from its previous position. Moving only a light's color or positive intensity should not require a depth refresh.
5. **Adjust Depth Bias or Normal Bias.** The receiver comparison changes immediately. Point depth can stay cached; directional projection padding can change and trigger a redraw. Bias controls are not a general substitute for correct projection coverage or receiver-plane calculations.
6. **Move only the camera.** With static lights/casters and eight admitted point lights, point maps normally stay cached. With more than sixteen eligible lights, camera-based admission can legitimately change the resident set. Directional camera fits may refresh. New resource detail arriving because of the changed view can also trigger legitimate map updates.
7. **Adjust Attenuation, Range Falloff, Edge Softness or Specular Scale.** These change receiver lighting rather than stored depth. A positive-intensity, unchanged-position/radius light can retain its cached map. Source Radius behaves the same way for map reuse while changing PCSS filtering, as described above.
8. **Disable one point light's Cast Shadows.** Active point count decreases, while direct lighting remains. Capacity and atlas memory can remain at their prior high-water mark. Turning the light itself off or setting intensity to zero also removes it from point-shadow eligibility.

## Interpretation limits

Point work counts unique shared-world maps; directional contexts accumulate across cameras. Contexts, projections, faces, model instances and draw calls have different units. Zero depth draws does not eliminate shadow sampling cost in ordinary surface/particle shading. The current shared atlas uses uniform resolution; variable per-light resolution, automatic shrinkage and separate static/dynamic caster layers are additional systems.

## Source trace

[Panel labels](../src/tools/SceneDiagnostics.cpp), [counter/cache/allocation owner](../src/crimson-renderer/PointShadowRenderer.cpp), [update scheduler](../src/crimson-renderer/ShadowUpdateScheduler.cpp), [light admission](../src/crimson-renderer/ShadowLightSelection.h), [renderer aggregation and caster draws](../src/crimson-renderer/Renderer.cpp), [tile packing and fitting](../src/crimson-renderer/DirectionalShadowMath.cpp), and [editor draw ordering](../src/editor/EditorSession.cpp).
