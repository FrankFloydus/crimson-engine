////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

    highp float overlayWireSurfaceDepth(highp vec4 first, highp vec4 second,
            highp vec4 support, highp vec2 devicePosition, highp float fallbackDepth) {
        // A projected face has affine device depth even when its supporting
        // vertex is behind the camera. Solve in homogeneous XYW rather than
        // dividing that vertex by W or clipping it to the near plane.
        first /= max(max(abs(first.x), abs(first.y)), max(abs(first.w), 0.000001));
        second /= max(max(abs(second.x), abs(second.y)), max(abs(second.w), 0.000001));
        support /= max(max(abs(support.x), abs(support.y)), max(abs(support.w), 0.000001));
        highp vec3 firstPoint = first.xyw;
        highp vec3 secondPoint = second.xyw;
        highp vec3 supportPoint = support.xyw;
        highp vec3 secondCrossSupport = cross(secondPoint, supportPoint);
        highp float determinant = dot(firstPoint, secondCrossSupport);
        highp float determinantScale = length(firstPoint) * length(secondPoint) * length(supportPoint);
        if (abs(determinant) <= 0.0000001 * determinantScale) {
            return fallbackDepth;
        }
        highp vec3 plane = (first.z * secondCrossSupport +
                second.z * cross(supportPoint, firstPoint) +
                support.z * cross(firstPoint, secondPoint)) / determinant;
        return dot(plane, vec3(devicePosition, 1.0));
    }

