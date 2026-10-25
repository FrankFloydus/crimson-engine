#pragma once
#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace crimson::decals::detail {
// A source polygon's successive finite cuts form an ordered spatial tree.
// Internal bounds remain conservative after splitting, so a later short cut
// can reject complete subtrees without visiting every previously made cell.
// Left/right traversal preserves the scalar cut-and-replace recipe's order.
template<class Polygon,class Box>
class ContactCells {
public:
    ContactCells(Polygon polygon,Box bounds) { nodes_.push_back({std::move(polygon),std::move(bounds)}); }
    size_t size()const noexcept { return leaves_; }

    // Split returns false for an unchanged leaf, or true and emits zero to two
    // valid child polygons in their original recipe order. Children are not
    // processed again by this cut. The caller checks its live-cell budget after
    // each cut, exactly as for the scalar algorithm.
    template<class Overlap,class Bounds,class Split,class Cancel>
    size_t apply(const Box& support,Overlap&& overlap,Bounds&& bounds,Split&& split,Cancel&& cancel) {
        work_.clear();work_.push_back(0);size_t visited=0;
        while(!work_.empty()) {
            cancel();const size_t id=work_.back();work_.pop_back();
            if(!overlap(nodes_[id].bounds,support))continue;
            if(!nodes_[id].leaf) { visitChildren(id);continue; }
            ++visited;std::array<Polygon,2> children;size_t count=0;
            const bool changed=split(nodes_[id].polygon,[&](Polygon child) {
                if(count==children.size())throw std::logic_error("A contact cut produced more than two child cells.");
                children[count++]=std::move(child);
            });
            if(!changed)continue;
            const size_t first=nodes_.size();
            nodes_[id].polygon=Polygon{};nodes_[id].leaf=false;
            nodes_[id].children={count?first:None,count>1?first+1:None};
            leaves_=leaves_-1+count;
            for(size_t i=0;i<count;++i) {
                auto childBounds=bounds(children[i]);
                nodes_.push_back({std::move(children[i]),std::move(childBounds)});
            }
        }
        return visited;
    }

    template<class Emit> void forEach(Emit&& emit) {
        work_.clear();work_.push_back(0);
        while(!work_.empty()) {
            const size_t id=work_.back();work_.pop_back();
            if(nodes_[id].leaf)emit(nodes_[id].polygon);else visitChildren(id);
        }
    }
private:
    static constexpr size_t None=std::numeric_limits<size_t>::max();
    struct Node {
        Polygon polygon;Box bounds;std::array<size_t,2> children{None,None};bool leaf{true};
    };
    void visitChildren(size_t id) {
        const auto children=nodes_[id].children;
        if(children[1]!=None)work_.push_back(children[1]);
        if(children[0]!=None)work_.push_back(children[0]);
    }
    std::vector<Node> nodes_;
    std::vector<size_t> work_;
    size_t leaves_{1};
};
} // namespace crimson::decals::detail
