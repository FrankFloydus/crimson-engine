$input v_collision, v_viewPosition, v_planeA, v_planeB
#include <bgfx_shader.sh>
#include "collision_depth.sh"
uniform vec4 u_collisionColor;
uniform vec4 u_collisionDepth;
float windowDepth(vec4 clip)
{
    float depth = clip.z / clip.w;
    return depth * (1.0 - 0.5 * u_collisionDepth.y) + 0.5 * u_collisionDepth.y;
}
float wireFragmentDepth(vec4 fragCoord,vec4 adjacentA,vec4 adjacentB,bool concave,bool planeFallback)
{
    vec2 uv = (fragCoord.xy - u_viewRect.xy) / u_viewRect.zw;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, u_collisionDepth.w > 0.5 ? uv.y * 2.0 - 1.0 : 1.0 - uv.y * 2.0);
    vec3 ray = mul(u_invProj, vec4(ndc, 0.5, 1.0)).xyz;
    ray /= ray.z;
    float dotA = dot(adjacentA.xyz, ray), dotB = dot(adjacentB.xyz, ray);
    float inverseA = dotA / (abs(adjacentA.w) > 1.0e-20 ? adjacentA.w : 1.0);
    float inverseB = dotB / (abs(adjacentB.w) > 1.0e-20 ? adjacentB.w : 1.0);
    float planeA = windowDepth(mul(u_proj, vec4(ray, inverseA)));
    float planeB = windowDepth(mul(u_proj, vec4(ray, inverseB)));
    // Evaluate these actual surface gradients before any discard. The world/
    // view-position interpolant on a 1D line has no valid perpendicular slope.
    float slopeA = abs(dFdx(planeA)) + abs(dFdy(planeA));
    float slopeB = abs(dFdx(planeB)) + abs(dFdy(planeB));
    float rayLength = length(ray);
    bool validA = !planeFallback && collisionPlaneInverseViewDepth(adjacentA.w, dotA, length(adjacentA.xyz), rayLength) < 0.0
        && collisionWirePlaneAccepted(fragCoord.z, planeA, slopeA);
    bool validB = !planeFallback && collisionPlaneInverseViewDepth(adjacentB.w, dotB, length(adjacentB.xyz), rayLength) < 0.0
        && collisionWirePlaneAccepted(fragCoord.z, planeB, slopeB);
    float wireDepth = collisionWireSurfaceDepth(fragCoord.z, planeA, planeB, validA, validB, concave);
    float wireSlope = validA && validB ? (concave ? (planeA < planeB ? slopeA : slopeB) : (planeA > planeB ? slopeA : slopeB))
        : (validA ? slopeA : (validB ? slopeB : 0.0));
    return collisionFragmentDepth(wireDepth, 1.0, validA || validB ? 3.0 : u_collisionDepth.x, 0.0, wireSlope);
}
void main()
{
    float fragmentDepth;
    // This branch is uniform across each draw. Fill pixels do not need wire
    // plane intersections; all derivatives are evaluated before any discard.
    if (u_collisionDepth.z > 0.5) {
        // Normalize before projection: A + B / viewZ avoids the extra rounding
        // of (A * viewZ + B) / viewZ with very small editor near planes.
        vec4 clip = mul(u_proj, vec4(v_viewPosition / v_viewPosition.z, 1.0 / v_viewPosition.z));
        float depth = windowDepth(clip);
        float depthSlope = abs(dFdx(depth)) + abs(dFdy(depth));
        fragmentDepth = collisionFragmentDepth(clip.z, clip.w, u_collisionDepth.x, u_collisionDepth.y, depthSlope);
    } else fragmentDepth = wireFragmentDepth(gl_FragCoord,v_planeA,v_planeB,v_collision.y>0.5,v_collision.z>0.5);
    if (v_collision.x < 0.5 || u_collisionColor.a <= 0.0) discard;
    gl_FragDepth = fragmentDepth;
    // Display-referred color decoded on the CPU, after tone mapping and before
    // the sole final sRGB encoding. Premultiplied alpha, no fog or exposure.
    gl_FragColor = vec4(u_collisionColor.rgb * u_collisionColor.a, u_collisionColor.a);
}
