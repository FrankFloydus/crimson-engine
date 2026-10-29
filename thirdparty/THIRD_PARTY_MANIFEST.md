# Third-Party Dependencies

Quader native app dependencies must remain compatible with proprietary/non-free distribution. Keep license evidence in `thirdparty/licenses` and avoid release builds that rely on live network fetches.

| Dependency | Source | Version / commit | License | Used for |
| --- | --- | --- | --- | --- |
| Qt Direct3D 12 allocator header | Qt 6.10.3 `qtbase/src/3rdparty/D3D12MemoryAllocator` | Exact Qt-bundled header, SHA-256 recorded in `qt-d3d12-allocator/README.md` | MIT, see `qt-d3d12-allocator/LICENSE.txt` | Completes the installed pinned QRhi Direct3D 12 private-header include closure; no second allocator implementation is compiled. |
| OpenMesh | https://gitlab.vci.rwth-aachen.de:9000/OpenMesh/OpenMesh.git | `5f016723f9f183770cffe5787a6f9f628e2914d8` | BSD 3-Clause, see `licenses/openmesh-BSD-3-Clause.txt` | Topology-backed polygon edit operations when `QUADER_USE_OPENMESH_TOPOLOGY=ON`. |
| earcut.hpp | https://github.com/mapbox/earcut.hpp | vendored header | ISC, see `licenses/earcut-hpp-ISC.txt` | Polygon triangulation. |
| nlohmann/json | https://github.com/nlohmann/json | `v3.12.0` single header and native vcpkg package | MIT, see `licenses/nlohmann-json-MIT.txt` | Human-readable `.qproj` manifests, recovery metadata, the per-user recent-project index and private canonical-renderer material IR serialization. |
| FlatBuffers | https://github.com/google/flatbuffers | vcpkg `25.12.19` | Apache-2.0, see `licenses/flatbuffers-Apache-2.0.txt` | Typed binary `.qdr` chunk payloads, schema verification, and build-time private-header generation with `flatc`. |
| Google Material Icons | https://github.com/google/material-design-icons | commit `abd7f5c0e179c83f068c770650bd14ebac5d5a09` | Apache-2.0, see `licenses/google-material-icons-Apache-2.0.txt` and `google-material-icons-PROVENANCE.md` | Layer visibility and lock-state UI icon artwork. |
| Home Video font | https://ggbot.itch.io/home-video-font | Regular v0.80, SHA-256 `E73D67F92457BD05D24C42F43156BC422E98982E9E62D267AD6D1A3CC595BB01` | CC0 1.0 Universal, see `licenses/home-video-CC0-1.0.txt` and `home-video-font-PROVENANCE.md` | Pixel-style viewport dimension labels. |
| Godot AgX | https://github.com/godotengine/godot | Local 4.7.0 beta source snapshot; exact source hashes in `godot-agx-PROVENANCE.md` | MIT, exact upstream text in `licenses/godot-MIT.txt` | SDR AgX tone mapper adapted from Godot's allenwp curve and color transforms. |
| Zstandard | https://github.com/facebook/zstd | vcpkg `1.5.7` | BSD-3-Clause OR GPL-2.0-only; Quader uses the BSD-3-Clause option, see `licenses/zstd-BSD-3-Clause-OR-GPL-2.0-only.txt` | Adaptive level-1 compression for sufficiently large binary `.qdr` geometry chunks. |
| EnTT | https://github.com/skypjack/entt | vcpkg `3.16.0` | MIT, see `licenses/entt-MIT.txt` | Entity-component storage for scene/runtime data. |
| OpenGL Mathematics (GLM) | https://glm.g-truc.net | vcpkg `1.0.3` | MIT or Happy Bunny License (Modified MIT), see `licenses/glm-MIT.txt` | Vector, matrix, and transform math. |
| Filament SDK and bundled IBL assets | https://github.com/google/filament | Unreleased `main` commit `d1ccd94d8cd152a7a5d8933d75d2c1d03d99d185` (upstream version label `1.78.1`), source SHA-256 `f7f215b087dc30c4a070a9fbb427f8e0c360058f14b068e40115d6d7b4c5d336`, Quader patch `quader-vsync10` | Apache-2.0, see `licenses/filament-Apache-2.0.txt` and `filament/README.md` | Vulkan renderer with explicit VSync, retained swapchain presentation lifetimes, recoverable surface-loss propagation, upstream native queue allocation correction, optional Windows presentation scaling, unclipped presentable images, shifted AO reconstruction, light distance attenuation, opt-in Burley/Lambert diffuse shading, and correct sRGB presentation after dithering, materials, KTX IBL loading, and default `lightroom_14b` viewport environment. |
| Crimson Renderer | https://github.com/qvertexworks/crimson-renderer | Recorded Git submodule commit at `thirdparty/crimson-renderer`, with `renderer-manifest.json` | Proprietary first-party renderer, used under the author's authorization; bundled dependency notices remain separate. | One borrowed Qt device hosts the complete modeling application and Engine scene, game and document-preview rendering. The renderer also retains its independent Quader Vulkan recipe. |
| bgfx | https://github.com/bkaradzic/bgfx.cmake | Pinned `1.129.8940-496` archive; the exact maintained overlay revision and native ABI are owned by `crimson-renderer/renderer-manifest.json` | BSD-2-Clause, see `licenses/bgfx-BSD-2-Clause.txt` | Canonical renderer device, resource, pass and native composition mechanisms. |
| meshoptimizer | https://github.com/zeux/meshoptimizer | Native vcpkg `1.1`, upstream tag `v1.1`, at the existing manifest baseline | MIT, exact installed notice in `licenses/meshoptimizer-MIT.txt` | Canonical renderer support for mesh detail preparation and content identity. |
| bx and bimg | https://github.com/bkaradzic/bgfx.cmake | Bundled in the same pinned bgfx archive | BSD-2-Clause, see `licenses/bx-BSD-2-Clause.txt` and `licenses/bimg-BSD-2-Clause.txt` | bgfx platform utilities and image support. |
| TinySTL | https://github.com/mendsley/tinystl | Bundled bx headers in the same pinned bgfx archive | BSD-2-Clause, see `licenses/tinystl-BSD-2-Clause.txt` | bgfx internal containers. |
| miniz | https://github.com/richgel999/miniz | Crimson dependency prefix, vcpkg `3.1.1` | MIT, see `licenses/miniz-MIT.txt` | bimg's compression dependency. |
| Qt Base | https://code.qt.io/cgit/qt/qtbase.git | vcpkg `6.10.3` | LGPL-3.0-only for the dynamically linked distribution path, see `licenses/qtbase-LGPL-3.0-only.txt` | Qt Core, GUI, and Widgets support for the retained-mode editor shell, including Vulkan composition support. |
| Vulkan Headers | https://github.com/KhronosGroup/Vulkan-Headers | vcpkg `1.4.341.0`, upstream tag `vulkan-sdk-1.4.341.0` | Apache-2.0 OR MIT, with the MIT-only `parse_dependency.py` exception; see `licenses/vulkan-headers-Apache-2.0-OR-MIT.txt` | Vulkan API declarations required by Qt Base and the canonical renderer's borrowed-context API. |
| Vulkan Loader | https://github.com/KhronosGroup/Vulkan-Loader | vcpkg `1.4.341.0`, upstream tag `vulkan-sdk-1.4.341.0` | Apache-2.0 with MIT-licensed JSON components; see `licenses/vulkan-loader-Apache-2.0.txt` and `licenses/vulkan-loader-MIT-notices.txt` | Vulkan loader required by Qt Base's Vulkan feature on Windows. |
| Qt Image Formats | https://code.qt.io/cgit/qt/qtimageformats.git | vcpkg `6.10.3` | LGPL-3.0-only for the dynamically loaded plugin distribution path, see `licenses/qtimageformats-LGPL-3.0-only.txt` | TGA, TIFF, and WebP image plugins used by project texture import; Jasper/JP2 support is deliberately disabled. |
| libjpeg-turbo | https://github.com/libjpeg-turbo/libjpeg-turbo | vcpkg `3.1.4.1` | IJG and BSD-3-Clause, see `licenses/libjpeg-turbo-BSD-3-Clause-IJG.txt` | JPEG decoding through Qt's dynamically loaded `qjpeg` image plugin. |
| LibTIFF | https://gitlab.com/libtiff/libtiff | vcpkg `4.7.1` | libtiff, see `licenses/libtiff-libtiff.txt` | Minimal TIFF decoding through Qt's dynamically loaded `qtiff` image plugin; optional JPEG, LZMA, and Deflate TIFF features remain disabled. |
| libwebp | https://chromium.googlesource.com/webm/libwebp | vcpkg `1.6.0#2` | BSD-3-Clause with an additional patent grant, see `licenses/libwebp-BSD-3-Clause-and-PATENTS.txt` | Still WebP decoding and animation detection through Qt's dynamically loaded `qwebp` image plugin. |
| GoogleTest | https://github.com/google/googletest | vcpkg `1.17.0` | BSD 3-Clause, see `licenses/gtest-BSD-3-Clause.txt` | Native C++ tests. |
| lodepng | https://github.com/lvandeve/lodepng | vcpkg `2021-12-04` | zlib license, see `licenses/lodepng-Zlib.txt` | PNG image loading support. |
| Sentry Native SDK | https://github.com/getsentry/sentry-native | vcpkg `0.14.0` | MIT, see `licenses/sentry-native-MIT.txt` | Optional native crash/event reporting; disabled in the canonical native preset. |
| zlib | https://www.zlib.net | vcpkg `1.3.2` | Zlib, see `licenses/zlib-Zlib.txt` | Compression runtime used by Qt dependencies and the optional Sentry crashpad handler. |
| spdlog | https://github.com/gabime/spdlog | vcpkg `1.17.0` | MIT, see `licenses/spdlog-MIT.txt` | Runtime logging. |
| {fmt} | https://github.com/fmtlib/fmt | vcpkg `12.1.0` | MIT, see `licenses/fmt-MIT.txt` | spdlog default formatting backend. |
| stduuid | https://github.com/mariusbancila/stduuid | vcpkg `1.2.3` | MIT, see `licenses/stduuid-MIT.txt` | Stable UUID generation and parsing for scene entity identity. |
| Tracy Profiler | https://github.com/wolfpld/tracy | Pinned vcpkg `0.14.1`, matching the canonical renderer's exact package requirement | BSD 3-Clause; the package copyright accompanies deployed profiler artifacts | Shared on-demand Engine CPU/GPU capture; retained Quader modeling instrumentation is enabled only in Debug. |

