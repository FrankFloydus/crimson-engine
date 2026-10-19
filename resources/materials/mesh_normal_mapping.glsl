// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.

vec3 safeTextureNormal(highp vec3 value, vec3 fallback) {
    if (any(isnan(value)) || any(isinf(value))) {
        return fallback;
    }
    highp float magnitude = max(max(abs(value.x), abs(value.y)), abs(value.z));
    if (magnitude <= 1.0e-20) {
        return fallback;
    }
    highp vec3 scaled = value / magnitude;
    return scaled * inversesqrt(dot(scaled, scaled));
}

vec3 normalForTextureUv(vec3 normalSample, highp vec2 uv) {
    // Keep the existing interpolated tangent frame for an unchanged chart,
    // including pure texture translations. Nonlinear UV interpolation marks
    // the chart changed so this path derives tangents from the final UV.
    if (variable_textureSamplingCorrection.x <= 0.5
            && materialParams.textureTransformRotationRadians == 0.0
            && all(equal(materialParams.textureTransformScale, vec2(1.0)))) {
        return normalSample;
    }

    // The implicit vertex flip and explicit fragment flip cancel. Derive
    // the chart from the final sampled UVs, including every mapping edit.
    highp vec2 uvDx = dFdx(uv);
    highp vec2 uvDy = dFdy(uv);
    if (any(isnan(uvDx)) || any(isinf(uvDx))
            || any(isnan(uvDy)) || any(isinf(uvDy))) {
        return normalSample;
    }
    highp float uvMagnitude = max(max(abs(uvDx.x), abs(uvDx.y)),
                                 max(abs(uvDy.x), abs(uvDy.y)));
    if (uvMagnitude <= 1.0e-20) {
        return normalSample;
    }
    uvDx /= uvMagnitude;
    uvDy /= uvMagnitude;
    highp float uvDeterminant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
    if (abs(uvDeterminant) <= 0.00000001) {
        return normalSample;
    }

    highp vec3 positionDx = dFdx(getWorldPosition());
    highp vec3 positionDy = dFdy(getWorldPosition());
    highp float orientation = uvDeterminant < 0.0 ? -1.0 : 1.0;
    highp vec3 tangent =
            (positionDx * uvDy.y - positionDy * uvDx.y) * orientation;
    highp vec3 bitangent =
            (positionDy * uvDx.x - positionDx * uvDy.x) * orientation;
    highp vec3 normal = getWorldGeometricNormalVector();
    tangent = safeTextureNormal(tangent - normal * dot(normal, tangent),
                                vec3(0.0));
    bitangent = safeTextureNormal(bitangent, vec3(0.0));
    if (dot(tangent, tangent) <= 0.000001
            || dot(bitangent, bitangent) <= 0.000001) {
        return normalSample;
    }
    highp vec3 crossBitangent = cross(normal, tangent);
    bitangent = crossBitangent
            * (dot(crossBitangent, bitangent) < 0.0 ? -1.0 : 1.0);
    // Filament's packed tangent frame uses -dP/dV for normal-map +Y:
    // image rows run down while the tangent-space normal points up.
    // Keep that convention when a changed chart requires derivatives.
    // UV derivatives retain authored orientation, while Filament flips the
    // entire packed frame on a two-sided backface. Its normal is already flipped.
    float tangentFacing = isDoubleSided() && !gl_FrontFacing ? -1.0 : 1.0;
    highp vec3 mappedWorldNormal =
            (tangent * normalSample.x - bitangent * normalSample.y) * tangentFacing
            + normal * normalSample.z;

    // Filament retains an unnormalized, interpolated TBN. Invert that frame
    // instead of transposing it so prepareMaterial reproduces this direction.
    highp mat3 worldTangentFrame = getWorldTangentFrame();
    highp float frameDeterminant = determinant(worldTangentFrame);
    if (isnan(frameDeterminant) || isinf(frameDeterminant)
            || abs(frameDeterminant) <= 0.00000001) {
        return normalSample;
    }
    return safeTextureNormal(inverse(worldTangentFrame) * mappedWorldNormal,
                             normalSample);
}

vec3 sampleTextureNormal(highp vec2 uv) {
    vec3 normalSample = texture(materialParams_normalTexture, uv).xyz * 2.0 - 1.0;
    normalSample.y *= materialParams.normalYSign;
    normalSample.xy *= clamp(materialParams.normalStrength, -16.0, 16.0);
    normalSample = safeTextureNormal(normalSample, vec3(0.0, 0.0, 1.0));
    return normalForTextureUv(normalSample, uv);
}
