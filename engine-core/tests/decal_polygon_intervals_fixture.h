#pragma once
#include "decals/DecalPolygonIntervals.h"
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace crimson::decals::test {
inline void runPolygonIntervalsProofs() {
    using namespace detail;
    const auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    size_t calls=0;
    const auto positive=[&](size_t,size_t,size_t){++calls;return 1.;};
    auto result=triangulatePolygonIntervals(2,positive);
    require(result.status==PolygonIntervalStatus::Invalid&&result.triangles.empty()&&calls==0,
        "An incomplete interval boundary invoked geometry or published triangles.");
    result=triangulatePolygonIntervals(3,positive,8);
    require(result.status==PolygonIntervalStatus::LimitExceeded&&result.triangles.empty()&&calls==0,
        "Interval table storage exceeded its budget before rejecting the request.");
    result=triangulatePolygonIntervals(3,positive,9);
    require(result.status==PolygonIntervalStatus::Ready&&result.triangles==std::vector<std::array<size_t,3>>{{0,1,2}},
        "A budget-admitted base triangle did not reconstruct exactly.");

    calls=0;result=triangulatePolygonIntervals(9,positive,81);
    require(result.status==PolygonIntervalStatus::LimitExceeded&&result.triangles.empty()&&calls==81,
        "Interval candidate exhaustion returned a partial triangulation or exceeded its work allowance.");
    calls=0;
    result=triangulatePolygonIntervals(9,[&](size_t,size_t,size_t){++calls;return 0.;},81);
    require(result.status==PolygonIntervalStatus::LimitExceeded&&result.triangles.empty()&&calls==7,
        "Infeasible interval candidates escaped work accounting or published partial output.");
    result=triangulatePolygonIntervals(6,[](size_t,size_t,size_t){return 0.;});
    require(result.status==PolygonIntervalStatus::Invalid&&result.triangles.empty(),
        "An impossible complete partition returned partially feasible intervals.");
    for(const double invalid:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        result=triangulatePolygonIntervals(3,[&](size_t,size_t,size_t){return invalid;});
        require(result.status==PolygonIntervalStatus::Invalid&&result.triangles.empty(),
            "Interval triangulation admitted a nonpositive or nonfinite triangle quality.");
    }

    // The attractive root triangle scores nine but strands a quality-one
    // child. The alternative partition has minimum quality four throughout.
    result=triangulatePolygonIntervals(4,[](size_t a,size_t b,size_t c) {
        if(a==0&&b==1&&c==3)return 9.;
        if(a==1&&b==2&&c==3)return 1.;
        return 4.;
    });
    require(result.status==PolygonIntervalStatus::Ready&&result.triangles==std::vector<std::array<size_t,3>>{{0,2,3},{0,1,2}},
        "Interval selection optimized one triangle instead of the partition's minimum quality.");
    result=triangulatePolygonIntervals(4,[](size_t,size_t,size_t){return 1.;});
    require(result.status==PolygonIntervalStatus::Ready&&result.triangles==std::vector<std::array<size_t,3>>{{0,1,3},{1,2,3}},
        "Equal-quality interval partitions lost their earliest-index deterministic tie break.");

    // An independent concave simple boundary checks the interval geometry
    // precondition beyond the convex contact cells used by the production path.
    constexpr std::array<std::array<double,2>,5> points{{{0,0},{2,0},{2,2},{1,1},{0,2}}};
    const auto area=[&](size_t a,size_t b,size_t c){return
        (points[b][0]-points[a][0])*(points[c][1]-points[a][1])-
        (points[b][1]-points[a][1])*(points[c][0]-points[a][0]);};
    result=triangulatePolygonIntervals(points.size(),area);
    require(result.status==PolygonIntervalStatus::Ready&&result.triangles.size()==points.size()-2,
        "Positive interval partitions failed on a simple concave boundary.");
    std::map<std::pair<size_t,size_t>,unsigned> edges;double covered=0;
    for(const auto& triangle:result.triangles) {
        require(area(triangle[0],triangle[1],triangle[2])>0,"Interval reconstruction reversed a geometric face.");
        covered+=.5*area(triangle[0],triangle[1],triangle[2]);
        for(size_t i=0;i<3;++i)++edges[std::minmax(triangle[i],triangle[(i+1)%3])];
    }
    require(std::abs(covered-3.)<1e-12,"Interval reconstruction changed simple-boundary coverage.");
    for(size_t i=0;i<points.size();++i) {
        const size_t next=(i+1)%points.size();const auto edge=std::minmax(i,next);
        require(edges[edge]==1,"Interval reconstruction discarded a boundary segment.");edges.erase(edge);
    }
    for(const auto& [edge,count]:edges){(void)edge;require(count==2,"Interval reconstruction left an unmatched interior diagonal.");}
}
} // namespace crimson::decals::test
