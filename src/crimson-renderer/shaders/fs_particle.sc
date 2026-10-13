$input v_particleUv, v_particleColor

#include <bgfx_shader.sh>

void main()
{
    float edge = max(1.0 - dot(v_particleUv, v_particleUv), 0.0);
    float opacity = edge * edge * v_particleColor.a;
    // RGB is premultiplied radiance. The additive blend preserves destination
    // alpha, which stores covered fraction rather than emissive brightness.
    gl_FragColor = vec4(v_particleColor.rgb * opacity, opacity);
}