## Qt and renderer source policy

This Engine branch starts from the complete `quader-crimson` source and links
the Full feature profile to the canonical Crimson host. Qt owns the native
application, composition devices and textures. Historical Filament hosts remain
available as source references, gated by `QUADER_BUILD_FILAMENT_REFERENCE`.
The native preset selects the renderer's maintained bgfx overlay, with
package/source provenance checked at configure time.
The portable SDK does not consume these native feature dependencies. See
[the integration and dependency contract](crimson/README.md) for source identity,
overlay ownership, license evidence and explicit CMake configuration.

The native manifest enables `qtbase[vulkan]` at the existing vcpkg baseline
`d0c191d142b7bc048141ddfb618aa9ae5dba0233`. Its Windows dependency chain is the
empty `vulkan` detection package (`2023-12-17`), `vulkan-headers`, and
`vulkan-loader`; it does not request the full Vulkan SDK or validation layers.
Qt remains dynamically linked at version `6.10.3`.

The following source archives were verified locally against their pinned vcpkg
port SHA-512 values. Their SHA-256 identities are:

| Archive | SHA-256 |
| --- | --- |
| `KhronosGroup-Vulkan-Headers-vulkan-sdk-1.4.341.0.tar.gz` | `d73bc5036b6556b741f6985ff600ca720308c5f2850e4a43ceb498bd3de069e7` |
| `KhronosGroup-Vulkan-Loader-vulkan-sdk-1.4.341.0.tar.gz` | `fe982697c780a950641bfcf94707135c26c501352242d285fa95d087d691292e` |

