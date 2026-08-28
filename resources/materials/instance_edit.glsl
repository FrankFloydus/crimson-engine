////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

// Shared linear RGB context treatment for mesh surfaces and scene wireframes.
// Keep the semantic-overlay counterpart in InstanceEditStyle aligned.
// Context alpha is a target opacity multiplier, preserving source transparency.
vec4 applyInstanceEditContext(vec4 sourceColor, vec4 contextColor, float amount,
                             float tintAmount, float desaturation) {
    amount = clamp(amount, 0.0, 1.0);
    float luminance = dot(sourceColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 desaturated = mix(sourceColor.rgb, vec3(luminance),
                           clamp(desaturation, 0.0, 1.0));
    vec3 target = mix(desaturated, contextColor.rgb, clamp(tintAmount, 0.0, 1.0));
    return vec4(mix(sourceColor.rgb, target, amount),
                sourceColor.a * mix(1.0, clamp(contextColor.a, 0.0, 1.0), amount));
}
