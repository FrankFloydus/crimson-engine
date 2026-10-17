#ifndef CC_PREPASS_RECEIVER_SH
#define CC_PREPASS_RECEIVER_SH
vec3 receiverNormalEncode(vec3 normal)
{
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 0.00000001);
    vec2 encoded = normal.xy;
    if (normal.z < 0.0)
        encoded = (vec2_splat(1.0) - abs(encoded.yx)) * vec2(encoded.x < 0.0 ? -1.0 : 1.0, encoded.y < 0.0 ? -1.0 : 1.0);
    // Signed 12-bit octahedral coordinates preserve zero exactly. Pack their
    // low bytes in G/B and both high nibbles in A; R remains the receiver scope.
    uvec2 quantized = uvec2(floor(clamp(encoded, vec2_splat(-1.0), vec2_splat(1.0)) * 2047.0 + vec2_splat(0.5)) + vec2_splat(2047.0));
    uvec3 packed = uvec3(quantized.x & 255u, quantized.y & 255u,
        (quantized.x >> 8u) | ((quantized.y >> 8u) << 4u));
    return vec3(packed) / 255.0;
}
vec3 receiverNormalDecode(vec3 encoded)
{
    uvec3 packed = uvec3(floor(clamp(encoded, vec3_splat(0.0), vec3_splat(1.0)) * 255.0 + vec3_splat(0.5)));
    uvec2 quantized = uvec2(packed.x | ((packed.z & 15u) << 8u), packed.y | ((packed.z >> 4u) << 8u));
    vec2 oct = (vec2(quantized) - vec2_splat(2047.0)) / 2047.0;
    vec3 normal = vec3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    float unfold = clamp(-normal.z, 0.0, 1.0);
    normal.xy += vec2(normal.x < 0.0 ? unfold : -unfold, normal.y < 0.0 ? unfold : -unfold);
    return normalize(normal);
}
#ifndef CC_LIGHTING_COMPUTE
uniform vec4 u_ssgiDraw; // Capture allowed, reactive geometry/material, reserved ZW.

float prepassReceiverFlags(float ambientMask, float directMask)
{
    uint flags = (uint(ambientMask) & 3u) | ((uint(directMask) & 3u) << 2u);
    if (u_ssgiDraw.y > 0.5) flags |= 16u;
    return float(flags) / 255.0;
}

// Evaluate derivative normals before discard. Encode the actual triangle plane
// rather than rebuilding a noisy normal from quantized distant-camera depth.
vec3 prepassReceiverPlane(vec3 position, vec3 fallback)
{
    vec3 dx = dFdx(position), dy = dFdy(position);
    dx *= inversesqrt(max(dot(dx, dx), 0.00000000000000000001));
    dy *= inversesqrt(max(dot(dy, dy), 0.00000000000000000001));
    vec3 normal = cross(dx, dy);
    float squared = dot(normal, normal);
    return squared > 0.000000000001 ? normal * inversesqrt(squared) : normalize(fallback);
}
vec3 prepassReceiverNormal(vec3 position, vec3 planeNormal)
{
    vec3 towardCamera = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz - position;
    return receiverNormalEncode(dot(planeNormal, towardCamera) < 0.0 ? -planeNormal : planeNormal);
}
#endif
#endif
