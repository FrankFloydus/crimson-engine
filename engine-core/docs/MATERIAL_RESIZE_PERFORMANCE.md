# Resize integration measurements

Measured 2026-10-20, Windows x64 Release/MSVC, AMD Ryzen 9 7900X. Canonical renderer revision `0524a48672373dbb27a5a308ba342680f7a8c740`; Engine branch `codex/material-resize`. This is a scoped image-generation benchmark and integration check, not an FPS or GPU-time benchmark.

The benchmark starts from an already decoded 256×256 RGBA8 Data image, applies Levels Gamma 1.285 on its working lattice, and linearly resizes it. Each extent has one untimed bake and five measured samples. Mip preparation packs the complete linear RGBA16F mip chain. The table reports medians; total is the median of paired bake-plus-mip samples. File decoding, queue wait and GPU upload are excluded.

| Result | CPU image bake | CPU float mip preparation | Total CPU preparation | GPU texel storage with full mips |
| --- | ---: | ---: | ---: | ---: |
| 512×512 | 7.84 ms | 6.85 ms | 14.70 ms | 2.67 MiB |
| 1024×1024 | 14.43 ms | 27.17 ms | 41.66 ms | 10.67 MiB |
| 2048×2048 | 39.40 ms | 107.67 ms | 147.63 ms | 42.67 MiB |

These costs occur on asynchronous resource workers when content changes. They are not added to every render frame. A more expensive source branch or larger working lattice costs more. The float mip preparation is the largest measured cold cost at 2K; no claim of instant large-image regeneration is made. GPU storage excludes allocation metadata and temporary replacement overlap. CPU float output alone occupies 4/16/64 MiB at the three sizes.

Warm streaming retained one generation and one resource load across 120 requests. A separate actual AssetLibrary fixture retained its handle/signatures and unchanged load count for 120 requests, then coherently regenerated both direct and baked samplers after an input PNG edit. The prior leased generation remained valid. Non-power-of-two 1000×500 output at a 512-pixel demand retained its correct 500×250 mip after the CPU descriptor was released, without rebaking, for mipmapped and single-level textures.

The actual DX12 test streamed a rectangular RGBA16F Resize image into the native tessellation domain stage and compared its displaced silhouette against a separately uploaded RGBA32F reference. Image RMS was 0.996094 on 8-bit output, below the test limit of 2; displacement differed from the flat reference. DX11 preserved its authored ordinary-geometry fallback. This verifies upload format, row pitch and native Height sampling; it is not a GPU execution-cost measurement.

The Release application build completed, including material-cache preparation with 8/8 topologies, 36 cached stages, zero compiler processes and zero failures. Seventeen focused checks passed across graph math/typing/serialization, editor transactions/cancellation, shader compilation and preflight, publication/cache, Grunge preservation, float precision/mips, streaming, AssetLibrary source reload and DX12/DX11 rendering. Existing authored project files were excluded from the change.

Reproduce CPU measurements after building:

```powershell
.\build\Release\bin\material_resize_tests.exe --benchmark build/Release/diagnostics/material-resize/cpu-generation.json
```

The JSON contains all five samples per phase. See [Resize](MATERIAL_RESIZE.md) for scope, precision, sampling and consumer integration limits.
