#pragma once

#include "DecalInternal.h"

namespace crimson::decals::detail {

// This is a conservative broad phase for original surface connectivity. The
// derived, oriented edge uses remain authoritative, including ambiguous joins.
// In particular, an intersection through a triangle's interior is not a shared
// boundary; the caller preserves it separately as a traversal barrier.
inline bool sourceBoundaryOverlap(const Source& a,const Source& b,double tolerance,double epsilon) {
    for(size_t i=0;i<3;++i) {
        const D3 p=a.p[i],edgeA=a.p[(i+1)%3]-p;
        const double extentA=length(edgeA);
        if(extentA<=epsilon)continue;
        const D3 directionA=edgeA/extentA;
        for(size_t j=0;j<3;++j) {
            const D3 q=b.p[j],edgeB=b.p[(j+1)%3]-q;
            const double extentB=length(edgeB);
            if(extentB<=epsilon)continue;
            const D3 directionB=edgeB/extentB;
            // Test the common part of mismatched edges, rather than requiring
            // either edge's remote endpoints to lie on the other finite edge.
            const double angularTolerance=std::max(1e-8,std::min(1e-3,tolerance/std::max(extentA,extentB)));
            if(length(cross(directionA,directionB))>angularTolerance)continue;
            const double first=dot(q-p,directionA),last=dot(q+edgeB-p,directionA);
            const double lo=std::max(0.,std::min(first,last)),hi=std::min(extentA,std::max(first,last));
            if(hi-lo<=epsilon||std::abs(last-first)<=epsilon)continue;
            bool close=true;
            for(const double along:{lo,hi}) {
                const D3 onA=p+directionA*along,onB=q+edgeB*((along-first)/(last-first));
                close&=length(onA-onB)<=tolerance;
            }
            if(close)return true;
        }
    }
    return false;
}

}
