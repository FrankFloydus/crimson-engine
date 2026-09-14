// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.

bool requiresWorldTextureNormalCorrection() {
    highp mat3 worldFromModel = mat3(getWorldFromModelMatrix());
    if (any(isnan(worldFromModel[0])) || any(isinf(worldFromModel[0]))
            || any(isnan(worldFromModel[1])) || any(isinf(worldFromModel[1]))
            || any(isnan(worldFromModel[2])) || any(isinf(worldFromModel[2]))) {
        return true;
    }

    // Remove scale magnitude before testing the Gram matrix so very small
    // and large uniform transforms retain the interpolated tangent frame.
    highp vec3 maximum = max(max(abs(worldFromModel[0]),
                                 abs(worldFromModel[1])),
                            abs(worldFromModel[2]));
    highp float magnitude = max(max(maximum.x, maximum.y), maximum.z);
    if (magnitude <= 0.0) {
        return true;
    }
    worldFromModel /= magnitude;
    if (determinant(worldFromModel) <= 0.0) {
        return true;
    }

    highp vec3 squaredLengths = vec3(dot(worldFromModel[0], worldFromModel[0]),
                                    dot(worldFromModel[1], worldFromModel[1]),
                                    dot(worldFromModel[2], worldFromModel[2]));
    highp float maximumSquaredLength =
            max(max(squaredLengths.x, squaredLengths.y), squaredLengths.z);
    highp float minimumSquaredLength =
            min(min(squaredLengths.x, squaredLengths.y), squaredLengths.z);
    highp float tolerance = maximumSquaredLength * 0.00001;
    highp vec3 crossTerms = abs(vec3(dot(worldFromModel[0], worldFromModel[1]),
                                    dot(worldFromModel[0], worldFromModel[2]),
                                    dot(worldFromModel[1], worldFromModel[2])));
    // Nonuniform scale, shear and reflection need UV-derived tangents.
    return maximumSquaredLength - minimumSquaredLength > tolerance
            || any(greaterThan(crossTerms, vec3(tolerance)));
}
