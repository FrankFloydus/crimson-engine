# Godot AgX source provenance

The SDR AgX implementation in [scene_tone_mapper.cpp](../src/render/scene_tone_mapper.cpp)
is adapted from the user-provided Godot source at
`C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/godot-master`, inspected
2026-09-08. Its `version.py` identifies 4.7.0 beta. This snapshot has no Git metadata;
no upstream commit identity is asserted. Builds do not depend on the local checkout.

| Original source | SHA-256 |
| --- | --- |
| `servers/rendering/renderer_rd/shaders/effects/tonemap.glsl` | `266774B9F98A798F2F7F4C8388598F322723C1CFC2813975ED8A3153C6FD68A8` |
| `servers/rendering/storage/environment_storage.cpp` | `912430003210839FF6510EBE960C40679A1EB6450C687C4046ADB698B249DF03` |
| `LICENSE.txt` | `B0435E3B3E4E55238F05F4B306F30524A1B2E20147810D436EAA554FA6855C80` |

The original MIT license is retained verbatim in [godot-MIT.txt](licenses/godot-MIT.txt)
and deployed with the native runtime. The original Godot copyright notices are
also present at the adaptation. Upstream project: [Godot Engine](https://github.com/godotengine/godot).

The port retains Godot's Rec.709/AgX inset and outset matrices, allenwp toe/shoulder
curve, and SDR defaults: middle gray 0.18, contrast 1.25, white 16.29, maximum 1.
It wraps Godot's linear Rec.709 input/output in Filament's linear Rec.2020 contract.
Final sRGB encoding remains Filament ColorGrading's responsibility. This is Godot's
AgX approximation, rather than Blender's complete display transform. Subsequent
engine versions may change their defaults or algorithm.

The upstream comments attribute the curve to
[Allen Pestaluky](https://allenwp.com/blog/2025/05/29/allenwp-tonemapping-curve/)
and the original AgX LUT derivation to
[EaryChow](https://github.com/EaryChow/AgX_LUT_Gen/blob/main/AgXBasesRGB.py).
The adapted code is from the MIT-licensed Godot sources above; no Blender code is copied.

## ACES audit

Quader's retained ACES mode delegates to the pinned Filament `main` snapshot at
`d1ccd94d8cd152a7a5d8933d75d2c1d03d99d185`, with
[`ACESToneMapper`](https://raw.githubusercontent.com/google/filament/d1ccd94d8cd152a7a5d8933d75d2c1d03d99d185/filament/src/ToneMapper.cpp).
This implements an ACES 1-style fitted RRT/ODT with AP0/AP1 transforms, glow, red
modifier, desaturation and dim-surround adjustment. It is a standard real-time
approximation, not the reference-exact ACES transform or ACES 2. Filament's separate
legacy mapper adds a 1.6667 brightness scale; Quader no longer offers that mode.
Godot, Unreal and Filament need not produce identical pixels when each selects
its own ACES approximation. Lighting calibration therefore uses neutral Linear
output first and compares tone mapping independently.

The 2026-10-22 source check found this snapshot's `ToneMapper.cpp` byte-identical to the
intermediate `34ff730afa71` and earlier `aab101b4c038` snapshots (SHA-256
`1e3212b67f2954721a4336c68fef1904204873835896e25c9ba77f9030aa42cd`). The prior
2026-09-14 comparison established that implementation was unchanged from v1.76.1,
retaining the scalar ACES equations previously audited in v1.71.6. This source comparison
does not reattribute earlier GPU calibration measurements to the upgraded SDK and does not
change the Godot adaptation or its source hashes above.

CPU reference coverage: [scene_tone_mapper_tests.cpp](../tests/scene_tone_mapper_tests.cpp).
The opt-in native/Godot GPU calibration compares the complete output pipeline.
