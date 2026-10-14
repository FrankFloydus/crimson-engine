$input v_texcoord0
#include <bgfx_shader.sh>
#include "procedural_sky.sh"
SAMPLERCUBE(s_skyEnvironment, 0);
uniform vec4 u_skyRight;
uniform vec4 u_skyUp;
uniform vec4 u_skyForward;
uniform vec4 u_environment;
void main()
{
    vec3 direction = normalize(u_skyForward.xyz + (v_texcoord0.x * 2.0 - 1.0) * u_skyRight.xyz + (1.0 - v_texcoord0.y * 2.0) * u_skyUp.xyz);
    if (u_environment.w > 0.5)
    {
        float footprint = length(cross(dFdx(direction), dFdy(direction)));
        gl_FragColor = vec4(proceduralSkyRadiance(direction, footprint) * u_environment.x, 1.0);
        return;
    }
    float c = cos(u_environment.y);
    float s = sin(u_environment.y);
    direction = vec3(c * direction.x + s * direction.z, direction.y, -s * direction.x + c * direction.z);
    gl_FragColor = vec4(textureCubeLod(s_skyEnvironment, direction, 0.0).rgb * u_environment.x, 1.0);
}
