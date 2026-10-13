$input a_position
$output v_worldPosition

#include <bgfx_shader.sh>

uniform vec4 u_gridParams;

void main()
{
    vec4 world = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPosition = world.xyz;
    vec4 clip = mul(u_viewProj, world);
    // Quader uses reversed depth. Convert its bias to bgfx's forward-depth clip range.
    float depth = clip.z / clip.w;
    depth = mix(depth, depth * 0.5 + 0.5, u_gridParams.w);
    float bias = mix(0.000001, u_gridParams.z, 1.0 - clamp(depth, 0.0, 1.0));
    clip.z -= clip.w * bias * (1.0 + u_gridParams.w);
    gl_Position = clip;
}
