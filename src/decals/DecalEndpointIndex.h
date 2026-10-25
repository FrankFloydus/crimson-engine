#pragma once
#include "DecalInternal.h"
#include <limits>
#include <numeric>
#include <vector>

namespace crimson::decals::detail {
// Immutable point index for the shared-endpoint pass. The query only rejects
// points outside a conservative segment box; the caller retains the precise
// edge-distance, interval and provenance predicates and final ordering.
class EndpointIndex {
public:
    explicit EndpointIndex(const std::vector<D3>& positions):positions_(positions),ids_(positions.size()) {
        std::iota(ids_.begin(),ids_.end(),size_t{});
        nodes_.reserve(positions.size()/4+1);
        if(!ids_.empty())build(0,ids_.size());
    }
    template<class Emit> void querySegment(D3 a,D3 b,double tolerance,Emit&& emit)const {
        if(nodes_.empty())return;
        D3 lo{},hi{};const double extent=length(b-a);
        for(size_t axis=0;axis<3;++axis){
            // Broad-phase padding covers the caller's rounded normalization,
            // projection and subtraction too. It never changes acceptance.
            const double pad=tolerance+8*std::numeric_limits<double>::epsilon()*(std::abs(a[axis])+std::abs(b[axis])+extent);
            const double lower=std::nextafter(std::min(a[axis],b[axis])-pad,-INFINITY),upper=std::nextafter(std::max(a[axis],b[axis])+pad,INFINITY);
            if(axis==0){lo.x=lower;hi.x=upper;}else if(axis==1){lo.y=lower;hi.y=upper;}else{lo.z=lower;hi.z=upper;}
        }
        query(0,lo,hi,emit);
    }
private:
    static constexpr size_t None=std::numeric_limits<size_t>::max();
    struct Node {D3 lo,hi;size_t begin,end,left{None},right{None};};
    size_t build(size_t begin,size_t end) {
        D3 lo=positions_[ids_[begin]],hi=lo;
        for(size_t i=begin+1;i<end;++i)for(size_t axis=0;axis<3;++axis){const double value=positions_[ids_[i]][axis];if(axis==0){lo.x=std::min(lo.x,value);hi.x=std::max(hi.x,value);}else if(axis==1){lo.y=std::min(lo.y,value);hi.y=std::max(hi.y,value);}else{lo.z=std::min(lo.z,value);hi.z=std::max(hi.z,value);}}
        const size_t id=nodes_.size();nodes_.push_back({lo,hi,begin,end});
        if(end-begin<=16)return id;
        size_t axis=0;for(size_t i=1;i<3;++i)if(hi[i]-lo[i]>hi[axis]-lo[axis])axis=i;
        const size_t middle=begin+(end-begin)/2;
        std::nth_element(ids_.begin()+static_cast<std::ptrdiff_t>(begin),ids_.begin()+static_cast<std::ptrdiff_t>(middle),ids_.begin()+static_cast<std::ptrdiff_t>(end),[&](size_t a,size_t b){const double x=positions_[a][axis],y=positions_[b][axis];return x!=y?x<y:a<b;});
        const size_t left=build(begin,middle),right=build(middle,end);nodes_[id].left=left;nodes_[id].right=right;return id;
    }
    template<class Emit> void query(size_t id,D3 lo,D3 hi,Emit& emit)const {
        const auto&node=nodes_[id];
        for(size_t axis=0;axis<3;++axis)if(node.lo[axis]>hi[axis]||node.hi[axis]<lo[axis])return;
        if(node.left!=None){query(node.left,lo,hi,emit);query(node.right,lo,hi,emit);return;}
        for(size_t i=node.begin;i<node.end;++i){const size_t point=ids_[i];const D3 p=positions_[point];if(p.x>=lo.x&&p.x<=hi.x&&p.y>=lo.y&&p.y<=hi.y&&p.z>=lo.z&&p.z<=hi.z)emit(point);}
    }
    const std::vector<D3>&positions_;
    std::vector<size_t>ids_;
    std::vector<Node>nodes_;
};
} // namespace crimson::decals::detail
