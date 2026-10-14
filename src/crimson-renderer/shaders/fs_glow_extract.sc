$input v_texcoord0

#include <bgfx_shader.sh>
#include "glow_sampling.sh"

SAMPLER2D(s_glowSource,0);
uniform vec4 u_glowParams; // Exposure, threshold, vertical orientation, Size.
uniform vec4 u_glowTexel; // Source inverse dimensions, blur step (unused here).

vec3 brightColor(vec2 uv)
{
    // Coverage alpha is deliberately ignored: additive emission has zero coverage.
    vec3 color = max(texture2D(s_glowSource,uv).rgb,vec3_splat(0.0))*u_glowParams.x;
    float brightness = max(max(color.r,color.g),color.b);
    float knee = max(u_glowParams.y*0.5,0.0001);
    float soft = clamp(brightness-u_glowParams.y+knee,0.0,2.0*knee);
    soft = soft*soft/(4.0*knee);
    float contribution = max(brightness-u_glowParams.y,soft)/max(brightness,0.000001);
    return color*contribution;
}

void main()
{
    vec2 uv = v_texcoord0;
    uv.y = mix(uv.y,1.0-uv.y,u_glowParams.z);
    vec2 halfTexel = u_glowTexel.xy*0.5;
    vec3 color = brightColor(uv-halfTexel)+brightColor(uv+halfTexel)
        +brightColor(uv+vec2(halfTexel.x,-halfTexel.y))+brightColor(uv+vec2(-halfTexel.x,halfTexel.y));
    gl_FragColor = vec4(color*0.25,0.0);
}
