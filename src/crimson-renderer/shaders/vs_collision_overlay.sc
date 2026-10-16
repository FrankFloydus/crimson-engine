$input a_position, a_texcoord0, a_texcoord1, a_texcoord2
$output v_collision, v_viewPosition, v_planeA, v_planeB
#include <bgfx_shader.sh>
uniform vec4 u_collisionLocalEye;
uniform mat4 u_collisionNormalView;
void main()
{
    vec4 world = mul(u_model[0], vec4(a_position, 1.0));
    v_viewPosition = mul(u_view, world).xyz;
    // Keep clipping geometric. Reconstruct and bias fragment depth after the
    // perspective interpolation, including triangles that cross the near plane.
    gl_Position = mul(u_viewProj, world);
    // Both endpoints carry the same adjacent normals and local midpoint. Local
    // eye displacement gives the affine-correct facing signs without averaging
    // nonuniform scale or applying a direction transform to a surface normal.
    vec3 towardEye = u_collisionLocalEye.xyz - a_texcoord2.xyz;
    float a = dot(a_texcoord0, towardEye), b = dot(a_texcoord1, towardEye);
    float fallback = floor(a_texcoord2.w * 0.25);
    float flags = a_texcoord2.w - 4.0 * fallback;
    float concave = floor(flags * 0.5);
    float silhouette = flags - 2.0 * concave;
    v_collision = vec4(silhouette < 0.5 || a * b <= 0.0 ? 1.0 : 0.0, concave, fallback, 0.0);
    vec3 normalA = mul(u_collisionNormalView, vec4(a_texcoord0, 0.0)).xyz;
    vec3 normalB = mul(u_collisionNormalView, vec4(a_texcoord1, 0.0)).xyz;
    float lengthA = length(normalA), lengthB = length(normalB);
    normalA = lengthA > 1.0e-15 ? normalA / lengthA : vec3_splat(0.0);
    normalB = lengthB > 1.0e-15 ? normalB / lengthB : vec3_splat(0.0);
    vec3 midpoint = mul(u_view, mul(u_model[0], vec4(a_texcoord2.xyz, 1.0))).xyz;
    v_planeA = vec4(normalA, dot(normalA, midpoint));
    v_planeB = vec4(normalB, dot(normalB, midpoint));
}
