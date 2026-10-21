# SSR and PBR implementation review

Reviewed 2026-10-21 against the canonical shared renderer, the local Unreal 5.8
raster SSR sources, research papers and maintained production references.

The PBR quality slice now adds practical GGX energy compensation, opt-in specular
antialiasing and shared environment/SSR reflection visibility, including glass.
The current-frame SSR tracer retains its corrected rough-hit visibility path.
These changes do not establish state-of-the-art reflection reconstruction: the
later history, receiver-guide and sampling work remains proposed. Temporal SSR
and planar reflections remain deferred.

## Rough mirror regression and correction

The reported mirror uses Lit Alpha, coverage 1, Metallic 1 and Roughness .116.
The saved Environment enables Medium SSR with Strength 1 and Max Roughness .8.
The maximum-roughness fade is therefore 1 for this material. The shader switches
from one mirror direction to visible-GGX rough directions at roughness .1.

The former rough branch incorrectly required its filtered color footprint to fit
one reconstructed donor plane. That eliminated valid stair treads/risers and
could eliminate continuous ground: conservative odd-size Hi-Z reductions include
overlapping source texels beyond the geometric mip-cell corners used by the
plane predicate. Missing neighbors also invalidated rough hits, while an entire
footprint crossing the viewport boundary was discarded before its edge fade.
These additional gates explain the sudden reduction in valid reflected color.

The correction in
[ssr_trace.sh](../third_party/crimson-renderer/src/shaders/ssr_trace.sh) retains
FP32 linear depth, perspective-correct interpolation, directional cell ownership,
one-sided leaf intervals, actual-leaf validation and geometric self-hit rejection.
Each validated GGX hit now samples ordinary radiance/coverage mips. Donor
derivatives select the closest-depth usable neighbor and only estimate the
isotropic footprint; missing derivatives preserve the hit. The scalar footprint
uses a bounded fourfold grazing stretch, a neutral incidence estimate when no
plane is available, and continuous footprint-aware screen-edge confidence.
This is approximate filtered importance sampling, not exact integration over
the geometry inside a cone.

The closed hard-normal cube fixture uses the saved position and material
properties, tiled ground, four colored opaque steps and two downward views.
Recoloring only the steps measures their reflected color inside a masked cube
interior, excluding directly visible donor pixels. The linear contrast retained
relative to roughness .09 is:

| View | Roughness | Former source | Corrected source |
| --- | ---: | ---: | ---: |
| 0 | .10 | .287 | .909 |
| 0 | .116 | .187 | .852 |
| 0 | .15 | .050 | .746 |
| 1 | .10 | .293 | .882 |
| 1 | .116 | .163 | .821 |
| 1 | .15 | .025 | .690 |

The former captures fail all six localized retention bounds; corrected DX12 and
DX11 captures pass. These are controlled-fixture measurements, not a numerical
match to the user's unsaved camera. The fixture is part of
[ssr_render_tests.cpp](../tests/ssr_render_tests.cpp).

## What the current PBR implementation gets right

[model_lighting.sh](../third_party/crimson-renderer/src/shaders/model_lighting.sh),
[fs_pbr.sc](../third_party/crimson-renderer/src/shaders/fs_pbr.sc) and
[Environment.cpp](../third_party/crimson-renderer/src/forward/Environment.cpp)
share perceptual roughness, `alpha = roughness^2`, GGX distribution and
height-correlated Smith visibility. The preview sphere now includes the common
lighting implementation instead of maintaining duplicate BRDF functions. The
CPU BRDF LUT still integrates the single-scattering response; the shader applies
compensation using its existing scale/bias encoding. The specular prefilter's roughness-to-mip mapping
agrees with runtime sampling; diffuse preprocessing includes division by pi once.
No basic double-pi, squared-roughness or LUT-convention mismatch was found.

Ordinary dielectric F0 is `.08 * Specular`, producing .04 at the default .5.
Active glass instead uses `((IOR-1)/(IOR+1))^2`, also .04 at IOR 1.5, with an
explicit index-matched limit. Shared lighting, reflection replacement and glass
must keep those conventions coherent. The existing
[environment checks](../tests/environment_tests.cpp) compare the LUT with a
separate numerical integration and check finite extreme inputs.

