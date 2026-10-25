#pragma once
#include "decal_intrinsic_source_fixture.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace crimson::decals::test {
struct IntrinsicCoverageReport {
    size_t samples{},archSamples{},roomSamples{};
    double maximumUvError{},archFrontArea{},roomFrontArea{},returnArea{},frontMinimumY{};
};
inline double savedIntrinsicFrontMinimumY(){
    // The original first sixteen triangles are the doorway's inner reveal.
    // Above their highest vertex, the joined arch/front-room surface has no
    // opening. Round upward by less than a millimetre to stay clear of its edge.
    static const double minimum=[] {
        const auto model=intrinsic_source::archModel();const auto world=intrinsic_source::world();
        double highest=-std::numeric_limits<double>::infinity();
        const auto&primitive=model.primitives.front();
        for(size_t index=0;index<16*3;++index){const auto p=primitive.vertices.at(primitive.indices.at(index)).position;
            const double y=world.at(1,3)+double(world.at(1,0))*p[0]+double(world.at(1,1))*p[1]+double(world.at(1,2))*p[2];highest=std::max(highest,y);}
        return std::ceil(highest*1000)/1000;
    }();
    return minimum;
}
inline IntrinsicCoverageReport assertSavedIntrinsicCoverage(const DecalPatch&patch,const DecalBuildRequest&request,uint64_t roomEntity){
    const auto require=[](bool value,const char*message){if(!value)throw std::runtime_error(message);};
    require(patch.usable(),"Saved doorway coverage requires a usable source-validated patch.");
    require(request.seed.receiver.entity==intrinsic_source::ArchEntity,"Saved doorway coverage received a different source placement.");
    constexpr double worldTolerance=2e-6,maximumUvError=.01;
    const auto origin=math::transformPoint(request.frame,{0,0,0});
    const auto fx=math::transformDirection(request.frame,{1,0,0}),fy=math::transformDirection(request.frame,{0,1,0});
    const auto front=math::normalized(math::transformDirection(request.frame,{0,0,1}));
    require(fy[1]>.99&&std::abs(fx[1])<1e-4,"Saved doorway coverage no longer has its proven upright metre frame.");
    struct Triangle {std::array<const DecalPatchVertex*,3> vertices;bool arch;double area;};std::vector<Triangle> triangles;
    IntrinsicCoverageReport report;report.frontMinimumY=savedIntrinsicFrontMinimumY();
    for(const auto&chunk:patch.chunks){
        if(chunk.receiver.entity!=intrinsic_source::ArchEntity&&chunk.receiver.entity!=roomEntity)continue;
        const bool arch=chunk.receiver.entity==intrinsic_source::ArchEntity;
        for(size_t i=0;i+2<chunk.indices.size();i+=3){
            const std::array<const DecalPatchVertex*,3> vertex{&chunk.vertices.at(chunk.indices[i]),&chunk.vertices.at(chunk.indices[i+1]),&chunk.vertices.at(chunk.indices[i+2])};
            const auto cross=math::cross(vertex[1]->position-vertex[0]->position,vertex[2]->position-vertex[0]->position);
            const double twiceArea=math::length(cross);if(twiceArea<=1e-14)continue;
            const double facing=math::dot(cross,front)/twiceArea;
            if(arch&&vertex[0]->sourceTriangle<16&&std::abs(facing)<.1)report.returnArea+=twiceArea*.5;
            if(facing<.99)continue;
            bool onFront=true;for(const auto*v:vertex)onFront&=std::abs(double(v->position[2])-origin[2])<2e-5;
            if(!onFront)continue;
            if(arch&&(vertex[0]->sourceTriangle<40||vertex[0]->sourceTriangle>=80))continue;
            triangles.push_back({vertex,arch,twiceArea*.5});
            (arch?report.archFrontArea:report.roomFrontArea)+=twiceArea*.5;
        }
    }
    constexpr size_t columns=41,rows=25;size_t missing=0,wrong=0;std::string firstFailure;
    for(size_t x=0;x<columns;++x){
        const double u=(double(x)+.5)/columns;
        const double lastV=.5-(report.frontMinimumY-origin[1]-double(fx[1])*(u-.5))/fy[1];
        require(lastV>.5&&lastV<.6,"Saved doorway's source-derived guaranteed rectangle changed unexpectedly.");
        for(size_t y=0;y<rows;++y){
            const double v=(double(y)+.5)/rows*lastV;
            const double px=origin[0]+double(fx[0])*(u-.5)+double(fy[0])*(.5-v);
            const double py=origin[1]+double(fx[1])*(u-.5)+double(fy[1])*(.5-v);
            double best=std::numeric_limits<double>::infinity();bool found=false,arch=false;
            for(const auto&triangle:triangles){const auto&a=triangle.vertices[0]->position;const auto&b=triangle.vertices[1]->position;const auto&c=triangle.vertices[2]->position;
                if(px<std::min({a[0],b[0],c[0]})-worldTolerance||px>std::max({a[0],b[0],c[0]})+worldTolerance||py<std::min({a[1],b[1],c[1]})-worldTolerance||py>std::max({a[1],b[1],c[1]})+worldTolerance)continue;
                const double abx=double(b[0])-a[0],aby=double(b[1])-a[1],acx=double(c[0])-a[0],acy=double(c[1])-a[1],qx=px-a[0],qy=py-a[1];
                const double determinant=abx*acy-aby*acx;if(determinant==0)continue;
                const double sign=determinant>0?1:-1;
                const auto inside=[&](const Vec3&first,const Vec3&last){const double dx=double(last[0])-first[0],dy=double(last[1])-first[1];return sign*(dx*(py-first[1])-dy*(px-first[0]))>=-worldTolerance*std::hypot(dx,dy);};
                if(!inside(a,b)||!inside(b,c)||!inside(c,a))continue;
                const double wb=(qx*acy-qy*acx)/determinant,wc=(abx*qy-aby*qx)/determinant,wa=1-wb-wc;
                const auto&ta=triangle.vertices[0]->uv;const auto&tb=triangle.vertices[1]->uv;const auto&tc=triangle.vertices[2]->uv;
                const double paintedU=wa*ta[0]+wb*tb[0]+wc*tc[0],paintedV=wa*ta[1]+wb*tb[1]+wc*tc[1];
                const double error=std::hypot(paintedU-u,paintedV-v);if(!std::isfinite(error))continue;
                if(error<best){best=error;arch=triangle.arch;}found=true;
            }
            ++report.samples;if(!found)++missing;else if(best>maximumUvError)++wrong;else{report.maximumUvError=std::max(report.maximumUvError,best);if(arch)++report.archSamples;else ++report.roomSamples;}
            if((!found||best>maximumUvError)&&firstFailure.empty()){std::ostringstream message;message.precision(9);message<<" UV("<<u<<','<<v<<") world("<<px<<','<<py<<") error="<<best;firstFailure=message.str();}
        }
    }
    if(missing||wrong){std::ostringstream message;message<<"Saved doorway lost its proven flat front: missing="<<missing<<", wrong metre UV="<<wrong<<" of "<<report.samples<<" samples; first"<<firstFailure;throw std::runtime_error(message.str());}
    require(report.archSamples>5&&report.roomSamples>100,"Saved doorway coverage did not reach both native arch front and room front.");
    require(report.returnArea>.05,"Saved doorway lost its meaningful original inner return/depth surface.");
    return report;
}

