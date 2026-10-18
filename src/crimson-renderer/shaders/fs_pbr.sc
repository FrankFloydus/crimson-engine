$input v_worldPosition, v_normal, v_texcoord0, v_tangent

#include <bgfx_shader.sh>
#include "fog.sh"
#include "point_light.sh"

uniform vec4 u_baseColorMetallic;
uniform vec4 u_roughness;
uniform vec4 u_cameraPos;
uniform vec4 u_direction[4];
uniform vec4 u_directionColor[4];
uniform vec4 u_lightCounts;
uniform vec4 u_textureFlags;
uniform vec4 u_environment; // Intensity, Y rotation in radians, highest specular mip.
#include "environment_ambient.sh"
SAMPLER2D(s_albedo, 0);
SAMPLER2D(s_roughness, 1);
SAMPLER2D(s_metallic, 2);
SAMPLER2D(s_normal, 3);
SAMPLERCUBE(s_irradiance, 4);
SAMPLERCUBE(s_prefiltered, 5);
SAMPLER2D(s_brdfLut, 6);
#include "fog_environment.sh"
#include "clustered_lighting.sh"
#include "point_shadow.sh"
#include "directional_shadow.sh"
#include "debug_view.sh"
#include "ssao_material.sh"

// CONST is static const on HLSL: a plain global const becomes an unset uniform.
CONST(float PI = 3.141592653589793);

vec3 safeNormalize(vec3 value, vec3 fallback)
{
    float lengthSquared = dot(value, value);
    if (lengthSquared <= 0.00000001) return fallback;
    return value * inversesqrt(lengthSquared);
}

vec3 materialNormal(vec3 normal, vec4 tangent, vec2 uv)
{
    vec3 N = safeNormalize(normal, vec3(0.0, 1.0, 0.0));
    if (u_textureFlags.w < 0.5 || u_roughness.y <= 0.0) return N;

    // Gram-Schmidt removes interpolation skew. Degenerate future mesh tangents
    // get a stable orthogonal basis instead of an inverse-square-root of zero.
    vec3 axis = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 fallbackT = normalize(cross(axis, N));
    vec3 T = safeNormalize(tangent.xyz - N * dot(N, tangent.xyz), fallbackT);
    vec3 B = cross(N, T) * (tangent.w < 0.0 ? -1.0 : 1.0);
    vec3 tangentNormal = texture2D(s_normal, uv).xyz * 2.0 - 1.0;
    // Both neighboring 8-bit encodings of 0.5 are neutral. In particular a
    // conventional (128,128,255) flat map must reproduce the untextured sphere.
    if (abs(tangentNormal.x) <= 0.00392257) tangentNormal.x = 0.0;
    if (abs(tangentNormal.y) <= 0.00392257) tangentNormal.y = 0.0;
    tangentNormal.xy *= clamp(u_roughness.y, 0.0, 2.0);
    tangentNormal = safeNormalize(tangentNormal, vec3(0.0, 0.0, 1.0));
    return safeNormalize(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z, N);
}

vec3 environmentDirection(vec3 direction)
{
    float cosine = cos(u_environment.y);
    float sine = sin(u_environment.y);
    return vec3(cosine * direction.x + sine * direction.z, direction.y,
                -sine * direction.x + cosine * direction.z);
}

vec3 evaluateEnvironment(vec3 N, vec3 V, vec3 base, float metallic, float roughness)
{
    if (u_environment.x <= 0.0 && dot(u_ambientLight.rgb, vec3_splat(1.0)) <= 0.0) return vec3_splat(0.0);
    float NoV = clamp(dot(N, V), 0.00001, 1.0);
    vec3 F0 = mix(vec3_splat(0.08 * clamp(u_roughness.z, 0.0, 1.0)), base, metallic);
    vec3 F = F0 + (vec3_splat(1.0) - F0) * pow(1.0 - NoV, 5.0);
    // Preprocessing stores irradiance / PI, so its normalization is applied once.
    vec3 diffuse = environmentAmbientIrradiance(textureCube(s_irradiance, environmentDirection(N)).rgb);
    diffuse *= (vec3_splat(1.0) - F) * (1.0 - metallic) * base;
    vec3 reflection = reflect(-V, N);
    // The prefilter spans the usable roughness interval, including both endpoints.
    float specularLod = (roughness - 0.045) / (1.0 - 0.045) * u_environment.z;
    vec3 prefiltered = textureCubeLod(s_prefiltered, environmentDirection(reflection), specularLod).rgb;
    vec2 lut = forwardPlusBrdf(vec2(NoV, roughness));
    // LUT uses the direct BRDF's correlated Smith visibility and alpha=roughness^2.
    vec3 specular = prefiltered * (F0 * lut.x + vec3_splat(lut.y));
    return diffuse + specular * u_environment.x;
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

    // Perceptual roughness -> GGX alpha. Smith's correlated visibility includes 1/(4 NoL NoV).
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

vec3 evaluateLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 base, float metallic, float roughness)
{
    return evaluateLightResponse(N, V, L, radiance, base, metallic, roughness, u_roughness.z, 1.0, 1.0);
}


