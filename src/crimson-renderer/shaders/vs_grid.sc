$input a_position
$output v_worldPosition

#include <bgfx_shader.sh>

void main()
{
    vec4 world = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPosition = world.xyz;
    // Keep frustum clipping geometric. The fragment stage evaluates the
    // coplanar bias after reconstructing depth from the world interpolant.
    gl_Position = mul(u_viewProj, world);
}
