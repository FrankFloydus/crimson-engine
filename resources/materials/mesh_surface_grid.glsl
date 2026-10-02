////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

float surfaceGridFloor(float spacing) {
    return exp2(floor(log2(max(spacing, 0.0001))));
}

float surfaceGridLineCoverage(
        float coord, float spacing, float thicknessValue,
        float coordinatePixelWidth, float worldPixelWidth) {
    float safeSpacing = max(spacing, 0.0001);
    float wrapped = abs(fract(coord / safeSpacing + 0.5) - 0.5) * safeSpacing;
    float thickness = clamp(thicknessValue, 0.05, 8.0);
    float derivativeWidth = coordinatePixelWidth;
    // A constant coordinate cannot form a surface line. In smooth projection
    // blends, treating its grid multiple as covered would tint the whole face.
    // Ignore perspective interpolation noise relative to the surface footprint.
    if (derivativeWidth <= worldPixelWidth * 0.001) {
        return 0.0;
    }
    float antialias = clamp(derivativeWidth, safeSpacing * 0.0005, safeSpacing * 0.125);
    float lineWidth = min(antialias * 0.75 * thickness, safeSpacing * 0.125);
    float feather = min(antialias, safeSpacing * 0.125);
    return 1.0 - smoothstep(lineWidth, lineWidth + feather, wrapped);
}

float surfaceGridLine(
        float coord, float spacing, float thicknessValue,
        float worldPixelWidth, float coordinatePixelWidth) {
    float safeSpacing = max(spacing, 0.0001);
    float minRenderableSpacing = max(worldPixelWidth, 0.0) * 4.0;
    float lodSpacing = max(surfaceGridFloor(max(minRenderableSpacing, safeSpacing)), safeSpacing);
    float nextSpacing = max(lodSpacing * 2.0, lodSpacing + 0.0001);
    float blend = smoothstep(lodSpacing, nextSpacing, minRenderableSpacing);
    float currentCoverage = surfaceGridLineCoverage(
            coord, lodSpacing, thicknessValue, coordinatePixelWidth, worldPixelWidth);
    float nextCoverage = surfaceGridLineCoverage(
            coord, nextSpacing, thicknessValue, coordinatePixelWidth, worldPixelWidth);
    return mix(currentCoverage, nextCoverage, blend);
}

float surfaceGridLineSlotCoverage(
        float coord, float spacing, float thicknessValue,
        float worldPixelWidth, float coordinatePixelWidth) {
    float safeSpacing = max(spacing, 0.0001);
    float minRenderableSpacing = max(worldPixelWidth, 0.0) * 4.0;
    float lodSpacing = max(surfaceGridFloor(max(minRenderableSpacing, safeSpacing)), safeSpacing);
    float nextSpacing = max(lodSpacing * 2.0, lodSpacing + 0.0001);
    float currentCoverage = surfaceGridLineCoverage(
            coord, lodSpacing, thicknessValue, coordinatePixelWidth, worldPixelWidth);
    float nextCoverage = surfaceGridLineCoverage(
            coord, nextSpacing, thicknessValue, coordinatePixelWidth, worldPixelWidth);
    return max(currentCoverage, nextCoverage);
}

vec3 surfaceGridProjectionWeights(vec3 normal) {
    // Blend line coverage instead of coordinates so curved smooth meshes
    // transition between world projection planes without seams or stubs.
    vec3 weights = abs(normal);
    weights *= weights;
    weights *= weights;
    float weightSum = weights.x + weights.y + weights.z;
    if (weightSum <= 0.000001) {
        return vec3(0.0, 0.0, 1.0);
    }
    return weights / weightSum;
}

vec3 surfaceGridFaceProjectionWeights(vec3 normal) {
    // TrenchBroom is Z-up. After conversion to Quader's Y-up coordinates,
    // strict dominant-axis ties resolve Y, then Z, then X.
    vec3 dominant = abs(normal);
    if (dominant.x > dominant.y && dominant.x > dominant.z) {
        return vec3(1.0, 0.0, 0.0);
    }
    if (dominant.z > dominant.y) {
        return vec3(0.0, 0.0, 1.0);
    }
    return vec3(0.0, 1.0, 0.0);
}

vec3 surfaceGridPlaneLineCoverages(
        highp vec3 worldPosition,
        float spacing,
        float thicknessValue,
        float worldPixelWidth,
        vec3 coordinatePixelWidths) {
    vec3 axisCoverages = vec3(
            surfaceGridLine(worldPosition.x, spacing, thicknessValue,
                           worldPixelWidth, coordinatePixelWidths.x),
            surfaceGridLine(worldPosition.y, spacing, thicknessValue,
                           worldPixelWidth, coordinatePixelWidths.y),
            surfaceGridLine(worldPosition.z, spacing, thicknessValue,
                           worldPixelWidth, coordinatePixelWidths.z));
    return vec3(
            max(axisCoverages.y, axisCoverages.z),
            max(axisCoverages.x, axisCoverages.z),
            max(axisCoverages.x, axisCoverages.y));
}

