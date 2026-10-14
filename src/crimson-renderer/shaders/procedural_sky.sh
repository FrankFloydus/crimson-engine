// Gradient and solar-halo curves adapted from Godot ProceduralSkyMaterial:
// https://github.com/godotengine/godot/blob/master/scene/resources/3d/sky_material.cpp
// Copyright (c) 2014-present Godot Engine contributors (see Godot AUTHORS.md).
// Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Matches procedural::Parameters. RGB is linear; exposure/IBL intensity are
// applied by consumers, and never baked into the shared environment.
uniform vec4 u_proceduralSky[7];
CONST(float PROCEDURAL_PI = 3.14159265358979323846);

float proceduralCircleSegment(float angle)
{
    float square = angle*angle;
    // Avoid cancellation in the small segment of a large filtering footprint.
    return angle < 0.1 ? angle*square*(2.0/3.0-square*(2.0/15.0-square*4.0/315.0))
        : angle-sin(angle)*cos(angle);
}

float proceduralDiskCoverage(float separation, float radius, float footprint)
{
    if (radius <= 0.0) return 0.0;
    if (footprint <= 1e-12) return step(separation, radius);
    float s = sqrt(min(footprint, 4.0 * PROCEDURAL_PI) / PROCEDURAL_PI);
    if (separation >= radius + s) return 0.0;
    if (separation <= abs(radius - s)) return min(1.0, radius * radius / (s * s));
    float d = separation;
    float r = radius;
    float x = clamp(((d-s)*(d+s) + r*r) / (2.0*d*r), -1.0, 1.0);
    float height = r*sqrt(max(0.0, 1.0-x*x));
    float a = acos(x);
    float b = atan2(height, d-r*x);
    return clamp((r*r*proceduralCircleSegment(a) + s*s*proceduralCircleSegment(b)) / (PROCEDURAL_PI*s*s), 0.0, 1.0);
}

vec3 proceduralSkyRadiance(vec3 direction, float footprint)
{
    float y = clamp(direction.y, -1.0, 1.0);
    if (y < 0.0)
        return mix(u_proceduralSky[2].rgb, u_proceduralSky[3].rgb, pow(1.0+y, u_proceduralSky[2].w));
    vec3 sky = mix(u_proceduralSky[0].rgb, u_proceduralSky[1].rgb, pow(1.0-y, u_proceduralSky[0].w));
    if (u_proceduralSky[6].w <= 0.0) return sky;
    float cosine = clamp(dot(u_proceduralSky[4].xyz, direction), -1.0, 1.0);
    // atan2 retains sub-degree disk precision near cosine=1.
    float separation = atan2(length(cross(u_proceduralSky[4].xyz, direction)), cosine);
    float disk = proceduralDiskCoverage(separation, u_proceduralSky[6].y, footprint);
    float denominator = u_proceduralSky[4].w - u_proceduralSky[5].w;
    float halo = denominator > 1e-7
        ? pow(clamp((cosine-u_proceduralSky[5].w)/denominator, 0.0, 1.0), u_proceduralSky[6].x) : 0.0;
    return mix(sky, u_proceduralSky[5].rgb, halo + (1.0-halo)*disk);
}
