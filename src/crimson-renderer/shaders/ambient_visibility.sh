#ifndef CC_AMBIENT_VISIBILITY_SH
#define CC_AMBIENT_VISIBILITY_SH

// The optional darkness response attenuates the complete reflected-light sum:
// directional and local direct lighting, plus image based lighting. Local light
// reveal roles independently determine their contribution to this shared mask.
// The same equations serve depth producers and transparent surface receivers.
uniform vec4 u_ambientSettings; // Enabled, strength, darkness floor, shadow aware.
uniform vec4 u_ambientTransition; // Energy threshold, transition width, reserved.
uniform vec4 u_revealAtlas; // First packed vec4, independent reveal source count.
uniform vec4 u_lightingResponse; // Attenuate Lit emission, reserved YZW.
#include "ambient_visibility_response.sh"

float ambientVisibilityResponse(float energy)
{
    return ambientVisibilityResponse(energy,u_ambientTransition.x,u_ambientTransition.y,u_ambientSettings.z,u_ambientSettings.y);
}
float ambientVisibilityAt(vec3 position, vec3 geometricNormal, vec3 planeNormal, uint receiverMask)
{
    if ((receiverMask & 3u) == 0u) return 1.0;
    float energy = 0.0;
    for (int light = 0; light < 4; ++light)
    {
        if (float(light) >= u_lightCounts.x) break;
        vec4 roles = u_directionLightingRoles[light];
        if ((uint(roles.y) & receiverMask & 3u) == 0u || roles.z <= 0.0) continue;
        float visibility = 1.0;
        if (u_ambientSettings.w > 0.5 && roles.w > 0.5)
            visibility = directionalShadowVisibility(light, position, geometricNormal, planeNormal);
        energy += u_direction[light].w * roles.z * visibility;
    }
    int cluster = forwardPlusCluster(position);
    for (int word = 0; word < forwardPlusWordCount(); ++word)
    {
        uint mask = forwardPlusMask(cluster, word);
        while (mask != 0u)
        {
            int light = word * 32 + forwardPlusFirstBit(mask);
            mask &= mask - 1u;
            vec4 roles = forwardPlusPointRoles(light);
            uint flags = uint(roles.z);
            if ((flags & 16u) == 0u || (((flags >> 2u) & receiverMask & 3u) == 0u) || roles.w <= 0.0) continue;
            vec4 pointPosition = forwardPlusPointPosition(light);
            vec4 pointColor = forwardPlusPointColor(light);
            vec4 pointResponse = forwardPlusPointResponse(light);
            vec3 delta = pointPosition.xyz - position;
            float attenuation = pointLightAttenuation(max(dot(delta, delta), 0.000001), pointColor.w, pointResponse.x, pointResponse.z, pointResponse.w, forwardPlusPointAttenuation(light));
            if (attenuation <= 0.0) continue;
            float visibility = 1.0;
            if (u_ambientSettings.w > 0.5 && (flags & 32u) != 0u)
                visibility = pointShadowVisibility(int(roles.x), pointPosition, pointColor, position, geometricNormal, planeNormal, roles.y);
            energy += pointPosition.w * attenuation * roles.w * visibility;
        }
    }
    float visibility = ambientVisibilityResponse(energy);
    // Distance reveal components emit no light. Independent sources combine
    // through max after the global strength/floor response, never by addition.
    for (int source = 0; source < int(u_revealAtlas.y); ++source)
    {
        if (visibility >= 1.0) break;
        vec4 profile = forwardPlusFetch(int(u_revealAtlas.x) + source);
        vec3 delta = position - profile.xyz;
        float distanceSquared = dot(delta, delta);
        float support = profile.w * 8.0;
        if (distanceSquared >= support * support) continue;
        float weight = radialRevealWeightSquared(distanceSquared, profile.w);
        visibility = max(visibility, playerRevealVisibility(weight, u_ambientSettings.z, u_ambientSettings.y));
    }
    return visibility;
}
float ambientVisibility(vec3 position, vec3 geometricNormal, vec3 planeNormal)
{
    if (u_ambientSettings.x < 0.5) return 1.0;
    if (u_surfaceLighting.w > 0.5 || u_ambientAtlas.w < 0.5)
        return ambientVisibilityAt(position, geometricNormal, planeNormal, uint(u_surfaceLighting.y));
    vec3 view = mul(u_clusterView, vec4(position, 1.0)).xyz;
    float depth = max(-view.z, u_clusterDepth.z);
    vec2 ndc = view.xy / (depth * u_clusterViewport.zw);
    ivec2 pixel = ivec2(clamp(floor(vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * u_ambientAtlas.yz),
        vec2_splat(0.0), u_ambientAtlas.yz - vec2_splat(1.0)));
    int pixelIndex = pixel.y * int(u_ambientAtlas.y) + pixel.x;
    vec4 values = forwardPlusFetch(int(u_ambientAtlas.x) + pixelIndex / 4);
    return values[pixelIndex - 4 * (pixelIndex / 4)];
}
#endif
