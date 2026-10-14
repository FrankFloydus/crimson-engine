#if CC_GRAPH_PICK
uniform vec4 u_pickId;
#endif
void main()
{
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
    if (u_graphParams[3].w > 1.5 && material.opacity * v_color0.a < 0.01) discard;
    gl_FragColor = u_pickId;
#else
    gl_FragColor = vec4_splat(0.0);
#endif
}
