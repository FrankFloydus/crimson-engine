$input a_position, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_worldPosition, v_normal, v_texcoord0, v_texcoord1, v_tangent, v_color0, v_localPosition, v_localNormal

#include <bgfx_shader.sh>

void main()
{
    float c = cos(i_data2.x);
    float s = sin(i_data2.x);
    vec2 corner = a_position.xy * i_data0.w;
    vec2 rotated = vec2(c * corner.x - s * corner.y, s * corner.x + c * corner.y);
    vec3 right = i_data3.xyz;
    vec3 up = i_data4.xyz;
    vec3 world;
    if (i_data2.y > 0.5 && i_data2.y < 1.5)
    {
        // Retain the existing camera-plane construction for migrated effects.
        vec3 center = mul(u_view, vec4(i_data0.xyz, 1.0)).xyz;
        center.xy += rotated;
        world = mul(u_invView, vec4(center, 1.0)).xyz;
        right = mul(u_invView, vec4(1.0, 0.0, 0.0, 0.0)).xyz;
        up = mul(u_invView, vec4(0.0, 1.0, 0.0, 0.0)).xyz;
        gl_Position = mul(u_proj, vec4(center, 1.0));
    }
    else
    {
        if (i_data2.y > 1.5)
        {
            // Keep world Y upright. At a vertical camera pole, use its right
            // axis to select a stable horizontal facing instead of normalizing zero.
            vec3 facing = mul(u_invView, vec4(0.0, 0.0, 1.0, 0.0)).xyz;
            facing.y = 0.0;
            if (dot(facing, facing) < 0.000001)
            {
                vec3 cameraRight = mul(u_invView, vec4(1.0, 0.0, 0.0, 0.0)).xyz;
                facing = vec3(-cameraRight.z, 0.0, cameraRight.x);
            }
            facing *= inversesqrt(max(dot(facing, facing), 0.000001));
            up = vec3(0.0, 1.0, 0.0);
            right = cross(up, facing);
        }
        world = i_data0.xyz + right * rotated.x + up * rotated.y;
        gl_Position = mul(u_viewProj, vec4(world, 1.0));
    }
    v_worldPosition = world;
    v_normal = normalize(cross(right, up));
    v_tangent = vec4(normalize(right * c + up * s), -1.0);
    v_localPosition = a_position;
    v_localNormal = vec3(0.0, 0.0, 1.0);
    v_texcoord0 = vec2(a_position.x + 0.5, 0.5 - a_position.y);
    v_texcoord1 = v_texcoord0; // Sprite quads have one UV domain; UV1 aliases UV0.
    v_color0 = i_data1; // Exposed exclusively by the Particle Color graph node.
}
