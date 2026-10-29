#pragma once
#include "decals/DecalEndpointIndex.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>
#include <vector>

namespace crimson::decals::test {
inline void runEndpointIndexProofs() {
    using namespace detail;
    const auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    struct Segment {D3 a,b;double tolerance;};
    std::vector<Segment> segments{
        {{-2,0,0},{2,0,0},4e-10},{{0,-2,0},{0,2,0},1e-10},{{0,0,-2},{0,0,2},1e-10},
        {{-.7,-.7,-.7},{.9,.9,.9},4e-10},{{0,0,0},{1e-8,0,0},1e-12},
        {{-100,-100,0},{0,0,0},1e-10},{{0,0,0},{0,0,0},1e-10}};
    std::mt19937_64 random(0xdecafbad);std::uniform_real_distribution<double> coordinate(-3,3);
    for(unsigned i=0;i<16;++i)segments.push_back({{coordinate(random),coordinate(random),coordinate(random)},
        {coordinate(random),coordinate(random),coordinate(random)},1e-9});
    std::vector<D3> source;
    for(unsigned i=0;i<512;++i)source.push_back({coordinate(random),coordinate(random),coordinate(random)});
    for(int y=-8;y<=8;++y)for(int x=-8;x<=8;++x)source.push_back({x*.25,y*.25,0});
    for(const auto& segment:segments) {
        const D3 edge=segment.b-segment.a;const double extent=length(edge);if(extent==0)continue;
        const D3 direction=edge/extent;
        const D3 perpendicular=unit(cross(direction,std::abs(direction.z)<.9?D3{0,0,1}:D3{0,1,0}));
        for(const double along:{0.,1e-12,.125,.5,.875,1.-1e-12,1.}) {
            const D3 point=segment.a+edge*along;source.push_back(point);
            for(const double offset:{-.999999999,1.,1.000000001,2.})source.push_back(point+perpendicular*(segment.tolerance*offset));
        }
    }
    // Exact inclusive radius boundaries and adjacent representable values.
    for(const double distance:{std::nextafter(4e-10,0.),4e-10,std::nextafter(4e-10,INFINITY)})source.push_back({.5,distance,0});
    source.push_back({.5,0,0});source.push_back({.5,0,0}); // Distinct source IDs at the same point remain distinct.

    const auto check=[&](const std::vector<D3>& positions,const std::vector<Segment>& queries) {
        const EndpointIndex index(positions);
        const auto positionLess=[](D3 a,D3 b){if(a.x!=b.x)return a.x<b.x;if(a.y!=b.y)return a.y<b.y;return a.z<b.z;};
        for(size_t query=0;query<queries.size();++query)for(const bool reverse:{false,true}) {
            const auto& segment=queries[query];const D3 a=reverse?segment.b:segment.a,b=reverse?segment.a:segment.b;
            const D3 edge=b-a;const double extent=length(edge);const bool canonicalReverse=positionLess(b,a);
            const D3 origin=canonicalReverse?b:a,direction=extent>0?(canonicalReverse?edge*-1:edge)/extent:D3{};
            const auto eligible=[&](size_t id) {
                if(extent==0)return false;
                const double along=dot(positions[id]-origin,direction),t=canonicalReverse?extent-along:along;
                const D3 residual=positions[id]-origin-direction*along;
                const double nodeRadius=segment.tolerance*(id%5==0?.25:1.);
                return t>0&&t<extent&&dot(residual,residual)<=nodeRadius*nodeRadius;
            };
            std::vector<size_t> expected,actual,candidates;
            for(size_t id=0;id<positions.size();++id)if(eligible(id))expected.push_back(id);
            index.querySegment(a,b,segment.tolerance,[&](size_t id){candidates.push_back(id);if(eligible(id))actual.push_back(id);});
            std::sort(candidates.begin(),candidates.end());std::sort(actual.begin(),actual.end());
            require(std::adjacent_find(candidates.begin(),candidates.end())==candidates.end(),
                "The endpoint index returned a source point more than once.");
            require(actual==expected,
                "The endpoint index rejected a scalar-eligible boundary point or changed precise endpoint acceptance.");
            if(query==0&&positions.size()>100)require(candidates.size()*4<positions.size(),
                "The endpoint index did not prune a localized axis-aligned segment query.");
        }
    };
    for(const double scale:{1e-4,1.,1e4})for(const D3 offset:{D3{},D3{170,110,1000},D3{1e9,-1e9,1e9}}) {
        std::vector<D3> positions;positions.reserve(source.size());for(const auto point:source)positions.push_back(point*scale+offset);
        auto queries=segments;for(auto& query:queries){query.a=query.a*scale+offset;query.b=query.b*scale+offset;query.tolerance*=scale;}
        check(positions,queries);
    }
    check({},{{{-1,0,0},{1,0,0},0}});
    check({{0,0,0}},{{{-1,0,0},{1,0,0},0}});
    check(std::vector<D3>(65,D3{}),{{{-1,0,0},{1,0,0},0}});
}
} // namespace crimson::decals::test
