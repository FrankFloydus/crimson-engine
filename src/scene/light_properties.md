<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Authored light properties

`LightComponent` remains the scene's only mutable light state. The Qt/Filament-independent
[helpers](light_properties.hpp) derive engine output, temperature RGB, and source presets.
Public API DTOs preserve authored fields through equality, scene revisions, undo/redo,
duplication, and QDR persistence. [Engine mapping and source audit](light_interoperability.md)
records the current calibration and limits of appearance matching across renderers.

## Intensity and compatibility

Every Shape uses the same **Intensity** numeric input, accepting nonnegative values without
an artificial upper cap, up to finite float storage limits. There is no intensity slider.
**16** remains the new-light default and reference intensity. This is an artist control,
not a physical unit selector. For example, entering **150000** retains that value and
resolves to preview energy **9375**.
`light_engine_values()` resolves preview energy `G = intensity / 16`. Physical backends use
`G * pi / exposure`, as directional lux or Point/Spot candela. The fixed preview exposure is
`1 / 38400`, using f/16, a 1/125-second shutter, and ISO 100. Stored camera fields remain
serializable but do not alter this output. New scene tone mapping is Linear; AgX is an
additional option with serialized value 5, preserving existing tone-mapper IDs.

The same mapping applies to existing lights. Their valid stored intensity numbers are
preserved; their former rendered brightness is intentionally not preserved. The Inspector,
API, and persistence retain these numbers without rescaling them. Legacy Light Unit metadata
remains readable and writable for
compatibility, is hidden in the Inspector, and does not affect engine output.

Falloff edits remain clamped to 0-2. New lights use Falloff 1; absent falloff in legacy QDR
remains 2. Existing falloff values outside the UI range survive load and save. Historical
stored RGB is never gamma-converted on load. The light picker uses the existing sRGB transfer
functions in `core/color_space.hpp` only at its display boundary. Loading a scene never
reapplies the current preset catalog.

## Temperature and source defaults

