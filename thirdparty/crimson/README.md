# Shared Crimson renderer integration

The public 0.1 editor and Crimson Engine consume the canonical
[Crimson Renderer repository](https://github.com/qvertexworks/crimson-renderer).
Quader pins it at `thirdparty/crimson-renderer`; Engine pins the same commit at
`third_party/crimson-renderer`. The Full editor retains Filament. Each Quader
executable supplies its renderer through the same application interfaces; the
optional composition probes verify the two implementations.

`QUADER_CRIMSON_SOURCE_DIR` defaults to the submodule root, which exports
`Crimson::Renderer`. It has no source dependency on an Engine checkout. Native
configuration checks the canonical URL, the parent repository's recorded
gitlink and the selected checkout's commit. An explicit development checkout must
match that commit. Initialize the source explicitly after cloning:

```powershell
git submodule update --init --recursive
```

`renderer-manifest.json` records the schema, supported rendering recipes, material
and borrowed-Vulkan ABIs, vcpkg baseline, bgfx version/port revision and archive
digest. Configure validates those contracts and the installed bgfx SPDX record.
It checks the actual overlay patches/portfile and installed Release library,
interop header and shader compiler against the recorded SHA-256 hashes. Missing
or mismatched inputs fail before renderer targets are added. CMake never clones
source or downloads replacement renderer/dependency archives.

The native `vs2026` preset selects the submodule's maintained bgfx overlay. The
`native-app` manifest feature supplies bgfx tools, Vulkan headers and the renderer's
private math/JSON/PNG dependencies and meshoptimizer at Quader's existing vcpkg baseline. Portable
SDK builds select their own features and do not inherit this preset-local overlay
or require the renderer submodule. `QUADER_CRIMSON_DEPENDENCY_PREFIX` is empty by
default, selecting the native build's vcpkg target prefix. It can explicitly reuse
another matching installation; its provenance checks remain mandatory.

## Ownership contract

Qt owns the Vulkan instance, device, graphics queue, window swapchain, and current
composition texture. Crimson borrows the context and uses only capabilities the
owner attests are enabled. Missing extension evidence disables that optional path.
Quader's adapter pins those capabilities for the device generation.

One Crimson device/frame owner creates independent offscreen color and
depth/stencil targets. GPU copy returns Qt's current destination in shader-read
layout. CPU submission completion and GPU completion remain distinct. Ordinary
presentation must not read pixels back or idle the device each frame. Resources
must be retired after all rendering and copy use, before the borrowed Qt device.

Quader retains documents, modeling, assets, semantic overlays, exact selection,
tools, history, and release policy. Immutable extraction data is translated
to neutral Crimson descriptors at `src/render/crimson`. Full remains on Filament;
the Public host links `quader_render_crimson_host`. A clear-image integration probe
alone does not establish modeling-editor parity.

`quader_render_common` contains CPU extraction and overlay builders. The shared
application, UI and editor API consume `RenderHost`, `RenderSceneSink` and
`RenderView`; each final executable links one `create_app_render_host()`
composition. `quader_render_full_host` supplies the Filament implementation.
`quader_render_crimson` adapts immutable scene and preview snapshots to
`Crimson::Renderer`. Its cache retains position, attribute, index and image pins
independently, and rejects an incomplete generation without changing accepted
preview suppression. Crimson Engine's rich forward recipe retains its PBR,
generated materials, lighting, shadows, GI/AO and presentation; Quader's modeling
recipe retains its editor display contracts. The canonical library owns their
common device, GPU resources, target, frame and retirement mechanisms. Host
adapters retain SDL/Qt/ImGui, application scenes, asset catalogs and history.

The Public host requires Qt's Vulkan composition path. Legacy OpenGL launch
selectors apply to the Full host. Its public-profile test executable selects the
Crimson factory too; an explicit Full renderer link remains there only for the
existing concrete helper fixtures. The shipping Public link closure excludes
the Full host, Full renderer and Filament SDK.

All visible panes preflight their scene/editor generations, resources and pass
reservations before any pane records commands. A rejected scene translation or
later-pane preflight leaves every accepted target available for copying. Scene,
preview suppression and editor coverage advance together after one shared
submission. Position, attribute, index, image and point-visibility streams retain
independent immutable lifetimes.

Collision snapshots use Quader's shared `CollisionShapeOverlayPlanner` for
geometry, placements and exact preview suppression. Its deduplicated triangle
edges include collision diagonals independently of authored topology wires.
Keep the existing selected/unselected opacity, scene-occluded fill and per-pane
wire visibility, including collision wires in Wireframe mode.

Wireframe preserves the existing X-ray topology display: it excludes scene color
and scene depth, while selected-object masks retain independent occupancy and
nearest selected depth. Component markers follow the existing Wireframe policy.
Picking still uses exact scene-surface queries and the Backface Selection setting;
visible X-ray wires do not imply selection through foreground geometry. See the
[Wireframe runtime contracts](../../tests/selection_overlay_runtime_tests.cpp) and
[selection fallback](../../src/editor/viewport/viewport_scene_hit_policy.cpp).
Crimson currently supplies no optional GPU selection-depth accelerator, so the
existing exact CPU fallback remains authoritative.

Normal shutdown consumes all bgfx allocation-recycling buckets before the final
memory-cache eviction, then frees those evicted allocations before returning the
borrowed device to Qt. GPU completion failure retains the exceptional quarantine
contract; the host cannot allow Qt to destroy a device with unknown in-flight
ownership.

## Composition gate

After initializing the submodule and preparing its matching maintained bgfx
overlay through vcpkg, configure the canonical Quader tree:

```powershell
cmake --preset vs2026 `
  -DQUADER_BUILD_CRIMSON_COMPOSITION_PROBE=ON
cmake --build --preset vs2026-release --target quader_crimson_composition_probe
build/vs2026/Release/crimson-composition-probe/quader_crimson_composition_probe.exe --output-dir build/crimson-composition-gate
```

An existing build cache from the former Engine-checkout integration must reset
`QUADER_CRIMSON_SOURCE_DIR` to `thirdparty/crimson-renderer` and clear the optional
dependency-prefix override, or select an explicitly matching dependency prefix.
An explicit alternative source checkout still has to match the recorded gitlink.

The dedicated probe checks actual GPU-composited clear pixels, target replacement,
visible crops, resize, hide/show, root replacement and shutdown. Its readback is
verification only. It does not prove shaded meshes, overlays, selection, previews,
four-pane scheduling, or a migrated public executable.

The separate `quader_crimson_scene_composition_probe` target uses the same
dependency pair. Run it from
`build/vs2026/Release/crimson-scene-composition-probe` with an explicit
`--output-dir`. It checks presented and offscreen mesh pixels, winding/culling,
sRGB image sampling and final encoding, camera and pose changes without geometry
uploads, malformed scene rejection, transformed normals, and root replacement.
Readback and desktop capture belong to these verification executables only.

The paired `quader_filament_editor_composition_probe` and
`quader_crimson_editor_composition_probe` targets exercise the production
four-pane Qt panel through the neutral interfaces. Both require `--resource-dir`
pointing to the native Release runtime and a separate `--output-dir`. Their
`--overlay-parity` option adds selection, components, hover, live preview, grid,
display modes and gizmos. Run the Full reference first, then pass its output
directory to Crimson with `--reference-dir` for matched overlay captures.
The Crimson fixture also injects a malformed source candidate through the sink
and checks accepted-pixel retention and corrected retry.

The paired `quader_filament_depth_composition_probe` and
`quader_crimson_depth_composition_probe` targets use the same resource/output
arguments. They check a fixed physical target in perspective and orthographic
views: oblique wire continuity, real foreground depth, grazing edges, source
wires and unchanged-frame retention. Source faces must be visibly present in
their baselines, so missing scene geometry cannot produce a false pass.

Use process-local test settings when running these probes. Optional Vulkan
validation likewise belongs to the diagnostic process, without changing the
system layer configuration. Pinned Qt 6.10.3 itself creates optimal textures with
`PREINITIALIZED`, which current validation reports as
`VUID-VkImageCreateInfo-initialLayout-12478` in both host paths. Keep that diagnostic
distinct from renderer-owned resource, synchronization and child-object errors;
do not suppress it or claim a completely clean validation run while it remains.

## Dependency provenance

Crimson and Quader are authored proprietary projects used under the user's
authorization. bgfx is the existing Crimson dependency, using the existing vcpkg
baseline `d0c191d142b7bc048141ddfb618aa9ae5dba0233`, bgfx.cmake
`1.129.8940-496`, maintained port revision `14`. The authoritative overlay and
original source archive SHA-512 live in the renderer submodule's
`cmake/vcpkg-ports/bgfx` and `renderer-manifest.json`. Changes belong in that recipe and
patch, never extracted build sources. Its BSD-2-Clause notice is retained in
[the dependency notice](../licenses/bgfx-BSD-2-Clause.txt). Runtime link dependencies
retain their notices for [bx](../licenses/bx-BSD-2-Clause.txt),
[bimg](../licenses/bimg-BSD-2-Clause.txt),
[TinySTL](../licenses/tinystl-BSD-2-Clause.txt) and
[miniz](../licenses/miniz-MIT.txt). The package tool includes repository notices
from `thirdparty/licenses`, including the canonical renderer's
[meshoptimizer MIT notice](../licenses/meshoptimizer-MIT.txt); the opt-in gates and shader compiler are not added to
the public release package.
