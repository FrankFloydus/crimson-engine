# Filament presentation and lighting patch

Quader uses upstream Filament v1.71.6 at commit
`ab65c099438ead4fabf496706da4aa3e0a272472` with the `quader-vsync2` patch in this directory.
The archive identity is recorded in `filament-v1.71.6-quader-vsync2.json`. Filament and changes to
these upstream source files are licensed under Apache-2.0; see
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
`FILAMENT_QUADER_VSYNC_PATCH_VERSION` is `2` in the public backend header; revision 2 retains
revision 1's presentation behavior and adds the light attenuation extension below.

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
dependency's build directory is separate from the canonical native `build/vs2026` tree. The SDK
contains freshly rebuilt matching headers, libraries, and `matc.exe`; mixing an official SDK with
patched headers would break the platform ABI. The installed `quader-filament-sdk.json` records
the source and patch identities and the hashes of every library used by Quader plus public
presentation headers, `LightManager.h`, and `matc.exe`. Native configure verifies those identities
and hashes. Quader's material outputs depend on this matching compiler executable, so rebuilding
the SDK recompiles existing materials with the modified punctual-light shader.
The install also includes BlueVK's `bluevk`, `vulkan`, and `vk_video` public header groups from
the pinned source, which upstream's normal install omits despite exposing `VulkanPlatform.h`.
These headers retain their Apache-2.0/Khronos notices and introduce no additional runtime DLL.
The rebuilt `matc.exe` uses the standard MSVC/UCRT runtime available on the required Visual Studio
build machines; the official standalone `matc.exe` bundled its C runtime statically. `matc.exe`
is a build tool, and Quader's shipped renderer libraries retain the same `/MD` runtime linkage.

Changing this patch requires changing its version and the corresponding source pin, install name,
configure guardrails and render integration together. Validate FIFO default,
Immediate selection, explicit FIFO fallback, runtime toggling, resize, multiple viewport surfaces,
and material previews. Verify default light appearance and authored point/spot attenuation in both
scene and tool previews. FPS measurements require the same native build and scene on a real GPU.
