$input v_worldPosition, v_normal, v_texcoord0, v_texcoord1, v_tangent, v_color0, v_localPosition, v_localNormal
#include <bgfx_shader.sh>
#define CC_GRAPH_SURFACE 0
#define CC_GRAPH_PARTICLE 1
#define CC_GRAPH_LIT 1
uniform vec4 u_graphParams[208];
uniform vec4 u_particleParams; // RGB texture presence only; alpha is explicitly authored.
uniform mat4 u_modelNormalMatrix;
uniform vec4 u_modelUv;
SAMPLER2D(s_particleColor, 0);
#include "material_graph_common.sh"
#include "material_mapping.sh"
GraphMaterial ccEvaluateGraph(GraphContext context)
{
    GraphMaterial result;
    result.baseColor = u_graphParams[0].rgb;
    if (u_particleParams.x > 0.5)
        result.baseColor *= ccGraphSample(s_particleColor, context, u_materialUvScale, u_materialUvOffset, u_materialUvRotation, vec4(0.0, 1.0, 0.0, 0.0)).rgb;
    result.opacity = clamp(u_graphParams[0].a, 0.0, 1.0);
    result.roughness = u_graphParams[1].x;
    result.metallic = u_graphParams[1].y;
    result.occlusion = u_graphParams[1].z;
    result.emission = u_graphParams[2].rgb;
    result.specular = u_graphParams[4].x;
    result.normal = context.normal;
    return result;
}
#include "material_graph_particle.sh"
