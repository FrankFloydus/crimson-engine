#ifndef CRIMSON_MODEL_UV_SH
#define CRIMSON_MODEL_UV_SH
// Mode and correction are constant on each triangle. Keep a single return:
// FXC's interactive DX11 path can skip corrections with helper early returns.
bool ccModelUvActive(float mode) { return mode > 0.5; }
vec2 ccModelUv0(vec2 uv, vec4 coordinates, vec4 correction, float mode)
{
    vec2 result = uv;
    if (mode > 0.5 && mode < 1.5)
    {
        float scale = max(abs(coordinates.x), abs(coordinates.y));
        if (scale > 0.0)
        {
            float radius = scale * length(coordinates.xy / scale);
            vec2 delta = vec2(radius - coordinates.z, atan2(coordinates.y, coordinates.x) - coordinates.w);
            vec2 corrected = uv + vec2(dot(correction.xy, delta), dot(correction.zw, delta));
            if (!any(isnan(corrected)) && !any(isinf(corrected))) result = corrected;
        }
    }
    else if (mode > 1.5)
    {
        float residual = coordinates.x * coordinates.y - coordinates.z;
        float linearTerm = 1.0 + correction.y * coordinates.x + correction.x * coordinates.y;
        float discriminant = linearTerm * linearTerm - 4.0 * correction.x * correction.y * residual;
        if (discriminant > 0.0 && !isinf(discriminant))
        {
            float denominator = linearTerm + sqrt(discriminant);
            if (denominator > 0.0 && !isinf(denominator))
            {
                vec2 corrected = uv + correction.zw * (2.0 * residual / denominator);
                if (!any(isnan(corrected)) && !any(isinf(corrected))) result = corrected;
            }
        }
    }
    return result;
}
void ccModelUvFrame(vec3 normal, vec3 position, vec2 finalUv, inout vec3 tangent, inout vec3 bitangent)
{
    vec2 ux = dFdx(finalUv), uy = dFdy(finalUv);
    float uvMagnitude = max(max(abs(ux.x), abs(ux.y)), max(abs(uy.x), abs(uy.y)));
    if (uvMagnitude <= 1e-20 || any(isnan(ux)) || any(isinf(ux)) || any(isnan(uy)) || any(isinf(uy))) return;
    ux /= uvMagnitude;
    uy /= uvMagnitude;
    float determinant = ux.x * uy.y - ux.y * uy.x;
    if (abs(determinant) <= 0.00000001) return;
    vec3 dx = dFdx(position), dy = dFdy(position);
    float orientation = determinant < 0.0 ? -1.0 : 1.0;
    vec3 rawT = (dx * uy.y - dy * ux.y) * orientation;
    vec3 rawB = (dy * ux.x - dx * uy.x) * orientation;
    rawT -= normal * dot(normal, rawT);
    float magnitude = max(max(abs(rawT.x), abs(rawT.y)), abs(rawT.z));
    float bMagnitude = max(max(abs(rawB.x), abs(rawB.y)), abs(rawB.z));
    if (magnitude <= 1e-20 || bMagnitude <= 1e-20 || any(isnan(rawT)) || any(isinf(rawT)) || any(isnan(rawB)) || any(isinf(rawB))) return;
    tangent = normalize(rawT / magnitude);
    vec3 crossB = cross(normal, tangent);
    bitangent = crossB * (dot(crossB, rawB / bMagnitude) < 0.0 ? -1.0 : 1.0);
}
#endif
