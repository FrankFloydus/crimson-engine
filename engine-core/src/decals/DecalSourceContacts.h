#pragma once

#include "DecalInternal.h"

namespace crimson::decals::detail {

struct SourceBoundaryContact {size_t edgeA{},edgeB{};std::array<D3,2> onA{},onB{};};
// The broad seam tolerance admits candidates; only the existing positional
// weld and each original triangle's feature width can certify a boundary pair.
// Work in the finite common interval, so a short fine edge may meet part of a
// longer source boundary without projecting its remote endpoints onto it.
inline std::vector<SourceBoundaryContact> originalBoundaryContacts(const Source& a,const Source& b,double tolerance,double epsilon,double weld){
    const auto feature=[](const Source& source){double minimum=INFINITY;for(size_t i=0;i<3;++i){const D3 edge=source.p[(i+1)%3]-source.p[i];const double extent=length(edge);if(extent==0)return 0.;minimum=std::min(minimum,extent);minimum=std::min(minimum,length(cross(edge,source.p[(i+2)%3]-source.p[i]))/extent);}return minimum;};
    const double admissible=std::min({weld,feature(a)*.125,feature(b)*.125});std::vector<SourceBoundaryContact> result;
    for(size_t i=0;i<3;++i){const D3 p=a.p[i],edgeA=a.p[(i+1)%3]-p;const double extentA=length(edgeA);if(extentA<=epsilon)continue;const D3 directionA=edgeA/extentA;
        for(size_t j=0;j<3;++j){const D3 q=b.p[j],edgeB=b.p[(j+1)%3]-q;const double extentB=length(edgeB);if(extentB<=epsilon)continue;const D3 directionB=edgeB/extentB;
            const double angular=std::max(1e-8,std::min(1e-3,tolerance/std::max(extentA,extentB)));if(length(cross(directionA,directionB))>angular)continue;
            const double first=dot(q-p,directionA),last=dot(q+edgeB-p,directionA),lo=std::max(0.,std::min(first,last)),hi=std::min(extentA,std::max(first,last));if(hi-lo<=epsilon||std::abs(last-first)<=epsilon)continue;
            SourceBoundaryContact contact;contact.edgeA=i;contact.edgeB=j;bool close=true;size_t end{};for(double along:{lo,hi}){contact.onA[end]=p+directionA*along;contact.onB[end]=q+edgeB*((along-first)/(last-first));close&=length(contact.onA[end]-contact.onB[end])<=admissible;++end;}if(close)result.push_back(contact);
        }
    }return result;
}

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
