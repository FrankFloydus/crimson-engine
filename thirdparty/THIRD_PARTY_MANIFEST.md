# Third-Party Dependencies

Quader native app dependencies must remain compatible with proprietary/non-free distribution. Keep license evidence in `thirdparty/licenses` and avoid release builds that rely on live network fetches.

| Dependency | Source | Version / commit | License | Used for |
| --- | --- | --- | --- | --- |
| OpenMesh | https://gitlab.vci.rwth-aachen.de:9000/OpenMesh/OpenMesh.git | `5f016723f9f183770cffe5787a6f9f628e2914d8` | BSD 3-Clause, see `licenses/openmesh-BSD-3-Clause.txt` | Topology-backed polygon edit operations when `QUADER_USE_OPENMESH_TOPOLOGY=ON`. |
| earcut.hpp | https://github.com/mapbox/earcut.hpp | vendored header | ISC, see `licenses/earcut-hpp-ISC.txt` | Polygon triangulation. |
| nlohmann/json | https://github.com/nlohmann/json | `v3.12.0` single header | MIT, see `licenses/nlohmann-json-MIT.txt` | Portable `.qdr` JSON persistence. |
| Geometric Tools Engine Mathematics | https://github.com/davideberly/GeometricTools | `GTE-version-8.2` / `e095b84018766274f1546a7baa12c257fd18f7d8`, vendored at `thirdparty/geometrictools/gte-8.2-e095b84018766274f1546a7baa12c257fd18f7d8/GTE/Mathematics` | Boost Software License 1.0, see `licenses/geometrictools-BSL-1.0.txt` | Robust native geometry and modeling runtime helpers. Only GTE Mathematics headers are vendored; Graphics, Applications, GPU, and Samples are excluded. |
| EnTT | https://github.com/skypjack/entt | vcpkg `3.16.0` | MIT, see `licenses/entt-MIT.txt` | Entity-component storage for scene/runtime data. |
| OpenGL Mathematics (GLM) | https://glm.g-truc.net | vcpkg `1.0.3` | MIT or Happy Bunny License (Modified MIT), see `licenses/glm-MIT.txt` | Vector, matrix, and transform math. |
| Filament SDK and bundled IBL assets | https://github.com/google/filament | `v1.71.6` local SDK | Apache-2.0, see `licenses/filament-Apache-2.0.txt` | Vulkan renderer, materials, KTX IBL loading, and default `lightroom_14b` viewport environment. |
| SDL3 | https://www.libsdl.org | vcpkg `3.4.8` | Zlib AND MIT AND Apache-2.0, see `licenses/sdl3-Zlib-MIT-Apache-2.0.txt` | Native windowing, events, and platform input. |
| Dear ImGui | https://github.com/ocornut/imgui | vcpkg `1.92.8` | MIT, see `licenses/imgui-MIT.txt` | Immediate-mode editor shell, dockspace, panels, and SDL3 input backend. |
| Phosphor Icons | https://github.com/phosphor-icons/web | `Phosphor.ttf` runtime font asset | MIT, see `licenses/phosphor-icons-MIT.txt` | ImGui icon glyphs for editor controls. |
| GoogleTest | https://github.com/google/googletest | vcpkg `1.17.0` | BSD 3-Clause, see `licenses/gtest-BSD-3-Clause.txt` | Native C++ tests. |
| lodepng | https://github.com/lvandeve/lodepng | vcpkg `2021-12-04` | zlib license, see `licenses/lodepng-Zlib.txt` | PNG image loading support. |
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

## Tracy Source Policy

Tracy is optional and not vendored. Local diagnostic builds may enable it with:

```powershell
cmake --preset vs2026 -DQUADER_ENABLE_TRACY=ON -DQUADER_TRACY_SOURCE_DIR=C:\path\to\tracy
```

Builds with Tracy enabled must use a checkout that contains `public/tracy/Tracy.hpp` and `LICENSE`. Do not ship Tracy-enabled diagnostic packages without also including that license text.
