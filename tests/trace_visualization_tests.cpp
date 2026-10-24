#include "runtime/TraceVisualization.h"
#include "editor/CollisionWire.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
bool close(Vec3 a,Vec3 b) { return math::length(a-b)<1e-5; }
PhysicsTraceHit contact(float distance,Vec3 position) {
    PhysicsTraceHit result;result.entity=10;result.distance=distance;result.fraction=distance/10;
    result.position=position;result.center={0,0,-distance};result.normal={0,0,1};return result;
}
void lineSplitAndSurfaceQuad() {
    PhysicsTrace request;request.ray={{0,0,0},{0,0,-7}};request.maxDistance=10;
    TraceVisualization visuals;
    const PhysicsTraceResult miss{true,{}};
    require(visuals.add(request,miss),"A valid miss did not create a visualization.");
    require(visuals.records().size()==1,"A miss created the wrong number of records.");
    const auto& missRecord=visuals.records()[0];
    require(missRecord.segments.size()==1 && !missRecord.segments[0].afterHit && close(missRecord.segments[0].start,{}) &&
        close(missRecord.segments[0].end,{0,0,-10}) && missRecord.contacts.empty(),"Miss visualization was not green over the full normalized length.");
    visuals.clear();
    // Unsorted inputs must still split the trajectory at the nearest contact.
    PhysicsTraceResult hits{true,{contact(7,{0,0,-7}),contact(3,{0,0,-3})}};
    TraceVisualizationSettings settings;settings.collisionQuadSize=.4f;
    require(visuals.add(request,hits,settings),"A hit did not create a visualization.");
    const auto& record=visuals.records()[0];
    require(record.segments.size()==2 && !record.segments[0].afterHit && record.segments[1].afterHit &&
        close(record.segments[0].start,{}) && close(record.segments[0].end,{0,0,-3}) &&
        close(record.segments[1].start,{0,0,-3}) && close(record.segments[1].end,{0,0,-10}),
        "Hit changed color before contact or shortened the requested red remainder.");
    require(record.contacts.size()==2,"Multiple contacts lost collision quads.");
    const auto& corners=record.contacts[1].corners;
    Vec3 center;for(const auto& corner:corners){center=center+corner;require(std::abs(corner[2]+3)<1e-5,"Quad left its collision surface plane.");}
    require(close(center*.25f,{0,0,-3}) && std::abs(math::length(corners[1]-corners[0])-.4)<1e-5,
        "Collision quad is not centered at the contact with its authored side length.");
    visuals.clear();hits.hits={contact(0,{})};visuals.add(request,hits);
    require(visuals.records()[0].segments.size()==1 && visuals.records()[0].segments[0].afterHit,
        "Inside start retained a green section before the initial overlap.");
}
void shapesAndContactDistance() {
    for(const auto shape:{PhysicsTraceShape::Sphere,PhysicsTraceShape::Box,PhysicsTraceShape::Capsule}) {
        PhysicsTrace request;request.shape=shape;request.orientation=math::axisAngle({1,0,0},.7);request.ray={{1,2,3},{0,0,-1}};
        const PhysicsTraceResult hits{true,{contact(4,{1.2f,2.1f,-1.25f})}};
        TraceVisualization visuals;require(visuals.add(request,hits),"A shape sweep could not be visualized.");
        const auto& record=visuals.records()[0];require(record.segments.size()>20 && record.contacts.size()==1,"A shape sweep lost its outlines or contact.");
        require(!record.segments[0].afterHit && close(record.segments[0].end,{1,2,-1}) && record.segments[1].afterHit &&
            close(record.segments[1].end,{1,2,-7}),"A shape trace split at surface contact instead of center travel.");
        Vec3 center;for(const auto& p:record.contacts[0].corners)center=center+p;
        require(close(center*.25f,hits.hits[0].position),"A sweep quad used its center instead of the target contact.");
        for(const auto& segment:record.segments)require(math::finite(segment.start) && math::finite(segment.end),"Shape outline contains nonfinite geometry.");
    }
}
void lifetimeAndBounds() {
    PhysicsTrace request;TraceVisualization visuals;const PhysicsTraceResult miss{true,{}};
    TraceVisualizationSettings disabled;disabled.duration=0;
    require(!visuals.add(request,miss,disabled) && visuals.records().empty(),"Zero duration created a persistent drawing.");
    require(!visuals.add(request,{false,{}}),"Invalid query result created a drawing.");
    visuals.add(request,miss);visuals.advance(.49);
    require(visuals.records().size()==1,"Drawing expired before its duration.");
    visuals.advance(.011);require(visuals.records().empty(),"Drawing survived its duration.");
    for(unsigned index=0;index<TraceVisualization::MaximumRecords+5;++index) {
        request.ray.origin[0]=static_cast<float>(index);visuals.add(request,miss);
    }
    require(visuals.records().size()==TraceVisualization::MaximumRecords && visuals.records()[0].segments[0].start[0]==5,
        "Repeated traces did not bound their lifetime store by evicting oldest records.");
    visuals.clear();request.ray.direction={};require(!visuals.add(request,miss),"Zero direction created invalid geometry.");
    request.ray.direction={0,0,-1};request.maxDistance=std::numeric_limits<float>::infinity();
    require(!visuals.add(request,miss),"Nonfinite distance created invalid geometry.");
}
void cameraPlaneClipping() {
    Camera camera;camera.position={0,0,0};camera.target={0,0,-1};camera.nearPlane=.05f;camera.farPlane=100;
    const Vec4 viewport{20,30,960,600};
    for(const auto depth:{ClipDepth::ZeroToOne,ClipDepth::NegativeOneToOne}) {
        const auto vp=camera.projectionMatrix(1.6f,depth)*camera.viewMatrix();
        const auto clipped=projectClippedSegment({0,0,0},{0,0,-10},vp,viewport,depth);
        require(clipped && math::finite((*clipped)[0]) && math::finite((*clipped)[1]),"Camera-origin ray was lost at the near plane.");
        require(!projectClippedSegment({0,0,1},{0,0,10},vp,viewport,depth),"Behind-camera trace was projected into the viewport.");
    }
}
}
int main() {
    try {lineSplitAndSurfaceQuad();shapesAndContactDistance();lifetimeAndBounds();cameraPlaneClipping();
        std::cout<<"Trace visualization split, quads, shapes, clipping and lifetime checks passed.\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
