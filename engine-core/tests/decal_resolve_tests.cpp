#include "decals/DecalResolve.h"
#include <crimson/renderer/forward/MaterialGraph.h>
#include <iostream>
#include <stdexcept>

using namespace crimson;
using namespace crimson::decals;
namespace {
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
ModelAsset plane() {
    ModelAsset model;model.id="seed-proof";ModelPrimitive primitive;
    for(Vec3 point:std::array<Vec3,4>{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}}) {
        ModelVertex vertex;vertex.position=point;vertex.normal={0,0,1};primitive.vertices.push_back(vertex);
    }
    primitive.indices={0,1,2,0,2,3};model.primitives.push_back(std::move(primitive));model.draws.push_back({0});return model;
}
void boundedSurfaceProjection() {
    auto model=plane();model.draws.push_back({0,math::translation({3,0,0})});const auto source=buildModelSurface(model,9,SourceNormals::UseMeshNormals);
    const auto hit=projectModelSurfaceHit(source,91,Mat4{},{.4f,-.2f,.35f},.5f);
    check(hit.hit&&math::length(hit.hit->localPoint-Vec3{.4f,-.2f,0})<1e-5,"Ordinary translation failed its bounded source reprojection.");
    check(!projectModelSurfaceHit(source,91,Mat4{},{.4f,-.2f,.6f},.5f).hit,"Surface query crossed its authored projection envelope.");
    const auto peer=projectModelSurfaceHit(source,91,Mat4{},{3.4f,-.2f,.2f},.5f);
    check(peer.hit&&peer.hit->seed.receiver.draw==1,"Ordinary translation remained restricted to the old draw occurrence.");
    check(projectModelSurfaceHit(source,91,Mat4{},{1,1,.2f},.5f).hit.has_value(),"A valid corner vertex became an ambiguous receiving surface.");
    auto contact=plane();contact.draws.push_back({0,math::translation({1,0,-1})*math::eulerRotationXYZ({0,1.5707963268f,0})});
    check(projectModelSurfaceHit(buildModelSurface(contact,10,SourceNormals::UseMeshNormals),91,Mat4{},{1,.3f,0},.5f).hit.has_value(),"A shared physical edge between transformed draw occurrences became ambiguous.");
    const auto transformed=math::translation({8,-2,3})*math::eulerRotationXYZ({.4f,-.7f,.3f})*math::scaling({-2,.7f,1.4f});
    const auto transformedPoint=math::transformPoint(transformed,{.4f,-.2f,0}),normal=math::normalized(math::transformDirection(*math::normalMatrix(transformed),{0,0,1}));
    const auto mirrored=projectModelSurfaceHit(source,91,transformed,transformedPoint+normal*.2f,.5f);
    check(mirrored.hit&&math::length(math::transformPoint(transformed,mirrored.hit->localPoint)-transformedPoint)<1e-5,"Closest-point reprojection lost a mirrored, rotated, nonuniform receiver transform.");
    model.primitives[0].indices.insert(model.primitives[0].indices.end(),{0,1,2});
    const auto ambiguous=projectModelSurfaceHit(buildModelSurface(model,10,SourceNormals::UseMeshNormals),91,Mat4{},{.4f,-.2f,.2f},.5f);
    check(ambiguous.status==SurfaceHitStatus::Ambiguous&&!ambiguous.hit,"Closest-point query accepted coincident triangle interiors.");
}
void rotatedSurfaceFrameRetainsFootprint() {
    for(const Vec3 angle:std::array<Vec3,4>{{{1.5707963268f,0,0},{0,1.5707963268f,0},{0,0,1.5707963268f},{.7f,1.2f,-.8f}}}) {
        auto authored=math::eulerRotationXYZ(angle)*math::scaling({-2,3,.4f});authored.at(0,1)+=.15f;
        const auto actual=conformDecalFrame(authored,{2,3,4},{0,0,1});check(actual.has_value(),"A valid XYZ rotation collapsed the derived footprint.");
        check(math::length(math::transformPoint(*actual,{})-Vec3{2,3,4})<1e-5,"Derived frame did not use the receiving point.");
        const auto x=math::transformDirection(*actual,{1,0,0}),y=math::transformDirection(*actual,{0,1,0});
        check(std::abs(x[2])+std::abs(y[2])<1e-5,"Derived footprint is not tangent to the receiving surface.");
        for(unsigned i=0;i<2;++i)for(unsigned j=0;j<2;++j){const Vec3 a{authored.at(0,i),authored.at(1,i),authored.at(2,i)},b{authored.at(0,j),authored.at(1,j),authored.at(2,j)},c{actual->at(0,i),actual->at(1,i),actual->at(2,i)},d{actual->at(0,j),actual->at(1,j),actual->at(2,j)};check(std::abs(math::dot(a,b)-math::dot(c,d))<2e-5,"Surface conformity changed authored size or shear.");}
        check(math::handedness(*actual)==math::handedness(authored),"Derived frame lost the authored mirror.");
    }
    check(!conformDecalFrame(math::scaling({0,1,1}),{},{0,0,1}),"Collapsed footprint was accepted.");
}
void surfaceAuthoringControls() {
    // The saved wall example had an invisible 215-metre Z column. Editing its
    // visible footprint must never rotate that depth into width or height.
    const Mat4 authored{
        1.242539525f,.399440706f,-1.746122241f,0,
        .522620201f,-2.223793983f,-.136816323f,0,
        -170.141799927f,-32.085025787f,-128.412567139f,0,
        .000000857f,2.934373379f,24.563133240f,1};
    const Vec3 normal{1,0,0},position=math::transformPoint(authored,{});
    const auto axis=[](const Mat4& frame,unsigned column){return Vec3{frame.at(0,column),frame.at(1,column),frame.at(2,column)};};
    const auto same=[](const Mat4& a,const Mat4& b,double tolerance=2e-5){
        for(std::size_t i=0;i<a.size();++i)if(std::abs(a[i]-b[i])>tolerance)return false;return true;
    };
    const auto frame=decalAuthoringFrame(authored,normal);
    const auto visible=conformDecalFrame(authored,position,normal);
    check(frame&&visible&&same(*frame,*visible),"Authoring canonicalization changed the existing visible decal frame.");
    check(math::transformPoint(*frame,{})==position,"Authoring canonicalization moved the authored position.");
    check(math::length(axis(*frame,2)-normal)<1e-6,"Authoring retained the hidden depth column.");
    const auto size=decalSize(*frame);
    check(std::abs(size[0]-2.18f)<2e-5&&std::abs(size[1]-2.28847335f)<2e-5,"Authoring size does not measure the saved visible footprint.");
    const auto proxy=decalGizmoFrame(*frame);
    check(proxy&&math::handedness(*proxy)>0,"Surface gizmo did not produce a right-handed frame.");
    check(math::transformPoint(*proxy,{})==position,"Surface gizmo moved the authored position.");
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
        check(std::abs(math::dot(axis(*proxy,i),axis(*proxy,j))-(i==j?1.:0.))<2e-6,"Surface gizmo retained scale or shear.");
    const float initialAngle=decalRotationDegrees(*frame);
    const auto turned=withDecalRotation(*frame,initialAngle+90);
    check(turned&&std::abs(math::dot(axis(*turned,0),axis(*frame,0)))<2e-5,"A 90-degree surface rotation did not turn width orthogonally.");
    check(math::length(axis(*turned,0)-math::cross(normal,axis(*frame,0)))<2e-5,"Positive surface rotation used the wrong normal-axis direction.");
    check(std::abs(decalRotationDegrees(*turned,initialAngle+90)-(initialAngle+90))<2e-5,"Surface rotation did not retain its requested angle branch.");
    const auto absolute=withDecalRotation(*frame,90);
    const auto stable=surfacePlacementFrame({},normal);
    check(absolute&&math::length(math::normalized(axis(*absolute,0))-axis(stable,1))<2e-5,"Setting surface rotation to 90 degrees did not use the stable wall basis.");
    const auto widened=withDecalSize(*frame,{size[0]*2,size[1]});
    check(widened&&math::length(axis(*widened,0)-axis(*frame,0)*2)<2e-5&&math::length(axis(*widened,1)-axis(*frame,1))<2e-5,
        "Changing decal width changed height or mixed hidden depth into its footprint.");
    check(math::transformPoint(*turned,{})==position&&math::transformPoint(*widened,{})==position,"Footprint editing moved the authored translation.");
    check(math::length(axis(*turned,2)-normal)<1e-6&&math::length(axis(*widened,2)-normal)<1e-6,"Footprint editing changed the unit receiver normal.");

    auto sheared=*frame;
    for(unsigned row=0;row<3;++row)sheared.at(row,1)=-frame->at(row,1)+frame->at(row,0)*.35f;
    const auto shearSize=decalSize(sheared);
    check(shearSize[1]<0,"Signed decal height did not expose reflection.");
    const auto edited=withDecalSize(sheared,{shearSize[0]*1.7f,shearSize[1]*.6f});
    const auto spun=withDecalRotation(sheared,decalRotationDegrees(sheared)+90);
    check(edited&&spun&&math::handedness(*edited)<0&&math::handedness(*spun)<0,"Surface size or rotation discarded a mirrored footprint.");
    const double cosineBefore=math::dot(axis(sheared,0),axis(sheared,1))/(math::length(axis(sheared,0))*math::length(axis(sheared,1)));
    const double cosineAfter=math::dot(axis(*edited,0),axis(*edited,1))/(math::length(axis(*edited,0))*math::length(axis(*edited,1)));
    check(std::abs(cosineAfter-cosineBefore)<2e-6,"Independent surface size edits changed the shear angle.");
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<2;++j)
        check(std::abs(math::dot(axis(*spun,i),axis(*spun,j))-math::dot(axis(sheared,i),axis(sheared,j)))<2e-5,"Surface rotation changed the mirrored XY metric.");
    const auto unmirrored=withDecalSize(sheared,{shearSize[0],-shearSize[1]});
    check(unmirrored&&math::handedness(*unmirrored)>0,"Changing signed height did not reflect the footprint.");
    check(!withDecalSize(*frame,{0,1})&&!withDecalSize(*frame,{1,0}),"Collapsed surface sizes were accepted.");
    check(!decalAuthoringFrame(authored,{})&&!decalAuthoringFrame(authored,{1e-12f,0,0}),"A missing receiver normal produced an editable surface frame.");
    auto noNormal=*frame;for(unsigned row=0;row<3;++row)noNormal.at(row,2)=0;
    check(!decalGizmoFrame(noNormal)&&!withDecalRotation(noNormal,90)&&!withDecalSize(noNormal,{2,3}),"Surface edit helpers accepted a frame without a receiver normal.");
    check(std::abs(decalRotationDegrees(*absolute,810)-810)<2e-5,"Surface angle lost an equivalent multi-turn UI branch.");

    // A world-frame edit is converted back through the complete parent affine
    // transform. Neither a mirrored parent nor shear changes the visible result.
    for(const Vec3 parentScale:std::array<Vec3,2>{{{1.5f,.7f,2},{-1.5f,.7f,2}}}) {
        auto parent=math::translation({8,-3,5})*math::eulerRotationXYZ({.3f,-.7f,.8f})*math::scaling(parentScale);
        parent.at(0,1)+=.2f;
        const auto inverse=math::inverse(parent);check(inverse.has_value(),"Parent fixture is singular.");
        const auto local=(*inverse)*authored;
        const auto parented=decalAuthoringFrame(parent*local,normal);
        check(parented&&same(*parented,*frame),"Parent affine transform changed the canonical surface frame.");
        const auto parentedTurn=withDecalRotation(*parented,initialAngle+90);
        check(parentedTurn.has_value(),"Parented surface rotation failed.");
        const auto editedLocal=(*inverse)*(*parentedTurn);
        check(same(parent*editedLocal,*turned),"Converting a surface edit back to parent-local space lost its result.");
    }
}
void materialEligibility() {
    ModelMaterial material;check(receiverMaterialEligible(material),"Opaque imported material excluded.");
    material.alphaMask=true;check(receiverMaterialEligible(material),"Masked imported material excluded.");
    material.alphaBlend=true;check(!receiverMaterialEligible(material),"Imported blend material admitted.");
    auto graph=std::make_shared<MaterialGraph>();MaterialNode output;output.id=7;output.kind=MaterialNodeKind::Output;graph->nodes.push_back(output);material.graph=graph;
    check(receiverMaterialEligible(material),"Native opaque graph should own blend semantics.");
    graph->nodes[0].output.blend=MaterialBlend::Mask;check(receiverMaterialEligible(material),"Native masked graph excluded.");
    graph->nodes[0].output.blend=MaterialBlend::Alpha;check(!receiverMaterialEligible(material),"Native transparent graph admitted.");
    graph->nodes[0].output.blend=MaterialBlend::Opaque;graph->nodes[0].output.shading=MaterialShading::Unlit;check(!receiverMaterialEligible(material),"Unlit graph admitted.");
    graph->nodes[0].output.shading=MaterialShading::StandardPbr;graph->nodes[0].output.tessellation.enabled=true;check(!receiverMaterialEligible(material),"Displaced graph admitted.");
    graph->nodes[0].output.tessellation.enabled=false;graph->nodes[0].output.opacity=.5f;check(!receiverMaterialEligible(material),"Faded opaque graph admitted.");
    graph->nodes[0].output.opacity=1;graph->links.push_back({1,99,7,MaterialPin::Value,MaterialPin::Opacity});
    check(!receiverMaterialEligible(material),"Runtime opacity graph admitted.");
    graph->links[0].toPin=MaterialPin::OpacityMask;check(receiverMaterialEligible(material),"Masked coverage incorrectly treated as runtime opacity.");
}
void geometryChangeReprojection() {
    auto changed=buildModelSurface(plane(),2,SourceNormals::UseMeshNormals);
    for(auto& triangle:changed.draws[0].surface.triangles)triangle.triangle+=7;
    const auto rebound=projectModelSurfaceHit(changed,91,Mat4{},{.4f,-.2f,0},.5f);
    check(rebound.hit&&rebound.hit->seed.triangle>=7,"Changed geometry reused stale triangle indices instead of bounded point matching.");
    check(!projectModelSurfaceHit(changed,91,math::translation({5,0,0}),{.4f,-.2f,0},.5f).hit,"World-anchored decal silently followed a moved receiver.");
}
void reanchorRetainsAffineFootprint() {
    const Vec3 oldNormal=math::normalized({.2f,.1f,1}),newNormal={0,1,0};
    const auto oldBasis=surfacePlacementFrame({},oldNormal);
    Mat4 residual=math::eulerRotationXYZ({0,0,.43f})*math::scaling({-2,3,.7f});
    residual.at(0,1)+=.6f;residual.at(1,2)-=.2f;
    Mat4 authored=oldBasis*residual;authored.at(0,3)=4;authored.at(1,3)=5;authored.at(2,3)=6;
    const auto target=surfacePlacementFrame({-2,8,1},newNormal);
    const auto anchored=reanchorDecalFrame(authored,oldNormal,target);
    check(math::length(math::transformPoint(anchored,{})-Vec3{-2,8,1})<1e-5,"Explicit reanchor did not replace translation.");
    check(math::handedness(anchored)==math::handedness(authored),"Explicit reanchor lost authored reflection.");
    const auto inverseNew=math::inverse(surfacePlacementFrame({},newNormal));
    const auto recovered=(*inverseNew)*anchored;
    for(size_t row=0;row<3;++row)for(size_t column=0;column<3;++column)
        check(std::abs(recovered.at(row,column)-residual.at(row,column))<2e-5,"Explicit reanchor lost residual rotation, shear, or scale.");
    for(size_t i=0;i<3;++i)for(size_t j=0;j<3;++j){
        const Vec3 beforeI{authored.at(0,i),authored.at(1,i),authored.at(2,i)},beforeJ{authored.at(0,j),authored.at(1,j),authored.at(2,j)};
        const Vec3 afterI{anchored.at(0,i),anchored.at(1,i),anchored.at(2,i)},afterJ{anchored.at(0,j),anchored.at(1,j),anchored.at(2,j)};
        check(std::abs(math::dot(beforeI,beforeJ)-math::dot(afterI,afterJ))<2e-5,"Explicit reanchor changed the affine Gram matrix.");
    }
}
}
int main() {
    try {boundedSurfaceProjection();rotatedSurfaceFrameRetainsFootprint();surfaceAuthoringControls();materialEligibility();geometryChangeReprojection();reanchorRetainsAffineFootprint();
        std::cout<<"decal_resolve: generic closest-point projection, surface footprint controls, XYZ affine frames, PBR coverage eligibility, changed source provenance and ambiguous projection proofs passed\n";return 0;
    } catch(const std::exception&error){std::cerr<<error.what()<<'\n';return 1;}
}
