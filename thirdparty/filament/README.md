# Filament presentation and lighting patch

## Daily upstream check

[UPSTREAM_STATUS.md](UPSTREAM_STATUS.md) records the last successful local check day, our pinned
version and patch identity, the latest stable release, and an exact snapshot of upstream `main`.
Root `AGENTS.md` instructs the coordinating agent to run the checker at task start or when
resuming on a new day. This is an agent instruction, not a background scheduler or an executable
Markdown file; no check runs while the project is idle. A user instruction to stay offline wins.

Run from the repository root (Python 3.10+ and Git, no additional Python packages):

```powershell
python tools/check_filament_updates.py --allow-network
python tools/check_filament_updates.py --allow-network --force
python -B tools/test_filament_updates.py
```

The first command is quiet after a successful check on the same local day. `--force` checks again.
Without `--allow-network`, a due check reports that network access is required and makes no
requests. Requests read only public `google/filament` metadata; nothing is fetched into a source
checkout, applied, built, pushed or posted. An optional existing `GH_TOKEN` or `GITHUB_TOKEN`
environment variable authenticates these GETs; unauthenticated access also works within GitHub's
rate limits. Tokens are never saved in the cache or report.

The active CMake pin remains authoritative. The checker derives the displayed version from it,
and hashes the manifest identity and patch bytes so either changing forces a fresh baseline.
Machine state lives in `<git-common-dir>/quader/filament-updates/<fingerprint>.json`; linked
worktrees with the same pin share the daily cache and an operating-system lock. Each worktree's
Markdown report can be regenerated from that cache. The report is generated repository content;
review its diff, and put durable integration decisions in this README rather than editing it.

Checks capture `main` as an immutable SHA, then paginate every commit since the previous successful
SHA. A separate pin-to-main comparison retains candidates across daily checks, including days with
no new commits. Initial checks establish a baseline and do not call existing history "new today".
This candidate list is not a review ledger; reviewing a commit does not remove it. Release/main
divergence and cherry-picks mean ancestry candidates may already have equivalent changes in our
release. Review the actual diff before selecting an upgrade or backport.

Overlap reporting uses returned net file paths, including rename sources. GitHub caps that list
at 300 files; reaching the cap is explicitly marked incomplete. Even a complete net comparison
does not prove patch compatibility or cover changes later reverted. Commit summaries are escaped
display data. Large reports show the most recent 60 entries per list, with full comparison links
and the complete commit lists in the local cache.

Network failures, rate limits and incomplete pagination leave the successful date/SHA unchanged,
so a later task retries. A check has a 45-second request budget with individual request timeouts;
startup failures do not block other work. Rewritten `main` history also preserves the old state.
After manually reviewing a rewrite, explicitly accept a new baseline with:

```powershell
python tools/check_filament_updates.py --allow-network --reset-main-baseline
```

If a cache is corrupt or from an unsupported schema, preserve that exact JSON file under a backup
name before running a fresh check; do not silently discard its previous SHA. Upgrade decisions
remain separate: assess the Quader extensions and presentation lifecycle changes, rebase or
backport selected changes, update the pin/provenance/guardrails together, rebuild the matching SDK
and materials, then run the required app and GPU verification described below.

