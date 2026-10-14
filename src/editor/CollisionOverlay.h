#pragma once
#include "CollisionWire.h"
#include <imgui.h>

namespace crimson {
// Display-referred overlays share the same clipped shape geometry in Scene/Game.
// As in Godot's collision debug material, fill opacity is 1/16 of wire opacity.
template<class LocalPoint,class Line> void drawCollisionOverlay(ImDrawList& draw,const CollisionDebugMesh& mesh,
    LocalPoint&& localPoint,const Mat4& world,const Mat4& viewProjection,const Vec4& viewport,
    const Vec4& color,Line&& line,ClipDepth depth=ClipDepth::NegativeOneToOne) {
    if(color[3]<=0)return;
    std::vector<Vec3> vertices;vertices.reserve(mesh.vertices.size());
    for(const auto& vertex:mesh.vertices)vertices.push_back(math::transformPoint(world,localPoint(vertex)));
    const auto fill=ImGui::ColorConvertFloat4ToU32({color[0],color[1],color[2],color[3]*.0625f});
    const auto flags=draw.Flags;
    // Adjacent triangles must share an exact edge, without independent AA fringes.
    draw.Flags&=~ImDrawListFlags_AntiAliasedFill;
    const auto handedness=math::handedness(world);
    for(const auto& indices:mesh.triangles) {
        const auto face=projectClippedTriangle(vertices[indices[0]],vertices[indices[1]],vertices[indices[2]],viewProjection,viewport,depth);
        if(face.count<3)continue;
        double area=0;
        std::array<ImVec2,9> points;
        for(unsigned i=0;i<face.count;++i) {
            const auto& a=face.vertices[i];const auto& b=face.vertices[(i+1)%face.count];
            area+=static_cast<double>(a[0])*b[1]-static_cast<double>(b[0])*a[1];
            points[i]={a[0],a[1]};
        }
        if(area*handedness<0) {
            // ImGui expects clockwise contours in its downward-Y screen space.
            if(area<0)std::reverse(points.begin(),points.begin()+face.count);
            draw.AddConvexPolyFilled(points.data(),static_cast<int>(face.count),fill);
        }
    }
    draw.Flags=flags;
    const auto wire=ImGui::ColorConvertFloat4ToU32({color[0],color[1],color[2],color[3]});
    vertices.clear();vertices.reserve(mesh.wireVertices.size());
    for(const auto& vertex:mesh.wireVertices)vertices.push_back(math::transformPoint(world,localPoint(vertex)));
    for(const auto& edge:mesh.edges)if(const auto segment=projectClippedSegment(vertices[edge[0]],vertices[edge[1]],viewProjection,viewport,depth)) {
        const ImVec2 start{(*segment)[0][0],(*segment)[0][1]},end{(*segment)[1][0],(*segment)[1][1]};
        draw.AddLine(start,end,wire,1.f);line(start,end);
    }
}
}
