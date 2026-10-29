#include "editor/ViewportDecalDrop.h"
#include <iostream>
#include <stdexcept>

using namespace crimson;
namespace {
void check(bool condition,const char*message){if(!condition)throw std::runtime_error(message);}
PickResult hit(uint64_t serial,uint64_t entity=42) {
    PickResult result;result.mode=PickMode::Surface;result.serial=serial;result.entityId=entity;
    result.surfaceValid=entity!=0;result.drawIndex=3;result.primitive=7;result.resourceRevision=12;
    result.worldPosition={2,3,4};return result;
}
void releaseOwnsExactPixel() {
    ViewportDecalDrop drop;const AssetRef<DecalAsset> asset{"blood"};
    check(drop.update(asset,{10,20},false)&&drop.needsPick(),"Initial drag did not request a hover surface.");
    drop.request(1);check(!drop.resolve(hit(1))&&drop.preview(),"Hover should preview without committing.");
    drop.request(2);
    check(drop.update(asset,{90,80},true),"Release did not supersede the old hover request.");
    check(!drop.resolve(hit(2))&&drop.active(),"Late hover result committed the released gesture.");
    check(drop.needsPick(),"Released pixel was not repicked.");drop.request(3);
    check(drop.pending()->pixel==ViewportDecalDrop::Pixel{90,80},"Release request used a stale hover pixel.");
    const auto placement=drop.resolve(hit(3));
    check(placement&&placement->asset==asset&&placement->hit.serial==3&&!drop.active(),"Release did not commit exactly its own surface result.");
}
void pendingRetryRetainsDeliveredPixel() {
    ViewportDecalDrop drop;drop.update(AssetRef<DecalAsset>{"blood"},{31,47},true);drop.request(10);
    auto pending=hit(10);pending.surfacePending=true;pending.surfaceValid=false;
    check(!drop.resolve(pending)&&drop.active()&&drop.delivered()&&drop.needsPick(),"Pending original LOD canceled the delivered gesture.");
    drop.update(AssetRef<DecalAsset>{"blood"},{999,999},false);
    drop.request(11);check(drop.pending()->pixel==ViewportDecalDrop::Pixel{31,47},"Pending surface retry lost release coordinates.");
    check(!drop.resolve(hit(10))&&drop.pending()->serial==11,"Late prior retry consumed the newer request.");
    const auto placement=drop.resolve(hit(11));check(placement&&placement->hit.serial==11&&!drop.active(),"Ready retry failed to commit its delivered surface.");
}
void cancelAndReplacementRejectStaleResults() {
    ViewportDecalDrop drop;drop.update(AssetRef<DecalAsset>{"first"},{3,4},true);drop.request(20);drop.cancel();
    check(!drop.active()&&!drop.resolve(hit(20))&&!drop.preview(),"Canceled gesture accepted a late result.");
    drop.update(AssetRef<DecalAsset>{"first"},{5,6},false);drop.request(21);
    check(drop.update(AssetRef<DecalAsset>{"second"},{7,8},true),"New dragged asset did not supersede the pending request.");
    check(!drop.resolve(hit(21))&&drop.needsPick(),"Prior asset's result was accepted for a new asset.");
    drop.request(22);const auto placement=drop.resolve(hit(22));
    check(placement&&placement->asset.id=="second","New gesture committed the wrong asset.");
    drop.update(AssetRef<DecalAsset>{"second"},{7,8},true);drop.request(23);
    check(!drop.resolve(hit(23,0))&&!drop.active(),"Background release left a stranded active gesture.");
}
void explicitReanchorWaitsForClick() {
    ViewportDecalDrop drop;drop.beginReplace(91);
    check(drop.active()&&drop.awaitingClick()&&!drop.needsPick(),"Explicit reanchor did not wait for a viewport click.");
    check(!drop.resolve(hit(30)),"Explicit reanchor accepted an unsolicited old pick.");
    drop.click({50,60});check(drop.delivered()&&!drop.awaitingClick()&&drop.needsPick(),"Reanchor click did not request its exact pixel.");
    drop.request(31);check(drop.pending()->pixel==ViewportDecalDrop::Pixel{50,60},"Reanchor click lost exact coordinates.");
    const auto placement=drop.resolve(hit(31));
    check(placement&&placement->replaceEntity==91&&placement->asset.empty()&&!drop.active(),"Explicit reanchor failed to retain existing decal identity.");
}
}
int main(){try{releaseOwnsExactPixel();pendingRetryRetainsDeliveredPixel();cancelAndReplacementRejectStaleResults();explicitReanchorWaitsForClick();
    std::cout<<"decal_viewport: exact release pixels, pending retries, cancellation, serial isolation and explicit reanchor proofs passed\n";return 0;
}catch(const std::exception&error){std::cerr<<error.what()<<'\n';return 1;}}
