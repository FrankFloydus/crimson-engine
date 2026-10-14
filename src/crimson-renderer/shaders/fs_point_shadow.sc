$input v_texcoord0, v_texcoord1, v_color0

#include <bgfx_shader.sh>

uniform vec4 u_modelBase;
uniform vec4 u_modelEmissive;
uniform vec4 u_modelUv;
uniform vec4 u_modelOptions;
uniform vec4 u_modelMaps;
SAMPLER2D(s_albedo, 0);

void main()
{
    // Identical alpha silhouette to color and ID picking, including UV1,
    // vertex alpha, authored factor, resident sampler and cutoff.
    float alpha = u_modelBase.a * v_color0.a;
    if (u_modelMaps.x > 0.5)
    {
        vec2 uv = u_modelUv.x > 0.5 ? v_texcoord1 : v_texcoord0;
        alpha *= texture2D(s_albedo, uv).a;
    }
    if (u_modelOptions.y > 0.5 && alpha < u_modelEmissive.w) discard;
    // The framebuffer has only depth; raster depth is stored by fixed function.
    gl_FragColor = vec4_splat(0.0);
}
