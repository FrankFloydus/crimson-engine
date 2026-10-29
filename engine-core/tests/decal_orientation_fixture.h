#pragma once
#include "decals/DecalOrientation.h"
#include <array>
#include <cmath>
#include <stdexcept>

namespace crimson::decals::test {
inline void runOrientationProofs() {
    using Point=std::array<double,2>;
    const auto orient=[](Point a,Point b,Point c){return detail::robustOrient2D(a[0],a[1],b[0],b[1],c[0],c[1]);};
    const auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    // Integer inputs give an exact determinant of one. Ordinary products lose
    // that unit at 2^54. Check all cyclic/reversed orders and a translation.
    const double extent=std::ldexp(1.,27);
    for(const double offset:{0.,std::ldexp(1.,30)}) {
        const std::array<Point,3> points{{{offset+extent,offset+extent+1},
            {offset+extent-1,offset+extent},{offset,offset}}};
        for(size_t i=0;i<3;++i) {
            require(orient(points[i],points[(i+1)%3],points[(i+2)%3])==1,
                "Filtered orientation lost the exact sign of cancelling products.");
            require(orient(points[i],points[(i+2)%3],points[(i+1)%3])==-1,
                "Filtered orientation changed its sign under boundary reversal.");
        }
    }
    // The point differences themselves round before multiplication here.
    // Retaining product residuals alone returns the wrong determinant; the
    // exact subtraction tails restore the independently known p*2^-52 value.
    const double unit=std::ldexp(1.,-52),large=1e16,expected=large*unit;
    const std::array<Point,3> mixed{{{0,0},{large,large},{1,1+unit}}};
    for(size_t i=0;i<3;++i)require(std::abs(orient(mixed[i],mixed[(i+1)%3],mixed[(i+2)%3])-expected)<1e-15,
        "Filtered orientation discarded coordinate-subtraction residuals.");
    require(orient({0,0},{large,large},{1,1})==0,
        "Exact collinear points became a fabricated contact triangle.");
    require(orient({0,0},{1,1},{2,2})==0,
        "Ordinary collinear points became a fabricated contact triangle.");
}
} // namespace crimson::decals::test
