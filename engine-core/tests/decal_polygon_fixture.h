#pragma once
#include "decals/DecalPolygon.h"
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>

namespace crimson::decals::test {
inline void polygonCheck(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
inline double polygonProductDifference(double a,double b,double c,double d){const double product=c*d;return std::fma(a,b,-product)+std::fma(-c,d,product);}
inline double conformingPolygon(std::vector<detail::D3> positions,std::vector<size_t> boundary,detail::D3 normal,double expectedArea,double epsilon=1e-10,double minimumTwiceArea=0){
    using namespace detail;
    std::vector<std::array<size_t,3>> triangles;
    polygonCheck(triangulateConvexPolygon(boundary,positions,normal,epsilon,[&](auto triangle){triangles.push_back(triangle);}),"Convex polygon with collinear contact vertices failed triangulation.");
    polygonCheck(triangles.size()==boundary.size()-2,"Triangulation did not preserve all boundary vertices.");
    std::map<std::pair<size_t,size_t>,unsigned> edges;std::set<size_t> used;double area=0,smallest=INFINITY;
    for(const auto& triangle:triangles){
        const D3 a=positions[triangle[1]]-positions[triangle[0]],b=positions[triangle[2]]-positions[triangle[0]];
        // Validate in 3D with compensated products. The tiny mandatory tip
        // fixtures lose significant area in an ordinary subtract-of-products
        // oracle even though all input coordinates are exactly representable.
        const D3 crossValue{polygonProductDifference(a.y,b.z,a.z,b.y),polygonProductDifference(a.z,b.x,a.x,b.z),polygonProductDifference(a.x,b.y,a.y,b.x)};
        const double oriented=dot(crossValue,unit(normal));
        polygonCheck(oriented>minimumTwiceArea,"Triangulation emitted a degenerate, reversed or avoidable needle triangle.");area+=oriented*.5;smallest=std::min(smallest,oriented);
        for(size_t i=0;i<3;++i){used.insert(triangle[i]);++edges[std::minmax(triangle[i],triangle[(i+1)%3])];}
    }
    if(std::abs(area-expectedArea)>std::max(1e-28,std::abs(expectedArea)*1e-10)){std::ostringstream message;message.precision(17);message<<"Triangulation changed the surface area: observed="<<area<<", expected="<<expectedArea<<", vertices="<<boundary.size()<<", first={"<<positions[boundary[0]].x<<','<<positions[boundary[0]].y<<','<<positions[boundary[0]].z<<"}.";throw std::runtime_error(message.str());}
    polygonCheck(used.size()==boundary.size(),"Triangulation discarded a collinear contact vertex.");
    for(size_t i=0;i<boundary.size();++i){const auto edge=std::minmax(boundary[i],boundary[(i+1)%boundary.size()]);polygonCheck(edges[edge]==1,"Triangulation skipped a boundary segment and left a T junction.");edges.erase(edge);}
    for(const auto&[edge,count]:edges){(void)edge;polygonCheck(count==2,"Triangulation introduced a hole or overlapping boundary.");}
    return smallest;
}
inline void runPolygonProofs(){
    using namespace detail;
    std::vector<D3> rectangle{{0,0,0},{1,0,0},{2,0,0},{2,1,0},{2,2,0},{1,2,0},{0,2,0},{0,1,0}};
    std::vector<size_t> boundary{0,1,2,3,4,5,6,7};conformingPolygon(rectangle,boundary,{0,0,1},4);
    std::reverse(boundary.begin(),boundary.end());conformingPolygon(rectangle,boundary,{0,0,1},4);
    for(auto& p:rectangle)p={170+p.x,110,1000+p.y};conformingPolygon(rectangle,boundary,{0,-1,0},4);
    conformingPolygon({{0,0,0},{.5,0,0},{1,0,0},{1.5,0,0},{2,0,0},{1,1,0}}, {0,1,2,3,4,5},{0,0,1},1);
    // Distinct, widely separated vertices surround a strip much narrower than
    // positional epsilon. Expanded ear occupancy incorrectly rejects all four
    // valid ears even though their signed areas exceed epsilon squared.
    for(const double scale:{.001,1.,1000.})for(const bool translated:{false,true}){
        std::vector<D3> thin{{0,0,0},{scale/3,-scale*1e-12,0},{scale,0,0},{2*scale/3,scale*1e-12,0}};
        if(translated)for(auto&p:thin)p={17+p.x,13+p.z,3+p.y};
        const D3 normal=translated?D3{0,-1,0}:D3{0,0,1};
        double twiceArea=0;for(size_t i=1;i+1<thin.size();++i)twiceArea+=dot(cross(thin[i]-thin[0],thin[i+1]-thin[0]),normal);
        std::vector<size_t> ids{0,1,2,3};
        for(size_t rotation=0;rotation<ids.size();++rotation){
            conformingPolygon(thin,ids,normal,std::abs(twiceArea)*.5,scale*1e-10);
            std::reverse(ids.begin(),ids.end());conformingPolygon(thin,ids,normal,std::abs(twiceArea)*.5,scale*1e-10);std::reverse(ids.begin(),ids.end());
            std::rotate(ids.begin(),ids.begin()+1,ids.end());
        }
    }
    // Captured Main contact cell: the corners inserted on two long edges differ
    // from exact collinearity by about 2e-14. All eleven IDs/segments must survive
    // without turning the chain into avoidable 1e-18-area metric triangles.
    const std::vector<D3> contact{
        {5.8802078461481528,.37375002226481868,25.749999992859856},
        {5.8164057877535669,.32281252710459185,25.749999989056956},
        {5.8180131372216799,.36923349281697265,25.749999989152737},
        {5.8180131454739703,.36923373114681818,25.749999989152741},
        {5.818445044779784,.38170717470347881,25.749999989178505},
        {5.8203119719953404,.4356248609192942,25.749999989289758},
        {5.820311977838486,.43562502967193772,25.749999989289783},
        {5.8725052118446115,.38170717470347881,25.749999992400742},
        {5.8762691392152568,.37781887588569774,25.749999992625067},
        {5.87626939954104,.37781860695796904,25.749999992625082},
        {5.8802076704919539,.37375020372522483,25.749999992859824}};
    const D3 contactNormal{5.960464951234247e-8,0,-.99999999999999833};
    std::vector<size_t> contactBoundary(contact.size());std::iota(contactBoundary.begin(),contactBoundary.end(),size_t{});
    conformingPolygon(contact,contactBoundary,contactNormal,.0069986983378956509*.5,1e-10,1e-15);
    std::reverse(contactBoundary.begin(),contactBoundary.end());conformingPolygon(contact,contactBoundary,contactNormal,.0069986983378956509*.5,1e-10,1e-15);
    // A high-quality ear can still be illegal if it strands the last three
    // contact samples on an edge. Keep the final triangle's validity in the
    // local choice rather than failing after publishing a greedy recipe.
    conformingPolygon({
        {5.5491882003325168,0,25.476863030297697},
        {5.4999999566755911,0,25.476863030297697},
        {5.4999999549554737,0,25.505721795962938},
        {5.5000000145601149,0,25.505721795962941},
        {5.5014603951620638,0,25.505721796049986},
        {5.5020772830913334,0,25.505721796086757},
        {5.5392524314068341,0,25.505721798302567},
        {5.5482448763949739,0,25.489583349557908}},
        {0,1,2,3,4,5,6,7},{0,1,0},.0026514531181158507*.5,1e-10,1e-12);
    conformingPolygon({
        {5.5160894086865611,4.0000001192092896,25.249063955790696},
        {5.5143017307212459,4.0000001192092896,25.250000000852452},
        {5.5143017304073547,4.0000001192092896,25.250000000631971},
        {5.5143017099291143,4.0000001192092896,25.24999998624773},
        {5.5149942407985391,4.0000001192092896,25.247567798989014},
        {5.5160894088129426,4.0000001192092896,25.246943691869163}},
        {0,1,2,3,4,5},{0,-1,0},6.0218788750612104e-6*.5,1e-10,1e-15);
    conformingPolygon({
        {5.9999995827675026,2.1250000633299351,25.749999999999979},
        {5.9999995827674866,2.1250003604147327,25.749999999999979},
        {5.9999995827674866,2.1250003864230251,25.749999999999979},
        {5.9999995827674866,2.1317071747034788,25.749999999999979},
        {5.9999995841465577,2.1317071747034788,25.726863030297697},
        {5.9999995841465577,2.1250000633299351,25.726863030297697}},
        {0,1,2,3,4,5},{-.99999999999999822,0,-5.960464832810441e-8},.00031036446527902655*.5,1e-10);
    // Independent endpoint arithmetic can put a contact chain a few ULPs on
    // either side of the source edge. A greedy large ear strands that chain;
    // a valid constrained triangulation retains its vertices with healthy area.
    const std::vector<D3> ceilingChain{
        {5.504438439589336,4.0000001192092896,25.259742602986677},
        {5.504438467089467,4.0000001192092896,25.259742593328454},
        {5.5044384902233494,4.0000001192092896,25.259742585203689},
        {5.5050052683386248,4.0000001192092896,25.259543529130532},
        {5.5044199028522138,4.0000001192092896,25.259937913519913}};
    std::vector<size_t> ceilingBoundary{0,1,2,3,4};
    conformingPolygon(ceilingChain,ceilingBoundary,{0,-1,0},1.0701744553822445e-7*.5,1e-10,1e-14);
    std::reverse(ceilingBoundary.begin(),ceilingBoundary.end());conformingPolygon(ceilingChain,ceilingBoundary,{0,-1,0},1.0701744553822445e-7*.5,1e-10,1e-14);
    const std::vector<D3> pillarChain{
        {5.8229162237710455,2.2400001557668072,25.749999989444984},
        {5.7796949035636498,2.2375593618781231,25.74999998686879},
        {5.7796949230987806,2.2375595181591854,25.749999986868794},
        {5.7796949261155515,2.2375595422933534,25.749999986868794},
        {5.7796949584770347,2.2375598011852342,25.749999986868797},
        {5.7812499832361937,2.25,25.749999986961484},
        {5.782088380024156,2.2567071747034788,25.749999987011456},
        {5.7887305110639096,2.3098442261887246,25.74999998740736},
        {5.8147388459906333,2.2567071747034788,25.749999988957576}};
    std::vector<size_t> pillarBoundary{0,1,2,3,4,5,6,7,8};
    conformingPolygon(pillarChain,pillarBoundary,contactNormal,.0031021932109495365*.5,1e-10,1e-14);
    std::reverse(pillarBoundary.begin(),pillarBoundary.end());conformingPolygon(pillarChain,pillarBoundary,contactNormal,.0031021932109495365*.5,1e-10,1e-14);
    // These valid cells force a tip triangle below the whole-cell epsilon²
    // threshold. Every segment still needs an oriented face; a per-triangle
    // epsilon² floor makes a conforming triangulation mathematically impossible.
    const double forcedCeiling=conformingPolygon({
        {5.8764472888821517,4.0000001192092896,25.490949418127702},
        {5.8764472923758486,4.0000001192092896,25.490949424834319},
        {5.8685457404542767,4.0000001192092896,25.484375030473227},
        {5.868545734708265,4.0000001192092896,25.484375025692326},
        {5.8685457401529169,4.0000001192092896,25.484375030222484}},
        // Exact-coordinate area evaluated independently at 100 decimal digits.
        {0,1,2,3,4},{0,-1,0},1.5011875076672914298236174248252397e-11);
    polygonCheck(forcedCeiling<1e-20,"Ceiling tip proof no longer exercises a mandatory sub-epsilon² triangle.");
    const double forcedPillar=conformingPolygon({
        {5.510167610570031,0,25.541015619467867},
        {5.5101676105747677,0,25.541015619436024},
        {5.5101676105321538,0,25.541015617859884},
        {5.509906900562318,0,25.529947912804591},
        {5.5101676105099138,0,25.541015619360447}},
        {0,1,2,3,4},{0,1,0},6.3735751916450493e-13*.5);
    polygonCheck(forcedPillar<1e-20,"Pillar tip proof no longer exercises a mandatory sub-epsilon² triangle.");
    unsigned emitted=0;
    polygonCheck(!triangulateConvexPolygon({0,1,2},{{0,0,0},{1,0,0},{2,0,0}},{0,0,1},1e-10,[&](auto){++emitted;})&&emitted==0,"Degenerate polygon partially published triangles.");
    polygonCheck(!triangulateConvexPolygon({0,1,2,1},{{0,0,0},{1,0,0},{0,1,0}},{0,0,1},1e-10,[&](auto){++emitted;})&&emitted==0,"Repeated boundary identity partially published triangles.");
}
}
