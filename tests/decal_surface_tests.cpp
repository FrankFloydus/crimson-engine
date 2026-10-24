#include "decals/DecalSurface.h"
#include "decals/DecalResolve.h"
#include "decal_arch_fixture.h"
#include "decal_polygon_fixture.h"
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <chrono>
namespace {
using namespace crimson;using namespace crimson::decals;
void require(bool ok,const std::string&m){if(!ok)throw std::runtime_error(m);}
SurfaceReceiver quad(uint64_t id,Vec3 a,Vec3 b,Vec3 c,Vec3 d){SurfaceReceiver r;r.id.entity=id;r.generation=73;r.triangles={{0,{a,b,c},{}},{1,{a,c,d},{}}};return r;}
DecalBuildRequest request(uint64_t id=1){DecalBuildRequest r;r.seed={{id,0,0},0,{.5f,0,.5f}};return r;}
DecalPatch build(const DecalSurfaceSnapshot&s,const DecalBuildRequest&r,const char*name){auto out=buildDecalPatch(s,r);std::cout<<name<<": "<<decalPatchStatusName(out.status)<<", "<<out.message<<", triangles="<<out.statistics.analysisTriangles<<"/"<<out.statistics.outputTriangles<<", stretch="<<out.statistics.maximumStretch<<", ms="<<out.statistics.topologyMilliseconds+out.statistics.solveMilliseconds<<'\n';return out;}
void provenance(const DecalSurfaceSnapshot&s,const DecalPatch&p){
    for(const auto&c:p.chunks){const SurfaceReceiver*rec=nullptr;for(const auto&r:s.receivers)if(r.id==c.receiver)rec=&r;require(rec&&rec->generation==c.generation,"Receiver provenance/generation lost");for(const auto&v:c.vertices){const SurfaceTriangle*t=nullptr;for(const auto&tri:rec->triangles)if(tri.triangle==v.sourceTriangle)t=&tri;require(t,"Source triangle lost");Vec3 local{};for(size_t i=0;i<3;++i)for(size_t k=0;k<3;++k)local[k]+=t->positions[i][k]*v.barycentrics[i];for(size_t k=0;k<3;++k){double expected=rec->localToWorld.at(k,3);for(size_t j=0;j<3;++j)expected+=rec->localToWorld.at(k,j)*local[j];require(std::abs(expected-v.position[k])<2e-5,"Output not exactly on its source triangle");}require(v.uv[0]>=0&&v.uv[0]<=1&&v.uv[1]>=0&&v.uv[1]<=1,"UV outside clipped footprint");}}
}
void planeAndAffine(){
    DecalSurfaceSnapshot s{{quad(1,{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0})}};auto r=request();auto p=build(s,r,"coarse plane");require(p.usable(),p.message);require(p.status==DecalPatchStatus::Ready,"Interior plane was incomplete");provenance(s,p);
    for(const auto&c:p.chunks)for(const auto&v:c.vertices){require(std::abs(v.uv[0]-(v.position[0]+.5))<.02,"Plane metre scale X drift");require(std::abs(v.uv[1]-(.5-v.position[1]))<.02,"Plane image V does not run downward");}
    auto repeat=buildDecalPatch(s,r);require(repeat.chunks.size()==p.chunks.size()&&repeat.statistics.outputTriangles==p.statistics.outputTriangles,"Repeated chart is nondeterministic");for(size_t i=0;i<p.chunks[0].vertices.size();++i)require(p.chunks[0].vertices[i].uv==repeat.chunks[0].vertices[i].uv,"Repeat UV bits changed");
    r.frame.at(0,0)=-1;r.frame.at(0,1)=.25f;auto mirror=build(s,r,"mirrored sheared plane");require(mirror.usable(),mirror.message);for(const auto&c:mirror.chunks)for(const auto&v:c.vertices){double y=v.position[1],x=v.position[0];require(std::abs(v.uv[0]-(.5-x+.25*y))<.02,"Mirror/shear applied incorrectly");require(std::abs(v.uv[1]-(.5-y))<.02,"Raster V changed under authored X mirror/shear");}provenance(s,mirror);
    auto&m=s.receivers[0].localToWorld;m.at(0,0)=2;m.at(1,1)=.5f;m.at(2,2)=-1;m.at(0,3)=2;m.at(1,3)=3;m.at(2,3)=1;r=request();r.frame.at(0,3)=2;r.frame.at(1,3)=3;r.frame.at(2,3)=1;auto affine=build(s,r,"affine receiver");require(affine.usable(),affine.message);provenance(s,affine);for(const auto&c:affine.chunks)for(const auto&v:c.vertices)require(v.baseNormal[2]<-.99f,"Mirrored normal transform lost handedness");
}
void imageOrientation(){
    DecalSurfaceSnapshot surface{{quad(1,{-3,-3,0},{3,-3,0},{3,3,0},{-3,3,0})}};
    auto r=request();r.frame=surfacePlacementFrame({0,0,0},{0,0,1})*math::scaling({2,2,1});
    for(const bool mirroredY:{false,true}){
        auto oriented=r;if(mirroredY)oriented.frame.at(1,1)*=-1;
        const auto patch=build(surface,oriented,mirroredY?"authored vertical image mirror":"upright asymmetric image");require(patch.usable(),patch.message);
        bool top=false,bottom=false;
        for(const auto&chunk:patch.chunks)for(const auto&vertex:chunk.vertices){
            const double localY=vertex.position[1]*(mirroredY?-.5:.5);
            require(std::abs(vertex.uv[1]-(.5-localY))<1e-5,"Image V disagrees with the authored vertical axis");
            if(localY>.499){top=true;require(vertex.uv[1]<1e-5,"Top of the decal samples the bottom image row");}
            if(localY<-.499){bottom=true;require(vertex.uv[1]>1-1e-5,"Bottom of the decal samples the top image row");}
        }
        require(top&&bottom,"Orientation proof did not reach both asymmetric image edges");provenance(surface,patch);
    }
}
void translatedPlanePrecision(){
    DecalSurfaceSnapshot s{{quad(1,{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0})}};
    const float angle=.67f;const Mat4 world=math::translation({170,110,1000})*math::eulerRotationXYZ({angle*.33f,angle,angle*.71f});s.receivers[0].localToWorld=world;
    // This interior seed is valid at every size. Rounding the published world
    // vertices before seed-distance/connectivity validation used to reject the
    // first three sizes while accepting the larger footprint on the same plane.
    for(const float size:{.5f,1.f,1.3f,2.f}){
        auto r=request();r.seed.barycentrics={.4f,.1f,.5f};r.frame=world*math::translation({2,0,0})*math::scaling({size,size,1});
        const auto p=build(s,r,"translated rotated plane precision");require(p.status==DecalPatchStatus::Ready,"World-coordinate quantization rejected a valid plane footprint: "+p.message);
        double area=0;for(const auto&chunk:p.chunks)for(size_t i=0;i<chunk.indices.size();i+=3){const auto a=chunk.vertices[chunk.indices[i]].uv,b=chunk.vertices[chunk.indices[i+1]].uv,c=chunk.vertices[chunk.indices[i+2]].uv;area+=.5*std::abs((double(b[0])-a[0])*(double(c[1])-a[1])-(double(b[1])-a[1])*(double(c[0])-a[0]));}
        require(area>.999,"Translated plane lost part of its interior decal footprint.");
    }
}
DecalSurfaceSnapshot corner(float y0=-1,float y1=1,float gap=0){return {{quad(1,{-1,-1,0},{0,-1,0},{0,1,0},{-1,1,0}),quad(2,{gap,y1,0},{gap,y0,0},{gap,y0,1},{gap,y1,1})}};}
double uvArea(const DecalPatch&patch){
    double area=0;for(const auto&chunk:patch.chunks)for(size_t i=0;i<chunk.indices.size();i+=3){const auto a=chunk.vertices[chunk.indices[i]].uv,b=chunk.vertices[chunk.indices[i+1]].uv,c=chunk.vertices[chunk.indices[i+2]].uv;area+=.5*std::abs((double(b[0])-a[0])*(double(c[1])-a[1])-(double(b[1])-a[1])*(double(c[0])-a[0]));}return area;
}
void resizedArch(){
    const auto[boundarySurface,boundaryRequest]=test::bloodDecalArchFixture();
    const auto boundary=build(boundarySurface,boundaryRequest,"resized arch retained vertex fans");
    require(boundary.usable()&&uvArea(boundary)>.1,"Resizing the saved arch decal lost its seed-connected chart: "+boundary.message);provenance(boundarySurface,boundary);
    const auto[surface,baseRequest]=test::bloodDecalCutoffFixture();
    for(const float size:{1.f,1.5f,2.f}){
        auto r=baseRequest;for(size_t axis=0;axis<3;++axis){r.frame.at(axis,0)*=size*.5f;r.frame.at(axis,1)*=size*.5f;}
        const auto patch=build(surface,r,"resized arch thin chart triangles");
        require(patch.usable()&&uvArea(patch)>.999,"A valid arch footprint stopped at a source face edge after resize: "+patch.message);provenance(surface,patch);
    }
}
void localDevelopableFootprint(){
    // The image lies on a cylindrical side. End caps are outside the image but
    // inside the wider analysis halo, so their curvature must not distort it.
    SurfaceReceiver receiver;receiver.id.entity=1;receiver.generation=1;constexpr unsigned sides=32;
    const auto point=[](unsigned i,float y){const double angle=(i==sides?0.:double(i)/sides*2*3.14159265358979323846);return Vec3{.75*std::cos(angle),y,.75*std::sin(angle)};};
    const auto add=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 cap=Vec3{}){SurfaceTriangle t;t.triangle=static_cast<uint32_t>(receiver.triangles.size());t.positions={a,b,c};for(unsigned j=0;j<3;++j)t.normals[j]=math::length(cap)>.5?cap:math::normalized(Vec3{t.positions[j][0],0,t.positions[j][2]});receiver.triangles.push_back(t);};
    for(unsigned i=0;i<sides;++i){add(point(i,2),point(i+1,2),point(i,-2));add(point(i+1,2),point(i+1,-2),point(i,-2));}
    for(unsigned i=0;i<sides;++i){add({0,2,0},point(i+1,2),point(i,2),{0,1,0});add({0,-2,0},point(i,-2),point(i+1,-2),{0,-1,0});}
    receiver.localToWorld=math::translation({3,2,25});DecalSurfaceSnapshot surface{{receiver}};auto r=request();r.seed.barycentrics={.25f,.35f,.4f};
    const auto&t=receiver.triangles[0];Vec3 local{},normal{};for(size_t j=0;j<3;++j){local=local+t.positions[j]*r.seed.barycentrics[j];normal=normal+t.normals[j]*r.seed.barycentrics[j];}
    const auto center=math::transformPoint(receiver.localToWorld,local);r.frame=surfacePlacementFrame(center,normal)*math::scaling({2,2,1});
    const auto patch=build(surface,r,"local cylinder image excludes curved end-cap halo");
    require(patch.usable()&&patch.statistics.usedDevelopableUnfolding&&uvArea(patch)>.999,"Outside-image end-cap curvature changed the local cylindrical footprint: "+patch.message);provenance(surface,patch);
}
DecalBuildRequest cornerRequest(){auto r=request();r.seed.triangle=0;r.seed.barycentrics={.2f,.3f,.5f};r.frame.at(0,3)=-.2f;return r;}
void seams(){
    auto s=corner();auto r=cornerRequest();auto p=build(s,r,"separate mesh 90deg corner");require(p.usable(),p.message);require(p.chunks.size()==2,"90-degree corner did not reach second mesh");provenance(s,p);double worst=0;for(const auto&c:p.chunks)for(const auto&v:c.vertices){const double unrolled=c.receiver.entity==1?v.position[0]:v.position[2];worst=std::max(worst,std::abs(v.uv[0]-(.7+unrolled)));}require(worst<.02,"Corner wrap streaked or changed metre scale");
    auto partial=corner(-.37f,.47f);auto pp=build(partial,r,"partial mismatched edges");require(pp.usable()&&pp.chunks.size()==2,"Partial edge contact was not split/joined");provenance(partial,pp);
    auto gap=corner(-1,1,.01f);auto gp=build(gap,r,"open gap");require(gp.usable()&&gp.chunks.size()==1,"Surface chart jumped an open gap");
    DecalSurfaceSnapshot t{{quad(1,{-1,-1,0},{0,-1,0},{0,1,0},{-1,1,0}),quad(2,{0,-1,0},{1,-1,0},{1,.17f,0},{0,.17f,0}),quad(3,{0,.17f,0},{1,.17f,0},{1,1,0},{0,1,0})}};auto tp=build(t,r,"coplanar T junction");require(tp.usable()&&tp.chunks.size()==3,"Two-sheet T topology did not connect all three separate meshes");provenance(t,tp);
    auto three=corner();three.receivers[0]=quad(1,{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0});r=request();r.seed.triangle=1;r.seed.barycentrics={.5f,.4f,.1f};auto amb=build(three,r,"three-sheet wall T");require(amb.usable()&&amb.status==DecalPatchStatus::Incomplete,"Ambiguous three-sheet contact not diagnosed");require(amb.chunks.size()==1,"Ambiguous contact crossed onto another sheet");
}
void boundedConnectedPreparation(){
    DecalSurfaceSnapshot surface{{quad(1,{-3,-3,0},{3,-3,0},{3,3,0},{-3,3,0})}};SurfaceReceiver nearby;nearby.id.entity=2;nearby.generation=1;
    // A dense independent receiver sits within the extraction box. Its original
    // triangles already exceed the seed sheet's derived budget, but none can
    // join the plane. The budget must not be spent subdividing that receiver.
    constexpr unsigned side=40;for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){const float a=-1.f+x*.05f,b=-1.f+y*.05f;auto cell=quad(2,{a,b,.1f},{a+.05f,b,.1f},{a+.05f,b+.05f,.1f},{a,b+.05f,.1f});for(auto triangle:cell.triangles){triangle.triangle=static_cast<uint32_t>(nearby.triangles.size());nearby.triangles.push_back(triangle);}}
    surface.receivers.push_back(nearby);auto r=request();r.limits.maximumTriangles=2048;
    const auto independent=build(surface,r,"dense disconnected receiver keeps seed budget");require(independent.status==DecalPatchStatus::Ready&&independent.chunks.size()==1&&uvArea(independent)>.999,"Disconnected geometry consumed the seed sheet's preparation budget: "+independent.message);require(independent.statistics.inputTriangles>r.limits.maximumTriangles,"Dense receiver proof did not exceed the derived budget in its broad phase");provenance(surface,independent);
    // Many short interior intersections lie outside the image but inside the
    // analysis halo. Their finite barriers remain real; extending every cut
    // across the coarse wall used to create a quadratic arrangement and fail.
    surface.receivers.resize(1);nearby.triangles.clear();for(unsigned y=0;y<20;++y)for(unsigned x=0;x<20;++x){const float a=.8f+x*.02f,b=-.8f+y*.08f;auto cell=quad(2,{a,b,-.1f},{a,b+.025f,-.1f},{a,b+.025f,.1f},{a,b,.1f});for(auto triangle:cell.triangles){triangle.triangle=static_cast<uint32_t>(nearby.triangles.size());nearby.triangles.push_back(triangle);}}
    surface.receivers.push_back(nearby);r=request();const auto contacts=build(surface,r,"finite contacts beside image stay local");require(contacts.usable()&&contacts.chunks.size()==1&&uvArea(contacts)>.999,"Short neighboring contacts destroyed an interior plane footprint: "+contacts.message);provenance(surface,contacts);
    // A terminating foreign sheet is still a barrier even though its interior
    // cannot join the seed's boundary graph. Do not regain coverage by simply
    // deleting the third branch from topology preparation.
    surface={{quad(1,{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}),quad(2,{0,-.2f,0},{0,.2f,0},{0,.2f,1},{0,-.2f,1})}};r=request();r.seed.triangle=1;r.seed.barycentrics={.5f,.4f,.1f};const auto barrier=build(surface,r,"finite interior branch remains a barrier");require(barrier.usable()&&barrier.status==DecalPatchStatus::Incomplete&&barrier.chunks.size()==1,"Excluded branch lost its finite ambiguity barrier");provenance(surface,barrier);
}
void curved(){
    DecalSurfaceSnapshot s;SurfaceReceiver receiver;receiver.id.entity=1;receiver.generation=1;constexpr int count=24;for(int i=0;i<count;++i){double aa=-1.2+i*.1,bb=aa+.1;auto p=[](double t,float y){return Vec3{std::sin(t),y,1-std::cos(t)};};auto q=quad(1,p(aa,-1),p(bb,-1),p(bb,1),p(aa,1));for(auto&t:q.triangles){t.triangle=static_cast<uint32_t>(receiver.triangles.size());receiver.triangles.push_back(t);}}s.receivers.push_back(receiver);auto r=request();r.seed.triangle=25;r.seed.barycentrics={.5f,0,.5f};r.frame.at(0,0)=1.2f;r.frame.at(1,1)=.8f;auto p=build(s,r,"cylindrical surface");require(p.usable(),p.message);provenance(s,p);for(const auto&c:p.chunks)for(const auto&v:c.vertices){double angle=std::atan2(v.position[0],1-v.position[2]);require(std::abs(v.uv[0]-(.5+angle/1.2))<.025,"Curved wrap did not follow surface distance");}
}
void nondevelopable(){
    DecalSurfaceSnapshot s;SurfaceReceiver receiver;receiver.id.entity=1;receiver.generation=1;
    const auto p=[](double x,double y){return Vec3{x,y,.1*(x*x+y*y)};};
    for(int y=0;y<6;++y)for(int x=0;x<6;++x){double a=-1.5+x*.5,b=-1.5+y*.5;auto q=quad(1,p(a,b),p(a+.5,b),p(a+.5,b+.5),p(a,b+.5));for(auto&t:q.triangles){t.triangle=static_cast<uint32_t>(receiver.triangles.size());receiver.triangles.push_back(t);}}
    s.receivers.push_back(receiver);auto r=request();r.seed.triangle=42;r.seed.barycentrics={1,0,0};r.frame.at(0,0)=.8f;r.frame.at(1,1)=.8f;auto result=build(s,r,"nondevelopable bowl AffineAdaptive");require(result.usable(),result.message);require(!result.statistics.usedDevelopableUnfolding,"Curved bowl incorrectly accepted as developable");provenance(s,result);
}
void closedBuiltinCylinder(){
    // Match the editor's default builtin cylinder, including its separate cap
    // vertices: radius .5, height 1, 32 sectors, smooth side normals.
    DecalSurfaceSnapshot s;SurfaceReceiver receiver;receiver.id.entity=1;receiver.generation=1;
    constexpr unsigned sides=32;const auto point=[](unsigned col,float y){const double phi=(col==sides?0.:double(col)/sides*2*3.14159265358979323846);return Vec3{.5*std::cos(phi),y,.5*std::sin(phi)};};
    const auto add=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 cap=Vec3{}) {SurfaceTriangle t;t.triangle=static_cast<uint32_t>(receiver.triangles.size());t.positions={a,b,c};for(unsigned i=0;i<3;++i)t.normals[i]=math::length(cap)>.5?cap:math::normalized(Vec3{t.positions[i][0],0,t.positions[i][2]});receiver.triangles.push_back(t);};
    for(unsigned i=0;i<sides;++i){add(point(i,.5f),point(i+1,.5f),point(i,-.5f));add(point(i+1,.5f),point(i+1,-.5f),point(i,-.5f));}
    for(const float sign:{1.f,-1.f})for(unsigned i=0;i<sides;++i){const Vec3 center{0,.5f*sign,0};if(sign>0)add(center,point(i+1,.5f),point(i,.5f),{0,1,0});else add(center,point(i,-.5f),point(i+1,-.5f),{0,-1,0});}
    s.receivers.push_back(receiver);for(const float height:{0.f,.0441694f}) {auto r=request();r.seed.triangle=16;r.seed.barycentrics={.25f,.25f+height,.5f-height};
    const auto&t=receiver.triangles[r.seed.triangle];Vec3 center{},normal{};for(unsigned j=0;j<3;++j){center=center+t.positions[j]*r.seed.barycentrics[j];normal=normal+t.normals[j]*r.seed.barycentrics[j];}normal=math::normalized(normal);const auto x=math::normalized(math::cross(Vec3{0,1,0},normal)),y=math::cross(normal,x);
    for(unsigned axis=0;axis<3;++axis){r.frame.at(axis,0)=x[axis];r.frame.at(axis,1)=y[axis];r.frame.at(axis,2)=normal[axis];r.frame.at(axis,3)=center[axis];}
    const auto p=build(s,r,height==0?"closed builtin cylinder default footprint":"closed builtin cylinder offset footprint");require(p.usable(),p.message);provenance(s,p);
    double closest=1,furthest=-1;for(const auto&c:p.chunks)for(const auto&v:c.vertices){const auto distance=math::dot(v.position-center,normal);closest=std::min(closest,distance);furthest=std::max(furthest,distance);}require(furthest-closest>.05f,"Closed cylinder decal stayed on its flat seed tangent plane.");
    double area=0;Vec2 lo{1,1},hi{};for(const auto&chunk:p.chunks)for(size_t i=0;i<chunk.indices.size();i+=3){const auto a=chunk.vertices[chunk.indices[i]].uv,b=chunk.vertices[chunk.indices[i+1]].uv,c=chunk.vertices[chunk.indices[i+2]].uv;area+=std::abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))*.5;for(const auto uv:{a,b,c})for(unsigned axis=0;axis<2;++axis){lo[axis]=std::min(lo[axis],uv[axis]);hi[axis]=std::max(hi[axis],uv[axis]);}}
    std::cout<<"closed cylinder footprint UV coverage="<<area<<", depth extent="<<furthest-closest<<'\n';require(area>.8&&lo[0]<.05f&&hi[0]>.95f&&lo[1]<.05f&&hi[1]>.95f,"Closed cylinder retained only a small seed blob instead of the authored image.");
    if(height==0)for(const auto&[label,edit]:std::array<std::pair<const char*,Mat4>,5>{{
        {"X90",math::eulerRotationXYZ({1.5707963268f,0,0})},
        {"Y90",math::eulerRotationXYZ({0,1.5707963268f,0})},
        {"Z63",math::eulerRotationXYZ({0,0,1.1f})},
        {"nonuniform",math::scaling({.65f,.7f,2})},
        {"mirrored",math::scaling({-.8f,.7f,1})}
    }}){
        auto transformed=r;const auto frame=conformDecalFrame(r.frame*edit,center,normal);require(frame.has_value(),"A valid cylinder transform lost its tangent frame.");transformed.frame=*frame;
        const auto name=std::string("closed builtin cylinder transform ")+label;const auto patch=build(s,transformed,name.c_str());require(patch.usable(),patch.message);provenance(s,patch);
        double uvArea=0;float minDepth=1,maxDepth=-1;for(const auto& chunk:patch.chunks){for(const auto& vertex:chunk.vertices){const auto depth=math::dot(vertex.position-center,normal);minDepth=std::min(minDepth,float(depth));maxDepth=std::max(maxDepth,float(depth));}for(size_t index=0;index<chunk.indices.size();index+=3){const auto a=chunk.vertices[chunk.indices[index]].uv,b=chunk.vertices[chunk.indices[index+1]].uv,c=chunk.vertices[chunk.indices[index+2]].uv;uvArea+=.5*std::abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]));}}
        require(uvArea>.7&&maxDepth-minDepth>.04f,"A transformed cylinder decal published only a seed blob or tangent plane.");
    }
    }
}
void failures(){DecalSurfaceSnapshot s{{quad(1,{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0})}};auto r=request();std::stop_source stop;stop.request_stop();require(buildDecalPatch(s,r,stop.get_token()).status==DecalPatchStatus::Cancelled,"Cancellation ignored");r.limits.maximumTriangles=5;require(buildDecalPatch(s,r).status==DecalPatchStatus::LimitExceeded,"Geometry cap silently truncated");r=request();r.frame.at(0,0)=0;require(buildDecalPatch(s,r).status==DecalPatchStatus::InvalidInput,"Singular frame accepted");r=request();r.seed.receiver.entity=99;require(buildDecalPatch(s,r).status==DecalPatchStatus::MissingSeed,"Missing seed invented surface");}
}
int main(){try{test::runPolygonProofs();planeAndAffine();imageOrientation();translatedPlanePrecision();resizedArch();localDevelopableFootprint();seams();boundedConnectedPreparation();curved();nondevelopable();closedBuiltinCylinder();failures();std::cout<<"All decal CPU surface proofs passed\n";return 0;}catch(const std::exception&e){std::cerr<<"DECAL PROOF FAILED: "<<e.what()<<'\n';return 1;}}
