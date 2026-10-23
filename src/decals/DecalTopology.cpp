#include "DecalInternal.h"
#include <chrono>
#include <map>
#include <numeric>
#include <queue>
#include <set>
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
bool overlaps(const Box&a,const Box&b,double e){for(size_t k=0;k<3;++k)if(a.lo[k]>b.hi[k]+e||b.lo[k]>a.hi[k]+e)return false;return true;}
void addPlane(std::vector<Plane>&v,D3 n,D3 p){if(length(n)>1e-15){n=unit(n);v.push_back({n,dot(n,p)});}}
void contactCuts(const Source&a,const Source&b,std::vector<Plane>&ca,std::vector<Plane>&cb,double tol){
    const D3 na=unit(cross(a.p[1]-a.p[0],a.p[2]-a.p[0])),nb=unit(cross(b.p[1]-b.p[0],b.p[2]-b.p[0]));
    if(length(cross(na,nb))<1e-8){
        if(std::abs(dot(na,b.p[0]-a.p[0]))>tol)return;
        Polygon intersection{a.p[0],a.p[1],a.p[2]};for(size_t i=0;i<3;++i){D3 n=unit(cross(b.p[(i+1)%3]-b.p[i],nb));intersection=clip(intersection,{n,dot(n,b.p[i])},tol*1e-5);if(intersection.size()<3)break;}
        double overlap=0;if(intersection.size()>=3)for(size_t j=1;j+1<intersection.size();++j)overlap+=length(cross(intersection[j]-intersection[0],intersection[j+1]-intersection[0]));
        // Cut actual coplanar overlap, not arbitrary extensions of unrelated
        // edges. The latter creates tiny cells on otherwise regular meshes.
        for(size_t i=0;i<3;++i){if(overlap>tol*tol){addPlane(ca,cross(na,b.p[(i+1)%3]-b.p[i]),b.p[i]);addPlane(cb,cross(nb,a.p[(i+1)%3]-a.p[i]),a.p[i]);}
            const D3 ea=a.p[(i+1)%3]-a.p[i],eb=b.p[(i+1)%3]-b.p[i];
            for(D3 q:b.p){double t=dot(q-a.p[i],ea)/dot(ea,ea);if(t>1e-7&&t<1-1e-7&&length(q-a.p[i]-ea*t)<=tol)addPlane(ca,ea,q);}
            for(D3 q:a.p){double t=dot(q-b.p[i],eb)/dot(eb,eb);if(t>1e-7&&t<1-1e-7&&length(q-b.p[i]-eb*t)<=tol)addPlane(cb,eb,q);}
        }return;
    }
    std::vector<D3> pts;
    const auto collect=[&](const Source&s,const Source&t,D3 nt){for(size_t i=0;i<3;++i){D3 p=s.p[i],q=s.p[(i+1)%3];const double x=dot(nt,p-t.p[0]),y=dot(nt,q-t.p[0]);if(std::abs(x)<=tol&&inside(t,p,tol))pts.push_back(p);if(x*y<0){D3 at=p+(q-p)*(x/(x-y));if(inside(t,at,tol))pts.push_back(at);}}};
    collect(a,b,nb);collect(b,a,na);double best=tol;D3 p{},q{};
    for(D3 x:pts)for(D3 y:pts)if(length(x-y)>best){best=length(x-y);p=x;q=y;}
    if(best<=tol)return;D3 dir=unit(q-p);addPlane(ca,cross(na,dir),p);addPlane(cb,cross(nb,dir),p);
    for(D3 end:{p,q}){bool va=false,vb=false;for(D3 v:a.p)va|=length(v-end)<=tol;for(D3 v:b.p)vb|=length(v-end)<=tol;if(!va)addPlane(ca,dir,end);if(!vb)addPlane(cb,dir,end);}
}
struct Dsu {std::vector<size_t> p;explicit Dsu(size_t n):p(n){std::iota(p.begin(),p.end(),size_t{});}size_t find(size_t a){while(p[a]!=a){p[a]=p[p[a]];a=p[a];}return a;}void join(size_t a,size_t b){a=find(a);b=find(b);if(a!=b)p[std::max(a,b)]=std::min(a,b);}};
struct EdgeUse {size_t face{},corner{};};
}
AnalysisPatch buildTopology(const DecalSurfaceSnapshot&snapshot,const DecalBuildRequest&r,DecalBuildStatistics&stats,std::stop_token stop){
    const auto started=std::chrono::steady_clock::now();AnalysisPatch patch;
    if(r.limits.maximumTriangles<4||r.limits.maximumInputTriangles<1||!std::isfinite(r.limits.maximumStretch)||r.limits.maximumStretch<1)throw Failure(DecalPatchStatus::InvalidInput,"Invalid surface preparation limits.");
    for(float f:r.frame)if(!std::isfinite(f))throw Failure(DecalPatchStatus::InvalidInput,"Footprint frame is nonfinite.");
    if(std::abs(r.frame.at(3,0))+std::abs(r.frame.at(3,1))+std::abs(r.frame.at(3,2))+std::abs(r.frame.at(3,3)-1)>1e-6)throw Failure(DecalPatchStatus::InvalidInput,"Footprint frame must be affine.");
    const SurfaceReceiver*seedReceiver=nullptr;const SurfaceTriangle*seedTriangle=nullptr;
    for(const auto&rec:snapshot.receivers)if(rec.id==r.seed.receiver){if(seedReceiver)throw Failure(DecalPatchStatus::AmbiguousSeed,"Receiver occurrence identity is duplicated.");seedReceiver=&rec;for(const auto&t:rec.triangles)if(t.triangle==r.seed.triangle){if(seedTriangle)throw Failure(DecalPatchStatus::AmbiguousSeed,"Source triangle identity is duplicated.");seedTriangle=&t;}}
    if(!seedTriangle)throw Failure(DecalPatchStatus::MissingSeed,"Seed receiver triangle is unavailable.");
    const D3 bary=d3(r.seed.barycentrics);if(!finite(bary)||std::abs(bary.x+bary.y+bary.z-1)>1e-4||std::min({bary.x,bary.y,bary.z})< -1e-5)throw Failure(DecalPatchStatus::InvalidInput,"Invalid seed barycentrics.");
    const auto&sm=seedReceiver->localToWorld;
    patch.seedPosition=point(sm,d3(seedTriangle->positions[0])*bary.x+d3(seedTriangle->positions[1])*bary.y+d3(seedTriangle->positions[2])*bary.z);
    patch.seedNormal=normal(sm,cross(d3(seedTriangle->positions[1])-d3(seedTriangle->positions[0]),d3(seedTriangle->positions[2])-d3(seedTriangle->positions[0])));
    const D3 fx=column(r.frame,0),fy=column(r.frame,1),px=fx-patch.seedNormal*dot(fx,patch.seedNormal),py=fy-patch.seedNormal*dot(fy,patch.seedNormal);
    patch.basisX=unit(px);patch.basisY=cross(patch.seedNormal,patch.basisX);patch.ax=dot(px,patch.basisX);patch.ay=dot(px,patch.basisY);patch.bx=dot(py,patch.basisX);patch.by=dot(py,patch.basisY);patch.frameDet=patch.ax*patch.by-patch.ay*patch.bx;patch.scale=std::min(length(px),length(py));
    if(!finite(patch.seedNormal)||length(patch.seedNormal)<.5||patch.scale<1e-6||std::abs(patch.frameDet)<1e-10*length(px)*length(py))throw Failure(DecalPatchStatus::InvalidInput,"Footprint has a singular surface frame.");
    const double tol=r.limits.seamTolerance>0?r.limits.seamTolerance:std::min(.001,.001*patch.scale),spacing=r.limits.sampleSpacing>0?r.limits.sampleSpacing:patch.scale/8;
    if(!std::isfinite(tol)||!std::isfinite(spacing)||tol<=0||spacing<=tol*4)throw Failure(DecalPatchStatus::InvalidInput,"Invalid sampling or seam tolerance.");patch.tolerance=tol;
    const double radius=.5*std::max(length(px+py),length(px-py))*r.limits.maximumStretch+patch.scale*.5;
    if(radius/spacing>128)throw Failure(DecalPatchStatus::LimitExceeded,"Sampling extent exceeds the bounded grid budget.");
    const D3 o=patch.seedPosition;const Box roi{o-D3{radius,radius,radius},o+D3{radius,radius,radius}};size_t seedSource=SIZE_MAX;
    for(size_t ri=0;ri<snapshot.receivers.size();++ri){const auto&rec=snapshot.receivers[ri];const Mat4&m=rec.localToWorld;
        for(float f:m)if(!std::isfinite(f))throw Failure(DecalPatchStatus::InvalidInput,"Receiver frame is nonfinite.");
        if(std::abs(dot(column(m,0),cross(column(m,1),column(m,2))))<1e-15)throw Failure(DecalPatchStatus::InvalidInput,"Receiver frame is singular.");
        for(size_t ti=0;ti<rec.triangles.size();++ti){cancelled(stop);const auto&t=rec.triangles[ti];Source s;s.receiver=ri;s.triangle=ti;D3 ng=cross(d3(t.positions[1])-d3(t.positions[0]),d3(t.positions[2])-d3(t.positions[0]));if(length(ng)<1e-16)continue;s.geometricNormal=normal(m,ng);
            for(size_t j=0;j<3;++j){s.p[j]=point(m,d3(t.positions[j]));s.n[j]=normal(m,length(d3(t.normals[j]))>1e-10?d3(t.normals[j]):ng);if(!finite(s.p[j])||!finite(s.n[j]))throw Failure(DecalPatchStatus::InvalidInput,"Receiver geometry is nonfinite.");}
            if(!overlaps(bounds(s),roi,tol))continue;if(&rec==seedReceiver&&&t==seedTriangle)seedSource=patch.sources.size();patch.sources.push_back(s);if(patch.sources.size()>r.limits.maximumInputTriangles)throw Failure(DecalPatchStatus::LimitExceeded,"Input triangle budget exceeded.");
        }
    }
    if(seedSource==SIZE_MAX)throw Failure(DecalPatchStatus::MissingSeed,"Seed geometry is degenerate or outside its footprint.");stats.inputTriangles=static_cast<uint32_t>(patch.sources.size());
    std::vector<std::vector<Plane>> cuts(patch.sources.size());std::vector<Box> boxes;for(const auto&s:patch.sources)boxes.push_back(bounds(s));std::vector<size_t> order(patch.sources.size());std::iota(order.begin(),order.end(),size_t{});std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return boxes[a].lo.x<boxes[b].lo.x;});size_t pairs=0;
    size_t cutCount=0;
    for(size_t a=0;a<order.size();++a)for(size_t b=a+1;b<order.size()&&boxes[order[b]].lo.x<=boxes[order[a]].hi.x+tol;++b){cancelled(stop);if(++pairs>1000000)throw Failure(DecalPatchStatus::LimitExceeded,"Local contact-pair budget exceeded.");const size_t i=order[a],j=order[b];if(!overlaps(boxes[i],boxes[j],tol))continue;const size_t before=cuts[i].size()+cuts[j].size();contactCuts(patch.sources[i],patch.sources[j],cuts[i],cuts[j],tol);cutCount+=cuts[i].size()+cuts[j].size()-before;if(cutCount>static_cast<size_t>(r.limits.maximumTriangles)*8)throw Failure(DecalPatchStatus::LimitExceeded,"Local contact-plane budget exceeded.");}
    const double eps=std::max(1e-11,patch.scale*1e-10),weld=std::min(tol,std::max(1e-9,patch.scale*1e-7));std::vector<AnalysisFace> allFaces;std::map<std::tuple<int64_t,int64_t,int64_t>,std::vector<size_t>> buckets;
    const auto node=[&](D3 p){const int64_t ix=static_cast<int64_t>(std::floor((p.x-o.x)/weld)),iy=static_cast<int64_t>(std::floor((p.y-o.y)/weld)),iz=static_cast<int64_t>(std::floor((p.z-o.z)/weld));size_t found=SIZE_MAX;double nearest=weld;
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)for(int dz=-1;dz<=1;++dz){auto it=buckets.find({ix+dx,iy+dy,iz+dz});if(it!=buckets.end())for(size_t id:it->second){double d=length(p-patch.positions[id]);if(d<nearest||(d==nearest&&id<found)){found=id;nearest=d;}}}
        if(found!=SIZE_MAX)return found;const size_t id=patch.positions.size();patch.positions.push_back(p);buckets[{ix,iy,iz}].push_back(id);return id;};
    for(size_t si=0;si<patch.sources.size();++si){cancelled(stop);const Source&s=patch.sources[si];Polygon poly{s.p[0],s.p[1],s.p[2]};for(size_t k=0;k<3;++k){D3 n{};if(k==0)n.x=1;if(k==1)n.y=1;if(k==2)n.z=1;poly=clip(poly,{n,o[k]+radius},eps);poly=clip(poly,{n*-1,-o[k]+radius},eps);if(poly.size()<3)break;}if(poly.size()<3)continue;
        const size_t omittedAxis=std::abs(patch.seedNormal.x)>std::abs(patch.seedNormal.y)?(std::abs(patch.seedNormal.x)>std::abs(patch.seedNormal.z)?0:2):(std::abs(patch.seedNormal.y)>std::abs(patch.seedNormal.z)?1:2);
        std::vector<Polygon> cells{std::move(poly)};std::vector<Plane> planes=std::move(cuts[si]);for(size_t k=0;k<3;++k){if(k==omittedAxis)continue;D3 n{};if(k==0)n.x=1;if(k==1)n.y=1;if(k==2)n.z=1;const int steps=static_cast<int>(std::ceil(radius/spacing));for(int j=-steps;j<=steps;++j)planes.push_back({n,o[k]+j*spacing});}
        for(const Plane&plane:planes){std::vector<Polygon> next;for(auto&cell:cells){bool positive=false,negative=false;for(D3 p:cell){double d=distance(plane,p);positive|=d>eps;negative|=d< -eps;}if(positive&&negative){auto a=clip(cell,plane,eps),b=clip(cell,{plane.n*-1,-plane.d},eps);if(a.size()>=3)next.push_back(std::move(a));if(b.size()>=3)next.push_back(std::move(b));}else next.push_back(std::move(cell));}cells=std::move(next);if(cells.size()+allFaces.size()>r.limits.maximumTriangles)throw Failure(DecalPatchStatus::LimitExceeded,"Derived triangle budget exceeded.");}
        for(auto&cell:cells)for(size_t j=1;j+1<cell.size();++j){if(length(cross(cell[j]-cell[0],cell[j+1]-cell[0]))<=eps*eps)continue;AnalysisFace f{{node(cell[0]),node(cell[j]),node(cell[j+1])},si};if(f.v[0]==f.v[1]||f.v[1]==f.v[2]||f.v[2]==f.v[0])continue;if(dot(cross(cell[j]-cell[0],cell[j+1]-cell[0]),s.geometricNormal)<0)std::swap(f.v[1],f.v[2]);allFaces.push_back(f);if(allFaces.size()>r.limits.maximumTriangles)throw Failure(DecalPatchStatus::LimitExceeded,"Derived triangle budget exceeded.");}
    }
    std::map<std::pair<size_t,size_t>,std::vector<EdgeUse>> edges;size_t seedFace=SIZE_MAX,seedCorner=0;
    for(size_t i=0;i<allFaces.size();++i){const auto&f=allFaces[i];for(size_t j=0;j<3;++j){if(f.source==seedSource&&length(patch.positions[f.v[j]]-o)<=tol){seedFace=i;seedCorner=j;}edges[std::minmax(f.v[j],f.v[(j+1)%3])].push_back({i,j});}}
    if(seedFace==SIZE_MAX)throw Failure(DecalPatchStatus::InvalidChart,"Seed insertion failed in the derived patch.");std::vector<std::vector<size_t>> adjacent(allFaces.size());Dsu corners(allFaces.size()*3);std::set<size_t> ambiguousFaces;
    for(const auto&[edge,uses]:edges){if(uses.size()<2)continue;bool good=uses.size()==2;if(good){const auto&a=uses[0];const auto&b=uses[1];const auto&fa=allFaces[a.face];const auto&fb=allFaces[b.face];good=fa.v[a.corner]==fb.v[(b.corner+1)%3]&&dot(patch.sources[fa.source].geometricNormal,patch.sources[fb.source].geometricNormal)>-.999999;
            if(good){adjacent[a.face].push_back(b.face);adjacent[b.face].push_back(a.face);corners.join(3*a.face+a.corner,3*b.face+(b.corner+1)%3);corners.join(3*a.face+(a.corner+1)%3,3*b.face+b.corner);if(patch.sources[fa.source].receiver!=patch.sources[fb.source].receiver)++stats.joinedEdges;}}
        if(!good){++stats.ambiguousEdges;for(auto use:uses)ambiguousFaces.insert(use.face);}
    }
    // The Euclidean extraction box is only a broad phase. A small mark on a
    // closed cylinder can otherwise collect the whole circumference and both
    // caps, making its local log map solve through the back-side cut locus.
    // Bound the analysis sheet by intrinsic edge distance, on the proven
    // oriented topology rather than coincident position vertices. The halo
    // covers the edge-path approximation and keeps the footprint interior
    // away from the artificial frontier.
    const double intrinsicRadius=.5*std::max(length(px+py),length(px-py))*r.limits.maximumStretch+2*spacing;
    struct Step {double distance;size_t corner;bool operator>(const Step&other)const{return distance>other.distance;}};
    std::vector<std::vector<std::pair<size_t,double>>> graph(allFaces.size()*3);
    for(size_t i=0;i<allFaces.size();++i)for(size_t j=0;j<3;++j){const auto a=corners.find(i*3+j),b=corners.find(i*3+(j+1)%3);const double weight=length(patch.positions[allFaces[i].v[j]]-patch.positions[allFaces[i].v[(j+1)%3]]);graph[a].push_back({b,weight});graph[b].push_back({a,weight});}
    std::vector<double>distances(graph.size(),INFINITY);std::priority_queue<Step,std::vector<Step>,std::greater<Step>>frontier;const size_t seedNode=corners.find(seedFace*3+seedCorner);distances[seedNode]=0;frontier.push({0,seedNode});
    while(!frontier.empty()){cancelled(stop);const auto current=frontier.top();frontier.pop();if(current.distance!=distances[current.corner]||current.distance>intrinsicRadius+spacing)continue;for(const auto[next,weight]:graph[current.corner])if(current.distance+weight<distances[next]){distances[next]=current.distance+weight;frontier.push({distances[next],next});}}
    const auto insideFrontier=[&](size_t face){for(size_t j=0;j<3;++j)if(distances[corners.find(face*3+j)]<=intrinsicRadius)return true;return false;};
    std::vector<bool>visited(allFaces.size());std::queue<size_t>queue;queue.push(seedFace);visited[seedFace]=true;while(!queue.empty()){const size_t f=queue.front();queue.pop();if(ambiguousFaces.contains(f))patch.incomplete=true;for(size_t n:adjacent[f])if(!visited[n]&&insideFrontier(n)){visited[n]=true;queue.push(n);}}
    std::vector<D3> compact;std::map<size_t,size_t> remap;
    for(size_t i=0;i<allFaces.size();++i)if(visited[i]){AnalysisFace f=allFaces[i];for(size_t j=0;j<3;++j){const size_t root=corners.find(i*3+j);auto[it,inserted]=remap.try_emplace(root,compact.size());if(inserted)compact.push_back(patch.positions[f.v[j]]);f.v[j]=it->second;if(i==seedFace&&j==seedCorner)patch.seedVertex=f.v[j];}if(i==seedFace)patch.seedFace=patch.faces.size();patch.faces.push_back(f);}
    patch.positions=std::move(compact);stats.analysisTriangles=static_cast<uint32_t>(patch.faces.size());stats.topologyMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return patch;
}
}
