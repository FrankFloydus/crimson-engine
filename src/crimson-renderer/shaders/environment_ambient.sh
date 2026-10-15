#ifndef CC_ENVIRONMENT_AMBIENT_SH
#define CC_ENVIRONMENT_AMBIENT_SH

// Custom color is decoded and weighted once on the CPU. The alpha component
// weights sky diffuse only; u_environment.x remains the sky/reflection energy.
uniform vec4 u_ambientLight;

vec3 environmentAmbientIrradiance(vec3 skyIrradiance)
{
    return u_ambientLight.rgb + skyIrradiance * (u_ambientLight.w * u_environment.x);
}

#endif
