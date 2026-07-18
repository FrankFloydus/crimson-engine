<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Verification workflow

`windows-sdk.yml` protects four supported build boundaries from clean GitHub-hosted runners:

| Job | Boundary | Local equivalent |
| --- | --- | --- |
| `sdk-msvc` | Windows/MSVC SDK-only configure, build, install, external consumer, examples, and guardrails | Configure with `QUADER_BUILD_NATIVE_APP=OFF`, `QUADER_BUILD_TESTS=ON`, the `tests` vcpkg feature, and then run the complete CTest inventory. |
| `sdk-portable` | The same SDK-only graph with GCC and Clang on Ubuntu | Use Ninja with `g++` or `clang++`, the pinned vcpkg toolchain, and the same native-app-off options. |
| `sdk-sanitizers` | Clang AddressSanitizer and UndefinedBehaviorSanitizer coverage for the portable SDK consumer suite | Add `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all`, build `quader_modeling_sdk_consumer_tests`, and run that CTest entry. |
| `native-windows` | Complete native configure, app/test-owner build, parity, all registered CTest entries, and the isolated SDK install/external-consumer check | Run `cmake --preset vs2026`, build the debug preset, run the full debug CTest preset, and build `quader`. |

The workflow bootstraps vcpkg at the `builtin-baseline` recorded in `vcpkg.json`. Native CI checks
out OpenMesh at `QUADER_OPENMESH_COMMIT` and downloads the official
`filament-v1.71.6-windows.tgz` release asset. The Filament archive is accepted only when its
SHA-256 is `f56b7b2717eda5094179748687e57fa20c2f245a3203fdf1fe274dba37e21180`; the immutable
version, digest, and vcpkg manifest form the cache keys. No job reads a developer workstation path
or silently fetches an unpinned dependency.

The native hosted runner is suitable for configure, compile, and headless tests. Vulkan/window
smoke remains a local GPU gate through `tools/boot_smoke.ps1` because hosted runner display and GPU
capabilities are not part of the supported artifact contract.
