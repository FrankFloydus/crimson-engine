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
struct Point {D3 p;D2 uv;unsigned corners{};};
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
    std::vector<Point>out;const auto dist=[&](const Point&p){const double d=((axis==0?p.uv.x:p.uv.y)-boundary)*(upper?1:-1);return std::abs(d)<=1e-12?0.:d;};
    const auto snap=[&](Point p){if(axis==0)p.uv.x=boundary;else p.uv.y=boundary;return p;};
    for(size_t i=0;i<poly.size();++i){const auto&a=poly[i];const auto&b=poly[(i+1)%poly.size()];const double da=dist(a),db=dist(b);if(da<=0)out.push_back(da==0?snap(a):a);if(da*db<0){const double t=da/(da-db);out.push_back(snap({a.p+(b.p-a.p)*t,a.uv+(b.uv-a.uv)*t,t<=0?a.corners:t>=1?b.corners:a.corners|b.corners}));}}
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
// Develop only the seed-connected image footprint. Curvature in the numerical
// neighborhood, beyond the image, must not change an otherwise exact local
// development. Relevant cycles and edge metrics still have to agree; curvature
// inside the image selects the intrinsic log-map solve instead.
bool developableChart(const AnalysisPatch&a,std::vector<D2>&chart,std::vector<bool>&selected){
    struct Use{size_t face,edge;};
    std::map<std::pair<size_t,size_t>,std::vector<Use>>edges;
    const size_t first=a.seedFace;
    for(size_t i=0;i<a.faces.size();++i)for(size_t j=0;j<3;++j){const auto&v=a.faces[i].v;edges[std::minmax(v[j],v[(j+1)%3])].push_back({i,j});}
    chart.resize(a.positions.size());std::vector<bool>assigned(a.positions.size());selected.assign(a.faces.size(),false);
    for(size_t v:a.faces[first].v){D3 p=a.positions[v]-a.seedPosition;chart[v]={dot(p,a.basisX),dot(p,a.basisY)};assigned[v]=true;}
    std::queue<size_t>queue;queue.push(first);selected[first]=true;
    const double tolerance=std::max(a.scale*1e-6,a.tolerance*2);
    const auto intersectsImage=[&](const std::array<D2,3>&metres){
        std::vector<Point>poly;for(const auto p:metres)poly.push_back({{},
            {.5+(a.by*p.x-a.bx*p.y)/a.frameDet,.5-(-a.ay*p.x+a.ax*p.y)/a.frameDet}});
        constexpr double halo=1e-6;
        for(size_t axis=0;axis<2&&!poly.empty();++axis){poly=clipUv(poly,axis,-halo,false);if(!poly.empty())poly=clipUv(poly,axis,1+halo,true);}
        return poly.size()>=3;
    };
    while(!queue.empty()){
        const size_t f=queue.front();queue.pop();const auto&v=a.faces[f].v;
        for(size_t j=0;j<3;++j){const size_t va=v[j],vb=v[(j+1)%3],vc=v[(j+2)%3];const auto&uses=edges[std::minmax(va,vb)];if(uses.size()!=2)continue;
            const Use next=uses[0].face==f?uses[1]:uses[0];const auto&nv=a.faces[next.face].v;const size_t vd=nv[(next.edge+2)%3];
            D2 delta=chart[vb]-chart[va];const double l=std::sqrt(delta.x*delta.x+delta.y*delta.y);if(l<1e-12)return false;
            // Work in the source edge frame. Subtracting squared lengths loses
            // the altitude of narrow contact triangles and can corrupt an
            // otherwise exact development around a shared-vertex cycle.
            const D3 edge=a.positions[vb]-a.positions[va],diagonal=a.positions[vd]-a.positions[va];const double sourceLength=length(edge);
            if(sourceLength<1e-12)return false;
            const double x=dot(diagonal,edge/sourceLength);
            const double sign=cross2(delta,chart[vc]-chart[va])>0?-1:1;const double y=length(cross(edge/sourceLength,diagonal))*sign;
            const D2 proposed=chart[va]+D2{delta.x/l,delta.y/l}*x+D2{-delta.y/l,delta.x/l}*y;
            // Test the face's own proposed development before assigning its
            // third vertex. An irrelevant neighbor cannot pollute a later
            // image-interior cycle through a shared vertex.
            if(!intersectsImage({chart[va],chart[vb],proposed}))continue;
            if(std::abs(l-sourceLength)>tolerance)return false;
            if(assigned[vd]){D2 error=chart[vd]-proposed;if(std::sqrt(error.x*error.x+error.y*error.y)>tolerance)return false;}
            else{chart[vd]=proposed;assigned[vd]=true;}
            if(!selected[next.face]){selected[next.face]=true;queue.push(next.face);}
        }
    }
    return true;
}
}

