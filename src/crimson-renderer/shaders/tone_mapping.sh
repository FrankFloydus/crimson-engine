// ACES and AgX adapted from Godot Engine's tonemap.glsl and
// environment_storage.cpp (local 4.7 development source).
// https://github.com/godotengine/godot/blob/master/servers/rendering/renderer_rd/shaders/effects/tonemap.glsl
// ACES fit by Stephen Hill, via MJP / David Neubelt's Baking Lab:
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// AgX inset/outset derived from EaryChow's Blender AgX LUT generator:
// https://github.com/EaryChow/AgX_LUT_Gen/blob/main/AgXBasesRGB.py
// AgX curve: https://allenwp.com/blog/2025/05/29/allenwp-tonemapping-curve/
//
// Copyright (c) 2014-present Godot Engine contributors (see Godot AUTHORS.md).
// Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.
// Copyright (c) 2016 MJP.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Every curve returns display-linear Rec.709. fs_encode.sc performs the single
// sRGB encoding after compositing the display-referred editor matte and grid.
// Coefficients are prepared from the authored controls by ToneMapping.cpp.
uniform vec4 u_toneCurve; // Reinhard inverse white squared, ACES denominator, AgX contrast/toe A.
uniform vec4 u_toneShoulder; // AgX slope/W, reserved ZW.

vec3 toneAces(vec3 color)
{
    color *= 1.8;
    color = vec3(dot(color,vec3(.59719,.35458,.04823)),
                 dot(color,vec3(.076,.90834,.01566)),
                 dot(color,vec3(.0284,.13383,.83777)));
    color = (color*(color+.0245786)-.000090537) /
            (color*(.983729*color+.432951)+.238081);
    color = vec3(dot(color,vec3(1.60475,-.53108,-.07367)),
                 dot(color,vec3(-.10208,1.10813,-.00605)),
                 dot(color,vec3(-.00327,-.07276,1.07602)));
    // The fitted curve undershoots zero slightly. Keep uncovered pixels black
    // so an empty HDR target cannot darken the independent editor matte.
    return max(vec3_splat(0.0),color / u_toneCurve.y);
}

vec3 toneAgx(vec3 color)
{
    color = vec3(dot(color,vec3(.544814746488245,.373787398372697,.0813978551390581)),
                 dot(color,vec3(.140416948464053,.754137554567394,.105445496968552)),
                 dot(color,vec3(.0888104196149096,.178871756420858,.732317823964232)));
    // allenwp curve with Godot's SDR white and contrast coefficients. This
    // newer AgX formulation is display-linear: do not apply the older
    // log-polynomial AgX EOTF here.
    vec3 shoulder=color-.18;
    shoulder=.18+(u_toneShoulder.x*shoulder)*(1.0+shoulder/u_toneShoulder.y)/(1.0+u_toneShoulder.x*shoulder/.82);
    vec3 toe=pow(color,vec3_splat(u_toneCurve.z));
    toe=toe/(toe+u_toneCurve.w);
    color=min(vec3_splat(1.0),mix(toe,shoulder,step(vec3_splat(.18),color)));
    return vec3(dot(color,vec3(1.96488741169489,-.855988495690215,-.108898916004672)),
                dot(color,vec3(-.299313364904742,1.32639796461980,-.0270845997150571)),
                dot(color,vec3(-.164352742528393,-.238183969428088,1.40253671195648)));
}

vec3 toneMap(vec3 color,float mode)
{
    if (mode>2.5) return toneAgx(color);
    if (mode>1.5) return toneAces(color);
    if (mode>.5) return color;
    if (u_toneCurve.x>0.0) return color*(vec3_splat(1.0)+color*u_toneCurve.x)/(vec3_splat(1.0)+color);
    return vec3_splat(1.0)-vec3_splat(1.0)/(vec3_splat(1.0)+color);
}
