# Third-Party Dependencies

Quader native app dependencies must remain compatible with proprietary/non-free distribution. Keep license evidence in `thirdparty/licenses` and avoid release builds that rely on live network fetches.

| Dependency | Source | Version / commit | License | Used for |
| --- | --- | --- | --- | --- |
| OpenMesh | https://gitlab.vci.rwth-aachen.de:9000/OpenMesh/OpenMesh.git | `5f016723f9f183770cffe5787a6f9f628e2914d8` | BSD 3-Clause, see `licenses/openmesh-BSD-3-Clause.txt` | Topology-backed polygon edit operations when `QUADER_USE_OPENMESH_TOPOLOGY=ON`. |
| earcut.hpp | https://github.com/mapbox/earcut.hpp | vendored header | ISC, see `licenses/earcut-hpp-ISC.txt` | Polygon triangulation. |
| nlohmann/json | https://github.com/nlohmann/json | `v3.12.0` single header | MIT, see `licenses/nlohmann-json-MIT.txt` | Human-readable `.qproj` manifests and the per-user recent-project index. |
| FlatBuffers | https://github.com/google/flatbuffers | vcpkg `25.12.19` | Apache-2.0, see `licenses/flatbuffers-Apache-2.0.txt` | Typed binary `.qdr` chunk payloads, schema verification, and build-time private-header generation with `flatc`. |
| Google Material Icons | https://github.com/google/material-design-icons | commit `abd7f5c0e179c83f068c770650bd14ebac5d5a09` | Apache-2.0, see `licenses/google-material-icons-Apache-2.0.txt` and `google-material-icons-PROVENANCE.md` | Layer visibility and lock-state UI icon artwork. |
| Zstandard | https://github.com/facebook/zstd | vcpkg `1.5.7` | BSD-3-Clause OR GPL-2.0-only; Quader uses the BSD-3-Clause option, see `licenses/zstd-BSD-3-Clause-OR-GPL-2.0-only.txt` | Adaptive level-1 compression for sufficiently large binary `.qdr` geometry chunks. |
| EnTT | https://github.com/skypjack/entt | vcpkg `3.16.0` | MIT, see `licenses/entt-MIT.txt` | Entity-component storage for scene/runtime data. |
| OpenGL Mathematics (GLM) | https://glm.g-truc.net | vcpkg `1.0.3` | MIT or Happy Bunny License (Modified MIT), see `licenses/glm-MIT.txt` | Vector, matrix, and transform math. |
| Filament SDK and bundled IBL assets | https://github.com/google/filament | `filament-v1.71.6-windows.tgz`, SHA-256 `f56b7b2717eda5094179748687e57fa20c2f245a3203fdf1fe274dba37e21180` | Apache-2.0, see `licenses/filament-Apache-2.0.txt` | Vulkan renderer, materials, KTX IBL loading, default `lightroom_14b` viewport environment, and checksum-pinned native CI. |
| Qt Base | https://code.qt.io/cgit/qt/qtbase.git | vcpkg `6.10.3` | LGPL-3.0-only for the dynamically linked distribution path, see `licenses/qtbase-LGPL-3.0-only.txt` | Qt Core, GUI, and Widgets support for the retained-mode editor shell. |
| Qt Image Formats | https://code.qt.io/cgit/qt/qtimageformats.git | vcpkg `6.10.3` | LGPL-3.0-only for the dynamically loaded plugin distribution path, see `licenses/qtimageformats-LGPL-3.0-only.txt` | TGA, TIFF, and WebP image plugins used by project texture import; Jasper/JP2 support is deliberately disabled. |
| libjpeg-turbo | https://github.com/libjpeg-turbo/libjpeg-turbo | vcpkg `3.1.4.1` | IJG and BSD-3-Clause, see `licenses/libjpeg-turbo-BSD-3-Clause-IJG.txt` | JPEG decoding through Qt's dynamically loaded `qjpeg` image plugin. |
| LibTIFF | https://gitlab.com/libtiff/libtiff | vcpkg `4.7.1` | libtiff, see `licenses/libtiff-libtiff.txt` | Minimal TIFF decoding through Qt's dynamically loaded `qtiff` image plugin; optional JPEG, LZMA, and Deflate TIFF features remain disabled. |
| libwebp | https://chromium.googlesource.com/webm/libwebp | vcpkg `1.6.0#2` | BSD-3-Clause with an additional patent grant, see `licenses/libwebp-BSD-3-Clause-and-PATENTS.txt` | Still WebP decoding and animation detection through Qt's dynamically loaded `qwebp` image plugin. |
| GoogleTest | https://github.com/google/googletest | vcpkg `1.17.0` | BSD 3-Clause, see `licenses/gtest-BSD-3-Clause.txt` | Native C++ tests. |
| lodepng | https://github.com/lvandeve/lodepng | vcpkg `2021-12-04` | zlib license, see `licenses/lodepng-Zlib.txt` | PNG image loading support. |
| Sentry Native SDK | https://github.com/getsentry/sentry-native | vcpkg `0.14.0` | MIT, see `licenses/sentry-native-MIT.txt` | Native crash and event reporting through Sentry. |
| zlib | https://www.zlib.net | vcpkg `1.3.2` | Zlib, see `licenses/zlib-Zlib.txt` | Compression runtime dependency for the Sentry crashpad handler. |
| spdlog | https://github.com/gabime/spdlog | vcpkg `1.17.0` | MIT, see `licenses/spdlog-MIT.txt` | Runtime logging. |
| {fmt} | https://github.com/fmtlib/fmt | vcpkg `12.1.0` | MIT, see `licenses/fmt-MIT.txt` | spdlog default formatting backend. |
| stduuid | https://github.com/mariusbancila/stduuid | vcpkg `1.2.3` | MIT, see `licenses/stduuid-MIT.txt` | Stable UUID generation and parsing for scene entity identity. |
| Tracy Profiler | https://github.com/wolfpld/tracy | external checkout when `QUADER_ENABLE_TRACY=ON` | BSD 3-Clause, see `licenses/tracy-BSD-3-Clause.txt` | Optional diagnostic instrumentation for local performance captures. |

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

Filament is not vendored. Local and release builds provide a binary SDK with
`FILAMENT_SDK_ROOT`. Clean native CI downloads the official v1.71.6 Windows release archive and
verifies its SHA-256 before extraction or configure. Updating Filament requires one atomic change
to the CMake default version, this manifest row, the workflow URL/digest/cache key, license evidence
when applicable, and the configure-time SDK layout checks. An unversioned URL or a checksum bypass
is not a supported fallback.

## Tracy Source Policy

Tracy is optional and not vendored. Local diagnostic builds may enable it with:

```powershell
cmake --preset vs2026 -DQUADER_ENABLE_TRACY=ON -DQUADER_TRACY_SOURCE_DIR=C:\path\to\tracy
```

Builds with Tracy enabled must use a checkout that contains `public/tracy/Tracy.hpp` and `LICENSE`. Do not ship Tracy-enabled diagnostic packages without also including that license text.
