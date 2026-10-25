#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#if defined(_MSC_VER)
#pragma float_control(precise,on,push)
#endif

namespace crimson::decals::detail {
namespace orientation_detail {
struct Pair {double head,tail;};
inline Pair difference(double a,double b) {
    const double head=a-b,bVirtual=a-head,aVirtual=head+bVirtual;
    return {head,(a-aVirtual)+(bVirtual-b)};
}
inline Pair sum(double a,double b) {
    const double head=a+b,bVirtual=head-a,aVirtual=head-bVirtual;
    return {head,(a-aVirtual)+(b-bVirtual)};
}
struct Expansion {
    std::array<double,32> terms{};size_t count{};
    void add(double value) {
        if(value==0)return;
        std::array<double,32> next{};size_t length=0;double accumulator=value;
        for(size_t i=0;i<count;++i) {
            const auto part=sum(accumulator,terms[i]);
            if(part.tail!=0)next[length++]=part.tail;
            accumulator=part.head;
        }
        if(accumulator!=0)next[length++]=accumulator;
        terms=next;count=length;
    }
    void product(double a,double b,double sign) {
        const double head=a*b,tail=std::fma(a,b,-head);
        add(sign*tail);add(sign*head);
    }
};
} // namespace orientation_detail

// Filtered 2D orientation, with an exact expansion fallback for the finite,
// bounded coordinates used by decal analysis. TwoDiff retains subtraction
// tails; FMA retains each product's residual. The eight products need at most
// sixteen expansion terms. The final nonzero term supplies the exact sign.
// Error bounds/error-free transformations follow Shewchuk's public-domain
// robust-predicate arithmetic: https://www.cs.cmu.edu/~quake/robust.html
inline double robustOrient2D(double ax,double ay,double bx,double by,double cx,double cy) {
    const double acx=ax-cx,acy=ay-cy,bcx=bx-cx,bcy=by-cy;
    const double left=acx*bcy,right=acy*bcx,determinant=left-right;
    constexpr double roundoff=std::numeric_limits<double>::epsilon()*.5;
    constexpr double errorFactor=(3+16*roundoff)*roundoff;
    if(std::abs(determinant)>errorFactor*(std::abs(left)+std::abs(right)))return determinant;
    using namespace orientation_detail;
    const auto x=difference(ax,cx),y=difference(ay,cy),u=difference(bx,cx),v=difference(by,cy);
    Expansion exact;
    for(const double a:{x.tail,x.head})for(const double b:{v.tail,v.head})exact.product(a,b,1);
    for(const double a:{y.tail,y.head})for(const double b:{u.tail,u.head})exact.product(a,b,-1);
    if(!exact.count)return 0.;
    double estimate=0;for(size_t i=0;i<exact.count;++i)estimate+=exact.terms[i];
    const double sign=exact.terms[exact.count-1];
    return estimate!=0?std::copysign(std::abs(estimate),sign):sign;
}
} // namespace crimson::decals::detail

#if defined(_MSC_VER)
#pragma float_control(pop)
#endif