vec3 surfaceGridPlaneSlotCoverages(
        highp vec3 worldPosition,
        float spacing,
        float thicknessValue,
        float worldPixelWidth,
        vec3 coordinatePixelWidths) {
    vec3 axisCoverages = vec3(
            surfaceGridLineSlotCoverage(worldPosition.x, spacing, thicknessValue,
                                       worldPixelWidth, coordinatePixelWidths.x),
            surfaceGridLineSlotCoverage(worldPosition.y, spacing, thicknessValue,
                                       worldPixelWidth, coordinatePixelWidths.y),
            surfaceGridLineSlotCoverage(worldPosition.z, spacing, thicknessValue,
                                       worldPixelWidth, coordinatePixelWidths.z));
    return vec3(
            max(axisCoverages.y, axisCoverages.z),
            max(axisCoverages.x, axisCoverages.z),
            max(axisCoverages.x, axisCoverages.y));
}

vec4 surfaceGridOverlay() {
    // Materials retain one shared grid; the view independently suppresses it.
    if (materialParams.surfaceGridEnabled <= 0.5 || getMaterialGlobal0().z >= 0.5) {
        return vec4(0.0);
    }

    highp vec3 worldPosition = getUserWorldPosition().xyz;
    highp vec3 rasterWorldPosition = getWorldPosition();
    float worldPixelWidth = max(
            length(dFdx(rasterWorldPosition)),
            length(dFdy(rasterWorldPosition)));
    // Camera-relative derivatives retain small surface footprints when the
    // world position includes a distant uniform translation.
    highp mat3 userWorldFromWorld = mat3(getUserWorldFromWorldMatrix());
    highp vec3 userWorldDx = userWorldFromWorld * dFdx(rasterWorldPosition);
    highp vec3 userWorldDy = userWorldFromWorld * dFdy(rasterWorldPosition);
    vec3 coordinatePixelWidths = abs(userWorldDx) + abs(userWorldDy);
    float baseSpacing = max(materialParams.surfaceGridSize, 0.0001);
    float majorSpacing = max(materialParams.surfaceGridMajorSize, baseSpacing);
    vec4 faceProjection = variable_surfaceGridProjection;
    // Coarse face normals are authored in user world by their CPU owner;
    // ordinary normals include Filament's internal environment rotation.
    vec3 faceNormal = materialParams.coarseGeometryEnabled > 0.5
            ? faceProjection.xyz : userWorldFromWorld * faceProjection.xyz;
    vec3 projectionWeights = faceProjection.w > 0.5
            ? surfaceGridFaceProjectionWeights(faceNormal)
            : surfaceGridProjectionWeights(
                    userWorldFromWorld * getWorldGeometricNormalVector());
    vec3 minorPlaneCoverages = surfaceGridPlaneLineCoverages(
            worldPosition, baseSpacing,
            materialParams.surfaceGridMinorLineThickness,
            worldPixelWidth, coordinatePixelWidths);
    vec3 majorPlaneCoverages = surfaceGridPlaneLineCoverages(
            worldPosition, majorSpacing,
            materialParams.surfaceGridMajorLineThickness,
            worldPixelWidth, coordinatePixelWidths);

    float majorSlotThickness = max(
            materialParams.surfaceGridMinorLineThickness,
            materialParams.surfaceGridMajorLineThickness);
    vec3 majorSlotCoverages = surfaceGridPlaneSlotCoverages(
            worldPosition, majorSpacing, majorSlotThickness,
            worldPixelWidth, coordinatePixelWidths);
    vec3 majorSlotMask = step(vec3(0.000001), majorSlotCoverages);
    float minorLine = dot(
            minorPlaneCoverages * (vec3(1.0) - majorSlotMask),
            projectionWeights);
    float majorLine = dot(majorPlaneCoverages, projectionWeights);

    float majorCoverage = majorLine * materialParams.surfaceGridMajorColor.a;
    float minorCoverage =
            minorLine * materialParams.surfaceGridMinorColor.a;
    float alpha = clamp(minorCoverage + majorCoverage, 0.0, 1.0);
    if (alpha <= 0.000001) {
        return vec4(0.0);
    }
    vec3 gridColor = (materialParams.surfaceGridMinorColor.rgb * minorCoverage
            + materialParams.surfaceGridMajorColor.rgb * majorCoverage) / alpha;
    return vec4(gridColor, alpha);
}

vec3 applySurfaceGrid(vec3 baseColor) {
    vec4 grid = surfaceGridOverlay();
    // Fixed editor colors use coverage-scaled alpha over the shaded surface.
    return mix(baseColor, grid.rgb, grid.a);
}
