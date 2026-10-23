#include "DecalInternal.h"
#include <geometrycentral/surface/common_subdivision.h>
#include <geometrycentral/surface/integer_coordinates_intrinsic_triangulation.h>
#include <geometrycentral/surface/vector_heat_method.h>
#include <chrono>
#include <map>
#include <queue>
#include <tuple>

namespace crimson::decals::detail {
namespace {
struct D2 {double x{},y{};};
D2 operator+(D2 a,D2 b){return {a.x+b.x,a.y+b.y};}
D2 operator-(D2 a,D2 b){return {a.x-b.x,a.y-b.y};}
D2 operator*(D2 a,double b){return {a.x*b,a.y*b};}
double cross2(D2 a,D2 b){return a.x*b.y-a.y*b.x;}
struct Point {D3 p;D2 uv;};
bool intersectsFootprintInterior(D2 a,D2 b) {
    double first=0,last=1;
    for(size_t axis=0;axis<2;++axis) {
        const double origin=axis==0?a.x:a.y,delta=(axis==0?b.x:b.y)-origin;
        if(std::abs(delta)<1e-15){if(origin<=1e-5||origin>=1-1e-5)return false;continue;}
        double lo=(1e-5-origin)/delta,hi=(1-1e-5-origin)/delta;if(lo>hi)std::swap(lo,hi);
        first=std::max(first,lo);last=std::min(last,hi);if(first>last)return false;
    }
    return true;
}
std::vector<Point> clipUv(const std::vector<Point>&poly,size_t axis,double boundary,bool upper){
    std::vector<Point>out;const auto dist=[&](const Point&p){return ((axis==0?p.uv.x:p.uv.y)-boundary)*(upper?1:-1);};
    for(size_t i=0;i<poly.size();++i){const auto&a=poly[i];const auto&b=poly[(i+1)%poly.size()];const double da=dist(a),db=dist(b);if(da<=1e-12)out.push_back(a);if(da*db< -1e-24){const double t=da/(da-db);out.push_back({a.p+(b.p-a.p)*t,a.uv+(b.uv-a.uv)*t});}}
    return out;
}
double stretch(D3 a,D3 b,D2 u,D2 v){
    const double x=length(a),xx=dot(a,b)/x,yy=length(cross(a,b))/x;
    if(x<1e-12||yy<1e-12)return INFINITY;
    const D2 c=u*(1/x),d=(v-c*xx)*(1/yy);
    const double aa=c.x*c.x+c.y*c.y,ab=c.x*d.x+c.y*d.y,bb=d.x*d.x+d.y*d.y;
    const double disc=std::sqrt(std::max(0.,(aa-bb)*(aa-bb)+4*ab*ab));
    const double hi=std::sqrt(std::max(0.,.5*(aa+bb+disc))),lo=std::sqrt(std::max(0.,.5*(aa+bb-disc)));
    return lo>1e-10?std::max({hi,1/lo,hi/lo}):INFINITY;
}
struct UvTriangle {std::array<D2,3>p;};
double overlapArea(const UvTriangle&a,const UvTriangle&b){
    std::vector<D2>poly{a.p[0],a.p[1],a.p[2]};const double sign=cross2(b.p[1]-b.p[0],b.p[2]-b.p[0])>0?1:-1;
    for(size_t j=0;j<3&&!poly.empty();++j){std::vector<D2>next;D2 e=b.p[(j+1)%3]-b.p[j];const auto dist=[&](D2 p){return cross2(e,p-b.p[j])*sign;};for(size_t i=0;i<poly.size();++i){D2 x=poly[i],y=poly[(i+1)%poly.size()];double dx=dist(x),dy=dist(y);if(dx>=-1e-12)next.push_back(x);if(dx*dy< -1e-24)next.push_back(x+(y-x)*(dx/(dx-dy)));}poly=std::move(next);}
    double area=0;for(size_t i=0;i<poly.size();++i)area+=cross2(poly[i],poly[(i+1)%poly.size()]);return std::abs(area)*.5;
}
double triangleDistance(D3 point,const std::array<D3,3>&p){
    Source s;s.p=p;const D3 n=unit(cross(p[1]-p[0],p[2]-p[0]));const double h=dot(point-p[0],n);D3 projected=point-n*h,b=barycentric(s,projected);
    if(std::min({b.x,b.y,b.z})>=0)return std::abs(h);
    double best=INFINITY;for(size_t i=0;i<3;++i){D3 e=p[(i+1)%3]-p[i];double t=std::clamp(dot(point-p[i],e)/dot(e,e),0.,1.);best=std::min(best,length(point-p[i]-e*t));}return best;
}
// A developable sheet has a path-independent Euclidean development. Compute it
// by unfolding each neighboring triangle and verify every cycle; curvature
// makes those checks fail and selects the intrinsic log-map solve instead.
bool developableChart(const AnalysisPatch&a,std::vector<D2>&chart){
    struct Use{size_t face,edge;};
    std::map<std::pair<size_t,size_t>,std::vector<Use>>edges;
    const size_t first=a.seedFace;
    for(size_t i=0;i<a.faces.size();++i)for(size_t j=0;j<3;++j){const auto&v=a.faces[i].v;edges[std::minmax(v[j],v[(j+1)%3])].push_back({i,j});}
    chart.resize(a.positions.size());std::vector<bool>assigned(a.positions.size()),visited(a.faces.size());
    for(size_t v:a.faces[first].v){D3 p=a.positions[v]-a.seedPosition;chart[v]={dot(p,a.basisX),dot(p,a.basisY)};assigned[v]=true;}
    std::queue<size_t>queue;queue.push(first);visited[first]=true;
    const double tolerance=std::max(a.scale*1e-6,a.tolerance*2);
    while(!queue.empty()){
        const size_t f=queue.front();queue.pop();const auto&v=a.faces[f].v;
        for(size_t j=0;j<3;++j){const size_t va=v[j],vb=v[(j+1)%3],vc=v[(j+2)%3];const auto&uses=edges[std::minmax(va,vb)];if(uses.size()!=2)continue;
            const Use next=uses[0].face==f?uses[1]:uses[0];const auto&nv=a.faces[next.face].v;const size_t vd=nv[(next.edge+2)%3];
            D2 delta=chart[vb]-chart[va];const double l=std::sqrt(delta.x*delta.x+delta.y*delta.y);if(l<1e-12)return false;
            const double ad=length(a.positions[vd]-a.positions[va]),bd=length(a.positions[vd]-a.positions[vb]);const double x=(ad*ad-bd*bd+l*l)/(2*l),y2=ad*ad-x*x;
            if(y2< -tolerance*tolerance)return false;
            const double sign=cross2(delta,chart[vc]-chart[va])>0?-1:1;const double y=std::sqrt(std::max(0.,y2))*sign;
            const D2 proposed=chart[va]+D2{delta.x/l,delta.y/l}*x+D2{-delta.y/l,delta.x/l}*y;
            if(assigned[vd]){D2 error=chart[vd]-proposed;if(std::sqrt(error.x*error.x+error.y*error.y)>tolerance)return false;}
            else{chart[vd]=proposed;assigned[vd]=true;}
            if(!visited[next.face]){visited[next.face]=true;queue.push(next.face);}
        }
    }
    return std::all_of(assigned.begin(),assigned.end(),[](bool v){return v;});
}
}

void buildChart(const DecalSurfaceSnapshot&snapshot,const DecalBuildRequest&r,const AnalysisPatch&a,DecalPatch&out,std::stop_token stop){
    namespace gc=geometrycentral;namespace gs=geometrycentral::surface;
    const auto started=std::chrono::steady_clock::now();cancelled(stop);
    std::vector<std::vector<size_t>>polygons;polygons.reserve(a.faces.size());for(const auto&f:a.faces)polygons.push_back({f.v[0],f.v[1],f.v[2]});
    gs::ManifoldSurfaceMesh mesh(polygons);gs::VertexData<gc::Vector3> positions(mesh);
    for(gs::Vertex v:mesh.vertices()){const D3 p=a.positions[v.getIndex()];positions[v]={p.x,p.y,p.z};}
    struct ChartTriangle{size_t sourceFace;std::array<D3,3>positions;std::array<D2,3>metres;};
    std::vector<ChartTriangle> triangles;std::vector<D2>inputChart;
    out.statistics.usedDevelopableUnfolding=developableChart(a,inputChart);
    if(out.statistics.usedDevelopableUnfolding){for(size_t i=0;i<a.faces.size();++i){ChartTriangle t{};t.sourceFace=i;for(size_t j=0;j<3;++j){size_t v=a.faces[i].v[j];t.positions[j]=a.positions[v];t.metres[j]=inputChart[v];}triangles.push_back(t);}}
    else{
    gs::VertexPositionGeometry geometry(mesh,positions);gs::IntegerCoordinatesIntrinsicTriangulation intrinsic(mesh,geometry,0.0);intrinsic.flipToDelaunay();cancelled(stop);
    gs::Vertex seed=intrinsic.intrinsicMesh->vertex(a.seedVertex);gs::VertexData<gc::Vector2> chart(*intrinsic.intrinsicMesh);
    gs::VectorHeatMethodSolver solver(intrinsic);chart=solver.computeLogMap(seed,gs::LogMapStrategy::AffineAdaptive);cancelled(stop);
    // The intrinsic vertex tangent x-axis follows its current halfedge. Trace it
    // onto the input surface before aligning to the authored world tangent.
    D3 direction{};const auto path=intrinsic.traceIntrinsicHalfedgeAlongInput(seed.halfedge());
    for(const auto&p:path){const auto q=p.interpolate(positions);D3 d{q.x-a.seedPosition.x,q.y-a.seedPosition.y,q.z-a.seedPosition.z};d=d-a.seedNormal*dot(d,a.seedNormal);if(length(d)>1e-9){direction=unit(d);break;}}
    if(length(direction)<.5)throw Failure(DecalPatchStatus::InvalidChart,"Intrinsic seed tangent could not be aligned.");
    intrinsic.requireHalfedgeVectorsInVertex();const auto h=intrinsic.halfedgeVectorsInVertex[seed.halfedge()].normalize();
    const double dx=dot(direction,a.basisX),dy=dot(direction,a.basisY),rx=dx*h.x+dy*h.y,ry=dy*h.x-dx*h.y;const gc::Vector2 origin=chart[seed];
    for(gs::Vertex v:intrinsic.intrinsicMesh->vertices()){gc::Vector2 p=chart[v]-origin;chart[v]={rx*p.x-ry*p.y,ry*p.x+rx*p.y};}
    auto&common=intrinsic.getCommonSubdivision();common.constructMesh(true);cancelled(stop);
    if(common.mesh->nFaces()>static_cast<size_t>(r.limits.maximumTriangles)*4)throw Failure(DecalPatchStatus::LimitExceeded,"Intrinsic common-subdivision budget exceeded.");
    auto commonChart=common.interpolateAcrossB(chart);auto commonPosition=common.interpolateAcrossA(positions);
    auto restricted=intrinsic.restrictToInput(chart);inputChart.resize(a.positions.size());for(gs::Vertex v:mesh.vertices())inputChart[v.getIndex()]={restricted[v].x,restricted[v].y};
    for(gs::Face f:common.mesh->faces()){ChartTriangle t{};t.sourceFace=common.sourceFaceA[f].getIndex();size_t i=0;for(gs::Vertex v:f.adjacentVertices()){if(i>=3)throw Failure(DecalPatchStatus::InvalidChart,"Common subdivision did not triangulate.");auto p=commonPosition[v];auto c=commonChart[v];t.positions[i]={p.x,p.y,p.z};t.metres[i++]={c.x,c.y};}if(i==3)triangles.push_back(t);}
    }
    const auto uv=[&](D2 p){return D2{.5+(a.by*p.x-a.bx*p.y)/a.frameDet,.5+(-a.ay*p.x+a.ax*p.y)/a.frameDet};};
    struct OutputRef{size_t chunk,index;};
    std::map<size_t,size_t>chunks;std::vector<UvTriangle>uvTriangles;std::vector<OutputRef>outputRefs;
    bool incomplete=a.incomplete;
    // Surface boundaries inside the intended image mean a deliberately clipped footprint.
    for(gs::Edge e:mesh.edges())if(e.isBoundary()){
        auto he=e.halfedge();D2 x=uv(inputChart[he.tailVertex().getIndex()]),y=uv(inputChart[he.tipVertex().getIndex()]);
        if(intersectsFootprintInterior(x,y))incomplete=true;
    }
    for(const ChartTriangle&f:triangles){
        cancelled(stop);const size_t sourceFace=f.sourceFace;if(sourceFace>=a.faces.size())throw Failure(DecalPatchStatus::InvalidChart,"Common subdivision lost source provenance.");
        // The working halo is a numerical boundary condition, never a donor
        // surface for a map whose distant heat quotient has lost precision.
        const double support=.5*std::max(std::hypot(a.ax+a.bx,a.ay+a.by),std::hypot(a.ax-a.bx,a.ay-a.by))*r.limits.maximumStretch;
        if(triangleDistance(a.seedPosition,f.positions)>support)continue;
        const Source&s=a.sources[a.faces[sourceFace].source];const auto&receiver=snapshot.receivers[s.receiver];const auto&source=receiver.triangles[s.triangle];
        std::vector<Point>poly;const auto&metres=f.metres;
        for(size_t i=0;i<3;++i){D2 c=metres[i];if(!std::isfinite(c.x)||!std::isfinite(c.y))throw Failure(DecalPatchStatus::InvalidChart,"Chart coordinates are nonfinite.");poly.push_back({f.positions[i],uv(c)});}
        const auto original=poly;
        for(size_t axis=0;axis<2;++axis){poly=clipUv(poly,axis,0,false);if(poly.empty())break;poly=clipUv(poly,axis,1,true);if(poly.empty())break;}
        if(poly.size()<3)continue;
        const double chartArea=cross2(metres[1]-metres[0],metres[2]-metres[0]);const double surfaceParity=dot(cross(original[1].p-original[0].p,original[2].p-original[0].p),s.geometricNormal);
        if(std::abs(surfaceParity)<a.scale*a.scale*1e-13)continue;
        if(chartArea*surfaceParity<=0){incomplete=true;continue;}
        const double distortion=stretch(original[1].p-original[0].p,original[2].p-original[0].p,metres[1]-metres[0],metres[2]-metres[0]);
        if(!std::isfinite(distortion)||distortion>r.limits.maximumStretch+1e-6){incomplete=true;continue;}
        out.statistics.maximumStretch=std::max(out.statistics.maximumStretch,distortion);
        auto[it,inserted]=chunks.try_emplace(s.receiver,out.chunks.size());if(inserted)out.chunks.push_back({receiver.id,receiver.generation,{},{}});auto&chunk=out.chunks[it->second];
        for(size_t j=1;j+1<poly.size();++j){std::array<Point,3>tri{poly[0],poly[j],poly[j+1]};if(std::abs(cross2(tri[1].uv-tri[0].uv,tri[2].uv-tri[0].uv))<1e-14)continue;UvTriangle uvtri{{tri[0].uv,tri[1].uv,tri[2].uv}};uvTriangles.push_back(uvtri);outputRefs.push_back({it->second,chunk.indices.size()});
            for(const Point&p:tri){D3 b=barycentric(s,p.p),exact=s.p[0]*b.x+s.p[1]*b.y+s.p[2]*b.z,n=unit(s.n[0]*b.x+s.n[1]*b.y+s.n[2]*b.z);chunk.indices.push_back(static_cast<uint32_t>(chunk.vertices.size()));chunk.vertices.push_back({v3(exact),v3(n),Vec2{std::clamp(p.uv.x,0.,1.),std::clamp(p.uv.y,0.,1.)},source.triangle,v3(b)});}
            if(++out.statistics.outputTriangles>r.limits.maximumTriangles)throw Failure(DecalPatchStatus::LimitExceeded,"Output triangle budget exceeded.");
        }
    }
    // Validate global chart injectivity on the bounded output, using a sweep to
    // avoid comparing spatially disjoint UV triangles.
    std::vector<size_t>order(uvTriangles.size());for(size_t i=0;i<order.size();++i)order[i]=i;
    const auto minx=[&](size_t i){const auto&t=uvTriangles[i];return std::min({t.p[0].x,t.p[1].x,t.p[2].x});};const auto maxx=[&](size_t i){const auto&t=uvTriangles[i];return std::max({t.p[0].x,t.p[1].x,t.p[2].x});};
    std::stable_sort(order.begin(),order.end(),[&](size_t x,size_t y){return minx(x)<minx(y);});
    const auto positionsOf=[&](size_t triangle){std::array<D3,3>p;const auto ref=outputRefs[triangle];const auto&chunk=out.chunks[ref.chunk];for(size_t j=0;j<3;++j)p[j]=d3(chunk.vertices[chunk.indices[ref.index+j]].position);return p;};
    std::vector<bool>valid(uvTriangles.size(),true);std::vector<double>sourceDistances;sourceDistances.reserve(uvTriangles.size());for(size_t i=0;i<uvTriangles.size();++i)sourceDistances.push_back(triangleDistance(a.seedPosition,positionsOf(i)));
    size_t comparisons=0;for(size_t i=0;i<order.size();++i)for(size_t j=i+1;j<order.size()&&minx(order[j])<=maxx(order[i])+1e-12;++j){cancelled(stop);if(++comparisons>4000000)throw Failure(DecalPatchStatus::LimitExceeded,"Chart overlap-validation budget exceeded.");const auto x=order[i],y=order[j];if(!valid[x]||!valid[y])continue;if(overlapArea(uvTriangles[x],uvTriangles[y])>1e-10){valid[sourceDistances[x]<=sourceDistances[y]?y:x]=false;incomplete=true;}}
    // Validation failures are chart boundaries. Retain only the valid portion
    // reached from the actual seed through shared surface edges; never publish
    // disconnected islands beyond a cut locus or a rejected cap-rim sliver.
    const double weld=std::max(5e-7,a.scale*1e-6);std::vector<D3>nodes;std::map<std::tuple<int64_t,int64_t,int64_t>,std::vector<size_t>>buckets;
    const auto node=[&](D3 p){const auto ix=static_cast<int64_t>(std::floor((p.x-a.seedPosition.x)/weld)),iy=static_cast<int64_t>(std::floor((p.y-a.seedPosition.y)/weld)),iz=static_cast<int64_t>(std::floor((p.z-a.seedPosition.z)/weld));for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){const auto it=buckets.find({ix+x,iy+y,iz+z});if(it!=buckets.end())for(size_t candidate:it->second)if(length(nodes[candidate]-p)<=weld)return candidate;}const auto index=nodes.size();nodes.push_back(p);buckets[{ix,iy,iz}].push_back(index);return index;};
    std::map<std::pair<size_t,size_t>,std::vector<size_t>>edges;
    size_t seedTriangle=SIZE_MAX;double seedDistance=INFINITY;
    for(size_t i=0;i<uvTriangles.size();++i)if(valid[i]){const auto p=positionsOf(i);std::array<size_t,3>ids;for(size_t j=0;j<3;++j)ids[j]=node(p[j]);for(size_t j=0;j<3;++j)if(ids[j]!=ids[(j+1)%3])edges[std::minmax(ids[j],ids[(j+1)%3])].push_back(i);if(sourceDistances[i]<seedDistance){seedTriangle=i;seedDistance=sourceDistances[i];}}
    if(seedTriangle==SIZE_MAX||seedDistance>std::max(a.scale*1e-5,1e-5))throw Failure(DecalPatchStatus::InvalidChart,"Surface chart has no valid seed-connected footprint.");
    std::vector<std::vector<size_t>>adjacent(uvTriangles.size());for(const auto&[edge,uses]:edges){(void)edge;if(uses.size()==2){adjacent[uses[0]].push_back(uses[1]);adjacent[uses[1]].push_back(uses[0]);}}
    std::vector<bool>retained(uvTriangles.size());std::queue<size_t>pending;pending.push(seedTriangle);retained[seedTriangle]=true;while(!pending.empty()){cancelled(stop);const auto f=pending.front();pending.pop();for(size_t next:adjacent[f])if(valid[next]&&!retained[next]){retained[next]=true;pending.push(next);}}
    auto previous=std::move(out.chunks);out.chunks.clear();out.chunks.resize(previous.size());for(size_t i=0;i<previous.size();++i){out.chunks[i].receiver=previous[i].receiver;out.chunks[i].generation=previous[i].generation;}out.statistics.outputTriangles=0;
    for(size_t i=0;i<retained.size();++i){if(!retained[i]){incomplete=true;continue;}const auto ref=outputRefs[i];auto&chunk=out.chunks[ref.chunk];for(size_t j=0;j<3;++j){chunk.indices.push_back(static_cast<uint32_t>(chunk.vertices.size()));chunk.vertices.push_back(previous[ref.chunk].vertices[previous[ref.chunk].indices[ref.index+j]]);}++out.statistics.outputTriangles;}
    std::erase_if(out.chunks,[](const auto&chunk){return chunk.indices.empty();});
    if(out.statistics.outputTriangles==0)throw Failure(DecalPatchStatus::InvalidChart,"Footprint has no valid receiver triangles.");
    out.status=incomplete?DecalPatchStatus::Incomplete:DecalPatchStatus::Ready;out.message=decalPatchStatusName(out.status);out.statistics.solveMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
}
}
