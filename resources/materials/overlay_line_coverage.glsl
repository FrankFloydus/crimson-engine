////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

float overlayLineCoverage(vec3 distancePixels, float widthPixels, float featherPixels) {
    // The ribbon keeps square caps, extending half its authored width beyond
    // each endpoint. Include their longitudinal distance in the same coverage
    // calculation as the sides so the caps do not end at a hard raster edge.
    float halfWidth = max(widthPixels, 1.0) * 0.5;
    vec2 edgeDistance = abs(distancePixels.xy) -
            vec2(halfWidth, distancePixels.z + halfWidth);
    float signedDistance = length(max(edgeDistance, vec2(0.0))) +
            min(max(edgeDistance.x, edgeDistance.y), 0.0);
    float feather = max(featherPixels, 0.0);
    if (feather <= 0.0001) {
        return step(signedDistance, 0.0);
    }
    return saturate(0.5 - signedDistance / feather);
}
