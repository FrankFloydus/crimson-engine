<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Light engine source audit

This records upstream source versions and exact evidence used to establish the quantities
and conversions discussed in [Quader's shared light scale](../src/scene/light_interoperability.md).
The external engine checkouts are reference material, not newly linked or vendored dependencies.
Quader's actual dependency pins and license evidence remain in
[THIRD_PARTY_MANIFEST.md](THIRD_PARTY_MANIFEST.md).

## Retained source audit: checked 2026-09-08

| Implementation inspected | Directional UI | Runtime quantity and exposure |
| --- | --- | --- |
| Unreal 5.8.0, CL 55116800 | Slider 0–150, exponent 2, label lux; component default 10; larger numeric values supported | Lux passes through. Default histogram auto exposure has +1 stop bias. Optional fixed editor exposure remembers EV100 1. |
| Blender local 5.2.0 alpha snapshot | Sun Strength, W/m²; soft range 0–10; new Sun defaults to 1 | Radiometric strength, multiplied by light exposure. View exposure and color management are separate. |
| Unity Graphics HDRP/Core 17.6.0, commit `a7e4c051d256a781ab362c64316b125a1e104694` | Directional slider covers 0–130,000 lux across Moon/Low Sun/Cloudy/High Sun intervals | Directional native intensity is lux; Point/Spot native intensity is candela. New HDRP directional default is 100,000 lux. |
| Unity URP; Built-in public C# reference 6000.7.0a6, commit `6b50e5544f6efcca1f44dbace3d1778b465ac6d0` | Scalar intensity field | Requires a pipeline-specific appearance calibration; not the HDRP physical-unit contract. Native Unity engine code is not fully public. |
| Quader's patched Filament 1.71.6, upstream commit `ab65c099438ead4fabf496706da4aa3e0a272472` | App-owned controls | Directional input is lux. Point input lumens becomes candela by division by 4π; ordinary Spot divides by π. Photographic exposure is applied separately. |

Unreal evidence: [directional UI](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Editor/DetailCustomizations/Private/DirectionalLightComponentDetails.cpp:43>),
[default intensity](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Private/Components/DirectionalLightComponent.cpp:1030>),
[brightness](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Private/Components/LightComponent.cpp:560>),
[automatic exposure defaults](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Private/SceneView.cpp:182>),
[fixed editor exposure](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Classes/Engine/EngineBaseTypes.h:1143>).

Blender evidence: [Sun and local-light units](C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/blender-main/source/blender/makesrna/intern/rna_light.cc:300),
[new Sun default](C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/blender-main/source/blender/editors/object/object_add.cc:1876),
[Cycles input](C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/blender-main/intern/cycles/blender/light.cpp:50),
[Eevee input](C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/blender-main/source/blender/draw/engines/eevee/eevee_light.cc:71).

Unity evidence: [slider ranges and mapping](https://github.com/Unity-Technologies/Graphics/blob/a7e4c051d256a781ab362c64316b125a1e104694/Packages/com.unity.render-pipelines.core/Editor/Lighting/LightIntensitySlider.cs#L22-L44),
[native units](https://github.com/Unity-Technologies/Graphics/blob/a7e4c051d256a781ab362c64316b125a1e104694/Packages/com.unity.render-pipelines.core/Runtime/Utilities/LightUnitUtils.cs#L24-L88),
[HDRP default](https://github.com/Unity-Technologies/Graphics/blob/a7e4c051d256a781ab362c64316b125a1e104694/Packages/com.unity.render-pipelines.high-definition/Runtime/Lighting/Light/HDAdditionalLightData.cs#L2894-L2917),
[URP/Built-in intensity field](https://github.com/Unity-Technologies/UnityCsReference/blob/6b50e5544f6efcca1f44dbace3d1778b465ac6d0/Editor/Mono/Inspector/LightEditor.cs#L484-L487).

Filament evidence: [unit conversion](C:/Users/Drako/Desktop/quader-windows/_SOURCES/filament-v1.71.6-quader-vsync3-source/filament/src/components/LightManager.cpp:324),
[exposure calculation](C:/Users/Drako/Desktop/quader-windows/_SOURCES/filament-v1.71.6-quader-vsync3-source/filament/src/Exposure.cpp:86),
[directional uniform](C:/Users/Drako/Desktop/quader-windows/_SOURCES/filament-v1.71.6-quader-vsync3-source/filament/src/ds/ColorPassDescriptorSet.cpp:323).

## Current Filament source: checked 2026-10-21

Quader now pins the unreleased `main` snapshot at commit
`34ff730afa710f6001e207d6ecd8f01bb5a4fd98`, with `quader-vsync10`.
Patch revision 10 rebases the maintained Vulkan presentation, color and lighting extensions
onto this source while preserving the light-unit contract.
Its [light-unit conversion](https://github.com/google/filament/blob/34ff730afa710f6001e207d6ecd8f01bb5a4fd98/filament/src/components/LightManager.cpp),
[photographic exposure](https://github.com/google/filament/blob/34ff730afa710f6001e207d6ecd8f01bb5a4fd98/filament/src/Exposure.cpp),
and [directional-light exposure multiplication](https://github.com/google/filament/blob/34ff730afa710f6001e207d6ecd8f01bb5a4fd98/filament/src/ds/ColorPassDescriptorSet.cpp)
retain the quantities above. Compared with the previous `aab101b4c038` snapshot, the
exposure and directional-uniform files are byte-identical. The light-manager changes make
the Builder's build method const and suppress unchanged shadow-option notifications; its
unit-conversion calculation is unchanged. The maintained patch preserves Quader's distance-
attenuation extension. Upstream rewrote scene/view entity caching, so this audit does not
claim the entire scene implementation is unchanged. Additional directional lights remain
opt-in and disabled in Quader.
This source check does not reattribute earlier GPU calibration measurements to the upgraded SDK;
the retained September 8 audit records the engine versions originally inspected.

## Physical conversion evidence

Conversion evidence: [Unreal local lights](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Private/Components/LocalLightComponent.cpp:146>),
[Unreal Spot](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Private/Components/SpotLightComponent.cpp:79>),
[HDRP geometry/conversion factors](https://github.com/Unity-Technologies/Graphics/blob/a7e4c051d256a781ab362c64316b125a1e104694/Packages/com.unity.render-pipelines.core/Runtime/Utilities/LightUnitUtils.cs#L99-L272),
[Blender SPEC import](C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/blender-main/scripts/addons_core/io_scene_gltf2/blender/imp/light.py:58),
[Blender 683 convention](C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/blender-main/scripts/addons_core/io_scene_gltf2/blender/com/conversion.py:10).