void main()
{
    vec3 shadowPlaneNormal = pointShadowPlaneNormal(v_worldPosition, v_normal);
    vec3 N = materialNormal(v_normal, v_tangent, v_texcoord0);
    vec3 V = safeNormalize(u_cameraPos.xyz - v_worldPosition, N);
    vec3 base = u_baseColorMetallic.rgb;
    float metallic = u_baseColorMetallic.a;
    float roughness = u_roughness.x;
    if (u_textureFlags.x > 0.5) base *= texture2D(s_albedo, v_texcoord0).rgb;
    if (u_textureFlags.y > 0.5) roughness *= texture2D(s_roughness, v_texcoord0).r;
    if (u_textureFlags.z > 0.5) metallic *= texture2D(s_metallic, v_texcoord0).r;
    roughness = clamp(roughness, 0.045, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    vec3 color = vec3_splat(0.0);
    for (int i = 0; i < 4; ++i)
    {
        if (float(i) >= u_lightCounts.x) break;
        if (!forwardPlusReceivesDirectional(i)) continue;
        color += evaluateLight(N, V, normalize(-u_direction[i].xyz),
            u_directionColor[i].rgb * u_direction[i].w, base, metallic, roughness);
    }
    uint cluster = forwardPlusCluster(v_worldPosition);
    for (uint word = 0u; word < forwardPlusWordCount(); ++word)
    {
      uint mask = forwardPlusMask(cluster, word);
      while (mask != 0u)
      {
        int i = int(word * 32u) + forwardPlusFirstBit(mask);
        mask &= mask - 1u;
        if (!forwardPlusReceivesPoint(i)) continue;
        vec4 pointPosition = forwardPlusPointPosition(i);
        vec4 pointColor = forwardPlusPointColor(i);
        vec3 delta = pointPosition.xyz - v_worldPosition;
        float distanceSquared = max(dot(delta, delta), 0.000001);
        vec4 pointResponse = forwardPlusPointResponse(i);
        float attenuation = pointLightAttenuation(distanceSquared, pointColor.w, pointResponse.x, pointResponse.z, pointResponse.w, forwardPlusPointAttenuation(i));
        if (attenuation <= 0.0) continue;
        vec3 L = delta * inversesqrt(distanceSquared);
        vec3 radiance = pointColor.rgb * (pointPosition.w * attenuation);
        vec3 shadowNormal = safeNormalize(v_normal, vec3(0.0, 1.0, 0.0));
        radiance *= pointShadowVisibility(forwardPlusShadowIndex(i), pointPosition, pointColor, v_worldPosition, shadowNormal, shadowPlaneNormal, forwardPlusSourceRadius(i));
        float softSourceSinSquared = pointLightSourceSinSquared(distanceSquared, forwardPlusSourceRadius(i));
        color += evaluateLightResponse(N, V, L, radiance, base, metallic, roughness, u_roughness.z, 1.0, pointResponse.y, softSourceSinSquared);
      }
    }
    vec3 directLighting = color;
    vec3 indirectLighting = evaluateEnvironment(N, V, base, metallic, roughness) * ssaoVisibility(v_worldPosition);
    color += indirectLighting;
    // Keep extreme near-light highlights finite in half-float HDR storage.
    color = debugViewColor(color, base, roughness, metallic, N, directLighting, indirectLighting);
    if (!debugViewActive()) color = applySurfaceFog(color, v_worldPosition, 0.0);
    gl_FragData[0] = vec4(clamp(color, vec3_splat(0.0), vec3_splat(60000.0)), 1.0);
    // The legacy sphere is isolated preview content, never a GI source.
    gl_FragData[1] = vec4_splat(0.0);
    gl_FragData[2] = vec4_splat(0.0);
    gl_FragData[3] = vec4_splat(0.0);
}
