#pragma once
#include "math/Math.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <set>
#include <vector>

namespace crimson {
inline std::optional<std::array<Vec2,2>> projectClippedSegment(const Vec3& start,const Vec3& end,
    const Mat4& viewProjection,const Vec4& viewport,ClipDepth depth=ClipDepth::NegativeOneToOne) {
    if(!math::finite(start) || !math::finite(end) || !math::finite(viewProjection) || !math::finite(viewport) ||
        viewport[2]<=0 || viewport[3]<=0)return std::nullopt;
    const auto a=math::transformHomogeneous(viewProjection,{start[0],start[1],start[2],1});
    const auto b=math::transformHomogeneous(viewProjection,{end[0],end[1],end[2],1});
    if(!math::finite(a) || !math::finite(b))return std::nullopt;
    const auto distances=[&](const Vec4& point) {
        const double x=point[0],y=point[1],z=point[2],w=point[3];
        return std::array<double,6>{w+x,w-x,w+y,w-y,depth==ClipDepth::ZeroToOne?z:w+z,w-z};
    };
    const auto first=distances(a),last=distances(b);
    double minimum=0,maximum=1;
    for(unsigned plane=0;plane<first.size();++plane) {
        if(first[plane]<0 && last[plane]<0)return std::nullopt;
        if(first[plane]>=0 && last[plane]>=0)continue;
        const double crossing=first[plane]/(first[plane]-last[plane]);
        if(first[plane]<0)minimum=std::max(minimum,crossing);
        else maximum=std::min(maximum,crossing);
        if(minimum>maximum)return std::nullopt;
    }
    std::array<Vec2,2> result;
    for(unsigned index=0;index<2;++index) {
        const double t=index?maximum:minimum;
        const double w=a[3]+t*(static_cast<double>(b[3])-a[3]);
        if(w<=1e-12)return std::nullopt;
        const double x=(a[0]+t*(static_cast<double>(b[0])-a[0]))/w;
        const double y=(a[1]+t*(static_cast<double>(b[1])-a[1]))/w;
        result[index]={viewport[0]+(x+1)*.5*viewport[2],viewport[1]+(1-y)*.5*viewport[3]};
        if(!math::finite(result[index]))return std::nullopt;
    }
    return result;
}
struct ProjectedCollisionFace {
    // Clipping a triangle by six planes can add at most six vertices.
    std::array<Vec2,9> vertices{};
    unsigned count{};
};
inline ProjectedCollisionFace projectClippedTriangle(const Vec3& a,const Vec3& b,const Vec3& c,
    const Mat4& viewProjection,const Vec4& viewport,ClipDepth depth=ClipDepth::NegativeOneToOne) {
    if(!math::finite(a) || !math::finite(b) || !math::finite(c) || !math::finite(viewProjection) ||
        !math::finite(viewport) || viewport[2]<=0 || viewport[3]<=0)return {};
    using ClipPoint=std::array<double,4>;
    std::array<ClipPoint,9> polygon{};
    unsigned count=0;
    for(const auto& vertex:{a,b,c}) {
        const auto point=math::transformHomogeneous(viewProjection,{vertex[0],vertex[1],vertex[2],1});
        if(!math::finite(point))return {};
        polygon[count++]={point[0],point[1],point[2],point[3]};
    }
    const auto distance=[&](const ClipPoint& point,unsigned plane) {
        switch(plane) {
        case 0:return point[3]+point[0];case 1:return point[3]-point[0];
        case 2:return point[3]+point[1];case 3:return point[3]-point[1];
        case 4:return depth==ClipDepth::ZeroToOne?point[2]:point[3]+point[2];
        default:return point[3]-point[2];
        }
    };
    for(unsigned plane=0;plane<6 && count;++plane) {
        std::array<ClipPoint,9> clipped{};unsigned nextCount=0;
        auto previous=polygon[count-1];auto previousDistance=distance(previous,plane);
        for(unsigned i=0;i<count;++i) {
            const auto current=polygon[i];const auto currentDistance=distance(current,plane);
            if((previousDistance<0 && currentDistance>0) || (previousDistance>0 && currentDistance<0)) {
                const auto t=previousDistance/(previousDistance-currentDistance);
                ClipPoint crossing;
                for(unsigned axis=0;axis<4;++axis)crossing[axis]=previous[axis]+t*(current[axis]-previous[axis]);
                if(nextCount==clipped.size())return {};
                clipped[nextCount++]=crossing;
            }
            if(currentDistance>=0) {
                if(nextCount==clipped.size())return {};
                clipped[nextCount++]=current;
            }
            previous=current;previousDistance=currentDistance;
        }
        polygon=clipped;count=nextCount;
    }
    ProjectedCollisionFace result;
    if(count<3)return result;
    for(unsigned i=0;i<count;++i) {
        const auto& point=polygon[i];if(point[3]<=1e-12)return {};
        result.vertices[i]={viewport[0]+(point[0]/point[3]+1)*.5*viewport[2],
            viewport[1]+(1-point[1]/point[3])*.5*viewport[3]};
        if(!math::finite(result.vertices[i]))return {};
    }
    result.count=count;return result;
}
// Fill triangles and visible wires are separate surfaces. Primitive capsules
// use contour lines; their fill tessellation must not become a triangle cage.
struct CollisionDebugMesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t,3>> triangles;
    std::vector<Vec3> wireVertices;
    std::vector<std::array<std::uint32_t,2>> edges;
    void finishEdges() {
        std::set<std::array<std::uint32_t,2>> unique;
        for(const auto& triangle:triangles)for(unsigned i=0;i<3;++i) {
            const auto a=triangle[i],b=triangle[(i+1)%3];
            unique.insert({std::min(a,b),std::max(a,b)});
        }
        edges.assign(unique.begin(),unique.end());wireVertices=vertices;
    }
};
inline const CollisionDebugMesh& boxCollisionMesh() {
    static const auto mesh=[] {
        CollisionDebugMesh result;
        for(unsigned i=0;i<8;++i)result.vertices.emplace_back(i&1?.5f:-.5f,i&2?.5f:-.5f,i&4?.5f:-.5f);
        constexpr std::array<std::array<std::uint32_t,4>,6> faces{{
            {0,2,3,1},{4,5,7,6},{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3}}};
        for(const auto& face:faces) {
            result.triangles.push_back({face[0],face[1],face[2]});
            result.triangles.push_back({face[0],face[2],face[3]});
        }
        result.finishEdges();return result;
    }();
    return mesh;
}
inline const CollisionDebugMesh& capsuleCollisionMesh(float height,float radius) {
    const auto build=[](bool sphere) {
        // Same 32 radial segments / 8 intermediate rings per section used by
        // Godot CapsuleShape3D's debug faces. These triangles only draw the fill.
        constexpr unsigned radial=32,bands=9;
        CollisionDebugMesh result;result.vertices.emplace_back(0,0,0);
        for(unsigned row=1;row<3*bands;++row) {
            if(sphere && row>bands && row<=2*bands)continue;
            float ringRadius=1,y=0;
            if(row<=bands) {
                const auto angle=row*std::numbers::pi_v<float>/(2*bands);
                ringRadius=std::sin(angle);y=1-std::cos(angle);
            } else if(row<=2*bands)y=1+static_cast<float>(row-bands)/bands;
            else {
                const auto angle=(row-2*bands)*std::numbers::pi_v<float>/(2*bands);
                ringRadius=std::cos(angle);y=2+std::sin(angle);
            }
            for(unsigned segment=0;segment<radial;++segment) {
                const auto angle=segment*2*std::numbers::pi_v<float>/radial;
                result.vertices.emplace_back(ringRadius*std::cos(angle),y,ringRadius*std::sin(angle));
            }
        }
        const auto top=static_cast<std::uint32_t>(result.vertices.size());result.vertices.emplace_back(0,3,0);
        const auto index=[](unsigned row,unsigned segment) {
            return static_cast<std::uint32_t>(1+row*radial+segment%radial);
        };
        const auto ringCount=static_cast<unsigned>((result.vertices.size()-2)/radial);
        for(unsigned segment=0;segment<radial;++segment) {
            result.triangles.push_back({0,index(0,segment),index(0,segment+1)});
            for(unsigned row=0;row+1<ringCount;++row) {
                const auto a=index(row,segment),b=index(row,segment+1),c=index(row+1,segment),d=index(row+1,segment+1);
                result.triangles.push_back({a,c,b});result.triangles.push_back({b,c,d});
            }
            result.triangles.push_back({index(ringCount-1,segment),top,index(ringCount-1,segment+1)});
        }
        // CollisionShape3DGizmoPlugin draws two equator rings, four straight
        // sides and hemisphere arcs in XY/YZ, at 16 segments per octant.
        constexpr unsigned segments=128;
        const auto circle=[](unsigned i) -> Vec2 {
            i%=segments;
            switch(i) {
            case 0:return {1,0};case segments/4:return {0,1};
            case segments/2:return {-1,0};case 3*segments/4:return {0,-1};
            default: {
                const auto angle=i*2*std::numbers::pi/segments;
                return {std::cos(angle),std::sin(angle)};
            }
            }
        };
        const auto line=[&](Vec3 a,Vec3 b) {
            const auto first=static_cast<std::uint32_t>(result.wireVertices.size());
            result.wireVertices.push_back(a);result.wireVertices.push_back(b);
            result.edges.push_back({first,static_cast<std::uint32_t>(first+1)});
        };
        for(unsigned i=0;i<segments;++i) {
            const auto a=circle(i),b=circle(i+1);
            line({a[0],1,a[1]},{b[0],1,b[1]});
            if(!sphere)line({a[0],2,a[1]},{b[0],2,b[1]});
        }
        for(unsigned i=0;i<segments/2;++i) {
            const auto a=circle(i),b=circle(i+1);
            line({a[0],1-a[1],0},{b[0],1-b[1],0});
            line({a[0],2+a[1],0},{b[0],2+b[1],0});
            line({0,1-a[1],a[0]},{0,1-b[1],b[0]});
            line({0,2+a[1],a[0]},{0,2+b[1],b[0]});
        }
        if(!sphere)for(unsigned i=0;i<4;++i) {
            const auto side=circle(i*segments/4);
            line({side[0],1,side[1]},{side[0],2,side[1]});
        }
        return result;
    };
    static const auto capsule=build(false),sphere=build(true);
    return height==2*radius?sphere:capsule;
}
inline Vec3 capsuleCollisionPoint(const Vec3& point,float height,float radius) {
    const auto y=point[1]<=1 ? point[1]*radius : point[1]>=2 ? height-(3-point[1])*radius :
        radius+(point[1]-1)*(height-2*radius);
    return {point[0]*radius,y,point[2]*radius};
}
// Local-space wire geometry shared by Scene and Game overlays.
template<class Line> void boxWireSegments(const Vec3& size,Line&& line) {
    const auto half=size*.5f;
    std::array<Vec3,8> corners;
    for(unsigned i=0;i<8;++i)corners[i]={i&1?half[0]:-half[0],i&2?half[1]:-half[1],i&4?half[2]:-half[2]};
    for(unsigned i=0;i<8;++i)for(unsigned bit=1;bit<=4;bit*=2)if(!(i&bit))line(corners[i],corners[i|bit]);
}
}
