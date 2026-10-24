#pragma once
#include "CollisionWire.h"
#include "runtime/TraceVisualization.h"
#include "crimson-renderer/RenderTypes.h"
#include <imgui.h>

namespace crimson {
// Gameplay diagnostics deliberately draw through scene geometry so the red
// remainder of a blocked trace is visible, independently of lighting/exposure.
inline void drawTraceOverlay(ImDrawList& draw,const Camera& camera,float projectionWidth,float projectionHeight,
                             const Vec4& rectangle,std::span<const TraceVisualRecord> records,
                             ClipDepth depth=ClipDepth::ZeroToOne) {
    if(!std::isfinite(projectionWidth) || !std::isfinite(projectionHeight) || projectionWidth<=0 || projectionHeight<=0 ||
       !math::finite(rectangle) || rectangle[2]<=0 || rectangle[3]<=0 || records.empty())return;
    const auto vp=camera.projectionMatrix(projectionWidth/projectionHeight,depth)*camera.viewMatrix();
    const auto color=[](const Vec4& value,float opacity=1.f){return ImGui::ColorConvertFloat4ToU32({value[0],value[1],value[2],value[3]*opacity});};
    draw.PushClipRect({rectangle[0],rectangle[1]},{rectangle[0]+rectangle[2],rectangle[1]+rectangle[3]},true);
    for(const auto& record:records) {
        for(const auto& segment:record.segments)if(const auto projected=projectClippedSegment(segment.start,segment.end,vp,rectangle,depth)) {
            const ImVec2 a{(*projected)[0][0],(*projected)[0][1]},b{(*projected)[1][0],(*projected)[1][1]};
            const auto tint=color(segment.afterHit?record.settings.hitColor:record.settings.clearColor);
            if(std::hypot(b.x-a.x,b.y-a.y)<.5f)draw.AddCircleFilled(a,record.settings.lineWidth*.5f,tint,8);
            else draw.AddLine(a,b,tint,record.settings.lineWidth);
        }
        for(const auto& contact:record.contacts) {
            for(unsigned triangle=0;triangle<2;++triangle) {
                const auto face=projectClippedTriangle(contact.corners[0],contact.corners[triangle+1],contact.corners[triangle+2],vp,rectangle,depth);
                if(face.count>=3) {
                    std::array<ImVec2,9> points;for(unsigned i=0;i<face.count;++i)points[i]={face.vertices[i][0],face.vertices[i][1]};
                    // ImGui's fill AA fringe assumes clockwise screen winding.
                    // A world-space contact quad is visible from either side.
                    double area{};
                    for(unsigned i=0;i<face.count;++i) {
                        const auto& a=points[i];const auto& b=points[(i+1)%face.count];
                        area+=(double(a.x)-points[0].x)*(double(b.y)-points[0].y)-
                              (double(a.y)-points[0].y)*(double(b.x)-points[0].x);
                    }
                    if(std::abs(area)<1e-6)continue;
                    if(area<0)std::reverse(points.begin(),points.begin()+face.count);
                    draw.AddConvexPolyFilled(points.data(),static_cast<int>(face.count),color(record.settings.hitColor,.28f));
                }
            }
            for(unsigned edge=0;edge<4;++edge)if(const auto line=projectClippedSegment(contact.corners[edge],contact.corners[(edge+1)%4],vp,rectangle,depth)) {
                const ImVec2 a{(*line)[0][0],(*line)[0][1]},b{(*line)[1][0],(*line)[1][1]};
                draw.AddLine(a,b,IM_COL32(0,0,0,230),record.settings.lineWidth+2.f);
                draw.AddLine(a,b,color(record.settings.hitColor),record.settings.lineWidth);
            }
        }
    }
    draw.PopClipRect();
}
}
