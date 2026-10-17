$input v_texcoord0
#include <bgfx_shader.sh>
#include "procedural_sky.sh"
#include "fog.sh"
SAMPLERCUBE(s_skyEnvironment, 0);
uniform vec4 u_skyRight;
uniform vec4 u_skyUp;
uniform vec4 u_skyForward;
uniform vec4 u_environment;
uniform vec4 u_background;
void main()
{
    gl_FragData[1] = vec4_splat(0.0);
    gl_FragData[2] = vec4_splat(0.0);
    gl_FragData[3] = vec4_splat(0.0);
    vec3 direction = normalize(u_skyForward.xyz + (v_texcoord0.x * 2.0 - 1.0) * u_skyRight.xyz + (1.0 - v_texcoord0.y * 2.0) * u_skyUp.xyz);
    vec3 skyColor = u_background.rgb;
    bool visibleSource = u_environment.z > 0.5;
    if (visibleSource && u_environment.w > 0.5)
    {
        float footprint = length(cross(dFdx(direction), dFdy(direction)));
        skyColor = proceduralSkyRadiance(direction, footprint) * u_environment.x;
    }
    else if (visibleSource)
    {
        float c = cos(u_environment.y);
        float s = sin(u_environment.y);
        vec3 lookup = vec3(c * direction.x + s * direction.z, direction.y, -s * direction.x + c * direction.z);
        skyColor = textureCubeLod(s_skyEnvironment, lookup, 0.0).rgb * u_environment.x;
    }
    // Use a finite camera-far proxy so density decreases continuously to zero.
    // Height remains in world space, independent of HDRI texture rotation.
    float worldHeight = u_skyRight.w + direction.y * u_skyForward.w;
    float transmission = fogCombinedTransmittance(u_fogColorDensity.w, u_skyForward.w,
        u_fogDepth.x, u_fogDepth.y, u_fogDepth.z, u_fogDepth.w, worldHeight,
        u_fogHeight.x, u_fogHeight.y, u_fogHeight.z, u_fogHeight.w);
    float opacity = fogBackgroundOpacity(transmission, u_fogSky.x);
    vec3 source = visibleSource ? skyColor : vec3_splat(0.0);
    vec3 radiance = source * (1.0 - opacity) + fogLightColor(skyColor, direction) * opacity;
    gl_FragData[0] = vec4(clamp(radiance, vec3_splat(0.0), vec3_splat(60000.0)), visibleSource ? 1.0 : opacity);
}
