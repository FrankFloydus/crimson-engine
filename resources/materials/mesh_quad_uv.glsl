////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

// Renderer-derived inverse bilinear coordinates for one convex planar quad.
// Keep the numerical counterpart in geometry/quad_uv_interpolation.cpp aligned.
// P = p0 + e*s + f*t + (e*hx + f*hy)*s*t. With delta=s*t-wl, we have
// s=sl-hx*delta and t=tl-hy*delta. The correction R*delta converts triangle UV
// interpolation to four-corner bilinear interpolation, and is zero on every
// boundary because sl*tl=wl there. Affine placement corrections have R=0.
highp vec2 applyQuadUvInterpolation(highp vec2 uv) {
    if (variable_textureSamplingCorrection.y > 0.5) {
        highp vec4 coordinate = variable_quadUvCoordinates;
        highp float scale = max(abs(coordinate.x), abs(coordinate.y));
        if (isnan(scale) || isinf(scale) || scale <= 0.0) {
            return uv;
        }
        highp float radius = scale * length(coordinate.xy / scale);
        highp vec2 delta = vec2(radius - coordinate.z,
                atan(coordinate.y, coordinate.x) - coordinate.w);
        highp vec2 corrected = uv + vec2(
                dot(variable_quadUvCorrection.xy, delta),
                dot(variable_quadUvCorrection.zw, delta));
        return any(isnan(corrected)) || any(isinf(corrected)) ? uv : corrected;
    }
    if (variable_quadUvCoordinates.w <= 0.5) {
        return uv;
    }
    highp vec3 coordinate = variable_quadUvCoordinates.xyz;
    highp vec2 h = variable_quadUvCorrection.xy;
    highp float residual = coordinate.x * coordinate.y - coordinate.z;
    highp float linear = 1.0 + h.y * coordinate.x + h.x * coordinate.y;
    highp float discriminant = linear * linear - 4.0 * h.x * h.y * residual;
    if (isnan(discriminant) || isinf(discriminant) || discriminant <= 0.0) {
        return uv;
    }
    highp float denominator = linear + sqrt(discriminant);
    if (isnan(denominator) || isinf(denominator) || denominator <= 0.0) {
        return uv;
    }
    highp float weightDelta = 2.0 * residual / denominator;
    highp vec2 corrected = uv + variable_quadUvCorrection.zw * weightDelta;
    return any(isnan(corrected)) || any(isinf(corrected)) ? uv : corrected;
}
