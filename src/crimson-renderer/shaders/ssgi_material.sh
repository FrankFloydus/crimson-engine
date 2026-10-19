#ifndef CC_SSGI_MATERIAL_SH
#define CC_SSGI_MATERIAL_SH
#include "prepass_receiver.sh"

// Published incoming irradiance / PI occupies a camera-local region of the
// existing Forward+ data atlas; no material texture stage is added.
uniform vec4 u_ssgiAtlas; // First texel, viewport width/height, published.
uniform vec4 u_ssgiSettings; // Enabled, strength, reserved ZW.

bool giReceiverSurface(float blendMode, float instanceOpacity)
{
    return u_ssgiSettings.x > 0.5 && u_ssgiDraw.x > 0.5 && blendMode < 1.5 && instanceOpacity >= 1.0;
}
bool ssgiCaptureSurface(float blendMode, float instanceOpacity)
{
    return giReceiverSurface(blendMode, instanceOpacity);
}

vec3 ssgiDiffuseResponse(vec3 N, vec3 V, vec3 base, float metallic, float roughness, float specularLevel, float occlusion)
{
    vec3 specularResponse = integratedSpecularResponse(N, V, base, metallic, roughness, specularLevel);
    return integratedDiffuseResponse(base, metallic, specularResponse) * occlusion;
}

vec3 ssgiDiffuse(vec3 worldPosition, vec3 diffuseResponse)
{
    if (u_ssgiAtlas.w < 0.5 || u_ssgiSettings.x < 0.5 || u_ssgiDraw.x < 0.5 || (uint(u_surfaceLighting.x) & 3u) == 0u)
        return vec3_splat(0.0);
    vec3 view = mul(u_clusterView, vec4(worldPosition, 1.0)).xyz;
    vec2 ndc = view.xy / (max(-view.z, u_clusterDepth.z) * u_clusterViewport.zw);
    vec2 pixel = floor(vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * u_ssgiAtlas.yz);
    pixel = clamp(pixel, vec2_splat(0.0), u_ssgiAtlas.yz - vec2_splat(1.0));
    vec3 incoming = max(forwardPlusFetch(int(u_ssgiAtlas.x) + int(pixel.y) * int(u_ssgiAtlas.y) + int(pixel.x)).rgb, vec3_splat(0.0));
    return incoming * max(u_ssgiSettings.y, 0.0) * diffuseResponse;
}

void ssgiAccumulateSource(vec3 diffuse, uint receivers, inout vec3 worldSource, inout vec3 characterSource)
{
    if ((receivers & 1u) != 0u) worldSource += diffuse;
    if ((receivers & 2u) != 0u) characterSource += diffuse;
}

vec2 ssgiSourceNormal(vec3 position, vec3 planeNormal)
{
    vec3 towardCamera = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz - position;
    vec3 normal = dot(planeNormal, towardCamera) < 0.0 ? -planeNormal : planeNormal;
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 0.00000001);
    vec2 oct = normal.xy;
    if (normal.z < 0.0)
        oct = (vec2_splat(1.0) - abs(oct.yx)) * vec2(oct.x < 0.0 ? -1.0 : 1.0, oct.y < 0.0 ? -1.0 : 1.0);
    return oct * 0.5 + vec2_splat(0.5);
}

void ssgiWriteSources(vec3 worldSource, vec3 characterSource, vec3 diffuseResponse, vec3 position, vec3 planeNormal, float deviceDepth, bool capture,
    out vec4 worldOutput, out vec4 characterOutput, out vec4 geometryOutput)
{
    // A zero source alpha preserves the opaque source under the existing
    // premultiplied Alpha blend; Additive also preserves it by adding zero.
    worldOutput = capture ? vec4(clamp(worldSource, vec3_splat(0.0), vec3_splat(60000.0)), 1.0) : vec4_splat(0.0);
    characterOutput = capture ? vec4(clamp(characterSource, vec3_splat(0.0), vec3_splat(60000.0)), 1.0) : vec4_splat(0.0);
    // All 23 payload bits are exact in RGBA32F. Response excludes authored GI
    // strength and is bounded so additional diffuse path segments lose energy.
    uvec3 response = uvec3(floor(clamp(diffuseResponse, vec3_splat(0.0), vec3_splat(1.0)) * 127.0 + vec3_splat(0.5)));
    uint payload = (u_ssgiDraw.y > 0.5 ? 2u : 1u) | (response.r << 2u) | (response.g << 9u) | (response.b << 16u);
    geometryOutput = capture ? vec4(ssgiSourceNormal(position, planeNormal), deviceDepth, float(payload)) : vec4_splat(0.0);
}

#endif
