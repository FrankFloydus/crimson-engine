#include "DecalSurface.h"
#include "DecalInternal.h"
namespace crimson::decals {
DecalPatch buildDecalPatch(const DecalSurfaceSnapshot&snapshot,const DecalBuildRequest&request,std::stop_token stop){
    DecalPatch result;
    try{detail::cancelled(stop);auto topology=detail::buildTopology(snapshot,request,result.statistics,stop);detail::buildChart(snapshot,request,topology,result,stop);}
    catch(const detail::Failure&e){result.status=e.status;result.message=e.what();result.chunks.clear();}
    catch(const std::exception&e){result.status=DecalPatchStatus::InvalidChart;result.message=e.what();result.chunks.clear();}
    return result;
}
const char* decalPatchStatusName(DecalPatchStatus s) {
    switch(s) {
    case DecalPatchStatus::Ready:return "Ready";
    case DecalPatchStatus::Incomplete:return "Incomplete footprint";
    case DecalPatchStatus::InvalidInput:return "Invalid footprint";
    case DecalPatchStatus::MissingSeed:return "Missing surface";
    case DecalPatchStatus::AmbiguousSeed:return "Ambiguous surface";
    case DecalPatchStatus::InvalidChart:return "Invalid surface chart";
    case DecalPatchStatus::LimitExceeded:return "Surface budget exceeded";
    case DecalPatchStatus::Cancelled:return "Cancelled";
    }
    return "Invalid footprint";
}
}
