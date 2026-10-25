#include "DecalInternal.h"
#include "DecalPolygon.h"
#include "DecalContactCells.h"
#include "DecalEndpointIndex.h"
#include "DecalFiniteContact.h"
#include "DecalSourceContacts.h"
#include "DecalSampling.h"
#include <chrono>
#include <map>
#include <numeric>
#include <optional>
#include <queue>
#include <tuple>
namespace crimson::decals::detail {
namespace {
struct Plane {D3 n;double d;};
using Polygon=std::vector<D3>;
double distance(const Plane&p,D3 v){return dot(p.n,v)-p.d;}
Polygon clip(const Polygon&poly,const Plane&plane,double epsilon){
    Polygon out;
    for(size_t i=0;i<poly.size();++i){const D3 a=poly[i],b=poly[(i+1)%poly.size()];const double da=distance(plane,a),db=distance(plane,b);
        if(da<=epsilon)out.push_back(a);
        if((da>epsilon&&db< -epsilon)||(da< -epsilon&&db>epsilon))out.push_back(a+(b-a)*(da/(da-db)));
    }
    Polygon clean;for(D3 p:out)if(clean.empty()||length(p-clean.back())>epsilon)clean.push_back(p);
    if(clean.size()>1&&length(clean.front()-clean.back())<=epsilon)clean.pop_back();return clean;
}
bool inside(const Source&s,D3 p,double e){const D3 b=barycentric(s,p);return b.x>=-e&&b.y>=-e&&b.z>=-e;}
struct Box {D3 lo,hi;};
Box bounds(const Source&s){Box b{s.p[0],s.p[0]};for(D3 p:s.p){b.lo={std::min(b.lo.x,p.x),std::min(b.lo.y,p.y),std::min(b.lo.z,p.z)};b.hi={std::max(b.hi.x,p.x),std::max(b.hi.y,p.y),std::max(b.hi.z,p.z)};}return b;}
Box bounds(const Polygon&p){Box b{p.front(),p.front()};for(D3 v:p){b.lo={std::min(b.lo.x,v.x),std::min(b.lo.y,v.y),std::min(b.lo.z,v.z)};b.hi={std::max(b.hi.x,v.x),std::max(b.hi.y,v.y),std::max(b.hi.z,v.z)};}return b;}
bool overlaps(const Box&a,const Box&b,double e){for(size_t k=0;k<3;++k)if(a.lo[k]>b.hi[k]+e||b.lo[k]>a.hi[k]+e)return false;return true;}
struct Cut {Plane plane;Box support;std::optional<FiniteContactInterval> finite;};
struct Segment {D3 a,b;};
bool positionLess(D3 a,D3 b){return std::tie(a.x,a.y,a.z)<std::tie(b.x,b.y,b.z);}
void addPlane(std::vector<Cut>&v,D3 n,D3 p,const Box&support,std::optional<FiniteContactInterval> finite={}){if(length(n)>1e-15){n=unit(n);v.push_back({{n,dot(n,p)},support,std::move(finite)});}}
bool boundarySegment(const Source&s,D3 p,D3 q,double tol){for(size_t i=0;i<3;++i){const D3 a=s.p[i],e=s.p[(i+1)%3]-a;const double len=length(e);if(len<=1e-15)continue;const D3 n=e/len;const double u=dot(p-a,n),v=dot(q-a,n);if(length(p-a-n*u)<=tol&&length(q-a-n*v)<=tol&&std::min(u,v)>=-tol&&std::max(u,v)<=len+tol)return true;}return false;}
// Return coplanar overlap separately: overlapping sources must participate in
// multiplicity tests even though an interior crossing is never a surface join.
bool contactCuts(const Source&a,const Source&b,std::vector<Cut>&ca,std::vector<Cut>&cb,std::vector<Segment>&ba,std::vector<Segment>&bb,double tol,double weld,double epsilon){
    const D3 na=unit(cross(a.p[1]-a.p[0],a.p[2]-a.p[0])),nb=unit(cross(b.p[1]-b.p[0],b.p[2]-b.p[0]));
    if(length(cross(na,nb))<1e-8){
        if(std::abs(dot(na,b.p[0]-a.p[0]))>tol)return false;
        Polygon intersection{a.p[0],a.p[1],a.p[2]};for(size_t i=0;i<3;++i){D3 n=unit(cross(b.p[(i+1)%3]-b.p[i],nb));intersection=clip(intersection,{n,dot(n,b.p[i])},tol*1e-5);if(intersection.size()<3)break;}
        double overlap=0;if(intersection.size()>=3)for(size_t j=1;j+1<intersection.size();++j)overlap+=length(cross(intersection[j]-intersection[0],intersection[j+1]-intersection[0]));
        // Cut actual coplanar overlap, not arbitrary extensions of unrelated
        // edges. The latter creates tiny cells on otherwise regular meshes.
        const bool areaOverlap=overlap>tol*tol;const Box support=intersection.empty()?bounds(a):bounds(intersection);
        for(size_t i=0;i<3;++i){if(areaOverlap){addPlane(ca,cross(na,b.p[(i+1)%3]-b.p[i]),b.p[i],support);addPlane(cb,cross(nb,a.p[(i+1)%3]-a.p[i]),a.p[i],support);}
            const D3 ea=a.p[(i+1)%3]-a.p[i],eb=b.p[(i+1)%3]-b.p[i];
            for(D3 q:b.p){double t=dot(q-a.p[i],ea)/dot(ea,ea);if(t>1e-7&&t<1-1e-7&&length(q-a.p[i]-ea*t)<=tol)addPlane(ca,ea,q,{q,q});}
            for(D3 q:a.p){double t=dot(q-b.p[i],eb)/dot(eb,eb);if(t>1e-7&&t<1-1e-7&&length(q-b.p[i]-eb*t)<=tol)addPlane(cb,eb,q,{q,q});}
        }return overlap>0;
    }
    const auto boundary=originalBoundaryContacts(a,b,tol,epsilon,weld);
    bool boundaryOnly=boundary.size()==1;
    if(boundaryOnly){const auto& contact=boundary.front();const auto onSegment=[&](D3 point,const std::array<D3,2>& ends){const D3 edge=ends[1]-ends[0];const double squared=dot(edge,edge);const double parameter=squared>0?std::clamp(dot(point-ends[0],edge)/squared,0.,1.):0.;return length(point-ends[0]-edge*parameter)<=weld;};
        // A near-boundary correspondence must not hide a second genuine
        // plane intersection through either triangle's interior.
        const auto interior=[&](const Source& source,const Source& other,D3 normal){for(size_t i=0;i<3;++i){const D3 p=source.p[i],q=source.p[(i+1)%3];const double x=dot(normal,p-other.p[0]),y=dot(normal,q-other.p[0]);if(x==0&&inside(other,p,epsilon)&&(!onSegment(p,contact.onA)||!onSegment(p,contact.onB)))return true;if(x*y<0){const D3 at=p+(q-p)*(x/(x-y));if(inside(other,at,epsilon)&&(!onSegment(at,contact.onA)||!onSegment(at,contact.onB)))return true;}}return false;};
        boundaryOnly=!interior(a,b,nb)&&!interior(b,a,na);
    }
    if(boundaryOnly){const auto& contact=boundary.front();
        const auto endpoints=[&](const Source& source,size_t edge,const std::array<D3,2>& points,std::vector<Cut>& cuts){const D3 direction=unit(source.p[(edge+1)%3]-source.p[edge]);const Box support=bounds(Polygon{points[0],points[1]});for(const auto point:points){bool original=false;for(const auto corner:source.p)original|=length(point-corner)<=weld;if(!original)addPlane(cuts,direction,point,support);}};
        endpoints(a,contact.edgeA,contact.onA,ca);endpoints(b,contact.edgeB,contact.onB,cb);return false;
    }
    std::vector<D3> pts;
    const auto collect=[&](const Source&s,const Source&t,D3 nt){for(size_t i=0;i<3;++i){D3 p=s.p[i],q=s.p[(i+1)%3];const double x=dot(nt,p-t.p[0]),y=dot(nt,q-t.p[0]);if(std::abs(x)<=tol&&inside(t,p,tol))pts.push_back(p);if(x*y<0){D3 at=p+(q-p)*(x/(x-y));if(inside(t,at,tol))pts.push_back(at);}}};
    collect(a,b,nb);collect(b,a,na);double best=tol;D3 p{},q{};
    for(D3 x:pts)for(D3 y:pts)if(length(x-y)>best){best=length(x-y);p=x;q=y;}
    if(best<=tol)return false;
    const auto finiteCut=[&](const Source&source,D3 normal,std::vector<Cut>&cuts){
        const D3 first=p-normal*dot(normal,p-source.p[0]),last=q-normal*dot(normal,q-source.p[0]);
        const double extent=length(last-first);if(extent<=epsilon)return;
        const D3 direction=(last-first)/extent;const Box support=bounds(Polygon{first,last});
        // These must precede the longitudinal plane. Otherwise a finite
        // contact splits the entire common-grid cell before its ends exist.
        // Exact source corners naturally fail the ordinary +/-epsilon split
        // gate, so no broader proximity test may omit a required endpoint.
        addPlane(cuts,direction,first,support);addPlane(cuts,direction,last,support);
        addPlane(cuts,cross(normal,direction),first,support,FiniteContactInterval{first,direction,extent});
    };
    finiteCut(a,na,ca);finiteCut(b,nb,cb);
    if(!boundarySegment(a,p,q,tol)||!boundarySegment(b,p,q,tol)){
        const auto projected=[](D3 p,const Source&s,D3 n){return p-n*dot(n,p-s.p[0]);};
        ba.push_back({projected(p,a,na),projected(q,a,na)});bb.push_back({projected(p,b,nb),projected(q,b,nb)});
    }
    return false;
}
struct Dsu {std::vector<size_t> p;explicit Dsu(size_t n):p(n){std::iota(p.begin(),p.end(),size_t{});}size_t find(size_t a){while(p[a]!=a){p[a]=p[p[a]];a=p[a];}return a;}void join(size_t a,size_t b){a=find(a);b=find(b);if(a!=b)p[std::max(a,b)]=std::min(a,b);}};
struct EdgeUse {size_t face{},corner{};};
}
AnalysisPatch buildTopology(const DecalSurfaceSnapshot&snapshot,const DecalBuildRequest&r,DecalBuildStatistics&stats,std::stop_token stop){
    const auto started=std::chrono::steady_clock::now();AnalysisPatch patch;
    if(r.limits.maximumTriangles<4||r.limits.maximumTriangles>1'000'000||r.limits.maximumInputTriangles<1||r.limits.maximumInputTriangles>1'000'000||!std::isfinite(r.limits.maximumStretch)||r.limits.maximumStretch<1)throw Failure(DecalPatchStatus::InvalidInput,"Invalid surface preparation limits.");
    const SurfaceReceiver*seedReceiver=nullptr;const SurfaceTriangle*seedTriangle=nullptr;
    for(const auto&rec:snapshot.receivers)if(rec.id==r.seed.receiver){if(seedReceiver)throw Failure(DecalPatchStatus::AmbiguousSeed,"Receiver occurrence identity is duplicated.");seedReceiver=&rec;for(const auto&t:rec.triangles)if(t.triangle==r.seed.triangle){if(seedTriangle)throw Failure(DecalPatchStatus::AmbiguousSeed,"Source triangle identity is duplicated.");seedTriangle=&t;}}
    if(!seedTriangle)throw Failure(DecalPatchStatus::MissingSeed,"Seed receiver triangle is unavailable.");
    const D3 bary=d3(r.seed.barycentrics);if(!finite(bary)||std::abs(bary.x+bary.y+bary.z-1)>1e-4||std::min({bary.x,bary.y,bary.z})< -1e-5)throw Failure(DecalPatchStatus::InvalidInput,"Invalid seed barycentrics.");
    const auto&sm=seedReceiver->localToWorld;
    patch.seedPosition=point(sm,d3(seedTriangle->positions[0])*bary.x+d3(seedTriangle->positions[1])*bary.y+d3(seedTriangle->positions[2])*bary.z);
    patch.seedNormal=normal(sm,cross(d3(seedTriangle->positions[1])-d3(seedTriangle->positions[0]),d3(seedTriangle->positions[2])-d3(seedTriangle->positions[0])));
    const auto domain=samplingDomain(r,patch.seedPosition,patch.seedNormal);
    patch.basisX=domain.basisX;patch.basisY=domain.basisY;patch.ax=domain.ax;patch.ay=domain.ay;patch.bx=domain.bx;patch.by=domain.by;patch.frameDet=domain.frameDet;patch.scale=domain.scale;patch.tolerance=domain.tolerance;
    // The authored footprint and actual normal retain their original frame.
    // Only a proven refined perimeter may replace its rounded construction
    // coordinates in the working topology; publication keeps actualP below.
    const auto workingSeed=boundaryPositions(*seedReceiver,*seedTriangle);
    patch.seedPosition=point(sm,workingSeed[0]*bary.x+workingSeed[1]*bary.y+workingSeed[2]*bary.z);
    const double radius=domain.radius,tol=domain.tolerance,spacing=domain.spacing,intrinsicRadius=domain.intrinsicRadius;
    const D3 o=patch.seedPosition;const Box roi{o-D3{radius,radius,radius},o+D3{radius,radius,radius}};size_t seedSource=SIZE_MAX;
    for(size_t ri=0;ri<snapshot.receivers.size();++ri){const auto&rec=snapshot.receivers[ri];const Mat4&m=rec.localToWorld;
        for(float f:m)if(!std::isfinite(f))throw Failure(DecalPatchStatus::InvalidInput,"Receiver frame is nonfinite.");
        if(std::abs(dot(column(m,0),cross(column(m,1),column(m,2))))<1e-15)throw Failure(DecalPatchStatus::InvalidInput,"Receiver frame is singular.");
        for(size_t ti=0;ti<rec.triangles.size();++ti){cancelled(stop);const auto&t=rec.triangles[ti];Source s;s.receiver=ri;s.triangle=ti;D3 ng=cross(d3(t.positions[1])-d3(t.positions[0]),d3(t.positions[2])-d3(t.positions[0]));if(length(ng)<1e-16)continue;s.geometricNormal=normal(m,ng);
            const auto working=boundaryPositions(rec,t);
            for(size_t j=0;j<3;++j){s.actualP[j]=point(m,d3(t.positions[j]));s.p[j]=point(m,working[j]);s.boundaryAdjusted|=length(s.p[j]-s.actualP[j])>0;s.n[j]=normal(m,length(d3(t.normals[j]))>1e-10?d3(t.normals[j]):ng);if(!finite(s.p[j])||!finite(s.actualP[j])||!finite(s.n[j]))throw Failure(DecalPatchStatus::InvalidInput,"Receiver geometry is nonfinite.");}
            if(s.boundaryAdjusted&&length(cross(s.p[1]-s.p[0],s.p[2]-s.p[0]))<1e-20)throw Failure(DecalPatchStatus::InvalidInput,"Paint boundary construction is degenerate.");
            // The box is only a broad phase. A triangle farther than the
            // intrinsic frontier cannot supply a path or a contact inside it.
            // Retain one sample of halo for edge-path distance approximation.
            if(!overlaps(bounds(s),roi,tol)||triangleDistance(o,s.p)>intrinsicRadius+spacing+tol)continue;if(&rec==seedReceiver&&&t==seedTriangle)seedSource=patch.sources.size();patch.sources.push_back(s);if(patch.sources.size()>r.limits.maximumInputTriangles)throw Failure(DecalPatchStatus::LimitExceeded,"Input triangle budget exceeded.");
        }
    }
    if(seedSource==SIZE_MAX)throw Failure(DecalPatchStatus::MissingSeed,"Seed geometry is degenerate or outside its footprint.");stats.inputTriangles=static_cast<uint32_t>(patch.sources.size());
    // Analysis includes a sampling halo and original source boundaries that
    // disappear when the chart is clipped to the image. Its allowance must be
    // independent of the final output cap, while remaining strictly bounded.
    constexpr size_t analysisLimit=1'000'000;
    std::vector<std::vector<Cut>> cuts(patch.sources.size());std::vector<std::vector<Segment>> barriers(patch.sources.size());Dsu sourceSheets(patch.sources.size());std::vector<Box> boxes;for(const auto&s:patch.sources)boxes.push_back(bounds(s));std::vector<size_t> order(patch.sources.size());std::iota(order.begin(),order.end(),size_t{});
    Box sourceBounds=boxes.front();for(const auto& box:boxes){sourceBounds.lo={std::min(sourceBounds.lo.x,box.lo.x),std::min(sourceBounds.lo.y,box.lo.y),std::min(sourceBounds.lo.z,box.lo.z)};sourceBounds.hi={std::max(sourceBounds.hi.x,box.hi.x),std::max(sourceBounds.hi.y,box.hi.y),std::max(sourceBounds.hi.z,box.hi.z)};}
    size_t sweepAxis=0;for(size_t axis=1;axis<3;++axis)if(sourceBounds.hi[axis]-sourceBounds.lo[axis]>sourceBounds.hi[sweepAxis]-sourceBounds.lo[sweepAxis])sweepAxis=axis;
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return boxes[a].lo[sweepAxis]<boxes[b].lo[sweepAxis];});size_t pairs=0;
    // Contact metadata follows the bounded original input. Reserve the smaller
    // derived-geometry allowance for the sheet that can actually reach the seed.
    size_t cutCount=0;const double eps=std::max(1e-11,patch.scale*1e-10),weld=std::min(tol,std::max(1e-9,patch.scale*1e-7));
    for(size_t a=0;a<order.size();++a)for(size_t b=a+1;b<order.size()&&boxes[order[b]].lo[sweepAxis]<=boxes[order[a]].hi[sweepAxis]+tol;++b){
        cancelled(stop);const size_t i=order[a],j=order[b];if(!overlaps(boxes[i],boxes[j],tol))continue;
        if(++pairs>1000000)throw Failure(DecalPatchStatus::LimitExceeded,"Local contact-pair budget exceeded.");
        const size_t before=cuts[i].size()+cuts[j].size();const bool overlap=contactCuts(patch.sources[i],patch.sources[j],cuts[i],cuts[j],barriers[i],barriers[j],tol,weld,eps);
        cutCount+=cuts[i].size()+cuts[j].size()-before;if(cutCount>static_cast<size_t>(r.limits.maximumInputTriangles)*32)throw Failure(DecalPatchStatus::LimitExceeded,"Local contact-plane budget exceeded.");
        if(overlap||sourceBoundaryOverlap(patch.sources[i],patch.sources[j],tol,std::max(1e-11,patch.scale*1e-10)))sourceSheets.join(i,j);
    }
    // Only an original boundary/boundary contact can give a traversable
    // two-sided seam. Interior crossings have three/four uses. Keep their exact
    // cuts and barrier segments, but do not tessellate an entire unrelated mesh
    // merely to rediscover that it cannot join the seed sheet.
    const size_t seedSheet=sourceSheets.find(seedSource);
    std::vector<AnalysisFace> allFaces;
    struct Cell {Polygon polygon;size_t source;double weldRadius;std::vector<size_t> corners;};std::vector<Cell> allCells;size_t baseTriangles=0;
    for(size_t si=0;si<patch.sources.size();++si){cancelled(stop);if(sourceSheets.find(si)!=seedSheet)continue;const Source&s=patch.sources[si];Polygon poly{s.p[0],s.p[1],s.p[2]};for(size_t k=0;k<3;++k){D3 n{};if(k==0)n.x=1;if(k==1)n.y=1;if(k==2)n.z=1;poly=clip(poly,{n,o[k]+radius},eps);poly=clip(poly,{n*-1,-o[k]+radius},eps);if(poly.size()<3)break;}if(poly.size()<3)continue;
        const size_t omittedAxis=std::abs(patch.seedNormal.x)>std::abs(patch.seedNormal.y)?(std::abs(patch.seedNormal.x)>std::abs(patch.seedNormal.z)?0:2):(std::abs(patch.seedNormal.y)>std::abs(patch.seedNormal.z)?1:2);
        // Establish the common grid first. A short contact may split the cells
        // it touches, never extend hundreds of infinite planes across a wall.
        double maximumEdge=0;for(size_t edge=0;edge<poly.size();++edge)maximumEdge=std::max(maximumEdge,length(poly[(edge+1)%poly.size()]-poly[edge]));
        const Box initialBounds=bounds(poly);ContactCells<Polygon,Box> cells(std::move(poly),initialBounds);std::vector<Cut> planes;for(size_t k=0;k<3;++k){if(k==omittedAxis)continue;D3 n{};if(k==0)n.x=1;if(k==1)n.y=1;if(k==2)n.z=1;
            // Already fine source triangles need no second sampling grid. The
            // seed still needs both planes through its exact position. Contact
            // cuts and the shared-endpoint pass below maintain conformity.
            const int steps=maximumEdge>spacing?static_cast<int>(std::ceil(radius/spacing)):0;
            if(steps||si==seedSource)for(int j=-steps;j<=steps;++j)planes.push_back({{n,o[k]+j*spacing},roi});}
        planes.insert(planes.end(),cuts[si].begin(),cuts[si].end());
        for(const Cut&cut:planes){cancelled(stop);cells.apply(cut.support,[&](const Box&a,const Box&b){return overlaps(a,b,tol);},[](const Polygon&p){return bounds(p);},[&](const Polygon&cell,auto&&emit){
            if(cut.finite&&!cut.finite->intersects(cell,eps))return false;
            const Plane&plane=cut.plane;bool positive=false,negative=false;for(D3 p:cell){const double d=distance(plane,p);positive|=d>eps;negative|=d< -eps;}if(!positive||!negative)return false;
            auto a=clip(cell,plane,eps),b=clip(cell,{plane.n*-1,-plane.d},eps);if(a.size()>=3)emit(std::move(a));if(b.size()>=3)emit(std::move(b));return true;
        },[&]{cancelled(stop);});if(cells.size()+allCells.size()>analysisLimit)throw Failure(DecalPatchStatus::LimitExceeded,"Analysis triangle budget exceeded.");}
        cells.forEach([&](Polygon&cell){cancelled(stop);
            // Charge the exact minimum triangulation work before allocating
            // node requests. Shared-edge insertion is charged again at emission.
            if(cell.size()-2>analysisLimit-baseTriangles)throw Failure(DecalPatchStatus::LimitExceeded,"Analysis triangle budget exceeded.");baseTriangles+=cell.size()-2;
            // A global positional tolerance must not flatten a real thin cell
            // or merge its distinct corners. Bound movement by its local edge
            // and vertex-to-nonincident-edge separation. Exact collinear chains
            // carry no additional width constraint; retain their IDs below.
            double feature=INFINITY;
            for(size_t i=0;i<cell.size();++i){
                feature=std::min(feature,length(cell[(i+1)%cell.size()]-cell[i]));
                for(size_t j=0;j<cell.size();++j){if(i==j||i==(j+1)%cell.size())continue;const D3 a=cell[j],e=cell[(j+1)%cell.size()]-a;const double squared=dot(e,e);if(squared==0)continue;const double t=std::clamp(dot(cell[i]-a,e)/squared,0.,1.);const double separation=length(cell[i]-a-e*t);if(separation>0)feature=std::min(feature,separation);}
            }
            const double cellWeld=std::min(weld,feature*.125);
            allCells.push_back({std::move(cell),si,cellWeld,{}});if(allCells.size()>analysisLimit)throw Failure(DecalPatchStatus::LimitExceeded,"Analysis triangle budget exceeded.");
        });
    }
    struct NodeRequest {size_t cell,corner;double radius;};std::vector<NodeRequest> requests;requests.reserve(baseTriangles+2*allCells.size());
    for(size_t i=0;i<allCells.size();++i){auto&cell=allCells[i];cell.corners.resize(cell.polygon.size());for(size_t j=0;j<cell.polygon.size();++j)requests.push_back({i,j,cell.weldRadius});}
    // Register the most constrained features first. A later coarse cell cannot
    // absorb their nodes under a larger tolerance, and registration is performed
    // once before building the immutable shared-endpoint search lists.
    std::stable_sort(requests.begin(),requests.end(),[](const NodeRequest&a,const NodeRequest&b){return a.radius<b.radius;});
    std::map<std::tuple<int64_t,int64_t,int64_t>,std::vector<size_t>> buckets;std::vector<double> nodeRadii;
    for(const auto&request:requests){cancelled(stop);auto&cell=allCells[request.cell];const D3 p=cell.polygon[request.corner];const int64_t ix=static_cast<int64_t>(std::floor((p.x-o.x)/weld)),iy=static_cast<int64_t>(std::floor((p.y-o.y)/weld)),iz=static_cast<int64_t>(std::floor((p.z-o.z)/weld));size_t found=SIZE_MAX;double nearest=weld;
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)for(int dz=-1;dz<=1;++dz){auto it=buckets.find({ix+dx,iy+dy,iz+dz});if(it!=buckets.end())for(size_t id:it->second){const double d=length(p-patch.positions[id]);if(d<=std::min(request.radius,nodeRadii[id])&&(d<nearest||(d==nearest&&id<found))){found=id;nearest=d;}}}
        if(found==SIZE_MAX){found=patch.positions.size();patch.positions.push_back(p);nodeRadii.push_back(request.radius);buckets[{ix,iy,iz}].push_back(found);}else nodeRadii[found]=std::min(nodeRadii[found],request.radius);
        cell.corners[request.corner]=found;
    }
    // Local cuts can end on a neighboring cell's edge. Insert every welded
    // endpoint on that edge before triangulation, preserving a conforming mesh.
    const EndpointIndex endpointIndex(patch.positions);
    for(const auto&cell:allCells){cancelled(stop);std::vector<size_t> boundary;
        const SurfaceEndpointProjection projection(patch.sources[cell.source].geometricNormal);
        struct Endpoint {size_t id,edge;double along,distanceSquared;};std::vector<Endpoint> endpoints;
        std::vector<std::vector<std::pair<double,size_t>>> edgePoints(cell.corners.size());
        std::vector<std::array<D3,2>> edgeKeys(cell.corners.size());
        for(size_t j=0;j<cell.polygon.size();++j){const size_t first=cell.corners[j],last=cell.corners[(j+1)%cell.polygon.size()];if(first==last)continue;const D3 a=patch.positions[first],b=patch.positions[last],e=b-a;const double len=length(e);if(len==0)continue;const bool reverse=positionLess(b,a);const D3 origin=reverse?b:a,direction=(reverse?e*-1:e)/len;edgeKeys[j]={origin,reverse?a:b};edgePoints[j].push_back({0,first});
            endpointIndex.querySegment(a,b,std::min(eps*4,cell.weldRadius),[&](size_t id){if(std::find(cell.corners.begin(),cell.corners.end(),id)!=cell.corners.end())return;const D3 q=patch.positions[id];for(const auto corner:cell.corners)if(projection.coincident(q,patch.positions[corner]))return;const double along=dot(q-origin,direction),t=reverse?len-along:along;const D3 residual=q-origin-direction*along;const double squared=dot(residual,residual),limit=std::min({eps*4,cell.weldRadius,nodeRadii[id]});if(t>0&&t<len&&squared<=limit*limit)endpoints.push_back({id,j,t,squared});});
        }
        // Thin cells can place the same endpoint within the tolerance bands of
        // two converging edges. It belongs to its closest boundary edge once;
        // inserting it twice creates a backtracking spur and destroys the cell.
        // Original cell corners already own their two incident edges.
        std::sort(endpoints.begin(),endpoints.end(),[&](const Endpoint&a,const Endpoint&b){if(a.id!=b.id)return a.id<b.id;if(a.distanceSquared!=b.distanceSquared)return a.distanceSquared<b.distanceSquared;for(size_t i=0;i<2;++i){const D3 x=edgeKeys[a.edge][i],y=edgeKeys[b.edge][i];if(positionLess(x,y))return true;if(positionLess(y,x))return false;}return a.edge<b.edge;});
        for(size_t i=0;i<endpoints.size();){const auto&endpoint=endpoints[i];edgePoints[endpoint.edge].push_back({endpoint.along,endpoint.id});const size_t id=endpoint.id;do{++i;}while(i<endpoints.size()&&endpoints[i].id==id);}
        for(auto&points:edgePoints){std::sort(points.begin(),points.end());for(const auto&[t,id]:points)if(boundary.empty()||boundary.back()!=id)boundary.push_back(id);}
        if(boundary.size()>1&&boundary.front()==boundary.back())boundary.pop_back();
        if(boundary.size()<3)continue;double area=0;for(size_t j=1;j+1<boundary.size();++j)area+=dot(cross(patch.positions[boundary[j]]-patch.positions[boundary[0]],patch.positions[boundary[j+1]]-patch.positions[boundary[0]]),patch.sources[cell.source].geometricNormal);if(std::abs(area)<=eps*eps)continue;
        if(!triangulateConvexPolygon(boundary,patch.positions,patch.sources[cell.source].geometricNormal,eps,[&](std::array<size_t,3> triangle){allFaces.push_back({triangle,cell.source});if(allFaces.size()>analysisLimit)throw Failure(DecalPatchStatus::LimitExceeded,"Analysis triangle budget exceeded.");}))throw Failure(DecalPatchStatus::InvalidChart,"Contact cell triangulation failed.");
    }
    std::map<std::pair<size_t,size_t>,std::vector<EdgeUse>> edges;size_t seedFace=SIZE_MAX,seedCorner=0;double seedDistance=INFINITY;
    for(size_t i=0;i<allFaces.size();++i){const auto&f=allFaces[i];for(size_t j=0;j<3;++j){const double distance=length(patch.positions[f.v[j]]-o);if(f.source==seedSource&&distance<=tol&&distance<seedDistance){seedFace=i;seedCorner=j;seedDistance=distance;}edges[std::minmax(f.v[j],f.v[(j+1)%3])].push_back({i,j});}}
    if(seedFace==SIZE_MAX)throw Failure(DecalPatchStatus::InvalidChart,"Seed insertion failed in the derived patch.");std::vector<std::vector<size_t>> adjacent(allFaces.size());Dsu corners(allFaces.size()*3);std::vector<std::pair<EdgeUse,EdgeUse>> joinedEdges;
    const auto blocked=[&](const EdgeUse&use){const auto&f=allFaces[use.face];const D3 p=patch.positions[f.v[use.corner]],q=patch.positions[f.v[(use.corner+1)%3]];for(const auto&segment:barriers[f.source]){const D3 e=segment.b-segment.a;const double len=length(e);if(len<=eps)continue;const D3 n=e/len;const double u=dot(p-segment.a,n),v=dot(q-segment.a,n);if(std::min(u,v)>=-4*weld&&std::max(u,v)<=len+4*weld&&length(p-segment.a-n*u)<=4*weld&&length(q-segment.a-n*v)<=4*weld)return true;}return false;};
    for(const auto&[edge,uses]:edges){const bool barrier=std::any_of(uses.begin(),uses.end(),blocked);if(uses.size()<2&&!barrier)continue;bool good=uses.size()==2&&!barrier;if(good){const auto&a=uses[0];const auto&b=uses[1];const auto&fa=allFaces[a.face];const auto&fb=allFaces[b.face];good=fa.v[a.corner]==fb.v[(b.corner+1)%3]&&dot(patch.sources[fa.source].geometricNormal,patch.sources[fb.source].geometricNormal)>-.999999;
            if(good){adjacent[a.face].push_back(b.face);adjacent[b.face].push_back(a.face);joinedEdges.emplace_back(a,b);corners.join(3*a.face+a.corner,3*b.face+(b.corner+1)%3);corners.join(3*a.face+(a.corner+1)%3,3*b.face+b.corner);if(patch.sources[fa.source].receiver!=patch.sources[fb.source].receiver)++stats.joinedEdges;}}
        if(!good)++stats.ambiguousEdges;
    }
    // The Euclidean extraction box is only a broad phase. A small mark on a
    // closed cylinder can otherwise collect the whole circumference and both
    // caps, making its local log map solve through the back-side cut locus.
    // Bound the analysis sheet by intrinsic edge distance, on the proven
    // oriented topology rather than coincident position vertices. The halo
    // covers the edge-path approximation and keeps the footprint interior
    // away from the artificial frontier.
    struct Step {double distance;size_t corner;bool operator>(const Step&other)const{return distance>other.distance;}};
    std::vector<std::vector<std::pair<size_t,double>>> graph(allFaces.size()*3);
    for(size_t i=0;i<allFaces.size();++i)for(size_t j=0;j<3;++j){const auto a=corners.find(i*3+j),b=corners.find(i*3+(j+1)%3);const double weight=length(patch.positions[allFaces[i].v[j]]-patch.positions[allFaces[i].v[(j+1)%3]]);graph[a].push_back({b,weight});graph[b].push_back({a,weight});}
    std::vector<double>distances(graph.size(),INFINITY);std::priority_queue<Step,std::vector<Step>,std::greater<Step>>frontier;const size_t seedNode=corners.find(seedFace*3+seedCorner);distances[seedNode]=0;frontier.push({0,seedNode});
    while(!frontier.empty()){cancelled(stop);const auto current=frontier.top();frontier.pop();if(current.distance!=distances[current.corner]||current.distance>intrinsicRadius+spacing)continue;for(const auto[next,weight]:graph[current.corner])if(current.distance+weight<distances[next]){distances[next]=current.distance+weight;frontier.push({distances[next],next});}}
    const auto insideFrontier=[&](size_t face){for(size_t j=0;j<3;++j)if(distances[corners.find(face*3+j)]<=intrinsicRadius)return true;return false;};
    std::vector<bool>visited(allFaces.size());std::queue<size_t>queue;queue.push(seedFace);visited[seedFace]=true;while(!queue.empty()){const size_t f=queue.front();queue.pop();for(size_t n:adjacent[f])if(!visited[n]&&insideFrontier(n)){visited[n]=true;queue.push(n);}}
    // Clipping the neighborhood can separate vertex fans that were joined
    // through faces outside the intrinsic frontier. Reusing their old roots
    // would give the chart a bow-tie vertex with multiple boundary loops.
    // Only retain proven joins whose two incident faces survived; a rejected
    // ambiguous branch must not become a new seam after pruning.
    Dsu retainedCorners(allFaces.size()*3);
    for(const auto&[a,b]:joinedEdges)if(visited[a.face]&&visited[b.face]) {
        retainedCorners.join(3*a.face+a.corner,3*b.face+(b.corner+1)%3);
        retainedCorners.join(3*a.face+(a.corner+1)%3,3*b.face+b.corner);
    }
    std::vector<D3> compact;std::map<size_t,size_t> remap;
    for(size_t i=0;i<allFaces.size();++i)if(visited[i]){AnalysisFace f=allFaces[i];for(size_t j=0;j<3;++j){const size_t root=retainedCorners.find(i*3+j);auto[it,inserted]=remap.try_emplace(root,compact.size());if(inserted)compact.push_back(patch.positions[f.v[j]]);f.v[j]=it->second;if(i==seedFace&&j==seedCorner)patch.seedVertex=f.v[j];}if(i==seedFace)patch.seedFace=patch.faces.size();patch.faces.push_back(f);}
    patch.positions=std::move(compact);stats.analysisTriangles=static_cast<uint32_t>(patch.faces.size());stats.topologyMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return patch;
}
}