void buildChart(const DecalSurfaceSnapshot&snapshot,const DecalBuildRequest&r,const AnalysisPatch&a,DecalPatch&out,std::stop_token stop){
    namespace gc=geometrycentral;namespace gs=geometrycentral::surface;
    const auto started=std::chrono::steady_clock::now();cancelled(stop);
    std::vector<std::vector<size_t>>polygons;polygons.reserve(a.faces.size());for(const auto&f:a.faces)polygons.push_back({f.v[0],f.v[1],f.v[2]});
    gs::ManifoldSurfaceMesh mesh(polygons);gs::VertexData<gc::Vector3> positions(mesh);
    for(gs::Vertex v:mesh.vertices()){const D3 p=a.positions[v.getIndex()];positions[v]={p.x,p.y,p.z};}
    struct ChartTriangle{size_t sourceFace;std::array<D3,3>positions;std::array<D2,3>metres;std::array<size_t,3>vertices;};
    std::vector<ChartTriangle> triangles;std::vector<D2>inputChart;std::vector<bool>developedFaces;
    out.statistics.usedDevelopableUnfolding=developableChart(a,inputChart,developedFaces);
    if(out.statistics.usedDevelopableUnfolding){for(size_t i=0;i<a.faces.size();++i)if(developedFaces[i]){ChartTriangle t{};t.sourceFace=i;for(size_t j=0;j<3;++j){size_t v=a.faces[i].v[j];t.positions[j]=a.positions[v];t.metres[j]=inputChart[v];t.vertices[j]=v;}triangles.push_back(t);}}
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
    // Original input boundaries and the analysis halo are not final output.
    // Keep the solver's temporary transfer bounded independently of clipping.
    const size_t commonLimit=static_cast<size_t>(std::min<uint64_t>(1'000'000,std::max<uint64_t>(uint64_t(r.limits.maximumTriangles)*4,uint64_t(a.faces.size())*4)));
    if(common.mesh->nFaces()>commonLimit)throw Failure(DecalPatchStatus::LimitExceeded,"Intrinsic common-subdivision budget exceeded.");
    auto commonChart=common.interpolateAcrossB(chart);auto commonPosition=common.interpolateAcrossA(positions);
    for(gs::Face f:common.mesh->faces()){ChartTriangle t{};t.sourceFace=common.sourceFaceA[f].getIndex();size_t i=0;for(gs::Vertex v:f.adjacentVertices()){if(i>=3)throw Failure(DecalPatchStatus::InvalidChart,"Common subdivision did not triangulate.");auto p=commonPosition[v];auto c=commonChart[v];t.positions[i]={p.x,p.y,p.z};t.vertices[i]=v.getIndex();t.metres[i++]={c.x,c.y};}if(i==3)triangles.push_back(t);}
    }
    // Chart metres retain the oriented surface basis. Raster V runs downward,
    // so the authored local +Y edge samples the top of the source image.
    const auto uv=[&](D2 p){return D2{.5+(a.by*p.x-a.bx*p.y)/a.frameDet,.5-(-a.ay*p.x+a.ax*p.y)/a.frameDet};};
    // Keep analysis positions in double precision through validation. Published
    // world-space floats can move an FPS-sized patch off its seed at large
    // coordinates and can separate otherwise shared surface edges by an ULP.
    using VertexKey=std::tuple<unsigned,size_t,size_t,unsigned>;
    const auto vertexKey=[](const ChartTriangle&face,size_t faceIndex,const Point&p)->VertexKey {
        for(size_t j=0;j<3;++j)if(p.corners==(1u<<j))return {0,face.vertices[j],0,0};
        unsigned boundary=0;
        for(size_t axis=0;axis<2;++axis){const double v=axis==0?p.uv.x:p.uv.y;if(std::abs(v)<=1e-12)boundary|=1u<<(axis*2);if(std::abs(v-1)<=1e-12)boundary|=1u<<(axis*2+1);}
        for(size_t j=0;j<3;++j)for(size_t k=j+1;k<3;++k)if(p.corners==((1u<<j)|(1u<<k))){const auto edge=std::minmax(face.vertices[j],face.vertices[k]);return {1,edge.first,edge.second,boundary};}
        return {2,faceIndex,0,boundary};
    };
    struct OutputRef{size_t chunk,index;std::array<D3,3>positions;std::array<VertexKey,3>vertices;};
    std::map<size_t,size_t>chunks;std::vector<UvTriangle>uvTriangles;std::vector<OutputRef>outputRefs;
    for(size_t faceIndex=0;faceIndex<triangles.size();++faceIndex){const auto&f=triangles[faceIndex];
        cancelled(stop);const size_t sourceFace=f.sourceFace;if(sourceFace>=a.faces.size())throw Failure(DecalPatchStatus::InvalidChart,"Common subdivision lost source provenance.");
        // The working halo is a numerical boundary condition, never a donor
        // surface for a map whose distant heat quotient has lost precision.
        const double support=.5*std::max(std::hypot(a.ax+a.bx,a.ay+a.by),std::hypot(a.ax-a.bx,a.ay-a.by))*r.limits.maximumStretch;
        if(triangleDistance(a.seedPosition,f.positions)>support)continue;
        const Source&s=a.sources[a.faces[sourceFace].source];const auto&receiver=snapshot.receivers[s.receiver];const auto&source=receiver.triangles[s.triangle];
        std::vector<Point>poly;const auto&metres=f.metres;
        for(size_t i=0;i<3;++i){D2 c=metres[i];if(!std::isfinite(c.x)||!std::isfinite(c.y))throw Failure(DecalPatchStatus::InvalidChart,"Chart coordinates are nonfinite.");poly.push_back({f.positions[i],uv(c),1u<<i});}
        const auto original=poly;
        for(size_t axis=0;axis<2;++axis){poly=clipUv(poly,axis,0,false);if(poly.empty())break;poly=clipUv(poly,axis,1,true);if(poly.empty())break;}
        if(poly.size()<3)continue;
        const double chartArea=cross2(metres[1]-metres[0],metres[2]-metres[0]);const double surfaceParity=dot(cross(original[1].p-original[0].p,original[2].p-original[0].p),s.geometricNormal);
        if(std::abs(surfaceParity)<a.scale*a.scale*1e-13)continue;
        if(chartArea*surfaceParity<=0)continue;
        const double distortion=stretch(original[1].p-original[0].p,original[2].p-original[0].p,metres[1]-metres[0],metres[2]-metres[0]);
        if(!std::isfinite(distortion)||distortion>r.limits.maximumStretch+1e-6)continue;
        out.statistics.maximumStretch=std::max(out.statistics.maximumStretch,distortion);
        auto[it,inserted]=chunks.try_emplace(s.receiver,out.chunks.size());if(inserted)out.chunks.push_back({receiver.id,receiver.generation,{},{}});auto&chunk=out.chunks[it->second];
        for(size_t j=1;j+1<poly.size();++j){std::array<Point,3>tri{poly[0],poly[j],poly[j+1]};if(std::abs(cross2(tri[1].uv-tri[0].uv,tri[2].uv-tri[0].uv))<1e-14)continue;UvTriangle uvtri{{tri[0].uv,tri[1].uv,tri[2].uv}};uvTriangles.push_back(uvtri);outputRefs.push_back({it->second,chunk.indices.size(),{}, {}});
            for(size_t k=0;k<tri.size();++k){const Point&p=tri[k];D3 b=barycentric(s,p.p),exact=s.p[0]*b.x+s.p[1]*b.y+s.p[2]*b.z,n=unit(s.n[0]*b.x+s.n[1]*b.y+s.n[2]*b.z);outputRefs.back().positions[k]=exact;outputRefs.back().vertices[k]=vertexKey(f,faceIndex,p);chunk.indices.push_back(static_cast<uint32_t>(chunk.vertices.size()));chunk.vertices.push_back({v3(exact),v3(n),Vec2{std::clamp(p.uv.x,0.,1.),std::clamp(p.uv.y,0.,1.)},source.triangle,v3(b)});}
            if(++out.statistics.outputTriangles>r.limits.maximumTriangles)throw Failure(DecalPatchStatus::LimitExceeded,"Output triangle budget exceeded.");
        }
    }
    // Validate global chart injectivity on the bounded output, using a sweep to
    // avoid comparing spatially disjoint UV triangles.
    struct UvBounds{D2 lo,hi;};std::vector<UvBounds>uvBounds;uvBounds.reserve(uvTriangles.size());
    for(const auto&t:uvTriangles)uvBounds.push_back({{std::min({t.p[0].x,t.p[1].x,t.p[2].x}),std::min({t.p[0].y,t.p[1].y,t.p[2].y})},{std::max({t.p[0].x,t.p[1].x,t.p[2].x}),std::max({t.p[0].y,t.p[1].y,t.p[2].y})}});
    std::vector<size_t>order(uvTriangles.size());for(size_t i=0;i<order.size();++i)order[i]=i;
    std::stable_sort(order.begin(),order.end(),[&](size_t x,size_t y){return uvBounds[x].lo.x<uvBounds[y].lo.x;});
    const auto positionsOf=[&](size_t triangle)->const std::array<D3,3>&{return outputRefs[triangle].positions;};
    std::vector<bool>valid(uvTriangles.size(),true);std::vector<double>sourceDistances;sourceDistances.reserve(uvTriangles.size());for(size_t i=0;i<uvTriangles.size();++i)sourceDistances.push_back(triangleDistance(a.seedPosition,positionsOf(i)));
    size_t comparisons=0;for(size_t i=0;i<order.size();++i)for(size_t j=i+1;j<order.size()&&uvBounds[order[j]].lo.x<=uvBounds[order[i]].hi.x+1e-12;++j){cancelled(stop);const auto x=order[i],y=order[j];if(!valid[x]||!valid[y]||uvBounds[x].lo.y>uvBounds[y].hi.y+1e-12||uvBounds[y].lo.y>uvBounds[x].hi.y+1e-12)continue;if(++comparisons>4000000)throw Failure(DecalPatchStatus::LimitExceeded,"Chart overlap-validation budget exceeded.");if(overlapArea(uvTriangles[x],uvTriangles[y])>1e-10)valid[sourceDistances[x]<=sourceDistances[y]?y:x]=false;}
    // Validation failures are chart boundaries. Retain only the valid portion
    // reached from the actual seed through proven chart edges. Carry their
    // topology through UV clipping: a spatial weld can collapse thin valid
    // triangles and poison adjacency, or reconnect intentionally split sheets.
    std::map<VertexKey,size_t>nodes;
    const auto node=[&](const VertexKey&key){const auto[it,inserted]=nodes.try_emplace(key,nodes.size());(void)inserted;return it->second;};
    struct EdgeUse{size_t triangle;bool forward;};
    std::map<std::pair<size_t,size_t>,std::vector<EdgeUse>>edges;
    size_t seedTriangle=SIZE_MAX;double seedDistance=INFINITY;
    for(size_t i=0;i<uvTriangles.size();++i)if(valid[i]){std::array<size_t,3>ids;for(size_t j=0;j<3;++j)ids[j]=node(outputRefs[i].vertices[j]);if(ids[0]==ids[1]||ids[1]==ids[2]||ids[2]==ids[0]){valid[i]=false;continue;}for(size_t j=0;j<3;++j)edges[std::minmax(ids[j],ids[(j+1)%3])].push_back({i,ids[j]<ids[(j+1)%3]});if(sourceDistances[i]<seedDistance){seedTriangle=i;seedDistance=sourceDistances[i];}}
    if(seedTriangle==SIZE_MAX||seedDistance>std::max(a.scale*1e-5,1e-5))throw Failure(DecalPatchStatus::InvalidChart,"Surface chart has no valid seed-connected footprint.");
    std::vector<std::vector<size_t>>adjacent(uvTriangles.size());for(const auto&[edge,uses]:edges){(void)edge;if(uses.size()==2&&uses[0].forward!=uses[1].forward){adjacent[uses[0].triangle].push_back(uses[1].triangle);adjacent[uses[1].triangle].push_back(uses[0].triangle);}}
    std::vector<bool>retained(uvTriangles.size());std::queue<size_t>pending;pending.push(seedTriangle);retained[seedTriangle]=true;while(!pending.empty()){cancelled(stop);const auto f=pending.front();pending.pop();for(size_t next:adjacent[f])if(valid[next]&&!retained[next]){retained[next]=true;pending.push(next);}}
    // Completeness belongs to the validated image, not its preparation halo.
    // Contacts, cut loci and rejected components outside the image can make
    // analysis incomplete while leaving every image pixel covered. Conversely,
    // an actual gap/barrier/distortion cut leaves a proven output boundary in
    // the rectangle's interior. Use connectivity, never area alone, to tell.
    bool incomplete=false;
    for(const auto&[edge,uses]:edges){
        std::array<EdgeUse,2> kept{};size_t count=0;for(const auto& use:uses)if(retained[use.triangle]){if(count<kept.size())kept[count]=use;++count;}
        if(!count||(count==2&&kept[0].forward!=kept[1].forward))continue;
        if(count!=1){incomplete=true;continue;}
        const size_t triangle=kept[0].triangle;
        for(size_t corner=0;corner<3;++corner){const auto first=node(outputRefs[triangle].vertices[corner]),last=node(outputRefs[triangle].vertices[(corner+1)%3]);if(std::min(first,last)==edge.first&&std::max(first,last)==edge.second&&intersectsFootprintInterior(uvTriangles[triangle].p[corner],uvTriangles[triangle].p[(corner+1)%3]))incomplete=true;}
    }
    auto previous=std::move(out.chunks);out.chunks.clear();out.chunks.resize(previous.size());for(size_t i=0;i<previous.size();++i){out.chunks[i].receiver=previous[i].receiver;out.chunks[i].generation=previous[i].generation;}out.statistics.outputTriangles=0;
    for(size_t i=0;i<retained.size();++i){if(!retained[i])continue;const auto ref=outputRefs[i];auto&chunk=out.chunks[ref.chunk];for(size_t j=0;j<3;++j){chunk.indices.push_back(static_cast<uint32_t>(chunk.vertices.size()));chunk.vertices.push_back(previous[ref.chunk].vertices[previous[ref.chunk].indices[ref.index+j]]);}++out.statistics.outputTriangles;}
    std::erase_if(out.chunks,[](const auto&chunk){return chunk.indices.empty();});
    if(out.statistics.outputTriangles==0)throw Failure(DecalPatchStatus::InvalidChart,"Footprint has no valid receiver triangles.");
    out.status=incomplete?DecalPatchStatus::Incomplete:DecalPatchStatus::Ready;out.message=decalPatchStatusName(out.status);out.statistics.solveMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
}
}
