# Third-Party Dependencies

Quader SDK dependencies must remain compatible with proprietary/non-free engine adapters. Keep license evidence in `thirdparty/licenses` and avoid release builds that rely on live network fetches.

| Dependency | Source | Version / commit | License | Used for |
| --- | --- | --- | --- | --- |
| OpenMesh | https://gitlab.vci.rwth-aachen.de:9000/OpenMesh/OpenMesh.git | `5f016723f9f183770cffe5787a6f9f628e2914d8` | BSD 3-Clause, see `licenses/openmesh-BSD-3-Clause.txt` | Topology-backed polygon edit operations when `QUADER_SDK_USE_OPENMESH_TOPOLOGY=ON`. |
| Catch2 | https://github.com/catchorg/Catch2 | `v3.9.1` / `644821ce28cb25d7992a4d0375b1d83214392592` | Boost Software License 1.0, see `licenses/catch2-BSL-1.0.txt` | SDK C++ test harness. |
| earcut.hpp | https://github.com/mapbox/earcut.hpp | vendored header | ISC, see `licenses/earcut-hpp-ISC.txt` | Polygon triangulation. |
| nlohmann/json | https://github.com/nlohmann/json | `v3.12.0` single header | MIT, see `licenses/nlohmann-json-MIT.txt` | Portable `.qdr` JSON persistence. |
| Geometric Tools Engine Mathematics | https://github.com/davideberly/GeometricTools | `GTE-version-8.2` / `e095b84018766274f1546a7baa12c257fd18f7d8`, vendored at `geometrictools/gte-8.2-e095b84018766274f1546a7baa12c257fd18f7d8/GTE/Mathematics` | Boost Software License 1.0, see `licenses/geometrictools-BSL-1.0.txt` | Robust portable geometry and mathematics helpers for SDK geometry/editor-core work. Only GTE Mathematics headers are vendored; Graphics, Applications, GPU, and Samples are excluded. |
| Tracy Profiler | https://github.com/wolfpld/tracy | external checkout when `QUADER_ENABLE_TRACY=ON` | BSD 3-Clause, see the external checkout `LICENSE` | Optional diagnostic instrumentation for local performance captures. |

## OpenMesh Source Policy

`QUADER_SDK_USE_OPENMESH_TOPOLOGY` defaults to `ON`, but CMake no longer downloads OpenMesh unless explicitly allowed. Release and package builds should provide a local checkout with:

```powershell
cmake -S . -B build\sdk -DQUADER_SDK_OPENMESH_SOURCE_DIR=C:\path\to\OpenMesh
```

For one-off development builds only, opt in to the pinned upstream fetch:

```powershell
cmake -S . -B build\sdk -DQUADER_SDK_ALLOW_EXTERNAL_OPENMESH_FETCH=ON
```

If a local source snapshot is checked into a release workspace, place it at `thirdparty/openmesh/openmesh-5f016723f9f183770cffe5787a6f9f628e2914d8` or pass `QUADER_SDK_OPENMESH_SOURCE_DIR`.

## Catch2 Source Policy

Catch2-backed SDK tests use pinned Catch2 `v3.9.1` at commit `644821ce28cb25d7992a4d0375b1d83214392592`. Release and package builds should provide a local checkout with:

```powershell
cmake -S . -B build\sdk -DQUADER_SDK_CATCH2_SOURCE_DIR=C:\path\to\Catch2
```

For one-off development builds only, opt in to the pinned upstream fetch:

```powershell
cmake -S . -B build\sdk -DQUADER_SDK_ALLOW_EXTERNAL_CATCH2_FETCH=ON
```

If a local source snapshot is checked into a release workspace, place it at `thirdparty/catch2/catch2-644821ce28cb25d7992a4d0375b1d83214392592` or pass `QUADER_SDK_CATCH2_SOURCE_DIR`.

## Tracy Source Policy

Tracy is optional and not vendored. Local diagnostic builds may enable it with:

```powershell
cmake -S . -B build\sdk -DQUADER_ENABLE_TRACY=ON -DQUADER_TRACY_SOURCE_DIR=C:\path\to\tracy
```

Builds with Tracy enabled must use a checkout that contains `public/tracy/Tracy.hpp` and `LICENSE`. Do not ship Tracy-enabled diagnostic packages without also including that license text.
