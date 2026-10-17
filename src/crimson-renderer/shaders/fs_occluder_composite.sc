$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_occluderColor, 0);
SAMPLER2D(s_occluderDepth, 1);
uniform vec4 u_occluderComposite; // Bottom-left render target orientation.
void main()
{
    vec2 uv = v_texcoord0;
    uv.y = mix(uv.y, 1.0 - uv.y, u_occluderComposite.x);
    vec4 color = texture2D(s_occluderColor, uv);
    float depth = texture2D(s_occluderDepth, uv).x;
    if (color.a <= 0.0 || depth >= 1.0) discard;
    // Sampled D24 is already normalized device depth. Test the nearest faded
    // surface against opaque/player depth without changing that depth buffer.
    gl_FragDepth = depth;
    gl_FragData[0] = color;
    gl_FragData[1] = vec4_splat(0.0);
    gl_FragData[2] = vec4_splat(0.0);
    gl_FragData[3] = vec4_splat(0.0);
}
