#ifndef CC_DEBUG_VIEW_SH
#define CC_DEBUG_VIEW_SH

// Per-viewport inspection mode. Data channels are kept in linear space; the
// presentation pass selects whether to apply lighting exposure/tone mapping.
uniform vec4 u_debugView; // Lit, Albedo, Roughness, Metal, World Normal, Direct, Indirect.

bool debugViewActive()
{
    return u_debugView.x > 0.5;
}

vec3 debugViewColor(vec3 lit, vec3 albedo, float roughness, float metallic, vec3 normal,
    vec3 directLighting, vec3 indirectLighting)
{
    if (u_debugView.x < 0.5) return lit;
    if (u_debugView.x < 1.5) return albedo;
    if (u_debugView.x < 2.5) return vec3_splat(roughness);
    if (u_debugView.x < 3.5) return vec3_splat(metallic);
    if (u_debugView.x < 4.5)
        return normal * inversesqrt(max(dot(normal, normal), 0.00000001)) * 0.5 + vec3_splat(0.5);
    if (u_debugView.x < 5.5) return directLighting;
    return indirectLighting;
}

#endif
