#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace crimson::decals::detail {
enum class PolygonIntervalStatus { Ready,Invalid,LimitExceeded };
struct PolygonIntervalResult {
    PolygonIntervalStatus status{PolygonIntervalStatus::Invalid};
    std::vector<std::array<size_t,3>> triangles;
};

// Complete triangulation of an ordered simple boundary. A positive quality
// admits an oriented triangle; zero rejects it. Feasible interval partitions
// preserve every boundary segment, including collinear contact endpoints. Pick
// the partition with the best minimum triangle quality, breaking ties by the
// earliest boundary index. Unlike a greedy ear, this cannot strand a chain
// that another legal diagonal can triangulate. All triangles publish together.
template<class Quality>
PolygonIntervalResult triangulatePolygonIntervals(size_t count,Quality&& quality,size_t maximumWork=2'000'000) {
    PolygonIntervalResult result;
    if(count<3)return result;
    // Bound both table storage and candidate work, including failed searches.
    if(count>maximumWork/count){result.status=PolygonIntervalStatus::LimitExceeded;return result;}
    const size_t entries=count*count;
    std::vector<double> scores(entries);
    std::vector<size_t> splits(entries,count);
    for(size_t i=0;i+1<count;++i)scores[i*count+i+1]=std::numeric_limits<double>::infinity();
    size_t work=0;
    for(size_t span=2;span<count;++span)for(size_t first=0;first+span<count;++first) {
        const size_t last=first+span,index=first*count+last;
        for(size_t middle=first+1;middle<last;++middle) {
            if(++work>maximumWork){result.status=PolygonIntervalStatus::LimitExceeded;return result;}
            const double left=scores[first*count+middle],right=scores[middle*count+last];
            if(left<=0||right<=0)continue;
            const double triangle=quality(first,middle,last);
            if(!std::isfinite(triangle)||triangle<=0)continue;
            const double score=std::min({triangle,left,right});
            if(score>scores[index]){scores[index]=score;splits[index]=middle;}
        }
    }
    if(scores[count-1]<=0)return result;
    std::vector<std::array<size_t,2>> pending{{0,count-1}};
    result.triangles.reserve(count-2);
    while(!pending.empty()) {
        const auto interval=pending.back();pending.pop_back();
        const size_t first=interval[0],last=interval[1];
        if(last<=first+1)continue;
        const size_t middle=splits[first*count+last];
        result.triangles.push_back({first,middle,last});
        pending.push_back({middle,last});pending.push_back({first,middle});
    }
    result.status=PolygonIntervalStatus::Ready;return result;
}
} // namespace crimson::decals::detail