The checker uses GitHub's [commit comparison API](https://docs.github.com/en/rest/commits/commits#compare-two-commits)
and [latest stable release API](https://docs.github.com/en/rest/releases/releases#get-the-latest-release).

## Maintained renderer patch

Quader uses the complete upstream Filament `main` snapshot at commit
`aab101b4c038da83f71a16accb8279516a0247b9` (checked 2026-09-14), with the `quader-vsync9`
patch in this directory. This is an unreleased source snapshot, not a released version tag.
Its upstream version label is still `1.76.1`; the exact commit and `upstream_ref: main`
distinguish it from the v1.76.1 release archive.
The archive identity is recorded in
[`filament-main-aab101b4c038-quader-vsync9.json`](filament-main-aab101b4c038-quader-vsync9.json).
Filament and changes to these upstream source files are licensed under Apache-2.0; see
[`../licenses/filament-Apache-2.0.txt`](../licenses/filament-Apache-2.0.txt). The patch modifies files
carrying the upstream license notices. This directory contains the patch and provenance only.

`SwapChain::CONFIG_UNSYNCHRONIZED` requests `VK_PRESENT_MODE_IMMEDIATE_KHR`. Each native Vulkan
surface selects Immediate only when advertised by that surface; otherwise it retains FIFO.
Omitting the flag preserves upstream FIFO presentation. Mailbox is deliberately not a hidden
fallback. Headless swapchains and other backends retain their existing behavior.

`VulkanPlatform::onSwapChainPresentModeSelected(void* nativeWindow, bool unsynchronized) noexcept`
is a protected virtual callback with a default empty implementation. It reports the actual mode
after successful creation and recreation, including resize. It runs on the backend thread, so
Quader's override publishes synchronized state and never calls the UI or Engine from the callback.
The platform must outlive the Engine. Vulkan creation diagnostics record requested and actual modes.
`FILAMENT_QUADER_VSYNC_PATCH_VERSION` is `9` in the public backend header. Revision 6 rebased
revision 5 onto the pinned `main` snapshot while preserving all four extensions: VSync control,
light distance attenuation, hardware-sRGB presentation correction, and opt-in Burley/Lambert
diffuse selection. Revision 7 introduced Vulkan swapchain replacement retention and presentation-
semaphore lifetime tracking. Revision 8 introduced optional Windows presentation scaling.
Revision 9 preserves hidden backing-surface pixels and corrects depth reconstruction for shifted
AO projections. These revisions keep the same upstream source and the four existing extensions.
The patch retains upstream's `VulkanSync` constructor and swapchain `setFrameRate` implementation.
All upstream changes through the pinned SHA are included; no backport selection is needed for
that snapshot. Future upstream commits remain separate updates detected by the daily checker.

### Vulkan swapchain replacement lifetime

Surface recreation supplies the current handle as `oldSwapchain` and retains retired handles.
`VulkanPlatform::SwapChainBundle::generation` associates the wrapper's retained `mFinishedDrawing`
semaphore owners with each old chain; raw handles do not preserve ownership. Headless/custom
bundles default to generation zero. These public record layouts require matching rebuilt headers
and libraries.

Pending retirement uses a reusable acquisition fence for successful or suboptimal images.
Reacquiring a presented replacement image with a signaled fence proves completion of that
presentation and earlier resources on the same queue. A persistent
`ImageSyncData::completedRetirementGeneration` watermark releases their semaphore owners. See the
[Khronos acquire-fence retirement workaround](https://docs.vulkan.org/samples/latest/samples/api/swapchain_recreation/README.html).
Ordinary acquisition adds no fence wait after retirement completes; repeated resizes retain
pending generations until that proof. Suboptimal acquisition proceeds through rendering and
presentation to consume its readiness semaphore. Capability failure or zero extent preserves the
active bundle, and unsuccessful recreation skips update/acquire. Creation failure retains the
fatal error contract and clears the active handle because Vulkan retires a supplied `oldSwapchain`
even on failure; see the [creation contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html).

GPU completion protects render/depth resources. Final teardown retains upstream's queue-idle
fallback with semaphore owners alive; the general presentation-engine teardown limitation remains.
Pending acquire waits and rapid-resize retention require native measurement.

### Optional Windows presentation scaling

Windows optionally enables `VK_KHR_get_surface_capabilities2`, `VK_EXT_surface_maintenance1` and
`VK_EXT_swapchain_maintenance1`, pruning missing dependencies before instance/device creation.
The maintenance feature is queried and explicitly enabled for owned devices; adopted/shared
devices keep the fallback because their enabled features are unknown.

Creation/recreation requests `VK_PRESENT_SCALING_STRETCH_BIT_EXT` only after a successful query
for the selected FIFO or Immediate mode confirms scaling, extent and image-count support. Counts
satisfy ordinary and mode-specific limits, both gravity fields stay zero, and failed checks keep
platform-default creation. Headless behavior is unchanged; ordinary frames add no query or
allocation. Diagnostics record `present-scaling=STRETCH` or `PLATFORM_DEFAULT`. See the
[scaling contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentScalingCreateInfoKHR.html)
and [mode-specific capabilities](https://docs.vulkan.org/refpages/latest/refpages/source/VkSurfacePresentScalingCapabilitiesKHR.html)
(equivalent EXT names in the pinned headers).

### Hidden backing-surface pixels

Revision 9 sets `VkSwapchainCreateInfoKHR::clipped` to `VK_FALSE`: reserved pixels outside the
visible parent clip must remain owned and rendered before reveal. `VK_TRUE` permits undefined
occluded contents and discarded fragment work. See the
[Vulkan clipping contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html).

`RenderSystem::supports_hidden_native_pixels()` enables retained vertical backing only on Vulkan.
The [viewport panel](../../src/ui/qt_viewport_panel.cpp) anchors it at `(0, 0)` within a native clip
host. Bottom-dock toggles change the visible clip while preserving the scene HWND's extent and
origin; centering the backing would move its previous image. Visible camera/input/HUD dimensions
remain separate from the backing raster. Scene and overlay projections scale/shift into that
raster, and the ground grid covers the reserved region. Bloom retains its semantic options and
compensates only vertical resolution by backing/visible height, within Filament's resolution limits.
Retained signatures gate redundant projection/bloom setters, and native geometry queries are cached.
The Asset Browser visibility change preserves `updatesEnabled()` while holding Qt painting until
layout has updated native pane clips, preventing an uncovered dock footprint from being painted
over the expanded scene. Reveal activates the dock's layout before raising it and settling the
main-window layout, so its first minimum already reflects current child metrics.

SSAO's `computeViewSpacePositionFromDepth()` uses projection translation to derive its UV center
instead of assuming `(0.5, 0.5)`. This keeps depth-derived positions/normals aligned with the shifted
camera; centered projection retains the original calculation. The native AO crop oracle checks
perspective and orthographic contacts, with strict noise-aligned and tiled-noise comparisons. It
passes with the correction and fails when only the old depth helper is restored.

Shorter scheduling, STRETCH and synchronous GPU submission did not preserve the composed image
when the HWND changed size. Acceptance therefore examines actual window-capture frames: the old
scene intersection stays fixed until one current projection replaces it, with valid grid pixels
in the newly exposed region. Window-targeted captures pass selected/unselected menu/Ctrl+Space
show/close cases in Shaded with VSync off, Rendered with VSync on and DPR 2 with VSync on; no eligible
captured frames were dropped by the retention limit. The native fixture also passes first-reveal
metrics at DPR 2 and repeated/interrupted resize with minimize/restore. Qt browser chrome can still
repaint in stages outside the scene; verified continuity applies to the viewport image. Settled
dimensions or successful submission alone do not prove compositor continuity.

### Preserved color and lighting extensions

Color-grading LUTs, dithering, FXAA, and scaling remain in the encoded output space. Filament's
unchanged dither assumes this space and an 8-bit destination; applying it to linear values before
hardware sRGB encoding amplifies dark noise. The renderer now inserts a terminal shader blit when
encoded color grading targets the renderer's swapchain and its effective flags include
`CONFIG_SRGB_COLORSPACE`. The blit applies the sRGB EOTF so the hardware's OETF writes the intended
encoded values exactly once. Unprocessed shaded views, overlays, explicit linear color grading,
custom render targets, and swapchains without hardware sRGB encoding keep their existing paths.

The presentation conversion preserves coverage alpha by unpremultiplying, decoding RGB, and
premultiplying again. Opaque intermediate alpha can contain FXAA luminance and is restored to one.
Scaling retains alpha while deferring blending to the terminal conversion, including sharpened
bilinear upscaling; material-instance reuse resets conversion uniforms on every blit path.
Revision 3 added an internal presentation pass without changing light or material shading.

`LightManager::Builder::distanceAttenuation(float)`, `setDistanceAttenuation()` and
`getDistanceAttenuation()` expose the point/spot distance exponent, defaulting to `2`. Non-finite
inputs are ignored. Directional lights retain the setting without using it. Runtime changes
publish the same component notifications as intensity and falloff changes. The exponent occupies
the previously reserved full-float `LightsUib` slot after direction (`data[1].w`); the light uniform
remains 64 bytes, with its other offsets unchanged.

Exponent `2` uses the unchanged upstream inverse-square shader calculation, including its 1 cm
minimum-distance convention. Other values use `pow(max(distance, 0.0001), -exponent)` to match Godot's
distance response. The existing smooth range cutoff and camera-distance fade still apply, and
zero cutoff returns before evaluating the power. Range, clustering, spotlight cone attenuation,
PBR shading, and shadows retain their existing behavior. Exponent `0` removes distance decay;
negative exponents increase brightness with distance. Extreme exponents or distances can overflow
floating-point shader arithmetic, as with the corresponding Godot formula.

Materials can opt into direct diffuse selection by defining `QUADER_HAS_DIFFUSE_MODEL` at the
start of their fragment block and declaring an `int diffuseModel` material parameter. The
standard surface shader selects **Burley = 0** or **Lambert = 1** from that uniform; unknown
values also select Burley. Burley calls `Fd_Burley(pixel.perceptualRoughness, NoV, NoL, LoH)`;
Lambert calls `Fd_Lambert()`. Using perceptual roughness follows Quader's shared material
contract. The selector applies to directional and punctual direct lighting without changing
their strength, specular response, shadows, or the indirect-light path. Materials without the
macro keep the exact upstream `diffuse(pixel.roughness, NoV, NoL, LoH)` call and need no new
uniform. Quader's `quader_mesh_lit.mat` opts in; scene and material-preview bindings supply the
same authored value. Material fragment code precedes the standard surface helper in generated
shaders, so the opt-in is visible when that helper is preprocessed.

Upstream's additional directional lights require `Engine::Config::enableMultipleDirectionalLights`,
which remains disabled in Quader. They use separate per-view uniforms and do not overlap the
patched distance-exponent slot. The second specular lobe and iridescence are available in the
SDK and require explicit material integration before Quader can expose authoring controls.
The upstream runtime defaults are retained, including disabled Vulkan dynamic pipeline state;
enable and benchmark that option separately before changing the app's default. Updating the
renderer snapshot does not add new material controls or change saved material properties.

The perceptual-roughness convention matches the Burley evaluation in
[Godot 4.7's direct-light shader](https://raw.githubusercontent.com/godotengine/godot/4.7-stable/servers/rendering/renderer_rd/shaders/scene_forward_lights_inc.glsl).
GPU comparisons include roughness 0.5 as well as 1.0 to distinguish this convention from
accidentally supplying squared roughness.

Build from the Quader repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_filament_sdk.ps1 -AllowDownload
```

The first run verifies the pinned source archive, extracts a dedicated versioned source directory,
applies the patch exactly, and builds with Visual Studio 2026. Every run compares cached source
inputs with the verified archive and independently patched originals; modified or additional
source inputs cause an error without overwriting them. `-PrepareOnly` stops after
verified extraction and patching; `-WorkRoot C:\path\to\dependencies` changes the parent directory;
`-Jobs 6` bounds concurrent build projects. Network access is off unless `-AllowDownload` is present.
`-InstallOnly` validates and reinstalls an existing Release /MD dependency build without compiling.
The script never deletes or mutates the original official SDK or an unversioned source checkout.

The dependency build is Release with `/MD`; both native Quader configurations consume it. This
dependency's build directory is separate from the canonical native `build/vs2026` tree. Default
locations are sibling `_SOURCES/filament-main-aab101b4c038-quader-vsync9-source`,
`_SOURCES/filament-main-aab101b4c038-quader-vsync9-build` and
`_SOURCES/filament-main-aab101b4c038-quader-vsync9-windows` for source, build and install.
The SDK contains freshly rebuilt matching headers, libraries, and `matc.exe`; mixing an official SDK with
patched headers would break the platform ABI. The installed `quader-filament-sdk.json` records
the source and patch identities and the hashes of every library used by Quader plus public
presentation headers, `LightManager.h`, and `matc.exe`. Native configure verifies those identities
and hashes. Quader's material outputs depend on this matching compiler executable. This snapshot
retains material format version 76 from v1.76.1, but changes upstream material variant generation
and shaders. Recompile every material with the rebuilt `matc.exe`, including the modified
punctual-light and diffuse shaders; an unchanged format number does not establish binary compatibility.
The install also includes BlueVK's `bluevk`, `vulkan`, and `vk_video` public header groups from
the pinned source, which upstream's normal install omits despite exposing `VulkanPlatform.h`.
These headers retain their Apache-2.0/Khronos notices and introduce no additional runtime DLL.
The rebuilt `matc.exe` uses the standard MSVC/UCRT runtime available on the required Visual Studio
build machines; the official standalone `matc.exe` bundled its C runtime statically. `matc.exe`
is a build tool, and Quader's shipped renderer libraries retain the same `/MD` runtime linkage.

Changing a shipped patch requires changing its version and the corresponding source pin, install
name, configure guardrails and render integration together. An unshipped revision may be amended
before handoff, with its patch hashes, verified source and rebuilt SDK manifest synchronized.
Validate FIFO default,
Immediate selection, explicit FIFO fallback, runtime toggling, resize, multiple viewport surfaces,
and material previews. Include repeated/interrupted resize, minimize/restore, native compositor
capture, and presentation validation for retired swapchain and semaphore lifetimes. Verify default
light appearance and authored point/spot attenuation in both scene and tool previews. Check
STRETCH selection and unsupported/shared-device fallback in both presentation modes. Verify actual
composed pixels when a dock reveals the previously clipped backing region. Compare dark
gradients with dithering enabled and disabled, encoded
UNORM and hardware-sRGB presentation, scaled output, and translucent compositing. Compile opted-in
and ordinary materials, and compare Burley/Lambert at normal and grazing angles for directional,
point, and spot lights in the scene and material preview. FPS measurements
require the same native build and scene on a real GPU.
