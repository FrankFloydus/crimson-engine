$input v_particleUv, v_particleColor

#include <bgfx_shader.sh>

SAMPLER2D(s_particleColor, 0);
uniform vec4 u_particleBaseColor;
uniform vec4 u_particleParams;

void main()
{
    vec2 radial = v_particleUv * 2.0 - 1.0;
    float edge = max(1.0 - dot(radial, radial), 0.0);
    // Color-role textures decode RGB in the sampler; alpha remains linear.
    // A supplied sprite replaces the procedural mask rather than multiplying it.
    vec4 sprite = texture2D(s_particleColor, v_particleUv);
    sprite = mix(vec4(1.0, 1.0, 1.0, edge * edge), sprite, u_particleParams.x);
    float opacity = sprite.a * u_particleBaseColor.a * v_particleColor.a;
    vec3 radiance = sprite.rgb * u_particleBaseColor.rgb * v_particleColor.rgb;
    // Premultiply once. Alpha OVER accumulates coverage; Additive preserves it.
    gl_FragColor = vec4(radiance * opacity, opacity);
}
