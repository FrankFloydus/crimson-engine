#pragma once
#include "math/Math.h"
#include "scene/Scene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <bit>
#include <limits>
#include <stop_token>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
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
struct CollisionDebugEdge {
    std::array<std::uint32_t,2> vertices{};
    Vec3 normalA{},normalB{};
    bool concave{};
    bool planeFallback{};
    std::uint32_t operator[](std::size_t index)const{return vertices[index];}
};
struct CollisionDebugMesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t,3>> triangles;
    std::vector<Vec3> wireVertices;
    std::vector<CollisionDebugEdge> edges;
    // Smooth noncoplanar edges are classified against the view on the GPU.
    // Both edge collections index welded wireVertices; fill stays untouched.
    std::vector<CollisionDebugEdge> silhouetteEdges;
    void finishEdges(std::stop_token stop={});
};
inline void CollisionDebugMesh::finishEdges(std::stop_token stop) {
    edges.clear();silhouetteEdges.clear();wireVertices.clear();
    constexpr auto None=std::numeric_limits<std::uint32_t>::max();
    constexpr double WeldDistance=1e-6,CoplanarCosine=1-1e-6,CreaseCosine=.8660254037844386;
    struct Hash {
        std::size_t operator()(const std::array<std::int64_t,3>& value)const {std::size_t result{};for(const auto x:value)result^=std::hash<std::int64_t>{}(x)+0x9e3779b9+(result<<6)+(result>>2);return result;}
        std::size_t operator()(const std::array<std::uint32_t,3>& value)const {std::size_t result{};for(const auto x:value)result^=std::hash<std::uint32_t>{}(x)+0x9e3779b9+(result<<6)+(result>>2);return result;}
    };
    const auto bits=[](const Vec3& point){return std::array<std::uint32_t,3>{std::bit_cast<std::uint32_t>(point[0]==0?0.f:point[0]),std::bit_cast<std::uint32_t>(point[1]==0?0.f:point[1]),std::bit_cast<std::uint32_t>(point[2]==0?0.f:point[2])};};
    std::unordered_map<std::array<std::uint32_t,3>,std::uint32_t,Hash> exact;
    std::unordered_map<std::array<std::int64_t,3>,std::uint32_t,Hash> cells;
    std::vector<std::uint32_t> welded(vertices.size(),None),next;
    exact.reserve(vertices.size());cells.reserve(vertices.size());next.reserve(vertices.size());wireVertices.reserve(vertices.size());
    for(std::size_t index=0;index<vertices.size();++index) {
        if((index&1023)==0&&stop.stop_requested())throw std::runtime_error("Collision overlay preparation canceled.");
        const auto& point=vertices[index];if(!math::finite(point))continue;
        const auto key=bits(point);if(const auto found=exact.find(key);found!=exact.end()){welded[index]=found->second;continue;}
        std::array<std::int64_t,3> cell{};bool quantized=true;
        for(unsigned axis=0;axis<3;++axis) {
            const auto coordinate=std::floor(double(point[axis])/WeldDistance);
            // Extremely large finite positions still support exact welding.
            if(std::abs(coordinate)>9e18){quantized=false;break;}cell[axis]=static_cast<std::int64_t>(coordinate);
        }
        std::uint32_t match=None;
        if(quantized)for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z) {
            const auto found=cells.find({cell[0]+x,cell[1]+y,cell[2]+z});if(found==cells.end())continue;
            for(auto candidate=found->second;candidate!=None;candidate=next[candidate]) {
                double distance{};for(unsigned axis=0;axis<3;++axis){const auto d=double(point[axis])-wireVertices[candidate][axis];distance+=d*d;}
                if(distance<=WeldDistance*WeldDistance)match=std::min(match,candidate);
            }
        }
        if(match==None) {
            match=static_cast<std::uint32_t>(wireVertices.size());wireVertices.push_back(point);next.push_back(None);
            if(quantized){auto [found,inserted]=cells.try_emplace(cell,match);if(!inserted){next[match]=found->second;found->second=match;}}
        }
        welded[index]=match;exact.emplace(key,match);
    }
    struct Adjacent {Vec3 first,second;double secondSide{};unsigned count{};};
    std::unordered_map<std::uint64_t,Adjacent> adjacency;
    std::unordered_set<std::array<std::uint32_t,3>,Hash> faces;faces.reserve(triangles.size());adjacency.reserve(triangles.size());
    std::size_t processed{};
    for(const auto& triangle:triangles) {
        if((processed++&1023)==0&&stop.stop_requested())throw std::runtime_error("Collision overlay preparation canceled.");
        std::array<std::uint32_t,3> face{};bool valid=true;
        for(unsigned corner=0;corner<3;++corner){if(triangle[corner]>=welded.size()){valid=false;break;}face[corner]=welded[triangle[corner]];if(face[corner]==None)valid=false;}
        if(!valid||face[0]==face[1]||face[1]==face[2]||face[2]==face[0])continue;
        auto sorted=face;std::sort(sorted.begin(),sorted.end());if(!faces.insert(sorted).second)continue;
        const auto& a=wireVertices[face[0]];const auto& b=wireVertices[face[1]];const auto& c=wireVertices[face[2]];
        std::array<double,3> u{},v{},n{};for(unsigned axis=0;axis<3;++axis){u[axis]=double(b[axis])-a[axis];v[axis]=double(c[axis])-a[axis];}
        n={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
        const auto length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);if(!(length>0)||!std::isfinite(length))continue;
        const Vec3 normal{n[0]/length,n[1]/length,n[2]/length};
        for(unsigned corner=0;corner<3;++corner) {
            const auto first=std::min(face[corner],face[(corner+1)%3]),second=std::max(face[corner],face[(corner+1)%3]);
            auto& edge=adjacency[(std::uint64_t(first)<<32)|second];
            if(edge.count==0)edge.first=normal;
            else if(edge.count==1) {
                edge.second=normal;
                const auto& opposite=wireVertices[face[(corner+2)%3]];
                const auto& origin=wireVertices[first];
                for(unsigned axis=0;axis<3;++axis)edge.secondSide+=double(edge.first[axis])*(double(opposite[axis])-origin[axis]);
            }
            ++edge.count;
        }
    }
    for(const auto& [key,edge]:adjacency) {
        if((processed++&1023)==0&&stop.stop_requested())throw std::runtime_error("Collision overlay preparation canceled.");
        const std::array<std::uint32_t,2> endpoints{static_cast<std::uint32_t>(key>>32),static_cast<std::uint32_t>(key)};
        // An open edge has one surface plane. Nonmanifold edges retain the
        // first two planes without assigning a closed-wedge concavity.
        if(edge.count!=2){edges.push_back({endpoints,edge.first,edge.count>1?edge.second:edge.first,false,edge.count>2});continue;}
        const auto cosine=math::dot(edge.first,edge.second);
        if(std::abs(cosine)>=CoplanarCosine)continue;
        const CollisionDebugEdge feature{endpoints,edge.first,edge.second,edge.secondSide>0};
        if(cosine<=CreaseCosine)edges.push_back(feature);
        else silhouetteEdges.push_back(feature);
    }
    std::sort(edges.begin(),edges.end(),[](const auto& a,const auto& b){return a.vertices<b.vertices;});
    std::sort(silhouetteEdges.begin(),silhouetteEdges.end(),[](const auto& a,const auto& b){return a.vertices<b.vertices;});
}
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
        result.wireVertices=result.vertices;
        for(std::uint32_t corner=0;corner<8;++corner)for(std::uint32_t bit=1;bit<=4;bit*=2)if(!(corner&bit)) {
            std::array<Vec3,2> normals{};unsigned plane{};
            for(unsigned axis=0;axis<3;++axis)if(bit!=(1u<<axis))normals[plane++][axis]=corner&(1u<<axis)?1.f:-1.f;
            result.edges.push_back({{corner,corner|bit},normals[0],normals[1]});
        }
        return result;
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
            const auto midpoint=(a+b)*.5f;
            const auto normal=math::normalized(midpoint-Vec3{0,std::clamp(midpoint[1],1.f,2.f),0},Vec3{1,0,0});
            result.edges.push_back({{first,static_cast<std::uint32_t>(first+1)},normal,normal});
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
inline const CollisionDebugMesh& cylinderCollisionMesh() {
    static const auto mesh=[] {
        CollisionDebugMesh result;
        constexpr unsigned radial=32;
        result.vertices={{0,-.5f,0},{0,.5f,0}};
        for(unsigned row=0;row<2;++row)for(unsigned i=0;i<radial;++i) {
            const auto angle=i*2*std::numbers::pi_v<float>/radial;
            result.vertices.emplace_back(std::cos(angle),row?.5f:-.5f,std::sin(angle));
        }
        for(unsigned i=0;i<radial;++i) {
            const auto a=2+i,b=2+(i+1)%radial,c=a+radial,d=b+radial;
            result.triangles.push_back({0,a,b});result.triangles.push_back({1,d,c});
            result.triangles.push_back({a,c,b});result.triangles.push_back({b,c,d});
        }
        const auto line=[&](Vec3 a,Vec3 b) {
            const auto first=static_cast<std::uint32_t>(result.wireVertices.size());
            result.wireVertices.push_back(a);result.wireVertices.push_back(b);
            const auto midpoint=(a+b)*.5f;
            const auto side=math::normalized(Vec3{midpoint[0],0,midpoint[2]},Vec3{1,0,0});
            result.edges.push_back({{first,first+1},side,a[1]==b[1]?Vec3{0,midpoint[1]<0?-1.f:1.f,0}:side});
        };
        constexpr unsigned segments=128;
        for(unsigned i=0;i<segments;++i) {
            const auto angle=i*2*std::numbers::pi_v<float>/segments,next=(i+1)*2*std::numbers::pi_v<float>/segments;
            for(const auto y:{-.5f,.5f})line({std::cos(angle),y,std::sin(angle)},{std::cos(next),y,std::sin(next)});
        }
        for(unsigned i=0;i<4;++i) {
            const auto angle=i*std::numbers::pi_v<float>*.5f;
            line({std::cos(angle),-.5f,std::sin(angle)},{std::cos(angle),.5f,std::sin(angle)});
        }
        return result;
    }();
    return mesh;
}
inline const CollisionDebugMesh& colliderCollisionMesh(const SceneCollider& collider) {
    switch(collider.shape) {
    case ColliderShape::Sphere:return capsuleCollisionMesh(2,1);
    case ColliderShape::Cylinder:return cylinderCollisionMesh();
    case ColliderShape::Capsule:return capsuleCollisionMesh(collider.height,collider.radius);
    default:return boxCollisionMesh(); // Trimesh uses prepared source geometry.
    }
}
inline Vec3 colliderCollisionPoint(const Vec3& point,const SceneCollider& collider) {
    switch(collider.shape) {
    case ColliderShape::Sphere:return capsuleCollisionPoint(point,2*collider.radius,collider.radius)-Vec3{0,collider.radius,0};
    case ColliderShape::Cylinder:return {point[0]*collider.radius,point[1]*collider.height,point[2]*collider.radius};
    case ColliderShape::Capsule:return capsuleCollisionPoint(point,collider.height,collider.radius)-Vec3{0,collider.height*.5f,0};
    default:return {point[0]*collider.size[0],point[1]*collider.size[1],point[2]*collider.size[2]};
    }
}
// Tangent planes are computed in the final, centered primitive coordinates.
// In particular, Capsule's piecewise height mapping is not a linear normal
// transform of its unit prototype. These values are prepared once per size.
inline std::array<Vec3,2> colliderCollisionNormals(const Vec3& a,const Vec3& b,const SceneCollider& collider) {
    const auto midpoint=(a+b)*.5f;
    switch(collider.shape) {
    case ColliderShape::Sphere: {
        const auto normal=math::normalized(midpoint,Vec3{1,0,0},0);return {normal,normal};
    }
    case ColliderShape::Cylinder: {
        const auto side=math::normalized(Vec3{midpoint[0],0,midpoint[2]},Vec3{1,0,0},0);
        return {side,a[1]==b[1]?Vec3{0,midpoint[1]<0?-1.f:1.f,0}:side};
    }
    case ColliderShape::Capsule: {
        const auto stem=std::max(0.f,collider.height*.5f-collider.radius);
        const auto normal=math::normalized(midpoint-Vec3{0,std::clamp(midpoint[1],-stem,stem),0},Vec3{1,0,0},0);
        return {normal,normal};
    }
    default: {
        const auto direction=b-a;unsigned varying{};
        for(unsigned axis=1;axis<3;++axis)if(std::abs(direction[axis])>std::abs(direction[varying]))varying=axis;
        std::array<Vec3,2> normals{};unsigned plane{};
        for(unsigned axis=0;axis<3;++axis)if(axis!=varying)normals[plane++][axis]=midpoint[axis]<0?-1.f:1.f;
        return normals;
    }
    }
}
// Local-space wire geometry shared by Scene and Game overlays.
template<class Line> void boxWireSegments(const Vec3& size,Line&& line) {
    const auto half=size*.5f;
    std::array<Vec3,8> corners;
    for(unsigned i=0;i<8;++i)corners[i]={i&1?half[0]:-half[0],i&2?half[1]:-half[1],i&4?half[2]:-half[2]};
    for(unsigned i=0;i<8;++i)for(unsigned bit=1;bit<=4;bit*=2)if(!(i&bit))line(corners[i],corners[i|bit]);
}
}
