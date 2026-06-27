# Third-Party Dependencies

Quader native app dependencies must remain compatible with proprietary/non-free distribution. Keep license evidence in `thirdparty/licenses` and avoid release builds that rely on live network fetches.

| Dependency | Source | Version / commit | License | Used for |
| --- | --- | --- | --- | --- |
| OpenMesh | https://gitlab.vci.rwth-aachen.de:9000/OpenMesh/OpenMesh.git | `5f016723f9f183770cffe5787a6f9f628e2914d8` | BSD 3-Clause, see `licenses/openmesh-BSD-3-Clause.txt` | Topology-backed polygon edit operations when `QUADER_USE_OPENMESH_TOPOLOGY=ON`. |
| Catch2 | https://github.com/catchorg/Catch2 | `v3.9.1` / `644821ce28cb25d7992a4d0375b1d83214392592` | Boost Software License 1.0, see `licenses/catch2-BSL-1.0.txt` | Modeling-domain C++ test harness. |
| earcut.hpp | https://github.com/mapbox/earcut.hpp | vendored header | ISC, see `licenses/earcut-hpp-ISC.txt` | Polygon triangulation. |
| nlohmann/json | https://github.com/nlohmann/json | `v3.12.0` single header | MIT, see `licenses/nlohmann-json-MIT.txt` | Portable `.qdr` JSON persistence. |
| Geometric Tools Engine Mathematics | https://github.com/davideberly/GeometricTools | `GTE-version-8.2` / `e095b84018766274f1546a7baa12c257fd18f7d8`, vendored at `thirdparty/geometrictools/gte-8.2-e095b84018766274f1546a7baa12c257fd18f7d8/GTE/Mathematics` | Boost Software License 1.0, see `licenses/geometrictools-BSL-1.0.txt` | Robust native geometry and modeling runtime helpers. Only GTE Mathematics headers are vendored; Graphics, Applications, GPU, and Samples are excluded. |
| RmlUi | https://github.com/mikke89/RmlUi | vcpkg `6.2` | MIT, see `licenses/rmlui-MIT.txt` | RmlUi-backed native UI shell and viewport presentation. |
| itlib | https://github.com/iboB/itlib | vcpkg `1.12.2` | MIT, see `licenses/itlib-MIT.txt` | RmlUi transitive dependency. |
| robin-hood-hashing | https://github.com/martinus/robin-hood-hashing | vcpkg `3.11.5` | MIT, see `licenses/robin-hood-hashing-MIT.txt` | RmlUi transitive dependency. |
| FreeType | https://freetype.org/ | vcpkg `2.14.3` | FreeType License or GPL-2.0-or-later; Quader uses the FreeType License option, see `licenses/freetype-FTL-OR-GPL-2.0-or-later.txt` | RmlUi font backend. |
| Brotli | https://github.com/google/brotli | vcpkg `1.2.0` | MIT, see `licenses/brotli-MIT.txt` | FreeType transitive compression dependency. |
| bzip2 | https://sourceware.org/bzip2/ | vcpkg `1.0.8` | bzip2 license, see `licenses/bzip2-Bzip2.txt` | FreeType transitive compression dependency. |
| libpng | http://www.libpng.org/pub/png/libpng.html | vcpkg `1.6.58` | libpng license, see `licenses/libpng-Libpng-2.0.txt` | FreeType transitive image dependency. |
| zlib | https://zlib.net/ | vcpkg `1.3.2` | zlib license, see `licenses/zlib-Zlib.txt` | libpng/FreeType transitive compression dependency. |
| lodepng | https://github.com/lvandeve/lodepng | vcpkg `2021-12-04` | zlib license, see `licenses/lodepng-Zlib.txt` | PNG image loading support. |
| Nova Mono | https://github.com/google/fonts/tree/main/ofl/novamono | Google Fonts commit `e2ff3c28d1ce433a95ee071a0861b2a75235ef9b`, font copied to `resources/fonts/NovaMono.ttf` | SIL Open Font License 1.1, see `licenses/google-fonts-nova-mono-OFL-1.1.txt` | Secondary text in compact toolbar buttons. |
| Tracy Profiler | https://github.com/wolfpld/tracy | external checkout when `QUADER_ENABLE_TRACY=ON` | BSD 3-Clause, see the external checkout `LICENSE` | Optional diagnostic instrumentation for local performance captures. |

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

## Catch2 Source Policy

Catch2-backed absorbed modeling tests use pinned Catch2 `v3.9.1` at commit `644821ce28cb25d7992a4d0375b1d83214392592`. Release and package builds should provide a local checkout with:

```powershell
cmake --preset vs2026 -DQUADER_CATCH2_SOURCE_DIR=C:\path\to\Catch2
```

For one-off development builds only, opt in to the pinned upstream fetch:

```powershell
cmake --preset vs2026 -DQUADER_ALLOW_EXTERNAL_CATCH2_FETCH=ON
```

If a local source snapshot is checked into a release workspace, place it at `thirdparty/catch2/catch2-644821ce28cb25d7992a4d0375b1d83214392592` or pass `QUADER_CATCH2_SOURCE_DIR`.

## Tracy Source Policy

Tracy is optional and not vendored. Local diagnostic builds may enable it with:

```powershell
cmake --preset vs2026 -DQUADER_ENABLE_TRACY=ON -DQUADER_TRACY_SOURCE_DIR=C:\path\to\tracy
```

Builds with Tracy enabled must use a checkout that contains `public/tracy/Tracy.hpp` and `LICENSE`. Do not ship Tracy-enabled diagnostic packages without also including that license text.
