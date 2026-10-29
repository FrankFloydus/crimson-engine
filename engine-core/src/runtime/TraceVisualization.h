#pragma once
#include "physics/PhysicsWorld.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

namespace crimson {
struct TraceVisualizationSettings {
    float duration{.5f};
    float collisionQuadSize{.12f};
    Vec4 clearColor{.1f,1.f,.2f,1.f};
    Vec4 hitColor{1.f,.1f,.1f,1.f};
    float lineWidth{2.5f};
};
struct TraceVisualSegment {
    Vec3 start{},end{};
    bool afterHit{};
};
struct TraceVisualQuad { std::array<Vec3,4> corners{}; };
struct TraceVisualRecord {
    TraceVisualizationSettings settings;
    double remaining{};
    std::vector<TraceVisualSegment> segments;
    std::vector<TraceVisualQuad> contacts;
};
// A bounded, CPU-only snapshot of fired traces. Geometry stays in world space
// while the camera moves; no persistent scene entity or GPU owner is created.
class TraceVisualization {
public:
    static constexpr std::size_t MaximumRecords=128,MaximumContactQuads=256;
    std::span<const TraceVisualRecord> records() const { return records_; }
    void clear() { records_.clear(); }
    void advance(double seconds) {
        if(!std::isfinite(seconds) || seconds<=0)return;
        for(auto& record:records_)record.remaining-=seconds;
        std::erase_if(records_,[](const auto& record){return record.remaining<=0;});
    }
    bool add(const PhysicsTrace& request,const PhysicsTraceResult& result,
             const TraceVisualizationSettings& settings={}) {
        if(!result.valid || !std::isfinite(settings.duration) || settings.duration<=0 ||
           !std::isfinite(settings.collisionQuadSize) || settings.collisionQuadSize<=0 ||
           !std::isfinite(settings.lineWidth) || settings.lineWidth<=0 ||
           !math::finite(settings.clearColor) || !math::finite(settings.hitColor) ||
           !math::finite(request.ray.origin) || !math::finite(request.ray.direction) ||
           !std::isfinite(request.maxDistance) || request.maxDistance<=0)return false;
        if(request.shape!=PhysicsTraceShape::Line) {
            if(!math::finite(request.orientation))return false;
            if(request.shape==PhysicsTraceShape::Box) {
                if(!math::finite(request.halfExtents) || request.halfExtents[0]<=0 || request.halfExtents[1]<=0 || request.halfExtents[2]<=0)return false;
            } else if(request.shape==PhysicsTraceShape::Sphere || request.shape==PhysicsTraceShape::Capsule) {
                if(!std::isfinite(request.radius) || request.radius<=0)return false;
                if(request.shape==PhysicsTraceShape::Capsule && (!std::isfinite(request.capsuleHalfHeight) || request.capsuleHalfHeight<0))return false;
            } else return false;
        }
        const auto magnitude=math::length(request.ray.direction);
        if(!std::isfinite(magnitude) || magnitude<1e-8)return false;
        const auto direction=math::normalized(request.ray.direction);
        const auto start=request.ray.origin,end=start+direction*request.maxDistance;
        if(!math::finite(end))return false;
        TraceVisualRecord record;record.settings=settings;record.remaining=settings.duration;
        float firstDistance=request.maxDistance;bool hit=false;
        for(const auto& contact:result.hits) {
            if(!std::isfinite(contact.distance) || contact.distance<0 || contact.distance>request.maxDistance)continue;
            firstDistance=std::min(firstDistance,contact.distance);hit=true;
            if(!math::finite(contact.position) || !math::finite(contact.normal) || record.contacts.size()>=MaximumContactQuads)continue;
            const auto normal=math::normalized(contact.normal,-direction);
            const auto tangent=math::normalized(math::cross(normal,std::abs(normal[1])<.9f?Vec3{0,1,0}:Vec3{1,0,0}));
            const auto bitangent=math::cross(normal,tangent);
            const auto u=tangent*(settings.collisionQuadSize*.5f),v=bitangent*(settings.collisionQuadSize*.5f);
            record.contacts.push_back({{contact.position-u-v,contact.position+u-v,contact.position+u+v,contact.position-u+v}});
        }
        // A shape sweep's contact is on the target surface; the path split uses
        // its traveled center distance, rather than the off-axis contact point.
        const auto path=[&](Vec3 offset) {
            const auto middle=start+direction*firstDistance+offset;
            if(firstDistance>0)record.segments.push_back({start+offset,middle,false});
            if(hit && firstDistance<request.maxDistance)record.segments.push_back({middle,end+offset,true});
        };
        path({});
        const auto basis=math::rotation(request.orientation);
        const auto local=[&](Vec3 point){return math::transformDirection(basis,point);};
        const auto outline=[&](Vec3 center,bool afterHit) {
            const auto line=[&](Vec3 a,Vec3 b){record.segments.push_back({center+local(a),center+local(b),afterHit});};
            constexpr unsigned subdivisions=24;
            const auto ring=[&](unsigned axis,float radius,float yOffset=0.f) {
                const auto point=[&](unsigned index) {
                    const float angle=index*2*std::numbers::pi_v<float>/subdivisions;
                    Vec3 p{};p[(axis+1)%3]=radius*std::cos(angle);p[(axis+2)%3]=radius*std::sin(angle);p[1]+=yOffset;return p;
                };
                for(unsigned i=0;i<subdivisions;++i)line(point(i),point(i+1));
            };
            if(request.shape==PhysicsTraceShape::Box) {
                for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<3;++axis)if(!(corner&(1u<<axis))) {
                    Vec3 a,b;for(unsigned i=0;i<3;++i){a[i]=(corner&(1u<<i)?1.f:-1.f)*request.halfExtents[i];b[i]=a[i];}
                    b[axis]=-b[axis];line(a,b);
                }
            } else if(request.shape==PhysicsTraceShape::Sphere) {
                for(unsigned axis=0;axis<3;++axis)ring(axis,request.radius);
            } else if(request.shape==PhysicsTraceShape::Capsule) {
                const auto h=request.capsuleHalfHeight,r=request.radius;
                ring(1,r,-h);ring(1,r,h);
                // Two meridian contours retain the capsule's hemispheres and
                // cylinder, without drawing complete intersecting end spheres.
                for(unsigned axis:{0u,2u})for(int sign:{-1,1}) {
                    Vec3 a{},b{};a[axis]=b[axis]=sign*r;a[1]=-h;b[1]=h;line(a,b);
                }
                for(unsigned axis:{0u,2u})for(int sign:{-1,1}) {
                    const auto point=[&](unsigned index) {
                        const float angle=index*std::numbers::pi_v<float>/subdivisions;
                        Vec3 p{};p[axis]=r*std::cos(angle);p[1]=sign*(h+r*std::sin(angle));return p;
                    };
                    for(unsigned i=0;i<subdivisions;++i)line(point(i),point(i+1));
                }
            }
        };
        if(request.shape!=PhysicsTraceShape::Line) {
            outline(start,hit && firstDistance==0);outline(end,hit);
            if(hit && firstDistance>0 && firstDistance<request.maxDistance)outline(start+direction*firstDistance,true);
            // Rails show the full swept length, and use the same green/red
            // distance boundary as the central trajectory.
            if(request.shape==PhysicsTraceShape::Box) {
                for(unsigned corner=0;corner<8;++corner)path(local({(corner&1?1.f:-1.f)*request.halfExtents[0],
                    (corner&2?1.f:-1.f)*request.halfExtents[1],(corner&4?1.f:-1.f)*request.halfExtents[2]}));
            } else {
                for(unsigned axis=0;axis<3;++axis)for(int sign:{-1,1}) {
                    Vec3 p{};p[axis]=sign*(request.radius+(axis==1 && request.shape==PhysicsTraceShape::Capsule?request.capsuleHalfHeight:0));path(local(p));
                }
            }
        }
        if(records_.size()==MaximumRecords)records_.erase(records_.begin());
        records_.push_back(std::move(record));return true;
    }
private:
    std::vector<TraceVisualRecord> records_;
};
}