inline void runIntrinsicCoverageOracleProofs(){
    const auto request=intrinsic_source::savedRequest();constexpr uint64_t roomEntity=0x6f1f1f91d24b1ed1ull;
    DecalPatch patch;patch.status=DecalPatchStatus::Ready;patch.chunks.resize(2);patch.chunks[0].receiver={roomEntity,0,0};patch.chunks[1].receiver=request.seed.receiver;
    const auto append=[&](size_t chunk,std::array<Vec3,4> positions,std::array<Vec2,4> uv,uint32_t source){auto&out=patch.chunks[chunk];const auto first=static_cast<uint32_t>(out.vertices.size());for(size_t i=0;i<4;++i){DecalPatchVertex vertex;vertex.position=positions[i];vertex.uv=uv[i];vertex.sourceTriangle=source;out.vertices.push_back(vertex);}for(const unsigned i:{0u,2u,1u,0u,3u,2u})out.indices.push_back(first+i);};
    const auto frontRectangle=[&](size_t chunk,double firstV,double lastV){std::array<Vec2,4> uv{Vec2{0,firstV},Vec2{1,firstV},Vec2{1,lastV},Vec2{0,lastV}};std::array<Vec3,4> p;for(size_t i=0;i<4;++i)p[i]=math::transformPoint(request.frame,{uv[i][0]-.5,.5-uv[i][1],0});append(chunk,p,uv,chunk?46:1000);};
    const double splitV=.5-(3-request.frame.at(1,3));frontRectangle(0,0,splitV);frontRectangle(1,splitV,.58);
    const float cx=request.frame.at(0,3),z=request.frame.at(2,3);append(1,{Vec3{cx-.15f,2.8f,z},Vec3{cx+.15f,2.8f,z},Vec3{cx+.15f,2.8f,z+.25f},Vec3{cx-.15f,2.8f,z+.25f}},{Vec2{0,0},Vec2{1,0},Vec2{1,1},Vec2{0,1}},0);
    assertSavedIntrinsicCoverage(patch,request,roomEntity);
    const auto reject=[&](const DecalPatch&bad){bool rejected=false;try{assertSavedIntrinsicCoverage(bad,request,roomEntity);}catch(const std::runtime_error&){rejected=true;}if(!rejected)throw std::runtime_error("The saved doorway coverage oracle accepted missing geometry or shifted UVs.");};
    auto missing=patch;missing.chunks.erase(missing.chunks.begin());reject(missing);
    auto shifted=patch;for(auto&chunk:shifted.chunks)for(auto&vertex:chunk.vertices)vertex.uv[0]+=.025f;reject(shifted);
}
} // namespace crimson::decals::test
