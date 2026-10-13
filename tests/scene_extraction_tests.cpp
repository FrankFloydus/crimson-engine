#include "scene/SceneExtraction.h"

#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace {
using namespace crimson;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool close(const Vec3& a, const Vec3& b) { return math::length(a-b)<1e-4; }
bool close(const Mat4& a, const Mat4& b) {
    for (std::size_t i=0;i<16;++i) if (std::abs(a[i]-b[i])>1e-4f) return false;
    return true;
}
EntityRecord entity(EntityId id, const char* name, EntityId parent, Mat4 local) {
    EntityRecord result;
    result.id=id; result.name=name; result.parent=parent; result.local=local;
    return result;
}
void run() {
    Scene scene;
    const auto rotation = math::rotation(math::axisAngle({0,1,0},std::numbers::pi/2));
    auto parent=entity(1,"Parent",0,math::translation({10,2,3})*rotation);
    auto model=entity(2,"Nested model",1,math::translation({2,0,0})*math::scaling({-2,3,4}));
    model.model=AssetRef<ModelAsset>{"assets/models/first/manifest.json"};
    auto other=entity(3,"Repeated model",0,math::translation({-5,0,0}));
    other.model=model.model;
    auto missing=entity(4,"Missing model",0,math::translation({7,0,0}));
    missing.model=AssetRef<ModelAsset>{"assets/models/missing/manifest.json"};
    Mat4 shear;
    shear[4]=.8f; shear[8]=.2f; shear[9]=.7f;
    auto sun=entity(5,"Sun",1,shear*math::scaling({2,3,4}));
    sun.directional=SceneDirectionalLight{{.2f,.4f,.8f},7,false};
    auto point=entity(6,"Point",1,math::translation({0,4,1})*math::scaling({10,20,30}));
    point.point=ScenePointLight{{.8f,.3f,.1f},23,true};
    scene.apply({point,sun,missing,other,model,parent},{});
    const auto extracted=extractScene(scene);
    require(extracted.models.size()==3,"Extraction dropped a model or unresolved model reference.");
    require(extracted.models[0].id==2 && extracted.models[1].id==3 && extracted.models[2].id==4,
            "Extraction ordering must be deterministic across registry allocation order.");
    require(extracted.models[0].assetId==extracted.models[1].assetId,"Repeated models lost their shared asset identity.");
    require(close(extracted.models[0].world,parent.local*model.local),"Nested model did not accumulate its parent transform.");
    require(extracted.models[2].assetId==missing.model->id,"An unresolved asset reference was discarded.");
    require(extracted.directional.size()==1 && extracted.points.size()==1,"Light components were not extracted.");
    require(close(extracted.directional[0].direction,math::normalized({-1,-.7f,.2f})),
            "Directional light discarded its authored forward axis under affine parenting.");
    require(!extracted.directional[0].enabled && extracted.directional[0].intensity==7 && extracted.directional[0].color==sun.directional->color,
            "Directional light properties were altered during extraction.");
    require(close(extracted.points[0].position,math::transformPoint(parent.local*point.local,{0,0,0})),"Point light ignored its parent transform.");
    require(extracted.points[0].intensity==23 && extracted.points[0].enabled,"Point transform scale changed its physical properties.");

    const ModelBoundsQuery query=[&](const std::string& id)->std::optional<Aabb> {
        if (id==model.model->id) return Aabb{{-1,-1,-1},{1,1,1},true};
        return {};
    };
    const std::array<EntityId,1> modelSelection{2};
    const auto bounds=selectionBounds(scene,modelSelection,query);
    const auto expected=math::transformBounds({{-1,-1,-1},{1,1,1},true},parent.local*model.local);
    require(bounds.valid && close(bounds.minimum,expected.minimum) && close(bounds.maximum,expected.maximum),
            "Selection bounds did not retain mirrored/nonuniform nested model transforms.");
    std::size_t queried=0;
    const std::array<EntityId,3> overlapSelection{1,2,999};
    const auto parentBounds=selectionBounds(scene,overlapSelection,[&](const std::string& id) { ++queried; return query(id); });
    require(parentBounds.valid && queried==1,"Parent-plus-child selection traversed the selected model twice.");
    require(parentBounds.minimum[0]<=9.5f && parentBounds.maximum[1]>=6.5f,"Parent selection omitted descendant lights or parent position.");
    const std::array<EntityId,1> missingSelection{4};
    const auto missingBounds=selectionBounds(scene,missingSelection,query);
    require(close(missingBounds.minimum,{6.5f,-.5f,-.5f}) && close(missingBounds.maximum,{7.5f,.5f,.5f}),
            "Missing models must remain frameable at their saved transform.");
    require(!selectionBounds(scene,std::span<const EntityId>{},query).valid,"Empty selection produced invented scene bounds.");
    Scene empty;
    const auto emptyRender=extractScene(empty);
    require(emptyRender.models.empty() && emptyRender.directional.empty() && emptyRender.points.empty(),"Empty scene gained legacy content.");
}
void lightOrientation() {
    const double quarter=std::numbers::pi/2;
    const std::array<std::pair<Vec3,Vec3>,3> axes{{
        {{1,0,0},{0,1,0}}, {{0,1,0},{-1,0,0}}, {{0,0,1},{0,0,-1}}
    }};
    require(close(directionalLightDirection(Mat4{}),{0,0,-1}),"Identity light changed its local -Z convention.");
    for (const auto& [axis,expected]:axes) {
        const auto transform=math::translation({17,-9,3})*math::rotation(math::axisAngle(axis,quarter));
        require(close(directionalLightDirection(transform),expected),"Light did not follow a known positive-axis rotation.");
    }
    // A reconstructed cross(X,Y) axis reverses this authored reflection.
    require(close(directionalLightDirection(math::scaling({2,3,-4})),{0,0,1}),
            "A mirrored local forward axis was silently reversed.");
    Mat4 shear;
    shear[8]=.25f; shear[9]=-.75f;
    require(close(directionalLightDirection(shear),math::normalized({-.25f,.75f,-1})),
            "Light direction ignored shear in the authored forward column.");
    require(close(directionalLightDirection(shear*math::scaling({2,3,7})),directionalLightDirection(shear)),
            "Positive axis scale changed the light direction.");

    // Parent stretch followed by child rotation is not an orthogonal world
    // basis: the old Gram-Schmidt cross(X,Y) pointed along a different axis.
    const auto parent=math::rotation(math::axisAngle({0,1,0},quarter))*math::scaling({2,.5f,3});
    const auto local=math::rotation(math::axisAngle({1,0,0},std::numbers::pi/4));
    const float root37=std::sqrt(37.0f);
    const Vec3 expected{-6/root37,1/root37,0};
    require(close(directionalLightDirection(parent*local),expected),
            "Parent nonuniform scale disconnected the light from its transformed local axis.");
    Scene scene;
    auto group=entity(1,"Nonuniform parent",0,parent);
    auto light=entity(2,"Rotated child light",1,local); light.directional.emplace();
    scene.apply({group,light},{});
    const auto extracted=extractScene(scene);
    require(extracted.directional.size()==1 && close(extracted.directional.front().direction,expected),
            "Renderer extraction and the viewport light-direction helper diverged.");
    require(close(directionalLightDirection(math::scaling({1,1,0})),{0,0,-1}),
            "Degenerate light axis did not use its finite fallback.");
}
void gizmoAffineRotation() {
    Mat4 shear; shear[4]=.4f; shear[8]=-.2f; shear[9]=.7f;
    const Mat4 parent=math::translation({7,3,-5})*math::rotation(math::axisAngle({0,1,0},.7))*math::scaling({-2,.5f,3});
    const Mat4 local=math::translation({1,4,-2})*math::rotation(math::axisAngle({1,0,0},.4))*shear;
    const Mat4 originalWorld=parent*local;
    const auto parentInverse=math::inverse(parent);
    require(parentInverse.has_value(),"Regression parent must be invertible.");
    const Vec3 pivot=math::transformPoint(originalWorld,{0,0,0});
    const Vec3 originalDirection=directionalLightDirection(originalWorld);
    const std::array<Mat4,2> proxies{math::translation(pivot),math::orthonormalFrame(originalWorld)};
    for (const auto& startProxy:proxies) {
        const auto inverseProxy=math::inverse(startProxy);
        require(inverseProxy.has_value(),"Gizmo proxy must be invertible.");
        Mat4 resultLocal;
        Mat4 finalDelta;
        for (int frame=1;frame<=20;++frame) {
            const Mat4 rotatedProxy=startProxy*math::rotation(math::axisAngle({1,0,0},frame*std::numbers::pi/40));
            finalDelta=rotatedProxy* *inverseProxy;
            // Every preview is relative to frozen drag-start world values,
            // exactly as the editor applies ImGuizmo's proxy result.
            resultLocal=*parentInverse*finalDelta*originalWorld;
        }
        const Mat4 resultWorld=parent*resultLocal;
        require(close(resultWorld,finalDelta*originalWorld),"Gizmo world delta was applied in parent/local space.");
        require(close(math::transformPoint(resultWorld,{0,0,0}),pivot),"Rotating a light moved its pivot.");
        require(close(directionalLightDirection(resultWorld),math::normalized(math::transformDirection(finalDelta,originalDirection))),
                "Light emission did not follow the gizmo rotation delta.");
        require(math::handedness(resultWorld)==math::handedness(originalWorld),"Gizmo rotation discarded reflection.");
        const auto inverseDelta=math::inverse(finalDelta);
        require(inverseDelta && close(*inverseDelta*resultWorld,originalWorld),"Gizmo rotation discarded affine scale/shear.");
    }
}
void particleExtraction() {
    Scene scene;
    auto parent=entity(1,"Parent",0,math::translation({4,2,-3})*math::scaling({-2,3,.5f}));
    Mat4 shear;shear[4]=.7f;shear[8]=-.2f;
    auto particle=entity(7,"Particle",1,math::translation({1,2,3})*shear);
    particle.particle=AssetRef<ParticleEffectAsset>{"assets/particles/shared.crimson-particle"};
    auto missing=entity(9,"Unresolved",0,math::translation({-5,0,4}));
    missing.particle=AssetRef<ParticleEffectAsset>{"assets/particles/missing.crimson-particle"};
    auto cleared=entity(8,"Cleared",0,math::translation({7,1,2}));cleared.particle=AssetRef<ParticleEffectAsset>{};
    scene.apply({missing,particle,parent,cleared},{});
    const auto extracted=extractScene(scene);
    require(extracted.particles.size()==3 && extracted.particles[0].id==7 && extracted.particles[1].id==8 && extracted.particles[2].id==9,
            "Particle extraction omitted cleared/missing markers or lost deterministic ordering.");
    require(close(extracted.particles[0].world,parent.local*particle.local),"Particle extraction lost affine parent transforms.");
    require(extracted.particles[2].assetId==missing.particle->id && extracted.particles[1].assetId.empty(),"Particle extraction resolved or discarded authored identities.");
    const std::array<EntityId,1> selection{9};const auto bounds=selectionBounds(scene,selection,{});
    require(bounds.valid && close(bounds.minimum,{-5.5f,-.5f,3.5f}),"Missing effect marker could not be focused.");
    const std::array<EntityId,2> nestedSelection{1,7};size_t queried{};
    const Aabb live{{-8,-2,-5},{13,8,10},true};
    const auto liveBounds=selectionBounds(scene,nestedSelection,{},[&](EntityId id)->std::optional<Aabb> {++queried;return id==7?std::optional<Aabb>{live}:std::nullopt;});
    require(queried==1 && liveBounds.valid && close(liveBounds.minimum,live.minimum) && close(liveBounds.maximum,live.maximum),
            "Particle focus duplicated selected descendants or transformed world-space live bounds again.");
    const auto fallback=selectionBounds(scene,selection,{},[](EntityId)->std::optional<Aabb>{return Aabb{};});
    require(close(fallback.minimum,bounds.minimum) && close(fallback.maximum,bounds.maximum),"Invalid live bounds removed the authored marker fallback.");
    auto copied=extracted;particle.local=math::translation({100,0,0});scene.apply({particle,parent},{});
    require(close(copied.particles[0].world,extracted.particles[0].world),"An extracted particle frame retained mutable scene storage.");
}
}
int main() {
    try { run(); lightOrientation(); gizmoAffineRotation();particleExtraction();std::cout<<"Scene extraction checks passed.\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