The retained header license evidence contains the unchanged upstream
`LICENSE.md`, `LICENSES/Apache-2.0.txt`, and `LICENSES/MIT.txt` texts. Loader
evidence retains its unchanged `LICENSE.txt` and the complete MIT notices from
`loader/cJSON.c` and `loader/loader_json.c`; their corresponding headers carry
the same notices. Preserve these notices with any distributed loader binaries.

## OpenMesh Source Policy

`QUADER_USE_OPENMESH_TOPOLOGY` defaults to `ON`, but CMake no longer downloads OpenMesh unless explicitly allowed. Release and package builds should provide a local checkout with:

```powershell
cmake --preset vs2026 -DQUADER_OPENMESH_SOURCE_DIR=C:\path\to\OpenMesh
```

For one-off development builds only, opt in to the pinned upstream fetch:

```powershell
cmake --preset vs2026 -DQUADER_ALLOW_EXTERNAL_OPENMESH_FETCH=ON
```

If a local source snapshot is checked into a release workspace, place it at `thirdparty/openmesh/openmesh-5f016723f9f183770cffe5787a6f9f628e2914d8` or pass `QUADER_OPENMESH_SOURCE_DIR`.

## Filament Artifact Policy

Filament is not vendored. `tools/build_filament_sdk.ps1` builds the pinned upstream source with
the reviewed patch under `thirdparty/filament`, producing one matching set of headers, tools,
and Release /MD libraries. Downloads require `-AllowDownload` and must match the source digest
before extraction. The default install is the sibling `_SOURCES/filament-main-d1ccd94d8cd1-quader-vsync10-windows`;
`FILAMENT_SDK_ROOT` may select another validated install. The generated `quader-filament-sdk.json`
records upstream and patch identities plus the installed header/library hashes. Native configure
rejects missing, unpatched, or mismatched SDK artifacts. The original official SDK and unversioned
local source checkouts are never modified.

