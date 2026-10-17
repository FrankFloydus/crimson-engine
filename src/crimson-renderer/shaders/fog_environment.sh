#ifndef CRIMSON_FOG_ENVIRONMENT_SH
#define CRIMSON_FOG_ENVIRONMENT_SH

#include "fog.sh"

// Include after u_cameraPos/u_environment and s_prefiltered are declared.
// Aerial perspective reuses the existing environment cubemap; it never takes
// another material texture slot or inherits the diffuse-lighting enable bit.
vec3 applySurfaceFog(vec3 radiance, vec3 worldPosition, float additive)
{
    if (u_fogHeight.w <= 0.0) return radiance;
    vec3 eyeToSurface = worldPosition - u_cameraPos.xyz;
    float distanceSquared = dot(eyeToSurface, eyeToSurface);
    float distanceToEye = sqrt(max(distanceSquared, 0.0));
    // Additive materials attenuate only; sky color and sun scattering do not
    // add fog radiance or change their destination coverage.
    if (additive > 0.5)
        return applyDistanceFog(radiance, distanceToEye, worldPosition.y, additive);
    vec3 direction = eyeToSurface * inversesqrt(max(distanceSquared, 1.0e-12));
    vec3 skyRadiance = u_fogColorDensity.rgb;
    if (u_fogSky.y > 0.0)
    {
        float c = cos(u_environment.y);
        float s = sin(u_environment.y);
        vec3 skyDirection = vec3(c * direction.x + s * direction.z, direction.y,
            -s * direction.x + c * direction.z);
        skyRadiance = textureCubeLod(s_prefiltered, skyDirection, 0.0).rgb * u_fogSky.w;
    }
    return applyDistanceFog(radiance, distanceToEye, worldPosition.y, additive, skyRadiance, direction);
}

#endif
