#include "environment_ambient.sh"

CONST(float PI = 3.141592653589793);

vec3 safeNormalize(vec3 value, vec3 fallback)
{
    float lengthSquared = dot(value, value);
    if (lengthSquared <= 0.00000001) return fallback;
    return value * inversesqrt(lengthSquared);
}

vec3 environmentDirection(vec3 direction)
{
    float cosine = cos(u_environment.y);
    float sine = sin(u_environment.y);
    return vec3(cosine * direction.x + sine * direction.z, direction.y,
                -sine * direction.x + cosine * direction.z);
}

// Keep these BRDF and IBL equations in agreement with fs_pbr.sc. Imported
// models and the preview sphere share the same lighting response.
vec3 evaluateEnvironment(vec3 N, vec3 V, vec3 base, float metallic, float roughness, float specularLevel, out vec3 diffuse)
{
    diffuse = vec3_splat(0.0);
    if (u_environment.x <= 0.0 && dot(u_ambientLight.rgb, vec3_splat(1.0)) <= 0.0) return vec3_splat(0.0);
    float NoV = clamp(dot(N, V), 0.00001, 1.0);
    vec3 F0 = mix(vec3_splat(0.08 * clamp(specularLevel, 0.0, 1.0)), base, metallic);
    vec3 F = F0 + (vec3_splat(1.0) - F0) * pow(1.0 - NoV, 5.0);
    diffuse = environmentAmbientIrradiance(textureCube(s_irradiance, environmentDirection(N)).rgb);
    diffuse *= (vec3_splat(1.0) - F) * (1.0 - metallic) * base;
    vec3 reflection = reflect(-V, N);
    float specularLod = (roughness - 0.045) / (1.0 - 0.045) * u_environment.z;
    vec3 prefiltered = textureCubeLod(s_prefiltered, environmentDirection(reflection), specularLod).rgb;
    vec2 lut = forwardPlusBrdf(vec2(NoV, roughness));
    vec3 specular = prefiltered * (F0 * lut.x + vec3_splat(lut.y));
    return diffuse + specular * u_environment.x;
}

vec3 evaluateEnvironment(vec3 N, vec3 V, vec3 base, float metallic, float roughness, float specularLevel)
{
    vec3 diffuse;
    return evaluateEnvironment(N, V, base, metallic, roughness, specularLevel, diffuse);
}

vec3 evaluateLightDiffuse(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 base, float metallic, float specularLevel)
{
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    if (NoL <= 0.0 || dot(N, V) <= 0.0) return vec3_splat(0.0);
    vec3 halfSum = V + L;
    vec3 H = halfSum * inversesqrt(max(dot(halfSum, halfSum), 0.00000000000001));
    float VoH = clamp(dot(V, H), 0.0, 1.0);
    vec3 F0 = mix(vec3_splat(0.08 * clamp(specularLevel, 0.0, 1.0)), base, metallic);
    vec3 F = F0 + (vec3_splat(1.0) - F0) * pow(1.0 - VoH, 5.0);
    return (vec3_splat(1.0) - F) * (1.0 - metallic) * base * radiance * (NoL / PI);
}

vec3 evaluateLightResponse(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 base, float metallic, float roughness, float specularLevel,
    float diffuseResponse, float specularResponse, float softSourceSinSquared)
{
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float rawNoV = dot(N, V);
    if (NoL <= 0.0 || rawNoV <= 0.0) return vec3_splat(0.0);
    float NoV = clamp(rawNoV, 0.00001, 1.0);
    vec3 halfSum = V + L;
    vec3 H = halfSum * inversesqrt(max(dot(halfSum, halfSum), 0.00000001));
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);
    vec3 F0 = mix(vec3_splat(0.08 * clamp(specularLevel, 0.0, 1.0)), base, metallic);
    vec3 F = F0 + (vec3_splat(1.0) - F0) * pow(1.0 - VoH, 5.0);
    float alpha = roughness * roughness;
    float alphaSquared = pointLightSpecularAlphaSquared(alpha * alpha, VoH, softSourceSinSquared);
    float denom = NoH * NoH * (alphaSquared - 1.0) + 1.0;
    float D = alphaSquared / max(PI * denom * denom, 0.000000000001);
    float smithL = NoV * sqrt(NoL * NoL * (1.0 - alphaSquared) + alphaSquared);
    float smithV = NoL * sqrt(NoV * NoV * (1.0 - alphaSquared) + alphaSquared);
    float visibility = 0.5 / max(smithL + smithV, 0.000001);
    vec3 specular = D * visibility * F;
    vec3 diffuse = (vec3_splat(1.0) - F) * (1.0 - metallic) * base / PI;
    return (diffuse * diffuseResponse + specular * specularResponse) * radiance * NoL;
}

vec3 evaluateLightResponse(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 base, float metallic, float roughness, float specularLevel,
    float diffuseResponse, float specularResponse)
{
    return evaluateLightResponse(N, V, L, radiance, base, metallic, roughness, specularLevel, diffuseResponse, specularResponse, 0.0);
}

vec3 evaluateLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 base, float metallic, float roughness, float specularLevel)
{
    return evaluateLightResponse(N, V, L, radiance, base, metallic, roughness, specularLevel, 1.0, 1.0);
}
