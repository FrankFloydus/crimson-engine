#pragma once
#include "decals/DecalContactCells.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace crimson::decals::test {
namespace contact_cells_fixture {
using Point=std::array<double,2>;
using Polygon=std::vector<Point>;
struct Box {Point minimum,maximum;};
struct Cut {size_t axis;double position;Box support;};
inline void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
inline Box bounds(const Polygon& polygon) {
    Box result{polygon.front(),polygon.front()};
    for(const auto& point:polygon)for(size_t axis=0;axis<2;++axis) {
        result.minimum[axis]=std::min(result.minimum[axis],point[axis]);
        result.maximum[axis]=std::max(result.maximum[axis],point[axis]);
    }
    return result;
}
inline bool overlaps(const Box& a,const Box& b) {
    constexpr double tolerance=1e-4;
    for(size_t axis=0;axis<2;++axis)
        if(a.minimum[axis]>b.maximum[axis]+tolerance||b.minimum[axis]>a.maximum[axis]+tolerance)return false;
    return true;
}
inline Polygon clip(const Polygon& polygon,const Cut& cut,double sign) {
    Polygon result;
    for(size_t i=0;i<polygon.size();++i) {
        const auto& a=polygon[i];const auto& b=polygon[(i+1)%polygon.size()];
        const double da=(a[cut.axis]-cut.position)*sign,db=(b[cut.axis]-cut.position)*sign;
        if(da<=0)result.push_back(a);
        if((da<0&&db>0)||(da>0&&db<0)) {
            const double t=da/(da-db);result.push_back({a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t});
        }
    }
    return result;
}
template<class Emit> bool split(const Polygon& polygon,const Cut& cut,Emit&& emit) {
    bool positive=false,negative=false;
    for(const auto& point:polygon){positive|=point[cut.axis]>cut.position;negative|=point[cut.axis]<cut.position;}
    if(!positive||!negative)return false;
    auto a=clip(polygon,cut,1),b=clip(polygon,cut,-1);
    if(a.size()>=3)emit(std::move(a));
    if(b.size()>=3)emit(std::move(b));
    return true;
}
inline double area(const Polygon& polygon) {
    double result=0;
    for(size_t i=0;i<polygon.size();++i)result+=polygon[i][0]*polygon[(i+1)%polygon.size()][1]-polygon[(i+1)%polygon.size()][0]*polygon[i][1];
    return std::abs(result)*.5;
}
inline void exercise(Polygon initial) {
    detail::ContactCells<Polygon,Box> tree(initial,bounds(initial));std::vector<Polygon> scalar{initial};
    std::vector<Cut> cuts;const Box whole{{0,0},{16,16}};
    for(size_t axis=0;axis<2;++axis)for(unsigned line=1;line<16;++line)cuts.push_back({axis,double(line),whole});
    const auto globalCuts=cuts.size();
    for(unsigned row=0;row<8;++row)for(unsigned column=0;column<8;++column) {
        const double x=column*2+.37,y=row*2+.41;cuts.push_back({0,x,{{x-.01,y-.01},{x+.01,y+.01}}});
    }
    // Only tolerance admits this support against the source's right boundary.
    cuts.push_back({1,8.25,{{16.00005,8.2},{16.00005,8.3}}});
    cuts.push_back({1,9.25,{{16.0002,9.2},{16.0002,9.3}}});
    size_t localizedTreeQueries=0,localizedScalarQueries=0;
    for(size_t step=0;step<cuts.size();++step) {
        const auto& cut=cuts[step];std::vector<Polygon> next;size_t scalarQueries=0,scalarSplitCalls=0;
        for(const auto& polygon:scalar) {
            ++scalarQueries;
            if(!overlaps(bounds(polygon),cut.support)){next.push_back(polygon);continue;}
            ++scalarSplitCalls;
            if(!split(polygon,cut,[&](Polygon child){next.push_back(std::move(child));}))next.push_back(polygon);
        }
        scalar=std::move(next);size_t treeQueries=0,cancellationChecks=0;
        const auto visited=tree.apply(cut.support,[&](const Box& a,const Box& b){++treeQueries;return overlaps(a,b);},bounds,
            [&](const Polygon& polygon,auto&& emit){return split(polygon,cut,emit);},[&]{++cancellationChecks;});
        require(visited==scalarSplitCalls,"Contact-cell indexing changed which finite cuts reach source cells.");
        require(treeQueries==cancellationChecks,"Contact-cell traversal omitted a cancellation checkpoint.");
        std::vector<Polygon> actual;tree.forEach([&](const Polygon& polygon){actual.push_back(polygon);});
        require(actual==scalar&&tree.size()==scalar.size(),"Contact-cell indexing changed scalar child order, boundaries or live-cell count.");
        double covered=0;for(const auto& polygon:actual)covered+=area(polygon);
        require(std::abs(covered-area(initial))<1e-9,"Contact-cell indexing changed the source polygon coverage.");
        if(step>=globalCuts){localizedTreeQueries+=treeQueries;localizedScalarQueries+=scalarQueries;}
    }
    require(localizedTreeQueries*2<localizedScalarQueries,
        "Localized contact cuts still visit most previously generated source cells.");
}
} // namespace contact_cells_fixture
inline void runContactCellsProofs() {
    using namespace contact_cells_fixture;
    exercise({{0,0},{16,0},{16,16},{0,16}});
    exercise({{0,0},{16,0},{16,12},{12,16},{0,16}});
}
} // namespace crimson::decals::test