## PBR quality changes and remaining limits

### Specular energy preservation

The original model included one microfacet scattering event. Before compensation,
a white conductor with F0 = 1 under a constant white environment loses energy as roughness increases:

| Roughness | Reflected energy at normal view |
| ---: | ---: |
| .25 | .995688 |
| .50 | .915812 |
| .75 | .626886 |
| 1.00 | .306853 |

These values come from numerical integration of the original direct formula.
The fully rough result is `1-ln(2)` and remains asserted by the single-scattering
LUT test. The current shader computes `E = clamp(lut.x + lut.y, epsilon, 1)` and
the practical compensation `W = 1 + clamp(F0, 0, 1) * (1/E - 1)`. Direct specular
and integrated DFG both use W. A precomputed directional-albedo diffuse budget
uses the complement of compensated DFG, shared by direct lighting, IBL and GI.
This is a practical energy-preserving approximation, not a full reciprocal
multiple-scattering BSDF. The LUT and IBL preprocessing representation stay
unchanged. Another engine's differently packed DFG formula cannot be copied by
channel name. See [Heitz et al., 2016](https://jo.dreggn.org/home/2016_microfacets.pdf)
and [Filament's energy-loss discussion](https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/energylossinspecularreflectance).

Glass uses that same remaining budget for transmission before Beer attenuation.
Transmission, surface fog correction and SSR share the faced optical normal,
effective roughness and vertex-modulated material inputs. IOR one retains exact
zero dielectric interface reflection; a metallic component keeps its reflection.

### Shading antialiasing and visibility

**Environment -> Specular AA** is optional and defaults off. It applies a bounded
screen-derivative normal-variance adjustment in squared GGX-alpha space, using
fixed variance .15 and kernel threshold .2. Base lighting, IBL, SSR and glass
transmission share the same raw normal-variance filtering. GGX/DFG/SSR retain
their .045 numerical floor; the transmission cone uses the unfloored filtered
roughness so authored zero stays clear with AA off or constant normals. The
disabled branch retains the ordinary roughness calculation. This filters projected normal variation without adding texture
channels or graph inputs. It is not a full normal-distribution mip representation
and cannot recover every unresolved texture feature. SMAA remains separate.
[Improved Geometric Specular Antialiasing](https://yusuketokuyoshi.com/papers/2019/ImprovedGeometricSpecularAA.pdf)
and [LEAN Mapping](https://userpages.cs.umbc.edu/olano/papers/lean/) are relevant
references with different implementation costs.

Environment reflection and SSR replacement now use identical roughness/NoV-aware
specular AO and soft geometric-horizon attenuation. The geometric normal comes
from the actual triangle, faced toward the view; a mapped reflection below that
horizon fades by `clamp(1 + dot(R, Ng), 0, 1)^2`. This reduces below-surface light
leaks but is approximate lobe visibility, not exact hemisphere clipping. SSR's
individual rays keep their strict geometric-hemisphere rejection. Diffuse AO
remains independent of direct light. Visibility does not redirect occluded
reflection energy into glass transmission. See
[Filament's occlusion treatment](https://google.github.io/filament/Filament.md.html#lighting/occlusion).

### Rough reflection estimation

SSR currently averages visible-GGX incoming radiance with equal `1/N` weights
and multiplies by integrated DFG afterward. This is a split-sum-style speed
approximation. For our height-correlated Smith model, the exact VNDF per-sample
weight is `F(VoH) * G2(V,L) / G1(V)`; the simpler `F * G1(L)` expression requires
separable Smith. Any change must update both reflected radiance and matching
environment replacement weights, rather than changing only one side.

The fixed denominator also retains rejected below-normal directions while the
LUT already integrates upper-hemisphere reflection. At normal view their
accepted probability is `1/(1+roughness^4)`. This is negligible at .116 and cannot
explain the reported dropout, but matters at high roughness. Our IBL prefilter
uses `V=N`, so it retains the standard split-sum approximation and cannot fully
reproduce grazing-angle lobe elongation. These limitations are distinct from
the corrected visibility bug.

### Reconstruction and sampling

Current SSR traces deterministic directions at full receiver resolution and has
no reflection history, spatial ray reuse, variance-guided reconstruction or
adaptive ray allocation. The 1/4/12 ray settings alone do not establish quality
equivalence to a production stochastic SSR system. The current isotropic mip
footprint also remains an approximation to projected anisotropic glossy lobes.

## References and their practical role

| Primary source | Use in Crimson |
| --- | --- |
| [McGuire and Mara, Efficient GPU Screen-Space Ray Tracing, JCGT 2014](https://jcgt.org/published/0003/04/04/) | University research and reference implementation for perspective-correct ray intervals, clipping, depth layers and thin geometry. Compare our hierarchical results with a simple reference tracer. |
| [Stachowiak / Frostbite, Stochastic Screen-Space Reflections, SIGGRAPH 2015](https://advances.realtimerendering.com/s2015/), [author's article](https://h3.gd/stochastic-ssr/) | Importance sampling, filtered samples, spatial reuse, adaptive sampling and temporal reconstruction as a complete reflection system. |
| [Heitz, Sampling the GGX Distribution of Visible Normals, JCGT 2018](https://jcgt.org/published/0007/04/01/) | Mathematical basis for our rough directions and estimator analysis; sampling correctness does not validate downstream visibility or filtering. |
| [AMD FidelityFX SSSR 1.5](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/), [Reflection Denoiser 1.3](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/denoiser/) | Maintained production reference for hierarchical tracing, receiver guides, guided work allocation and separate spatiotemporal reconstruction. |
| [NVIDIA NRD integration guide](https://github.com/NVIDIA-RTX/NRD) | Specular radiance, hit-distance, motion, normal and roughness contracts; history validation, demodulation and filtering. |
| [Moving Frostbite to Physically Based Rendering](https://seblagarde.wordpress.com/2015/07/14/siggraph-2014-moving-frostbite-to-physically-based-rendering/), [Physically Based Rendering in Filament](https://google.github.io/filament/Filament.md.html) | Production PBR conventions, energy preservation, shading filtering and documented approximation limits. |

The academic references remain useful correctness foundations despite their age;
the maintained AMD/NVIDIA implementations supply a current production comparison.
Newer FSR SDK product versions are not SSSR technique version numbers. The
published FidelityFX shader/backend requirements also differ from Crimson's
SM5/DX11 family, so its SDK is not a drop-in replacement for the existing pass.

## Recommended implementation sequence

1. Preserve the implemented rough-hit correction and closed-cube regression,
   including no-donor floor, silhouette, glass, fog and resource checks.
2. Validate the implemented PBR quality changes with white-furnace and roughness
   sweeps, grazing normals, known colored reflection donors, optional AA,
   complementary glass energy and matching SSR replacement. The actual saved
   Main scene is captured before/after from ShadowCamera using one immutable
   project snapshot, including an additional after capture with AA enabled.
3. Keep energy preservation and effective roughness coherent across every
   lighting provider. Further material-model or normal-distribution filtering
   improvements need their own measured slice.
4. Publish positive raw reflection radiance, hit distance and confidence before
   final composition, with exact receiver normal/roughness/depth/motion guides.
   Reconstruct that signal first, then apply material response, fog and signed
   environment replacement. NRD's positive-RGB contract rules out denoising our
   signed correction directly.
5. Add independent per-view specular histories with disocclusion rejection,
   reflection-parallax reprojection, history clipping and variance-guided spatial
   filtering. Dedicated reflection history does not require global TAA, but it
   does require an explicit history validity/reset contract.
6. Add controlled frame-varying sampling, spatial reuse and adaptive ray budgets
   after reconstruction is reliable. Retain a deterministic reference path for
   comparison, rather than increasing a fixed ray count everywhere.

Glass requires an explicit receiver-layer policy throughout steps 4-6. Its
normal, roughness, depth and motion differ from the opaque surface behind it.
Do not silently apply opaque history to glass or remove glass support during
the upgrade. Reconstructing the nearest eligible transparent layer separately,
with a current-frame path for additional layers, is one bounded design to
evaluate. General transparency is not automatically solved by adopting NRD.

The quality target is production-grade spatiotemporal SSR with a measured PBR
response. Offscreen/hidden surfaces, arbitrary transparent donors, nested
reflection layers and multiple reflection bounces remain outside single-depth
screen-space visibility. Planar reflections remain a later provider.