The 1500-15000 K slider follows [Krystek's 1985 rational approximation of the Planckian locus
in CIE 1960 UCS](https://doi.org/10.1002/col.5080100109). The helpers transform chromaticity to
XYZ with Y=1, then D65 linear sRGB, normalize to the maximum channel, and clip out-of-gamut
channels. This matches the [Filament correlated-color-temperature contract](https://raw.githubusercontent.com/google/filament/v1.71.6/filament/include/filament/Color.h)
without introducing Filament into the scene owner. It describes RGB chromaticity, not spectral
power distributions or wavelength-dependent color rendering.

The immutable catalog in [light_properties.cpp](light_properties.cpp) retains each source's
nominal Kelvin, source unit, and output as reference data. These ratings select a useful
starting intensity through `clamp(16 + 4 * log2(output / reference), 1, 32)`, where the reference
is 800 lumens for lamps and 120000 lux for outdoor sources. Doubling a rating adds four
intensity units before clamping the preset default. An 800-lumen lamp and Daylight both select
16; Candle selects 1. The 1-32 interval bounds preset starting values only; manual intensity
entry has no such cap. This compression does not claim that the resulting light emits the
reference lamp's flux.

Every preset applies the same intensity and temperature-derived RGB to every Shape. It also
sets Color Temperature mode, remembered Kelvin, and preset identity. Shape, legacy unit
metadata, range, falloff, and shadows stay unchanged. Choosing Custom only clears the identity.
Manual color, temperature, mode, and intensity edits clear the identity in the editing owner.

### Lamp source references

These are representative lamp ratings. Electrical wattage in a name identifies the reference
lamp; it is not a conversion from watts to light output. Manufacturer flux is the rated
output under its specified conditions. Other products can differ. Source audit: 2026-09-08.

| Preset | Temperature (K) | Reference output (lm) | Reference |
| --- | ---: | ---: | --- |
| Candle | 1900 | 12.566 | Isotropic 1-candela approximation using [NIST's candela explanation](https://www.nist.gov/si-redefinition/candela); nominal temperature within [Filament's candle range](https://google.github.io/filament/main/filament.html#lighting/directlighting/lightsparameterization/colortemperature). |
| Incandescent 40 / 60 / 75 / 100 W | 2700 | 450 / 800 / 1100 / 1600 | [DOE reference bulb outputs and nominal incandescent CCT](https://www.energy.gov/cmei/femp/purchasing-energy-efficient-light-bulbs). |
| Halogen 80 W | 2900 | 1415 | [Philips Plusline 78 mm 80 W R7s](https://www.assets.lighting.philips.com/is/content/PhilipsLighting/comf2239-pss-fr_fr). |
| Decorative LED | 1800 | 470 | [Philips amber A60 E27, 8720169191273](https://www.lighting.philips.de/consumer/p/led-filament-lampe-bernstein-40w-a60-e27/8720169191273). |
| Warm LED / Daylight LED | 2700 / 5000 | 800 / 800 | [Philips A19, 929003004803 / 929003005003](https://www.assets.signify.com/is/content/PhilipsLighting/comf2307-pss-global). |
| Warm White LED / Cool Daylight LED | 3000 / 6500 | 800 / 800 | [Philips A19, 929002312323 / 929002312423](https://www.assets.signify.com/is/content/PhilipsLighting/comf6979538-pss-global). |
| Neutral LED | 4000 | 1055 | [Philips A60 E27, 8720169324213](https://www.lighting.philips.it/consumer/p/led-lampadina-75-w-a60-e27/8720169324213). |
| Warm / Neutral / Daylight Fluorescent 36 W | 3000 / 4000 / 6500 | 3350 / 3350 / 3200 | [Philips MASTER TL-D Super 80 36W/830, /840, /865](https://www.assets.lighting.philips.com/is/content/PhilipsLighting/comf2534-pss-global). |
| High-pressure Sodium 100 W | 2000 | 10600 | [Philips MASTER SON-T PIA PLUS 100W/220 E40](https://www.assets.lighting.philips.com/is/content/PhilipsLighting/comf1612-pss-en_ae). |
| Mercury Vapor 80 W | 4200 | 3600 | [Philips HPL-N 80W E27 SG](https://www.assets.signify.com/is/content/PhilipsConsumer/PDFDownloads/Peru/Lamparas/ODLI20151030_001-UPD-es_PE-928051007395_eu_pss_aenaa.pdf), rated EM flux at 25 C. |
| Metal Halide 400 W | 4500 | 32000 | [Philips MASTER HPI-T Plus 400W/645 E40](https://www.assets.signify.com/is/content/PhilipsConsumer/PDFDownloads/Peru/Lamparas/ODLI20151030_001-UPD-es_PE-928481600095_eu_pss_aenaa.pdf), horizontal operation, EM lamp flux. |
| Studio Tungsten 1000 W | 3200 | 26000 | [OSRAM 64745 1000 W 230 V](https://www.osram.com/appsj/pdc/pdf.do?cid=GPS01_265578&lid=EN&mpid=ZMP_56001&vid=MP_EUROPE_Europe_eCat), nominal lamp output and color temperature. |

Candle output is a geometric approximation, not a measurement of a particular flame.
Daylight Fluorescent uses 3200 lm from the cited Super 80 catalog; earlier 3250-lm examples
refer to other products or catalog versions. CCT-derived RGB cannot reproduce a discharge
lamp's spectrum or color rendering index, especially for sodium and mercury sources.

### Outdoor source references

Temperatures are nominal starting points from [Filament's CCT reference](https://google.github.io/filament/main/filament.html#lighting/directlighting/lightsparameterization/colortemperature).
Illuminance sources and the interpretation of each selected rating are listed separately.

| Preset | Temperature (K) | Reference illuminance (lux) | Reference and interpretation |
| --- | ---: | ---: | --- |
| Sunrise/Sunset | 2500 | 400 | [CDC NHANES ambient-light comparison](https://wwwn.cdc.gov/Nchs/Data/Nhanes/Public/2013/DataFiles/PAXLUX_H.htm), approximate sunrise/sunset level. |
| Midday Sun | 5200 | 110000 | [Filament's midday directional-light example](https://google.github.io/filament/main/filament.html#lighting/directlighting/directionallights). |
| Daylight (Sun + Sky) | 6000 | 120000 | [Filament's daylight sun-and-sky reference](https://google.github.io/filament/main/filament.html#lighting/units). |
| Overcast (Bright) | 7000 | 10000 | [LBNL Radiance tutorial](https://radsite.lbl.gov/radiance/refer/rc97tut.pdf), CIE overcast benchmark with 10000-lux horizontal illuminance. |
| Outdoor Shade | 7500 | 20000 | [Filament's measured sky-only illuminance](https://google.github.io/filament/main/filament.html#lighting/directlighting/directionallights), used as a shade starting point. |
| Blue Sky | 9000 | 25000 | [Filament's measured noon sky-only illuminance](https://google.github.io/filament/main/filament.html#lighting/directlighting/directionallights). |

Weather labels do not guarantee universal illuminance: location, time, cloud cover, and
obstruction matter. Their source ratings now choose relative artist defaults on the common
scale. The presets change one light; they do not construct a distributed sky model. Use an
environment light when the scene needs illumination from the whole sky.

Protect the model with `LightPropertiesTests`, persistence with `QdrBinarySceneCodecTests`,
and interactive picker behavior with the public scene-preview and Inspector tests.
