$input v_particleUv, v_particleColor, v_particleViewPosition

#include <bgfx_shader.sh>

SAMPLER2D(s_particleColor, 0);
uniform vec4 u_particleBaseColor;
uniform vec4 u_particleParams;
#include "material_mapping.sh"
#include "fog.sh"

void main()
{
    vec2 radial = v_particleUv * 2.0 - 1.0;
    float edge = max(1.0 - dot(radial, radial), 0.0);
    // Color-role textures decode RGB in the sampler; alpha remains linear.
    // A supplied sprite replaces the procedural mask rather than multiplying it.
    // Billboards retain their 2D UV domain even when the shared material uses
    // triplanar mapping on 3D meshes; its XY transform and sampler still apply.
    vec4 sprite = texture2D(s_particleColor, materialUv(v_particleUv));
    sprite = mix(vec4(1.0, 1.0, 1.0, edge * edge), sprite, u_particleParams.x);
    float opacity = sprite.a * u_particleBaseColor.a * v_particleColor.a;
    vec3 radiance = sprite.rgb * u_particleBaseColor.rgb * v_particleColor.rgb;
    radiance = applyDistanceFog(radiance, length(v_particleViewPosition.xyz), v_particleViewPosition.w, u_particleParams.y);
    // Premultiply once. Alpha OVER accumulates coverage; Additive preserves it.
    gl_FragColor = vec4(fogPremultipliedChannel(radiance.x, opacity),
        fogPremultipliedChannel(radiance.y, opacity), fogPremultipliedChannel(radiance.z, opacity), opacity);
}
