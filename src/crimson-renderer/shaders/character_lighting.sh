// Artistic character illumination is independent of world point lights and
// geometry occlusion. Directions are world-space rays and do not follow yaw.
uniform vec4 u_characterDirection[2]; // Ray XYZ, enabled intensity W.
uniform vec4 u_characterColor[2]; // Linear RGB, diffuse response W.
uniform vec4 u_characterResponse[2]; // Specular response X.

vec3 characterLighting(vec3 N, vec3 V, vec3 base, float metallic, float roughness, float specularLevel)
{
    // Entity category and authored light acceptance are independent. Default
    // World meshes/previews cannot receive this through an All direct mask.
    if (u_surfaceCategory.x < 1.5 || (uint(u_surfaceLighting.x) & 2u) == 0u) return vec3_splat(0.0);
    vec3 result = vec3_splat(0.0);
    for (int i = 0; i < 2; ++i)
    {
        if (u_characterDirection[i].w <= 0.0) continue;
        result += evaluateLightResponse(N, V, -u_characterDirection[i].xyz,
            u_characterColor[i].rgb * u_characterDirection[i].w,
            base, metallic, roughness, specularLevel,
            u_characterColor[i].w, u_characterResponse[i].x);
    }
    return result;
}
