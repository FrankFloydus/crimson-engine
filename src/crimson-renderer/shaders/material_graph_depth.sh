#if CC_GRAPH_PICK
uniform vec4 u_pickId;
uniform vec4 u_instanceOpacity;
#else
uniform vec4 u_surfaceLighting;
#endif
#include "prepass_receiver.sh"
void main()
{
#if !CC_GRAPH_PICK
    vec3 receiverPlane = v_normal;
    if (u_surfaceLighting.z > 0.5) receiverPlane = prepassReceiverPlane(v_worldPosition, v_normal);
#endif
    GraphContext context;
    context.particleColor = vec4_splat(1.0);
    context.uv = vec4(v_texcoord0, v_texcoord1);
    context.worldPosition = v_worldPosition;
    context.localPosition = v_localPosition;
    context.normal = v_normal;
    context.localNormal = v_localNormal;
    context.tangent = vec4(1.0, 0.0, 0.0, 1.0);
    GraphMaterial material = ccEvaluateGraph(context);
#if CC_GRAPH_MASK
    if (material.opacity * v_color0.a < u_graphParams[2].w) discard;
#endif
#if CC_GRAPH_PICK
    float coverage = (u_graphParams[3].w > 1.5 ? material.opacity * v_color0.a : 1.0) * u_instanceOpacity.x;
    if (coverage < 0.01) discard;
    gl_FragColor = u_pickId;
#else
    gl_FragColor = u_surfaceLighting.z > 0.5 ?
        vec4(u_surfaceLighting.y / 255.0, prepassReceiverNormal(v_worldPosition, receiverPlane)) : vec4_splat(0.0);
#endif
}