The current pin includes all upstream `main` changes through the recorded commit, including
unreleased changes. The numeric upstream version label alone is not its identity: the full SHA,
archive digest and Quader patch revision distinguish this snapshot from the v1.78.1 release.
Material format 78 replaces the prior SDK's format 76; compile all materials with the matching
rebuilt `matc.exe`. The retained Apache-2.0 license text was checked against the new source on
2026-10-22. All 94 upstream license/notice files and the 31 explicitly installed BlueVK, Vulkan
and video headers are unchanged from the previous pinned source.

Updating Filament requires one atomic change to the source pin, patch, build script, CMake default
and artifact validation, this manifest row, license evidence when applicable,
and guardrails. The patch retains the upstream Apache-2.0 license. An unversioned URL, checksum
bypass, header-only patch, or mixture of rebuilt and official libraries is unsupported.

## Tracy Source Policy

Native configuration resolves exactly `Tracy 0.14.1` through the pinned vcpkg
manifest. Engine and canonical renderer instrumentation share that package and
its on-demand capture lifecycle. Quader's portable modeling SDK does not acquire
this native dependency; its retained app instrumentation is still Debug-only.
Preserve the package copyright with distributed diagnostic artifacts.

[`tracy-0.13.1.json`](tracy-0.13.1.json) and its retained license describe the
original Quader source baseline. They do not select the active Engine package.
